#!/bin/bash

# 1. Output the mandatory HTTP Header
echo -ne "Content-Type: text/html\r\n\r\n"

# 2. Gather dynamic system info using Unix commands
CURRENT_TIME=$(date "+%H:%M:%S")
DISK_SPACE=$(df -h / | awk 'NR==2 {print $4}')
CURRENT_USER=$(whoami)

# 3. Output the HTML structure
echo "<html>"
echo "<head><title>Server Status</title></head>"
echo "<body style='font-family: sans-serif; background-color: #f4f4f9; padding: 20px;'>"
echo "  <div style='background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1);'>"
echo "    <h1 style='color: #333;'>💻 Webserv System Status</h1>"
echo "    <p><b>CGI Gateway:</b> <span style='color: green;'>Active (Bash Script)</span></p>"
echo "    <hr>"
echo "    <ul>"
echo "      <li><b>Current Time:</b> $CURRENT_TIME</li>"
echo "      <li><b>Available Disk Space:</b> $DISK_SPACE</li>"
echo "      <li><b>Executed By User:</b> $CURRENT_USER</li>"
echo "    </ul>"
echo "  </div>"
echo "</body>"
echo "</html>"