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
#include <algorithm>

/*
 * Finds the first occurrence of the byte sequence [seq, seq+seqlen) in `buf`
 * starting at index `from`, returning its index or std::string::npos. Lets the
 * readiness checks scan conn->in_buffer (a std::vector<char>) for CRLF markers
 * without first copying it into a std::string.
 */
static size_t findSeq(const std::vector<char>& buf, size_t from,
                      const char* seq, size_t seqlen) {
    if (from > buf.size())
        return std::string::npos;
    std::vector<char>::const_iterator it =
        std::search(buf.begin() + from, buf.end(), seq, seq + seqlen);
    if (it == buf.end())
        return std::string::npos;
    return static_cast<size_t>(it - buf.begin());
}

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
}

/*
 * Scans a (possibly partial) chunked body for completeness by walking the chunk
 * framing, mirroring ChunkDecoder's rules so this check and the later decode
 * never disagree. Returns REQ_INCOMPLETE while any framing token is only
 * partially buffered, REQ_ERROR (errorStatus 400/413) on malformed framing or an
 * over-limit running size, and REQ_COMPLETE once the zero-size last-chunk and its
 * trailer section (terminated by a blank line) have arrived. Unlike searching for
 * "0\r\n\r\n", this is not fooled by that byte sequence appearing inside chunk
 * data, and it correctly skips chunk extensions (";...") and trailer headers.
 */
Server::RequestReadiness Server::_chunkedReadiness(const std::vector<char>& buffer,
                                                   size_t body_start, size_t limit,
                                                   int& errorStatus,
                                                   size_t& consumed) const {
    size_t pos = body_start;
    size_t decoded = 0;

    while (true) {
        // chunk-size line: hex digits, optionally followed by a ";chunk-extension".
        size_t crlf = findSeq(buffer, pos, "\r\n", 2);
        if (crlf == std::string::npos)
            return REQ_INCOMPLETE;              // size line not fully arrived yet

        std::string size_line(buffer.begin() + pos, buffer.begin() + crlf);
        size_t semi = size_line.find(';');
        std::string hex = (semi == std::string::npos) ? size_line : size_line.substr(0, semi);
        if (hex.empty()) { errorStatus = 400; return REQ_ERROR; }

        size_t chunk_size = 0;
        for (size_t i = 0; i < hex.size(); ++i) {
            int d = hex_value(hex[i]);
            if (d < 0) { errorStatus = 400; return REQ_ERROR; }
            if (chunk_size > (((size_t)-1) - static_cast<size_t>(d)) / 16) {
                errorStatus = 400; return REQ_ERROR;   // chunk-size overflow
            }
            chunk_size = chunk_size * 16 + static_cast<size_t>(d);
        }

        size_t data_start = crlf + 2;            // crlf found => data_start <= size()

        // last-chunk: the body ends after the trailer section's terminating blank line.
        if (chunk_size == 0) {
            size_t tpos = data_start;
            while (true) {
                size_t tcrlf = findSeq(buffer, tpos, "\r\n", 2);
                if (tcrlf == std::string::npos)
                    return REQ_INCOMPLETE;        // trailers / final CRLF not all here
                if (tcrlf == tpos) {
                    consumed = tcrlf + 2;        // past the terminating blank line
                    return REQ_COMPLETE;          // blank line => full body received
                }
                tpos = tcrlf + 2;                // a trailer header line: skip it
            }
        }

        // Early 413: keep the running decoded size within the port's limit.
        if (limit != (size_t)-1) {
            if (chunk_size > limit - decoded) { errorStatus = 413; return REQ_ERROR; }
            decoded += chunk_size;
        }

        // Need chunk_size data bytes plus the trailing CRLF, all buffered.
        // Written as subtraction (avail is non-negative) to avoid pointer overflow.
        size_t avail = buffer.size() - data_start;
        if (chunk_size > avail || avail - chunk_size < 2)
            return REQ_INCOMPLETE;
        if (buffer[data_start + chunk_size] != '\r' ||
            buffer[data_start + chunk_size + 1] != '\n') {
            errorStatus = 400; return REQ_ERROR;     // missing/invalid CRLF after chunk-data
        }
        pos = data_start + chunk_size + 2;
    }
}

