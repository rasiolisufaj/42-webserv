#pragma once

class Connection;

class IServerCallback
{
public:
    /*
        * Virtual destructor to allow safe deletion through an IServerCallback pointer.
    */
    virtual ~IServerCallback() {};

    /*
        * Called when a listening socket accepts a new connection. The server will pass the file descriptor of the new client socket and the listener socket that accepted the connection.
    */
    virtual void onNewConnection(int client_fd, int listener_fd) = 0;

    /*
        * Called when a client socket is readable (EPOLLIN).
        *
        * Parameters:
        * - conn: connection associated with the client fd
    */
    virtual void onClientIN(Connection *conn) = 0;

    /*
        * Called when a client socket is writable (EPOLLOUT).
        *
        * Parameters:
        * - conn: connection associated with the client fd
    */
    virtual void onClientOUT(Connection *conn) = 0;

    /*
        * Called when a connection must be closed and cleaned up.
        *
        * Parameters:
        * - fd: file descriptor being closed (usually conn-getFd())
        * - conn: connection object to delete/cleanup
    */
    virtual void onCloseConnection(int fd, Connection *conn) = 0;

    /*
        * Called when a CGI process (stdout) pipe is readable (EPOLLIN).
        *
        * Parameters:
        * - conn: connection associated with the CGI process
    */
    virtual void onCgiPipeIN(Connection *conn) = 0;

    /*
        * Called when a CGI process (stdin) pipe is writable (EPOLLOUT).
        *
        * Parameters:
        * - conn: connection associated with the CGI process
    */
    virtual void onCgiPipeOUT(Connection *conn) = 0;
};
