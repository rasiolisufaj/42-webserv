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
#include "server/CgiManager.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <csignal>
#include <set>
#include <algorithm>

extern volatile sig_atomic_t g_running;

// Finds the first occurrence of a byte sequence in buf starting at `from`.
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

Server::Server(const std::vector<ServerConfig>& configs)
    : _configs(configs), _running(false), _last_timeout_check(std::time(NULL)),
      _cgi(*this, _epoll) {}

// Closes all connections and listening sockets.
Server::~Server() {
    for (std::map<int, Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it) {
        _epoll.removeFd(it->second->getFd());
        delete it->second;
    }
    _connections.clear();

    for (size_t i = 0; i < _listeners.size(); ++i) {
        _epoll.removeFd(_listeners[i]->getFd());
        delete _listeners[i];
    }
    _listeners.clear();
}

// Creates one listening socket per unique host:port and registers it with epoll.
// Multiple ServerConfigs on the same port share one socket (virtual hosting).
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

// Main event loop: waits for epoll events and dispatches them until stopped.
void Server::run() {
    _initListeners();
    _running = true;

    Logger::log(Logger::INFO, "Server event loop starting...");

    while (_running && g_running) {
        int nfds = _epoll.wait(1000); // 1s timeout

        if (nfds == -1) {
            Logger::log(Logger::ERROR, "epoll_wait failed");
            break;
        }

        // Use-after-free guard: skip later events whose handler was freed mid-batch.
        _retired.clear();

        _checkTimeouts();

        if (nfds == 0)
            continue;

        const struct epoll_event* events = _epoll.getEvents();
        if (!events) {
            continue;
        }

        for (int i = 0; i < nfds; ++i) {
            void* ptr = events[i].data.ptr;
            if (!ptr) {
                Logger::log(Logger::WARNING, "epoll returned null handler");
                continue;
            }

            if (_retired.count(ptr))
                continue;

            IEventHandler* handler = static_cast<IEventHandler*>(ptr);
            try {
                handler->handle(events[i].events, *this);
            } catch (const std::exception& e) {
                Logger::log(Logger::ERROR, std::string("Exception in event handler: ") + e.what());
            }
        }
    }

    Logger::log(Logger::INFO, "Server event loop stopped");
}

// Called by Socket when a new client connects: creates a Connection,
// resolves its port-default config, and registers it with epoll.
void Server::onNewConnection(int client_fd, int listener_fd) {
    if (client_fd < 0) {
        Logger::log(Logger::WARNING, "Received invalid client fd: " + to_string(client_fd));
        return;
    }

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

    Connection* conn = NULL;
    try {
        conn = new Connection(client_fd, cfg);
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, std::string("Failed to create Connection: ") + e.what());
        close(client_fd);
        return;
    }

    try {
        // EPOLLRDHUP also detects the client closing its write side.
        _epoll.addFd(client_fd, EPOLLIN | EPOLLRDHUP, conn);
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, std::string("Failed to add client fd to epoll: ") + e.what());
        delete conn;
        return;
    }

    _connections[client_fd] = conn;
    Logger::log(Logger::INFO, "New connection accepted (fd=" + to_string(client_fd) + ")");
}

// Walks chunked body framing to decide complete/incomplete/invalid, enforcing the
// body size limit as it goes. Mirrors ChunkDecoder's rules so it isn't fooled by a
// "0\r\n\r\n" sequence sitting inside chunk data.
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
            return REQ_INCOMPLETE;

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

        size_t data_start = crlf + 2;

        // last-chunk: body ends after the trailer section's terminating blank line.
        if (chunk_size == 0) {
            size_t tpos = data_start;
            while (true) {
                size_t tcrlf = findSeq(buffer, tpos, "\r\n", 2);
                if (tcrlf == std::string::npos)
                    return REQ_INCOMPLETE;
                if (tcrlf == tpos) {
                    consumed = tcrlf + 2;        // past the terminating blank line
                    return REQ_COMPLETE;
                }
                tpos = tcrlf + 2;                // skip a trailer header line
            }
        }

        // Early 413: keep the running decoded size within the port's limit.
        if (limit != (size_t)-1) {
            if (chunk_size > limit - decoded) { errorStatus = 413; return REQ_ERROR; }
            decoded += chunk_size;
        }

        // Require the chunk-data plus its trailing CRLF to be fully buffered.
        size_t avail = buffer.size() - data_start;
        if (chunk_size > avail || avail - chunk_size < 2)
            return REQ_INCOMPLETE;
        if (buffer[data_start + chunk_size] != '\r' ||
            buffer[data_start + chunk_size + 1] != '\n') {
            errorStatus = 400; return REQ_ERROR;
        }
        pos = data_start + chunk_size + 2;
    }
}

