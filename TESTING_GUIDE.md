# WebServ Testing Guide

After implementing the `Connection` class, your team can now test the following components **without waiting for the full Server event loop** to be implemented.

## What You Can Test Now

### ✅ Layer 1: Config Parser (Leny's Responsibility)
- **Status:** Fully testable
- **Tests included:** `test_config_parser()`
- **Test command:** `make test`
- **What's covered:**
  - Config file parsing
  - Server configuration loading
  - Location directive parsing
  - Port and host resolution

### ✅ Layer 2: HTTP Parsing (Thomas's Responsibility)
- **Status:** Fully testable
- **Tests included:** `test_http_parsing()`
- **What's covered:**
  - Request line parsing (GET, POST, DELETE, etc.)
  - HTTP header parsing (case-insensitive keys)
  - Content-Length extraction
  - Request target parsing (path + query string)
  - RequestContext building

### ✅ Layer 3: Connection State Machine (Rasiol's Responsibility)
- **Status:** Fully testable (without Server integration)
- **Tests included:** 
  - `test_connection_lifecycle()`
  - `test_connection_state_transitions()`
  - `test_connection_event_handling()`
- **What's covered:**
  - Connection creation and destruction (resource cleanup)
  - File descriptor management (FD cleanup verified)
  - State transitions (READING_HEADER → READING_BODY → SENDING_RESPONSE → CLOSED)
  - Event dispatch logic (EPOLLIN, EPOLLOUT, EPOLLERR events)
  - Upload/file streaming member initialization

### ✅ Layer 4: CGI Execution (Leny's Responsibility)
- **Status:** Mostly testable
- **Tests included:** `test_cgi_setup()`
- **What's covered:**
  - CGI request parsing
  - Query string extraction
  - Path resolution for CGI scripts
  - Note: Full fork/execution tested manually via `srcs/main.cpp`

---

## Running Tests

```bash
# Run all unit tests (recommended before each commit)
make test

# Or manually run the test binary
./tests/test_main

# Run the old manual test harness (exercises all layers end-to-end)
./webserv config/default.conf
```

---

## What's NOT Yet Testable (Waiting for Server Implementation)

These require the Server event loop to be implemented by Rasiol:
- ❌ Actual socket listening and accepting connections
- ❌ Non-blocking read/write operations
- ❌ Integration with epoll event loop
- ❌ Connection pooling and session management
- ❌ CGI pipe fd registration with epoll
- ❌ Timeout management (connection timeout, CGI timeout)
- ❌ Keep-Alive support
- ❌ Request body buffering and chunked encoding

---

## Code Quality Fixes Applied

The following issues were fixed in this session:

1. **🔧 CRITICAL BUG (Resource Leak)**: `upload_fd` was not initialized or closed
   - **Before:** Undefined behavior, file descriptor leak
   - **After:** Properly initialized to -1, closed in destructor

2. **⚡ Performance Optimization**: `setState()` string construction
   - **Before:** Created 4+ temporary std::string objects per state transition (hot path bloat)
   - **After:** Returns `const char*` — no allocations, much faster

3. **🧹 Code Cleanup**: Removed commented-out dead CGI code
   - **Before:** Maintenance debt from earlier development
   - **After:** Clean, focused interface

4. **📋 Initialization**: All members now properly initialized
   - **Before:** `expected_request_total` had garbage value
   - **After:** Initialized to 0

5. **📌 Refactored**: Event handling `handle()` method
   - **Before:** Repeated "close connection" check 3 times
   - **After:** Single check after callbacks, cleaner logic

---

## Next Steps for the Team

1. **Leny (Config + CGI):** 
   - Config parsing is fully tested ✓
   - CGI parsing tested ✓
   - Next: Integration testing with Server when event loop is ready

2. **Thomas (HTTP Parsing):**
   - All parsing layers are fully tested ✓
   - Next: Edge case testing (malformed requests, large bodies, chunked encoding)

3. **Rasiol (Server + Event):**
   - Connection class is ready ✓
   - Next: Implement `Server` event loop, wire `Connection` callbacks, integrate `EpollWrapper`

---

## Testing a Production Scenario

Once the Server event loop is implemented, you can run the comprehensive shell test:

```bash
bash test_connection.sh
```

This tests:
- 100 sequential connections
- 50 parallel connections
- 1MB POST bodies
- Chunked transfer encoding
- Abrupt client disconnects
- File descriptor leaks
- Non-blocking behavior
- Memory safety (if valgrind installed)

---

## Recommended Workflow

**Before each commit:**
```bash
make test          # Unit tests for all layers
make              # Full rebuild
./webserv config/default.conf  # Quick manual check
```

**For integration testing** (after Server is implemented):
```bash
bash test_connection.sh    # Comprehensive functional tests
```
