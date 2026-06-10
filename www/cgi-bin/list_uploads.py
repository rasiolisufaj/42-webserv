#!/usr/bin/env python3
import json
import os

UPLOAD_DIR = "../upload_store"
response_data = []

# Safely check if directory exists and read it
if os.path.exists(UPLOAD_DIR) and os.path.isdir(UPLOAD_DIR):
    for filename in os.listdir(UPLOAD_DIR):
        filepath = os.path.join(UPLOAD_DIR, filename)
        if os.path.isfile(filepath):
            size_bytes = os.path.getsize(filepath)
            # Convert to KB for UI display
            size_kb = round(size_bytes / 1024, 2)
            response_data.append({"name": filename, "size_kb": size_kb})

# Format the output strictly for the C++98 HttpHandler
print("Content-Type: application/json\r\n")
print(json.dumps(response_data))
