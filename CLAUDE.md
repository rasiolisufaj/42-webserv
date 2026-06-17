# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

42 School Webserv project: a non-blocking HTTP/1.1 web server in C++98, using `epoll` for I/O multiplexing. Must handle GET/POST/DELETE, static files, CGI execution, file uploads, and configurable virtual hosts — all without threads and without ever blocking.

## Build Commands

```bash
make          # build the webserv binary
make clean    # remove object files
make fclean   # remove object files and binary
make re       # fclean then rebuild
make test     # build and run tests/test_main.cpp (linked against all objects except main.o)
```

Objects live in `obj/`; the `make test` binary is written to `tests/test_main`. Builds use dependency tracking (`-MMD -MP`).

The `Makefile` compiles an **explicit `SRC` list** (not a wildcard glob), so a new `.cpp` must be added to that list to be built.

**Compiler flags:** `-std=c++98 -Wall -Wextra -Werror -MMD -MP`

**Run the server:**
```bash
./webserv config/default.conf
```

Example config files are in `config/` (`default.conf`, `eval_demo.conf`, `multipleServers.conf`, `nginx.conf`, `one_server.conf`, `tester.conf`). The binary takes exactly one argument (the config file path). `eval_demo.conf` is deliberately divergent from `default.conf` (different port `9090`, `server_name webserv.eval`, upload dir) but keeps the same route layout (`/`, `/cgi-bin`, `/upload_store`, `/errors`) the static dashboard frontend in `www/` depends on — it exists to prove the served site is config-portable.

## Architecture

Three developers, each owns a layer:

- **Leny** (`lellanos`) — config layer (`srcs/config/`, `includes/config/`)
- **Thomas** (`thodavid`) — HTTP parsing layer (`srcs/http/`, `includes/http/`)
- **Rasiol** (`risufaj`) — server/event layer (`srcs/server/`, `includes/server/`) + CGI layer (`srcs/cgi/`, `includes/cgi/`)

### Config layer

- `ConfigParser` reads and tokenizes the `.conf` file, validates directives, then populates `std::vector<ServerConfig>`. Entry point: `ConfigParser::parseConfig(path)`.
- `ServerConfig` holds per-server settings (`port`, `host`, `server_name`, `root`, `error_pages`, size limits, `upload_store`). Each contains a `std::vector<Location>` with per-route settings (`root`, `allowed_methods`, `autoindex`, `cgi` map, `upload_store`, `return_code`/`return_redir`). **Server-level defaults** (`ServerConfig::ServerConfig()` in `srcs/config/ServerConfig.cpp`): `port=8080`, `host=127.0.0.1`, `root=www`, `upload_store=www/upload_store`, `client_max_body_size=10485760` (10 MB), `client_max_header_size=8192`. A `Location` that omits `client_max_body_size` inherits the server's value (see `ConfigParser.cpp`); the `Location` struct's own pre-inheritance sentinel is `(size_t)-1` (SIZE_MAX = unlimited), which is also the value `_maxBodyLimitForPort` treats as "no limit".

### HTTP parsing layer

- `RequestLine` parses the first line into `method`, `target`, `version`, and `authority`. Internally delegates target parsing to `RequestTarget`.
- `RequestTarget` parses a URI target into `path` and `query`.
- `Headers` holds a case-normalized `std::map<std::string,std::string>`. Key methods: `parseLine()`, `getHeader()`, `hasHeader()`, `getContentLength()`, `wantsClose()`, `getHost()` (returns `HostInfo{name, port}`).
- `Request` ties the above together: `Request::parse(fullRequest)` populates `_requestLine`, `_headers`, and `_body`.
- `RequestContext` is the bridge to routing. Call `ctx.build(request, local_port)` to resolve the effective host (`effectiveHost()`), normalized `method()`, `path()`, `query()`, `bodySize()`, and `localPort()`. The server uses this to match `ServerConfig`/`Location`.
- `ConfigMatcher` routes a `RequestContext` to the right `ServerConfig` and `Location`. Call `matcher.match(ctx, servers)` which returns `RouteMatch{server, location}` (both raw `const` pointers). Server selection: prefers `server_name` match on the correct port, falls back to first config on that port, throws `std::runtime_error("400 Bad Request")` if no port matches. Location selection: longest-prefix match; throws `std::runtime_error("404 Not Found")` if no location matches.
- `ChunkDecoder` decodes `Transfer-Encoding: chunked` bodies via `clean_body_chunk(chunked_body)`. Max body size is configurable (default 100 MB).
- `utils_http.hpp` provides free functions: `to_lower_ascii()` and `hex_value()`.

