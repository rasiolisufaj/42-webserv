# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

42 School Webserv project: a non-blocking HTTP/1.1 web server in C++98, using `epoll` for I/O multiplexing. Must handle GET/POST/DELETE, static files, CGI execution, file uploads, and configurable virtual hosts — all without threads and without ever blocking.

## Build Commands

```bash
make          # build the webserv binary
make clean    # remove object files
make fclean   # remove object files and binary
make test     # build and run tests/test_main.cpp (linked against all objects except main.o)
```

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
- `ConfigMatcher` routes a `RequestContext` to the right `ServerConfig` and `Location`. Call `matcher.match(ctx, servers)` which returns `RouteMatch{server, location}`. Server selection: prefers `server_name` match on the correct port, falls back to first config on that port, throws `std::runtime_error("400 Bad Request")` if no port matches. Location selection: longest-prefix match; throws `std::runtime_error("404 Not Found")` if no location matches.
- `ChunkDecoder` decodes `Transfer-Encoding: chunked` bodies via `clean_body_chunk(chunked_body)`. Max body size is configurable (default 100 MB).
- `utils_http.hpp` provides free functions: `to_lower_ascii()` and `hex_value()`.

### CGI layer

- `Cgi` (`srcs/cgi/Cgi.cpp`, `includes/cgi/Cgi.hpp`) — fully implemented. Forks a child process via `execute(argv, envp)`, wires up two pipes (stdin/stdout), and returns the child `pid_t` to the caller. The parent retains `getFdInWrite()` (write body to CGI) and `getFdOutRead()` (read CGI response). Key methods:
  - `setUpEnvironment(ctx, loc)` — builds the CGI env map from a `RequestContext` and `Location`.
  - `setupArgv(loc, envMap)` — resolves the interpreter from `loc.cgi` keyed by file extension, returns `char**[interpreter, script_path, NULL]`.
  - `convertMapToEnvp(envMap)` — converts `std::map` to `char**` for `execve`.
  - `execute(argv, envp)` — creates pipes, `fork()`s, `dup2()`s, calls `execve` in child; parent closes unused ends and returns `pid`.
  - `free_array(char**)` — frees heap-allocated `char**` arrays.
- `CgiJob` — plain struct holding job parameters: `interpreter`, `script_path`, `working_dir`, `method`, `query_string`, `url_path`, `body` (`std::vector<char>`), `env` map. Not yet wired into the event loop.

### Server/event layer

- `EpollWrapper` owns the epoll fd. Handler pointers are stored in `epoll_event.data.ptr` (not a separate map). Key methods: `addFd`, `modifyFd`, `removeFd`, `wait`, `getEvents`. Throws `std::runtime_error` on epoll syscall failure. `wait()` retries on `EINTR`. `MAX_EVENTS = 1024`.
- `IEventHandler` — pure interface. `virtual void handle(uint32_t event, IServerCallback &callback)`. Both `Socket` and `Connection` implement this.
- `IServerCallback` — pure interface implemented by `Server`. Callbacks: `onNewConnection(client_fd, listener_fd)`, `onClientIN(conn)`, `onClientOUT(conn)`, `onCloseConnection(fd, conn)`, `onCgiPipeIN(conn)`, `onCgiPipeOUT(conn)`.
- `Socket` — listening socket, implements `IEventHandler`. `setup()` creates, binds, and sets `O_NONBLOCK` + `FD_CLOEXEC`. `handle()` drains the accept queue in a loop and calls `callback.onNewConnection()`.
- `Connection` — per-client state machine implementing `IEventHandler`. States: `READING_HEADER → READING_BODY → SENDING_RESPONSE → CGI_WRITING_BODY → CGI_READING_OUTPUT → CLOSED`. Holds `in_buffer`/`out_buffer` (`std::vector<char>`), `read_idx`/`write_idx`, timeout fields (`last_activity`, `first_byte_time`), file-streaming fields (`file_fd`, `file_bytes_sent`, `is_streaming_file`), upload-streaming fields (`upload_fd`, `body_bytes_expected`, `is_streaming_upload`), and `chunk_decoded`/`chunk_raw_offset` for resumable chunked decoding. `handle()` dispatches `EPOLLIN` → `onClientIN`, `EPOLLOUT` → `onClientOUT`, `EPOLLERR/EPOLLHUP` → `onCloseConnection`.
- `Server` — owns `EpollWrapper`, `std::vector<Socket*>`, and `std::map<int, Connection*>`. `run()` drives the event loop (`epoll.wait(1000ms)` → cast `data.ptr` → `IEventHandler::handle()`). `_selectConfig()` picks `ServerConfig` by matching `Host` header against `server_name`. `_buildResponse()` is currently a **Phase 6 stub** that echoes method/target; real file serving and CGI are not yet wired. `onCgiPipeIN`/`onCgiPipeOUT` are stub placeholders.
- `SessionManager` — stub (empty `.cpp`).

### Utilities

- `Logger` — static class with levels `DEBUG/INFO/WARNING/ERROR` and ANSI color output. Use `Logger::log(Logger::INFO, msg)`. `Logger::setLogLevel()` suppresses messages below the threshold.
- `to_string<T>()` — C++98 substitute for `std::to_string`, defined in `Logger.hpp`. Used throughout the codebase.

## Current Implementation Status

**Implemented:**
- Config parsing (`ConfigParser`, `ServerConfig`, `Location`)
- Full HTTP parsing (`Request`, `RequestContext`, `Headers`, `RequestLine`, `RequestTarget`, `ChunkDecoder`)
- `ConfigMatcher` — routes requests to server block + location block
- CGI execution (`Cgi::execute`, `setUpEnvironment`, `setupArgv`)
- Epoll event loop (`EpollWrapper`, `Socket`, `Server::run()`)
- Connection state machine (`Connection`, `onClientIN`, `onClientOUT`, `onCloseConnection`)

**Not yet implemented / stubs:**
- `Server::_buildResponse()` — Phase 6 stub: echoes method/target as plain text
- `Server::onCgiPipeIN` / `onCgiPipeOUT` — CGI pipe integration with epoll
- `SessionManager` — empty `.cpp`
- `O_NONBLOCK` + `FD_CLOEXEC` on `Cgi` pipe fds before forking
- Real response building: static file serving, error pages, autoindex, redirects, upload handling
- Connection timeout enforcement (`last_activity`, `first_byte_time` fields exist but no timeout logic)

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
- `fork()` is only permitted inside `Cgi::execute()` for CGI execution. All fds must have `FD_CLOEXEC` set before forking.
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
