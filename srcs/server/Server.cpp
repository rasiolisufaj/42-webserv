#include "server/Server.hpp"
#include "server/Connection.hpp"
#include "utils/Logger.hpp"
#include "http/Request.hpp"
#include "http/RequestContext.hpp"
#include "http/ConfigMatcher.hpp"
#include "http/Headers.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <ctime>

/*
 * Constructor: stores reference to configs, initializes members.
 */
Server::Server(const std::vector<ServerConfig>& configs)
    : _configs(configs), _running(false) {}

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
 * Initializes listening sockets for each server config.
 */
void Server::_initListeners() {
    for (size_t i = 0; i < _configs.size(); ++i) {
        const ServerConfig& cfg = _configs[i];

        // Create socket and setup
        Socket* listener = new Socket(cfg.host, to_string(cfg.port));
        try {
            listener->setup();
        } catch (const std::exception& e) {
            Logger::log(Logger::ERROR, std::string("Failed to setup listening socket: ") + e.what());
            delete listener;
            throw;
        }

        // Register with epoll
        _epoll.addFd(listener->getFd(), EPOLLIN, listener);
        _listeners.push_back(listener);

        Logger::log(Logger::INFO,
            "Listening on " + cfg.host + ":" + to_string(cfg.port) +
            " (fd=" + to_string(listener->getFd()) + ")");
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

        if (nfds == 0) {
            // Timeout - can be used for periodic tasks (timeouts, cleanup)
            // For now, just continue
            continue;
        }

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

    // Find which listener this came from (to select right config)
    const ServerConfig* cfg = NULL;
    for (size_t i = 0; i < _listeners.size(); ++i) {
        if (_listeners[i]->getFd() == listener_fd) {
            cfg = &_configs[i];
            break;
        }
    }

    if (!cfg) {
        Logger::log(Logger::ERROR, "Could not find config for listener fd: " + to_string(listener_fd));
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
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        Logger::log(Logger::WARNING, "recv error: " + std::string(strerror(errno)));
        conn->setState(Connection::CLOSED);
        return;
    }

    conn->in_buffer.insert(conn->in_buffer.end(), buf, buf + n);
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
        _sendError(conn, fd, 400, "Bad Request");
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
            _sendError(conn, fd, 400, "Bad Request");
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
        _sendError(conn, fd, 400, "Bad Request");
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
        _sendError(conn, fd, 400, "Bad Request");
        return;
    }

    // Route: ConfigMatcher selects the matching server block and location block.
    ConfigMatcher matcher;
    const ServerConfig* matched_server = NULL;
    const Location*     matched_location = NULL;
    try {
        RouteMatch route = matcher.match(ctx, _configs);
        matched_server   = route.server;
        matched_location = route.location;
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, std::string("Routing error: ") + e.what());
        std::string msg = e.what();
        if (msg.find("404") != std::string::npos)
            _sendError(conn, fd, 404, "Not Found");
        else
            _sendError(conn, fd, 400, "Bad Request");
        return;
    }

    std::string response = _buildResponse(req, ctx, matched_server, matched_location, conn);
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
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        Logger::log(Logger::WARNING, "send failed: " + std::string(strerror(errno)));
        conn->setState(Connection::CLOSED);
        return;
    }

    conn->write_idx += static_cast<size_t>(n);

    if (conn->write_idx >= conn->out_buffer.size()) {
        Logger::log(Logger::DEBUG, "Response fully sent to fd=" + to_string(fd));
        conn->setState(Connection::CLOSED);
    }
}

/*
 * Called when a connection needs to be closed.
 */
void Server::onCloseConnection(int fd, Connection* conn) {
    (void)conn;
    Logger::log(Logger::DEBUG, "Closing connection fd=" + to_string(fd));

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
 * Builds an HTTP response. Phase 6 stub: echoes method/target.
 * Phase 7+: real file serving, CGI, error pages, redirects.
 */
std::string Server::_buildResponse(const Request& req, const RequestContext& ctx,
                                    const ServerConfig* cfg, const Location* loc,
                                    Connection* conn) const {
    (void)ctx;
    (void)cfg;
    (void)loc;
    (void)conn;

    std::string method = req.requestLine().method();
    std::string target = req.requestLine().target();

    std::string body = "Method: " + method + "\nTarget: " + target + "\n";
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: " + to_string(body.size()) + "\r\n"
           "Connection: close\r\n"
           "\r\n" + body;
}

/*
 * Sends a minimal error response and arms EPOLLOUT.
 * onClientOUT will drain and then set state=CLOSED; Connection::handle() closes.
 */
void Server::_sendError(Connection* conn, int fd, int status, const std::string& reason) {
    std::string response = "HTTP/1.1 " + to_string(status) + " " + reason +
                           "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    conn->out_buffer.assign(response.begin(), response.end());
    conn->write_idx = 0;
    conn->setState(Connection::SENDING_RESPONSE);
    _epoll.modifyFd(fd, EPOLLOUT | EPOLLRDHUP, conn);
}

/*
 * Placeholder for CGI stdout pipe (Phase 7).
 */
void Server::onCgiPipeIN(Connection* conn) {
    (void)conn;
    Logger::log(Logger::WARNING, "onCgiPipeIN not implemented (Phase 7)");
}

/*
 * Placeholder for CGI stdin pipe (Phase 7).
 */
void Server::onCgiPipeOUT(Connection* conn) {
    (void)conn;
    Logger::log(Logger::WARNING, "onCgiPipeOUT not implemented (Phase 7)");
}

