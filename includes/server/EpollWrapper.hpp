#pragma once
#include <map>
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
        std::vector<struct epoll_event> _events;
        std::map<int, void *> _fd_to_data;
    
    public:
        EpollWrapper();
        ~EpollWrapper();

        /*
            * Registers an fd in the epoll instance and associates user data with it.
            *
            * Parameters:
            * - fd: file descriptor to watch
            * - events: epoll event mask (EPOLLIN/EPOLLOUT/...)
            * - data: pointer to an event handler or user state (stored separately)
        */
        void addFd(int fd, uint32_t events, void *data);

        /*
            * Modifies the events and/or user data associated with an already registered fd.
        */
        void modifyFd(int fd, uint32_t events, void *data);

        /*
            * Unregisters an fd from the epoll instance and removes its associated user data.
        */
        void removeFd(int fd);

        /*
            * Retrieves the user data pointer associated with a registered fd, or NULL if the fd is not registered.
        */
        void *getData(int fd) const;

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
