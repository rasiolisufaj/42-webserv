/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigMatcher.cpp                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/01 11:54:20 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/01 14:31:12 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "http/ConfigMatcher.hpp"
#include <stdexcept>

/*

Cette requête correspond à quel server block et quelle location
RequestContext + vector<ServerConfig>
=> RouteMatch { server, location }
*/

bool ConfigMatcher::serverNameMatch(const RequestContext &ctx,
                                    const ServerConfig &server) const
{
    if (ctx.effectiveHost().name == server.server_name)
        return (true);
    else
        return (false);
}

bool ConfigMatcher::portMatch(const RequestContext &ctx,
                              const ServerConfig &server) const
{
    if (ctx.localPort() == server.port)
        return (true);
    return (false);
}

bool ConfigMatcher::locationMatch(const std::string &request_path,
                                  const std::string &location_path) const
{
    size_t i;

    if (location_path.empty() || request_path.empty())
        return (false);
    if (location_path.size() > request_path.size())
        return (false);

    i = 0;
    while (i < location_path.size())
    {
        if (request_path[i] != location_path[i])
            return (false);
        i++;
    }

    if (location_path == "/")
        return (true);
    if (request_path.size() == location_path.size())
        return (true);
    if (request_path[location_path.size()] == '/')
        return (true);

    return (false);
}

const ServerConfig *ConfigMatcher::findServer(const RequestContext &ctx,
                                              const std::vector<ServerConfig> &servers) const
{
    const ServerConfig *server_candidate;
    size_t i;

    server_candidate = NULL;
    i = 0;
    while (i < servers.size())
    {
        if (portMatch(ctx, servers[i]))
        {
            if (serverNameMatch(ctx, servers[i]))
                return (&servers[i]);

            if (server_candidate == NULL)
                server_candidate = &servers[i];
        }
        i++;
    }

    if (server_candidate != NULL)
        return (server_candidate);

    throw(std::runtime_error("400 Bad Request: no matching server"));
}

const Location *ConfigMatcher::findLocation(const RequestContext &ctx,
                                            const ServerConfig &server) const
{
    const Location *best;
    size_t best_len;
    size_t i;

    best = NULL;
    best_len = 0;
    i = 0;
    while (i < server.locations.size())
    {
        if (locationMatch(ctx.path(), server.locations[i].path))
        {
            if (server.locations[i].path.size() > best_len)
            {
                best = &server.locations[i];
                best_len = server.locations[i].path.size();
            }
        }
        i++;
    }

    if (best == NULL)
        throw(std::runtime_error("404 Not Found: no matching location"));

    return (best);
}

RouteMatch ConfigMatcher::match(const RequestContext &ctx,
                                const std::vector<ServerConfig> &servers) const
{
    RouteMatch match;

    match.server = findServer(ctx, servers);
    match.location = findLocation(ctx, *match.server);

    return (match);
}