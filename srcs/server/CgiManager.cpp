#include "server/CgiManager.hpp"
#include "cgi/CGIHandler.hpp"
#include "cgi/CgiJob.hpp"
#include "server/Server.hpp"
#include "server/Connection.hpp"
#include "server/EpollWrapper.hpp"
#include "config/ConfigMatcher.hpp"
#include "http/RequestContext.hpp"
#include "http/utils_http.hpp"
#include "utils/Logger.hpp"
#include <sys/epoll.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <ctime>

CgiManager::CgiManager(Server& server, EpollWrapper& epoll)
    : _server(server), _epoll(epoll) {}

// Builds the job and launches the CGI child if this target maps to an interpreter
// (the reply is then produced asynchronously by the pipe callbacks). Returns false
// for a non-CGI target so the caller builds a static response. Method gating is the
// caller's job.
bool CgiManager::dispatch(Connection* conn, const RequestContext& ctx,
                          const RouteMatch& match) {
    CgiJob job;
    if (!CgiHandler::buildJob(ctx, *match.server, *match.location, job))
        return false;
    conn->cgi_job = job;
    start(conn);
    return true;
}

// Forks the CGI child (CgiHandler::execute sets the fds/pid and state) and rewires
// epoll: park the client socket and register both pipes. On failure, tear down and
// send 500.
void CgiManager::start(Connection* conn) {
    if (!CgiHandler::execute(conn, conn->cgi_job)) {
        Logger::log(Logger::ERROR, "CGI execute failed");
        _server._sendError(conn, 500);
        return;
    }

    conn->cgi_sent_idx = 0;
    // Clear stdout left over from a prior CGI run on this keep-alive connection;
    // otherwise onPipeIN appends to it and the previous response leaks into this one.
    conn->cgi_raw_output.clear();
    conn->last_activity = std::time(NULL);  // baseline for the CGI inactivity timer

    try {
        // Park the client socket (EPOLLRDHUP only) until the CGI reply is ready.
        _epoll.modifyFd(conn->getFd(), EPOLLRDHUP, conn);

        // Register both pipes up front: watching stdout immediately avoids a deadlock
        // where the child blocks writing a large reply while we still feed its stdin.
        conn->cgi_in_wrapper = new CgiPipeWrapper(conn, false);
        _epoll.addFd(conn->cgi_in_fd, EPOLLOUT, conn->cgi_in_wrapper);

        conn->cgi_out_wrapper = new CgiPipeWrapper(conn, true);
        _epoll.addFd(conn->cgi_out_fd, EPOLLIN, conn->cgi_out_wrapper);
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, std::string("CGI epoll setup failed: ") + e.what());
        teardown(conn);
        _server._sendError(conn, 500);
        return;
    }

    Logger::log(Logger::INFO, "CGI started pid=" + to_string(conn->cgi_pid) +
                              " (" + conn->cgi_job.interpreter + " " +
                              conn->cgi_job.script_path + ")");
}

// Closes both CGI pipes, reaps the child, and frees the wrappers. Idempotent. The
// Connection dtor does this too as a safety net, but here we pull the fds out of
// epoll while the wrappers are still alive. Must run before the Connection is freed.
void CgiManager::teardown(Connection* conn) {
    closeInPipe(conn);
    closePipe(conn->cgi_out_fd, conn->cgi_out_wrapper, "CGI out-pipe epoll remove");
    if (conn->cgi_pid > 0) {
        kill(conn->cgi_pid, SIGKILL);
        waitpid(conn->cgi_pid, NULL, 0);
        conn->cgi_pid = -1;
    }
}

// Closes the CGI stdin pipe so the child sees EOF. Safe to call repeatedly.
void CgiManager::closeInPipe(Connection* conn) {
    closePipe(conn->cgi_in_fd, conn->cgi_in_wrapper, "CGI in-pipe epoll remove");
}

// Removes one pipe fd from epoll, closes it, and frees its wrapper, resetting both
// to inactive sentinels (fd=-1, wrapper=NULL). Guarded so repeated calls are no-ops.
void CgiManager::closePipe(int& fd, CgiPipeWrapper*& wrapper, const char* label) {
    if (fd != -1) {
        try { _epoll.removeFd(fd); }
        catch (const std::exception& e) {
            Logger::log(Logger::WARNING, std::string(label) + ": " + e.what());
        }
        close(fd);
        fd = -1;
    }
    if (wrapper != NULL) {
        // Retire before freeing: a sibling fd / the client socket may still have an
        // event queued this batch; run() skips it via _retired (avoids use-after-free).
        _server._retireHandler(wrapper);
        delete wrapper;
        wrapper = NULL;
    }
}

// CGI stdout pipe readable: read once into cgi_raw_output (level-triggered epoll
// re-fires for the rest). read==0 is EOF -> finalize; read<0 is an error -> 502.
void CgiManager::onPipeIN(Connection* conn) {
    char buf[65536];

    ssize_t n = read(conn->cgi_out_fd, buf, sizeof(buf));
    if (n > 0) {
        conn->cgi_raw_output.insert(conn->cgi_raw_output.end(), buf, buf + n);
        conn->last_activity = std::time(NULL);  // progress: reset inactivity timer
        return;
    }
    if (n == 0) {                       // EOF: response complete
        finalize(conn, false);
        return;
    }
    finalize(conn, true);               // read error -> 502
}

