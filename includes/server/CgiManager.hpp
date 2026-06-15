#pragma once

#include <ctime>
#include <string>

class Server;
class EpollWrapper;
class Connection;
class CgiPipeWrapper;
class RequestContext;
struct RouteMatch;

/*
 * Owns the CGI exchange lifecycle on top of the single epoll loop. CgiHandler
 * (cgi layer) is the pure fork/exec utility; CgiManager is the event-loop
 * integration that was formerly spread across Server: it forks the child (via
 * CgiHandler), registers the stdin/stdout pipes with epoll, streams the request
 * body to stdin, drains stdout, reaps the child, and turns the collected output
 * into an HTTP response. It reuses the Server's single-owner response/error
 * transitions (_queueResponse / _sendError) rather than duplicating them, so it
 * holds a reference back to the Server (and is a friend of it).
 */
class CgiManager {
    public:
        CgiManager(Server& server, EpollWrapper& epoll);

        /*
         * If the routed request targets a CGI interpreter, builds the job, forks
         * the child, and wires up the pipes — returning true (the response is then
         * produced asynchronously by the pipe callbacks). Returns false when this
         * is not a CGI request, so the caller builds a static response instead.
         * Assumes match.server / match.location are non-NULL.
         */
        bool dispatch(Connection* conn, const RequestContext& ctx,
                      const RouteMatch& match);

        /*
         * CGI stdout pipe readable: read once into conn->cgi_raw_output. EOF
         * finalizes the exchange; a read error finalizes as 502.
         */
        void onPipeIN(Connection* conn);

        /*
         * CGI stdin pipe writable: stream the request body to the child, then
         * close stdin (child sees EOF) and move to CGI_READING_OUTPUT.
         */
        void onPipeOUT(Connection* conn);

        /*
         * Tears down any CGI state on the connection: removes the pipe fds from
         * epoll, closes them, kills and reaps the child, and frees the wrappers.
         * Idempotent / safe when no CGI is active. Must run before the Connection
         * is destroyed so epoll never references a freed wrapper.
         */
        void teardown(Connection* conn);

        /*
         * True while the connection is in a CGI state (stdin-writing or
         * stdout-reading), so the timeout sweep applies CGI caps to it rather than
         * the ordinary client idle/request timeouts.
         */
        bool isActive(Connection* conn) const;

        /*
         * True when a live CGI child has either made no I/O progress within the
         * inactivity window or exceeded the absolute hard cap. False if no child
         * is running.
         */
        bool isExpired(Connection* conn, std::time_t now) const;

        /*
         * Aborts a timed-out CGI exchange: kills/reaps the child, tears down the
         * pipes, and queues a 504 Gateway Timeout to the client.
         */
        void timeout(Connection* conn);

    private:
        Server&        _server;
        EpollWrapper&  _epoll;

        /*
         * Forks the CGI child (via CgiHandler::execute) for conn->cgi_job, parks
         * the client socket on EPOLLRDHUP, and registers both pipes with epoll.
         * On any failure, tears down the partial state and sends 500.
         */
        void start(Connection* conn);

        /*
         * Removes the CGI stdin pipe from epoll, closes it, and frees its wrapper
         * so the child sees EOF on stdin. Idempotent.
         */
        void closeInPipe(Connection* conn);

        /*
         * Removes one CGI pipe fd from epoll, closes it, frees its wrapper, and
         * resets both to inactive sentinels (fd = -1, wrapper = NULL). Each step is
         * guarded so repeated calls are no-ops. `label` names the pipe in the
         * epoll-removal warning. Shared by closeInPipe and teardown.
         */
        void closePipe(int& fd, CgiPipeWrapper*& wrapper, const char* label);

        /*
         * Reaps the child, tears down the pipes, then either turns the collected
         * output into an HTTP response (queued for sending) or, on read error /
         * empty / malformed output, sends 502 Bad Gateway.
         */
        void finalize(Connection* conn, bool readError);

        /*
         * Parses conn->cgi_raw_output into a full HTTP response: maps a CGI
         * "Status:" header to the status line, passes the remaining headers
         * through, and appends a computed Content-Length + the Connection header
         * per conn->keep_alive. Returns "" when the output has no header/body
         * separator (caller treats that as 502).
         */
        std::string buildResponse(Connection* conn) const;

        CgiManager(const CgiManager& other);
        CgiManager& operator=(const CgiManager& rhs);
};
