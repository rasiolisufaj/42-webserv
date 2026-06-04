#include "config/ConfigParser.hpp"
#include "http/Request.hpp"
#include "http/Response.hpp"
#include "server/Server.hpp"
#include <fcntl.h>
#include "http/RequestContext.hpp"
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <csignal>
#include <stdio.h>

int main(int ac, char **av)
{
    try
    {
        if (ac != 2)
            throw(std::runtime_error("Enter an argument and try again\n===BYE WEBSERV==="));
        // Writing to a CGI stdin pipe whose child has exited would raise SIGPIPE
        // and kill the server; ignore it so write() returns EPIPE instead.
        std::signal(SIGPIPE, SIG_IGN);
        std::cout << "\n===WebServ is running===\n\n";
        /*--------------------------------------------------------------------*/

        /*
        parsing RQUEST
        */

        Request request;
        RequestContext ctx;
        std::string raw_get =
            "GET /cgi-bin/cgi_post.py?Name=Leny&proyect=Webserv HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "User-Agent: curl/8.5.0\r\n"
            "\r\n";

        std::cout << "\n+++CONFIG PARSER RQST+++\n";
        request.parse(raw_get);
        ctx.build(request, 8081);
        std::cout << "method: " << ctx.method() << "\n";
        std::cout << "target: " << ctx.target() << "\n";
        /*--------------------------------------------------------------------*/

        /*
        parsing CONFIGFilE
        */

        std::cout << "\n+++CONFIG PARSER CONFFIle+++\n";

        ConfigParser Parser;

        if (Parser.parseConfig(av[1]) != 0)
            return (EXIT_FAILURE);

        const std::vector<ServerConfig> &all_servers = Parser.getServers();
        for (size_t i = 0; i < all_servers.size(); i++)
        {
            std::cout << "call me Maybe\n";
            all_servers[i].print_servers();
        }
        // const std::vector<ServerConfig> &all_servers = Parser.getServers();
        // const ServerConfig &my_server = all_servers[0];
        // const Location &loc = my_server.locations[0];
        /*--------------------------------------------------------------------*/

        /*
        RQST vs CONFIG
        */

        /*
            ----> Which SERV
            ----> Which LOCATION
            ----> Check METHOD
            ----> Check
            ----> BUILD RESPONSE_STRUCT

            ConfigMatcher       -> trouve server/location
            ResponseBuilder     -> construit la réponse
            HttpHandler         -> orchestre parser + matcher + builder

            1. ConfigMatcher
            2. Response
            3. ResponseBuilder
            4. HttpHandler
        */


        /*--------------------------------------------------------------------*/


        /*
        Response
        */

        std::cout << "\n+++TEST FOR RESPONSE+++\n";

        Response response;

        response.setStatus(200, "OK");
        response.setHeader("Content-Type", "text/html");
        response.setHeader("Connection", "close");
        response.setBody("<html><body>Hello Webserv</body></html>\n");

        std::cout << response.toString();

        // ConfigMatcher

        /*--------------------------------------------------------------------*/

        /*
        SERVER ENGINE
        */

        std::cout << "\n+++SERVER CORE LOG+++\n";

        try
        {
            Server server(all_servers);
            server.run();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Server error: " << e.what() << '\n';
            return (EXIT_FAILURE);
        }
    }

    catch (std::runtime_error &e)
    {
        std::cout << "Error: " << e.what() << '\n';
        return (EXIT_FAILURE);
    }
    return (EXIT_SUCCESS);
}
