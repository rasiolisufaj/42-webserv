/*

42 HEADER

*/

#include "Request.hpp"
#include <cstddef>
#include <exception>
#include <stdexcept>

// split request to: request line / headers / body
void Request::parse(const std::string &fullRequest)
{
    std::string single_CRLF;
    std::string double_CRLF;
    std::string head;
    std::string request_line;
    std::string headers_part;
    size_t      header_end;
    size_t      request_line_end;


    //TODO: for robustness body = content-len
    //rfc 4.1
    single_CRLF = "\r\n";
    double_CRLF = "\r\n\r\n";

    //split head and body with \r\n\r\n
    header_end = fullRequest.find(double_CRLF);
    if (header_end == std::string::npos)
        throw(std::runtime_error("400 Bad Request: invalid request"));

    head = fullRequest.substr(0, header_end);
    _body = fullRequest.substr(header_end + double_CRLF.size());

    //split headers and request line (RL = first \r\n)
    request_line_end = head.find(single_CRLF);
    if (request_line_end == std::string::npos){
        request_line = head;
        headers_part = "";
    }
    else{
        request_line = head.substr(0, request_line_end);
        headers_part = head.substr(request_line_end + single_CRLF.size());
    }

    _requestLine.parse(request_line);

    //split and stock headers
    size_t      start;
    size_t      end;
    std::string line;

    start = 0;
    while (start < headers_part.size())
    {
        end = headers_part.find(single_CRLF, start);
        if (end == std::string::npos){
            line = headers_part.substr(start);
            start = headers_part.size();
        }
        else{
            line = headers_part.substr(start, end - start);
            start = end + single_CRLF.size();
        }

        if (!line.empty())
            _headers.parseLine(line);
    }

    if (_requestLine.version() == "HTTP/1.1" && !_headers.hasHeader("host"))
        throw(std::runtime_error("400 Bad Request: missing Host header"));
}

const RequestLine   &Request::requestLine() const{ return (_requestLine); } 
const Headers       &Request::headers() const{ return (_headers); }
const std::string   &Request::body() const{ return (_body); }
