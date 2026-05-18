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
    this->_fd_to_data.clear();
    if (this->_epoll_fd != -1)
    {
        close(this->_epoll_fd);
        this->_epoll_fd = -1;
    }
}

/*
 * Registers an fd in the epoll instance and associates user data with it.
*/
void EpollWrapper::addFd(int fd, uint32_t events, void *data)
{
    struct epoll_event ev;
    ev.events = events;
    // store fd instead of raw pointer to avoid use-after-free in the epoll loop.
    ev.data.fd = fd;

    if (epoll_ctl(this->_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        throw std::runtime_error("epoll_ctl ADD: " + std::string(strerror(errno)));
    }
    this->_fd_to_data[fd] = data;
}

/*
 * Modifies the events and/or user data associated with an already registered fd.
*/
void EpollWrapper::modifyFd(int fd, uint32_t events, void *data)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(this->_epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        throw std::runtime_error("epoll_ctl MOD: " + std::string(strerror(errno)));
    }
    this->_fd_to_data[fd] = data;
}

/*
 * Unregisters an fd from the epoll instance and removes its associated user data.
*/
void EpollWrapper::removeFd(int fd)
{
    if (epoll_ctl(this->_epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1)
    {
        if (errno == ENOENT || errno == EBADF)
        {
            // fd was not registered or already closed, ignore
            this->_fd_to_data.erase(fd);
            return;
        }
        throw std::runtime_error("epoll_ctl DEL: " + std::string(strerror(errno)));
    }
    this->_fd_to_data.erase(fd);
}

/*
 * Retrieves the user data pointer associated with a registered fd, or NULL if the fd is not registered.
*/
void *EpollWrapper::getData(int fd) const
{
    std::map<int, void *>::const_iterator it = this->_fd_to_data.find(fd);
    if (it != this->_fd_to_data.end())
    {
        return it->second;
    }
    return NULL;
}

/*
 * Waits for epoll events.
*/
int EpollWrapper::wait(int timeout_ms)
{
    int nfds = epoll_wait(this->_epoll_fd, &this->_events[0], MAX_EVENTS, timeout_ms);
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
