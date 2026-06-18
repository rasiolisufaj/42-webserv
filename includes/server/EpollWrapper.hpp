#pragma once
#include <vector>
#include <stdint.h>
#include <sys/epoll.h>

class EpollWrapper
{
    private:
        EpollWrapper(const EpollWrapper &other);
        EpollWrapper &operator=(const EpollWrapper &rhs);

        int _epoll_fd;
        static const int MAX_EVENTS = 1024;
        std::vector<struct epoll_event> _events; // Internal storage for epoll events, used in wait() and getEvents()
    
    public:
        EpollWrapper();
        ~EpollWrapper();

        /*
            * Registers an fd in the epoll instance with a handler pointer.
            *
            * Parameters:
            * - fd: file descriptor to watch
            * - events: epoll event mask (EPOLLIN/EPOLLOUT/...)
            * - data: pointer to an event handler (stored in epoll_event.data.ptr)
        */
        void addFd(int fd, uint32_t events, void *data);

        /*
            * Modifies the events and/or handler pointer associated with an already registered fd.
        */
        void modifyFd(int fd, uint32_t events, void *data);

        /*
            * Unregisters an fd from the epoll instance.
        */
        void removeFd(int fd);

        /*
            * Waits for epoll events.
            *
            * Parameters:
            * - timeout_ms: timeout in milliseconds (or -1 for infinite)
            *
            * Returns:
            * - number of fds with events, 0 on timeout, or -1 on error
        */
        int wait(int timeout_ms);
        
        /*
            * Returns a pointer to the internal event array filled by the last wait() call, or NULL if wait() has not been called yet.
        */
        const struct epoll_event *getEvents() const;
};
