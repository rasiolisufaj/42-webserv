# Webserv

A non-blocking **HTTP/1.1 web server** written in **C++98**, built for the 42 School curriculum.

It serves static files, runs CGI scripts, handles file uploads, and supports multiple virtual hosts. All from a single `epoll` event loop, with no threads and without ever blocking.

## Features

- **HTTP/1.1** with `GET`, `HEAD`, `POST`, and `DELETE`
- **Single `epoll` loop** - every socket (listening, client, and CGI pipes) is non-blocking and goes through one event loop
- **Static file serving** with automatic directory listing (`autoindex`) and MIME typing
- **CGI execution** (e.g. Python) with query strings, POST body streaming, and timeouts
- **File uploads & deletion** via `POST` / `DELETE`
- **Virtual hosts** - multiple `server` blocks, matched by host name and port
- **Keep-alive & pipelining** - persistent connections with per-request negotiation
- **Custom error pages** and configurable redirects (`return`)
- **Chunked transfer decoding** (`Transfer-Encoding: chunked`)
- **Connection timeouts** so a request can never hang forever
- **Configurable limits** - body size, header size, allowed methods (per server and per route)
- Graceful shutdown on `SIGINT` / `SIGTERM`

## Build & Run

```bash
make                       # build the ./webserv binary
./webserv config/default.conf
```

The binary takes exactly **one argument**: the path to a configuration file.

| Command       | Description                          |
|---------------|--------------------------------------|
| `make`        | Build the server                     |
| `make clean`  | Remove object files                  |
| `make fclean` | Remove object files and binary       |
| `make re`     | Rebuild from scratch                 |
| `make test`   | Build and run the test binary        |

**Requirements:** a C++98 compiler (`c++`), `make`, and Linux (`epoll`). Python 3 is needed only for the example CGI scripts.

## Quick Test

```bash
curl -v http://localhost:8080/                       # static page
curl -X POST -d "hello" http://localhost:8080/upload # upload
curl -X DELETE http://localhost:8080/upload/file.txt # delete
curl http://localhost:8080/cgi-bin/hello.py          # CGI
```

## Server Communication

The whole server runs on **one `epoll` loop**. `epoll` only tells us *"fd X is ready"* - it doesn't know what that fd means. To keep the loop simple, we split responsibilities across **two small pure interfaces**:

- **`IEventHandler`** - *"I am an fd that knows how to react to an event."*
  Implemented by `Socket` (listening socket), `Connection` (a client), and `CgiPipeWrapper` (a CGI pipe). Each handler is stored directly inside the epoll event, so when the fd fires we call `handle(event, callback)` and the right object reacts.

- **`IServerCallback`** - *"I am the Server, I own the shared state."*
  Implemented only by `Server`. A handler should not manage connections, routing, or the connection map by itself - instead it **calls back** into the Server (`onNewConnection`, `onClientIN`, `onClientOUT`, `onCloseConnection`, `onCgiPipeIN`, `onCgiPipeOUT`).

**Why two interfaces?** It removes the need for a big `switch` on fd type and keeps state in one place. The event loop stays tiny ("cast the pointer, call `handle`"), each fd type lives in its own class, and only the Server touches shared resources. Adding a new kind of fd just means writing one more `IEventHandler` - the loop never changes.

```
       ┌───────────────┐   handle(event, callback)   ┌────────────────────┐
       │ IEventHandler │  ─────────────────────────► │  reacts to event   │
       │ (Socket /     │                             │  then calls back…  │
       │  Connection / │  ◄───────────────────────── │                    │
       │  CgiPipeWrap) │     callback.onClientIN()   └────────────────────┘
       └───────────────┘              │
                                      ▼
                              ┌─────────────────┐
                              │ IServerCallback │  ◄── implemented by Server
                              │  = the Server   │      (has the shared state)
                              └─────────────────┘
```

**In short:** `epoll` wakes a handler → the handler reacts → it calls back into the Server to do anything that touches shared state.

## Configuration

Config files use an nginx-like syntax. Example files live in `config/`.

```nginx
server {
    listen 8080;                       # port, or ip:port
    server_name example.com;
    root www;
    client_max_body_size 10485760;     # bytes (default 10 MB)
    error_page 404 /errors/404.html;
    upload_store ./www/upload_store;

    location / {
        allow_methods GET POST DELETE; # default: GET HEAD
        index index.html;
        autoindex on;
    }

    location /cgi-bin {
        cgi .py /usr/bin/python3;       # map extension -> interpreter
    }

    location /old {
        return 301 /new;                # redirect
    }
}
```

**Notes**
- Directives end with `;`; `location` blocks use `{ }`.
- Sizes are in bytes. `client_max_body_size` is inherited from the server unless overridden in a location.
- `cgi` maps a file extension to an interpreter; multiple `cgi` lines are allowed per location.

## Status Codes

The server emits the following HTTP status codes (see `reason_phrase()` in `srcs/http/HttpStatus.cpp`):

| Code | Reason | When |
|------|--------|------|
| `200` | OK | Successful `GET` / `HEAD` |
| `201` | Created | Successful `POST` upload |
| `204` | No Content | Successful `DELETE` |
| `301` | Moved Permanently | Trailing-slash redirect on a directory / `return` |
| `302` | Found | `return` redirect |
| `303` | See Other | `return` redirect |
| `307` | Temporary Redirect | `return` redirect |
| `308` | Permanent Redirect | `return` redirect |
| `400` | Bad Request | Malformed request / no matching server |
| `403` | Forbidden | Access denied |
| `404` | Not Found | No matching location / missing file |
| `405` | Method Not Allowed | Method not in `allow_methods` (with `Allow` header) |
| `408` | Request Timeout | Client too slow to send the request |
| `411` | Length Required | Missing `Content-Length` where required |
| `413` | Payload Too Large | Body exceeds `client_max_body_size` |
| `414` | URI Too Long | Request target too long |
| `431` | Request Header Fields Too Large | Headers exceed `client_max_header_size` |
| `500` | Internal Server Error | Unexpected server failure |
| `501` | Not Implemented | Unknown HTTP method |
| `502` | Bad Gateway | Empty / malformed CGI output |
| `504` | Gateway Timeout | CGI exceeded its time limit |
| `505` | HTTP Version Not Supported | Unsupported HTTP version |

## Project Structure

```
srcs/
├── config/   # config parsing, routing, route resolution
├── http/     # request parsing & response building
├── cgi/      # CGI fork/exec utility
├── server/   # epoll event loop, sockets, connections, CGI lifecycle
└── utils/    # logger and helpers
config/       # example configuration files
www/          # default web root (static site + CGI scripts)
tests/        # test harness and valgrind tooling
```

### How it works

A request flows through clear layers:

1. **Config** parses the `.conf` file into server and location blocks.
2. **Server / event loop** accepts connections and drives all I/O through one `epoll` instance.
3. **HTTP** parses each request and routes it to the matching server + location.
4. **Response / CGI** builds a static response, or forks a CGI process and streams its output back - all without blocking the loop.

## Authors

A 42 project by three students, each owning one layer:

- **Config layer** - Leny
- **HTTP layer** - Thomas
- **Server / event loop & CGI** - Rasiol
