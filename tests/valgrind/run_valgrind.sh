#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Valgrind harness for webserv.
#
# Boots ./webserv under valgrind, drives every request path with curl, then
# sends SIGTERM so the server exits run() cleanly and its destructors run
# (otherwise valgrind would report the server's still-live objects as noise).
#
# Usage:
#   tests/valgrind/run_valgrind.sh [--gen] [suppression_file]
#     --gen              also emit --gen-suppressions=all (raw suppressions)
#     suppression_file   pass --suppressions=<file> to filter library frames
#
# Checks: memory leaks (--leak-check=full --show-leak-kinds=all),
#         fd leaks (--track-fds=yes), and leaked/zombie CGI children.
# ---------------------------------------------------------------------------
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

BIN=./webserv
CONF=config/default.conf
HOST=127.0.0.1
PORT=8080
OUTDIR=tests/valgrind
VG_LOG="$OUTDIR/valgrind.log"
APP_LOG="$OUTDIR/webserv.out"

GEN=""
SUPP=""
for arg in "$@"; do
    case "$arg" in
        --gen) GEN="--gen-suppressions=all" ;;
        *)     SUPP="--suppressions=$arg" ;;
    esac
done

[ -x "$BIN" ] || { echo "FATAL: $BIN not built (run: make re CXXFLAGS=\"... -g\")"; exit 1; }

VG_OPTS=(
    --leak-check=full
    --show-leak-kinds=all
    --track-fds=yes
    --trace-children=no
    --child-silent-after-fork=yes
    --num-callers=40
    --error-exitcode=0
    --log-file="$VG_LOG"
)
[ -n "$GEN" ]  && VG_OPTS+=("$GEN")
[ -n "$SUPP" ] && VG_OPTS+=("$SUPP")

echo ">> launching: valgrind ${VG_OPTS[*]} $BIN $CONF"
valgrind "${VG_OPTS[@]}" "$BIN" "$CONF" >"$APP_LOG" 2>&1 &
VG_PID=$!

# Wait until the port is accepting connections (max ~20s; valgrind is slow).
for i in $(seq 1 100); do
    if curl -s -o /dev/null --max-time 1 "http://$HOST:$PORT/" 2>/dev/null; then
        break
    fi
    kill -0 "$VG_PID" 2>/dev/null || { echo "FATAL: server died on startup"; cat "$APP_LOG"; exit 1; }
    sleep 0.2
done
echo ">> server is up (valgrind pid=$VG_PID)"

base="http://$HOST:$PORT"
c() { curl -s -o /dev/null -w "  [%{http_code}] $*\n" --max-time 5 "$@"; }

echo ">> exercising request paths..."
c "$base/"                                            # 200 static index
c "$base/index.html"                                  # 200 file
c "$base/upload_store/"                               # autoindex listing
c -I "$base/"                                         # HEAD
c "$base/does-not-exist"                              # 404 custom error page
c -X POST --data "x=1" "$base/"                       # 405 (GET HEAD only) + Allow
c -X WACKY "$base/"                                   # 501 unknown method
c "$base/redirect"                                    # 302 return directive
c "$base/cgi-bin/hello.py"                            # CGI GET (fork/exec/reap)
c -X POST --data "name=valgrind" "$base/cgi-bin/hello.py"  # CGI POST body->stdin

# File upload then delete (same upload_store dir)
echo "leak-test-payload $(date)" > "$OUTDIR/.upload_probe"
c -X POST --data-binary @"$OUTDIR/.upload_probe" "$base/upload_store/vg_probe.txt"
c -X DELETE "$base/upload_store/vg_probe.txt"

# Oversize body -> early 413 (limit is 2048000 bytes)
head -c 3000000 /dev/zero | tr '\0' 'A' > "$OUTDIR/.big"
c -X POST --data-binary @"$OUTDIR/.big" "$base/upload_store/big.txt"
rm -f "$OUTDIR/.upload_probe" "$OUTDIR/.big"

# Keep-alive / pipelining on one connection
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\nGET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' \
    | timeout 5 bash -c "cat > /dev/tcp/$HOST/$PORT" 2>/dev/null && echo "  [--] pipelined 2 requests"

# Zombie/child check while server still running
ZOMBIES=$(ps -o stat= --ppid "$VG_PID" 2>/dev/null | grep -c 'Z' || true)
echo ">> zombie CGI children under server: ${ZOMBIES:-0}"

echo ">> sending SIGTERM for clean shutdown..."
kill -TERM "$VG_PID" 2>/dev/null
for i in $(seq 1 100); do
    kill -0 "$VG_PID" 2>/dev/null || break
    sleep 0.2
done
if kill -0 "$VG_PID" 2>/dev/null; then
    echo ">> WARN: did not exit on SIGTERM, forcing"; kill -KILL "$VG_PID" 2>/dev/null
fi
wait "$VG_PID" 2>/dev/null

echo ">> valgrind report written to $VG_LOG"
echo "---------------------------------------------------------------"
sed -n '/LEAK SUMMARY/,/suppressed:/p; /FILE DESCRIPTORS/,/Open file/p; /ERROR SUMMARY/p' "$VG_LOG"
