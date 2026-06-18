#pragma once
#include <map>
#include <string>
#include <vector>

struct CgiJob
{
    std::string interpreter;
    std::string script_path;
    std::string working_dir;

    std::string method;
    std::string query_string;
    std::string url_path;

    std::vector<char>   body;
    std::map<std::string, std::string> env;

    /*
        * Default constructor initializes all members to empty values.
        * The http layer fills this struct with script paths, request metadata, and environment variables before passing it to the CGI handler.
    */
    CgiJob() : interpreter(), script_path(), working_dir(),
               method(), query_string(), url_path(),
               body(), env() {}
};
