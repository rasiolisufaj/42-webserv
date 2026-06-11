#!/usr/bin/env python3
import os
import sys

body = sys.stdin.read()

print("Content-Type: text/plain")
print("")
print("Hello from CGI")
print("METHOD=", os.environ.get("REQUEST_METHOD", ""))
print("QS=", os.environ.get("QUERY_STRING", ""))
print("COOKIE=", os.environ.get("HTTP_COOKIE", ""))  # <-- Added this line
print("BODY=", body)
