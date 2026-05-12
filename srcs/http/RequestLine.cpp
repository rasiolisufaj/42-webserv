/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   RequestLine.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: kobe <kobe@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/05 15:43:43 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/12 08:25:37 by kobe             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "RequestLine.hpp"
#include <cstddef> 
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

//
// > GET /index.html HTTP/1.1 (request line WITHOUT the final CRLF)
//

//split line into token and put into a vector 
static std::vector<std::string> split_by_space(const std::string &line)
{
    std::vector<std::string> tokens;
    size_t i;
    size_t start;

    i = 0;
    while (i < line.size())
    {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            i++;

        start = i;

        while (i < line.size() && line[i] != ' ' && line[i] != '\t')
            i++;

        if (start < i)
            tokens.push_back(line.substr(start, i - start));
    }
    return (tokens);
}

//support method
static bool check_method(const std::string &method)
{
    // TODO: add rules if other method add to be supported
    if (method == "GET")
        return (true);
    if (method == "HEAD")
        return (true);
    if (method == "POST")
        return (true);
    if (method == "DELETE")
        return (true);
    return (false);
}

//store token into RequestLine class
void RequestLine::parse(const std::string &line)
{
    std::vector<std::string> tokens;

    tokens = split_by_space(line);
    if (tokens.size() != 3)
        throw (std::runtime_error("400 Bad Request: invalid request line"));

    _method = tokens[0];
    _target = tokens[1];
    _version = tokens[2];

    if (!check_method(_method))
        throw (std::runtime_error("501 Method Not Implemented"));

    if (_version != "HTTP/1.1" && _version != "HTTP/1.0")
        throw (std::runtime_error("505 HTTP Version Not Supported"));
     
    // TODO: URI absolue: http://example.com/index.html
    //> GET http://example.com/index.html HTTP/1.1 (request line with absolue URI)
    if (_target.empty() || _target[0] != '/')
        throw (std::runtime_error("400 Bad Request: invalid target"));
    
}

//getters
const std::string& RequestLine::method() const{ return (_method); }
const std::string& RequestLine::target() const{ return (_target); }
const std::string& RequestLine::version() const{ return (_version); }
