#!/bin/bash
echo "Content-Type: text/plain"
echo ""
echo "Hello from CGI (bash)"
echo "METHOD=$REQUEST_METHOD"
echo "QS=$QUERY_STRING"
