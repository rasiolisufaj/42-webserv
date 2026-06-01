/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigMatcher.hpp                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/01 11:50:44 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/01 13:36:59 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
#include "http/RequestContext.hpp"
#include "config/ServerConfig.hpp"
#include <cstddef>
#include <string>
#include <vector>

struct RouteMatch
{
    const ServerConfig *server;
    const Location     *location;
};

class ConfigMatcher
{
public:
    RouteMatch match(const RequestContext &ctx,
                     const std::vector<ServerConfig> &servers) const;

private:
    const ServerConfig *findServer(const RequestContext &ctx,
                                   const std::vector<ServerConfig> &servers) const;

    const Location     *findLocation(const RequestContext &ctx,
                                     const ServerConfig &server) const;

    bool                serverNameMatch(const RequestContext &ctx,
                                        const ServerConfig &server) const;

    bool                portMatch(const RequestContext &ctx,
                                  const ServerConfig &server) const;

    bool                locationMatch(const std::string &request_path,
                                      const std::string &location_path) const;             
};