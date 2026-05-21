/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   RequestLine.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/05 15:43:43 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/21 10:47:30 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "http/RequestLine.hpp"
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

// UTILS
//  control char (\r \t \n \0)
static bool is_ctl(char c)
{
    unsigned char uc;

    uc = static_cast<unsigned char>(c);
    return (uc <= 31 || uc == 127);
}

static bool is_space_or_tab(char c)
{
    return (c == ' ' || c == '\t');
}

// split line(without \r\n) into token and put into a vector
static std::vector<std::string> split_request_line(const std::string &line)
{
    std::vector<std::string> tokens;
    size_t first_sp;
    size_t second_sp;

    if (line.empty())
        throw(std::runtime_error("400 Bad Request: empty request line"));

    if (line[0] == ' ' || line[line.size() - 1] == ' ')
        throw(std::runtime_error("400 Bad Request: invalid request line spacing"));

    if (line.find('\t') != std::string::npos)
        throw(std::runtime_error("400 Bad Request: tab in request line"));

    first_sp = line.find(' ');
    if (first_sp == std::string::npos)
        throw(std::runtime_error("400 Bad Request: invalid request line"));

    second_sp = line.find(' ', first_sp + 1);
    if (second_sp == std::string::npos)
        throw(std::runtime_error("400 Bad Request: invalid request line"));

    //if spaces > 2 
    if (line.find(' ', second_sp + 1) != std::string::npos)
        throw(std::runtime_error("400 Bad Request: too many spaces in request line"));

    //if space at the begining/ if space between>1 token/ if space at the end 
    if (first_sp == 0 || second_sp == first_sp + 1 || second_sp == line.size() - 1)
        throw(std::runtime_error("400 Bad Request: empty request line token"));

    tokens.push_back(line.substr(0, first_sp));
    tokens.push_back(line.substr(first_sp + 1, second_sp - first_sp - 1));
    tokens.push_back(line.substr(second_sp + 1));

    return (tokens);
}

// support method
static bool check_method(const std::string &method)
{
    // TODO: add rules if other method be supported
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

// valid target
// target struct for absolute URI
struct ParsedTarget
{
    std::string target;
    std::string authority;
};

static bool is_valid_origin_target(const std::string &target)
{
    size_t i;

    if (target.empty())
        return (false);

    if (target[0] != '/')
        return (false);

    i = 0;
    while (i < target.size())
    {
        if (is_ctl(target[i]))
            return (false);
        if (is_space_or_tab(target[i]))
            return (false);
        i++;
    }
    return (true);
}

static bool http_https(const std::string &s, const std::string &prefix)
{
    size_t i;

    if (s.size() < prefix.size())
        return (false);

    i = 0;
    while (i < prefix.size())
    {
        if (s[i] != prefix[i])
            return (false);
        i++;
    }
    return (true);
}

static bool is_absolute_uri(const std::string &target)
{
    if (http_https(target, "http://"))
        return (true);
    // no need but easy to check
    if (http_https(target, "https://"))
        return (true);
    return (false);
}
static ParsedTarget parse_absolute_uri(const std::string &uri)
{
    ParsedTarget result;
    size_t scheme_end;
    size_t authority_start;
    size_t slash_pos;
    size_t query_pos;
    size_t authority_end;

    scheme_end = uri.find("://");
    if (scheme_end == std::string::npos)
        throw(std::runtime_error("400 Bad Request: invalid absolute URI"));

    authority_start = scheme_end + 3;
    if (authority_start >= uri.size())
        throw(std::runtime_error("400 Bad Request: invalid absolute URI"));

    slash_pos = uri.find('/', authority_start);
    query_pos = uri.find('?', authority_start);

    if (slash_pos == std::string::npos && query_pos == std::string::npos)
    {
        authority_end = uri.size();
        result.target = "/";
    }
    else if (slash_pos == std::string::npos)
    {
        authority_end = query_pos;
        result.target = "/" + uri.substr(query_pos);
    }
    else if (query_pos == std::string::npos)
    {
        authority_end = slash_pos;
        result.target = uri.substr(slash_pos);
    }
    else if (query_pos < slash_pos)
    {
        authority_end = query_pos;
        result.target = "/" + uri.substr(query_pos);
    }
    else
    {
        authority_end = slash_pos;
        result.target = uri.substr(slash_pos);
    }

    result.authority = uri.substr(authority_start, authority_end - authority_start);

    if (result.authority.empty())
        throw(std::runtime_error("400 Bad Request: invalid absolute URI authority"));

    if (!is_valid_origin_target(result.target))
        throw(std::runtime_error("400 Bad Request: invalid absolute URI target"));

    return (result);
}

// PARSING
// store token into RequestLine class
void RequestLine::parse(const std::string &line)
{
    std::vector<std::string> tokens;
    
    ParsedTarget    parsed;
    std::string     method;
    std::string     target;
    std::string     version;
    std::string     authority;

    tokens = split_request_line(line);
    if (tokens.size() != 3)
        throw(std::runtime_error("400 Bad Request: invalid request line"));

    method = tokens[0];
    target = tokens[1];
    version = tokens[2];
    authority = "";

    if (!check_method(method))
        throw(std::runtime_error("501 Method Not Implemented"));

    if (version != "HTTP/1.1" && version != "HTTP/1.0")
        throw(std::runtime_error("505 HTTP Version Not Supported"));

    if (is_absolute_uri(target))
    {
        parsed = parse_absolute_uri(target);
        target = parsed.target;
        authority = parsed.authority;
    }
    else if (!is_valid_origin_target(target))
        throw(std::runtime_error("400 Bad Request: invalid target"));

    _parsed_target.parse(target);
    _method = method;
    _target = target;
    _version = version;
    _authority = authority;
}

// GETTERS
const std::string &RequestLine::method() const { return (_method); }
const std::string &RequestLine::target() const { return (_target); }
const std::string &RequestLine::version() const { return (_version); }
const std::string &RequestLine::authority() const { return (_authority); }
const std::string &RequestLine::path() const { return (_parsed_target.path()); }
const std::string &RequestLine::query() const { return (_parsed_target.query()); }