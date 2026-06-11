#!/usr/bin/env python3
# Infinite-loop CGI: never produces output and never exits. The server must
# enforce its hard cap / inactivity timeout, SIGKILL the child (no zombie),
# and respond 504. A gentle sleep keeps the CPU idle while we wait to be reaped.
import time

while True:
    time.sleep(0.5)