/*
 * Decides whether a complete request is buffered, without mutating state.
 *
 * A request is complete once the "\r\n\r\n" header terminator is present AND the
 * body has fully arrived: for Transfer-Encoding, the "0\r\n\r\n" chunk terminator;
 * otherwise Content-Length bytes after the header block. The headers are parsed
 * through Headers (case-insensitive, reusing validated logic) only to read those
 * two fields. Errors map to a status for the caller to send: a malformed header
 * block or bad Content-Length via status_from_exception, or an early 413 when the
 * declared length already exceeds every route's limit on this port (so we reject
 * before buffering the body; the exact per-route limit is still enforced later in
 * ResponseBuilder for bodies within this maximum).
 */
Server::RequestReadiness Server::_requestReadiness(const std::vector<char>& buffer,
                                                   int port, int& errorStatus,
                                                   size_t& consumed) const {
    size_t header_end = findSeq(buffer, 0, "\r\n\r\n", 4);
    if (header_end == std::string::npos)
        return REQ_INCOMPLETE;

    size_t body_start = header_end + 4;
    size_t body_received = buffer.size() - body_start;

    Headers headers;
    try {
        // Copy only the header block (bounded by client_max_header_size), never
        // the body, so this stays cheap as a large body streams in.
        std::string head(buffer.begin(), buffer.begin() + header_end);
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
                headers.parseLine(line);
        }
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("Header parse error: ") + e.what());
        errorStatus = status_from_exception(e.what());
        return REQ_ERROR;
    }

    if (headers.hasHeader("transfer-encoding")) {
        // Chunked: walk the chunk framing (robust to data containing the
        // terminator, chunk extensions, and trailers) instead of a byte search.
        return _chunkedReadiness(buffer, body_start, _maxBodyLimitForPort(port),
                                 errorStatus, consumed);
    }

    size_t content_length = 0;
    try {
        content_length = headers.getContentLength();
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("Content-Length error: ") + e.what());
        errorStatus = status_from_exception(e.what());
        return REQ_ERROR;
    }

    size_t limit = _maxBodyLimitForPort(port);
    if (limit != (size_t)-1 && content_length > limit) {
        Logger::log(Logger::WARNING, "Content-Length " + to_string(content_length) +
                    " exceeds max body size " + to_string(limit) + " -> 413");
        errorStatus = 413;
        return REQ_ERROR;
    }

    if (body_received < content_length) {
        Logger::log(Logger::DEBUG, "Body incomplete: received " + to_string(body_received) +
                    " bytes, expecting " + to_string(content_length));
        return REQ_INCOMPLETE;
    }

    consumed = body_start + content_length;
    return REQ_COMPLETE;
}

/*
 * Called when a client socket is readable. Pure socket I/O: read one chunk, append
 * it to the connection buffer, refresh the timers, then hand off to _processRequest
 * which owns all HTTP parsing/routing. (epoll is level-triggered, so any unread
 * bytes simply trigger another EPOLLIN.)
 */
void Server::onClientIN(Connection* conn) {
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
    // Stamp the start of this request the first time any byte arrives. Unlike
    // last_activity (reset on every recv), this is NOT reset by later bytes, so it
    // anchors an absolute receive deadline that a slow-drip client cannot defer.
    if (conn->first_byte_time == 0)
        conn->first_byte_time = conn->last_activity;
    Logger::log(Logger::DEBUG, "Read " + to_string(n) + " bytes, in_buffer now " +
                to_string(conn->in_buffer.size()) + " bytes");

    _processRequest(conn);
}

/*
 * Turns the bytes buffered for one client into a queued response. Runs the
 * completeness gate (in place over in_buffer, no whole-buffer copy), and only once
 * a full request is present does it materialize the buffer as a string, parse it,
 * route it, and dispatch CGI or the static ResponseBuilder. Every failure path
 * funnels through _sendError. Called from onClientIN after each read.
 */
