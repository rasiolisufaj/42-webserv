/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigParser.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lellanos <lellanos@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/27 13:24:41 by lellanos          #+#    #+#             */
/*   Updated: 2026/05/07 17:09:11 by lellanos         ###   ########.fr       */
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
        //std::cout << "Error while filling config" << std::endl;
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

int ConfigParser::validate_error(std::string error_str)
{
    int i  = 0;
    int error = 0;
    
    while (error_str[i])
    {
        if(isdigit(error_str[i]))
            i++;
        else
            return 1;
    }
    error = atoi(error_str.c_str());
    if (error < 300 || error > 599)
        return 1;
    return 0;
}

int ConfigParser::validate_method(std::string method)
{
    if (method == "GET" || method == "POST" || method == "DELETE")
        return 0;
    else
        return 1;
}

int ConfigParser::validate_return_code(std::string return_code)
{
    int i  = 0;
    int code = 0;
    
    while (return_code[i])
    {
        if(isdigit(return_code[i]))
            i++;
        else
            return 1;
    }
    code = atoi(return_code.c_str());
    if (code < 200 || code > 599)
        return 1;
    return 0;
}

int ConfigParser::fillLocation(std::vector<std::vector<std::string> > &tokens, size_t &i, ServerConfig &server)
{
    Location new_location;
    bool inLocation = false;
    
    while (i < tokens.size())
    {
        size_t j = 0;
        size_t size = -1;
        int return_code = 0;
        while (j < tokens[i].size())
        {
            
            if (tokens[i][j] == "location")
            {
                inLocation = true;
                new_location.path = tokens[i][1];
            }
            
            else if(inLocation && tokens[i][j] == "allow_methods" && tokens[i].size() > 2)
            {
                j++;
                bool flag = true;
                while (tokens[i][j] != ";")
                {
                    if (validate_method(tokens[i][j]) == 0)
                    {
                        if(flag)
                        {
                            new_location.allowed_methods.clear();
                            flag = false;
                        }
                        new_location.allowed_methods.push_back(tokens[i][j]);
                        j++;
                    }
                    else
                    {
                        std::cout << "Error with 'Allowed Methods' " << std::endl;
                        return 1;
                    }
                }
            }
            else if (inLocation && tokens[i][j] == "root" && tokens[i].size() == 3)
            {
                new_location.root = tokens[i][j+1];
            }
            
            else if (inLocation && tokens[i][j] == "index" && tokens[i].size() == 3)
            {
                new_location.index = tokens[i][j+1];
            }
            
            else if (inLocation && tokens[i][j] == "autoindex" && tokens[i].size() == 3)
            {
                if (tokens[i][j+1] == "on")
                    new_location.autoindex = true;
                else if (tokens[i][j+1] == "off")
                    new_location.autoindex = false;
                else
                {
                    std::cout << "Error at location > autoindex " << std::endl;
                    return 1;
                }
            }
            
            else if (inLocation && tokens[i][j] == "return" && tokens[i].size() > 2)
            {
                if(validate_return_code(tokens[i][j+1]) != 0)
                {
                    std::cout << "Error at location > return " << std::endl;
                    return 1;
                }
                else
                {
                    return_code = atoi(tokens[i][j+1].c_str());
                    if ((return_code >= 300 && return_code <= 399))
                    {
                        if(tokens[i].size() == 4)
                        {
                            new_location.return_code = return_code;
                            new_location.return_redir = tokens[i][j+2];
                        }
                        else
                        {
                            std::cout << "Error at location > return " << std::endl;
                            return 1;
                        }
                    }
                    else
                    {
                        if(tokens[i].size() == 4)
                        {
                            new_location.return_code = return_code;
                            new_location.return_redir = tokens[i][j+2];
                        }
                        else
                            new_location.return_code = return_code;
                    }
                    
                }
            }
            
           else if (inLocation && tokens[i][j] == "upload_store" && tokens[i].size() == 3)
            {
                new_location.upload_store = tokens[i][j+1];
            }

           else if (inLocation && tokens[i][j] == "cgi" && tokens[i].size() == 4)
            {
                if (access(tokens[i][j+2].c_str(), X_OK) != 0)
                {
                    std::cerr << "Error with CGI: " << strerror(errno) << " : " << tokens[i][j+2] << std::endl;
                    return 1;
                }
                else
                    new_location.cgi[tokens[i][j+1]] = tokens[i][j+2];
            }
                        
            else if (inLocation && tokens[i][j] == "client_max_body_size" )
            {
                if (tokens[i].size() == 3)
                {
                    if (validate_body_size(tokens[i][j+1]) != 0)
                        return 1;
                    size = atoll(tokens[i][j+1].c_str());
                    new_location.client_max_body_size = size;
                }
                else
                {
                    std::cout << "Error in line 'location > client_max_body_size' " << std::endl;
                    return (1);
                }
            }
            
            else if (inLocation && tokens[i][j] == "}")
            {
                inLocation = false;
                if (new_location.client_max_body_size == (size_t)-1)
                    new_location.client_max_body_size = server.client_max_body_size;
                server.locations.push_back(new_location);
                return 0;
            }
        j++;
        }
    i++;
    }
    return 0;
}


