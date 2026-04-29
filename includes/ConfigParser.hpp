/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigParser.hpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lellanos <lellanos@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/27 13:25:56 by lellanos          #+#    #+#             */
/*   Updated: 2026/04/28 15:34:02 by lellanos         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef ConfigParser_hpp
#define ConfigParser_hpp

#include <fstream>
#include <string>
#include <iostream>
#include <cstring>
#include <sys/stat.h>
#include <sstream>
#include <vector>
#include "ServerConfig.hpp"

class ConfigParser
{
    private:
        std::vector<std::vector<std::string> > tokens;
        int readFile(std::string path);
        std::vector<std::string> splitLine(std::string line);
        std::string addSpaces(std::string line);
        std::string removeComments(std::string line);
        int validateBrackets(std::vector<std::vector<std::string> > tokens);
        int validateEndLines(std::vector<std::vector<std::string> > tokens);
        int fillConfig(std::vector<std::vector<std::string> > tokens);
    public:
        std::vector<ServerConfig> servers;
        ConfigParser();
        ~ConfigParser();
        int parseConfig(std::string path);
};

#endif