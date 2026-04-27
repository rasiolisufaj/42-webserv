# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

42 School Webserv — a custom HTTP/1.1 server in C++98. Non-blocking, event-driven (single `poll()`/`epoll()` loop), no threads, no external libraries. Handles GET/POST/DELETE, static files, CGI scripts, file uploads, and Nginx-like config files.

## Build Commands

```bash
make          # build ./webserv
make clean    # remove object files
make fclean   # remove objects + binary
make re       # fclean + all
```

**Required compile flags:** `-std=c++98 -Wall -Wextra -Werror`

**Run:** `./webserv config/webserv.conf`

**Test manually:**
```bash
curl -v http://localhost:8080/
curl -X POST -d "data" http://localhost:8080/upload
curl -X DELETE http://localhost:8080/upload/file.txt
curl -X UNKNOWN http://localhost:8080/   # should return 501
ab -c 100 -n 1000 http://localhost:8080/ # concurrency test
```

## Architecture

The server is split into 7 modules:

### `srcs/server/`
- **Server** — orchestrates everything: opens listening sockets, runs the epoll event loop, dispatches to other modules
- **EpollWrapper** — abstraction over `epoll`/`poll`; tracks all file descriptors and their readiness events
- **Socket** — low-level: `socket()`, `bind()`, `listen()`, `accept()`, `fcntl(O_NONBLOCK)`
- **Connection** — per-client state machine: accumulates read buffer, tracks parse state, holds pending response data
- **SessionManager** — map of FD → Connection, handles cleanup on disconnect

### `srcs/http/`
- **RequestParser** — stateful HTTP/1.1 parser: splits `\r\n\r\n`, handles chunked transfer encoding (must fully unchunk before CGI)
- **Request** — parsed request model (method, URI, headers map, body)
- **Response** — response builder: sets status line, headers, body; serializes to wire format
- **HttpHandler** — routing logic: maps URI to config location, dispatches to file serving, CGI, upload, or error response

### `srcs/config/`
- **ConfigParser** — parses Nginx-like config file into `GlobalConfig → ServerConfig → LocationConfig` hierarchy

### `srcs/cgi/`
- **CGIHandler** — `fork()` + `exec()`, `dup2()` for stdin/stdout pipes, sets required env vars (`REQUEST_METHOD`, `CONTENT_LENGTH`, `QUERY_STRING`, etc.), captures stdout as response body. **Only place `fork()` is allowed.**

### `srcs/utils/`
- **utils** — MIME type lookup, string helpers, file I/O wrappers

### Data flow
```
main.cpp → ConfigParser → Server::init() (bind ports)
         → Server event loop (EpollWrapper)
              ↓ EPOLLIN on listen fd
           Socket::accept() → new Connection → SessionManager
              ↓ EPOLLIN on client fd
           Connection reads → RequestParser → Request
              ↓
           HttpHandler routes → file/CGI/upload
              ↓
           Response → Connection write buffer → EPOLLOUT → send()
```

## Critical Constraints

- **Single poll/epoll loop** — all I/O goes through one loop. Using multiple loops or blocking I/O is a disqualifying bug.
- **All sockets non-blocking** — always `fcntl(fd, F_SETFL, O_NONBLOCK)`. Handle `EAGAIN`/`EWOULDBLOCK` without error.
- **No fork except CGI** — spawning processes elsewhere violates the subject rules.
- **C++98 only** — no `auto`, range-for, `std::to_string`, `nullptr`, lambdas, or C++11+ features.
- **Never crash** — invalid requests return 4xx/5xx, never segfault or hang.

## Config File Directives

```nginx
server {
    listen 8080;
    server_name example.com;
    root /var/www;
    index index.html;
    error_page 404 /errors/404.html;
    client_max_body_size 10M;

    location / {
        allow_methods GET HEAD;
        autoindex off;
    }
    location /upload {
        allow_methods POST DELETE;
        upload_store /var/www/uploads;
    }
    location /cgi-bin {
        cgi_handler .py /usr/bin/python3;
    }
    location /old {
        return 301 /new;
    }
}
```

## HTTP Behavior Requirements

- **GET** — serve file or autoindex directory listing
- **POST** — CGI input or file upload
- **DELETE** — remove file
- **HEAD** — same headers as GET, no body
- **Other methods** → `501 Not Implemented`
- Enforce `client_max_body_size` → `413 Request Entity Too Large`
- Persistent connections unless `Connection: close` header
- CRLF (`\r\n`) line endings in responses
- Required response headers: `Content-Length`, `Content-Type`, `Connection`

## Test Infrastructure

- `tests/test_basic.sh` — shell tests (needs implementation)
- `tests/test.py` — Python tests (needs implementation)
- `www/` — static test content with sample HTML, error pages, CGI scripts, and an upload directory
- `config/tester.conf` — config tuned for automated testing

## Common Pitfalls

- Forgetting `O_NONBLOCK` on accepted client sockets (not just listening sockets)
- Not handling partial reads — HTTP data may arrive across multiple `recv()` calls; accumulate in a buffer until `\r\n\r\n`
- Chunked encoding: must unchunk request bodies before passing to CGI
- Config inheritance: `location` blocks inherit `root` from parent `server` unless overridden
- `recv()` returning 0 means client disconnected — close and remove from epoll immediately
- `make` must succeed with zero warnings under `-Werror`
