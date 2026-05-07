# Webserv | 42 Project


Configuration File

server{
    listen [port_number];
    server_name [server_dns1] [server_dns2] [server_dns3];
    client_max_body_size [size_megas]; #by default 1000000
    error_page [Error_number1] [Location_error_page1];
    error_page [Error_number2] [Location_error_page2];
    error_page [Error_number3] [Location_error_page3];

location /path{
    allow_methods [Method1] [Method2]; #By default GET if there is none
    root [url];
    index [index.html]
    autoindex [on/off]
    return [return_code] [return_url]; #.300-399 requires url 200-299, 400-599 does not require url
    upload_store [url_upload];
    cgi [extension] [executable_url];
    cgi [extension] [executable_url];
    client_max_body_size [size_megas];
    }

}


server {
    listen 8080;
    server_name localhost servidor_test www.ejemplo.com;
    client_max_body_size 800;
    error_page 404;
    error_page 500 /errors/500.html;
    location / {
        allow_methods GET POST;
        root ./www;
        index index.html;
        autoindex on;
    }