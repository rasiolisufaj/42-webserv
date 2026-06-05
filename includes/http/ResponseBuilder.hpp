/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ResponseBuilder.hpp                                :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/02 14:07:13 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/05 14:18:17 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
 
#include "http/RequestContext.hpp"
#include "http/Response.hpp"
#include "config/RouteContext.hpp"
#include <string>
 
/*
    ResponseBuilder: RequestContext + RouteContext => Response (statique only)
 
    Une classe, un contrat (ce header), mais une implementation decoupee
    par responsabilite — un fichier = un type d'effet de bord:
 
        ResponseBuilder.cpp       decision pure (l'arbre), aucun I/O
        ResponseBuilderGet.cpp    LECTURE filesystem (GET/HEAD, autoindex)
        ResponseBuilderWrite.cpp  ECRITURE filesystem (POST, DELETE)
        ResponseBuilderError.cpp  fabrication des reponses erreur/return
 
    Modules purs associes: HttpStatus (codes), MimeTypes (Content-Type).
 
    Ordre de decision (inspire des phases nginx):
        1. return (directive de config, gagne toujours)
        2. 405 methode non autorisee (+ header Allow)
        3. 413 body trop gros
        4. mapping root + path (semantique "root" nginx)
        5. dispatch GET/HEAD | POST | DELETE
 
    Frontiere CGI: detectee AVANT d'appeler build() (dispatcher serveur).
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
    Response handlePost(const RequestContext &ctx, const RouteContext &route) const;
    Response handleDelete(const RouteContext &route, const std::string &file_path) const;
 
    // --- ResponseBuilderError.cpp : erreurs & return ---
    Response handleReturn(const RouteContext &route) const;
    Response errorResponse(int code, const RouteContext &route) const;
 
public:
    ResponseBuilder();
    ~ResponseBuilder();
 
    Response build(const RequestContext &ctx, const RouteContext &route) const;
};