void Server::_processRequest(Connection* conn) {
    // Decide whether a full request has arrived before doing any real parsing.
    int errorStatus = 0;
    size_t consumed = 0;
    RequestReadiness ready =
        _requestReadiness(conn->in_buffer, conn->server_cfg->port, errorStatus, consumed);
    if (ready == REQ_INCOMPLETE) {
        Logger::log(Logger::DEBUG, "Incomplete request, waiting for more data");
        return;  // keep first_byte_time set: the absolute receive deadline is ticking
    }
    if (ready == REQ_ERROR) {
        conn->first_byte_time = 0;  // done receiving; switch to the idle/send timer
        conn->in_buffer.clear();
        _sendError(conn, errorStatus);  // sets keep_alive = false
        return;
    }

    // Full request received — the absolute receive deadline no longer applies.
    conn->first_byte_time = 0;

    // Materialize exactly this request (header block + body) for the parser, then
    // drop those bytes from the buffer — any trailing bytes are a pipelined next
    // request and are kept for the following round.
    std::string buffer_str(conn->in_buffer.begin(), conn->in_buffer.begin() + consumed);
    conn->in_buffer.erase(conn->in_buffer.begin(), conn->in_buffer.begin() + consumed);

    Request req;
    try {
        req.parse(buffer_str);
        Logger::log(Logger::DEBUG, "Request parsed successfully");
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("Request parse error: ") + e.what());
        conn->in_buffer.clear();
        _sendError(conn, status_from_exception(e.what()));
        return;
    }

    // Keep-alive intent: HTTP/1.1 defaults to keep-alive unless "Connection: close";
    // HTTP/1.0 defaults to close unless "Connection: keep-alive".
    const Headers& reqHeaders = req.headers();
    if (req.requestLine().version() == "HTTP/1.0") {
        conn->keep_alive = reqHeaders.hasHeader("connection") &&
            to_lower_ascii(reqHeaders.getHeader("connection")) == "keep-alive";
    } else {
        conn->keep_alive = !reqHeaders.wantsClose();
    }

    // Build RequestContext using the port this connection arrived on.
    RequestContext ctx;
    try {
        ctx.build(req, conn->server_cfg->port);
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("RequestContext build error: ") + e.what());
        _sendError(conn, status_from_exception(e.what()));
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
        _sendError(conn, status_from_exception(e.what()));
        return;
    }

    // CGI dispatch: if the matched location maps this target's extension to an
    // interpreter, launch the CGI child instead of building a static response.
    // The response is then produced asynchronously by the CGI pipe callbacks.
    // CGI bypasses ResponseBuilder's 405 check, so we gate it on the route's
    // accepted methods here: a disallowed method falls through to _buildResponse,
    // which emits the canonical 405 (+ Allow header + custom error page).
    if (routeMatch.server && routeMatch.location &&
        _isMethodAllowed(routeMatch, ctx.method())) {
        CgiJob job;
        if (CgiHandler::buildJob(ctx, *routeMatch.server, *routeMatch.location, job)) {
            conn->cgi_job = job;
            _startCgi(conn);
            return;
        }
    }

    _queueResponse(conn, _buildResponse(ctx, routeMatch, conn->keep_alive));
}

/*
 * Called when a client socket is writable.
 * Sends buffered response data. Sets CLOSED when done; Connection::handle() calls
 * onCloseConnection after this returns.
 */
