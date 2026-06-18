/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Response.hpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/02 13:08:06 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/02 13:09:16 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
#include <cstddef>
#include <map>
#include <string>

class Response
{
private:
    int _status_code;
    std::string _reason;
    std::map<std::string, std::string> _headers;
    std::string _body;

public:
    Response();
    Response(int status_code, const std::string &reason);
    ~Response();

    void setStatus(int status_code, const std::string &reason);
    void setHeader(const std::string &key, const std::string &value);
    void setBody(const std::string &body);

    int statusCode() const;
    const std::string &reason() const;
    const std::string &body() const;

    bool hasHeader(const std::string &key) const;
    std::string getHeader(const std::string &key) const;

    std::string toString() const;
};