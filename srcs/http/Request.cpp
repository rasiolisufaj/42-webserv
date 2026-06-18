/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Request.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/19 12:55:11 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/21 12:15:15 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "http/Request.hpp"
#include "http/ChunkDecod.hpp"
#include "http/utils_http.hpp"
#include <cstddef>
#include <exception>
#include <stdexcept>

static void validate_content_length(const std::string &body_candidate, size_t content_length)
{
    if (body_candidate.size() < content_length)
        throw(std::runtime_error("400 Bad Request: incomplete body"));
}

// split request to: request line / headers / body
void Request::parse(const std::string &fullRequest)
{
    // RFC 2616 §4.1: request = request-line + headers + empty line + optional body

    std::string head;
    std::string body_candidate;
    std::string request_line;
    std::string headers_part;
    std::string double_CRLF;
    std::string single_CRLF;
    size_t header_end;
    size_t request_line_end;

    single_CRLF = "\r\n";
    double_CRLF = "\r\n\r\n";

    // split head and body with \r\n\r\n
    header_end = fullRequest.find(double_CRLF);
    if (header_end == std::string::npos)
        throw(std::runtime_error("400 Bad Request: invalid request"));

    head = fullRequest.substr(0, header_end);
    body_candidate = fullRequest.substr(header_end + double_CRLF.size());

    // HEADERS
    //  split headers and request line (RL = first \r\n)
    request_line_end = head.find(single_CRLF);
    if (request_line_end == std::string::npos)
    {
        request_line = head;
        headers_part = "";
    }
    else
    {
        request_line = head.substr(0, request_line_end);
        headers_part = head.substr(request_line_end + single_CRLF.size());
    }

    _requestLine.parse(request_line);

    // split and stock headers (via parsing_)
    size_t start;
    size_t end;
    std::string line;

    start = 0;
    while (start < headers_part.size())
    {
        end = headers_part.find(single_CRLF, start);
        if (end == std::string::npos)
        {
            line = headers_part.substr(start);
            start = headers_part.size();
        }
        else
        {
            line = headers_part.substr(start, end - start);
            start = end + single_CRLF.size();
        }

        if (!line.empty())
            _headers.parseLine(line);
    }

    // RFC 2616 §5.2 The Resource Identified by a Request
    if (_requestLine.version() == "HTTP/1.1" && !_headers.hasHeader("host"))
        throw(std::runtime_error("400 Bad Request: missing Host header"));

    // BODY
    bool has_transf_encoding = _headers.hasHeader("transfer-encoding");
    bool has_cont_len = _headers.hasHeader("content-length");

    if (has_transf_encoding && has_cont_len)
        throw(std::runtime_error("400 Bad Request: both Transfer-Encoding and Content-Length"));

    if (has_transf_encoding)
    {
        // only chunked is supported.
        std::string te = to_lower_ascii(_headers.getHeader("transfer-encoding"));
        if (te != "chunked")
            throw(std::runtime_error("501 Not Implemented: unsupported Transfer-Encoding (only chunk)"));

        ChunkDecoder decoder;
        _body = decoder.clean_body_chunk(body_candidate);
    }
    else
    {
        // Standard Content-Length/body
        size_t content_length = _headers.getContentLength();
        // TODO: récupérer max_body depuis la config plutôt qu'en dur
        if (content_length > 100 * 1024 * 1024)
            throw(std::runtime_error("413 Payload Too Large"));
        //TODO: non-keep-alive => OK maiis keep-alive => KO
        validate_content_length(body_candidate, content_length);
        _body = body_candidate.substr(0, content_length);

    }
}

const RequestLine &Request::requestLine() const { return (_requestLine); }
const Headers &Request::headers() const { return (_headers); }
const std::string &Request::body() const { return (_body); }
