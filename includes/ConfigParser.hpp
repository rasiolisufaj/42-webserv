/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigParser.hpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lellanos <lellanos@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/27 13:25:56 by lellanos          #+#    #+#             */
/*   Updated: 2026/05/10 15:01:49 by lellanos         ###   ########.fr       */
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
#include <algorithm>

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
        int validate_port(std::string port);
        int validate_body_size(std::string size_str);
        int validate_error(std::string size_str);
        int fillLocation(std::vector<std::vector<std::string> > &tokens, size_t &i, ServerConfig &server);
        int validate_method(std::string method);
        int validate_return_code(std::string return_code);
        int validate_extension(std::string extension);
        int validate_host(std::string host_str);
        int validate_segment(std::string segment);
        int validate_digits(std::string number_str);
        static void logError(const std::string& message, const std::string& detail = "");
        std::vector<ServerConfig> servers;

    public:
        
        ConfigParser();
        ~ConfigParser();
        int parseConfig(std::string path);
        const std::vector<ServerConfig>& getServers() const;
};

#endif