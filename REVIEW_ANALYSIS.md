# Connection Implementation Review & Architectural Analysis

## Executive Summary

**Status**: Core architecture is sound but several critical gaps exist before production use:
- Connection state machine foundation is present but incomplete
- Event handling framework established but incomplete Server loop
- Multiple fd leak and resource lifecycle risks
- Missing CGI pipeline integration
- Partial read/write handling incomplete

---

## Critical Issues Found

### 1. **Incomplete Server Event Loop (BLOCKING)**
**File**: `srcs/server/Server.cpp` is empty - no event loop implementation
**Impact**: Cannot start the server; entire epoll-based architecture is non-functional
**Risk Level**: CRITICAL

The entire Server layer doesn't exist:
- No `Server::run()` or event loop
- No callback implementations (`onClientIN`, `onClientOUT`, `onCloseConnection`, etc.)
- No SessionManager integration
- `main.cpp` is a manual test harness, not a real server

**Fix Required**: 
```cpp
class Server : public IServerCallback {
  EpollWrapper epoll;
  std::map<int, Connection*> sessions;  // fd -> Connection*
  std::vector<Socket*> listening_sockets;
  
  void run() {
    while (!should_exit) {
      int nfds = epoll.wait(TIMEOUT_MS);
      if (nfds <= 0) { handle_timeouts(); continue; }
      for (int i = 0; i < nfds; i++) {
        IEventHandler *handler = (IEventHandler*)epoll.getEvents()[i].data.ptr;
        handler->handle(epoll.getEvents()[i].events, *this);
      }
    }
  }
  // Implement all IServerCallback methods...
};
```

---

### 2. **File Descriptor Leak in Connection Cleanup**
**File**: `includes/server/Connection.hpp:47-50`, `srcs/server/Connection.cpp:24-36`
**Risk Level**: HIGH - Scalability killer with thousands of clients

The destructor closes `_fd` and `file_fd`, but misses:
- **upload_fd** (line 70) - never closed
- **CGI stdin/stdout pipes** (commented out lines 61-67) - if uncommented, these are never closed
- **Any open file descriptor from file streaming** - could leak if exception occurs

**Test Case**: Open 1000 connections, send requests that trigger file streaming, close → fd table fills up → future `accept()` fails with EMFILE

**Partial Fix**:
```cpp
Connection::~Connection() {
    if (_fd != -1) { close(_fd); _fd = -1; }
    if (file_fd != -1) { close(file_fd); file_fd = -1; }
    if (upload_fd != -1) { close(upload_fd); upload_fd = -1; }
    // When CGI is enabled:
    // if (cgi_stdin_fd != -1) { close(cgi_stdin_fd); }
    // if (cgi_stdout_fd != -1) { close(cgi_stdout_fd); }
}
```

---

### 3. **Epoll Event Handling Race Condition**
**File**: `srcs/server/Connection.cpp:77-117`
**Risk Level**: HIGH

**Problem**: `Connection::handle()` can transition to CLOSED state mid-callback, but subsequent code in `handle()` might still try to access `this` after it's been deleted by `onCloseConnection()`.

```cpp
if (event & EPOLLIN) {
    callback.onClientIN(this);  // This might call onCloseConnection -> deletes this
    if (this->state == CLOSED) {  // Use-after-free!
        callback.onCloseConnection(...);
        return;
    }
}
```

**Test**: Rapid EPOLLIN → server crashes with segfault

**Fix**: Don't call `onCloseConnection()` twice. Refactor:
```cpp
void Connection::handle(uint32_t event, IServerCallback &callback) {
    if (event & (EPOLLERR | EPOLLHUP)) {
        callback.onCloseConnection(_fd, this);
        return;
    }
    
    if (event & EPOLLRDHUP) {
        closeAfterWrite = true;
        if (!(event & (EPOLLIN | EPOLLOUT))) {
            state = CLOSED;
            callback.onCloseConnection(_fd, this);
            return;
        }
    }
    
    if (event & EPOLLIN) {
        callback.onClientIN(this);
    }
    if (event & EPOLLOUT) {
        callback.onClientOUT(this);
    }
    
    if (state == CLOSED) {
        callback.onCloseConnection(_fd, this);
    }
}
```

