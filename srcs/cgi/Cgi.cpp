/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Cgi.cpp                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: Leny <Leny@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/12 16:25:13 by Leny              #+#    #+#             */
/*   Updated: 2026/05/28 10:23:47 by Leny             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "cgi/Cgi.hpp"
#include <cstdio>

Cgi::Cgi(){}

Cgi::~Cgi(){}

void Cgi::print_env(std::map<std::string, std::string> env_print) const
{
   for (std::map<std::string, std::string>::const_iterator it = env_print.begin(); it != env_print.end(); ++it) 
    {
        std::cout << it->first << ": " << it->second << std::endl;
    }
}

void Cgi::free_array(char **array)
{
    if (!array)
        return ;
    for(int i = 0; array[i] != NULL; i++)
        free(array[i]);
    delete[] array;
}

int Cgi::execute(char **argv, char **envp)
{
    if (!argv || !envp)
    {
        return -1;
    }
    if (pipe(fd_in) < 0)
        return -1;
    else
        {
            if (pipe(fd_out) < 0)
            {
                close (fd_in[0]);
                close (fd_in[1]);
                return -1;
            }
        }
    pid_t pid = fork();
    if (pid == -1)
    {
        close(fd_in[0]);
        close(fd_in[1]);
        close(fd_out[0]);
        close(fd_out[1]);
        return -1;
    }
    else if (pid == 0)
    {
        //child
        dup2(fd_in[0], STDIN_FILENO);
        dup2(fd_out[1], STDOUT_FILENO);
        dup2(fd_out[1], STDERR_FILENO);
        close(fd_in[0]);
        close(fd_in[1]);
        close(fd_out[0]);
        close(fd_out[1]);

        if (execve(argv[0], argv, envp) == -1)
        {
            perror("Error en execve");
            free_array(argv);
            free_array(envp);
            exit(1);
        }
    }
    
    else 
    {
        // Father
        // fd_in[1] post for write, fd_out[0]listen the epoll
        close(fd_in[0]);
        close(fd_out[1]);
        return pid;
    }
    return -1;
}

char **Cgi::setupArgv(const Location &loc, std::map<std::string, std::string> &envMap)
{
    std::string extension = envMap["PATH_TRANSLATED"];
    size_t dotPos = extension.rfind('.');
    if (dotPos == std::string::npos)
    {
        return NULL;
    }
    extension = extension.substr(dotPos);
    
    std::map<std::string, std::string>::const_iterator it;
    it = loc.cgi.find(extension);
    if(it == loc.cgi.end())
        return NULL;

    char **argv =  new char*[3];
    if(!argv)
        return NULL;
    argv[2] = NULL;
    argv[0] = strdup(it->second.c_str());
    if(!argv[0])
    {
        delete[] argv;
        return NULL;
    }
    argv[1] = strdup(envMap["PATH_TRANSLATED"].c_str());
    if(!argv[1])
    {
        free(argv[0]);
        delete[] argv;
        return NULL;
    }
    return argv;
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

std::map<std::string, std::string> Cgi::setUpEnvironment(const RequestContext& ctx, const Location& loc)
{
    std::map<std::string, std::string> env;
    std::stringstream ss;

    ss << ctx.bodySize();
    std::string ContentLenght ;
    ss >> ContentLenght;
    
    env["GATEWAY_INTERFACE"] = "CGI/1.1";
    env["SERVER_PROTOCOL"] = ctx.request().requestLine().version();
    env["SERVER_SOFTWARE"] = "Webserv/1.0";
    env["REQUEST_METHOD"] = ctx.method();
    env["CONTENT_LENGTH"] = ContentLenght;
    env["PATH_TRANSLATED"] = loc.root + ctx.path();
    env["QUERY_STRING"] = ctx.query();
    env["PATH_INFO"] = "";
    if (ctx.headers().hasHeader("Content-Type"))
        env["CONTENT_TYPE"] = ctx.headers().getHeader("Content-Type");
    else
        env["CONTENT_TYPE"] = "";    
    env["REMOTE_ADDR"] = "127.0.0.1";
    env["SCRIPT_NAME"] = ctx.path();
    env["SERVER_PORT"] = ctx.effectiveHost().port ;
    env["SERVER_NAME"] = ctx.effectiveHost().name ;
    return env;
}

int Cgi::getFdInWrite()
{
    return fd_in[1];
}

int Cgi::getFdOutRead()
{
    return fd_out[0];
}