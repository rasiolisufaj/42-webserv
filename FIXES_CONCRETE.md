# Connection Layer: Concrete Fixes & Code Examples

## Fix 1: Complete Connection Destructor with All Fd Cleanup

**Current Issue**: upload_fd, CGI fds not closed

**Location**: `includes/server/Connection.hpp` + `srcs/server/Connection.cpp`

```cpp
// === Connection.hpp ===
class Connection : public IEventHandler {
    // ... existing fields ...
    
    // CGI PART (uncommented)
    int  cgi_stdin_fd;   // -1 if not in use
    int  cgi_stdout_fd;  // -1 if not in use
    pid_t cgi_pid;       // -1 if not in use
    std::time_t cgi_start_time;
    
    // ... rest of class ...
};

// === Connection.cpp ===
Connection::Connection(int fd, const ServerConfig *serverCfg)
    : _fd(fd), server_cfg(serverCfg), read_idx(0), write_idx(0),
      state(READING_HEADER), last_activity(std::time(NULL)),
      first_byte_time(0), header_complete(false), closeAfterWrite(false),
      file_fd(-1), file_bytes_sent(0), file_total_size(0), is_streaming_file(false),
      header_check_len(0), chunk_raw_offset(0),
      upload_fd(-1), body_bytes_expected(0), body_bytes_received(0), 
      is_streaming_upload(false),
      cgi_stdin_fd(-1), cgi_stdout_fd(-1), cgi_pid(-1), cgi_start_time(0) {}

Connection::~Connection() {
    // Close client socket
    if (this->_fd != -1) {
        close(this->_fd);
        this->_fd = -1;
    }
    
    // Close file streaming fd
    if (this->file_fd != -1) {
        close(this->file_fd);
        this->file_fd = -1;
    }
    
    // Close upload fd (NEW)
    if (this->upload_fd != -1) {
        close(this->upload_fd);
        this->upload_fd = -1;
    }
    
    // Close CGI pipes (NEW)
    if (this->cgi_stdin_fd != -1) {
        close(this->cgi_stdin_fd);
        this->cgi_stdin_fd = -1;
    }
    if (this->cgi_stdout_fd != -1) {
        close(this->cgi_stdout_fd);
        this->cgi_stdout_fd = -1;
    }
}
```

---

## Fix 2: Safe Event Handler to Avoid Double-Free

**Current Issue**: Race condition when Connection deleted during handle()

**Location**: `srcs/server/Connection.cpp:77-117`

```cpp
// BEFORE (unsafe):
void Connection::handle(uint32_t event, IServerCallback &callback) {
    if (event & (EPOLLERR | EPOLLHUP)) {
        Logger::log(Logger::DEBUG, "Epoll error/hangup event: " + to_string(event));
        callback.onCloseConnection(this->_fd, this);
        return;
    }
    
    if (event & EPOLLRDHUP) {
        this->closeAfterWrite = true;
        if (!(event & (EPOLLIN | EPOLLOUT))) {
            this->state = CLOSED;
            callback.onCloseConnection(this->_fd, this);
            return;
        }
    }
    
    if (event & EPOLLIN) {
        callback.onClientIN(this);
        if (this->state == CLOSED) {  // BUG: this might be deleted!
            callback.onCloseConnection(this->_fd, this);
            return;
        }
    } else if (event & EPOLLOUT) {
        callback.onClientOUT(this);
        if (this->state == CLOSED) {  // BUG: this might be deleted!
            callback.onCloseConnection(this->_fd, this);
            return;
        }
    }
}

// AFTER (safe):
void Connection::handle(uint32_t event, IServerCallback &callback) {
    if (event & (EPOLLERR | EPOLLHUP)) {
        Logger::log(Logger::DEBUG, 
            "Epoll error/hangup event: " + to_string(event));
        callback.onCloseConnection(this->_fd, this);
        return;  // Exit immediately, don't touch this again
    }
    
    if (event & EPOLLRDHUP) {
        this->closeAfterWrite = true;
        // If only RDHUP (no IN/OUT to handle), close now
        if (!(event & (EPOLLIN | EPOLLOUT))) {
            this->state = CLOSED;
            callback.onCloseConnection(this->_fd, this);
            return;  // Exit immediately, don't touch this again
        }
    }
    
    // Process IN/OUT events
    if (event & EPOLLIN) {
        callback.onClientIN(this);
    }
    if (event & EPOLLOUT) {
        callback.onClientOUT(this);
    }
    
    // Check state ONCE after all handlers
    // The callback handlers are responsible for managing state
    // If state is CLOSED, the Server will manage cleanup
    if (this->state == CLOSED) {
        callback.onCloseConnection(this->_fd, this);
    }
}
```

