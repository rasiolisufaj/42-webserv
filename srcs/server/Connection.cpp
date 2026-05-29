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

void Connection::handle(uint32_t event, IServerCallback &callback)
{
    if (event & (EPOLLERR | EPOLLHUP))
    {
        Logger::log(Logger::DEBUG,
                    "Epoll error/hangup event: " + to_string(event));
        callback.onCloseConnection(this->_fd, this);
        return;
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
    }
    
    if (event & EPOLLOUT)
    {
        callback.onClientOUT(this);
    }

    if (this->state == CLOSED)
        callback.onCloseConnection(this->_fd, this);
}
