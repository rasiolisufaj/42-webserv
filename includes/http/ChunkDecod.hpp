/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ChunkDecod.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/19 12:55:07 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/20 10:44:47 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
#include <cstddef>
#include <string>

/*
    ===Reference: RFC 2616 §3.6.1 + §19.4.6===

    normal(with no extension):
        5\r\n     => hexa (length of next line)
        hello\r\n
        0\r\n     => 0+CRLF end of the body part
        \r\n

    choice: no extension + no trailers headers
        5;foo=bar\r\n
        hello\r\n
*/

class ChunkDecoder
{
private:
    static const size_t DEFAULT_MAX_BODY = 100 * 1024 * 1024; // 100 MB

    size_t _max_body;

    static std::string read_line(const std::string &buf, size_t &pos);
    static size_t parse_chunk_size(const std::string &line);
    static std::string read_chunk_data(const std::string &buf, size_t &pos, size_t size);
    static void skip_trailers(const std::string &buf, size_t &pos);

public:
    ChunkDecoder();
    ChunkDecoder(size_t max_body);
    std::string clean_body_chunk(const std::string &chunked_body) const;
};