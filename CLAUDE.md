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

**Compiler flags:** `-std=c++98 -Wall -Wextra -Werror`

**Run the server:**
```bash
./webserv config/default.conf
```

Example config files are in `config/`. The binary takes exactly one argument (the config file path).

## Architecture

The codebase is split across three active developers (feature branches per person):

**Config layer** (`srcs/config/`, `includes/config/`)
- `ConfigParser` reads and tokenizes the `.conf` file, validates directives, then populates `std::vector<ServerConfig>`.
- `ServerConfig` holds per-server settings; each contains a `std::vector<Location>` with per-route settings (root, methods, CGI map, upload_store, etc.).

**HTTP parsing layer** (`srcs/http/`, `includes/http/`)
- `Request::parse()` accepts a full raw HTTP string and populates `RequestLine` (method/target/version) and `Headers` (case-normalized field map).
- `Headers` exposes `getHeader()`, `getContentLength()`, `wantsClose()`.

**Server/event layer** (`srcs/server/`, `includes/server/`)
- `EpollWrapper` owns the epoll fd. Handler pointers are stored in `epoll_event.data.ptr` (not a separate map). Key methods: `addFd`, `modifyFd`, `removeFd`, `wait`, `getEvents`. Throws `std::runtime_error` on epoll syscall failure.
- `IEventHandler` / `IServerCallback` are pure interfaces. `Socket` (listening socket) and `Connection` (client connection) implement `IEventHandler`. The `Server` class implements `IServerCallback` and drives the main event loop.
- `IServerCallback` callbacks: `onNewConnection`, `onClientIN`, `onClientOUT`, `onCloseConnection`, `onCgiPipeIN`, `onCgiPipeOUT`.
- `SessionManager` tracks active `Connection` objects.
- CGI is handled by `CGIHandler` — it forks, sets environment variables, pipes body in via stdin, and reads CGI stdout back through epoll (non-blocking).
- **Note:** `Server.cpp`, `Connection.cpp`, `CGIHandler.cpp`, and `SessionManager.cpp` are currently empty stubs. The server event loop is not yet wired to the config or HTTP layers.

**Utilities** (`srcs/utils/`, `includes/utils/`)
- `Logger` is a static utility class with levels DEBUG/INFO/WARNING/ERROR and ANSI color output. Use `Logger::log(Logger::INFO, msg)`.
- `to_string<T>()` is a C++98 substitute for `std::to_string`, defined in `Logger.hpp`.

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

## Key Constraints (42 rules)

- **C++98 only** — no `auto`, range-for, `std::to_string`, lambdas, or C++11 headers.
- **Single epoll loop** — all sockets (listening + client + CGI pipes) go through one `EpollWrapper::wait()` call.
- `fork()` is only permitted inside `CGIHandler` for CGI execution.
- All sockets must be set non-blocking (`O_NONBLOCK`).
- The server must never crash or hang regardless of input.

## Static Content & Test Files

- `www/` contains test HTML, error pages, CSS/JS, and `upload_store/`.
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
