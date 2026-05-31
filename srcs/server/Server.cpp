#include "server/Server.hpp"
#include "server/Connection.hpp"
#include "utils/Logger.hpp"
#include <sys/epoll.h>
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
    Logger::log(Logger::DEBUG, "New connection accepted (fd=" + to_string(client_fd) + ")");
}

/*
 * Called when a client socket is readable.
 * For Phase 5: read and buffer the request; send hardcoded response.
 */
void Server::onClientIN(Connection* conn) {
    if (!conn) {
        Logger::log(Logger::ERROR, "onClientIN: conn is null");
        return;
    }

    int fd = conn->getFd();
    Logger::log(Logger::DEBUG, "onClientIN fd=" + to_string(fd));

    // TODO Phase 6: Parse request and build response
    // For Phase 5: hardcoded OK response
    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Hello, World!";

    conn->out_buffer.resize(response.size());
    for (size_t i = 0; i < response.size(); ++i) {
        conn->out_buffer[i] = response[i];
    }
    conn->write_idx = 0;

    // Switch to writing mode
    conn->setState(Connection::SENDING_RESPONSE);
    _epoll.modifyFd(fd, EPOLLOUT | EPOLLRDHUP, conn);
}

/*
 * Called when a client socket is writable.
 * Sends buffered response data.
 */
void Server::onClientOUT(Connection* conn) {
    if (!conn) {
        Logger::log(Logger::ERROR, "onClientOUT: conn is null");
        return;
    }

    int fd = conn->getFd();
    Logger::log(Logger::DEBUG, "onClientOUT fd=" + to_string(fd));

    // Send from out_buffer
    if (conn->out_buffer.empty() || conn->write_idx >= conn->out_buffer.size()) {
        // Nothing to send, close connection
        conn->setState(Connection::CLOSED);
        onCloseConnection(fd, conn);
        return;
    }

    size_t remaining = conn->out_buffer.size() - conn->write_idx;
    ssize_t n = send(fd, &conn->out_buffer[conn->write_idx], remaining, MSG_NOSIGNAL);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Can't write now, epoll will fire again when ready
            return;
        }
        Logger::log(Logger::WARNING, "send failed: " + std::string(strerror(errno)));
        conn->setState(Connection::CLOSED);
        onCloseConnection(fd, conn);
        return;
    }

    conn->write_idx += n;

    if (conn->write_idx >= conn->out_buffer.size()) {
        // All data sent, close connection
        Logger::log(Logger::DEBUG, "Response fully sent to fd=" + to_string(fd));
        conn->setState(Connection::CLOSED);
        onCloseConnection(fd, conn);
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

/*
 * Selects a ServerConfig based on connection (Phase 6: virtual hosting).
 * For Phase 5: just return first config.
 */
const ServerConfig& Server::_selectConfig(Connection* conn) const {
    (void)conn;
    return _configs[0];
}
