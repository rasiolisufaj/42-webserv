/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   EffectiveRoute.cpp                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/02 14:35:44 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/02 15:51:17 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "config/EffectiveRoute.hpp"
#include <stdexcept>

EffectiveRouteBuilder::EffectiveRouteBuilder() {}
EffectiveRouteBuilder::~EffectiveRouteBuilder() {}

std::string EffectiveRouteBuilder::resolveString(const std::string &location_value,
                                                 const std::string &server_value) const
{
    if (!location_value.empty())
        return (location_value);
    return (server_value);
}

size_t EffectiveRouteBuilder::resolveSize(size_t location_value,
                                          size_t server_value) const
{
    if (location_value != 0)
        return (location_value);
    return (server_value);
}

/*
    README:
    allow_methods default: GET HEAD (if empty)
*/
std::vector<std::string> EffectiveRouteBuilder::resolveMethods(const std::vector<std::string> &location_methods) const
{
    std::vector<std::string> methods;

    if (!location_methods.empty())
        return (location_methods);

    methods.push_back("GET");
    methods.push_back("HEAD");

    return (methods);
}

EffectiveRoute EffectiveRouteBuilder::build(const RouteMatch &match) const
{
    EffectiveRoute route;

    if (match.server == NULL)
        throw(std::runtime_error("EffectiveRouteBuilder: NULL server"));
    if (match.location == NULL)
        throw(std::runtime_error("EffectiveRouteBuilder: NULL location"));

    route.server = match.server;
    route.location = match.location;

    route.root = resolveString(match.location->root, match.server->root);
    route.upload_store = resolveString(match.location->upload_store, match.server->upload_store);

    route.index = match.location->index;
    if (route.index.empty())
        route.index = "index.html";

    route.allow_methods = resolveMethods(match.location->allowed_methods);

    route.client_max_body_size = resolveSize(match.location->client_max_body_size,
                                             match.server->client_max_body_size);

    route.client_max_header_size = match.server->client_max_header_size;

    route.autoindex = match.location->autoindex;

    route.return_code = match.location->return_code;
    route.return_url = match.location->return_redir;

    route.cgi = match.location->cgi;

    if (route.root.empty())
        throw(std::runtime_error("500 Internal Server Error: empty effective root"));

    if (route.upload_store.empty())
        throw(std::runtime_error("500 Internal Server Error: empty effective upload_store"));

    return (route);
}