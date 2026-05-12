/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   cgi.cpp                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: Leny <Leny@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/12 16:25:13 by Leny              #+#    #+#             */
/*   Updated: 2026/05/12 17:09:19 by Leny             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "cgi/Cgi.hpp"

Cgi::Cgi(){
}
Cgi::~Cgi(){}

void Cgi::execute(const Request& req)
{
    this->target = req.requestLine().target();
    this->method = req.requestLine().method();
    
    std::cout << "Target: " << target << std::endl;
    std::cout << "Method: " << method << std::endl;
}