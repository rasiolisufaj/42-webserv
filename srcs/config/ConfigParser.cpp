/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigParser.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lellanos <lellanos@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/27 13:24:41 by lellanos          #+#    #+#             */
/*   Updated: 2026/05/01 15:22:54 by lellanos         ###   ########.fr       */
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
        std::vector<std::string> tokens_line;
        tokens_line = splitLine(line);
        if (!tokens_line.empty())
            tokens.push_back(tokens_line);
    }
    /*
    std::cout << "size --> " << tokens.size() << std::endl;
    std::cout << "size 14--> " << tokens[14].size() << std::endl;
    std::cout << "vector --> " << tokens[14][2] << std::endl;
    */
    MyReadFile.close();
    
    return (0);
}

std::vector<std::string>   ConfigParser::splitLine(std::string line)
{
    std::vector<std::string> token_line;
    std::string  word;
    line = removeComments(line);
    line = addSpaces(line);
    std::stringstream ss(line);
    while (ss >> word)
    {
        token_line.push_back(word);
    }
    return token_line;
}

std::string ConfigParser::addSpaces(std::string line)
{ 
    std::string new_line;
    int i = 0;
    while(line[i])
    {
        switch (line[i])
        {
            case ';' :
                new_line += " ; ";
                break;
            case '{' :
                new_line += " { ";
                break;
            case '}':
                new_line += " } ";
                break;
            default:
                new_line += line[i];
                break;
        }
        i++;
    }
    return new_line;
}

std::string ConfigParser::removeComments(std::string line)
{
    line = line.substr(0,line.find('#'));
    return line;   
}

int ConfigParser::validateEndLines(std::vector<std::vector<std::string> > tokens)
{
    size_t i = 0;
    while (i < tokens.size())
    {
       if(tokens[i][tokens[i].size() - 1] != "{" && tokens[i][tokens[i].size() - 1] != "}" && tokens[i][tokens[i].size() - 1] != ";")
            return (1);
    i++;
    }
    return (0);
}

int ConfigParser::validateBrackets(std::vector<std::vector<std::string> > tokens)
{
    size_t i = 0;
    int count_bracket = 0;
    while (i < tokens.size())
    {
        size_t  j = 0;
        while (j < tokens[i].size())
        {
            if(tokens[i][j] == "{")
                count_bracket++;
            else if (tokens[i][j] == "}")
                count_bracket--;
            if(count_bracket < 0)
                return -1;
            j++;
        }
        i++;
    }
    if (count_bracket != 0)
        return -1;
    return (0);
}

int ConfigParser::parseConfig(std::string path)
{
    if (readFile(path) != 0)
        return (-1);
    if (validateBrackets(tokens) != 0)
    {
        std::cout << "Invalid Brackets" << std::endl;
        return (-1);
    }
    if(validateEndLines(tokens) != 0)
    {
        std::cout << "Invalid sintaxis" << std::endl;
        return (1);
    }
    if (fillConfig(tokens) == 1)
    {
        std::cout << "Error while filling config" << std::endl;
        return (1);
    }
    return (0);
}


// Fill configuration ///////////
///////////////////////////////////

int ConfigParser::validate_body_size(std::string size_str)
{
    int i = 0;
    int size = 0;
    while(size_str[i])
    {
        if(isdigit(size_str[i]))
            i++;
        else
            return 1;
    }
    size = atoi(size_str.c_str());
    if (size <= 0)
        return 1;
    return 0;
}

int ConfigParser::validate_port(std::string port_str)
{
    int i = 0;
    int port = 0;
    while(port_str[i])
    {
        if(isdigit(port_str[i]))
            i++;
        else
            return 1;
    }
    port = atoi(port_str.c_str());
    if (port <= 0 || port > 65535)
        return 1;
    return 0;
}

int ConfigParser::fillConfig(std::vector<std::vector<std::string> > tokens)
{
    size_t i = 0;
   
    int port = 0;
    size_t size = 0;
    bool inServer = false;
    while (i < tokens.size())
        {
            size_t  j = 0;
            while (j < tokens[i].size())
            {
                //std::cout << "Tokens: < " << tokens[i][j] << " > " << i << j << std::endl;
                if(tokens[i][j] == "server")
                {
                    if (tokens[i][j+1] == "{")
                    {
                       // std::cout << "Server abierto " << i << std::endl;
                        inServer = true;
                        servers.push_back(ServerConfig());
                    }
                    
                    else
                        return (1);
                }
                else if (tokens[i][j] == "listen")
                {
                    if (inServer)
                    {
                        if(validate_port(tokens[i][j+1]) != 0)
                            return 1;
                        port = atoi(tokens[i][j+1].c_str());
                        servers.back().port = port;
                        //std::cout << "port: " << port << std::endl;
                    }
                    else
                        return (1);
                }
                else if (tokens[i][j] == "server_name")
                {
                    if (inServer)
                    {
                        j++;
                        while (tokens[i][j] != ";")
                        {
                            servers.back().server_names.push_back(tokens[i][j]);
                            j++;
                        }
                    }
                    else
                        return (1);
                }
                else if (tokens[i][j] == "client_max_body_size")
                {
                    if (inServer)
                    {
                        if (validate_body_size(tokens[i][j+1]) != 0)
                            return 1;
                        size = atoll(tokens[i][j+1].c_str());
                        servers.back().client_max_body_size = size;
                    }
                    else
                        return (1);
                }
                j++;
            }
            i++;
        }

        std::cout << "Tamanio de servers: " << servers.size() << std::endl;
        std::cout << "Server 1 port: " << servers.at(0).port << std::endl;
        std::cout << "Servers Name: " << servers.at(0).server_names.size() << std::endl;
        std::cout << "Server Name 0: " << servers.at(0).server_names.at(0) << std::endl;
        std::cout << "Server Name 1: " << servers.at(0).server_names.at(1) << std::endl;
        std::cout << "Server Name 2: " << servers.at(0).server_names.at(2) << std::endl;
        std::cout << "Size: " << servers.at(0).client_max_body_size << std::endl;
    return (0);
}