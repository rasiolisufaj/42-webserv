#pragma once

#include <vector>
#include <map>
#include <set>
#include <string>
#include <ctime>
#include "config/ServerConfig.hpp"
#include "server/IServerCallback.hpp"
#include "server/EpollWrapper.hpp"
#include "server/Socket.hpp"
#include "http/Request.hpp"
#include "http/RequestContext.hpp"
#include "server/CgiManager.hpp"

class Connection;
struct RouteMatch;

class Server : public IServerCallback {
    // Drives the CGI exchange on the same epoll loop; reuses _queueResponse / _sendError.
    friend class CgiManager;

    private:
        const std::vector<ServerConfig>& _configs;       // parsed server configs
        EpollWrapper                     _epoll;
        std::vector<Socket*>             _listeners;
        std::map<int, int>               _listenerPort;  // listener fd -> port
        std::map<int, Connection*>       _connections;
        bool                             _running;
        std::time_t                      _last_timeout_check;  // throttles sweep to ~1Hz
        CgiManager                       _cgi;            // owns the CGI lifecycle
        std::set<const void*>            _retired;        // handlers freed in this epoll batch

        // Result of inspecting a partially-read client buffer (see _requestReadiness).
        enum RequestReadiness {
            REQ_INCOMPLETE,  // need more data
            REQ_COMPLETE,    // full request buffered, ready to parse
            REQ_ERROR        // malformed / bad length / too large
        };

        // Creates a listening socket per ServerConfig and registers it with epoll.
        void _initListeners();

        // Drives one buffered request: completeness check -> parse -> route,
        // then dispatches to CGI or builds a static response.
        void _processRequest(Connection* conn);

        // Inspects the buffer before parsing: headers complete (CRLFCRLF), body
        // complete (Content-Length or chunked), and within this port's size limits.
        // Sets errorStatus (400/413) on REQ_ERROR and consumed on REQ_COMPLETE.
        RequestReadiness _requestReadiness(const std::vector<char>& buffer, int port,
                                           int& errorStatus, size_t& consumed) const;

        // Checks a chunked body: validates framing and enforces the size limit.
        RequestReadiness _chunkedReadiness(const std::vector<char>& buffer, size_t body_start,
                                           size_t limit, int& errorStatus,
                                           size_t& consumed) const;

        // Copies `response` into out_buffer, moves to SENDING_RESPONSE, and arms
        // EPOLLOUT. The single exit point for finished static/CGI/error replies.
        void _queueResponse(Connection* conn, const std::string& response);

        // Periodic (~1Hz) timeout sweep: aborts idle/over-cap CGI exchanges with
        // 504, and closes clients idle past CLIENT_TIMEOUT (anti-slowloris).
        void _checkTimeouts();

        // Largest client_max_body_size over all servers/locations on `port`; an
        // early-413 guard. (size_t)-1 if any matching route is unlimited.
        size_t _maxBodyLimitForPort(int port) const;

        // Largest client_max_header_size over all servers on `port` (no per-location
        // override); an early-431 guard. (size_t)-1 if unlimited.
        size_t _maxHeaderLimitForPort(int port) const;

        // Queues an error response (via make_error_response, custom error_page when
        // set) and arms EPOLLOUT; always sets Connection: close.
        void _sendError(Connection* conn, int status);

        // Builds the HTTP response for a routed (non-CGI) request: static files,
        // directory listings, and 405/404/500 errors.
        std::string _buildResponse(const RequestContext& ctx, const RouteMatch& match,
                                   bool keepAlive) const;

        // Whether `method` is allowed on the matched route (same resolution as
        // RouteContextBuilder). Gates CGI dispatch; a disallowed method falls
        // through to the static path's canonical 405.
        bool _isMethodAllowed(const RouteMatch& match, const std::string& method) const;

        // Records a handler (Connection or CGI-pipe wrapper) freed mid-batch so
        // run() skips its stale events, avoiding a use-after-free. Cleared each loop.
        void _retireHandler(const void* handler);

        Server(const Server& other);
        Server& operator=(const Server& rhs);

    public:
        // Constructs the server with the parsed configuration.
        explicit Server(const std::vector<ServerConfig>& configs);

        // Destroys all listeners and connections.
        ~Server();

        // Main event loop: waits for epoll events and dispatches them until stopped.
        void run();

        // IServerCallback implementation

        // New client accepted: creates a Connection and registers it with epoll.
        virtual void onNewConnection(int client_fd, int listener_fd);

        // Client socket readable; dispatched to Connection.
        virtual void onClientIN(Connection* conn);

        // Client socket writable; dispatched to Connection.
        virtual void onClientOUT(Connection* conn);

        // Closes a connection: removes from epoll and frees resources.
        virtual void onCloseConnection(int fd, Connection* conn);

        // CGI stdout pipe readable; delegates to _cgi.
        virtual void onCgiPipeIN(Connection* conn);

        // CGI stdin pipe writable; delegates to _cgi.
        virtual void onCgiPipeOUT(Connection* conn);
};
