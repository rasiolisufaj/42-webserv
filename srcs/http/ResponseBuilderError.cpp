/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ResponseBuilderError.cpp                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/05 14:24:52 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/07 15:23:11 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

/*
    REPONSES PILOTEES PAR LA CONFIG:
    - handleReturn: directive "return" (redirections et statuts forces)
    - errorResponse: pages d'erreur (custom via error_page, sinon generees)
*/
 
#include "http/ResponseBuilder.hpp"
#include "http/HttpStatus.hpp"
#include "http/MimeTypes.hpp"
#include <fstream>
#include <sstream>
 
/*
    return 301 /new   => statut + Location
    return 200 "txt"  => statut + body = texte (like nginx)
    return 404        => statut + body generate
*/
Response ResponseBuilder::handleReturn(const RouteContext &route) const
{
    int code;
 
    code = route.return_code;
    Response r(code, reason_phrase(code));
 
    if (code >= 300 && code <= 399 && !route.return_url.empty())
    {
        r.setHeader("Location", route.return_url);
        r.setHeader("Content-Type", "text/html");
        r.setBody(default_error_body(code));
        return (r);
    }
 
    r.setHeader("Content-Type", "text/plain");
    if (!route.return_url.empty())
        r.setBody(route.return_url + "\n");
    else
        r.setBody(reason_phrase(code) + "\n");
    return (r);
}
 
/*
    error_page valeur = URI (ex: "/errors/404.html"), pas un chemin disque.
    On la resout contre la root du serveur, comme nginx sert la page d'erreur
    depuis l'arborescence du document: root "www" + "/errors/404.html"
    => "www/errors/404.html".
*/
static std::string resolve_error_page_path(const std::string &root, const std::string &uri)
{
    std::string base;

    base = root;
    if (base.size() > 1 && base[base.size() - 1] == '/')
        base.erase(base.size() - 1);
    if (uri.empty())
        return (base);
    if (uri[0] == '/')
        return (base + uri);
    return (base + "/" + uri);
}

// Cherche une error_page custom dans le config; page absente/illisible => fallback genere
Response make_error_response(int code, const ServerConfig *server)
{
    Response r(code, reason_phrase(code));

    if (server != NULL)
    {
        std::map<int, std::string>::const_iterator it;
        it = server->error_pages.find(code);
        if (it != server->error_pages.end())
        {
            std::string fs_path = resolve_error_page_path(server->root, it->second);
            std::ifstream page(fs_path.c_str(), std::ios::binary);
            if (page.is_open())
            {
                std::ostringstream ss;
                ss << page.rdbuf();
                r.setHeader("Content-Type", mime_type(it->second));
                r.setBody(ss.str());
                return (r);
            }
        }
    }

    r.setHeader("Content-Type", "text/html");
    r.setBody(default_error_body(code));
    return (r);
}

Response ResponseBuilder::errorResponse(int code, const RouteContext &route) const
{
    return (make_error_response(code, route.server));
}
