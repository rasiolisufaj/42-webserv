#include "ServerConfig.hpp"

ServerConfig::ServerConfig() {
    port = 8080;         
    host = "127.0.0.1";
    client_max_body_size = 1000000;
}

ServerConfig::~ServerConfig() {}