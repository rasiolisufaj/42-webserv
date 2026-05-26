#include "config/ConfigParser.hpp"
#include "http/Request.hpp"
#include "utils/Logger.hpp"

int main(int ac, char **av)
{
    try
    {
        if (ac != 2)
            throw(std::runtime_error("Enter an argument and try again\n===BYE WEBSERV==="));
        std::cout << "\n===WebServ is running===\n\n";

        /**************/
        /*LENY'S PART*/
        /**************/
        std::cout << "\n+++CONFIG PARSER LOG+++\n";

        ConfigParser Parser;

        if (Parser.parseConfig(av[1]) != 0)
            return (EXIT_FAILURE);

        const std::vector<ServerConfig> &all_servers = Parser.getServers();

        for (size_t i = 0; i < all_servers.size(); i++)
        {
            all_servers[i].print_servers();
        }

        /***************/
        /*RASIOL'S PART*/
        /***************/

        //....


        /**************/
        /*THOMA'S PART*/
        /**************/
        std::cout << "\n+++REQUEST PARSER LOG+++\n";
        std::string good_requestTest = "POST /login HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello";
        std::string bad_requestTest = "POST /login HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 50\r\nConnection: close\r\n\rhello  ";
        
        /*TEST REQUEST*/
        Request rqs;
        rqs.parse(good_requestTest);

        RequestLine rl;
        rl = rqs.requestLine();
        std::cout << rl.target() << "\n> ";            //   => "/login"
        std::cout << rl.version() << "\n> ";           //   => "HTTP/1.1"

        Headers hd;
        hd = rqs.headers();
        std::cout << hd.getHeader("host") << "\n> ";   //   => "localhost:8080"
        std::cout << hd.getHost().name << "\n> ";
        std::cout << hd.getHost().port << "\n> ";
        std::cout << hd.getContentLength() << "\n> ";   //   => 5
        std::cout << hd.wantsClose() << "\n> ";         //   => true

        std::cout << rqs.body() << '\n'; // => "hello"

            
        /*
        rl.parse("GET /index.html HTTP/1.1"); //valide
        rl.parse("GET index.html HTTP/1.1"); //invalide
        
        hd.parseLine("Host: api.interactivtrading.com"); //valide
        hd.parseLine("Ho st: api.interactivtrading.com"); //invalide
        */

    }


    catch (std::runtime_error &e)
    {
        std::cout << "Error: " << e.what() << '\n';
        return (EXIT_FAILURE);
    }
    return (EXIT_SUCCESS);
}

// int main()
// {
//     Logger::log(Logger::INFO, "WebServ started successfully.");
//     Logger::log(Logger::DEBUG, "This is a debug message.");
//     Logger::log(Logger::WARNING, "This is a warning message.");
//     Logger::log(Logger::ERROR, "This is an error message.");

//     int fd = 42;
//     double timeout = 3.14;

//     Logger::log(Logger::INFO,
//         "fd = " + to_string(fd));

//     Logger::log(Logger::INFO,
//         "timeout = " + to_string(timeout));

//     std::cout << "\n=== Changing log level to WARNING ===" << std::endl;

//     Logger::setLogLevel(Logger::WARNING);
//     Logger::log(Logger::INFO, "This INFO message should NOT appear.");
//     Logger::log(Logger::DEBUG, "This DEBUG message should NOT appear.");
//     Logger::log(Logger::WARNING, "This WARNING message should appear.");
//     Logger::log(Logger::ERROR, "This ERROR message should appear.");
// }

//e.g context using
/*
#include "http/Request.hpp"
#include "http/RequestContext.hpp"
#include <iostream>

int main(void)
{
    Request request;
    RequestContext ctx;

    std::string raw =
        "GET /index.html?x=1 HTTP/1.1\r\n"
        "Host: site-a.com:8080\r\n"
        "User-Agent: curl/8.5.0\r\n"
        "\r\n";

    request.parse(raw);

    // e.g : port réellement touché par la socket
    ctx.build(request, 8080);

    std::cout << "local_port: " << ctx.localPort() << "\n";
    std::cout << "host name: " << ctx.effectiveHost().name << "\n";
    std::cout << "host port: " << ctx.effectiveHost().port << "\n";
    std::cout << "method: " << ctx.method() << "\n";
    std::cout << "target: " << ctx.target() << "\n";
    std::cout << "path: " << ctx.path() << "\n";
    std::cout << "query: " << ctx.query() << "\n";
    std::cout << "body size: " << ctx.bodySize() << "\n";

    if (ctx.headers().hasHeader("user-agent"))
        std::cout << "user-agent: " << ctx.headers().getHeader("user-agent") << "\n";

    return (0);
}

*/
