/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Response.cpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/02 13:07:52 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/02 13:38:43 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "http/Response.hpp"
#include <sstream>
#include <stdexcept>


//const/dest
Response::Response(){ _status_code = 200;_reason = "OK"; }
Response::Response(int status_code, const std::string &reason){ _status_code = status_code; _reason = reason; }
Response::~Response(){}

static std::string size_to_string(size_t n)
{
    std::ostringstream oss;

    oss << n;
    return (oss.str());
}

//Response response(404, "Not Found");
/*
200 -> OK
201 -> Created
204 -> No Content
301 -> Moved Permanently
303 -> See Other
400 -> Bad Request
403 -> Forbidden
404 -> Not Found
405 -> Method Not Allowed
413 -> Payload Too Large
500 -> Internal Server Error
501 -> Not Implemented
505 -> HTTP Version Not Supported
*/
void Response::setStatus(int status_code, const std::string &reason)
{
    _status_code = status_code;
    _reason = reason;
}

void Response::setHeader(const std::string &key, const std::string &value){ _headers[key] = value; }

void Response::setBody(const std::string &body)
{
    _body = body;
    _headers["Content-Length"] = size_to_string(_body.size());
}

int Response::statusCode() const{ return (_status_code); }
const std::string &Response::reason() const{ return (_reason); }
const std::string &Response::body() const{ return (_body); }


bool Response::hasHeader(const std::string &key) const
{
    if (_headers.find(key) != _headers.end())
        return (true);
    return (false);
}

std::string Response::getHeader(const std::string &key) const
{
    std::map<std::string, std::string>::const_iterator it;

    it = _headers.find(key);
    if (it == _headers.end())
        throw(std::runtime_error("Response: header not found"));

    return (it->second);
}

std::string Response::toString() const
{
    std::ostringstream oss; //flux string construction
    std::map<std::string, std::string>::const_iterator it; //for headers

    //HTTP-Version SP Status-Code SP Reason-Phrase CRLF
     //Status line HTTP
    oss << "HTTP/1.1 " << _status_code << " " << _reason << "\r\n";

    it = _headers.begin();
    while (it != _headers.end())
    {
        oss << it->first << ": " << it->second << "\r\n";
        it++;
    }

    oss << "\r\n";
    oss << _body;

    return (oss.str());
}