/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@learner.42.tech>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/05 15:53:02 by thodavid          #+#    #+#             */
/*   Updated: 2026/05/05 15:53:05 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "./http/RequestLine.hpp"
#include "./http/Headers.hpp"
#include <stdexcept>
#include <iostream>
#include <cstdlib>


int main(int ac, char **av)
{
    (void)av;
    try
    {
        if (ac != 2)
            throw(std::runtime_error("Enter an argument and try again\n===BYE WEBSERV==="));
        std::cout << "===WebServ is running===\n";

        /*TEST REQUSTLINE PARSING*/
        RequestLine rl;
        rl.parse("GET /index.html HTTP/1.1"); //valide
        //rl.parse("GET index.html HTTP/1.1"); //invalide

        /*TEST REQUSTLINE PARSING*/
        Headers hd;
        hd.parseLine("Host: api.interactivtrading.com"); //valide
        //hd.parseLine("Ho st: api.interactivtrading.com"); //invalide
    }
    catch (std::runtime_error &e)
    {
        std::cout << "Error: " << e.what() << '\n';
        return (EXIT_FAILURE);
    }
    return (EXIT_SUCCESS);
}