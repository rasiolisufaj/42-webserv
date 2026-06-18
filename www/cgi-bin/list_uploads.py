#!/usr/bin/env python3
import json
import os
import time

# The server exports the route's effective upload_store as the UPLOAD_STORE env
# var (already made absolute), so this listing follows whatever path the config
# sets -- e.g. `upload_store www/upload_store_rasiol;`. This keeps POST (create),
# DELETE (remove) and this listing in sync with the configured directory.
# Fallback "../upload_store" matches the default when run outside the server
# (CGI children chdir into www/cgi-bin, so it resolves to www/upload_store).
UPLOAD_DIR = os.environ.get("UPLOAD_STORE", "../upload_store")
response_data = []

if os.path.isdir(UPLOAD_DIR):
    for filename in sorted(os.listdir(UPLOAD_DIR)):
        filepath = os.path.join(UPLOAD_DIR, filename)
        if os.path.isfile(filepath):
            stat = os.stat(filepath)
            response_data.append({
                "name": filename,
                "size": stat.st_size,
                "size_kb": round(stat.st_size / 1024.0, 2),
                "date": time.strftime("%Y-%m-%d %H:%M:%S",
                                      time.localtime(stat.st_mtime)),
            })

print("Content-Type: application/json\r\n")
print(json.dumps(response_data))
