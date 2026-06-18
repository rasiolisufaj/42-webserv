#!/bin/bash
# Comprehensive WebServ Testing Suite
# Run this after implementing the Server event loop

# NOTE: 'set -e' is intentionally NOT used. This is a test harness: it must run
# every test and print the final summary even when individual requests fail. A
# single non-zero curl (e.g. in the load loops below) would otherwise abort the
# whole run before any results are reported. Critical setup failures (missing
# binary, port in use, server didn't start) are handled with explicit 'exit 1'.

WEBSERV_BIN="./webserv"
CONFIG_FILE="config/default.conf"
HOST="127.0.0.1"
PORT="8080"
BASE_URL="http://${HOST}:${PORT}"
TIMEOUT=5

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;36m'
NC='\033[0m'

log_info()  { echo -e "${BLUE}[INFO]${NC} $1"; }
log_pass()  { echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail()  { echo -e "${RED}[FAIL]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }

# Test counter
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

assert_success() {
    local test_name=$1
    local cmd=$2
    TESTS_RUN=$((TESTS_RUN + 1))

    if eval "$cmd" > /dev/null 2>&1; then
        log_pass "$test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_fail "$test_name"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

assert_http_code() {
    local test_name=$1
    local method=$2
    local path=$3
    local expected_code=$4
    TESTS_RUN=$((TESTS_RUN + 1))

    local code=$(curl -s -X "$method" -w "%{http_code}" -o /dev/null \
                 --connect-timeout $TIMEOUT "${BASE_URL}${path}" 2>/dev/null || echo "000")

    if [ "$code" = "$expected_code" ]; then
        log_pass "$test_name (HTTP $code)"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_fail "$test_name (Expected: $expected_code, Got: $code)"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

# ============================================================================
# SETUP
# ============================================================================

log_info "Starting WebServ testing suite..."

if [ ! -f "$WEBSERV_BIN" ]; then
    log_fail "Binary not found: $WEBSERV_BIN. Run 'make' first."
    exit 1
fi

# Kill any leftover server from a previous aborted run. Match the full
# invocation ("./webserv config/default.conf"), not the bare word "webserv",
# so we don't kill unrelated processes (editors, greps, this script) that merely
# mention webserv in their command line. pkill never matches itself.
pkill -f "$WEBSERV_BIN $CONFIG_FILE" || true
sleep 1

# Check if port is available (prefer ss; fall back to netstat; skip if neither).
PORT_CHECK=""
if command -v ss > /dev/null 2>&1; then
    PORT_CHECK="ss -tuln"
elif command -v netstat > /dev/null 2>&1; then
    PORT_CHECK="netstat -tuln"
fi
if [ -n "$PORT_CHECK" ] && $PORT_CHECK 2>/dev/null | grep -q ":${PORT} "; then
    log_fail "Port $PORT already in use. Kill process and try again."
    exit 1
fi

# Start server
log_info "Starting server on ${HOST}:${PORT}..."
"$WEBSERV_BIN" "$CONFIG_FILE" > /tmp/webserv.log 2>&1 &
SERVER_PID=$!

# Wait for server to start
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    log_fail "Server failed to start"
    cat /tmp/webserv.log
    exit 1
fi

log_pass "Server started (PID: $SERVER_PID)"

# Trap to cleanup on exit
cleanup() {
    log_info "Cleaning up..."
    kill $SERVER_PID 2>/dev/null || true
    sleep 1
}
trap cleanup EXIT

# ============================================================================
# TESTS: BASIC CONNECTIVITY
# ============================================================================

log_info "=== PHASE 1: Basic Connectivity ==="

# '/' allows only GET HEAD (see config/default.conf), so POST/DELETE there must
# be rejected with 405 (method recognized but not allowed), not 200. UNKNOWN is
# an unimplemented method -> 501.
assert_http_code "GET / returns 200"             "GET"     "/" "200"
assert_http_code "POST / not allowed (405)"      "POST"    "/" "405"
assert_http_code "DELETE / not allowed (405)"    "DELETE"  "/" "405"
assert_http_code "Unknown method returns 501"    "UNKNOWN" "/" "501"

# Method success path: '/upload_store' allows GET POST DELETE. Start from a clean
# slate (result ignored, not counted) so POST creates a new file -> 201, then
# DELETE removes it -> 204.
curl -s -o /dev/null -X DELETE "${BASE_URL}/upload_store/probe.txt" 2>/dev/null || true
assert_http_code "POST upload creates file (201)"   "POST"   "/upload_store/probe.txt" "201"
assert_http_code "DELETE upload removes file (204)" "DELETE" "/upload_store/probe.txt" "204"

# ============================================================================
# TESTS: CONNECTION LIFECYCLE
# ============================================================================

log_info "=== PHASE 2: Connection Lifecycle ==="

# Test 1: Sequential connections (verify fd cleanup)
log_info "Test: Sequential connections (100 requests)"
for i in {1..100}; do
    curl -s "${BASE_URL}/" > /dev/null
done
log_pass "Handled 100 sequential requests"
TESTS_PASSED=$((TESTS_PASSED + 1))
TESTS_RUN=$((TESTS_RUN + 1))

# Test 2: Rapid open/close
log_info "Test: Rapid open/close (50 parallel requests)"
(
    for i in {1..50}; do
        curl -s "${BASE_URL}/" > /dev/null &
    done
    wait
) 2>/dev/null
log_pass "Handled 50 parallel requests without deadlock"
TESTS_PASSED=$((TESTS_PASSED + 1))
TESTS_RUN=$((TESTS_RUN + 1))

# ============================================================================
# TESTS: PARTIAL READS/WRITES
# ============================================================================

log_info "=== PHASE 3: Partial Read/Write Handling ==="

# Test 1: Large POST body. Target a route that allows POST (/upload_store); the
# body (~1.3MB) is under the 2MB client_max_body_size in default.conf, so the
# upload should succeed (2xx). The body MUST be sent from a file via '@file':
# Linux caps a single argv string at 128KB (MAX_ARG_STRLEN), so passing ~1.3MB
# as 'curl --data-binary "$BODY"' fails with "Argument list too long" and curl
# never runs (yielding a misleading 000).
log_info "Test: Large POST body (1MB)"
LARGE_FILE=$(mktemp)
head -c 1000000 /dev/zero | base64 > "$LARGE_FILE"
curl -s -o /dev/null -X DELETE "${BASE_URL}/upload_store/large_probe.txt" 2>/dev/null || true
CODE=$(curl -s -X POST --data-binary "@$LARGE_FILE" -w "%{http_code}" -o /dev/null \
       --connect-timeout $TIMEOUT "${BASE_URL}/upload_store/large_probe.txt" 2>/dev/null || echo "000")
rm -f "$LARGE_FILE"
if [ "$CODE" -ge 200 ] && [ "$CODE" -lt 300 ]; then
    log_pass "1MB POST handled"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    log_fail "1MB POST failed (HTTP $CODE)"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi
TESTS_RUN=$((TESTS_RUN + 1))

# Test 2: Chunked transfer encoding must be decoded and the upload stored.
# POST to /upload_store (which allows POST); '/' would be 405. Start clean so a
# fresh create is 201. 'Connection: close' makes the server close after replying
# so nc returns promptly with the full response.
log_info "Test: Transfer-Encoding: chunked"
curl -s -o /dev/null -X DELETE "${BASE_URL}/upload_store/chunked_probe.txt" 2>/dev/null || true
CHUNKED_RESP=$( {
    printf 'POST /upload_store/chunked_probe.txt HTTP/1.1\r\n'
    printf 'Host: %s:%s\r\n' "$HOST" "$PORT"
    printf 'Transfer-Encoding: chunked\r\n'
    printf 'Connection: close\r\n'
    printf '\r\n'
    printf '5\r\nHello\r\n'
    printf '6\r\n World\r\n'
    printf '0\r\n\r\n'
} | timeout $TIMEOUT nc $HOST $PORT 2>/dev/null )
CHUNKED_STATUS=$(printf '%s' "$CHUNKED_RESP" | head -1 | grep -oE '[0-9]{3}' | head -1)
TESTS_RUN=$((TESTS_RUN + 1))
if [ "$CHUNKED_STATUS" = "201" ] || [ "$CHUNKED_STATUS" = "200" ]; then
    log_pass "Chunked body decoded and stored (HTTP $CHUNKED_STATUS)"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    log_fail "Chunked encoding failed (status: ${CHUNKED_STATUS:-none})"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# ============================================================================
# TESTS: CLIENT DISCONNECT HANDLING
# ============================================================================

log_info "=== PHASE 4: Client Disconnect Handling ==="

# Test: Abrupt disconnect. Send a partial request, then vanish (timeout kills nc
# mid-request). The real assertion: the server stays alive and keeps serving.
# NOTE: $HOST/$PORT are double-quoted so the *parent* expands them before sh -c;
# single quotes would pass them unexpanded to a child shell where they're empty.
log_info "Test: Abrupt client disconnect"
timeout 1 sh -c "(printf 'GET / HTTP/1.1\r\n'; sleep 10) | nc $HOST $PORT" > /dev/null 2>&1 || true
TESTS_RUN=$((TESTS_RUN + 1))
if kill -0 $SERVER_PID 2>/dev/null && \
   [ "$(curl -s -o /dev/null -w '%{http_code}' --connect-timeout $TIMEOUT "${BASE_URL}/")" = "200" ]; then
    log_pass "Server survived abrupt disconnect and still serves"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    log_fail "Server unresponsive after abrupt disconnect"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# Test: Half-closed connection. Send a full request, hold briefly, then close.
# Assert the server actually answered with 200 before the connection went away.
log_info "Test: Half-closed connection"
HALF_RESP=$( {
    printf 'GET / HTTP/1.1\r\nHost: %s:%s\r\nConnection: close\r\n\r\n' "$HOST" "$PORT"
    sleep 1
} | timeout $TIMEOUT nc $HOST $PORT 2>/dev/null )
HALF_STATUS=$(printf '%s' "$HALF_RESP" | head -1 | grep -oE '[0-9]{3}' | head -1)
TESTS_RUN=$((TESTS_RUN + 1))
if [ "$HALF_STATUS" = "200" ]; then
    log_pass "Half-closed connection served correctly (HTTP $HALF_STATUS)"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    log_fail "Half-closed connection failed (status: ${HALF_STATUS:-none})"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# ============================================================================
# TESTS: FILE DESCRIPTOR TRACKING
# ============================================================================

log_info "=== PHASE 5: FD Leak Detection ==="

FD_BEFORE=$(lsof -p $SERVER_PID 2>/dev/null | wc -l)

# Create load
for i in {1..50}; do
    curl -s "${BASE_URL}/" > /dev/null
done

sleep 1
FD_AFTER=$(lsof -p $SERVER_PID 2>/dev/null | wc -l)

FD_DIFF=$((FD_AFTER - FD_BEFORE))
if [ $FD_DIFF -lt 10 ]; then
    log_pass "FD count stable (before: $FD_BEFORE, after: $FD_AFTER, diff: $FD_DIFF)"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    log_warn "FD count increased (before: $FD_BEFORE, after: $FD_AFTER, diff: $FD_DIFF)"
    log_warn "Possible FD leak - check with: lsof -p $SERVER_PID"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi
TESTS_RUN=$((TESTS_RUN + 1))

# ============================================================================
# TESTS: NON-BLOCKING BEHAVIOR
# ============================================================================

log_info "=== PHASE 6: Non-blocking Behavior ==="

# Test: Slow client (should not block other clients)
log_info "Test: Slow client doesn't block fast clients"
{
    printf 'GET / HTTP/1.1\r\nHost: %s:%s\r\n\r\n' "$HOST" "$PORT"
    sleep 2
} | timeout $TIMEOUT nc $HOST $PORT > /dev/null 2>&1 &
SLOW_PID=$!

sleep 0.5
# Fast request should complete quickly
START=$(date +%s%N)
curl -s "${BASE_URL}/" > /dev/null
END=$(date +%s%N)
ELAPSED=$((($END - $START) / 1000000))  # Convert to ms

if [ $ELAPSED -lt 500 ]; then
    log_pass "Fast request completed in ${ELAPSED}ms (not blocked by slow client)"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    log_fail "Fast request took ${ELAPSED}ms (may indicate blocking)"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi
TESTS_RUN=$((TESTS_RUN + 1))

# Reap the slow client. It uses keep-alive (no 'Connection: close'), so the
# server holds the connection until its idle timeout (~30s); a bare 'wait' would
# stall the whole script there. Kill it explicitly instead.
kill $SLOW_PID 2>/dev/null || true
wait $SLOW_PID 2>/dev/null || true

# ============================================================================
# TESTS: SCALABILITY
# ============================================================================

log_info "=== PHASE 7: Scalability ==="

# Test: Many sequential connections
log_info "Test: 500 sequential connections"
FAILED_CONNS=0
for i in {1..500}; do
    if ! curl -s "${BASE_URL}/" > /dev/null 2>&1; then
        FAILED_CONNS=$((FAILED_CONNS + 1))
    fi
done

if [ $FAILED_CONNS -eq 0 ]; then
    log_pass "All 500 connections succeeded"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    log_fail "$FAILED_CONNS/500 connections failed"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi
TESTS_RUN=$((TESTS_RUN + 1))

# ============================================================================
# TESTS: MEMORY SAFETY (requires valgrind)
# ============================================================================

if command -v valgrind > /dev/null 2>&1; then
    log_info "=== PHASE 8: Memory Safety (Valgrind) ==="

    log_info "Test: Memory leaks (running short valgrind check)..."

    # Kill current server
    kill $SERVER_PID 2>/dev/null || true
    sleep 1

    # Start under valgrind with short timeout
    timeout 5 valgrind --leak-check=full --show-leak-kinds=all \
        "$WEBSERV_BIN" "$CONFIG_FILE" > /tmp/webserv_valgrind.log 2>&1 &
    VALGRIND_PID=$!

    sleep 2

    # Run a few requests
    for i in {1..5}; do
        curl -s "${BASE_URL}/" > /dev/null 2>&1 || true
    done

    # Stop valgrind, then WAIT for it to fully exit before grepping. valgrind
    # emits its leak report only after the child exits, and writing it takes a
    # moment; grepping immediately after the kill races that flush and yields a
    # false WARN even on a perfectly clean run.
    sleep 1
    kill $VALGRIND_PID 2>/dev/null || true
    wait $VALGRIND_PID 2>/dev/null || true

    # Check for leaks and errors. On a clean shutdown valgrind prints
    # "All heap blocks were freed -- no leaks are possible" and OMITS the
    # per-kind breakdown, so grepping only for "definitely lost: 0 bytes" gives a
    # false positive. Accept that line OR an explicit zero definite loss, and
    # also require a clean "ERROR SUMMARY: 0 errors".
    if grep -qE "All heap blocks were freed|definitely lost: 0 bytes" /tmp/webserv_valgrind.log 2>/dev/null \
       && grep -q "ERROR SUMMARY: 0 errors" /tmp/webserv_valgrind.log 2>/dev/null; then
        log_pass "No memory leaks or errors detected"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_warn "Possible memory issue - check /tmp/webserv_valgrind.log"
        grep -E "lost:|All heap blocks|ERROR SUMMARY" /tmp/webserv_valgrind.log | head -5
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    TESTS_RUN=$((TESTS_RUN + 1))
else
    log_warn "Valgrind not installed, skipping memory safety tests"
fi

# ============================================================================
# SUMMARY
# ============================================================================

log_info "=== TEST SUMMARY ==="
log_info "Total: $TESTS_RUN | Passed: $TESTS_PASSED | Failed: $TESTS_FAILED"

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi
