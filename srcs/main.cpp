#include "ConfigParser.hpp"

int main(int ac, char **av)
{
    ConfigParser Parser;
    if (ac != 2)
        return 1;

    if(!Parser.parseConfig(av[1]))
        return (1);
    return (0);
}