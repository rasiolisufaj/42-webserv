/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ResponseBuilderGet.cpp                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/05 14:20:56 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/05 14:33:19 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

/*
    LECTURE filesystem — la famille GET/HEAD:
    stat, fichier, repertoire (301 / index / autoindex / 403)
*/
 
#include "http/ResponseBuilder.hpp"
#include "http/HttpStatus.hpp"
#include "http/MimeTypes.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
 
/* ---- helpers locaux a l'autoindex (personne d'autre n'en a besoin) ---- */
 
static std::string html_escape(const std::string &s)
{
    std::string out;
    size_t i;
 
    i = 0;
    while (i < s.size())
    {
        if (s[i] == '&')
            out += "&amp;";
        else if (s[i] == '<')
            out += "&lt;";
        else if (s[i] == '>')
            out += "&gt;";
        else if (s[i] == '"')
            out += "&quot;";
        else
            out += s[i];
        i++;
    }
    return (out);
}
 
static bool is_unreserved(char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return (true);
    if (c == '-' || c == '.' || c == '_' || c == '~')
        return (true);
    return (false);
}
 
// percent-encode pour les href de l'autoindex (RFC 3986)
static std::string encode_href(const std::string &s)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    unsigned char c;
    size_t i;
 
    i = 0;
    while (i < s.size())
    {
        c = static_cast<unsigned char>(s[i]);
        if (is_unreserved(s[i]) || s[i] == '/')
            out += s[i];
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
        i++;
    }
    return (out);
}
 
/* ----------------------------- handlers ------------------------------ */
 
Response ResponseBuilder::handleGetHead(const RequestContext &ctx, const RouteContext &route,
                                        const std::string &file_path) const
{
    struct stat st;
 
    if (stat(file_path.c_str(), &st) != 0)
        return (errorResponse(404, route));
 
    if (S_ISDIR(st.st_mode))
    {
        // 1) repertoire demande sans slash final -> 301 (comme nginx),
        //    sinon les URLs relatives de la page seraient cassees
        if (ctx.path()[ctx.path().size() - 1] != '/')
            return (redirectSlash(ctx));
 
        // 2) fichier index ?
        std::string index_path = file_path;
        if (index_path[index_path.size() - 1] != '/')
            index_path += "/";
        index_path += route.index;
 
        struct stat ist;
        if (stat(index_path.c_str(), &ist) == 0 && S_ISREG(ist.st_mode))
            return (serveFile(index_path, route));
 
        // 3) listing ?
        if (route.autoindex)
            return (autoindexResponse(ctx, route, file_path));
 
        return (errorResponse(403, route));
    }
 
    if (!S_ISREG(st.st_mode)) // socket, fifo, device...
        return (errorResponse(403, route));
 
    return (serveFile(file_path, route));
}
 
Response ResponseBuilder::serveFile(const std::string &file_path, const RouteContext &route) const
{
    std::ifstream file(file_path.c_str(), std::ios::binary);
 
    if (!file.is_open())
        return (errorResponse(403, route)); // existe (stat ok) mais illisible => droits
 
    std::ostringstream ss;
    ss << file.rdbuf();
 
    Response r(200, reason_phrase(200));
    r.setHeader("Content-Type", mime_type(file_path));
    r.setBody(ss.str());
    return (r);
}
 
Response ResponseBuilder::autoindexResponse(const RequestContext &ctx, const RouteContext &route,
                                            const std::string &dir_path) const
{
    std::string base;
    DIR *dir;
    struct dirent *entry;
 
    base = dir_path;
    if (base.empty() || base[base.size() - 1] != '/')
        base += "/";
 
    dir = opendir(base.c_str());
    if (dir == NULL)
        return (errorResponse(403, route));
 
    std::ostringstream body;
    body << "<html><head><title>Index of " << html_escape(ctx.path())
         << "</title></head><body><h1>Index of " << html_escape(ctx.path())
         << "</h1><hr><ul>\n";
    body << "<li><a href=\"../\">../</a></li>\n";
 
    while ((entry = readdir(dir)) != NULL)
    {
        std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
 
        struct stat st;
        std::string full = base + name;
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            name += "/"; // les repertoires sont suffixes '/'
 
        body << "<li><a href=\"" << encode_href(name) << "\">"
             << html_escape(name) << "</a></li>\n";
    }
    closedir(dir);
 
    body << "</ul><hr></body></html>\n";
 
    Response r(200, reason_phrase(200));
    r.setHeader("Content-Type", "text/html");
    r.setBody(body.str());
    return (r);
}
 
/*
    GET /dir => 301 Location: /dir/ (+ query preservee, comme nginx).
    Indispensable pour que les liens RELATIFS de l'index/autoindex marchent.
*/
Response ResponseBuilder::redirectSlash(const RequestContext &ctx) const
{
    std::string location;
 
    location = ctx.path() + "/";
    if (!ctx.query().empty())
        location += "?" + ctx.query();
 
    Response r(301, reason_phrase(301));
    r.setHeader("Location", location);
    r.setHeader("Content-Type", "text/html");
    r.setBody(default_error_body(301));
    return (r);
}
