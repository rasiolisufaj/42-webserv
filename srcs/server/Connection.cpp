#include "server/Connection.hpp"
#include "server/IServerCallback.hpp"
#include "utils/Logger.hpp"
#include <cerrno>
#include <cstring>
#include <iostream>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <unistd.h>

/*
    * Constructor initializes the connection with the accepted client socket fd and server configuration.
    * It also initializes the state machine and timeout management variables.
*/

Connection::Connection(int fd, const ServerConfig *serverCfg)
    : _fd(fd), server_cfg(serverCfg), read_idx(0), write_idx(0),
      state(READING_HEADER), last_activity(std::time(NULL)),
      first_byte_time(0), header_complete(false), closeAfterWrite(false),
      cgi_in_fd(-1), cgi_out_fd(-1), cgi_pid(-1), cgi_start_time(0),
      cgi_sent_idx(0), cgi_in_wrapper(NULL), cgi_out_wrapper(NULL),
      file_fd(-1), file_bytes_sent(0), file_total_size(0), is_streaming_file(false),
      header_check_len(0), chunk_raw_offset(0), expected_request_total(0),
      upload_fd(-1), body_bytes_expected(0), body_bytes_received(0), is_streaming_upload(false) {}

Connection::~Connection()
{
    if (this->_fd != -1)
    {
        close(this->_fd);
        this->_fd = -1;
    }
    if (this->file_fd != -1)
    {
        close(this->file_fd);
        this->file_fd = -1;
    }
    if (this->upload_fd != -1)
    {
        close(this->upload_fd);
        this->upload_fd = -1;
    }
    // CGI cleanup: close parent pipe ends, reap the child, free epoll wrappers.
    // The Server is responsible for removing these fds from epoll before the
    // Connection is destroyed (see Server::_teardownCgi); here we guarantee the
    // OS resources are released even if that path was missed.
    if (this->cgi_in_fd != -1)
    {
        close(this->cgi_in_fd);
        this->cgi_in_fd = -1;
    }
    if (this->cgi_out_fd != -1)
    {
        close(this->cgi_out_fd);
        this->cgi_out_fd = -1;
    }
    if (this->cgi_pid > 0)
    {
        kill(this->cgi_pid, SIGKILL);
        waitpid(this->cgi_pid, NULL, 0);
        this->cgi_pid = -1;
    }
    delete this->cgi_in_wrapper;
    this->cgi_in_wrapper = NULL;
    delete this->cgi_out_wrapper;
    this->cgi_out_wrapper = NULL;
}

/*
    * Converts a ConnectionState enum value to a printable string.
*/

static const char* getState(int state)
{
    switch (state)
    {
    case Connection::READING_HEADER:
        return "READING_HEADER";
    case Connection::READING_BODY:
        return "READING_BODY";
    case Connection::SENDING_RESPONSE:
        return "SENDING_RESPONSE";
    case Connection::CGI_WRITING_BODY:
        return "CGI_WRITING_BODY";
    case Connection::CGI_READING_OUTPUT:
        return "CGI_READING_OUTPUT";
    case Connection::CLOSED:
        return "CLOSED";
    default:
        return "UNKNOWN_STATE";
    }
}

/*
    * Updates the state machine and logs the transition in DEBUG.
*/
void Connection::setState(ConnectionState new_state)
{
    Logger::log(Logger::DEBUG, "Fd: " + to_string(this->_fd) + " state changed from: " + getState(this->state) + " to: " + getState(new_state));
    this->state = new_state;
}

/*
    * Returns the client socket file descriptor.
*/
int Connection::getFd() const {return this->_fd;}

/*
    * Forwards epoll events on a CGI pipe to the matching Server callback.
    * The write end (stdin, !is_read_end) drives onCgiPipeOUT; the read end
    * (stdout, is_read_end) drives onCgiPipeIN. Error/hangup is forwarded to the
    * same side so the Server can finalize or abort the CGI exchange.
*/
void CgiPipeWrapper::handle(uint32_t event, IServerCallback &callback)
{
    if (is_read_end)
    {
        if (event & (EPOLLIN | EPOLLERR | EPOLLHUP))
            callback.onCgiPipeIN(this->conn);
    }
    else
    {
        if (event & (EPOLLOUT | EPOLLERR | EPOLLHUP))
            callback.onCgiPipeOUT(this->conn);
    }
}

void Connection::handle(uint32_t event, IServerCallback &callback)
{
    if (event & (EPOLLERR | EPOLLHUP))
    {
        Logger::log(Logger::DEBUG,
                    "Epoll error/hangup event: " + to_string(event));
        callback.onCloseConnection(this->_fd, this);
        return; // 'this' deleted — must not access any member after this point
    }

    if (event & EPOLLRDHUP)
    {
        this->closeAfterWrite = true;
        if (!(event & (EPOLLIN | EPOLLOUT)))
        {
            this->state = CLOSED;
            callback.onCloseConnection(this->_fd, this);
            return;
        }
    }

    if (event & EPOLLIN)
    {
        callback.onClientIN(this);
        if (this->state == CLOSED)
        {
            callback.onCloseConnection(this->_fd, this);
            return;
        }
    }

    if (event & EPOLLOUT)
    {
        callback.onClientOUT(this);
        if (this->state == CLOSED)
        {
            callback.onCloseConnection(this->_fd, this);
            return;
        }
    }
}
