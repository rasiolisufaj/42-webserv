/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigParser.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lellanos <lellanos@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/27 13:24:41 by lellanos          #+#    #+#             */
/*   Updated: 2026/04/27 16:01:50 by lellanos         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ConfigParser.hpp"

ConfigParser::ConfigParser()
{}

ConfigParser::~ConfigParser()
{}

int    ConfigParser::readFile(std::string path)
{
    std::string line;
    struct stat status;
    if(stat(path.c_str(), &status) == 0)
    {
        if(S_ISDIR(status.st_mode))
        {
            std::cout << "Is a directory: " << path << std::endl;
            return (1);
        }
    }
    else
    {
        std::cout << "Cannot open: " << path << ": " << strerror(errno) << std::endl;
        return (1);
    }
    
    std::ifstream MyReadFile(path.c_str());
    if (!MyReadFile.is_open())
    {
        std::cout << "Cannot open: " << path << ": " << strerror(errno) << std::endl;
        return (1);
    }
    while (getline (MyReadFile, line))
    {
        std::cout << line << std::endl;
    }
    MyReadFile.close();
    return (0);
}