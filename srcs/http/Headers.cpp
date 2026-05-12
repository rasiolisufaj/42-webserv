/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Headers.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@learner.42.tech>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/06 13:05:23 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/06 13:05:25 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Headers.hpp"
#include <cstddef>
#include <exception>
#include <stdexcept>
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

// ascii only!
static std::string to_lower_ascii(const std::string &s)
{
    std::string result;
    size_t i;
    char c;

    result = s;
    i = 0;
    while (i < result.size())
    {
        c = result[i];
        if (c >= 'A' && c <= 'Z')
            result[i] = c + 32;
        i++;
    }
    return (result);
}

// control char
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
    value = trim_lws(value);
    value = normalize_lws_inside(value);

    // stock in the map(_fields)
    // if two host or content-length =>error  | else if more same other header => header : value1, value2...
    // PARSING CHOICE headers duplicate!
    if (_fields.find(key) != _fields.end())
    {
        if (key == "host" || key == "content-length")
            throw(std::runtime_error("400 Bad Request: duplicated header (host/content-length)"));
        _fields[key] += ", " + value;
    }
    else
        _fields[key] = value;
}


//utils
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
    size_t      i;
    size_t      result;

    if (!hasHeader("content-length"))
        return (0);

    value = getHeader("content-length");
    if (value.empty())
        throw(std::runtime_error("400 Bad Request: invalid Content-Length"));

    result = 0;
    i = 0;
    while (i < value.size())
    {
        if (value[i] < '0' || value[i] > '9')
            throw(std::runtime_error("400 Bad Request: invalid Content-Length"));

        result = result * 10 + (value[i] - '0');
        i++;
    }
    return (result);
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