### Response-building layer (HTTP side, `srcs/http/`)

This layer turns a routed request into a finished HTTP response **in memory**. It is wired into the event loop: `Server::_buildResponse(ctx, routeMatch)` builds a `RouteContext` and calls `ResponseBuilder::build`, then serializes `Response::toString()` into the client's `out_buffer`.

- `RouteContext` (`includes/config/RouteContext.hpp`, `srcs/config/RouteContext.cpp`) — the resolved, inheritance-flattened view of a route (formerly "EffectiveRoute"). `RouteContextBuilder::build(RouteMatch)` collapses server-vs-location inheritance into one struct: effective `root`, `upload_store`, `index` (defaults to `index.html`), `allow_methods`, `client_max_body_size`/`client_max_header_size`, `autoindex`, `return_code`/`return_url`, and the `cgi` map. Throws `500 Internal Server Error` if `server`/`location` is NULL or the effective `root`/`upload_store` resolves empty.
- `Response` — a built response: `_status_code`, `_reason`, `_headers` map, in-memory `_body` string. `toString()` serializes the full HTTP message. Default ctor is `200 OK`.
- `ResponseBuilder` — the decision engine: `build(ctx, route)` → `Response`. Decision order (nginx semantics): (1) `return` directive always wins, (2) `405` if method not allowed (with `Allow` header), (3) `413` if body too large, (4) `root`+`path` mapping, (5) dispatch GET/HEAD | POST | DELETE. The CGI boundary is detected by the server *before* `build()` is called, so this layer never handles CGI. Split across four `.cpp` files by responsibility:
  - `ResponseBuilder.cpp` — pure decision tree, no I/O (`dispatch`, `mapPath`, `isMethodAllowed`, `allowHeaderValue`).
  - `ResponseBuilderGet.cpp` — filesystem **reads**: GET/HEAD via `stat`, `serveFile` (reads the whole file into `Response._body` with `std::ifstream` — *in-memory, not streamed*), `autoindexResponse`, `redirectSlash` (301 trailing-slash).
  - `ResponseBuilderWrite.cpp` — filesystem **writes**: `handlePost` (upload), `handleDelete`. Both resolve the target via the shared `uploadTargetPath` helper (`upload_store + basename(path)`), so a file uploaded via `POST /x` is removable via `DELETE /x` — uploads and deletes operate on the same `upload_store`, not on `root + path`. (`upload_store` is always set: `RouteContextBuilder` throws if it resolves empty.)
  - `ResponseBuilderError.cpp` — `handleReturn` (config redirects) and `errorResponse` (error pages). The actual page lookup is the free function `make_error_response(code, server)` (declared in `ResponseBuilder.hpp`): it serves the server's `error_page` resolved against the **server root** (`root + uri`, e.g. `www` + `/errors/404.html` → `www/errors/404.html`) when present and readable, else a generated `default_error_body`. This is **shared with the server's pre-routing error path** (see `Server::_sendError`).
- `HttpStatus` (`srcs/http/HttpStatus.cpp`) — pure helpers: `reason_phrase(code)`, `status_from_exception(what)` (parses the `"<code> <reason>: ..."` convention thrown across the codebase back into an int, → 500 if malformed), `default_error_body(code)` (default HTML error page).
- `MimeTypes` (`srcs/http/MimeTypes.cpp`) — `mime_type(path)` maps a file extension to `Content-Type` (default `application/octet-stream`).