---

### 4. **Missing Initialization of CGI Fields**
**File**: `includes/server/Connection.hpp:60-67`
**Risk Level**: MEDIUM

CGI fields are commented out but not initialized in constructor. If uncommented later:
- `cgi_stdin_fd`, `cgi_stdout_fd`, `cgi_pid` will be uninitialized
- Destructor checks if `file_fd != -1` but CGI fds won't exist in destructor

```cpp
// Add to Connection::Connection() constructor:
// cgi_stdin_fd(-1), cgi_stdout_fd(-1), cgi_pid(-1), cgi_start_time(0), is_streaming_upload(false), body_bytes_expected(0), body_bytes_received(0)
```

Current constructor is missing upload stream initializers too (line 22):
```cpp
Connection::Connection(int fd, const ServerConfig *serverCfg)
    : _fd(fd), server_cfg(serverCfg), read_idx(0), write_idx(0),
      state(READING_HEADER), last_activity(std::time(NULL)),
      first_byte_time(0), header_complete(false), closeAfterWrite(false),
      file_fd(-1), file_bytes_sent(0), file_total_size(0), is_streaming_file(false),
      header_check_len(0), chunk_raw_offset(0)
      // MISSING: upload_fd(-1), body_bytes_expected(0), body_bytes_received(0), is_streaming_upload(false)
```

---

### 5. **Unset O_NONBLOCK on CGI Pipes**
**File**: `srcs/cgi/Cgi.cpp:42-80`
**Risk Level**: HIGH - Hangs server

Cgi::execute() creates pipes but **does NOT set O_NONBLOCK** before returning.
- Parent writes/reads from blocking fds in the event loop
- Single blocking write to a full pipe blocks the entire epoll loop
- Server becomes unresponsive to other clients

**Fix**:
```cpp
// In Cgi::execute(), after fork, in parent:
fcntl(fd_in[1], F_SETFL, fcntl(fd_in[1], F_GETFL) | O_NONBLOCK);
fcntl(fd_out[0], F_SETFL, fcntl(fd_out[0], F_GETFL) | O_NONBLOCK);
// Also set FD_CLOEXEC
fcntl(fd_in[1], F_SETFD, FD_CLOEXEC);
fcntl(fd_out[0], F_SETFD, FD_CLOEXEC);
```

---

### 6. **No Timeout Management for Hung Connections**
**File**: `includes/server/Connection.hpp:39-40`
**Risk Level**: MEDIUM - DoS vulnerability

Fields exist (`last_activity`, `first_byte_time`, `expected_request_total`) but:
- No timeout enforcement in event loop
- Client can send 1 byte every 30 seconds indefinitely → exhausts fd limit
- CGI timeout commented out

**Required**: Implement timeout logic in Server::run():
```cpp
// Every loop iteration:
time_t now = time(NULL);
for (auto it = sessions.begin(); it != sessions.end(); ++it) {
    if (now - it->second->last_activity > TIMEOUT_SECS) {
        onCloseConnection(it->first, it->second);
    }
}
```

---

### 7. **Partial Write Handling Not Implemented**
**File**: `srcs/server/Connection.cpp` - `handle()` doesn't implement write logic
**Risk Level**: MEDIUM - Large responses fail silently

`EPOLLOUT` event is registered but there's no actual write loop:
```cpp
if (event & EPOLLOUT) {
    callback.onClientOUT(this);  // This is a stub, no implementation
}
```

Scenario: 10MB response → first `write()` sends 64KB (default SO_SNDBUF) → rest of response lost

**Fix Required**: In `onClientOUT`:
```cpp
ssize_t bytes_written = write(conn->_fd, &conn->out_buffer[conn->write_idx], 
                              conn->out_buffer.size() - conn->write_idx);
if (bytes_written > 0) {
    conn->write_idx += bytes_written;
}
if (conn->write_idx >= conn->out_buffer.size()) {
    if (conn->closeAfterWrite) {
        onCloseConnection(conn->_fd, conn);
    } else {
        epoll.modifyFd(conn->_fd, EPOLLIN, conn);  // Resume reading
    }
}
```

---

