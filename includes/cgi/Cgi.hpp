/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Cgi.hpp                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: Leny <Leny@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/12 16:26:03 by Leny              #+#    #+#             */
/*   Updated: 2026/05/26 14:20:48 by Leny             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CGI_HPP
#define CGI_HPP

#include "http/Request.hpp"
#include <string>
#include <iostream>
#include <map>
#include <sstream>
#include "config/ServerConfig.hpp"
#include <cstring>
#include <unistd.h>

class Cgi{
    private:
        int fd_in[2];
        int fd_out[2];
    public:
        Cgi();
        ~Cgi();
        void print_env(std::map<std::string, std::string> env_print) const;
        //std::map<std::string, std::string> setUpEnvironment(const Request& req, const Location& loc);
        std::map<std::string, std::string> setUpEnvironment(const Request& req);
        char **setupArgv(const Location &loc, std::map<std::string, std::string> &envMap);
        char **convertMapToEnvp(std::map<std::string, std::string> &envMap);
        int execute(char **argv, char **envp);
        void free_array(char **array);
        int getFdInWrite();
        int getFdOutRead();
};

#endif