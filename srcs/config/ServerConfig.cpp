#include "ServerConfig.hpp"

ServerConfig::ServerConfig() {
    port = 8080;         
    host = "127.0.0.1";
    client_max_body_size = 1000000;
}

ServerConfig::~ServerConfig() {}


void ServerConfig::print_servers() const
{
    std::cout << "listen: " << this->port << std::endl;
    //std::cout << "-----> " << ;
    size_t i = 0;
    while (i < this->server_names.size())
    {
        std::cout << "server_name: " << this->server_names.at(i) << std::endl;
        i++;
    }
    std::cout << "client_max_body_size: " << this->client_max_body_size << std::endl;
    for (std::map<int, std::string>::const_iterator it = error_pages.begin(); it != error_pages.end(); ++it) 
    {
    std::cout << "error_page : " << it->first << " " << it->second << std::endl;
    }
    //std::cout << "error_page: " << this->error_pages[500] << std::endl << std::endl;

        // if (servers.at(i).locations.size() > 0)
        // {
        //     print_location(server);
        //     std::cout << "" << std::endl;
        // }
        
    
}

// void ServerConfig::print_location()
// {
//     std::cout << "Location path: " << servers.at(0).locations.at(0).path << std::endl;
//     std::cout << "allow_methods: " << servers.at(0).locations.at(0).allowed_methods.at(0) << std::endl;
//     std::cout << "root: " << servers.at(0).locations.at(0).root << std::endl;
//     std::cout << "Index : " << servers.at(0).locations.at(0).index << std::endl;
//     std::cout << "Autoindex : " << servers.at(0).locations.at(0).autoindex << std::endl;
//     std::cout << "return code: " << servers.at(0).locations.at(0).return_code << std::endl;
//     std::cout << "return: " << servers.at(0).locations.at(0).return_redir << std::endl;
//     std::cout << "upload_store: " << servers.at(0).locations.at(0).upload_store << std::endl;
//     std::cout << "Cgi : " << servers.at(0).locations.at(0).cgi[".sh"] << std::endl;
//     if ( servers.at(0).locations.at(0).client_max_body_size == (size_t)-1)
//         std::cout << "client_max_body_size : -1 " << std::endl;
//     else
//         std::cout << "client_max_body_size : " << servers.at(0).locations.at(0).client_max_body_size << std::endl;
//     std::cout << std::endl;
// }