### 8. **Partial Read Handling Incomplete**
**File**: `srcs/server/Connection.cpp` - no read loop
**Risk Level**: MEDIUM

No implementation of `onClientIN`:
- Large POST bodies split across multiple packets
- Must handle EAGAIN/EWOULDBLOCK from non-blocking read
- Chunked encoding parsing needs resumable state (fields exist but not used)

**Test**: Send large POST body in small chunks → incomplete parse

---

### 9. **Double-Add/Double-Remove in Epoll**
**File**: `srcs/server/EpollWrapper.cpp:35-75`
**Risk Level**: LOW but can crash

`modifyFd` assumes fd is already in epoll. If not, `EPOLL_CTL_MOD` returns ENOENT → exception.
`removeFd` is forgiving but doesn't validate state.

**Pattern of misuse**:
```
Session created → addFd(client_fd, EPOLLIN, ...)
New data, need write → modifyFd(client_fd, EPOLLIN | EPOLLOUT, ...)  // OK
Close connection → removeFd(client_fd)
Destructor → tries removeFd again  // Silently ignored, but risky
```

**Recommendation**: Track which fds are registered, assert preconditions.

---

### 10. **Memory Leak: CGI Process Cleanup**
**File**: `srcs/cgi/Cgi.cpp:62-80`
**Risk Level**: HIGH

Child process execs successfully (no leak), but:
- Parent never `waitpid()` on child → zombie processes accumulate
- Parent never closes child's end of pipes after fork
  - `fd_in[0]` (read end of stdin pipe) unused by parent, should close
  - `fd_out[1]` (write end of stdout pipe) unused by parent, should close

After ~1000 CGI requests → zombie table fills → no more child processes possible

**Current code**:
```cpp
else if (pid == 0) {
    // child
    close(fd_in[0]); close(fd_in[1]);
    close(fd_out[0]); close(fd_out[1]);
    // ← WRONG: should close parent's ends before exec
}
// Parent code missing:
// close(fd_in[0]);   // Parent doesn't read from stdin
// close(fd_out[1]);  // Parent doesn't write to stdout (from child)
```

**Fix**: Parent should cleanup child:
```cpp
// In parent, after fork():
close(fd_in[0]);   // Close read end (parent won't use)
close(fd_out[1]);  // Close write end (parent won't use)

// Somewhere in event loop (when CGI finishes):
int status;
if (waitpid(cgi_pid, &status, WNOHANG) == cgi_pid) {
    // CGI process finished, reap it
}
```

---

## Missing But Outlined Architecture

### SessionManager (`srcs/server/SessionManager.cpp`)
**Status**: Empty file, needs to own Connection lifecycle

Should track active connections:
```cpp
class SessionManager {
    std::map<int, Connection*> active;  // fd -> Connection*
    
    Connection* createSession(int client_fd, const ServerConfig *cfg);
    void closeSession(int fd);
    Connection* getSession(int fd);
    void cleanup(time_t now);  // Close timed-out connections
};
```

### Server Main Loop (`srcs/server/Server.cpp`)
**Status**: Empty, must implement

Must include:
- Event loop with timeout
- Callback dispatch
- Timeout management
- Exception handling (must not crash on bad input)

---

## Design Risks & Decisions

### 1. **Handler Pointer Storage in `epoll_event.data.ptr`**
- ✅ **Advantage**: Direct casting avoids fd → handler lookup map
- ⚠️ **Risk**: If `Connection` is deleted, dangling pointer remains in epoll until fd is unregistered
- **Mitigation**: Always call `epoll.removeFd()` before deleting Connection

### 2. **Reusable Read/Write Buffers with Index**
- ✅ **Design**: `read_idx`, `write_idx` track position within vectors
- ⚠️ **Issue**: Buffers never shrink → if request is 10MB, buffer stays 10MB for connection lifetime
- **Mitigation**: Implement periodic buffer compaction or use circular buffer

### 3. **Connection Owns File Descriptor**
- ✅ **Design**: Connection destructor closes fd (RAII)
- ⚠️ **Issue**: Must be deleted *before* removing from epoll, or double-close could occur
- **Mitigation**: Server must call `epoll.removeFd()` before `delete connection`

