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

/*
 * If the matched location maps this target's extension to an interpreter, build
 * the job and launch the CGI child (the response is then produced asynchronously
 * by the pipe callbacks). Returns false when this is not a CGI request, so the
 * caller falls through to the static ResponseBuilder. The method-allowed gate is
 * applied by the caller before this is reached.
 */
bool CgiManager::dispatch(Connection* conn, const RequestContext& ctx,
                          const RouteMatch& match) {
    CgiJob job;
    if (!CgiHandler::buildJob(ctx, *match.server, *match.location, job))
        return false;
    conn->cgi_job = job;
    start(conn);
    return true;
}

/*
 * Forks the CGI child (CgiHandler::execute sets conn->cgi_in_fd / cgi_out_fd /
 * cgi_pid and moves the connection into CGI_WRITING_BODY), then rewires epoll:
 * the client socket is parked (kept only for EPOLLRDHUP so we notice a hangup)
 * and the CGI stdin pipe is registered for EPOLLOUT so onPipeOUT can stream
 * the request body. On any failure the CGI state is torn down and a 500 is sent.
 */
void CgiManager::start(Connection* conn) {
    if (!CgiHandler::execute(conn, conn->cgi_job)) {
        Logger::log(Logger::ERROR, "CGI execute failed");
        _server._sendError(conn, 500);
        return;
    }

    conn->cgi_sent_idx = 0;
    conn->last_activity = std::time(NULL);  // baseline for the CGI inactivity timer

    try {
        // Park the client socket: no more EPOLLIN/EPOLLOUT until the CGI reply
        // is ready, but keep EPOLLRDHUP so a client disconnect is detected.
        _epoll.modifyFd(conn->getFd(), EPOLLRDHUP, conn);

        // Register both pipes up front. Watching stdout from the start avoids a
        // deadlock where the child blocks writing a large response while we are
        // still feeding its stdin.
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

/*
 * Removes and closes the CGI pipe fds, reaps the child, and frees the pipe
 * wrappers. Idempotent: each resource is guarded so repeated calls are no-ops.
 * The Connection destructor also closes/reaps as a safety net, but doing it here
 * lets us pull the fds out of epoll while the wrappers are still alive.
 */
void CgiManager::teardown(Connection* conn) {
    closeInPipe(conn);  // stdin pipe fd + its wrapper (shared with the EOF path)
    closePipe(conn->cgi_out_fd, conn->cgi_out_wrapper, "CGI out-pipe epoll remove");
    if (conn->cgi_pid > 0) {
        kill(conn->cgi_pid, SIGKILL);
        waitpid(conn->cgi_pid, NULL, 0);
        conn->cgi_pid = -1;
    }
}

/*
 * Closes the CGI stdin pipe (write end) and frees its wrapper, so the child
 * sees EOF on stdin. Removes it from epoll first. Safe to call repeatedly.
 */
void CgiManager::closeInPipe(Connection* conn) {
    closePipe(conn->cgi_in_fd, conn->cgi_in_wrapper, "CGI in-pipe epoll remove");
}

/*
 * Removes one CGI pipe fd from epoll, closes it, and frees its wrapper, resetting
 * both to their inactive sentinels (fd = -1, wrapper = NULL). Each step is guarded
 * so repeated calls are no-ops. Shared by closeInPipe and teardown.
 */
void CgiManager::closePipe(int& fd, CgiPipeWrapper*& wrapper, const char* label) {
    if (fd != -1) {
        try { _epoll.removeFd(fd); }
        catch (const std::exception& e) {
            Logger::log(Logger::WARNING, std::string(label) + ": " + e.what());
        }
        close(fd);
        fd = -1;
    }
    delete wrapper;
    wrapper = NULL;
}

/*
 * CGI stdout pipe is readable: read once into conn->cgi_raw_output. epoll is
 * level-triggered, so if more data remains EPOLLIN fires again on the next loop
 * iteration. EOF (read == 0) means the child finished writing its response, so we
 * finalize. A negative return is a read error and finalizes as 502 (errno must
 * not be inspected after a read).
 */
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

/*
 * Reaps the CGI child, tears down its pipes, and turns the collected output into
 * the client response. Empty output, a read error, or output without a valid
 * header block becomes 502 Bad Gateway.
 */
void CgiManager::finalize(Connection* conn, bool readError) {
    // Reap the child without blocking the event loop. If it closed stdout but
    // hasn't exited, don't wait on it — kill it and reap (SIGKILL reaps fast).
    if (conn->cgi_pid > 0) {
        int status = 0;
        if (waitpid(conn->cgi_pid, &status, WNOHANG) == 0) {
            kill(conn->cgi_pid, SIGKILL);
            waitpid(conn->cgi_pid, &status, 0);
        }
        conn->cgi_pid = -1;
    }

    // Release the pipes/wrappers (cgi_pid already reaped above).
    teardown(conn);

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

/*
 * Translates raw CGI output into a complete HTTP/1.1 response.
 *
 * CGI scripts emit a header block (terminated by a blank line) followed by the
 * body. We honor a "Status:" header for the status line, pass the remaining
 * headers through (dropping hop-by-hop / length headers we control), and append
 * a computed Content-Length plus the Connection header per conn->keep_alive.
 * Returns "" if there is no header/body separator so the caller can emit 502.
 */
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

/*
 * True while the connection is parked in a CGI state, regardless of child
 * liveness, so the timeout sweep treats it as a CGI exchange (and not as an
 * ordinary idle client).
 */
bool CgiManager::isActive(Connection* conn) const {
    return conn->state == Connection::CGI_WRITING_BODY ||
           conn->state == Connection::CGI_READING_OUTPUT;
}

/*
 * A live CGI child is considered timed out once it has either made no I/O
 * progress for CGI_INACTIVITY seconds or run past the absolute CGI_HARD cap.
 * Returns false when no child is running (cgi_pid <= 0).
 */
bool CgiManager::isExpired(Connection* conn, std::time_t now) const {
    const std::time_t CGI_INACTIVITY = 5;   // no stdin/stdout progress
    const std::time_t CGI_HARD       = 60;  // absolute wall-clock cap

    if (conn->cgi_pid <= 0)
        return false;
    return (now - conn->cgi_start_time) >= CGI_HARD ||
           (now - conn->last_activity)  >= CGI_INACTIVITY;
}

/*
 * Aborts a timed-out CGI: kills + reaps the child and frees its pipes (via
 * teardown), then queues 504 Gateway Timeout on the client socket.
 */
void CgiManager::timeout(Connection* conn) {
    int client_fd = conn->getFd();
    Logger::log(Logger::WARNING, "CGI timeout (pid=" + to_string(conn->cgi_pid) +
                                 ") on fd=" + to_string(client_fd) + " -> 504");
    teardown(conn);
    _server._sendError(conn, 504);
}

/*
 * CGI stdin pipe is writable: stream the request body to the child.
 * Writes as much as the pipe accepts; resumes on the next EPOLLOUT (EAGAIN).
 * When the whole body is written — or the child closed its stdin (EPIPE) — the
 * stdin pipe is closed so the CGI sees EOF, and the connection moves to
 * CGI_READING_OUTPUT. The stdout pipe is already registered (see start), so
 * onPipeIN collects the reply independently.
 */
void CgiManager::onPipeOUT(Connection* conn) {
    const std::vector<char>& body = conn->cgi_job.body;

    if (conn->cgi_sent_idx < body.size()) {
        size_t remaining = body.size() - conn->cgi_sent_idx;
        ssize_t n = write(conn->cgi_in_fd, &body[conn->cgi_sent_idx], remaining);
        if (n < 0) {
            // Written only in response to EPOLLOUT. errno must not be inspected
            // after a write, so treat any negative return as the child having
            // closed/exited (EPIPE): stop feeding stdin and move to reading its
            // output. The stdin pipe is closed so the child sees EOF.
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
