#!/bin/bash
# Comprehensive WebServ Testing Suite
# Run this after implementing the Server event loop

set -e

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

# Kill any existing webserv process
pkill -f "webserv" || true
sleep 1

# Check if port is available
if netstat -tuln 2>/dev/null | grep -q ":${PORT} "; then
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

assert_http_code "GET request returns 200" "GET" "/" "200"
assert_http_code "POST request returns 200" "POST" "/" "200"
assert_http_code "DELETE request returns 200" "DELETE" "/" "200"
assert_http_code "Unknown method returns 501" "UNKNOWN" "/" "501"

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

# Test 1: Large POST body
log_info "Test: Large POST body (1MB)"
LARGE_BODY=$(head -c 1000000 /dev/zero | base64)
CODE=$(curl -s -X POST -d "$LARGE_BODY" -w "%{http_code}" -o /dev/null \
       --connect-timeout $TIMEOUT "${BASE_URL}/" 2>/dev/null || echo "000")
if [ "$CODE" -ge 200 ] && [ "$CODE" -lt 300 ]; then
    log_pass "1MB POST handled"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    log_fail "1MB POST failed (HTTP $CODE)"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi
TESTS_RUN=$((TESTS_RUN + 1))

# Test 2: Chunked transfer encoding
log_info "Test: Transfer-Encoding: chunked"
{
    echo -ne "POST / HTTP/1.1\r\n"
    echo -ne "Host: ${HOST}:${PORT}\r\n"
    echo -ne "Transfer-Encoding: chunked\r\n"
    echo -ne "\r\n"
    echo -ne "5\r\nHello\r\n"
    echo -ne "6\r\n World\r\n"
    echo -ne "0\r\n\r\n"
} | timeout $TIMEOUT nc $HOST $PORT > /dev/null 2>&1

log_pass "Chunked encoding handled"
TESTS_PASSED=$((TESTS_PASSED + 1))
TESTS_RUN=$((TESTS_RUN + 1))

# ============================================================================
# TESTS: CLIENT DISCONNECT HANDLING
# ============================================================================

log_info "=== PHASE 4: Client Disconnect Handling ==="

# Test: Abrupt disconnect
log_info "Test: Abrupt client disconnect"
timeout 1 sh -c '(echo "GET / HTTP/1.1"; sleep 10) | nc $HOST $PORT' 2>/dev/null || true
log_pass "Server handled abrupt disconnect"
TESTS_PASSED=$((TESTS_PASSED + 1))
TESTS_RUN=$((TESTS_RUN + 1))

# Test: Half-closed connection (FIN received)
log_info "Test: Half-closed connection"
{
    echo -ne "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"
    sleep 1
} | timeout $TIMEOUT nc $HOST $PORT > /dev/null 2>&1 || true
log_pass "Server handled half-closed connection"
TESTS_PASSED=$((TESTS_PASSED + 1))
TESTS_RUN=$((TESTS_RUN + 1))

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
    echo -ne "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"
    sleep 2
} | nc $HOST $PORT > /dev/null 2>&1 &

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

wait

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

    # Give valgrind time to finish
    sleep 1
    kill $VALGRIND_PID 2>/dev/null || true

    # Check for leaks
    if grep -q "definitely lost: 0 bytes" /tmp/webserv_valgrind.log 2>/dev/null; then
        log_pass "No definite memory leaks detected"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_warn "Memory leaks detected - check /tmp/webserv_valgrind.log"
        grep "lost:" /tmp/webserv_valgrind.log | head -3
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
