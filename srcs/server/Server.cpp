#include "server/Server.hpp"
#include "server/Connection.hpp"
#include "utils/Logger.hpp"
#include "http/Request.hpp"
#include "http/RequestContext.hpp"
#include "config/ConfigMatcher.hpp"
#include "config/RouteContext.hpp"
#include "http/Headers.hpp"
#include "http/utils_http.hpp"
#include "http/ResponseBuilder.hpp"
#include "http/Response.hpp"
#include "http/HttpStatus.hpp"
#include "cgi/CGIHandler.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <set>

/*
 * Constructor: stores reference to configs, initializes members.
 */
Server::Server(const std::vector<ServerConfig>& configs)
    : _configs(configs), _running(false), _last_timeout_check(std::time(NULL)) {}

/*
 * Destructor: closes all connections and listening sockets.
 */
Server::~Server() {
    // Delete all active connections
    for (std::map<int, Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it) {
        _epoll.removeFd(it->second->getFd());
        delete it->second;
    }
    _connections.clear();

    // Delete all listeners
    for (size_t i = 0; i < _listeners.size(); ++i) {
        _epoll.removeFd(_listeners[i]->getFd());
        delete _listeners[i];
    }
    _listeners.clear();
}

/*
 * Initializes listening sockets, one per unique host:port pair.
 * Multiple server blocks on the same address share one socket; ConfigMatcher
 * handles virtual-host selection per request via the Host header.
 */
void Server::_initListeners() {
    std::set<std::string> seen;
    for (size_t i = 0; i < _configs.size(); ++i) {
        const ServerConfig& cfg = _configs[i];
        std::string key = cfg.host + ":" + to_string(cfg.port);
        if (!seen.insert(key).second)
            continue;

        Socket* listener = new Socket(cfg.host, to_string(cfg.port));
        try {
            listener->setup();
        } catch (const std::exception& e) {
            Logger::log(Logger::ERROR, std::string("Failed to setup listening socket: ") + e.what());
            delete listener;
            throw;
        }

        _epoll.addFd(listener->getFd(), EPOLLIN, listener);
        _listenerPort[listener->getFd()] = cfg.port;
        _listeners.push_back(listener);

        Logger::log(Logger::INFO,
            "Listening on " + key + " (fd=" + to_string(listener->getFd()) + ")");
    }
}

/*
 * Main event loop: waits for events and dispatches them.
 */
void Server::run() {
    _initListeners();
    _running = true;

    Logger::log(Logger::INFO, "Server event loop starting...");

    while (_running) {
        int nfds = _epoll.wait(1000); // 1s timeout

        if (nfds == -1) {
            Logger::log(Logger::ERROR, "epoll_wait failed");
            break;
        }

        // Enforce timeouts on every wake-up (throttled to ~1Hz internally). Runs
        // whether or not events fired, so a stuck CGI is reaped under load too.
        _checkTimeouts();

        if (nfds == 0)
            continue;

        // Process all ready events
        const struct epoll_event* events = _epoll.getEvents();
        if (!events) {
            continue;
        }

        for (int i = 0; i < nfds; ++i) {
            IEventHandler* handler = static_cast<IEventHandler*>(events[i].data.ptr);
            if (!handler) {
                Logger::log(Logger::WARNING, "epoll returned null handler");
                continue;
            }

            try {
                handler->handle(events[i].events, *this);
            } catch (const std::exception& e) {
                Logger::log(Logger::ERROR, std::string("Exception in event handler: ") + e.what());
            }
        }
    }

    Logger::log(Logger::INFO, "Server event loop stopped");
}

/*
 * Called by Socket when a new client connects.
 */
void Server::onNewConnection(int client_fd, int listener_fd) {
    if (client_fd < 0) {
        Logger::log(Logger::WARNING, "Received invalid client fd: " + to_string(client_fd));
        return;
    }

    // Resolve port from listener fd, then find first config on that port.
    // server_cfg is only used for its .port field; ConfigMatcher selects the
    // real server block per-request via the Host header.
    std::map<int, int>::iterator pit = _listenerPort.find(listener_fd);
    if (pit == _listenerPort.end()) {
        Logger::log(Logger::ERROR, "Could not find port for listener fd: " + to_string(listener_fd));
        close(client_fd);
        return;
    }
    int port = pit->second;

    const ServerConfig* cfg = NULL;
    for (size_t i = 0; i < _configs.size(); ++i) {
        if (_configs[i].port == port) {
            cfg = &_configs[i];
            break;
        }
    }

    if (!cfg) {
        Logger::log(Logger::ERROR, "No config found for port: " + to_string(port));
        close(client_fd);
        return;
    }

    // Create new Connection object
    Connection* conn = NULL;
    try {
        conn = new Connection(client_fd, cfg);
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, std::string("Failed to create Connection: ") + e.what());
        close(client_fd);
        return;
    }

    // Register with epoll for reading
    try {
        // watch for EPOLLIN (data to read) and EPOLLRDHUP (client closed its write side)
        _epoll.addFd(client_fd, EPOLLIN | EPOLLRDHUP, conn);
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, std::string("Failed to add client fd to epoll: ") + e.what());
        delete conn;
        return;
    }

    _connections[client_fd] = conn;
    Logger::log(Logger::INFO, "New connection accepted (fd=" + to_string(client_fd) + ")");
    Logger::log(Logger::INFO, "New connection accepted (fd=" + to_string(client_fd) + ")");
}

/*
 * Called when a client socket is readable.
 */
