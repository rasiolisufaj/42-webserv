#pragma once

#include <vector>
#include <map>
#include <string>
#include "config/ServerConfig.hpp"
#include "server/IServerCallback.hpp"
#include "server/EpollWrapper.hpp"
#include "server/Socket.hpp"

class Connection;

class Server : public IServerCallback {
    private:
        const std::vector<ServerConfig>& _configs;
        EpollWrapper                     _epoll;
        std::vector<Socket*>             _listeners;
        std::map<int, Connection*>       _connections;
        bool                             _running;

        /*
         * Initializes listening sockets for each ServerConfig.
         * Registers each socket with epoll.
         */
        void _initListeners();

        /*
         * Closes and removes a connection from tracking.
         */
        void _closeConnection(int fd, Connection* conn);

        /*
         * Selects the appropriate ServerConfig based on the request Host header.
         * For Phase 5: returns first config (virtual hosting in Phase 6).
         */
        const ServerConfig& _selectConfig(Connection* conn) const;

        Server(const Server& other);
        Server& operator=(const Server& rhs);

    public:
        /*
         * Constructs the server with the parsed configuration.
         */
        explicit Server(const std::vector<ServerConfig>& configs);

        /*
         * Destroys all listeners and connections.
         */
        ~Server();

        /*
         * Main event loop: waits for epoll events and dispatches them.
         * Runs until _running is set to false.
         */
        void run();

        // IServerCallback implementation

        /*
         * Called when a listening socket accepts a new client.
         * Creates a new Connection and registers it with epoll.
         */
        virtual void onNewConnection(int client_fd, int listener_fd);

        /*
         * Called when a client socket is readable.
         * Dispatched to Connection for handling.
         */
        virtual void onClientIN(Connection* conn);

        /*
         * Called when a client socket is writable.
         * Dispatched to Connection for handling.
         */
        virtual void onClientOUT(Connection* conn);

        /*
         * Called when a connection must be closed.
         * Removes from epoll and cleans up resources.
         */
        virtual void onCloseConnection(int fd, Connection* conn);

        /*
         * Called when a CGI process stdout pipe is readable.
         * (Not implemented in Phase 5; stub for now)
         */
        virtual void onCgiPipeIN(Connection* conn);

        /*
         * Called when a CGI process stdin pipe is writable.
         * (Not implemented in Phase 5; stub for now)
         */
        virtual void onCgiPipeOUT(Connection* conn);
};
