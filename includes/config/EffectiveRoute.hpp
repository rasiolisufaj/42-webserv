/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   EffectiveRoute.hpp                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/02 14:30:32 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/02 14:39:03 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once

#include "config/ConfigMatcher.hpp"
#include <map>
#include <string>
#include <vector>
#include <cstddef>

struct EffectiveRoute
{
    const ServerConfig *server;
    const Location *location;

    std::string root;
    std::string upload_store;
    std::string index;
    std::vector<std::string> allow_methods;

    size_t client_max_body_size;
    size_t client_max_header_size;

    bool autoindex;

    int return_code;
    std::string return_url;

    std::map<std::string, std::string> cgi;
};

class EffectiveRouteBuilder
{
private:
    std::string resolveString(const std::string &location_value,
                              const std::string &server_value) const;

    size_t resolveSize(size_t location_value,
                       size_t server_value) const;

    std::vector<std::string> resolveMethods(const std::vector<std::string> &location_methods) const;

public:
    EffectiveRouteBuilder();
    ~EffectiveRouteBuilder();

    EffectiveRoute build(const RouteMatch &match) const;
};