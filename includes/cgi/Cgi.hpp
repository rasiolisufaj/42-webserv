/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   cgi.hpp                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: Leny <Leny@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/12 16:26:03 by Leny              #+#    #+#             */
/*   Updated: 2026/05/13 16:50:40 by Leny             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CGI_HPP
#define CGI_HPP

#include "http/Request.hpp"
#include <string>
#include <iostream>
#include <map>
#include <sstream>

class Cgi{
    private:
    public:
        Cgi();
        ~Cgi();
        void print_env(std::map<std::string, std::string> env_print) const;
        std::map<std::string, std::string> setUpEnvironment(const Request& req);
        char **convertMapToEnvp(std::map<std::string, std::string> &envMap);
};

#endif