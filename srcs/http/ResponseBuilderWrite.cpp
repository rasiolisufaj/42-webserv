/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ResponseBuilderWrite.cpp                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/05 14:23:27 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/05 14:27:46 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

/*
    ECRITURE filesystem — les methodes qui MODIFIENT l'etat du serveur:
    POST (upload dans upload_store) et DELETE (unlink).
    Seul fichier du builder qui ecrit/supprime sur le disque
*/
 
#include "http/ResponseBuilder.hpp"
#include "http/HttpStatus.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <fstream>
 
/*
    Cible commune POST/DELETE: upload_store + dernier segment du path.
    Le nom de fichier est sain PARCE QUE le path est canonique (pas de "..",
    pas de '\0', et le dernier segment ne contient pas de '/'). POST et DELETE
    passent par ici pour viser EXACTEMENT le meme fichier (un fichier uploade
    via POST /x est supprimable via DELETE /x).
*/
std::string ResponseBuilder::uploadTargetPath(const RequestContext &ctx, const RouteContext &route) const
{
    std::string store;
    std::string filename;

    store = route.upload_store;
    if (store.empty() || store[store.size() - 1] != '/')
        store += "/";
    filename = ctx.path().substr(ctx.path().find_last_of('/') + 1);
    return (store + filename);
}

/*
    Upload statique (le CGI est intercepte avant, par le dispatcher).
*/
Response ResponseBuilder::handlePost(const RequestContext &ctx, const RouteContext &route) const
{
    std::string filename;
    std::string dest;
    struct stat st;
    bool existed;

    if (ctx.path()[ctx.path().size() - 1] == '/')
        return (errorResponse(403, route)); // pas d'upload "vers un repertoire"

    filename = ctx.path().substr(ctx.path().find_last_of('/') + 1);
    dest = uploadTargetPath(ctx, route);

    existed = (stat(dest.c_str(), &st) == 0);
    if (existed && S_ISDIR(st.st_mode))
        return (errorResponse(403, route));
 
    std::ofstream out(dest.c_str(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return (errorResponse(500, route));
 
    out << ctx.body();
    bool write_ok = out.good();
    out.close();
    if (!write_ok)
        return (errorResponse(500, route));
 
    Response r;
    r.setHeader("Content-Type", "text/plain");
    if (existed)
    {
        r.setStatus(200, reason_phrase(200)); // ecrase: 200, cree: 201
        r.setBody("File updated: " + filename + "\n");
    }
    else
    {
        r.setStatus(201, reason_phrase(201));
        r.setHeader("Location", ctx.path()); // RFC 7231: Location sur 201
        r.setBody("File created: " + filename + "\n");
    }
    return (r);
}
 
Response ResponseBuilder::handleDelete(const RequestContext &ctx, const RouteContext &route) const
{
    std::string target;
    struct stat st;

    // Meme cible que POST: on supprime depuis upload_store, pas depuis root,
    // sinon un fichier uploade ne serait jamais supprimable.
    if (ctx.path()[ctx.path().size() - 1] == '/')
        return (errorResponse(403, route)); // pas de suppression "d'un repertoire"

    target = uploadTargetPath(ctx, route);

    if (stat(target.c_str(), &st) != 0)
        return (errorResponse(404, route));

    if (S_ISDIR(st.st_mode))
        return (errorResponse(403, route)); // pas de suppression de repertoires

    if (unlink(target.c_str()) != 0)
    {
        if (errno == EACCES || errno == EPERM)
            return (errorResponse(403, route));
        return (errorResponse(500, route));
    }
 
    // 204: par definition pas de body, et pas de Content-Length
    // (le framing est implicite: il n'y a rien a lire)
    Response r(204, reason_phrase(204));
    return (r);
}