**Key change**: Remove the intermediate `onCloseConnection()` calls inside the event handling. Let the caller (Server) manage the lifecycle after calling handle().

---

## Fix 3: O_NONBLOCK on CGI Pipes

**Current Issue**: CGI pipes are blocking → server hangs

**Location**: `srcs/cgi/Cgi.cpp:80-100+`

```cpp
#include "cgi/Cgi.hpp"
#include <cstdio>
#include <fcntl.h>     // ADD THIS
#include <cerrno>

// ... existing Cgi::execute() code ...

pid_t pid = fork();
if (pid == -1) {
    close(fd_in[0]); close(fd_in[1]);
    close(fd_out[0]); close(fd_out[1]);
    return -1;
}
else if (pid == 0) {
    // CHILD PROCESS
    dup2(fd_in[0], STDIN_FILENO);
    dup2(fd_out[1], STDOUT_FILENO);
    dup2(fd_out[1], STDERR_FILENO);
    close(fd_in[0]); close(fd_in[1]);
    close(fd_out[0]); close(fd_out[1]);
    
    if (execve(argv[0], argv, envp) == -1) {
        perror("Error en execve");
        free_array(argv);
        free_array(envp);
        exit(1);
    }
}
else {
    // PARENT PROCESS (NEW: ADD THIS BLOCK)
    
    // Close child's ends of pipes
    close(fd_in[0]);   // Parent won't read from stdin
    close(fd_out[1]);  // Parent won't write to stdout
    
    // Set non-blocking mode (CRITICAL FIX)
    int flags = fcntl(fd_in[1], F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd_in[1], F_SETFL, flags | O_NONBLOCK);
    }
    
    flags = fcntl(fd_out[0], F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd_out[0], F_SETFL, flags | O_NONBLOCK);
    }
    
    // Set close-on-exec (so CGI subprocesses don't inherit these)
    fcntl(fd_in[1], F_SETFD, FD_CLOEXEC);
    fcntl(fd_out[0], F_SETFD, FD_CLOEXEC);
    
    // Return pid to caller for tracking
    return pid;
}
```

---

## Fix 4: Implement Server Event Loop (Simplified Skeleton)

**Status**: File is empty, this is a starter template

**Location**: `srcs/server/Server.cpp` (new implementation)

