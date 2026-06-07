/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpStatus.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/05 14:25:54 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/05 14:26:15 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
#include <string>
 
/*
    Module pur (aucun etat, aucun I/O): tout ce qui tourne autour
    d'un code de statut HTTP. Utilise par ResponseBuilder ET par le
    dispatcher serveur (erreurs d'avant-matching: parsing, no server...).
*/
 
// 404 -> "Not Found" (codes inconnus -> "Unknown")
std::string reason_phrase(int code);
 
// pont exceptions -> statut: "404 Not Found: ..." -> 404
// message non conforme (pas de code 100-599 en prefixe) -> 500
int status_from_exception(const char *what);
 
// page HTML par defaut pour un statut donne
std::string default_error_body(int code);
