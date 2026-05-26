/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   utils_http.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/20 10:50:05 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/21 10:35:57 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "http/utils_http.hpp"

std::string to_lower_ascii(const std::string &s)
{
    std::string result;
    size_t i;
    char c;

    result = s;
    i = 0;
    while (i < result.size())
    {
        c = result[i];
        if (c >= 'A' && c <= 'Z')
            result[i] = c + 32;
        i++;
    }
    return (result);
}

int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return (c - '0');
    if (c >= 'a' && c <= 'f')
        return (c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
        return (c - 'A' + 10);
    return (-1);
}