### CGI layer

- `CgiHandler` (`srcs/cgi/CGIHandler.cpp`, `includes/cgi/CGIHandler.hpp`) — static utility class, fully wired into the event loop. Two public methods:
  - `buildJob(ctx, server, loc, job)` → `bool` — populates a `CgiJob` from a routed request: resolves the interpreter by the script's file extension (`loc.cgi`), the effective document root (`loc.root` or, if empty, `server.root`), splits the script into `working_dir` + bare-filename `script_path` (the child `chdir`s into `working_dir`, so `script_path` is relative and the full path is exported via `PATH_TRANSLATED`/`SCRIPT_FILENAME`), copies the request body, and builds the CGI/1.1 env. Beyond the standard CGI/1.1 vars, it exports a non-standard **`UPLOAD_STORE`** (the route's effective `upload_store`, location-over-server, resolved to an **absolute** path against the server CWD because the child `chdir`s away before exec) so upload-listing scripts like `www/cgi-bin/list_uploads.py` find the upload dir regardless of config. Returns `false` (job untouched) when the target isn't a CGI request.
  - `execute(conn, job)` → `bool` — creates two pipes (parent ends set `O_NONBLOCK` + `FD_CLOEXEC`), `fork()`s, `dup2()`s stdin/stdout in the child, `chdir`s to `working_dir`, `execve`s the interpreter. The parent stores `cgi_in_fd`/`cgi_out_fd`/`cgi_pid`/`cgi_start_time` in the `Connection` and moves it to `CGI_WRITING_BODY`. Private helpers `allocateEnv`/`freeEnv` convert the env map to `char**`.
- `CgiJob` — plain struct holding job parameters: `interpreter`, `script_path`, `working_dir`, `method`, `query_string`, `url_path`, `body` (`std::vector<char>`), `env` map.
- **Event-loop integration lives in `CgiManager`** (`srcs/server/CgiManager.cpp`, `includes/server/CgiManager.hpp`) — the server-layer class that owns the CGI exchange on top of the single epoll loop (it was formerly spread across `Server`). `CgiHandler` remains the pure fork/exec utility; `CgiManager` is the lifecycle wrapper. It holds a reference back to the `Server` (and is a `friend` of it) so it reuses the Server's single-owner response/error transitions (`_queueResponse` / `_sendError`) instead of duplicating them. Public surface: `dispatch(conn, ctx, match)` — builds the job via `CgiHandler::buildJob`, forks, wires the pipes; returns `false` for a non-CGI target so the caller builds a static response. `onPipeOUT`/`onPipeIN` stream the request body → stdin / drain stdout into `conn->cgi_raw_output`. `teardown` does epoll-safe pipe + child cleanup (must run before the `Connection` is destroyed). `isActive`/`isExpired`/`timeout` implement the 5s-inactivity / 60s-hard cap → 504. Private helpers: `start` (fork + register pipes via `CgiPipeWrapper`), `closeInPipe`/`closePipe`, `finalize` (reap + `buildResponse`, 502 on empty/malformed), `buildResponse` (`Status:` header → status line). `Server` owns a `CgiManager _cgi` member (declared after `_epoll`), forwards `onCgiPipeIN`/`onCgiPipeOUT` to it, and calls `_cgi.dispatch` from `_processRequest`. See the Server/event layer below.

### Server/event layer

