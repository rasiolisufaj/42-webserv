#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <cstdlib>

struct Location {
    std::string path;
    std::string root;
    std::string index;
    bool        autoindex;
    
    Location() : autoindex(false) {}
};

class ServerConfig {
public:
    int                         port;
    std::string                 host;
    std::vector<std::string>    server_names;
    std::map<int, std::string>  error_pages;
    size_t                      client_max_body_size;
    std::vector<Location>       locations;
    ServerConfig();
    ~ServerConfig();
};

#endif