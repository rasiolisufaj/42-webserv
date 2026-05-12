/*

HEADER

*/

#pragma once
#include "Headers.hpp"
#include "RequestLine.hpp"
#include <string>

class Request
{
private:
    RequestLine _requestLine;
    Headers     _headers;
    std::string _body;

public:
    void parse(const std::string &fullRequest);

    const RequestLine   &requestLine() const;
    const Headers       &headers() const;
    const std::string   &body() const;
};