```cpp
#include "server/Server.hpp"
#include "server/Connection.hpp"
#include "server/Socket.hpp"
#include "utils/Logger.hpp"
#include <signal.h>
#include <sys/wait.h>
#include <ctime>

// Static flag for signal handling
static volatile bool should_exit = false;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        should_exit = true;
    }
}

Server::Server() : _epoll(), _should_exit(false) {
    // Install signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
}

Server::~Server() {
    // Cleanup all connections
    for (std::map<int, Connection*>::iterator it = _sessions.begin(); 
         it != _sessions.end(); ++it) {
        _epoll.removeFd(it->first);
        delete it->second;
    }
    _sessions.clear();
    
    // Cleanup listening sockets
    for (size_t i = 0; i < _listening_sockets.size(); i++) {
        _epoll.removeFd(_listening_sockets[i]->getFd());
        delete _listening_sockets[i];
    }
    _listening_sockets.clear();
}

void Server::addListener(const std::string &host, const std::string &port,
                          const ServerConfig *cfg) {
    Socket *sock = new Socket(host, port);
    try {
        sock->setup();
        _epoll.addFd(sock->getFd(), EPOLLIN, sock);
        _listener_configs[sock->getFd()] = cfg;
        _listening_sockets.push_back(sock);
        Logger::log(Logger::INFO, "Listening on " + host + ":" + port);
    } catch (const std::exception &e) {
        Logger::log(Logger::ERROR, "Failed to setup listener: " + 
                    std::string(e.what()));
        delete sock;
        throw;
    }
}

void Server::run() {
    const int TIMEOUT_MS = 5000;      // 5 second timeout on epoll_wait
    const int CONNECTION_TIMEOUT_S = 60; // 60 second idle timeout per connection
    
    Logger::log(Logger::INFO, "Server started, waiting for connections...");
    
    while (!should_exit && !_should_exit) {
        // Timeout: reap dead children and check connection timeouts
        time_t now = std::time(NULL);
        
        // Reap any dead child processes (CGI)
        int status;
        while (waitpid(-1, &status, WNOHANG) > 0) {
            // Child reaped
        }
        
        // Check for timed-out connections
        for (std::map<int, Connection*>::iterator it = _sessions.begin(); 
             it != _sessions.end(); ) {
            if (now - it->second->last_activity > CONNECTION_TIMEOUT_S) {
                Logger::log(Logger::WARNING, "Connection timeout: fd " + 
                           to_string(it->first));
                _epoll.removeFd(it->first);
                delete it->second;
                it = _sessions.erase(it);
            } else {
                ++it;
            }
        }
        
        // Wait for events
        int nfds = _epoll.wait(TIMEOUT_MS);
        
        if (nfds < 0) {
            Logger::log(Logger::ERROR, "epoll_wait failed");
            break;
        }
        
        if (nfds == 0) {
            continue;  // Timeout, loop again
        }
        
        // Process all ready events
        const struct epoll_event *events = _epoll.getEvents();
        for (int i = 0; i < nfds; i++) {
            IEventHandler *handler = (IEventHandler *)events[i].data.ptr;
            if (handler == NULL) {
                Logger::log(Logger::ERROR, "NULL handler from epoll");
                continue;
            }
            
            try {
                handler->handle(events[i].events, *this);
            } catch (const std::exception &e) {
                Logger::log(Logger::ERROR, 
                           "Exception in event handler: " + std::string(e.what()));
            }
        }
    }
    
    Logger::log(Logger::INFO, "Server shutting down...");
}

// IServerCallback implementations:

void Server::onNewConnection(int client_fd, int listener_fd) {
    Logger::log(Logger::DEBUG, "New connection: fd " + to_string(client_fd));
    
    // Find the ServerConfig for this listener
    ServerConfig *cfg = NULL;
    if (_listener_configs.count(listener_fd) > 0) {
        cfg = const_cast<ServerConfig*>(_listener_configs[listener_fd]);
    }
    
    if (cfg == NULL) {
        Logger::log(Logger::ERROR, "No config for listener fd " + 
                   to_string(listener_fd));
        close(client_fd);
        return;
    }
    
    // Create new Connection
    Connection *conn = new Connection(client_fd, cfg);
    
    try {
        // Add to epoll for reading
        _epoll.addFd(client_fd, EPOLLIN | EPOLLRDHUP, conn);
        _sessions[client_fd] = conn;
    } catch (const std::exception &e) {
        Logger::log(Logger::ERROR, "Failed to add connection to epoll: " + 
                   std::string(e.what()));
        delete conn;
    }
}

void Server::onClientIN(Connection *conn) {
    if (conn == NULL) return;
    
    int fd = conn->getFd();
    conn->last_activity = std::time(NULL);
    
    // Read from client socket
    const size_t READ_SIZE = 8192;
    char buffer[READ_SIZE];
    
    ssize_t bytes_read = read(fd, buffer, READ_SIZE);
    
    if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;  // No data available right now
        }
        Logger::log(Logger::WARNING, "Read error on fd " + to_string(fd) + 
                   ": " + std::string(strerror(errno)));
        onCloseConnection(fd, conn);
        return;
    }
    
    if (bytes_read == 0) {
        // EOF: client closed connection
        Logger::log(Logger::DEBUG, "Client closed: fd " + to_string(fd));
        conn->state = Connection::CLOSED;
        onCloseConnection(fd, conn);
        return;
    }
    
    // Append to input buffer
    conn->in_buffer.insert(conn->in_buffer.end(), buffer, buffer + bytes_read);
    
    // TODO: Parse HTTP request, build response
    // conn->in_buffer -> Request::parse() -> response
    // Transition to SENDING_RESPONSE
    
    // For now, just switch to writing
    if (conn->state == Connection::READING_HEADER) {
        // Once we have a complete header, start building response
        // This is a placeholder
        conn->out_buffer.insert(conn->out_buffer.end(), 
                               "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n",
                               "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n" + 
                               std::string(45).length());
        conn->state = Connection::SENDING_RESPONSE;
        
        // Add EPOLLOUT to wait for write readiness
        _epoll.modifyFd(fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP, conn);
    }
}

void Server::onClientOUT(Connection *conn) {
    if (conn == NULL) return;
    
    int fd = conn->getFd();
    conn->last_activity = std::time(NULL);
    
    // Write to client socket
    if (conn->write_idx >= conn->out_buffer.size()) {
        // Nothing to write
        if (conn->closeAfterWrite) {
            conn->state = Connection::CLOSED;
            onCloseConnection(fd, conn);
            return;
        }
        // Resume reading
        _epoll.modifyFd(fd, EPOLLIN | EPOLLRDHUP, conn);
        return;
    }
    
    ssize_t bytes_to_write = conn->out_buffer.size() - conn->write_idx;
    ssize_t bytes_written = write(fd, 
                                  &conn->out_buffer[conn->write_idx],
                                  bytes_to_write);
    
    if (bytes_written < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;  // Try again later
        }
        Logger::log(Logger::WARNING, "Write error on fd " + to_string(fd));
        onCloseConnection(fd, conn);
        return;
    }
    
    conn->write_idx += bytes_written;
    
    // Check if we've written everything
    if (conn->write_idx >= conn->out_buffer.size()) {
        if (conn->closeAfterWrite) {
            conn->state = Connection::CLOSED;
            onCloseConnection(fd, conn);
            return;
        }
        // Response fully sent, go back to reading for next request
        conn->read_idx = 0;
        conn->write_idx = 0;
        conn->in_buffer.clear();
        conn->out_buffer.clear();
        conn->state = Connection::READING_HEADER;
        _epoll.modifyFd(fd, EPOLLIN | EPOLLRDHUP, conn);
    }
}

void Server::onCloseConnection(int fd, Connection *conn) {
    if (conn == NULL) return;
    
    Logger::log(Logger::DEBUG, "Closing connection: fd " + to_string(fd));
    
    _epoll.removeFd(fd);
    
    if (_sessions.count(fd) > 0) {
        _sessions.erase(fd);
    }
    
    delete conn;
}

void Server::onCgiPipeIN(Connection *conn) {
    // TODO: Read from CGI stdout
}

void Server::onCgiPipeOUT(Connection *conn) {
    // TODO: Write to CGI stdin
}
```