void Server::onClientIN(Connection* conn) {
    if (!conn) {
        Logger::log(Logger::ERROR, "onClientIN: conn is null");
        return;
    }

    int fd = conn->getFd();
    Logger::log(Logger::DEBUG, "onClientIN fd=" + to_string(fd));

    char buf[4096];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);

    if (n == 0) {
        Logger::log(Logger::INFO, "Client closed connection fd=" + to_string(fd));
        conn->setState(Connection::CLOSED);
        return;
    }
    if (n < 0) {
        // We only read after epoll signals readiness and read once per event
        // (epoll is level-triggered), so a negative return is a real error:
        // close the connection. errno must not be inspected after a read.
        conn->setState(Connection::CLOSED);
        return;
    }

    conn->in_buffer.insert(conn->in_buffer.end(), buf, buf + n);
    conn->last_activity = std::time(NULL);  // progress: reset the idle timer
    Logger::log(Logger::DEBUG, "Read " + to_string(n) + " bytes, in_buffer now " +
                to_string(conn->in_buffer.size()) + " bytes");

    std::string buffer_str(conn->in_buffer.begin(), conn->in_buffer.end());
    size_t header_end = buffer_str.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        Logger::log(Logger::DEBUG, "Incomplete request, waiting for more data");
        return;
    }

    size_t body_start = header_end + 4;
    size_t body_received = buffer_str.size() - body_start;

    // Use Headers to check body completeness: case-insensitive, reuses validated logic.
    Headers temp_headers;
    try {
        std::string head = buffer_str.substr(0, header_end);
        size_t rl_end = head.find("\r\n");
        std::string headers_only = (rl_end != std::string::npos) ? head.substr(rl_end + 2) : "";
        size_t pos = 0;
        while (pos < headers_only.size()) {
            size_t eol = headers_only.find("\r\n", pos);
            std::string line = (eol == std::string::npos)
                ? headers_only.substr(pos)
                : headers_only.substr(pos, eol - pos);
            pos = (eol == std::string::npos) ? headers_only.size() : eol + 2;
            if (!line.empty())
                temp_headers.parseLine(line);
        }
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("Header parse error: ") + e.what());
        conn->in_buffer.clear();
        _sendError(conn, fd, status_from_exception(e.what()));
        return;
    }

    if (temp_headers.hasHeader("transfer-encoding")) {
        // Look for chunked terminal "0\r\n\r\n" starting from the body section.
        if (buffer_str.find("0\r\n\r\n", body_start) == std::string::npos) {
            Logger::log(Logger::DEBUG, "Chunked body incomplete, waiting for more data");
            return;
        }
    } else {
        size_t content_length = 0;
        try {
            content_length = temp_headers.getContentLength();
        } catch (const std::exception& e) {
            Logger::log(Logger::WARNING, std::string("Content-Length error: ") + e.what());
            conn->in_buffer.clear();
            _sendError(conn, fd, status_from_exception(e.what()));
            return;
        }
        // Reject an over-large body before buffering it: if Content-Length
        // exceeds the largest client_max_body_size of any route on this port, no
        // route could accept it, so answer 413 now instead of reading the whole
        // body into memory (the precise per-route limit is still enforced later
        // in ResponseBuilder for bodies within this maximum).
        size_t limit = _maxBodyLimitForPort(conn->server_cfg->port);
        if (limit != (size_t)-1 && content_length > limit) {
            Logger::log(Logger::WARNING, "Content-Length " + to_string(content_length) +
                        " exceeds max body size " + to_string(limit) + " -> 413");
            conn->in_buffer.clear();
            _sendError(conn, fd, 413);
            return;
        }

        if (body_received < content_length) {
            Logger::log(Logger::DEBUG, "Body incomplete: received " + to_string(body_received) +
                        " bytes, expecting " + to_string(content_length));
            return;
        }
    }

    // Full request buffered — parse it.
    Request req;
    try {
        req.parse(buffer_str);
        Logger::log(Logger::DEBUG, "Request parsed successfully");
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("Request parse error: ") + e.what());
        conn->in_buffer.clear();
        _sendError(conn, fd, status_from_exception(e.what()));
        return;
    }

    // Buffer is no longer needed once the Request object is built.
    conn->in_buffer.clear();

    // Build RequestContext using the port this connection arrived on.
    RequestContext ctx;
    try {
        ctx.build(req, conn->server_cfg->port);
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("RequestContext build error: ") + e.what());
        _sendError(conn, fd, status_from_exception(e.what()));
        return;
    }

    // Route: ConfigMatcher selects the matching server block and location block.
    ConfigMatcher matcher;
    RouteMatch routeMatch;
    routeMatch.server = NULL;
    routeMatch.location = NULL;
    try {
        routeMatch = matcher.match(ctx, _configs);
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("Routing error: ") + e.what());
        _sendError(conn, fd, status_from_exception(e.what()));
        return;
    }

    // CGI dispatch: if the matched location maps this target's extension to an
    // interpreter, launch the CGI child instead of building a static response.
    // The response is then produced asynchronously by the CGI pipe callbacks.
    if (routeMatch.server && routeMatch.location) {
        CgiJob job;
        if (CgiHandler::buildJob(ctx, *routeMatch.server, *routeMatch.location, job)) {
            conn->cgi_job = job;
            _startCgi(conn, fd);
            return;
        }
    }

    std::string response = _buildResponse(ctx, routeMatch);
    conn->out_buffer.assign(response.begin(), response.end());
    conn->write_idx = 0;
    conn->setState(Connection::SENDING_RESPONSE);
    _epoll.modifyFd(fd, EPOLLOUT | EPOLLRDHUP, conn);
}

/*
 * Called when a client socket is writable.
 * Sends buffered response data. Sets CLOSED when done; Connection::handle() calls
 * onCloseConnection after this returns.
 */
