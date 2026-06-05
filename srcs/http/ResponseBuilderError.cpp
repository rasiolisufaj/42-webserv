/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ResponseBuilderError.cpp                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/05 14:24:52 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/05 14:32:44 by thodavid         ###   ########.fr       */
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
    return 301 /new   => statut + Location (le parser garantit l'URL en 3xx)
    return 200 "txt"  => statut + body = texte (semantique nginx)
    return 404        => statut + body genere
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
    Cherche une error_page custom dans le config; page absente ou
    illisible => fallback genere. JAMAIS de recursion errorResponse().
*/
Response ResponseBuilder::errorResponse(int code, const RouteContext &route) const
{
    Response r(code, reason_phrase(code));
 
    if (route.server != NULL)
    {
        std::map<int, std::string>::const_iterator it;
        it = route.server->error_pages.find(code);
        if (it != route.server->error_pages.end())
        {
            std::ifstream page(it->second.c_str(), std::ios::binary);
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
