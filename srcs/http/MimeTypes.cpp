/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   MimeTypes.cpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/05 14:42:31 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/05 14:50:51 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "http/MimeTypes.hpp"
#include "http/utils_http.hpp"
 
std::string mime_type(const std::string &path)
{
    size_t slash;
    size_t dot;
    std::string ext;
 
    slash = path.find_last_of('/');
    dot = path.find_last_of('.');
 
    // pas de '.' ou '.' dans un nom de repertoire => pas d'extension
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return ("application/octet-stream");
 
    ext = to_lower_ascii(path.substr(dot + 1));
 
    if (ext == "html" || ext == "htm")
        return ("text/html");
    if (ext == "css")
        return ("text/css");
    if (ext == "js")
        return ("application/javascript");
    if (ext == "json")
        return ("application/json");
    if (ext == "txt")
        return ("text/plain");
    if (ext == "png")
        return ("image/png");
    if (ext == "jpg" || ext == "jpeg")
        return ("image/jpeg");
    if (ext == "gif")
        return ("image/gif");
    if (ext == "svg")
        return ("image/svg+xml");
    if (ext == "ico")
        return ("image/x-icon");
    if (ext == "webp")
        return ("image/webp");
    if (ext == "pdf")
        return ("application/pdf");
    if (ext == "mp4")
        return ("video/mp4");
    if (ext == "mp3")
        return ("audio/mpeg");
    return ("application/octet-stream");
}
