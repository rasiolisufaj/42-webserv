/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   RequestContext.hpp                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/26 10:12:28 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/26 10:12:47 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once

#include "http/Request.hpp"
#include "http/Headers.hpp"
#include <cstddef>
#include <string>

class RequestContext
{
private:
    const Request *_request;

    int _local_port;
    HostInfo _effective_host;
    std::string _method;
    std::string _target;
    std::string _path;
    std::string _query;
    size_t _body_size;
    bool _use_authority;

public:
    RequestContext();

    void build(const Request &request, int local_port);

    int localPort() const;
    const HostInfo &effectiveHost() const;
    const std::string &method() const;
    const std::string &target() const;
    const std::string &path() const;
    const std::string &query() const;
    size_t bodySize() const;
    bool useAuthority() const;

    const Request &request() const;
    const Headers &headers() const;
    const std::string &body() const;
};