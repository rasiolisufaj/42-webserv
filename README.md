# Webserv | 42 Project


Configuration File Format

server{
    listen [port_number]; #Valid also [ip:port_number] #ip by default 127.0.0.1
    server_name [server_dns1];
    client_max_body_size [size_kylo_bytes]; #by default 10485760 bytes
    client_max_header_size [size_kylo_bytes]; #by default 8192 bytes
    error_page [Error_number1] [Location_error_page1]; #Server level
    error_page [Error_number2] [Location_error_page2];
    error_page [Error_number3] [Location_error_page3];
    upload_store [Upload_path]; #by default www/upload_store
    root [root_path]; #by default www
    
    location [/path] {
            allow_methods [Method1] [Method2]; #By default GET HEAD if there is none "GET POST DELETE HEAD"
            root [path]; #inherit from server if empty
            index [index.html]
            autoindex [on/off]
            return [return_code] [return_url]; #.300-399 requires url 200-299 or 400-599 does not require url
            upload_store [upload_path_folder]; #inherit from server if empty
            cgi [extension] [executable_path];
            cgi [extension] [executable_path];
            client_max_body_size [size_megas]; #inherit from server if empty
            }
}


