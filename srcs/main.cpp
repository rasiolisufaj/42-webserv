#include "ConfigParser.hpp"

int main(int ac, char **av)
{
    ConfigParser Parser;
    if (ac != 2)
        return 1;

    if(Parser.parseConfig(av[1]) != 0)
        return (1);
    Parser.print_servers();
    return (0);
}