**Header file** (`includes/server/Server.hpp`):
```cpp
#pragma once
#include "IServerCallback.hpp"
#include "EpollWrapper.hpp"
#include <map>
#include <vector>
#include <string>

struct ServerConfig;
class Socket;
class Connection;

class Server : public IServerCallback {
private:
    EpollWrapper _epoll;
    bool _should_exit;
    std::map<int, Connection*> _sessions;
    std::vector<Socket*> _listening_sockets;
    std::map<int, const ServerConfig*> _listener_configs;
    
public:
    Server();
    ~Server();
    
    void addListener(const std::string &host, const std::string &port,
                     const ServerConfig *cfg);
    void run();
    
    // IServerCallback implementations
    virtual void onNewConnection(int client_fd, int listener_fd);
    virtual void onClientIN(Connection *conn);
    virtual void onClientOUT(Connection *conn);
    virtual void onCloseConnection(int fd, Connection *conn);
    virtual void onCgiPipeIN(Connection *conn);
    virtual void onCgiPipeOUT(Connection *conn);
};
```

---

## Fix 5: Safe Buffer Management & Partial Write Pattern

Add to your event handlers:

```cpp
// Pattern for handling partial writes (used in onClientOUT):
ssize_t bytes_written = write(fd, 
                              &conn->out_buffer[conn->write_idx],
                              conn->out_buffer.size() - conn->write_idx);

if (bytes_written < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Socket buffer is full, try again next EPOLLOUT
        return;
    }
    // Real error
    onCloseConnection(fd, conn);
    return;
}

// Move index forward
conn->write_idx += bytes_written;

// Check if fully sent
if (conn->write_idx >= conn->out_buffer.size()) {
    // Response complete
    if (conn->closeAfterWrite) {
        onCloseConnection(fd, conn);
        return;
    }
    // Prepare for next request
    conn->clear_buffers();
    _epoll.modifyFd(fd, EPOLLIN | EPOLLRDHUP, conn);
}
```