// Reaps the child, tears down the pipes, and turns the collected output into the
// client response. Empty output, a read error, or a missing header block -> 502.
void CgiManager::finalize(Connection* conn, bool readError) {
    // Reap without blocking: if stdout closed but the child hasn't exited, kill+reap.
    if (conn->cgi_pid > 0) {
        int status = 0;
        if (waitpid(conn->cgi_pid, &status, WNOHANG) == 0) {
            kill(conn->cgi_pid, SIGKILL);
            waitpid(conn->cgi_pid, &status, 0);
        }
        conn->cgi_pid = -1;
    }

    teardown(conn);  // release the pipes/wrappers (child already reaped above)

    std::string response;
    if (!readError)
        response = buildResponse(conn);  // empty string if output is malformed

    if (response.empty()) {
        Logger::log(Logger::WARNING, "CGI produced no valid response -> 502");
        _server._sendError(conn, 502);
        return;
    }

    _server._queueResponse(conn, response);
    Logger::log(Logger::DEBUG, "CGI response ready (" + to_string(response.size()) + " bytes)");
}

// Translates raw CGI output (header block + body) into a complete HTTP/1.1 response:
// honors a "Status:" header, passes other headers through (dropping the length/
// connection ones we control), and appends our own Content-Length + Connection.
// Returns "" if there is no header/body separator so the caller emits 502.
std::string CgiManager::buildResponse(Connection* conn) const {
    std::string raw(conn->cgi_raw_output.begin(), conn->cgi_raw_output.end());

    // Locate the header/body separator: prefer CRLFCRLF, accept bare LFLF.
    size_t sep = std::string::npos;
    size_t sep_len = 0;
    size_t crlf = raw.find("\r\n\r\n");
    size_t lf = raw.find("\n\n");
    if (crlf != std::string::npos && (lf == std::string::npos || crlf <= lf)) {
        sep = crlf;
        sep_len = 4;
    } else if (lf != std::string::npos) {
        sep = lf;
        sep_len = 2;
    } else {
        return "";  // no header block: malformed CGI output
    }

    std::string header_block = raw.substr(0, sep);
    std::string body = raw.substr(sep + sep_len);

    std::string status_line = "HTTP/1.1 200 OK";
    std::string passthrough;

    // Walk the header lines (split on LF, tolerate trailing CR).
    size_t pos = 0;
    while (pos < header_block.size()) {
        size_t eol = header_block.find('\n', pos);
        std::string line = (eol == std::string::npos)
                               ? header_block.substr(pos)
                               : header_block.substr(pos, eol - pos);
        pos = (eol == std::string::npos) ? header_block.size() : eol + 1;
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        if (line.empty())
            continue;

        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string key = line.substr(0, colon);
        std::string lkey = to_lower_ascii(key);

        if (lkey == "status") {
            // "Status: 302 Found" -> "HTTP/1.1 302 Found"
            std::string val = line.substr(colon + 1);
            size_t s = val.find_first_not_of(" \t");
            if (s != std::string::npos)
                status_line = "HTTP/1.1 " + val.substr(s);
            continue;
        }
        // We set these ourselves; drop any the script provided.
        if (lkey == "content-length" || lkey == "connection" ||
            lkey == "transfer-encoding")
            continue;

        passthrough += line;
        passthrough += "\r\n";
    }

    std::string out = status_line + "\r\n";
    out += passthrough;
    out += "Content-Length: " + to_string(body.size()) + "\r\n";
    out += conn->keep_alive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
    out += body;
    return out;
}

// True while the connection is parked in a CGI state, so the timeout sweep treats
// it as a CGI exchange rather than an ordinary idle client.
bool CgiManager::isActive(Connection* conn) const {
    return conn->state == Connection::CGI_WRITING_BODY ||
           conn->state == Connection::CGI_READING_OUTPUT;
}

// A live child is timed out once it makes no I/O progress for CGI_INACTIVITY or runs
// past the absolute CGI_HARD cap. False when no child is running.
bool CgiManager::isExpired(Connection* conn, std::time_t now) const {
    const std::time_t CGI_INACTIVITY = 5;   // no stdin/stdout progress
    const std::time_t CGI_HARD       = 60;  // absolute wall-clock cap

    if (conn->cgi_pid <= 0)
        return false;
    return (now - conn->cgi_start_time) >= CGI_HARD ||
           (now - conn->last_activity)  >= CGI_INACTIVITY;
}

// Aborts a timed-out CGI: tears down the child/pipes, then queues 504.
void CgiManager::timeout(Connection* conn) {
    int client_fd = conn->getFd();
    Logger::log(Logger::WARNING, "CGI timeout (pid=" + to_string(conn->cgi_pid) +
                                 ") on fd=" + to_string(client_fd) + " -> 504");
    teardown(conn);
    _server._sendError(conn, 504);
}

// CGI stdin pipe writable: stream the request body to the child, resuming on the
// next EPOLLOUT. Once the body is fully written (or the child closed stdin), close
// stdin so the child sees EOF and move to CGI_READING_OUTPUT; stdout is already
// watched (see start), so onPipeIN collects the reply independently.
void CgiManager::onPipeOUT(Connection* conn) {
    const std::vector<char>& body = conn->cgi_job.body;

    if (conn->cgi_sent_idx < body.size()) {
        size_t remaining = body.size() - conn->cgi_sent_idx;
        ssize_t n = write(conn->cgi_in_fd, &body[conn->cgi_sent_idx], remaining);
        if (n < 0) {
            // errno must not be inspected after a write, so treat any negative return
            // as the child having closed/exited (EPIPE): stop feeding stdin.
            closeInPipe(conn);
            conn->setState(Connection::CGI_READING_OUTPUT);
            return;
        }
        conn->cgi_sent_idx += static_cast<size_t>(n);
        conn->last_activity = std::time(NULL);  // progress: reset inactivity timer
    }

    if (conn->cgi_sent_idx >= body.size()) {
        Logger::log(Logger::DEBUG, "CGI request body fully written (" +
                                   to_string(body.size()) + " bytes)");
        closeInPipe(conn);
        conn->setState(Connection::CGI_READING_OUTPUT);
    }
}
