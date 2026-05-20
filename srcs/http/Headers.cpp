/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Headers.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/06 13:05:23 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/20 11:11:46 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "http/Headers.hpp"
#include "http/utils_http.hpp"
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <limits>
#include <map>

/*
    ===RFC 2616 : 2.2===
    SP   = ' '
    HT   = '\t'
    CRLF = "\r\n"
    LWS  = [CRLF] + (SP | HT) (crlf with at least one SP or HT)
*/

static bool is_lws_char(char c)
{
    return (c == ' ' || c == '\t');
}

static std::string trim_lws(const std::string &s)
{
    size_t start;
    size_t end;

    start = 0;
    while (start < s.size() && is_lws_char(s[start]))
        start++;

    end = s.size();
    while (end > start && is_lws_char(s[end - 1]))
        end--;

    return (s.substr(start, end - start));
}

// Replace all SP/HT by one SP (Exemple: "curl     8.11.1" => "curl 8.11.1")
// PARSING CHOICE (can modify value)
static std::string normalize_lws_inside(const std::string &s)
{
    std::string result;
    size_t i;
    bool in_lws;

    i = 0;
    in_lws = false;
    while (i < s.size())
    {
        if (is_lws_char(s[i]))
        {
            if (!in_lws)
            {
                result += ' ';
                in_lws = true;
            }
        }
        else
        {
            result += s[i];
            in_lws = false;
        }
        i++;
    }
    return (result);
}

// control char (\r \t \n \0)
static bool is_ctl(char c)
{
    unsigned char uc;

    uc = static_cast<unsigned char>(c);
    return (uc <= 31 || uc == 127);
}

// token = all CHAR except CTLs || séparateurs
static bool is_separator(char c)
{
    if (c == '(' || c == ')' || c == '<' || c == '>')
        return (true);
    if (c == '@' || c == ',' || c == ';' || c == ':')
        return (true);
    if (c == '\\' || c == '"' || c == '/')
        return (true);
    if (c == '[' || c == ']' || c == '?')
        return (true);
    if (c == '=' || c == '{' || c == '}')
        return (true);
    if (c == ' ' || c == '\t')
        return (true);
    return (false);
}

static bool is_token_char(char c)
{
    unsigned char uc;

    uc = static_cast<unsigned char>(c);
    if (uc > 127)
        return (false);
    if (is_ctl(c) || is_separator(c))
        return (false);
    return (true);
}

static bool is_valid_header_name(const std::string &name)
{
    size_t i;

    if (name.empty())
        return (false);

    i = 0;
    while (i < name.size())
    {
        if (!is_token_char(name[i]))
            return (false);
        i++;
    }
    return (true);
}

// stock in _fields
void Headers::parseLine(const std::string &line)
{
    size_t pos;
    std::string key;
    std::string value;

    // separate Key : value
    pos = line.find(':');
    if (pos == std::string::npos)
        throw(std::runtime_error("400 Bad Request: invalid header line (no ':')"));

    key = line.substr(0, pos);
    value = line.substr(pos + 1);

    if (!is_valid_header_name(key))
        throw(std::runtime_error("400 Bad Request: invalid header name"));

    key = to_lower_ascii(key);
    value = trim_lws(value); // supp all spaces
    value = normalize_lws_inside(value);

    // stock in the map(_fields)
    // if duplicate HOST or CONTENT-LENGTH =>error  | else if more same other header => header : value1, value2...
    if (_fields.find(key) != _fields.end())
    {
        if (key == "host" || key == "content-length"  || key == "transfer-encoding")
            throw(std::runtime_error("400 Bad Request: duplicated header (host/content-length)"));
        _fields[key] += ", " + value;
    }
    else
        _fields[key] = value;
}

// utils
bool Headers::hasHeader(const std::string &name) const { return (_fields.find(to_lower_ascii(name)) != _fields.end()); }

std::string Headers::getHeader(const std::string &name) const
{
    std::map<std::string, std::string>::const_iterator it;
    std::string key;

    key = to_lower_ascii(name);
    it = _fields.find(key);
    if (it == _fields.end())
        throw(std::runtime_error(" getHeader(): Header not find"));
    return (it->second);
}

size_t Headers::getContentLength() const
{
    std::string value;
    size_t i;
    size_t result;
    size_t digit;
    size_t max;

    if (!hasHeader("content-length"))
        return (0);

    value = getHeader("content-length");
    if (value.empty())
        throw(std::runtime_error("400 Bad Request: invalid Content-Length"));

    result = 0;
    max = std::numeric_limits<size_t>::max();

    i = 0;
    while (i < value.size())
    {
        if (value[i] < '0' || value[i] > '9')
            throw(std::runtime_error("400 Bad Request: invalid Content-Length"));

        digit = value[i] - '0';

        if (result > (max - digit) / 10)
            throw(std::runtime_error("400 Bad Request: Content-Length too large"));

        result = result * 10 + digit;
        i++;
    }
    return (result);
}

HostInfo Headers::getHost() const
{
    HostInfo info;
    std::string value;
    size_t pos;

    value = getHeader("host");

    if (value.empty())
        throw(std::runtime_error("400 Bad Request: empty Host"));

    pos = value.find(':');
    if (pos == std::string::npos)
    {
        info.name = value;
        info.port = "";
    }
    else
    {
        info.name = value.substr(0, pos);
        info.port = value.substr(pos + 1);

        if (info.name.empty() || info.port.empty())
            throw(std::runtime_error("400 Bad Request: invalid Host"));

        // Port: digits only
        size_t i = 0;
        while (i < info.port.size())
        {
            if (info.port[i] < '0' || info.port[i] > '9')
                throw(std::runtime_error("400 Bad Request: invalid Host port"));
            i++;
        }
    }
    return (info);
}

bool Headers::wantsClose() const
{
    std::string value;

    if (!hasHeader("connection"))
        return (false);

    value = to_lower_ascii(getHeader("connection"));
    if (value == "close")
        return (true);

    return (false);
}