- `EpollWrapper` owns the epoll fd. Handler pointers are stored in `epoll_event.data.ptr` (not a separate map). Key methods: `addFd`, `modifyFd`, `removeFd`, `wait`, `getEvents`. Throws `std::runtime_error` on epoll syscall failure. `wait()` retries on `EINTR`. `MAX_EVENTS = 1024`.
- `IEventHandler` — pure interface. `virtual void handle(uint32_t event, IServerCallback &callback)`. Both `Socket` and `Connection` implement this.
- `IServerCallback` — pure interface implemented by `Server`. Callbacks: `onNewConnection(client_fd, listener_fd)`, `onClientIN(conn)`, `onClientOUT(conn)`, `onCloseConnection(fd, conn)`, `onCgiPipeIN(conn)`, `onCgiPipeOUT(conn)`.
- `Socket` — listening socket, implements `IEventHandler`. `setup()` creates, binds, and sets `O_NONBLOCK` + `FD_CLOEXEC`. `handle()` drains the accept queue in a loop and calls `callback.onNewConnection()`.
- `Connection` — per-client state machine implementing `IEventHandler`. States: `READING_HEADER → READING_BODY → SENDING_RESPONSE → CGI_WRITING_BODY → CGI_READING_OUTPUT → CLOSED`. Holds `in_buffer`/`out_buffer` (`std::vector<char>`), `read_idx`/`write_idx`, timeout fields (`last_activity`, `first_byte_time`), CGI fields (`cgi_in_fd`, `cgi_out_fd`, `cgi_pid`, `cgi_start_time`, `cgi_sent_idx`, `cgi_raw_output`, `cgi_job`, and the `cgi_in_wrapper`/`cgi_out_wrapper` pipe adapters — all initialized in the ctor; the dtor closes the pipes, `SIGKILL`s + reaps the child, and frees the wrappers as a safety net), file-streaming fields (`file_fd`, `file_bytes_sent`, `is_streaming_file`), upload-streaming fields (`upload_fd`, `body_bytes_expected`, `is_streaming_upload`), and `chunk_decoded`/`chunk_raw_offset` for resumable chunked decoding. `handle()` dispatches `EPOLLIN` → `onClientIN`, `EPOLLOUT` → `onClientOUT`, `EPOLLERR/EPOLLHUP` → `onCloseConnection`.
- `CgiPipeWrapper` (declared in `Connection.hpp`) — a tiny `IEventHandler` adapter holding `{Connection* conn, bool is_read_end}`, used as the `data.ptr` for the two CGI pipe fds. `handle()` forwards events to `onCgiPipeIN` (read end) or `onCgiPipeOUT` (write end), so CGI pipes ride the same single epoll loop as sockets.
- `Server` — owns `EpollWrapper`, a `CgiManager _cgi` (declared after `_epoll`; drives the CGI lifecycle — see CGI layer above), the shared `const std::vector<ServerConfig>& _configs`, `std::vector<Socket*> _listeners`, `std::map<int,int> _listenerPort` (listener fd → port), `std::map<int, Connection*> _connections`, and `_last_timeout_check` (throttles the timeout sweep to ~1Hz). Multiple server blocks on the same address share one listening socket. `run()` drives the event loop (`epoll.wait(1000ms)` → `_checkTimeouts()` → cast `data.ptr` → `IEventHandler::handle()`). **Stale-event guard (use-after-free safety):** one connection owns several epoll fds (client socket + the two CGI pipes), so handling one event in a batch can free a handler that a *later* event in the **same** `epoll_wait` batch still points to — e.g. a client `EPOLLHUP` tears the connection down (freeing its `Connection` and `CgiPipeWrapper`s) while its CGI pipe event is still queued, or `_checkTimeouts` (which runs before the dispatch loop, on the already-returned snapshot) closes a connection that has a pending event. The loop defends against this with `_retired`, a `std::set<const void*>` of handler pointers freed during the current batch: it is cleared each iteration **before** `_checkTimeouts`, every freed `Connection`/`CgiPipeWrapper` is recorded via `_retireHandler` (called from `onCloseConnection` for the socket and `CgiManager::closePipe` for each pipe wrapper), and the dispatch loop **skips any event whose `data.ptr` is in `_retired`** before casting/dereferencing it (it compares the pointer value only — never touches the freed object). `onClientIN` does only socket I/O (one `recv`, append to `in_buffer`, refresh timers) and hands off to **`_processRequest`**, which gates on **request completeness**: `_requestReadiness(in_buffer, port, errorStatus, consumed)` returns REQ_INCOMPLETE / REQ_COMPLETE / REQ_ERROR without mutating state, scanning `in_buffer` in place (only the header block is ever copied, never the body) — it waits for the full header block, capping it with an **early 431** against `_maxHeaderLimitForPort(port)` (the largest `client_max_header_size` of any server on that port — a server-level directive with **no per-location override**, unlike the body limit; `(size_t)-1` = unlimited): a completed header block over the limit, or buffered header-only bytes that already pass it before any `\r\n\r\n` terminator arrives, is rejected so a header flood / slowloris drip can't grow the buffer unbounded. It then waits for the body per `Content-Length` or, for `Transfer-Encoding: chunked`, delegates to `_chunkedReadiness`, which **walks the actual chunk framing** (mirroring `ChunkDecoder`'s rules) instead of byte-searching for `0\r\n\r\n` — so it isn't fooled by that sequence inside chunk data and correctly skips chunk extensions/trailers. Both paths enforce an **early 413** against `_maxBodyLimitForPort(port)` (the largest `client_max_body_size` of any server on that port; `(size_t)-1` = unlimited) as the body arrives, and on REQ_COMPLETE report `consumed` (the request's total byte length) so any pipelined bytes that follow are kept in the buffer. Only on REQ_COMPLETE does routing proceed: `_processRequest` parses exactly the consumed bytes, builds a `RequestContext` (arrival port from `_listenerPort`), calls `ConfigMatcher::match(ctx, _configs)`, then **dispatches CGI vs static** — if the method is allowed (`_isMethodAllowed`) and `_cgi.dispatch(conn, ctx, match)` matches a CGI interpreter, the CGI child runs and the response is produced asynchronously by the pipe callbacks; otherwise it falls through to `_queueResponse(conn, _buildResponse(ctx, routeMatch, keepAlive))`. `_buildResponse` flattens the `RouteMatch` into a `RouteContext` (via `RouteContextBuilder`) and runs `ResponseBuilder::build` → `Response::toString()`, and `_queueResponse` copies it into `conn->out_buffer`, moves to `SENDING_RESPONSE`, and arms `EPOLLOUT` — it is the **single place** that hands a finished response (static, CGI, or error) back to the event loop. The `Connection` header reflects the request's **keep-alive intent** (HTTP/1.1 defaults to keep-alive unless `Connection: close`; HTTP/1.0 the reverse — stored in `conn->keep_alive`). (`_isMethodAllowed` mirrors `RouteContextBuilder`'s resolution so a method disallowed on a CGI route bypasses CGI and falls through to the static path's canonical 405 + `Allow` + error page.) A `RouteContextBuilder` throw (e.g. unresolved root/`upload_store`) yields a 500 page. On a fully-drained response `onClientOUT` either closes (`keep_alive` false) or **resets the connection** to `READING_HEADER`, re-arms `EPOLLIN`, and immediately runs `_processRequest` again on any pipelined request still buffered. The in-memory `serveFile` in `ResponseBuilderGet` slurps whole files into `Response._body` (no file streaming). CGI pipe callbacks (`onCgiPipeIN`/`onCgiPipeOUT`) and the CGI branch of `_checkTimeouts` delegate to `_cgi` (`onPipeIN`/`onPipeOUT`/`isActive`/`isExpired`/`timeout`). `_sendError(conn, status)` (which forces `keep_alive` false) builds the error response through `make_error_response(status, conn->server_cfg)` so even pre-routing failures (parse/routing) serve the matching custom `error_page` with a body; the status code is recovered from the thrown exception via `status_from_exception` (so a bad method → 501, bad version → 505, oversize → 413, rather than collapsing to 400). `main()` installs `SIG_IGN` for `SIGPIPE` so a write to a dead CGI child returns `EPIPE` instead of killing the server, and installs a handler for `SIGINT`/`SIGTERM` that clears the global `volatile sig_atomic_t g_running` (defined in `main.cpp`). `Server::run()`'s loop condition is `while (_running && g_running)` (it declares `g_running` via `extern`), so a signal makes the loop return and the `Server`/`Connection`/`Socket` destructors run — a graceful shutdown (≤1s latency, bounded by the 1000ms `epoll.wait`). Because `tests/test_main.cpp` has its own `main()` and excludes `obj/main.o` from the link, it provides its own `g_running` definition.
- `SessionManager` — stub (empty `.cpp`).

