/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ChunkDecod.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/19 12:52:28 by kobe              #+#    #+#             */
/*   Updated: 2026/05/21 10:40:26 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "http/ChunkDecod.hpp"
#include "http/utils_http.hpp"
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

ChunkDecoder::ChunkDecoder() : _max_body(DEFAULT_MAX_BODY) {}
ChunkDecoder::ChunkDecoder(size_t max_body) : _max_body(max_body) {}

std::string ChunkDecoder::read_line(const std::string &buf, size_t &pos)
{
    size_t      crlf_pos;
    std::string line;

    crlf_pos = buf.find("\r\n", pos);
    if (crlf_pos == std::string::npos)
        throw(std::runtime_error("400 Bad Request: missing CRLF"));

    line = buf.substr(pos, crlf_pos - pos);
    pos = crlf_pos + 2;
    return (line);
}

size_t ChunkDecoder::parse_chunk_size(const std::string &line)
{
    size_t i;
    size_t result;
    size_t max;
    int    digit;

    if (line.empty())
        throw(std::runtime_error("400 Bad Request: empty chunk-size"));

    result = 0;
    max = std::numeric_limits<size_t>::max();
    i = 0;
    while (i < line.size())
    {
        // chunk-extension -> we ignore everything from ';' 
        if (line[i] == ';')
            break;

        digit = hex_value(line[i]);
        if (digit < 0)
            throw(std::runtime_error("400 Bad Request: invalid hex in chunk-size"));

        // overflow check before "result * 16 + digit"
        if (result > (max - static_cast<size_t>(digit)) / 16)
            throw(std::runtime_error("400 Bad Request: chunk-size overflow"));

        result = result * 16 + static_cast<size_t>(digit);
        i++;
    }

    if (i == 0)
        throw(std::runtime_error("400 Bad Request: chunk-size missing hex digits"));

    return (result);
}

std::string ChunkDecoder::read_chunk_data(const std::string &buf, size_t &pos, size_t size)
{
    std::string data;

    if (pos + size > buf.size())
        throw(std::runtime_error("400 Bad Request: incomplete chunk-data"));

    data = buf.substr(pos, size);
    pos += size;

    // chunk-data MUST be followed by CRLF
    if (pos + 2 > buf.size())
        throw(std::runtime_error("400 Bad Request: missing CRLF after chunk-data"));
    if (buf[pos] != '\r' || buf[pos + 1] != '\n')
        throw(std::runtime_error("400 Bad Request: invalid CRLF after chunk-data"));
    pos += 2;

    return (data);
}

void ChunkDecoder::skip_trailers(const std::string &buf, size_t &pos)
{
    std::string line;

    while (pos < buf.size())
    {
        line = read_line(buf, pos);
        // empty line => end of trailers => end of chunked body
        if (line.empty())
            return;
        // non-empty: it's a trailer header — ignore and continue
    }
    throw(std::runtime_error("400 Bad Request: missing final CRLF after last-chunk"));
}

std::string ChunkDecoder::clean_body_chunk(const std::string &chunk_body) const
{
    std::string result;
    std::string size_line;
    size_t      pos;
    size_t      chunk_size;

    pos = 0;
    while (pos < chunk_body.size())
    {
        size_line = read_line(chunk_body, pos);
        chunk_size = parse_chunk_size(size_line);

        if (chunk_size == 0)
        {
            skip_trailers(chunk_body, pos);
            return (result);
        }
        if (result.size() > _max_body - chunk_size)
            throw(std::runtime_error("413 Payload Too Large: chunked body exceeds limit"));

        result += read_chunk_data(chunk_body, pos, chunk_size);
    }

    throw(std::runtime_error("400 Bad Request: chunked body missing last-chunk"));
}