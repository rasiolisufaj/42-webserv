/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ResponseBuilder.cpp                                :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/02 14:07:04 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/07 15:12:59 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */


#include "http/ResponseBuilder.hpp"
#include "http/HttpStatus.hpp"
#include <stdexcept>

ResponseBuilder::ResponseBuilder() {}
ResponseBuilder::~ResponseBuilder() {}

static void apply_head(Response &r)
{
    std::string content_length;

    if (r.hasHeader("Content-Length"))
        content_length = r.getHeader("Content-Length");

    r.setBody("");
    if (!content_length.empty())
        r.setHeader("Content-Length", content_length);
}


std::string ResponseBuilder::mapPath(const RequestContext &ctx, const RouteContext &route) const
{
    std::string root;

    root = route.root;
    if (root.size() > 1 && root[root.size() - 1] == '/')
        root.erase(root.size() - 1); // evite "root//path"

    if (ctx.path().empty() || ctx.path()[0] != '/')
        throw (std::runtime_error("500 Internal Server Error: non-canonical path"));

    return (root + ctx.path());
}

//Handle Method
bool ResponseBuilder::isMethodAllowed(const std::string &method, const RouteContext &route) const
{
    size_t i;

    i = 0;
    while (i < route.allow_methods.size())
    {
        if (route.allow_methods[i] == method)
            return (true);
        i++;
    }
    return (false);
}

std::string ResponseBuilder::allowHeaderValue(const RouteContext &route) const
{
    std::string value;
    size_t i;

    i = 0;
    while (i < route.allow_methods.size())
    {
        if (!value.empty())
            value += ", ";
        value += route.allow_methods[i];
        i++;
    }
    return (value);
}

//ENGINE
Response ResponseBuilder::dispatch(const RequestContext &ctx, const RouteContext &route) const
{
    std::string file_path;

    if (route.return_code != -1) //in serverConfig.hpp(l.28) response in confFile
        return (handleReturn(route));

    if (!isMethodAllowed(ctx.method(), route))
    {
        Response r = errorResponse(405, route);
        r.setHeader("Allow", allowHeaderValue(route)); // RFC: list all Methods Allow obligatoir sur 405
        return (r);
    }

    if (ctx.bodySize() > route.client_max_body_size)
        return (errorResponse(413, route));

    file_path = mapPath(ctx, route);

    if (ctx.method() == "GET" || ctx.method() == "HEAD")
        return (handleGetHead(ctx, route, file_path));
    if (ctx.method() == "POST")
        return (handlePost(ctx, route));
    if (ctx.method() == "DELETE")
        return (handleDelete(route, file_path));

    return (errorResponse(501, route));
}

Response ResponseBuilder::build(const RequestContext &ctx, const RouteContext &route) const
{
    Response r;

    try
    {
        r = dispatch(ctx, route);
    }
    catch (const std::exception &e)
    {
        // toute exception interne (invariant viole, I/O...) est convertie
        // ici pour beneficier des error_page du config
        r = errorResponse(status_from_exception(e.what()), route);
    }

    if (ctx.method() == "HEAD")
        apply_head(r);

    return (r);
}