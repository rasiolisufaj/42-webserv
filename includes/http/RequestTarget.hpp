/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   RequestTarget.hpp                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 10:29:19 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/21 10:40:13 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
#include <string>

class RequestTarget
{

private:
    std::string _path;
    std::string _query;

public:
    void parse(const std::string &target);
    const std::string &path() const;
    const std::string &query() const;
};