void Server::onClientOUT(Connection* conn) {
    if (!conn) {
        Logger::log(Logger::ERROR, "onClientOUT: conn is null");
        return;
    }

    int fd = conn->getFd();
    Logger::log(Logger::DEBUG, "onClientOUT fd=" + to_string(fd));

    if (conn->out_buffer.empty() || conn->write_idx >= conn->out_buffer.size()) {
        conn->setState(Connection::CLOSED);
        return;
    }

    size_t remaining = conn->out_buffer.size() - conn->write_idx;
    ssize_t n = send(fd, &conn->out_buffer[conn->write_idx], remaining, MSG_NOSIGNAL);

    if (n < 0) {
        // Sent only in response to EPOLLOUT; a negative return is a real error
        // (errno must not be inspected after a write), so close the connection.
        conn->setState(Connection::CLOSED);
        return;
    }

    conn->write_idx += static_cast<size_t>(n);
    conn->last_activity = std::time(NULL);  // progress: reset the idle timer

    if (conn->write_idx >= conn->out_buffer.size()) {
        Logger::log(Logger::DEBUG, "Response fully sent to fd=" + to_string(fd));
        conn->setState(Connection::CLOSED);
    }
}

/*
 * Called when a connection needs to be closed.
 */
void Server::onCloseConnection(int fd, Connection* conn) {
    Logger::log(Logger::DEBUG, "Closing connection fd=" + to_string(fd));

    // Release any CGI pipes/child first so epoll never holds a dangling wrapper.
    if (conn)
        _teardownCgi(conn);

    try {
        _epoll.removeFd(fd);
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("Error removing fd from epoll: ") + e.what());
    }

    std::map<int, Connection*>::iterator it = _connections.find(fd);
    if (it != _connections.end()) {
        delete it->second;
        _connections.erase(it);
    }
}

/*
 * Removes and closes the CGI pipe fds, reaps the child, and frees the pipe
 * wrappers. Idempotent: each resource is guarded so repeated calls are no-ops.
 * The Connection destructor also closes/reaps as a safety net, but doing it here
 * lets us pull the fds out of epoll while the wrappers are still alive.
 */
void Server::_teardownCgi(Connection* conn) {
    if (conn->cgi_in_fd != -1) {
        try { _epoll.removeFd(conn->cgi_in_fd); }
        catch (const std::exception& e) {
            Logger::log(Logger::WARNING, std::string("CGI in-pipe epoll remove: ") + e.what());
        }
        close(conn->cgi_in_fd);
        conn->cgi_in_fd = -1;
    }
    if (conn->cgi_out_fd != -1) {
        try { _epoll.removeFd(conn->cgi_out_fd); }
        catch (const std::exception& e) {
            Logger::log(Logger::WARNING, std::string("CGI out-pipe epoll remove: ") + e.what());
        }
        close(conn->cgi_out_fd);
        conn->cgi_out_fd = -1;
    }
    if (conn->cgi_pid > 0) {
        kill(conn->cgi_pid, SIGKILL);
        waitpid(conn->cgi_pid, NULL, 0);
        conn->cgi_pid = -1;
    }
    delete conn->cgi_in_wrapper;
    conn->cgi_in_wrapper = NULL;
    delete conn->cgi_out_wrapper;
    conn->cgi_out_wrapper = NULL;
}

/*
 * Forks the CGI child (CgiHandler::execute sets conn->cgi_in_fd / cgi_out_fd /
 * cgi_pid and moves the connection into CGI_WRITING_BODY), then rewires epoll:
 * the client socket is parked (kept only for EPOLLRDHUP so we notice a hangup)
 * and the CGI stdin pipe is registered for EPOLLOUT so onCgiPipeOUT can stream
 * the request body. On any failure the CGI state is torn down and a 500 is sent.
 */
