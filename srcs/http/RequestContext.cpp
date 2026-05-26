/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   RequestContext.cpp                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/26 10:09:20 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/26 10:17:04 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "http/RequestContext.hpp"
#include <stdexcept>

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

static bool is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

static bool is_valid_host_name(const std::string &name)
{
    size_t i;

    if (name.empty())
        return (false);

    i = 0;
    while (i < name.size())
    {
        if (is_ctl(name[i]))
            return (false);
        if (is_space_or_tab(name[i]))
            return (false);
        i++;
    }
    return (true);
}

static bool is_valid_port(const std::string &port)
{
    size_t i;
    size_t value;

    if (port.empty())
        return (false);

    i = 0;
    value = 0;
    while (i < port.size())
    {
        if (!is_digit(port[i]))
            return (false);

        value = value * 10 + (port[i] - '0');
        if (value > 65535)
            return (false);

        i++;
    }
    return (true);
}

/*
    Parse a Host-like authority value.

    Examples:
        "site-a.com"       -> name = "site-a.com", port = ""
        "site-a.com:8080"  -> name = "site-a.com", port = "8080"
        "localhost:8000"   -> name = "localhost", port = "8000"

    TODO:
        IPv6 authority like "[::1]:8080" is not supported yet.
*/
static HostInfo parse_host_authority(const std::string &value)
{
    HostInfo info;
    size_t pos;

    if (value.empty())
        throw(std::runtime_error("400 Bad Request: empty host authority"));

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

        if (!is_valid_port(info.port))
            throw(std::runtime_error("400 Bad Request: invalid host authority port"));
    }

    if (!is_valid_host_name(info.name))
        throw(std::runtime_error("400 Bad Request: invalid host authority name"));

    return (info);
}

RequestContext::RequestContext()
{
    _request = NULL;
    _local_port = -1;
    _body_size = 0;
    _use_authority = false;
}

/*
    Build a context usable by the config matcher.

    RFC 2616 §5.2:
    - if Request-URI is absoluteURI, use authority from Request-Line
    - otherwise, use Host header
*/
void RequestContext::build(const Request &request, int local_port)
{
    _request = &request;

    _local_port = local_port;
    _method = request.requestLine().method();
    _target = request.requestLine().target();
    _path = request.requestLine().path();
    _query = request.requestLine().query();
    _body_size = request.body().size();

    if (!request.requestLine().authority().empty())
    {
        _effective_host = parse_host_authority(request.requestLine().authority());
        _use_authority = true;
    }
    else
    {
        _effective_host = request.headers().getHost();
        _use_authority = false;
    }
}

int RequestContext::localPort() const { return (_local_port); }
const HostInfo &RequestContext::effectiveHost() const { return (_effective_host); }
const std::string &RequestContext::method() const { return (_method); }
const std::string &RequestContext::target() const { return (_target); }
const std::string &RequestContext::path() const { return (_path); }
const std::string &RequestContext::query() const { return (_query); }
size_t RequestContext::bodySize() const { return (_body_size); }
bool RequestContext::useAuthority() const { return (_use_authority); }

const Request &RequestContext::request() const
{
    if (_request == NULL)
        throw(std::runtime_error("RequestContext: request is NULL"));
    return (*_request);
}

const Headers &RequestContext::headers() const { return (request().headers()); }
const std::string &RequestContext::body() const { return (request().body()); }