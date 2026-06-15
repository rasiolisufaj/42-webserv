#!/usr/bin/env python3
import json
import os
import time

# CGI children chdir into their own dir (www/cgi-bin), so "../upload_store"
# resolves to www/upload_store -- the SAME directory the /upload_store location
# uses. This keeps POST (create), DELETE (remove) and this listing in sync.
UPLOAD_DIR = "../upload_store"
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
