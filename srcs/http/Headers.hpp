/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Headers.hpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@learner.42.tech>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/06 13:05:33 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/06 13:05:36 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
#include <map>
#include <string>

class Headers
{
 private:
    std::map<std::string, std::string> _fields;

public:
    void        parseLine(const std::string& line);
    bool        hasHeader(const std::string& name) const;
    std::string getHeader(const std::string& name) const;
    size_t      getContentLength() const;
    bool        wantsClose() const;
};

