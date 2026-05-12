/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   cgi.hpp                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: Leny <Leny@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/12 16:26:03 by Leny              #+#    #+#             */
/*   Updated: 2026/05/12 17:04:27 by Leny             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CGI_HPP
#define CGI_HPP

#include "http/Request.hpp"
#include <string>
#include <iostream>

class Cgi{
    private:
        std::string target;
        std::string method;
    public:
        Cgi();
        ~Cgi();
        void execute(const Request& req);
};

#endif