### 4. **Non-blocking + Manual Buffering**
- ✅ **Correct Approach**: Avoids threads, matches async architecture
- ⚠️ **Implementation**: Complex partial read/write handling not yet present

---

## Concrete Test Cases

### Test 1: Fd Leak Under Load
```bash
# Needs working Server loop
for i in {1..1000}; do
  (sleep 1; curl http://localhost:8080/bigfile.bin > /dev/null) &
done
lsof -p $(pgrep webserv) | wc -l  # Should not grow unbounded
```

### Test 2: Partial Write Handling
```bash
# Send 100MB file, saturate network
timeout 5 curl -X POST --data-binary @100mb.bin http://localhost:8080/ || echo "timeout"
# Server should continue accepting other clients
```

### Test 3: CGI Timeout/Hang
```bash
# Python CGI that sleeps
curl 'http://localhost:8080/cgi-bin/sleep.py'
# Server should timeout after 30s, not hang forever
```

### Test 4: Connection Reuse (Keep-Alive)
```bash
# HTTP/1.1 Keep-Alive: multiple requests on same connection
{
  echo -ne "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"
  echo -ne "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n"
} | nc localhost 8080
# Both responses should arrive on same TCP stream
```

### Test 5: Zombie Process Accumulation
```bash
# Run 1000 CGI requests
for i in {1..1000}; do curl 'http://localhost:8080/cgi-bin/simple.py'; done
ps aux | grep webserv  # Check for <defunct> processes
# Should be 0 zombies; if present, waitpid() not called
```

---

## Testing Roadmap

### Phase 1: Static Analysis
- [ ] Build with `-Wall -Wextra -Werror` ✅ Passes
- [ ] Check for unused variables
- [ ] Grep for `TODO` comments
- [ ] Review all `close()` calls match `open()` calls

### Phase 2: Memory Safety (Before Running)
```bash
cd /home/rasiol/42-CURSUS/42-Webserv
valgrind --leak-check=full --show-leak-kinds=all ./webserv config/default.conf &
# (Once Server loop exists, run test client)
```

### Phase 3: Socket & Fd Handling
- [ ] lsof while connections active
- [ ] Check SO_SNDBUF/SO_RCVBUF limits
- [ ] Test rapid connect/disconnect

### Phase 4: Scalability
- [ ] 100 concurrent connections
- [ ] 1000 sequential connections
- [ ] Large file streaming (>100MB)

---

## Recommendations

### Immediate (Blocking Server Start)
1. **Implement Server::run()** - Copy the structure from `Socket::handle()` loop
2. **Implement onClientIN/OUT callbacks** - Add read/write loops with EAGAIN handling
3. **Implement onCloseConnection** - Clean SessionManager and epoll

### High Priority
4. **Fix CGI pipe O_NONBLOCK** - Prevents server hang
5. **Initialize all Connection fields** - Prevents UB/crashes
6. **Close upload_fd in destructor** - Prevents fd leak
7. **Implement waitpid() for CGI** - Prevents zombies

### Medium Priority  
8. **Add timeout enforcement** - Prevents slowloris DoS
9. **Implement buffer compaction** - Prevents memory bloat
10. **Add connection limits** - Graceful degradation

### Polish
11. **Refactor handle() to avoid double-close** - Less risky state machine
12. **Add debug logging for fd lifecycle** - Easier troubleshooting

---

## Overall Verdict

**Status**: ✅ Strong foundation, ⚠️ Not production-ready

The architecture is sound:
- Event-based design using epoll is correct
- Connection state machine concept is good
- Config/HTTP parsing layers appear solid

But critical gaps make it non-functional:
- Server loop doesn't exist
- Event callbacks aren't implemented
- Resource cleanup is incomplete
- Timeout/scalability features missing

**Estimate to functional**: 20-40 hours
- Server loop: 4 hours
- Read/write handlers: 6 hours
- CGI integration: 8 hours
- Timeout/limits: 4 hours
- Testing/hardening: 8 hours

---

## References

- epoll(7): https://man7.org/linux/man-pages/man7/epoll.7.html
- How to Handle Partial Writes: https://man7.org/linux/man-pages/man2/write.2.html#NOTES
- Zombie Process Cleanup: https://man7.org/linux/man-pages/man2/waitpid.2.html
