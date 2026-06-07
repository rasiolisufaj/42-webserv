/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/07 12:08:17 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/07 12:42:12 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "config/ConfigParser.hpp"
#include "config/ConfigMatcher.hpp"
#include "http/Request.hpp"
#include "http/RequestContext.hpp"
#include "http/Response.hpp"
#include "http/ResponseBuilder.hpp"
#include "server/Server.hpp"
#include <fcntl.h>
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
        // ---         parsing CONFIGFilE         ---
        std::cout << "\n+++CONFIG PARSER CONFFIle+++\n";

        ConfigParser Parser;

        if (Parser.parseConfig(av[1]) != 0)
            return (EXIT_FAILURE);

        const std::vector<ServerConfig> &all_servers = Parser.getServers();
        // for (size_t i = 0; i < all_servers.size(); i++)
        //     all_servers[i].print_servers();
        // const std::vector<ServerConfig> &all_servers = Parser.getServers();
        // const ServerConfig &my_server = all_servers[0];
        // const Location &loc = my_server.locations[0];
        std::cout << "PARSING CONFIG ✅\n";
        /*--------------------------------------------------------------------*/

        /*--------------------------------------------------------------------*/
        // ---         parsing RQUEST         ---
        std::cout << "\n+++PARSING REQUEST+++\n";
        Request request;
        RequestContext ctx;
        std::string raw_get =
            "GET /cgi-bin/cgi_post.py?Name=Leny&proyect=Webserv HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "User-Agent: curl/8.5.0\r\n"
            "\r\n";

        request.parse(raw_get);
        // ctx.build(request, conn->server_cfg->port);   <===========!!!!
        ctx.build(request, 8080);
        std::cout << "PARSING REQUEST ✅\n";
        
        /*--------------------------------------------------------------------*/

        /*--------------------------------------------------------------------*/
        // ---         RQST vs CONFIG         ---
        std::cout << "\n+++RQST   MATCH  SERVER+++\n";
        ConfigMatcher confMatch;
        RouteMatch routeMatch;

        routeMatch = confMatch.match(ctx, all_servers);
        std::cout << "Request-Server matching : " << routeMatch.server->server_name << '\n';
        //std::cout << "Location matching : " << routeMatch.location.path << '\n';
        std::cout << "MATCHING REQUEST-CONF ✅\n";

        
        /*--------------------------------------------------------------------*/
        // ---         RESPONSE         ---
        std::cout << "\n+++TEST FOR RESPONSE+++\n";

        Response response(404, "Not Found bruuh"); //CTOR with no param = 200 OK by default

        std::cout << "Staus ligne Pattern✅:\n00000";
        std::cout << response.toString();
        std::cout << "\n00000\n";

        /*--------------------------------------------------------------------*/
        // ---         RESP BUILD         ---
        ResponseBuilder rspBuild;
        //Response Realresponse = rspBuild.build(ctx, ROUTE  CTX);
        
        
        /*--------------------------------------------------------------------*/
        // ---         SERVER ENGINE         ---
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
