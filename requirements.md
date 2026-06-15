# Webserv — Technical Specification

## 1. Project Overview

- Write an HTTP/1.1 server in C++98.
- The executable must be run as follows:

  ```
  ./webserv [configuration file]
  ```

- The program uses a configuration file, provided as a command-line argument or available at a default path.
- The server must be compatible with standard web browsers of your choice.
- NGINX may be used to compare headers and response behaviours (pay attention to differences between HTTP versions).
- You can take inspiration from the `server` section of the NGINX configuration file.

## 2. Build Requirements

- Submit a Makefile that compiles the source files. It must not perform unnecessary relinking.
- The Makefile must contain at least the rules: `$(NAME)`, `all`, `clean`, `fclean`, and `re`.
- Compile the code with `c++` and the flags `-Wall -Wextra -Werror`.
- The code must comply with the C++98 standard and must still compile when adding the flag `-std=c++98`.
- Leverage as many C++ features as possible (e.g., choose `<cstring>` over `<string.h>`). C functions are allowed, but always prefer their C++ versions when possible.
- Any external library and Boost libraries are forbidden.

## 3. Allowed Functions

All functionality must be implemented in C++98. The following external functions are allowed:

`execve`, `pipe`, `strerror`, `gai_strerror`, `errno`, `dup`, `dup2`, `fork`, `socketpair`, `htons`, `htonl`, `ntohs`, `ntohl`, `select`, `poll`, `epoll` (`epoll_create`, `epoll_ctl`, `epoll_wait`), `kqueue` (`kqueue`, `kevent`), `socket`, `accept`, `listen`, `send`, `recv`, `chdir`, `bind`, `connect`, `getaddrinfo`, `freeaddrinfo`, `setsockopt`, `getsockname`, `getprotobyname`, `fcntl`, `close`, `read`, `write`, `waitpid`, `kill`, `signal`, `access`, `stat`, `open`, `opendir`, `readdir`, `closedir`.

## 4. Mandatory Features

- Serve a fully static website.
- Support at least the `GET`, `POST`, and `DELETE` methods.
- Allow clients to upload files.
- Listen on multiple ports to deliver different content (see Configuration Requirements).
- Provide accurate HTTP response status codes.
- Provide default error pages if none are supplied.
- A request to the server must never hang indefinitely.

## 5. Configuration Requirements

The configuration file must allow you to:

- Define all the `interface:port` pairs on which the server will listen (defining multiple websites served by your program).
- Set up default error pages.
- Set the maximum allowed size for client request bodies.
- Specify rules or configurations on a URL/route (no regex required), for a website, among the following:
  - List of accepted HTTP methods for the route.
  - HTTP redirection.
  - Directory where the requested file should be located (e.g., if URL `/kapouet` is rooted to `/tmp/www`, URL `/kapouet/pouic/toto/pouet` will search for `/tmp/www/pouic/toto/pouet`).
  - Enabling or disabling directory listing.
  - Default file to serve when the requested resource is a directory.
  - Whether client file uploads are authorized, and the storage location for uploaded files.
  - Execution of CGI, based on file extension (for example `.php`) — see CGI Requirements.

Additional notes:

- You may include other rules or configuration information in the file (e.g., a server name for a website if you plan to implement virtual hosts).
- You must provide configuration files and default files to test and demonstrate that every feature works during the evaluation.

## 6. CGI Requirements

- Support at least one CGI (php-CGI, Python, and so forth).
- Have a careful look at the environment variables involved in the web server–CGI communication. The full request and arguments provided by the client must be available to the CGI.
- For chunked requests, the server must un-chunk them; the CGI will expect EOF as the end of the body.
- The same applies to the output of the CGI: if no `content_length` is returned from the CGI, EOF will mark the end of the returned data.
- The CGI must be run in the correct directory for relative path file access.

## 7. Non-Blocking / Event Loop Requirements

- The server must remain non-blocking at all times and properly handle client disconnections when necessary.
- It must use only one `poll()` (or equivalent) for all the I/O operations between the clients and the server (listening included).
- `poll()` (or equivalent) must monitor both reading and writing simultaneously.
- You must never perform a read or write operation without going through `poll()` (or equivalent).
- I/O that can wait for data (sockets, pipes/FIFOs, etc.) must be non-blocking and driven by a single `poll()` (or equivalent).
- You are not required to use `poll()` (or an equivalent function) for regular disk files; `read()` and `write()` on them do not require readiness notifications. Regular disk files are exempt from the readiness requirement.
- When using `poll()` or any equivalent call, you may use every associated macro or helper function (e.g., `FD_SET` for `select()`).

## 8. Evaluation-Critical Rules

- The program must not crash under any circumstances (even if it runs out of memory) or terminate unexpectedly. If this occurs, the project is considered non-functional and the grade will be 0.
- Calling `read`/`recv` or `write`/`send` on sockets, pipes/FIFOs, etc. without prior readiness notification will result in a grade of 0.
- A request to the server must never hang indefinitely.

## 9. Forbidden Behaviors

- Do not `execve` another web server.
- Do not use `fork` for anything other than CGI (such as PHP, Python, and so forth).
- Checking the value of `errno` to adjust server behaviour is strictly forbidden after performing a read or write operation.
- Any external library and Boost libraries are forbidden.
