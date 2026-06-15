# Valgrind harness for webserv

Checks **memory leaks**, **fd leaks**, and **leaked/zombie CGI children**, and
filters out leaks that come from prebuilt libraries (not your code).

## Files
- `run_valgrind.sh` — boots `./webserv` under valgrind, drives every request
  path (static GET/HEAD, autoindex, 404/405/501/413, redirect, CGI GET+POST,
  upload+delete, keep-alive/pipelining), then **SIGTERM** for a clean shutdown
  so destructors run.
- `webserv.supp` — suppressions for **prebuilt/library** allocations only
  (`getaddrinfo`/NSS, `dlopen`/ld-linux, libc init, libstdc++ locale/exception
  pool). Does not match anything in `srcs/`, so your own leaks still show.

## Prerequisite
Build with debug symbols so traces have file:line:
```bash
make re CXXFLAGS="-Wall -Wextra -Werror -std=c++98 -MMD -MP -g -O0"
```

## Run
```bash
# raw report (no filtering)
tests/valgrind/run_valgrind.sh

# filtered: hide library still-reachable, show only webserv allocations
tests/valgrind/run_valgrind.sh tests/valgrind/webserv.supp

# also dump generated suppressions for any new library noise
tests/valgrind/run_valgrind.sh --gen
```
Full report is written to `tests/valgrind/valgrind.log`.

## Clean shutdown is required
Valgrind reports memory still allocated **at process exit**. If you kill the
server with Ctrl-C and it has no signal handler, `run()` never returns, the
`Server` destructor never runs, and every live `Connection`/`Socket`/config
object is reported as *still reachable / lost* — that noise is **your objects**,
not library leaks, and a suppression file must NOT hide it. The fix is a
SIGINT/SIGTERM handler that breaks the event loop so teardown runs.

## Results on this machine (glibc, valgrind 3.18.1)

### Current code (no shutdown handler) — killed via SIGTERM, with webserv.supp
```
definitely lost: 0 bytes in 0 blocks      <- no real leak
indirectly lost: 0 bytes in 0 blocks
  possibly lost: 0 bytes in 0 blocks
still reachable: 20,240 bytes in 54 blocks <- webserv config + epoll buffer (see below)
     suppressed: 78,702 bytes in 27 blocks <- library noise filtered by webserv.supp
FILE DESCRIPTORS: 4 open (3 std)           <- only valgrind's own log fd -> no fd leak
ERROR SUMMARY: 0 errors from 0 contexts
zombie CGI children: 0
```
The 54 "still reachable" blocks are **your own objects still alive when SIGTERM
killed the process**: the parsed config (`ServerConfig`/`Location` vectors in
`ConfigParser`) and `EpollWrapper`'s `std::vector<epoll_event>` (12,288 bytes),
both owned by live `main`-scope objects. They are reachable, **not lost**, and
are freed on a normal return from `main()`.

### With a SIGINT/SIGTERM handler that breaks run() — fully clean
```
total heap usage: 2,376 allocs, 2,376 frees
All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors from 0 contexts
```
i.e. webserv leaks nothing. To make the report empty without a handler is not
possible — valgrind measures memory live at exit, and an un-handled SIGTERM
exits before destructors run. Add a handler that sets the loop flag false if you
want a zero "still reachable" report.

`webserv.supp` suppressed 27 library blocks here (`getaddrinfo`/NSS/`dlopen`/
libstdc++) — exactly the prebuilt still-reachable you asked to filter out.
