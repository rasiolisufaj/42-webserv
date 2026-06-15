#!/usr/bin/env python3
# Slow CGI: stays silent past the server's 5s inactivity timeout so the
# event loop must reap us and answer 504 Gateway Timeout. If the server
# ever lets us finish, it prints the line below (proving it did NOT time out).
import time
import sys

time.sleep(10)
sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("Slow CGI finished after 10s (server did not kill me)\n")
