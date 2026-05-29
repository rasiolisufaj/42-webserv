# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

42 School Webserv project: a non-blocking HTTP/1.1 web server in C++98, using `epoll` for I/O multiplexing. Must handle GET/POST/DELETE, static files, CGI execution, file uploads, and configurable virtual hosts — all without threads and without ever blocking.

## Build Commands

```bash
make          # build the webserv binary
make re       # clean rebuild
make clean    # remove object files
make fclean   # remove object files and binary
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
- `RequestContext` is the bridge to routing. Call `ctx.build(request, local_port)` to resolve the effective host (`effectiveHost()`), normalized `method()`, `path()`, `query()`, and `bodySize()`. The server will use this to match `ServerConfig`/`Location`.
- `ChunkDecoder` decodes `Transfer-Encoding: chunked` bodies via `clean_body_chunk(chunked_body)`. Max body size is configurable (default 100 MB).
- `utils_http.hpp` provides free functions: `to_lower_ascii()` and `hex_value()`.

### CGI layer

- `Cgi` (`srcs/cgi/Cgi.cpp`, `includes/cgi/Cgi.hpp`) — **implemented**. Forks a child process via `execute(argv, envp)`, wires up two pipes (stdin/stdout), and returns the child `pid_t` to the caller. The parent retains `getFdInWrite()` (write body to CGI) and `getFdOutRead()` (read CGI response). Key methods:
  - `setUpEnvironment(ctx, loc)` — builds the CGI env map from a `RequestContext` and `Location`.
  - `setupArgv(loc, envMap)` — resolves the interpreter from `loc.cgi` keyed by file extension, returns `char**[interpreter, script_path, NULL]`.
  - `convertMapToEnvp(envMap)` — converts `std::map` to `char**` for `execve`.
  - `execute(argv, envp)` — creates pipes, `fork()`s, `dup2()`s, calls `execve` in child; parent closes unused ends and returns `pid`.
  - `free_array(char**)` — frees heap-allocated `char**` arrays.
- `CgiJob` (`srcs/cgi/CgiJob.hpp`) — plain struct holding job parameters: `interpreter`, `script_path`, `working_dir`, `method`, `query_string`, `url_path`, `body` (`std::vector<char>`), `env` map. Intended to decouple the HTTP layer from the CGI executor (not yet wired into the event loop).

### Server/event layer

- `EpollWrapper` owns the epoll fd. Handler pointers are stored in `epoll_event.data.ptr` (not a separate map). Key methods: `addFd`, `modifyFd`, `removeFd`, `wait`, `getEvents`. Throws `std::runtime_error` on epoll syscall failure. `wait()` retries on `EINTR`.
- `IEventHandler` — pure interface. `virtual void handle(uint32_t event, IServerCallback &callback)`. Both listening sockets and connections implement this.
- `IServerCallback` — pure interface to be implemented by `Server`. Callbacks: `onNewConnection(client_fd, listener_fd)`, `onClientIN(conn)`, `onClientOUT(conn)`, `onCloseConnection(fd, conn)`, `onCgiPipeIN(conn)`, `onCgiPipeOUT(conn)`. All callbacks except `onNewConnection` take a `Connection*`.
- `Socket` — listening socket, implements `IEventHandler`. `setup()` creates, binds, and sets `O_NONBLOCK` + `FD_CLOEXEC`. `handle()` drains the accept queue in a loop and calls `callback.onNewConnection()`.
- `Connection` (`includes/server/Connection.hpp`) — header exists but is a stub (`#pragma once` + `#include "CgiJob.hpp"`); `srcs/server/Connection.cpp` is empty.

**Not yet implemented** (`.cpp` files are empty, no header in `includes/` unless noted):
- `srcs/server/Server.cpp` — main event loop, drives `EpollWrapper::wait()` and dispatches to `IEventHandler::handle()`
- `srcs/server/Connection.cpp` — per-client state machine (read buffer → `Request::parse` → build response → write)
- `srcs/server/SessionManager.cpp` — owns/tracks active `Connection` objects

> **Note:** `srcs/main.cpp` is currently a manual test harness that exercises the config parser, HTTP parsing, and CGI execution end-to-end with hardcoded requests. It does not start the epoll event loop.

### TODO — server layer integration

- [ ] Define `Connection` class (fd, read/write buffers, state machine, associated `Cgi` instance)
- [ ] Implement `Server` (event loop: `EpollWrapper::wait()` → cast `data.ptr` → `IEventHandler::handle()`)
- [ ] Implement `SessionManager` (map `fd → Connection*`, lifecycle management)
- [ ] Wire `Cgi` pipe fds into epoll (`onCgiPipeIN`/`onCgiPipeOUT` callbacks)
- [ ] Set `O_NONBLOCK` + `FD_CLOEXEC` on `Cgi` pipe fds before forking (currently missing)
- [ ] Replace `main.cpp` test harness with the real server startup sequence

### Utilities

- `Logger` — static class with levels `DEBUG/INFO/WARNING/ERROR` and ANSI color output. Use `Logger::log(Logger::INFO, msg)`. `Logger::setLogLevel()` suppresses messages below the threshold.
- `to_string<T>()` — C++98 substitute for `std::to_string`, defined in `Logger.hpp`. Used throughout the codebase.

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

## Static Content & Test Files

- `www/` contains test HTML, error pages, CSS/JS, and `upload_store/`.
- `www/cgi-bin/` contains Python CGI scripts for manual testing.
- `config/` contains example `.conf` files: `default.conf`, `webserv.conf`, `multipleServers.conf`, `tester.conf`, `pretty.conf`, `siege.conf`.

## Testing

Manual testing with `curl`:
```bash
curl -v http://localhost:8080/
curl -X POST -d "data" http://localhost:8080/upload
curl -X DELETE http://localhost:8080/upload/file.txt
curl -X UNKNOWN http://localhost:8080/  # expect 501
```

Both `tests/test.py` and `tests/test_basic.sh` are placeholder stubs with no actual tests.