int ConfigParser::fillConfig(std::vector<std::vector<std::string> > tokens)
{
    size_t i = 0;   
    int port = 0;
    size_t size = 0;
    int error = 0;
    bool inServer = false;

    while (i < tokens.size())
        {
            size_t  j = 0;
            while (j < tokens[i].size())
            {
                if(tokens[i][j] == "server")
                {
                    if (tokens[i][j+1] == "{")
                    {
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
                        {
                            std::cout << "Error in line 'listen' " << std::endl;
                            return 1;
                        }
                        port = atoi(tokens[i][j+1].c_str());
                        servers.back().port = port;
                    }
                    else
                        return (1);
                }
                else if (tokens[i][j] == "server_name")
                {
                    if (inServer && tokens[i].size() > 2)
                    {
                        j++;
                        while (tokens[i][j] != ";")
                        {
                            servers.back().server_names.push_back(tokens[i][j]);
                            j++;
                        }
                    }
                    else
                    {
                        std::cout << "Error in line 'server_name' " << std::endl;
                        return (1);
                    }
                }
                else if (tokens[i][j] == "client_max_body_size")
                {
                    if (inServer && tokens[i].size() > 2)
                    {
                        if (validate_body_size(tokens[i][j+1]) != 0)
                            return 1;
                        size = atoll(tokens[i][j+1].c_str());
                        servers.back().client_max_body_size = size;
                    }
                    else
                    {
                        std::cout << "Error in line 'client_max_body_size' " << std::endl;
                        return (1);
                    }
                }
                else if (tokens[i][j] == "error_page")
                {
                    if (inServer && tokens[i].size() == 4)
                    {
                        if (validate_error(tokens[i][j+1]) != 0)
                            return 1;
                        error = atoi(tokens[i][j+1].c_str());
                        servers.back().error_pages[error] = tokens[i][j+2];
                    }
                    else
                    {
                        std::cout << "Error in line 'error_page' " << std::endl;
                        return (1);
                    }
                }
                else if (tokens[i][j] == "location")
                {
                    if (inServer && tokens[i].size() == 3 && tokens[i][j+2] == "{")
                    {
                        if (fillLocation(tokens, i, servers.back()) != 0)
                        {
                            return 1;
                        }
                    }
                    else
                        return (1);
                }
                
                else if (tokens[i][j] == "}")
                {
                    inServer = false;
                }
                j++;
            }
            
            i++;
            
           
        }
                
        return (0);
}








void ConfigParser::print_location()
{
    
    std::cout << "Location path: " << servers.at(0).locations.at(0).path << std::endl;
    std::cout << "allow_methods: " << servers.at(0).locations.at(0).allowed_methods.at(0) << std::endl;
    std::cout << "root: " << servers.at(0).locations.at(0).root << std::endl;
    std::cout << "Index : " << servers.at(0).locations.at(0).index << std::endl;
    std::cout << "Autoindex : " << servers.at(0).locations.at(0).autoindex << std::endl;
    std::cout << "return code: " << servers.at(0).locations.at(0).return_code << std::endl;
    std::cout << "return: " << servers.at(0).locations.at(0).return_redir << std::endl;
    std::cout << "upload_store: " << servers.at(0).locations.at(0).upload_store << std::endl;
    std::cout << "Cgi : " << servers.at(0).locations.at(0).cgi[".sh"] << std::endl;
    if ( servers.at(0).locations.at(0).client_max_body_size == (size_t)-1)
        std::cout << "client_max_body_size : -1 " << std::endl;
    else
        std::cout << "client_max_body_size : " << servers.at(0).locations.at(0).client_max_body_size << std::endl;
    std::cout << std::endl;
}



void ConfigParser::print_servers()
{
    size_t i = 0;

    while (i < servers.size())
    {
    
        std::cout << "Servers size: " << servers.size() << std::endl;
        std::cout << "listen: " << servers.at(i).port << std::endl;
        std::cout << "server_name: " << servers.at(i).server_names.at(0) << std::endl;
        std::cout << "server_name: " << servers.at(i).server_names.at(1) << std::endl;
        std::cout << "server_name: " << servers.at(i).server_names.at(2) << std::endl;
        std::cout << "client_max_body_size: " << servers.at(0).client_max_body_size << std::endl;
        std::cout << "error_page: " << servers.at(i).error_pages[404] << std::endl;
        std::cout << "error_page: " << servers.at(i).error_pages[500] << std::endl << std::endl;
        
        //size_t j = 0;
        
        std::cout << "Location size:" << servers.at(i).locations.size();
        i++;
    }
}