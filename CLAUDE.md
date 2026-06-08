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

**Compiler flags:** `-std=c++98 -Wall -Wextra -Werror -MMD -MP`

**Run the server:**
```bash
./webserv config/default.conf
```

Example config files are in `config/`. The binary takes exactly one argument (the config file path).

## Architecture

Three developers, each owns a layer:

- **Leny** (`lellanos`) — config layer (`srcs/config/`, `includes/config/`) + CGI layer (`srcs/cgi/`, `includes/cgi/`)
- **Thomas** (`thodavid`) — HTTP parsing layer (`srcs/http/`, `includes/http/`)
- **Rasiol** (`risufaj`) — server/event layer (`srcs/server/`, `includes/server/`)

### Config layer

- `ConfigParser` reads and tokenizes the `.conf` file, validates directives, then populates `std::vector<ServerConfig>`. Entry point: `ConfigParser::parseConfig(path)`.
- `ServerConfig` holds per-server settings (`port`, `host`, `server_name`, `root`, `error_pages`, size limits, `upload_store`). Each contains a `std::vector<Location>` with per-route settings (`root`, `allowed_methods`, `autoindex`, `cgi` map, `upload_store`, `return_code`/`return_redir`). `client_max_body_size` defaults to `(size_t)-1` (unlimited) if absent.

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
  - `buildJob(ctx, server, loc, job)` → `bool` — populates a `CgiJob` from a routed request: resolves the interpreter by the script's file extension (`loc.cgi`), the effective document root (`loc.root` or, if empty, `server.root`), splits the script into `working_dir` + bare-filename `script_path` (the child `chdir`s into `working_dir`, so `script_path` is relative and the full path is exported via `PATH_TRANSLATED`/`SCRIPT_FILENAME`), copies the request body, and builds the CGI/1.1 env. Returns `false` (job untouched) when the target isn't a CGI request.
  - `execute(conn, job)` → `bool` — creates two pipes (parent ends set `O_NONBLOCK` + `FD_CLOEXEC`), `fork()`s, `dup2()`s stdin/stdout in the child, `chdir`s to `working_dir`, `execve`s the interpreter. The parent stores `cgi_in_fd`/`cgi_out_fd`/`cgi_pid`/`cgi_start_time` in the `Connection` and moves it to `CGI_WRITING_BODY`. Private helpers `allocateEnv`/`freeEnv` convert the env map to `char**`.
- `CgiJob` — plain struct holding job parameters: `interpreter`, `script_path`, `working_dir`, `method`, `query_string`, `url_path`, `body` (`std::vector<char>`), `env` map.
- **Event-loop integration lives in `Server`** (not in the CGI layer): `_startCgi` registers both pipes with epoll via `CgiPipeWrapper` adapters; `onCgiPipeOUT` streams the request body to stdin then closes it; `onCgiPipeIN` drains stdout into `conn->cgi_raw_output`; `_finalizeCgi` reaps the child + `_buildCgiResponse` parses the CGI headers (`Status:` → status line) into an HTTP response (502 on empty/malformed); `_checkTimeouts`/`_timeoutCgi` enforce a 5s-inactivity / 60s-hard cap (→ 504). See the Server/event layer below.

### Server/event layer

- `EpollWrapper` owns the epoll fd. Handler pointers are stored in `epoll_event.data.ptr` (not a separate map). Key methods: `addFd`, `modifyFd`, `removeFd`, `wait`, `getEvents`. Throws `std::runtime_error` on epoll syscall failure. `wait()` retries on `EINTR`. `MAX_EVENTS = 1024`.
- `IEventHandler` — pure interface. `virtual void handle(uint32_t event, IServerCallback &callback)`. Both `Socket` and `Connection` implement this.
- `IServerCallback` — pure interface implemented by `Server`. Callbacks: `onNewConnection(client_fd, listener_fd)`, `onClientIN(conn)`, `onClientOUT(conn)`, `onCloseConnection(fd, conn)`, `onCgiPipeIN(conn)`, `onCgiPipeOUT(conn)`.
- `Socket` — listening socket, implements `IEventHandler`. `setup()` creates, binds, and sets `O_NONBLOCK` + `FD_CLOEXEC`. `handle()` drains the accept queue in a loop and calls `callback.onNewConnection()`.
- `Connection` — per-client state machine implementing `IEventHandler`. States: `READING_HEADER → READING_BODY → SENDING_RESPONSE → CGI_WRITING_BODY → CGI_READING_OUTPUT → CLOSED`. Holds `in_buffer`/`out_buffer` (`std::vector<char>`), `read_idx`/`write_idx`, timeout fields (`last_activity`, `first_byte_time`), CGI fields (`cgi_in_fd`, `cgi_out_fd`, `cgi_pid`, `cgi_start_time`, `cgi_sent_idx`, `cgi_raw_output`, `cgi_job`, and the `cgi_in_wrapper`/`cgi_out_wrapper` pipe adapters — all initialized in the ctor; the dtor closes the pipes, `SIGKILL`s + reaps the child, and frees the wrappers as a safety net), file-streaming fields (`file_fd`, `file_bytes_sent`, `is_streaming_file`), upload-streaming fields (`upload_fd`, `body_bytes_expected`, `is_streaming_upload`), and `chunk_decoded`/`chunk_raw_offset` for resumable chunked decoding. `handle()` dispatches `EPOLLIN` → `onClientIN`, `EPOLLOUT` → `onClientOUT`, `EPOLLERR/EPOLLHUP` → `onCloseConnection`.
- `CgiPipeWrapper` (declared in `Connection.hpp`) — a tiny `IEventHandler` adapter holding `{Connection* conn, bool is_read_end}`, used as the `data.ptr` for the two CGI pipe fds. `handle()` forwards events to `onCgiPipeIN` (read end) or `onCgiPipeOUT` (write end), so CGI pipes ride the same single epoll loop as sockets.
- `Server` — owns `EpollWrapper`, the shared `const std::vector<ServerConfig>& _configs`, `std::vector<Socket*> _listeners`, `std::map<int,int> _listenerPort` (listener fd → port), `std::map<int, Connection*> _connections`, and `_last_timeout_check` (throttles the timeout sweep to ~1Hz). Multiple server blocks on the same address share one listening socket. `run()` drives the event loop (`epoll.wait(1000ms)` → `_checkTimeouts()` → cast `data.ptr` → `IEventHandler::handle()`). Routing is delegated: `onClientIN` builds a `RequestContext` (arrival port from `_listenerPort`), calls `ConfigMatcher::match(ctx, _configs)`, then **dispatches CGI vs static** — if `CgiHandler::buildJob` matches, it calls `_startCgi` (forks the child, parks the client socket on `EPOLLRDHUP`, registers both pipes); otherwise it falls through to `_buildResponse(ctx, routeMatch)`, which flattens the `RouteMatch` into a `RouteContext` (via `RouteContextBuilder`) and runs `ResponseBuilder::build` → `Response::toString()` into `conn->out_buffer`, forcing `Connection: close` (keep-alive not yet implemented). A `RouteContextBuilder` throw (e.g. unresolved root/`upload_store`) yields a default 500 page. Note the in-memory `serveFile` in `ResponseBuilderGet` does **not** use the `Connection` file/upload-streaming fields (`file_fd`/`is_streaming_file`/`upload_fd`/...), which remain unused — the two approaches still need reconciling. CGI callbacks are fully implemented: `onCgiPipeOUT` (stream body → stdin), `onCgiPipeIN` (drain stdout), `_finalizeCgi`/`_buildCgiResponse` (reap + parse → HTTP, 502 on empty/malformed), `_teardownCgi`/`_closeCgiInPipe` (epoll-safe pipe/child cleanup), `_checkTimeouts`/`_timeoutCgi` (5s-inactivity / 60s-hard → 504). `_sendError(conn, fd, status)` builds the error response through `make_error_response(status, conn->server_cfg)` so even pre-routing failures (parse/routing) serve the matching custom `error_page` with a body; the status code is recovered from the thrown exception via `status_from_exception` (so a bad method → 501, bad version → 505, oversize → 413, rather than collapsing to 400). `main()` installs `SIG_IGN` for `SIGPIPE` so a write to a dead CGI child returns `EPIPE` instead of killing the server.
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
- **Connection timeouts** (`_checkTimeouts`, swept ~1Hz from the event loop): non-CGI clients idle ≥30s are closed via `onCloseConnection` (slowloris/stall guard); CGI exchanges enforce 5s-inactivity / 60s-hard → 504. `last_activity` is reset on every client read/write and CGI I/O. Satisfies the subject's "a request must never hang indefinitely."

**Not yet implemented / stubs:**
- `srcs/main.cpp` is currently a **developer scratchpad**: it parses a hardcoded request and demos the matcher/`Response` before constructing the real `Server` and calling `run()`. The demo block above `Server server(all_servers)` is not part of the production path and should be stripped.
- In-memory vs. streamed file serving unreconciled: `ResponseBuilderGet::serveFile` slurps whole files into `Response._body`; the `Connection` file/upload-streaming fields are unused. The whole request body is also fully buffered (and `client_max_body_size`/413 only enforced) *after* the complete body is read.
- Keep-alive: responses force `Connection: close` and `onClientOUT` always closes; `closeAfterWrite` + keep-alive fields are unused.
- CGI POST body currently comes from `ctx.body()` (fully-parsed request); not yet sourced from streamed/chunked uploads (`Connection::chunk_decoded`)
- `SessionManager` — empty `.cpp`
- `first_byte_time` is initialized but unused — the idle timeout relies solely on `last_activity` (a single flat timer, not a separate header/body deadline).

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

`www/cgi-bin/` contains Python CGI scripts (`cgi_post.py`, `sum.py`) and a shell script (`status.sh`) for manual CGI testing. Both `tests/test.py` and `tests/test_basic.sh` are placeholder stubs.
