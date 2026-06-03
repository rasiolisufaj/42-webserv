/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Response.cpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/02 13:07:52 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/03 16:31:19 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "http/Response.hpp"
#include <sstream>
#include <stdexcept>


//CTOR/DTOR
Response::Response(){ _status_code = 200;_reason = "OK"; }
Response::Response(int status_code, const std::string &reason){ _status_code = status_code; _reason = reason; }
Response::~Response(){}

//UTILS
//getteurs
int Response::statusCode() const{ return (_status_code); }
const std::string &Response::reason() const{ return (_reason); }
const std::string &Response::body() const{ return (_body); }

//banag bang itoa de dieu
static std::string size_to_string(size_t n)
{
    std::ostringstream oss;

    oss << n;
    return (oss.str());
}

//setteurs
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
std::string Response::toString() const
{
    std::ostringstream oss; //flux string construction bang bang 
    std::map<std::string, std::string>::const_iterator it; //for headers

    //Status line HTTP: HTTP-Version [SP] Status-Code [SP] Reason-Phrase [CRLF]
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