void Server::onClientOUT(Connection* conn) {
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
        if (!conn->keep_alive) {
            conn->setState(Connection::CLOSED);
            return;
        }
        // Keep-alive: reset for the next request instead of closing.
        conn->out_buffer.clear();
        conn->write_idx = 0;
        conn->setState(Connection::READING_HEADER);
        _epoll.modifyFd(fd, EPOLLIN | EPOLLRDHUP, conn);
        // A pipelined request may already sit behind the one we just answered —
        // handle it now rather than waiting for another EPOLLIN edge.
        if (!conn->in_buffer.empty())
            _processRequest(conn);
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
    _closeCgiInPipe(conn);  // stdin pipe fd + its wrapper (shared with the EOF path)
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
void Server::_startCgi(Connection* conn) {
    if (!CgiHandler::execute(conn, conn->cgi_job)) {
        Logger::log(Logger::ERROR, "CGI execute failed");
        _sendError(conn, 500);
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
        _teardownCgi(conn);
        _sendError(conn, 500);
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
 * Mirrors RouteContextBuilder::resolveMethods: the location's allow_methods if
 * non-empty, otherwise the GET/HEAD default. Used to gate CGI dispatch so a
 * method outside the route's accepted set is rejected (405) just like the static
 * path, instead of silently executing the script.
 */
bool Server::_isMethodAllowed(const RouteMatch& match, const std::string& method) const {
    const std::vector<std::string>& methods = match.location->allowed_methods;
    if (methods.empty())
        return method == "GET" || method == "HEAD";
    for (size_t i = 0; i < methods.size(); ++i) {
        if (methods[i] == method)
            return true;
    }
    return false;
}

/*
 * Builds the HTTP response for a routed (non-CGI) request. Flattens the
 * RouteMatch into a RouteContext (resolving server/location inheritance), then
 * delegates to ResponseBuilder for the full GET/HEAD/POST/DELETE/return/error
 * decision. If the route context cannot be resolved (e.g. unresolved root /
 * upload_store), we fall back to the shared error builder for a 500 page. Either
 * way the Connection header tracks `keepAlive` (both paths are fully framed with a
 * Content-Length, so the connection can be reused).
 */
std::string Server::_buildResponse(const RequestContext& ctx, const RouteMatch& match,
                                   bool keepAlive) const {
    const char* connHeader = keepAlive ? "keep-alive" : "close";
    RouteContext route;
    try {
        RouteContextBuilder routeBuilder;
        route = routeBuilder.build(match);
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("RouteContext build error: ") + e.what());
        Response err = make_error_response(500, match.server);
        err.setHeader("Connection", connHeader);
        return err.toString();
    }

    ResponseBuilder builder;
    Response resp = builder.build(ctx, route);
    resp.setHeader("Connection", connHeader);
    return resp.toString();
}

/*
 * Hands a finished response back to the event loop: buffer it, reset the write
 * cursor, switch to SENDING_RESPONSE, and arm EPOLLOUT (+ EPOLLRDHUP) so
 * onClientOUT drains it and then closes. Single owner of this state transition,
 * shared by the static, CGI, and error paths.
 */
void Server::_queueResponse(Connection* conn, const std::string& response) {
    conn->out_buffer.assign(response.begin(), response.end());
    conn->write_idx = 0;
    conn->setState(Connection::SENDING_RESPONSE);
    _epoll.modifyFd(conn->getFd(), EPOLLOUT | EPOLLRDHUP, conn);
}

/*
 * Queues an error response and arms EPOLLOUT. Built via make_error_response so a
 * custom error_page (resolved against the server root) is served when configured,
 * falling back to a generated page. conn->server_cfg is the port-default server
 * block — the best error_page source available before routing completes.
 * onClientOUT will drain and then set state=CLOSED; Connection::handle() closes.
 */
void Server::_sendError(Connection* conn, int status) {
    conn->keep_alive = false;  // errors close: keeps the header and lifecycle in sync
    Response resp = make_error_response(status, conn->server_cfg);
    resp.setHeader("Connection", "close");
    _queueResponse(conn, resp.toString());
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
        _sendError(conn, 502);
        return;
    }

    _queueResponse(conn, response);
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
    out += conn->keep_alive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
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
 * longer than the absolute CGI_HARD cap. An ordinary client is closed when a
 * request it is sending exceeds REQUEST_TIMEOUT measured from its first byte (a
 * slow-drip / slowloris client cannot defer this by trickling bytes, since the
 * deadline is anchored to first_byte_time, not the per-recv last_activity), or
 * when it sits idle (no request in progress) for CLIENT_TIMEOUT. Guarantees the
 * "never hang indefinitely" rule for both runaway CGI and stalled clients.
 */
void Server::_checkTimeouts() {
    std::time_t now = std::time(NULL);
    if (now - _last_timeout_check < 1)
        return;
    _last_timeout_check = now;

    const std::time_t CGI_INACTIVITY  = 5;   // no stdin/stdout progress
    const std::time_t CGI_HARD        = 60;  // absolute wall-clock cap
    const std::time_t REQUEST_TIMEOUT = 60;  // absolute cap on receiving one request
    const std::time_t CLIENT_TIMEOUT  = 30;  // idle client with no request in flight

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
        } else if (conn->first_byte_time != 0) {
            // A request is mid-arrival. Enforce an ABSOLUTE deadline from its first
            // byte: trickle bytes keep resetting last_activity, so without this a
            // slowloris client could hold the request open forever (violating the
            // "never hang" rule). first_byte_time is not reset by later bytes.
            if ((now - conn->first_byte_time) >= REQUEST_TIMEOUT)
                idle.push_back(conn);
        } else {
            // No request in flight (freshly accepted but silent, or draining a
            // response): close if the socket made no progress for CLIENT_TIMEOUT.
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
    _sendError(conn, 504);
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

