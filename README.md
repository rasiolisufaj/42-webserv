# Webserv | 42 Project


Configuration File

server{
    listen [port_number];
    server_name [server_dns1] [server_dns2] [server_dns3];
    client_max_body_size [size_megas];
    error_page [Error_number1] [Location_error_page1];
    error_page [Error_number2] [Location_error_page2];
    error_page [Error_number3] [Location_error_page3];

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