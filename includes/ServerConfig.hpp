#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <cstdlib>
#include <unistd.h>

struct Location {
    std::string                         path;
    std::string                         root;
    std::string                         index;
    bool                                autoindex;
    std::vector<std::string>            allowed_methods;
    int                                 return_code; 
    std::string                         return_redir;
    std::string                         upload_store; 
    std::map<std::string, std::string>  cgi;
    size_t                              client_max_body_size;

    Location() : 
    path(""), 
    root(""), 
    index("index.html"), 
    autoindex(false), 
    return_code(-1),
    return_redir(""),
    client_max_body_size((size_t)-1)
    {
        allowed_methods.push_back("GET");
    }
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
    void print_location(const Location& loc) const; 
    void print_servers() const;
};

#endif