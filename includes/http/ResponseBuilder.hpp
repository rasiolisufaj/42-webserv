/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ResponseBuilder.hpp                                :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/02 14:07:13 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/04 15:19:21 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
 
#include "http/RequestContext.hpp"
#include "http/Response.hpp"
#include "config/EffectiveRoute.hpp"
#include <string>
 
/*
    ResponseBuilder: RequestContext + EffectiveRoute => Response (statique only)
 
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
    Response dispatch(const RequestContext &ctx, const EffectiveRoute &route) const;
    std::string mapPath(const RequestContext &ctx, const EffectiveRoute &route) const;
    bool isMethodAllowed(const std::string &method, const EffectiveRoute &route) const;
    std::string allowHeaderValue(const EffectiveRoute &route) const;
 
    // --- ResponseBuilderGet.cpp : lecture filesystem ---
    Response handleGetHead(const RequestContext &ctx, const EffectiveRoute &route,
                           const std::string &file_path) const;
    Response serveFile(const std::string &file_path, const EffectiveRoute &route) const;
    Response autoindexResponse(const RequestContext &ctx, const EffectiveRoute &route,
                               const std::string &dir_path) const;
    Response redirectSlash(const RequestContext &ctx) const;
 
    // --- ResponseBuilderWrite.cpp : ecriture filesystem ---
    Response handlePost(const RequestContext &ctx, const EffectiveRoute &route) const;
    Response handleDelete(const EffectiveRoute &route, const std::string &file_path) const;
 
    // --- ResponseBuilderError.cpp : erreurs & return ---
    Response handleReturn(const EffectiveRoute &route) const;
    Response errorResponse(int code, const EffectiveRoute &route) const;
 
public:
    ResponseBuilder();
    ~ResponseBuilder();
 
    Response build(const RequestContext &ctx, const EffectiveRoute &route) const;
};
