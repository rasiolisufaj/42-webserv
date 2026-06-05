/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpStatus.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/05 14:25:40 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/05 14:42:13 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "http/HttpStatus.hpp"
#include <cstdlib>
#include <sstream>
 
std::string reason_phrase(int code)
{
    switch (code)
    {
        case 200: return ("OK");
        case 201: return ("Created");
        case 204: return ("No Content");
        case 301: return ("Moved Permanently");
        case 302: return ("Found");
        case 303: return ("See Other");
        case 307: return ("Temporary Redirect");
        case 308: return ("Permanent Redirect");
        case 400: return ("Bad Request");
        case 403: return ("Forbidden");
        case 404: return ("Not Found");
        case 405: return ("Method Not Allowed");
        case 408: return ("Request Timeout");
        case 411: return ("Length Required");
        case 413: return ("Payload Too Large");
        case 414: return ("URI Too Long");
        case 431: return ("Request Header Fields Too Large");
        case 500: return ("Internal Server Error");
        case 501: return ("Not Implemented");
        case 502: return ("Bad Gateway");
        case 504: return ("Gateway Timeout");
        case 505: return ("HTTP Version Not Supported");
        default:  return ("Unknown");
    }
}
 
int status_from_exception(const char *what)
{
    int code;
 
    code = std::atoi(what);
    if (code < 100 || code > 599)
        return (500);
    return (code);
}
 
std::string default_error_body(int code)
{
    std::ostringstream body;
 
    body << "<html><head><title>" << code << " " << reason_phrase(code)
         << "</title></head><body><h1>" << code << " " << reason_phrase(code)
         << "</h1><hr><p>webserv</p></body></html>";
    return (body.str());
}
