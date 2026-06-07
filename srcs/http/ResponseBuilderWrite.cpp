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
    Upload statique (le CGI est intercepte avant, par le dispatcher).
    Nom de fichier = dernier segment du path. SECURITE: ce nom est sain
    PARCE QUE le path est canonique (pas de "..", pas de '\0', et le
    dernier segment ne contient pas de '/').
*/
Response ResponseBuilder::handlePost(const RequestContext &ctx, const RouteContext &route) const
{
    std::string filename;
    std::string store;
    std::string dest;
    struct stat st;
    bool existed;
 
    if (ctx.path()[ctx.path().size() - 1] == '/')
        return (errorResponse(403, route)); // pas d'upload "vers un repertoire"
 
    filename = ctx.path().substr(ctx.path().find_last_of('/') + 1);
 
    store = route.upload_store;
    if (store[store.size() - 1] != '/')
        store += "/";
    dest = store + filename;
 
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
 
Response ResponseBuilder::handleDelete(const RouteContext &route, const std::string &file_path) const
{
    struct stat st;
 
    if (stat(file_path.c_str(), &st) != 0)
        return (errorResponse(404, route));
 
    if (S_ISDIR(st.st_mode))
        return (errorResponse(403, route)); // pas de suppression de repertoires
 
    if (unlink(file_path.c_str()) != 0)
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
