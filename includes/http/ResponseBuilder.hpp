/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ResponseBuilder.hpp                                :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/02 14:07:13 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/07 11:25:22 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
 
#include "http/RequestContext.hpp"
#include "http/Response.hpp"
#include "config/RouteContext.hpp"
#include <string>

/*
    Builds an error Response: serves the server's custom error_page (resolved
    against the server root) when present and readable, otherwise a generated
    default page. Shared by ResponseBuilder's internal error path AND by the
    server's pre-routing error path (parse / routing failures), so both honor
    error_page and the real status code. NULL server => generated page only.
*/
Response make_error_response(int code, const ServerConfig *server);
 
/*
    ResponseBuilder: RequestContext + RouteContext => Response
 
        ResponseBuilder.cpp       decision pure (l'arbre), aucun I/O
        ResponseBuilderGet.cpp    LECTURE filesystem (GET/HEAD, autoindex)
        ResponseBuilderWrite.cpp  ECRITURE filesystem (POST, DELETE)
        ResponseBuilderError.cpp  fabrication des reponses erreur/return
 
    HttpStatus (codes), MimeTypes (Content-Type).
 
    Ordre de decision (nginx):
        1. return (directive de config, gagne toujours)
        2. 405 methode non autorisee (+ header Allow)
        3. 413 body trop gros
        4. mapping root + path (semantique "root" nginx)
        5. dispatch GET/HEAD | POST | DELETE
 
    Frontiere CGI: detectee AVANT d'appeler build() (dispatcher serveur)
*/
 
class ResponseBuilder
{
private:
    // --- ResponseBuilder.cpp : decision pure ---
    Response dispatch(const RequestContext &ctx, const RouteContext &route) const;
    std::string mapPath(const RequestContext &ctx, const RouteContext &route) const;
    bool isMethodAllowed(const std::string &method, const RouteContext &route) const;
    std::string allowHeaderValue(const RouteContext &route) const;
 
    // --- ResponseBuilderGet.cpp : lecture filesystem ---
    Response handleGetHead(const RequestContext &ctx, const RouteContext &route,
                           const std::string &file_path) const;
    Response serveFile(const std::string &file_path, const RouteContext &route) const;
    Response autoindexResponse(const RequestContext &ctx, const RouteContext &route,
                               const std::string &dir_path) const;
    Response redirectSlash(const RequestContext &ctx) const;
 
    // --- ResponseBuilderWrite.cpp : ecriture filesystem ---
    // Resout la cible d'un upload/suppression: upload_store + basename(path).
    // Partage par POST et DELETE pour qu'ils ne divergent jamais.
    std::string uploadTargetPath(const RequestContext &ctx, const RouteContext &route) const;
    Response handlePost(const RequestContext &ctx, const RouteContext &route) const;
    Response handleDelete(const RequestContext &ctx, const RouteContext &route) const;
 
    // --- ResponseBuilderError.cpp : erreurs & return ---
    Response handleReturn(const RouteContext &route) const;
    Response errorResponse(int code, const RouteContext &route) const;
 
public:
    ResponseBuilder();
    ~ResponseBuilder();
 
    Response build(const RequestContext &ctx, const RouteContext &route) const;
};
