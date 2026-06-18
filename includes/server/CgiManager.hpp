#pragma once

#include <ctime>
#include <string>

class Server;
class EpollWrapper;
class Connection;
class CgiPipeWrapper;
class RequestContext;
struct RouteMatch;

// Owns the CGI exchange lifecycle on the single epoll loop: forks the child (via
// CgiHandler, the pure fork/exec utility), wires the stdin/stdout pipes into epoll,
// streams the body, drains stdout, reaps, and builds the response. Reuses the
// Server's _queueResponse / _sendError (holds a Server& and is its friend).
class CgiManager {
    public:
        CgiManager(Server& server, EpollWrapper& epoll);

        // Builds the job, forks, and wires the pipes if the request targets a CGI
        // interpreter (reply produced asynchronously by the pipe callbacks); returns
        // false for a non-CGI target. Assumes match.server / match.location non-NULL.
        bool dispatch(Connection* conn, const RequestContext& ctx,
                      const RouteMatch& match);

        // CGI stdout readable: read into cgi_raw_output. EOF finalizes; error -> 502.
        void onPipeIN(Connection* conn);

        // CGI stdin writable: stream the body, then close stdin (EOF) and move to
        // CGI_READING_OUTPUT.
        void onPipeOUT(Connection* conn);

        // Tears down all CGI state (pipes out of epoll, child killed+reaped, wrappers
        // freed). Idempotent. Must run before the Connection is destroyed.
        void teardown(Connection* conn);

        // True while the connection is in a CGI state, so the timeout sweep applies
        // CGI caps instead of the ordinary client timeouts.
        bool isActive(Connection* conn) const;

        // True when a live child made no I/O progress within the inactivity window or
        // exceeded the hard cap. False if no child is running.
        bool isExpired(Connection* conn, std::time_t now) const;

        // Aborts a timed-out CGI: tears down the child/pipes and queues 504.
        void timeout(Connection* conn);

    private:
        Server&        _server;
        EpollWrapper&  _epoll;

        // Forks the child for conn->cgi_job, parks the client socket, and registers
        // both pipes. On failure, tears down and sends 500.
        void start(Connection* conn);

        // Closes the CGI stdin pipe so the child sees EOF. Idempotent.
        void closeInPipe(Connection* conn);

        // Removes one pipe fd from epoll, closes it, frees its wrapper, and resets
        // both to inactive sentinels (fd=-1, wrapper=NULL). Guarded for repeat calls;
        // `label` names the pipe in the epoll-removal warning.
        void closePipe(int& fd, CgiPipeWrapper*& wrapper, const char* label);

        // Reaps, tears down the pipes, and either queues the built response or, on
        // read error / empty / malformed output, sends 502.
        void finalize(Connection* conn, bool readError);

        // Parses cgi_raw_output into a full HTTP response: "Status:" -> status line,
        // other headers passed through, our own Content-Length + Connection appended.
        // Returns "" when there is no header/body separator (caller treats as 502).
        std::string buildResponse(Connection* conn) const;

        CgiManager(const CgiManager& other);
        CgiManager& operator=(const CgiManager& rhs);
};
