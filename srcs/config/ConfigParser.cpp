/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigParser.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: Leny <Leny@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/27 13:24:41 by lellanos          #+#    #+#             */
/*   Updated: 2026/05/12 15:00:52 by Leny             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "config/ConfigParser.hpp"

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
            logError("Is a directory", path);
            return (1);
        }
    }
    else
    {
        logError("Cannot open file", path + " > " + strerror(errno));
        return (1);
    }
    
    std::ifstream MyReadFile(path.c_str());
    if (!MyReadFile.is_open())
    {
        logError("Cannot open file", path + " > " + strerror(errno));
        return (1);
    }
    while (getline (MyReadFile, line))
    {
        std::vector<std::string> tokens_line;
        tokens_line = splitLine(line);
        if (!tokens_line.empty())
            tokens.push_back(tokens_line);
    }

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
        logError("Invalid Brackets");
        return (-1);
    }
    if(validateEndLines(tokens) != 0)
    {
        logError("Missing end line '{ } or ;'");
        return (1);
    }
    if (fillConfig(tokens) == 1)
    {
        return (1);
    }
    return (0);
}


int ConfigParser::validate_body_size(std::string size_str)
{
    size_t i = 0;
    size_t size = 0;
    if (size_str.empty())
        return 1;
    while(size_str[i])
    {
        if(isdigit(size_str[i]))
            i++;
        else
            return 1;
    }
    size = atoll(size_str.c_str());
    if (size <= 0)
        return 1;
    return 0;
}

 int ConfigParser::validate_digits(std::string number_str)
 {
    int i = 0;
    while(number_str[i])
    {
        if(isdigit(number_str[i]))
            i++;
        else
            return 1;
    }
    return 0;
 }

int ConfigParser::validate_port(std::string port_str)
{
    int port = 0;
    
    if(validate_digits(port_str))
        return 1;
    port = atoi(port_str.c_str());
    if (port <= 0 || port > 65535)
        return 1;
    return 0;
}

int ConfigParser::validate_error(std::string error_str)
{
    int error = 0;
    
    if(validate_digits(error_str))
        return 1;
    error = atoi(error_str.c_str());
    if (error < 300 || error > 599)
        return 1;
    return 0;
}

int ConfigParser::validate_method(std::string method)
{
    if (method == "GET" || method == "POST" || method == "DELETE" || method == "HEAD")
        return 0;
    else
        return 1;
}

int ConfigParser::validate_return_code(std::string return_code)
{
    int code = 0;
    
    if(validate_digits(return_code))
        return 1;
        
    code = atoi(return_code.c_str());
    if (code < 200 || code > 599)
        return 1;
    return 0;
}

int ConfigParser::validate_extension(std::string extension)
{
    if (extension[0] != '.' || extension.size() < 2)
            return 1;
    return 0;
}

int ConfigParser::validate_segment(std::string segment)
{
    int value = -1;
    if (segment == "")
        return 1;
    if(validate_digits(segment))
        return 1;
    value = atoi(segment.c_str());
    if (value < 0 || value > 255)
        return 1;
    return 0;
}

int ConfigParser::validate_host(std::string host_str)
{
    std::string segment;
    
    int count_dots = std::count(host_str.begin(), host_str.end(), '.');
    if (count_dots != 3)
        return 1;
    
    std::stringstream ss(host_str);
    while(getline(ss, segment, '.'))
    {
        if (validate_segment(segment))
            return 1;
    }
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
                        logError("Invalid allowed methods", tokens[i][j]);
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
                    logError("Invalid autoindex", tokens[i][j+1]);
                    return 1;
                }
            }
            
            else if (inLocation && tokens[i][j] == "return" && tokens[i].size() > 2)
            {
                if(validate_return_code(tokens[i][j+1]) != 0)
                {
                    logError("Invalid return", tokens[i][j+1]);
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
                            logError("Invalid return, missing url", tokens[i][j+1]);
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
                struct stat s;
                if (stat(tokens[i][j+1].c_str(), &s) == 0) 
                {
                    if (s.st_mode & S_IFDIR) 
                    {
                        new_location.upload_store = tokens[i][j+1];
                    }
                    else
                    {
                        logError("Is not a directory : ", tokens[i][j+1]);
                        return 1;
                    }
                }
                else
                {
                    logError("Directory path does not exist: ", tokens[i][j+1]);
                    return 1;
                }
            }

           else if (inLocation && tokens[i][j] == "cgi" && tokens[i].size() == 4)
            {
                struct stat s;
                if (stat(tokens[i][j+2].c_str(), &s) == 0) 
                {
                    if (s.st_mode & S_IFDIR) 
                    {
                        logError("CGI path is a directory", tokens[i][j+2]);
                        return 1;
                    }
                }
                if (validate_extension(tokens[i][j+1]) != 0)
                {
                    logError("CGI extension", tokens[i][j+1]);
                    return 1;
                }
                new_location.cgi[tokens[i][j+1]] = tokens[i][j+2];
            }
                        
            else if (inLocation && tokens[i][j] == "client_max_body_size" )
            {
                if (tokens[i].size() == 3)
                {
                    if (validate_body_size(tokens[i][j+1]) != 0)
                    {
                        logError("Incorrect client_max_body_size", tokens[i][j+1]);
                        return 1;
                    }
                    size = atoll(tokens[i][j+1].c_str());
                    new_location.client_max_body_size = size;
                }
                else
                {
                    logError("Incorrect client_max_body_size", tokens[i][j+1]);
                    return (1);
                }
            }
            
            else if (inLocation && tokens[i][j] == "}")
            {
                inLocation = false;
                if (new_location.client_max_body_size == (size_t)-1)
                    new_location.client_max_body_size = server.client_max_body_size;
                if (new_location.upload_store == "")
                    new_location.upload_store = server.upload_store;
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
                if(tokens[i][j] == "server" )
                {
                    if (tokens[i][j+1] == "{" && tokens[i].size() == 2)
                    {
                        inServer = true;
                        servers.push_back(ServerConfig());
                    }
                    else
                    {
                        logError("Invalid server", tokens[i][j+1]);
                        return (1);
                    }
                }
             
                else if (tokens[i][j] == "listen")
                {
                    if (inServer)
                    {
                        size_t pos = tokens[i][j+1].find(":");
                        if (pos == std::string::npos)
                        {
                            if(validate_port(tokens[i][j+1]) != 0)
                            {
                                logError("Invalid port", tokens[i][j+1]);
                                return 1;
                            }
                            port = atoi(tokens[i][j+1].c_str());
                            servers.back().port = port;
                        }
                        else
                        {
                            std::string host_str = "";
                            std::string port_str = "";
                            host_str = tokens[i][j+1].substr(0,pos);
                            port_str = tokens[i][j+1].substr(pos+1);
                            if (validate_port(port_str) || validate_host(host_str))
                            {
                                logError("Invalid port", tokens[i][j+1]);
                                return 1;
                            }
                            port = atoi(port_str.c_str());
                            servers.back().host = host_str;
                            servers.back().port = port;
                        }
                    }
                    else
                    {
                        logError("Invalid port", tokens[i][j+1]);
                        return (1);
                    }
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
                        logError("Invalid server_name");
                        return (1);
                    }
                }
             
                else if (tokens[i][j] == "client_max_body_size")
                {
                    if (inServer && tokens[i].size() == 3)
                    {
                        if (validate_body_size(tokens[i][j+1]) != 0)
                        {
                            logError("Invalid server client_max_body_size", tokens[i][j+1]);
                            return 1;
                        }
                        size = atoll(tokens[i][j+1].c_str());
                        servers.back().client_max_body_size = size;
                    }
                    else
                    {
                        logError("Invalid server client_max_body_size", tokens[i][j+1]);
                        return (1);
                    }
                }

                else if (tokens[i][j] == "client_max_header_size")
                {
                    if (inServer && tokens[i].size() == 3)
                    {
                        if (validate_body_size(tokens[i][j+1]) != 0)
                        {
                            logError("Invalid server client_max_header_size", tokens[i][j+1]);
                            return 1;
                        }
                        size = atoll(tokens[i][j+1].c_str());
                        servers.back().client_max_header_size = size;
                    }
                    else
                    {
                        logError("Invalid server client_max_header_size", tokens[i][j+1]);
                        return (1);
                    }
                }
                
                else if (tokens[i][j] == "error_page")
                {
                    if (inServer && tokens[i].size() == 4)
                    {
                        if (validate_error(tokens[i][j+1]) != 0)
                        {
                            logError("Invalid server error_page", tokens[i][j+1]);
                            return 1;
                        }
                        error = atoi(tokens[i][j+1].c_str());
                        servers.back().error_pages[error] = tokens[i][j+2];
                    }
                    else
                    {
                        logError("Invalid server error_page", tokens[i][j+1]);
                        return (1);
                    }
                }
                
                else if (inServer && tokens[i][j] == "upload_store" && tokens[i].size() == 3)
                {
                    struct stat s;
                    if (stat(tokens[i][j+1].c_str(), &s) == 0) 
                    {
                        if (s.st_mode & S_IFDIR) 
                        {
                            servers.back().upload_store = tokens[i][j+1];
                        }
                        else
                        {
                            logError("Is not a directory : ", tokens[i][j+1]);
                            return 1;
                        }
                    }
                    else
                    {
                        logError("Directory path does not exist: ", tokens[i][j+1]);
                        return 1;
                    }
                }
             
                else if (tokens[i][j] == "location")
                {
                    if (inServer && tokens[i].size() == 3 && tokens[i][j+2] == "{")
                    {
                        if (fillLocation(tokens, i, servers.back()) != 0)
                        {
                            //logError("Invalid location leny");
                            return 1;
                        }
                    }
                    else
                    {
                        logError("Invalid location");
                        return (1);
                    }        
                }
                
                else if (tokens[i][j] == "}")
                {
                    inServer = false;
                }
                
                j++;
            }
            i++;  
        }
        
        if (servers.back().upload_store == "")
        {
            logError("Missing upload_store value for server");
            return (1);
        }
        return (0);
}


const std::vector<ServerConfig>& ConfigParser::getServers() const
{
    return servers;
}

void ConfigParser::logError(const std::string& message, const std::string& detail) 
{
    std::cerr << "ERROR : " << message;
        if (!detail.empty()) 
        {
            std::cerr << " (" << detail << ")" ;
        }
    std::cerr << std::endl;
}





