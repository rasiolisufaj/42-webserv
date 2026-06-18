/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Request.hpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/19 12:55:44 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/19 12:55:48 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
#include "Headers.hpp"
#include "RequestLine.hpp"
#include <string>

class Request
{
private:
    RequestLine _requestLine;
    Headers     _headers;
    std::string _body;

public:
    void parse(const std::string &fullRequest);

    const RequestLine   &requestLine() const;
    const Headers       &headers() const;
    const std::string   &body() const;
};