---

## Fix 6: Add Helper Methods to Connection

**Add to `includes/server/Connection.hpp`:**

```cpp
class Connection : public IEventHandler {
    // ... existing ...
    
    // Helper method to safely clear buffers
    void reset_for_next_request() {
        read_idx = 0;
        write_idx = 0;
        in_buffer.clear();
        out_buffer.clear();
        state = READING_HEADER;
        header_complete = false;
    }
    
    // Helper to check if we should close
    bool should_close() const {
        return state == CLOSED || closeAfterWrite;
    }
};
```

---

## Fix 7: Implement Missing CGI Cleanup

**Add to Server event loop** (in `run()` method):

```cpp
// Reap any dead child processes (CGI)
// Add at the beginning of the main loop
int status;
pid_t child_pid;
while ((child_pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (WIFEXITED(status)) {
        Logger::log(Logger::DEBUG, 
           "CGI process exited: PID " + to_string(child_pid) + 
           " status " + to_string(WEXITSTATUS(status)));
    } else if (WIFSIGNALED(status)) {
        Logger::log(Logger::WARNING,
           "CGI process killed by signal: " + to_string(WTERMSIG(status)));
    }
    // Find the connection associated with this CGI and clean it up
}
```

---

## Summary of Changes Required

| Issue | File | Lines | Priority |
|-------|------|-------|----------|
| Initialize upload_fd, CGI fds | Connection.cpp | 18-22 | HIGH |
| Close upload_fd in destructor | Connection.cpp | 24-36 | HIGH |
| Fix double-close in handle() | Connection.cpp | 77-117 | HIGH |
| Set O_NONBLOCK on CGI pipes | Cgi.cpp | 80-100 | HIGH |
| Implement Server loop | Server.cpp | All | CRITICAL |
| Add timeout management | Server.cpp | run() | MEDIUM |
| Implement onClientIN/OUT | Server.cpp | NEW | CRITICAL |
| Add waitpid() for CGI | Server.cpp | run() | HIGH |

**Total estimated lines of new code**: ~500-600 lines
**Compilation time**: ~2 seconds
**Testing time**: 2-4 hours
