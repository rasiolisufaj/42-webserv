#!/usr/bin/env python3
import sys
import os

# 1. Detect the HTTP method sent by your C++ server
method = os.environ.get("REQUEST_METHOD", "GET")

received_data = ""

if method == "POST":
    # For POST, we read from the standard input (the pipe filled by C++)
    received_data = sys.stdin.read()
else:
    # For GET, the data comes from the URL parameters via environment variables
    received_data = os.environ.get("QUERY_STRING", "No data provided via GET")

# 2. Print the mandatory HTTP headers and HTML body that C++ will read
print("Content-type: text/html\r\n\r\n")
print("<html>")
print("<body>")
print(f"<h1>CGI Execution Successful!</h1>")
print(f"<p><b>Method detected:</b> {method}</p>")
print(f"<p><b>Data processed:</b> {received_data}</p>")
print("</body>")
print("</html>")