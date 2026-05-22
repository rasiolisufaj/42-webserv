#pragma once
#include "IServerCallback.hpp"
#include <stdint.h>

class IEventHandler
{
    public:
        /*
            * Virtual destructor to allow safe deletion through an IEventHandler pointer.
        */
        virtual ~IEventHandler() {};

        /*
            * Handle an event by calling the appropriate method on the provided IServerCallback.
            *
            * Parameters:
            * - event: bitmask of events (e.g., EPOLLIN, EPOLLOUT) that occurred on the associated file descriptor
            * - callback: pointer to an IServerCallback implementation to call based on the event type
        */
        virtual void handle(uint32_t event, IServerCallback *callback) = 0;
};
