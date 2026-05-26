/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   RequestLine.hpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/05 15:43:24 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/21 10:46:53 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
#include <string>
#include "RequestTarget.hpp"

class RequestLine
{
private:
    std::string _method;
    std::string _target;
    RequestTarget _parsed_target;
    std::string _version;
    std::string _authority;

public:
    void parse(const std::string& line);

    const std::string& method() const;
    const std::string& target() const;
    const std::string &path() const;
    const std::string &query() const;
    const std::string& version() const;
    const std::string& authority() const;
};