// Decides if a full request is buffered without mutating state, so partial
// requests are never parsed. See Server.hpp for the return-value contract.
Server::RequestReadiness Server::_requestReadiness(const std::vector<char>& buffer,
                                                   int port, int& errorStatus,
                                                   size_t& consumed) const {
    size_t header_end = findSeq(buffer, 0, "\r\n\r\n", 4);
    size_t header_limit = _maxHeaderLimitForPort(port);
    // Cap the header block early (431) so a slowloris drip can't grow it unbounded.
    if (header_end == std::string::npos) {
        if (header_limit != 0 && header_limit != (size_t)-1 && buffer.size() > header_limit) {
            Logger::log(Logger::WARNING, "Header block exceeds max header size " +
                        to_string(header_limit) + " -> 431");
            errorStatus = 431;
            return REQ_ERROR;
        }
        return REQ_INCOMPLETE;
    }

    if (header_limit != 0 && header_limit != (size_t)-1 && header_end > header_limit) {
        Logger::log(Logger::WARNING, "Header block of " + to_string(header_end) +
                    " bytes exceeds max header size " + to_string(header_limit) + " -> 431");
        errorStatus = 431;
        return REQ_ERROR;
    }

    size_t body_start = header_end + 4;
    size_t body_received = buffer.size() - body_start;

    Headers headers;
    try {
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

    // Transfer-Encoding present => body is chunked (RFC 7230 §3.3.1).
    if (headers.hasHeader("transfer-encoding")) {
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

// Client socket readable (EPOLLIN): one recv into in_buffer, then hand off to
// _processRequest. Closes on EOF (n==0) or read error (n<0).
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
        conn->setState(Connection::CLOSED);
        return;
    }

    conn->in_buffer.insert(conn->in_buffer.end(), buf, buf + n);
    // first_byte_time anchors the absolute request timeout; last_activity the idle one.
    conn->last_activity = std::time(NULL);
    if (conn->first_byte_time == 0)
        conn->first_byte_time = conn->last_activity;
    Logger::log(Logger::DEBUG, "Read " + to_string(n) + " bytes, in_buffer now " +
                to_string(conn->in_buffer.size()) + " bytes");

    _processRequest(conn);
}

// Drives one buffered request: completeness gate -> parse -> route -> CGI or
// static response. Runs per recv and per pipelined leftover; no-op until complete.
void Server::_processRequest(Connection* conn) {
    int errorStatus = 0;
    size_t consumed = 0;
    RequestReadiness ready =
        _requestReadiness(conn->in_buffer, conn->server_cfg->port, errorStatus, consumed);
    if (ready == REQ_INCOMPLETE) {
        Logger::log(Logger::DEBUG, "Incomplete request, waiting for more data");
        return;
    }
    if (ready == REQ_ERROR) {
        conn->first_byte_time = 0;  // done receiving; switch to the idle/send timer
        conn->in_buffer.clear();
        _sendError(conn, errorStatus);  // sets keep_alive = false
        return;
    }

    conn->first_byte_time = 0;

    // Parse exactly the consumed bytes; keep any pipelined remainder in the buffer.
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

    // Keep-alive intent: HTTP/1.0 opts in, HTTP/1.1 defaults on unless Connection: close.
    const Headers& reqHeaders = req.headers();
    if (req.requestLine().version() == "HTTP/1.0") {
        conn->keep_alive = reqHeaders.hasHeader("connection") &&
            to_lower_ascii(reqHeaders.getHeader("connection")) == "keep-alive";
    } else {
        conn->keep_alive = !reqHeaders.wantsClose();
    }

    RequestContext ctx;
    try {
        ctx.build(req, conn->server_cfg->port);
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("RequestContext build error: ") + e.what());
        _sendError(conn, status_from_exception(e.what()));
        return;
    }

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

    // CGI runs only for an allowed method on a matched CGI interpreter; a
    // disallowed method falls through to the static path's canonical 405.
    if (routeMatch.server && routeMatch.location &&
        _isMethodAllowed(routeMatch, ctx.method()) &&
        _cgi.dispatch(conn, ctx, routeMatch))
        return;

    _queueResponse(conn, _buildResponse(ctx, routeMatch, conn->keep_alive));
}

// Client socket writable (EPOLLOUT): drains out_buffer. When fully sent, either
// closes or (keep-alive) resets for the next request. Sets CLOSED on completion or
// error; Connection::handle() then calls onCloseConnection.
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
        // errno must not be inspected after a write, so any negative return = close.
        conn->setState(Connection::CLOSED);
        return;
    }

    conn->write_idx += static_cast<size_t>(n);
    conn->last_activity = std::time(NULL);

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
        // Handle a pipelined request now rather than waiting for another EPOLLIN.
        if (!conn->in_buffer.empty())
            _processRequest(conn);
    }
}

// Closes a connection: tears down CGI, removes from epoll, frees the Connection.
void Server::onCloseConnection(int fd, Connection* conn) {
    Logger::log(Logger::DEBUG, "Closing connection fd=" + to_string(fd));

    // Release any CGI pipes/child first so epoll never holds a dangling wrapper.
    if (conn)
        _cgi.teardown(conn);

    try {
        _epoll.removeFd(fd);
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("Error removing fd from epoll: ") + e.what());
    }

    std::map<int, Connection*>::iterator it = _connections.find(fd);
    if (it != _connections.end()) {
        // Retire the client handler before freeing: a later event in this same
        // batch may still point to it (teardown above retired the CGI wrappers).
        _retireHandler(it->second);
        delete it->second;
        _connections.erase(it);
    }
}

