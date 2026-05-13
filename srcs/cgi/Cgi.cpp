/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   cgi.cpp                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: Leny <Leny@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/12 16:25:13 by Leny              #+#    #+#             */
/*   Updated: 2026/05/13 17:32:13 by Leny             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "cgi/Cgi.hpp"

Cgi::Cgi(){
}
Cgi::~Cgi(){}

void Cgi::print_env(std::map<std::string, std::string> env_print) const
{
   for (std::map<std::string, std::string>::const_iterator it = env_print.begin(); it != env_print.end(); ++it) 
    {
        std::cout << it->first << ": " << it->second << std::endl;
    }
}


char **Cgi::convertMapToEnvp(std::map<std::string, std::string> &envMap)
{
    char **envp = new char*[envMap.size() + 1];
    if (!envp)
        return NULL;
    envp[envMap.size()] = NULL;

    int i = 0;
    for (std::map<std::string, std::string>::const_iterator it = envMap.begin(); it != envMap.end(); ++it) 
    {
        std::string value = it->first + "=" + it->second;
        envp[i] = strdup(value.c_str());
        if(!envp[i])
        {
            i--;
            while (i >= 0)
            {
                free(envp[i]);
                i--;
            }
            delete[] envp;
            return NULL;
        }    
        i++;
    }
    return envp;
}

std::map<std::string, std::string> Cgi::setUpEnvironment(const Request& req)
{
    std::map<std::string, std::string> env;
    std::stringstream ss;

    ss << req.headers().getContentLength();
    std::string ContentLenght ;
    ss >> ContentLenght;
    
    env["GATEWAY_INTERFACE"] = "CGI/1.1";
    env["SERVER_PROTOCOL"] = req.requestLine().version();
    env["SERVER_SOFTWARE"] = "Webserv/1.0";
    env["REQUEST_METHOD"] = req.requestLine().method();
    env["PATH_INFO"] = req.requestLine().target();
    env["PATH_TRANSLATED"] = "./www/cgi-bin/test.py";
    env["QUERY_STRING"] = "" ;
    env["CONTENT_LENGTH"] = ContentLenght;
    env["CONTENT_TYPE"] = "" ;
    env["REMOTE_ADDR"] = "127.0.0.1";
    env["SCRIPT_NAME"] = "/opt/homebrew/bin/python3" ;
    env["SERVER_PORT"] = "8080" ;
    env["SERVER_NAME"] = "localhost" ;
    return env;
}

