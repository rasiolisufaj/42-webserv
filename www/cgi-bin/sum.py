#!/usr/bin/env python3
import sys
import os
import urllib.parse

def main():
    try:
        content_length = int(os.environ.get('CONTENT_LENGTH', 0))
    except ValueError:
        content_length = 0
    
    
    if content_length > 0:
        body_data = sys.stdin.read(content_length)
    else:
        body_data = ""
        
    parsed_data = urllib.parse.parse_qs(body_data)
    
    try:
        num1 = int(parsed_data.get('n1', ['0'])[0])
        num2 = int(parsed_data.get('n2', ['0'])[0])
        total_sum = num1 + num2
        
        print("Content-Type: text/html\r\n\r\n")
        print("<html>")
        print("<head><title>result CGI</title></head>")
        print("<body>")
        print("<h1> CGI is working !</h1>")
        print(f"<p>Lenght {content_length}</p>")
        print(f"<p>The result of adding {num1} + {num2} is: <b>{total_sum}</b></p>")
        print("</body>")
        print("</html>")
        
    except Exception as e:
        print("Content-Type: text/plain\r\n\r\n")
        print(f"Error: {str(e)}")

if __name__ == "__main__":
    main()