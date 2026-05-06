/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   RequestLine.hpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@learner.42.tech>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/05 15:43:24 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/05 15:43:27 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
#include <string>

class RequestLine
{
private:
    std::string _method;
    std::string _target;
    std::string _version;

public:
    void parse(const std::string& line);

    const std::string& method() const;
    const std::string& target() const;
    const std::string& version() const;
};