### Utilities

- `Logger` — static class with levels `DEBUG/INFO/WARNING/ERROR` and ANSI color output. Use `Logger::log(Logger::INFO, msg)`. `Logger::setLogLevel()` suppresses messages below the threshold.
- `to_string<T>()` — C++98 substitute for `std::to_string`, defined in `Logger.hpp`. Used throughout the codebase.

## Current Implementation Status

**Implemented:**
- Config parsing (`ConfigParser`, `ServerConfig`, `Location`)
- Full HTTP parsing (`Request`, `RequestContext`, `Headers`, `RequestLine`, `RequestTarget`, `ChunkDecoder`)
- `ConfigMatcher` — routes requests to server block + location block
- Epoll event loop (`EpollWrapper`, `Socket`, `Server::run()`)
- Connection state machine (`Connection`, `onClientIN`, `onClientOUT`, `onCloseConnection`)
- **CGI end-to-end** (`CgiHandler::buildJob`/`execute` + `Server` dispatch and pipe callbacks): GET with query string, POST body streamed to stdin, `Status:` header → HTTP status, 502 on empty/malformed output, 5s-inactivity / 60s-hard timeout → 504, child kill+reap (no zombies), `O_NONBLOCK` + `FD_CLOEXEC` on pipe fds, `SIGPIPE` ignored.
- **Static response serving, wired end-to-end** (`ResponseBuilder` + `RouteContext`/`RouteContextBuilder`, `Response`, `HttpStatus`, `MimeTypes`, called from `Server::_buildResponse`): GET/HEAD, autoindex, 301 trailing-slash, POST/DELETE writes, `return` redirects, MIME typing. Verified with `curl` (200 index, dir→301, POST→201, DELETE, HEAD, 405+`Allow`, 413, 302 redirect).
- **Unified error handling** (`make_error_response`): both `ResponseBuilder` errors and the server's pre-routing errors serve custom `error_page` (resolved against server root) with a generated fallback; pre-routing status codes recovered via `status_from_exception` (501/505/413/400, not collapsed to 400).
- **Connection timeouts** (`_checkTimeouts`, swept ~1Hz from the event loop): two distinct deadlines for ordinary clients — a request mid-arrival (`first_byte_time != 0`) is closed once it exceeds the **60s absolute `REQUEST_TIMEOUT`** anchored to `first_byte_time` (a slowloris/slow-drip client can't defer this by trickling bytes, since the deadline is *not* reset by later reads), while a connection with no request in flight (freshly accepted but silent, or draining a response) is closed after **30s `CLIENT_TIMEOUT`** of `last_activity` inactivity; both close via `onCloseConnection`. CGI exchanges enforce 5s-inactivity / 60s-hard → 504. `last_activity` is reset on every client read/write and CGI I/O. Satisfies the subject's "a request must never hang indefinitely."

**Not yet implemented / stubs:**
- `srcs/main.cpp` is now the clean production entry point (the old hardcoded-request demo block was stripped): arg check → install signal handlers → `ConfigParser::parseConfig` → `Server(parser.getServers())` → `run()`. See the Server/event layer above for the `g_running` graceful-shutdown wiring.
- In-memory vs. streamed file serving unreconciled: `ResponseBuilderGet::serveFile` slurps whole files into `Response._body`; the `Connection` file/upload-streaming fields are unused. The whole request body is still fully buffered before routing; however, an **early 413** is now enforced as the body arrives via `_requestReadiness`/`_chunkedReadiness` (against `_maxBodyLimitForPort`), and `ResponseBuilder` re-checks 413 against the matched route's effective limit. The header block is likewise capped with a parallel **early 431** (against `_maxHeaderLimitForPort`) before the body is read.
- (Keep-alive is now implemented — see the Server/event layer above. Persistent connections, pipelining, and per-request `Connection`-header negotiation all work; errors and `EPOLLRDHUP` force close.)
- CGI POST body currently comes from `ctx.body()` (fully-parsed request); not yet sourced from streamed/chunked uploads (`Connection::chunk_decoded`)
- `SessionManager` — empty `.cpp`

## Config File Syntax

```nginx
server {
    listen 8080;               # or ip:port
    server_name example.com;
    client_max_body_size 1000000;
    client_max_header_size 8192;
    error_page 404 /errors/404.html;
    upload_store ./www/upload_store;

    location /path {
        allow_methods GET POST DELETE;
        root ./www/public;
        index index.html;
        autoindex on;
        return 303 https://example.com;
        upload_store ./www/uploads;
        cgi .py /usr/bin/python3;
        client_max_body_size 5000000;
    }
}
```

- Directives end with `;`. Location blocks use `{}`.
- `client_max_body_size` / `client_max_header_size` are in bytes and inherited from server if absent in location.
- `cgi` maps a file extension to an executable path; multiple `cgi` lines are allowed per location.
- Default `allowed_methods` if unset: `GET HEAD`.

## Key Constraints (42 rules)

- **C++98 only** — no `auto`, range-for, `std::to_string`, lambdas, or C++11 headers.
- **Single epoll loop** — all sockets (listening + client + CGI pipes) go through one `EpollWrapper::wait()` call.
- `fork()` is only permitted inside `CgiHandler::execute()` for CGI execution. All fds must have `FD_CLOEXEC` set before forking.
- All sockets must be set non-blocking (`O_NONBLOCK`).
- The server must never crash or hang regardless of input.

## Testing

```bash
make test     # compile and run tests/test_main.cpp
```

Manual testing with `curl`:
```bash
curl -v http://localhost:8080/
curl -X POST -d "data" http://localhost:8080/upload
curl -X DELETE http://localhost:8080/upload/file.txt
curl -X UNKNOWN http://localhost:8080/  # expect 501
```

`www/cgi-bin/` contains Python CGI scripts for manual CGI testing: `hello.py`, `list_uploads.py`, `loop.py` (infinite loop — exercises the CGI hard-timeout path), and `slow.py` (delayed output — exercises the 5s-inactivity timeout). Both `tests/test.py` and `tests/test_basic.sh` are placeholder stubs.

### Valgrind harness (`tests/valgrind/`)

Checks memory leaks, fd leaks, and leaked/zombie CGI children, filtering out prebuilt-library noise. Build with debug symbols first so traces carry file:line:

```bash
make re CXXFLAGS="-Wall -Wextra -Werror -std=c++98 -MMD -MP -g -O0"
tests/valgrind/run_valgrind.sh                          # raw report
tests/valgrind/run_valgrind.sh tests/valgrind/webserv.supp   # filtered (hide library still-reachable)
tests/valgrind/run_valgrind.sh --gen                    # dump generated suppressions for new library noise
```

`run_valgrind.sh` boots `./webserv` under valgrind, drives every request path (static GET/HEAD, autoindex, 404/405/501/413, redirect, CGI GET+POST, upload+delete, keep-alive/pipelining), then `SIGTERM`s for a clean shutdown so destructors run; the full report lands in `tests/valgrind/valgrind.log`. `webserv.supp` suppresses **library** allocations only and matches nothing in `srcs/`. Since `main()` now installs a `SIGINT`/`SIGTERM` handler that breaks `run()` (see Server/event layer), the harness `SIGTERM` unwinds the loop and runs all destructors: a debug-build run reports `All heap blocks were freed -- no leaks are possible`, `0 errors`, and only valgrind's own log fd open at exit (no fd leak, 0 zombie CGI children). `tests/valgrind/README.md` documents the older no-handler behavior ("still reachable" `main`-scope objects) for reference.
