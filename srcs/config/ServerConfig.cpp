#include "config/ServerConfig.hpp"
#include <iostream>

ServerConfig::ServerConfig() {
    port = 8080;         
    host = "0.0.0.0";
    client_max_body_size = 1000000;
    client_max_header_size = 8192;
    upload_store = "";
}

ServerConfig::~ServerConfig() {}


void ServerConfig::print_servers() const
{
    std::cout << std::endl << "-------------- Server --------------" << std::endl;
    std::cout << "listen: " << this->port << std::endl;
    std::cout << "host: " << this->host << std::endl;
    size_t i = 0;
    std::cout << "server_name : ";
    while (i < this->server_names.size())
    {
        std::cout << this->server_names.at(i) << " ";
        i++;
    }
    std::cout <<  std::endl;
    std::cout << "client_max_body_size: " << this->client_max_body_size << std::endl;
    std::cout << "client_max_header_size: " << this->client_max_header_size << std::endl;
    for (std::map<int, std::string>::const_iterator it = error_pages.begin(); it != error_pages.end(); ++it) 
    {
    std::cout << "error_page : " << it->first << " " << it->second << std::endl;
    }
    std::cout << "upload_store: " << this->upload_store << std::endl;

    size_t j = 0;
    while (j < this->locations.size())
    {
        this->print_location(this->locations[j]);
        j++;
    }
}

void ServerConfig::print_location(const Location& loc) const 
{
    std::cout << std::endl << "-----Location ------" << std::endl;
    std::cout << "path: " << loc.path << std::endl;
    std::cout << "allow_methods: ";
    size_t i  = 0;
    while (i < loc.allowed_methods.size())
    {
        std::cout << loc.allowed_methods.at(i) << " ";
        i++;
    }
    std::cout <<  std::endl;
    std::cout << "root: " << loc.root << std::endl;
    std::cout << "Index : " << loc.index << std::endl;
    std::cout << "Autoindex : " << loc.autoindex << std::endl;
    std::cout << "return : " << loc.return_code << " " << loc.return_redir << std::endl;
    std::cout << "upload_store: " << loc.upload_store << std::endl;

    for (std::map<std::string, std::string>::const_iterator it = loc.cgi.begin(); it != loc.cgi.end(); ++it) 
    {
    std::cout << "cgi : " << it->first << " " << it->second << std::endl;
    }

    std::cout << "client_max_body_size : " << loc.client_max_body_size << std::endl;
    std::cout << std::endl;
}