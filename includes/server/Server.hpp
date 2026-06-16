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
    // CgiManager drives the CGI exchange on top of the same epoll loop and reuses
    // the single-owner _queueResponse / _sendError transitions below.
    friend class CgiManager;

    private:
        // Stores all parsed server configurations (from config file)
        const std::vector<ServerConfig>& _configs;
        EpollWrapper                     _epoll;
        std::vector<Socket*>             _listeners;
        std::map<int, int>               _listenerPort;  // listener fd -> port
        std::map<int, Connection*>       _connections;
        bool                             _running;
        std::time_t                      _last_timeout_check;  // throttles the sweep to ~1Hz
        CgiManager                       _cgi;  // owns the CGI lifecycle (declared after _epoll)
        // Handler pointers (Connection / CgiPipeWrapper) freed while the current
        // epoll batch is still being processed. epoll_wait returns a snapshot of
        // ready events, but handling one event can free a handler that a later
        // event in the same batch points to (e.g. a client EPOLLHUP tears down a
        // connection while its CGI pipe still has a queued event, or _checkTimeouts
        // closes a connection that has a pending event). run() skips any event
        // whose handler is in this set, preventing a use-after-free. Cleared at the
        // start of every loop iteration, so it only ever holds this batch's frees.
        std::set<const void*>            _retired;

        // Result of inspecting a partially-read client buffer (see _requestReadiness).
        enum RequestReadiness {
            REQ_INCOMPLETE,  // headers/body not fully received yet — wait for more data
            REQ_COMPLETE,    // a full request is buffered and can be parsed
            REQ_ERROR        // malformed headers / bad length / too large — send error
        };

        /*
         * Initializes listening sockets for each ServerConfig.
         * Registers each socket with epoll.
         */
        void _initListeners();

        /*
         * Drives one buffered client request: completeness check -> parse -> route
         * -> CGI/static dispatch (or error). Called by onClientIN after each read,
         * once the new bytes are appended. Keeping this out of onClientIN separates
         * the socket-I/O concern (recv + buffering + timestamps) from the HTTP
         * concern handled here.
         */
        void _processRequest(Connection* conn);

        /*
         * Inspects the bytes buffered so far for one client and decides whether a
         * complete request has arrived. Does not mutate any state. Scans the raw
         * in_buffer in place (only the header block is ever copied, never the body),
         * so it stays cheap as a large body streams in over many reads. Returns
         * REQ_INCOMPLETE while still waiting, REQ_COMPLETE once the full
         * header block plus body (Content-Length, or the chunked terminator) is
         * present, or REQ_ERROR — setting `errorStatus` to the HTTP status to send
         * — on a malformed header block, a bad Content-Length, or a body whose
         * declared length already exceeds every route's limit on `port` (early 413).
         * On REQ_COMPLETE, `consumed` is set to the total byte length of the request
         * (header block + body) so the caller can keep any pipelined bytes that
         * follow it in the buffer.
         */
        RequestReadiness _requestReadiness(const std::vector<char>& buffer, int port,
                                           int& errorStatus, size_t& consumed) const;

        /*
         * Completeness check for a Transfer-Encoding: chunked body, walking the
         * actual chunk framing from `body_start` (the offset of the first
         * chunk-size line) rather than searching for the "0\r\n\r\n" byte pattern,
         * which false-positives when chunk data contains that sequence and
         * mis-handles chunk extensions / trailers. Uses the same rules as
         * ChunkDecoder so detection and later decoding agree. `limit` is the
         * largest body size allowed on this port ((size_t)-1 = unlimited), checked
         * against the running decoded length for an early 413. Sets `errorStatus`
         * (400 / 413) and returns REQ_ERROR on malformed framing or over-limit.
         */
        RequestReadiness _chunkedReadiness(const std::vector<char>& buffer, size_t body_start,
                                           size_t limit, int& errorStatus,
                                           size_t& consumed) const;

        /*
         * Copies `response` into the connection's out buffer, resets the write
         * cursor, moves it to SENDING_RESPONSE, and arms EPOLLOUT (plus EPOLLRDHUP)
         * so onClientOUT drains it. The single place that hands a finished response
         * back to the event loop — used for static, CGI, and error replies alike.
         */
        void _queueResponse(Connection* conn, const std::string& response);

        /*
         * Periodic (≈1Hz) sweep that enforces timeouts. For CGI exchanges: aborts
         * any CGI idle too long (no I/O progress) or past the hard cap, killing the
         * child and replying 504. For ordinary client connections: closes any that
         * has made no socket I/O for CLIENT_TIMEOUT seconds, so a stalled or
         * slowloris client can never hold a connection open indefinitely.
         */
        void _checkTimeouts();

        /*
         * Returns the largest client_max_body_size configured across every server
         * block listening on `port` and all their locations. Used as a safe early
         * 413 guard: a request whose Content-Length exceeds this maximum cannot be
         * accepted by any route on that port, so the body need not be buffered.
         * Returns (size_t)-1 (unlimited) if any matching route is unlimited.
         */
        size_t _maxBodyLimitForPort(int port) const;

        /*
         * Largest client_max_header_size across every server block listening on
         * `port`. Header size is a server-level directive (no per-location
         * override), used as an early 431 guard: a header block exceeding this
         * maximum cannot be accepted by any server on that port, so it is rejected
         * before the request is fully buffered. Returns (size_t)-1 if unlimited.
         */
        size_t _maxHeaderLimitForPort(int port) const;

        /*
         * Queues an error response and arms EPOLLOUT. Routes through
         * make_error_response(conn->server_cfg) so a pre-routing failure (parse /
         * routing) still serves the matching custom error_page (or a generated
         * page) with a body, and always sets Connection: close.
         */
        void _sendError(Connection* conn, int status);

        /*
         * Builds the HTTP response for a routed (non-CGI) request: flattens the
         * RouteMatch into a RouteContext (server/location inheritance) and runs it
         * through ResponseBuilder, returning the serialized response. A failure to
         * resolve the route context (e.g. unresolved root) yields a 500 page.
         * The Connection header is set to keep-alive or close per `keepAlive`.
         */
        std::string _buildResponse(const RequestContext& ctx, const RouteMatch& match,
                                   bool keepAlive) const;

        /*
         * Whether `method` is in the matched route's accepted methods, using the
         * same resolution as RouteContextBuilder (location's allow_methods if set,
         * else GET HEAD). CGI dispatch bypasses ResponseBuilder's 405 check, so the
         * caller uses this to gate CGI: a disallowed method falls through to the
         * static path, which emits the canonical 405 (+ Allow + error page).
         */
        bool _isMethodAllowed(const RouteMatch& match, const std::string& method) const;

        /*
         * Records a handler pointer (a Connection or a CGI-pipe wrapper) that is
         * being freed while the current epoll batch is still being processed, so
         * run() skips any not-yet-handled event in the same batch that still points
         * to it. Without this, dereferencing that stale event's data.ptr would be a
         * use-after-free. Called by onCloseConnection and by CgiManager when it
         * tears down pipe wrappers. The set is cleared each loop iteration.
         */
        void _retireHandler(const void* handler);

        Server(const Server& other);
        Server& operator=(const Server& rhs);

    public:
        /*
         * Constructs the server with the parsed configuration.
         */
        explicit Server(const std::vector<ServerConfig>& configs);

        /*
         * Destroys all listeners and connections.
         */
        ~Server();

        /*
         * Main event loop: waits for epoll events and dispatches them.
         * Runs until _running is set to false.
         */
        void run();

        // IServerCallback implementation

        /*
         * Called when a listening socket accepts a new client.
         * Creates a new Connection and registers it with epoll.
         */
        virtual void onNewConnection(int client_fd, int listener_fd);

        /*
         * Called when a client socket is readable.
         * Dispatched to Connection for handling.
         */
        virtual void onClientIN(Connection* conn);

        /*
         * Called when a client socket is writable.
         * Dispatched to Connection for handling.
         */
        virtual void onClientOUT(Connection* conn);

        /*
         * Called when a connection must be closed.
         * Removes from epoll and cleans up resources.
         */
        virtual void onCloseConnection(int fd, Connection* conn);

        /*
         * Called when a CGI process stdout pipe is readable. Delegates to _cgi.
         */
        virtual void onCgiPipeIN(Connection* conn);

        /*
         * Called when a CGI process stdin pipe is writable. Delegates to _cgi.
         */
        virtual void onCgiPipeOUT(Connection* conn);
};
