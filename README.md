# Webserv | 42 Project


Configuration File

server{
    listen [port_number]; #Valid also [ip:port_number]
    server_name [server_dns1] [server_dns2] [server_dns3];
    client_max_body_size [size_megas]; #by default 1000000
    client_max_header_size [size_megas]; #by default 8192
    error_page [Error_number1] [Location_error_page1];
    error_page [Error_number2] [Location_error_page2];
    error_page [Error_number3] [Location_error_page3];
    upload_store [Upload_path]; #Mandatory

location /path{
    allow_methods [Method1] [Method2]; #By default GET if there is none "GET POST DELETE HEAD"
    root [url];
    index [index.html]
    autoindex [on/off]
    return [return_code] [return_url]; #.300-399 requires url 200-299, 400-599 does not require url
    upload_store [url_upload]; #inherit from server if empty
    cgi [extension] [executable_url];
    cgi [extension] [executable_url];
    client_max_body_size [size_megas]; #inherit from server if empty
    }

}


