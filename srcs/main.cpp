#include "config/ConfigParser.hpp"

int main(int ac, char **av)
{
    ConfigParser Parser;
    if (ac != 2)
        return 1;

    if(Parser.parseConfig(av[1]) != 0)
        return (1);

    const std::vector<ServerConfig>& all_servers = Parser.getServers();
    
    for (size_t i = 0; i < all_servers.size(); i++) 
    {
        all_servers[i].print_servers();
    }
    return (0);
}