void Server::_startCgi(Connection* conn, int client_fd) {
    if (!CgiHandler::execute(conn, conn->cgi_job)) {
        Logger::log(Logger::ERROR, "CGI execute failed");
        _sendError(conn, client_fd, 500);
        return;
    }

    conn->cgi_sent_idx = 0;
    conn->last_activity = std::time(NULL);  // baseline for the CGI inactivity timer

    try {
        // Park the client socket: no more EPOLLIN/EPOLLOUT until the CGI reply
        // is ready, but keep EPOLLRDHUP so a client disconnect is detected.
        _epoll.modifyFd(client_fd, EPOLLRDHUP, conn);

        // Register both pipes up front. Watching stdout from the start avoids a
        // deadlock where the child blocks writing a large response while we are
        // still feeding its stdin.
        conn->cgi_in_wrapper = new CgiPipeWrapper(conn, false);
        _epoll.addFd(conn->cgi_in_fd, EPOLLOUT, conn->cgi_in_wrapper);

        conn->cgi_out_wrapper = new CgiPipeWrapper(conn, true);
        _epoll.addFd(conn->cgi_out_fd, EPOLLIN, conn->cgi_out_wrapper);
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, std::string("CGI epoll setup failed: ") + e.what());
        _teardownCgi(conn);
        _sendError(conn, client_fd, 500);
        return;
    }

    Logger::log(Logger::INFO, "CGI started pid=" + to_string(conn->cgi_pid) +
                              " (" + conn->cgi_job.interpreter + " " +
                              conn->cgi_job.script_path + ")");
}

/*
 * Closes the CGI stdin pipe (write end) and frees its wrapper, so the child
 * sees EOF on stdin. Removes it from epoll first. Safe to call repeatedly.
 */
void Server::_closeCgiInPipe(Connection* conn) {
    if (conn->cgi_in_fd != -1) {
        try { _epoll.removeFd(conn->cgi_in_fd); }
        catch (const std::exception& e) {
            Logger::log(Logger::WARNING, std::string("CGI in-pipe epoll remove: ") + e.what());
        }
        close(conn->cgi_in_fd);
        conn->cgi_in_fd = -1;
    }
    delete conn->cgi_in_wrapper;
    conn->cgi_in_wrapper = NULL;
}

/*
 * Builds the HTTP response for a routed (non-CGI) request. Flattens the
 * RouteMatch into a RouteContext (resolving server/location inheritance), then
 * delegates to ResponseBuilder for the full GET/HEAD/POST/DELETE/return/error
 * decision. If the route context cannot be resolved (e.g. unresolved root /
 * upload_store), we cannot consult error_page, so we emit a default 500 page.
 * Connection: close is forced because keep-alive is not yet implemented
 * (onClientOUT closes the socket once the response is drained).
 */
std::string Server::_buildResponse(const RequestContext& ctx, const RouteMatch& match) const {
    RouteContext route;
    try {
        RouteContextBuilder routeBuilder;
        route = routeBuilder.build(match);
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("RouteContext build error: ") + e.what());
        Response err(500, reason_phrase(500));
        err.setHeader("Content-Type", "text/html");
        err.setBody(default_error_body(500));
        err.setHeader("Connection", "close");
        return err.toString();
    }

    ResponseBuilder builder;
    Response resp = builder.build(ctx, route);
    resp.setHeader("Connection", "close");
    return resp.toString();
}

/*
 * Queues an error response and arms EPOLLOUT. Built via make_error_response so a
 * custom error_page (resolved against the server root) is served when configured,
 * falling back to a generated page. conn->server_cfg is the port-default server
 * block — the best error_page source available before routing completes.
 * onClientOUT will drain and then set state=CLOSED; Connection::handle() closes.
 */
void Server::_sendError(Connection* conn, int fd, int status) {
    Response resp = make_error_response(status, conn->server_cfg);
    resp.setHeader("Connection", "close");
    std::string response = resp.toString();
    conn->out_buffer.assign(response.begin(), response.end());
    conn->write_idx = 0;
    conn->setState(Connection::SENDING_RESPONSE);
    _epoll.modifyFd(fd, EPOLLOUT | EPOLLRDHUP, conn);
}

/*
 * CGI stdout pipe is readable: read once into conn->cgi_raw_output. epoll is
 * level-triggered, so if more data remains EPOLLIN fires again on the next loop
 * iteration. EOF (read == 0) means the child finished writing its response, so we
 * finalize. A negative return is a read error and finalizes as 502 (errno must
 * not be inspected after a read).
 */
