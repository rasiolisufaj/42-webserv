#include "server/EpollWrapper.hpp"
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <unistd.h>

/*
 * Initializes the epoll instance and pre-allocates the event array.
*/
EpollWrapper::EpollWrapper() : _epoll_fd(-1)
{
    this->_epoll_fd = epoll_create(1);
    if (this->_epoll_fd == -1)
    {
        throw std::runtime_error("epoll_create: " + std::string(strerror(errno)));
    }
    this->_events.resize(MAX_EVENTS);
}

/*
 * Closes the epoll file descriptor.
*/
EpollWrapper::~EpollWrapper()
{
    if (this->_epoll_fd != -1)
    {
        close(this->_epoll_fd);
        this->_epoll_fd = -1;
    }
}

/*
 * Registers an fd in the epoll instance with handler pointer stored in epoll_event.data.ptr.
*/
void EpollWrapper::addFd(int fd, uint32_t events, void *data)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = data;

    if (epoll_ctl(this->_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        throw std::runtime_error("epoll_ctl ADD: " + std::string(strerror(errno)));
    }
}

/*
 * Modifies the events and/or handler pointer associated with an already registered fd.
*/
void EpollWrapper::modifyFd(int fd, uint32_t events, void *data)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = data;

    if (epoll_ctl(this->_epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        throw std::runtime_error("epoll_ctl MOD: " + std::string(strerror(errno)));
    }
}

/*
 * Unregisters an fd from the epoll instance.
*/
void EpollWrapper::removeFd(int fd)
{
    if (epoll_ctl(this->_epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
    {
        if (errno == ENOENT || errno == EBADF)
        {
            return;
        }
        throw std::runtime_error("epoll_ctl DEL: " + std::string(strerror(errno)));
    }
}

/*
 * Waits for epoll events.
*/
int EpollWrapper::wait(int timeout_ms)
{
    int nfds;
    do {
        nfds = epoll_wait(this->_epoll_fd, &this->_events[0], MAX_EVENTS, timeout_ms);
    } while (nfds == -1 && errno == EINTR);
    return nfds;
}

/*
 * Returns a pointer to the internal event array filled by the last wait() call, or NULL if wait() has not been called yet.
*/
const struct epoll_event *EpollWrapper::getEvents() const
{
    if (this->_events.empty())
    {
        return NULL;
    }
    return &this->_events[0];
}
