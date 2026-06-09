#pragma once
#include "IEventHandler.hpp"
#include "cgi/CgiJob.hpp"
#include <cstddef>
#include <ctime>
#include <vector>
#include <string>
struct ServerConfig;
class Connection;

class CgiPipeWrapper : public IEventHandler
{
    public:
        Connection *conn;
        bool is_read_end;

        /*
            * Constructor for creating a new CgiPipeWrapper.
            * Parameters:
            * - c: pointer to the associated Connection object
            * - is_read: true if this wrapper is for the CGI stdout pipe, false if for stdin
        */
        CgiPipeWrapper(Connection *c, bool is_read) : conn(c), is_read_end(is_read) {}

        /*
            * Forwards epoll events to the correct CGI callback on the Server.
        */
        virtual void handle(uint32_t event, IServerCallback &callback);
};

class Connection : public IEventHandler
{
    private:
        int _fd;
        Connection(const Connection &other);
        Connection &operator=(const Connection &rhs);

    public:
        const ServerConfig   *server_cfg;
       
        // INPUT: what the client sent
        size_t   read_idx;
        std::vector<char>    in_buffer;

        // OUTPUT: what we will send back to the client
        size_t  write_idx;
        std::vector<char>    out_buffer;
        
        enum ConnectionState {
            READING_HEADER,
            READING_BODY,
            SENDING_RESPONSE,
            CGI_WRITING_BODY,
            CGI_READING_OUTPUT,
            CLOSED
        };
        ConnectionState state;
        
        // TIMEOUT MANAGEMENT
        std::time_t last_activity;
        std::time_t first_byte_time;

        // KEEP-ALIVE: whether to keep the connection open after the current
        // response is fully sent. Set per request from the HTTP version + the
        // Connection header; forced false on errors and on EPOLLRDHUP.
        bool keep_alive;

        // Cgi Process Management
        int cgi_in_fd;
        int cgi_out_fd;
        pid_t cgi_pid;
        std::time_t cgi_start_time;
        std::vector<char> cgi_raw_output;

        CgiJob cgi_job;
        size_t  cgi_sent_idx;
        CgiPipeWrapper *cgi_in_wrapper;
        CgiPipeWrapper *cgi_out_wrapper;

        // METHODS

         /*
            * Constructor for creating a new connection.
            * Parameters:
            * - fd: file descriptor of the accepted client socket
            * - serverCfg: pointer to the server configuration, used for managing timeouts and other connection
        */
        Connection(int fd, const ServerConfig *serverCfg);
        ~Connection();

        /*
            * Returns the client socket fd for this connection.
        */
        int  getFd() const;

        /*
            * Updates the connection state machine (and emits a DEBUG log).
        */
        void setState(ConnectionState new_state);
        
        /*
            * IEventHandler entry point: handles events on the client socket, including reading requests, writing responses, and managing CGI processes.
        */
        virtual void handle(uint32_t event, IServerCallback &callback);
};
