/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: raisufaj <raisufaj@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/07 12:08:17 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/15 18:19:40 by raisufaj         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "config/ConfigParser.hpp"
#include "server/Server.hpp"
#include "utils/Logger.hpp"
#include <csignal>
#include <cstdlib>
#include <iostream>

// Cleared by the signal handler so Server::run()'s event loop returns and the
// Server/Connection/Socket destructors run (clean shutdown, no leaked fds).
// Starts at 1 (running); the handler flips it to 0 (stop).
volatile sig_atomic_t g_running = 1;

static void signalHandler(int) { g_running = 0; }

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return EXIT_FAILURE;
    }

    // SIGINT/SIGTERM: flip g_running instead of terminating, so the loop unwinds cleanly. 
    // SIGPIPE: ignore, so a write to a dead CGI pipe returns EPIPE instead of killing the server.
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    try
    {
        ConfigParser parser;
        if (parser.parseConfig(argv[1]) != 0)
            return EXIT_FAILURE;

        Server server(parser.getServers());
        server.run();
    }
    catch (const std::exception &e)
    {
        Logger::log(Logger::ERROR, std::string("Fatal: ") + e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