// Records a handler freed mid-batch so run() skips its stale events (see _retired).
// Stores the pointer value only — the object must not be dereferenced.
void Server::_retireHandler(const void* handler) {
    if (handler)
        _retired.insert(handler);
}

// Whether `method` is allowed on the route (location's allow_methods, else GET/HEAD),
// matching RouteContextBuilder. Gates CGI so a disallowed method yields the static 405.
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

// Flattens the route (RouteContextBuilder) and runs ResponseBuilder for a non-CGI
// request, serializing the result. A build failure yields a 500 page.
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

// Hands a finished response to the event loop: buffer it, switch to
// SENDING_RESPONSE, arm EPOLLOUT. The single exit point for static/CGI/error replies.
void Server::_queueResponse(Connection* conn, const std::string& response) {
    conn->out_buffer.assign(response.begin(), response.end());
    conn->write_idx = 0;
    conn->setState(Connection::SENDING_RESPONSE);
    _epoll.modifyFd(conn->getFd(), EPOLLOUT | EPOLLRDHUP, conn);
}

// Queues an error response via make_error_response (custom error_page when set,
// else a generated page). Uses conn->server_cfg, the best source pre-routing, and
// forces Connection: close so the header and lifecycle stay in sync.
void Server::_sendError(Connection* conn, int status) {
    conn->keep_alive = false;
    Response resp = make_error_response(status, conn->server_cfg);
    resp.setHeader("Connection", "close");
    _queueResponse(conn, resp.toString());
}

// CGI stdout pipe readable (IServerCallback): delegate to the CGI subsystem.
void Server::onCgiPipeIN(Connection* conn) {
    _cgi.onPipeIN(conn);
}

// Largest effective client_max_body_size over all servers/locations on `port`, the
// early-413 guard. A location's (size_t)-1 inherits the server limit (see
// RouteContextBuilder::resolveSize); returns (size_t)-1 if any route is unlimited.
size_t Server::_maxBodyLimitForPort(int port) const {
    size_t max = 0;
    for (size_t i = 0; i < _configs.size(); ++i) {
        const ServerConfig& cfg = _configs[i];
        if (cfg.port != port)
            continue;
        if (cfg.locations.empty()) {
            // No route: requests resolve to the server-level limit.
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

// Largest client_max_header_size over all servers on `port` (no per-location
// override), the early-431 guard. Returns (size_t)-1 if unlimited.
size_t Server::_maxHeaderLimitForPort(int port) const {
    size_t max = 0;
    for (size_t i = 0; i < _configs.size(); ++i) {
        const ServerConfig& cfg = _configs[i];
        if (cfg.port != port)
            continue;
        if (cfg.client_max_header_size == (size_t)-1)
            return (size_t)-1;
        if (cfg.client_max_header_size > max)
            max = cfg.client_max_header_size;
    }
    return max;
}

// Throttled (~1Hz) timeout sweep enforcing the "never hang" rule: aborts timed-out
// CGI exchanges with 504, and closes clients past their deadline.
void Server::_checkTimeouts() {
    std::time_t now = std::time(NULL);
    if (now - _last_timeout_check < 1)
        return;
    _last_timeout_check = now;

    const std::time_t REQUEST_TIMEOUT = 60;  // absolute cap on receiving one request
    const std::time_t CLIENT_TIMEOUT  = 30;  // idle client with no request in flight

    // Collect first, then act: idle clients are closed via onCloseConnection (which
    // mutates _connections), so we can't close them while iterating the map.
    std::vector<Connection*> expired;
    std::vector<Connection*> idle;
    for (std::map<int, Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it) {
        Connection* conn = it->second;
        if (_cgi.isActive(conn)) {
            if (_cgi.isExpired(conn, now))
                expired.push_back(conn);
        } else if (conn->first_byte_time != 0) {
            // Request mid-arrival: absolute deadline from first_byte_time (not reset
            // by later bytes), so a slowloris drip can't defer it indefinitely.
            if ((now - conn->first_byte_time) >= REQUEST_TIMEOUT)
                idle.push_back(conn);
        } else {
            // No request in flight (silent or draining): close on idle inactivity.
            if ((now - conn->last_activity) >= CLIENT_TIMEOUT)
                idle.push_back(conn);
        }
    }

    for (size_t i = 0; i < expired.size(); ++i)
        _cgi.timeout(expired[i]);
    for (size_t i = 0; i < idle.size(); ++i) {
        Logger::log(Logger::INFO, "Closing idle client fd=" +
                    to_string(idle[i]->getFd()));
        onCloseConnection(idle[i]->getFd(), idle[i]);
    }
}

// CGI stdin pipe writable (IServerCallback): delegate to the CGI subsystem.
void Server::onCgiPipeOUT(Connection* conn) {
    _cgi.onPipeOUT(conn);
}