void Server::onCgiPipeIN(Connection* conn) {
    char buf[65536];

    ssize_t n = read(conn->cgi_out_fd, buf, sizeof(buf));
    if (n > 0) {
        conn->cgi_raw_output.insert(conn->cgi_raw_output.end(), buf, buf + n);
        conn->last_activity = std::time(NULL);  // progress: reset inactivity timer
        return;
    }
    if (n == 0) {                       // EOF: response complete
        _finalizeCgi(conn, false);
        return;
    }
    _finalizeCgi(conn, true);           // read error -> 502
}

/*
 * Reaps the CGI child, tears down its pipes, and turns the collected output into
 * the client response. Empty output, a read error, or output without a valid
 * header block becomes 502 Bad Gateway.
 */
void Server::_finalizeCgi(Connection* conn, bool readError) {
    int client_fd = conn->getFd();

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
    _teardownCgi(conn);

    std::string response;
    if (!readError)
        response = _buildCgiResponse(conn);  // empty string if output is malformed

    if (response.empty()) {
        Logger::log(Logger::WARNING, "CGI produced no valid response -> 502");
        _sendError(conn, client_fd, 502);
        return;
    }

    conn->out_buffer.assign(response.begin(), response.end());
    conn->write_idx = 0;
    conn->setState(Connection::SENDING_RESPONSE);
    _epoll.modifyFd(client_fd, EPOLLOUT | EPOLLRDHUP, conn);
    Logger::log(Logger::DEBUG, "CGI response ready (" + to_string(response.size()) + " bytes)");
}

/*
 * Translates raw CGI output into a complete HTTP/1.1 response.
 *
 * CGI scripts emit a header block (terminated by a blank line) followed by the
 * body. We honor a "Status:" header for the status line, pass the remaining
 * headers through (dropping hop-by-hop / length headers we control), and append
 * a computed Content-Length plus "Connection: close". Returns "" if there is no
 * header/body separator so the caller can emit 502.
 */
std::string Server::_buildCgiResponse(Connection* conn) const {
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
    out += "Connection: close\r\n\r\n";
    out += body;
    return out;
}

/*
 * Largest client_max_body_size across every server block on `port` and all of
 * their locations, used as an early 413 guard. A location value of (size_t)-1
 * means "inherit from the server" (see RouteContextBuilder::resolveSize), so it
 * resolves to the server-level limit. Returns (size_t)-1 if any matching route
 * is unlimited (in which case no early rejection is possible).
 */
size_t Server::_maxBodyLimitForPort(int port) const {
    size_t max = 0;
    for (size_t i = 0; i < _configs.size(); ++i) {
        const ServerConfig& cfg = _configs[i];
        if (cfg.port != port)
            continue;
        if (cfg.locations.empty()) {
            // No route: a request here resolves to the server-level limit.
            if (cfg.client_max_body_size == (size_t)-1)
                return (size_t)-1;
            if (cfg.client_max_body_size > max)
                max = cfg.client_max_body_size;
            continue;
        }
        for (size_t j = 0; j < cfg.locations.size(); ++j) {
            size_t eff = (cfg.locations[j].client_max_body_size != (size_t)-1)
                             ? cfg.locations[j].client_max_body_size
                             : cfg.client_max_body_size;
            if (eff == (size_t)-1)
                return (size_t)-1;
            if (eff > max)
                max = eff;
        }
    }
    return max;
}

/*
 * Throttled (~1Hz) sweep over all connections. A CGI exchange is aborted with
 * 504 when it has either made no I/O progress for CGI_INACTIVITY seconds or run
 * longer than the absolute CGI_HARD cap. Guarantees the "never hang" rule even
 * for a child that loops forever or stalls.
 */
