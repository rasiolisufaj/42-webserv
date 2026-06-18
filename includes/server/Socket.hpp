#pragma once
#include "IEventHandler.hpp"
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>

class Socket : public IEventHandler
{
    private:
        Socket(const Socket &other);
        Socket &operator=(const Socket &rhs);

        int _fd;
        std::string _host;
        std::string _port;
        struct sockaddr_in _peer_addr;
        socklen_t _peer_addr_len;

    public:
        /*
            * Constructor that initializes the socket with the specified host and port.
            *
            * Parameters:
            * - host: IP address to bind to (e.g., 0.0.0.0 for all interfaces)"
            * - port: Port number to listen on (e.g., "8080")
        */
        Socket(const std::string &host, const std::string &port);

        /*
            * Destructor that closes the socket file descriptor if it is open.
        */
        ~Socket();

        /*
            * Creates the socket, bind it to the address, makes it non-blocking, and starts listening for incoming connections.
        */
        void setup();

        /*
            * Accepts a new client connection and configures the new client socket as non-blocking. 
            * Returns the file descriptor of the new client socket, or -1 on error.
        */
        int acceptConnection();

        /*
            * Returns the underlying socket file descriptor.
        */
        int getFd() const;

        /*
            * IEventHandler entry point: accepts clients and forwards them to the server.
        */
        virtual void handle(uint32_t event, IServerCallback &callback);
};
