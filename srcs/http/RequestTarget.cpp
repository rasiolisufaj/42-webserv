/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   RequestTarget.cpp                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 10:29:13 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/04 14:13:10 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "http/RequestTarget.hpp"
#include "http/utils_http.hpp"
#include <exception>
#include <stdexcept>
#include <vector>

//%2F = representation hexadec de 2F = '/'

static void split_path_query(const std::string &target, std::string &path, std::string &query)
{
    size_t pos;

    pos = target.find('?');
    if (pos == std::string::npos)
    {
        path = target;
        query = "";
    }
    else
    {
        path = target.substr(0, pos);
        query = target.substr(pos + 1);
    }
}

static char decode_one_escape(const std::string &s, size_t &i)
{
    int hi;
    int lo;
    int value;

    if (i + 2 >= s.size())
        throw(std::runtime_error("400 Bad Request: truncated percent escape"));

    hi = hex_value(s[i + 1]);
    lo = hex_value(s[i + 2]);

    if (hi < 0 || lo < 0)
        throw(std::runtime_error("400 Bad Request: invalid hex in percent escape"));

    value = (hi << 4) | lo;

    if (value == 0)
        throw(std::runtime_error("400 Bad Request: null byte in path"));

    i += 3;
    return (static_cast<char>(value));
}

static std::string percent_decode(const std::string &s)
{
    std::string result;
    size_t i;

    i = 0;
    while (i < s.size())
    {
        if (s[i] == '%')
        {
            result += decode_one_escape(s, i);
        }
        else
        {
            result += s[i];
            i++;
        }
    }
    return (result);
}

static void validate_decoded_path(const std::string &s)
{
    size_t i;
    unsigned char uc;

    i = 0;
    while (i < s.size())
    {
        uc = static_cast<unsigned char>(s[i]);
        if (uc <= 31 || uc == 127)
            throw(std::runtime_error("400 Bad Request: control char in decoded path"));
        i++;
    }
}

static std::string remove_dot_segments(const std::string &path)
{
    std::vector<std::string> segments;
    std::string segment;
    std::string result;
    size_t i;
    size_t start;
    bool is_dir;

    i = 0;
    is_dir = false;
    while (i < path.size())
    {
        while (i < path.size() && path[i] == '/') //skip /
            i++;
        if (i >= path.size())
        {
            is_dir = true; // finish by /
            break;
        }
        start = i;
        while (i < path.size() && path[i] != '/') //read un segment
            i++;
        segment = path.substr(start, i - start);

        if (segment == ".")
        {
            if (i >= path.size())
                is_dir = true; // "/a/." => 'a' dir
        }
        else if (segment == "..")
        {
            if (segments.empty())
                throw(std::runtime_error("400 Bad Request: path escapes root"));
            segments.pop_back();//go back (rm from the top of the stack)
            if (i >= path.size())
                is_dir = true; // "/a/b/.." -> "/a/"
        }
        else
            segments.push_back(segment);
    }

    if (segments.empty())
        return ("/");

    i = 0;
    while (i < segments.size())
    {
        result += "/" + segments[i];
        i++;
    }
    if (is_dir)
        result += "/";
    return (result);
}

/* choix securite (like nginx) decode before normalisation */
void RequestTarget::parse(const std::string &target)
{
    std::string raw_path;
    std::string raw_query;

    split_path_query(target, raw_path, raw_query);

    _path = percent_decode(raw_path); //decode before normalisation (representation en %HEXA => char ) 
    validate_decoded_path(_path);
    _path = remove_dot_segments(_path); // normalisation after decode
    _query = raw_query;
}

const std::string &RequestTarget::path() const { return (_path); }
const std::string &RequestTarget::query() const { return (_query); }