void Server::_checkTimeouts() {
    std::time_t now = std::time(NULL);
    if (now - _last_timeout_check < 1)
        return;
    _last_timeout_check = now;

    const std::time_t CGI_INACTIVITY = 5;   // no stdin/stdout progress
    const std::time_t CGI_HARD       = 60;  // absolute wall-clock cap
    const std::time_t CLIENT_TIMEOUT = 30;  // idle ordinary client (slowloris guard)

    // Collect first, then act. _timeoutCgi only rewires epoll (keeps the
    // connection alive to send 504), while idle clients are closed outright via
    // onCloseConnection (which deletes the Connection and erases the map entry),
    // so we must not mutate the map while iterating it.
    std::vector<Connection*> expired;
    std::vector<Connection*> idle;
    for (std::map<int, Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it) {
        Connection* conn = it->second;
        bool is_cgi = (conn->state == Connection::CGI_WRITING_BODY ||
                       conn->state == Connection::CGI_READING_OUTPUT);
        if (is_cgi) {
            if (conn->cgi_pid <= 0)
                continue;
            if ((now - conn->cgi_start_time) >= CGI_HARD ||
                (now - conn->last_activity)  >= CGI_INACTIVITY)
                expired.push_back(conn);
        } else {
            // No socket I/O for CLIENT_TIMEOUT seconds: a stalled or slowloris
            // client. Close it so a request can never hang the connection forever.
            if ((now - conn->last_activity) >= CLIENT_TIMEOUT)
                idle.push_back(conn);
        }
    }

    for (size_t i = 0; i < expired.size(); ++i)
        _timeoutCgi(expired[i]);
    for (size_t i = 0; i < idle.size(); ++i) {
        Logger::log(Logger::INFO, "Closing idle client fd=" +
                    to_string(idle[i]->getFd()));
        onCloseConnection(idle[i]->getFd(), idle[i]);
    }
}

/*
 * Aborts a timed-out CGI: kills + reaps the child and frees its pipes (via
 * _teardownCgi), then queues 504 Gateway Timeout on the client socket.
 */
void Server::_timeoutCgi(Connection* conn) {
    int client_fd = conn->getFd();
    Logger::log(Logger::WARNING, "CGI timeout (pid=" + to_string(conn->cgi_pid) +
                                 ") on fd=" + to_string(client_fd) + " -> 504");
    _teardownCgi(conn);
    _sendError(conn, client_fd, 504);
}

/*
 * CGI stdin pipe is writable: stream the request body to the child.
 * Writes as much as the pipe accepts; resumes on the next EPOLLOUT (EAGAIN).
 * When the whole body is written — or the child closed its stdin (EPIPE) — the
 * stdin pipe is closed so the CGI sees EOF, and the connection moves to
 * CGI_READING_OUTPUT. The stdout pipe is already registered (see _startCgi), so
 * onCgiPipeIN collects the reply independently.
 */
void Server::onCgiPipeOUT(Connection* conn) {
    const std::vector<char>& body = conn->cgi_job.body;

    if (conn->cgi_sent_idx < body.size()) {
        size_t remaining = body.size() - conn->cgi_sent_idx;
        ssize_t n = write(conn->cgi_in_fd, &body[conn->cgi_sent_idx], remaining);
        if (n < 0) {
            // Written only in response to EPOLLOUT. errno must not be inspected
            // after a write, so treat any negative return as the child having
            // closed/exited (EPIPE): stop feeding stdin and move to reading its
            // output. The stdin pipe is closed so the child sees EOF.
            _closeCgiInPipe(conn);
            conn->setState(Connection::CGI_READING_OUTPUT);
            return;
        }
        conn->cgi_sent_idx += static_cast<size_t>(n);
        conn->last_activity = std::time(NULL);  // progress: reset inactivity timer
    }

    if (conn->cgi_sent_idx >= body.size()) {
        Logger::log(Logger::DEBUG, "CGI request body fully written (" +
                                   to_string(body.size()) + " bytes)");
        _closeCgiInPipe(conn);
        conn->setState(Connection::CGI_READING_OUTPUT);
    }
}

