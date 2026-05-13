/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   RequestLine.hpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: kobe <kobe@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/05 15:43:24 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/13 13:48:47 by kobe             ###   ########.fr       */
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
    std::string _authority;

public:
    void parse(const std::string& line);

    const std::string& method() const;
    const std::string& target() const;
    const std::string& version() const;
    const std::string& authority() const;
};
