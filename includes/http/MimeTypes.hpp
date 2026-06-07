/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   MimeTypes.hpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/05 14:42:25 by thodavid          #+#    #+#             */
/*   Updated: 2026/06/05 14:51:13 by thodavid         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once
#include <string>
 
/*
    Module pur: chemin de fichier -> Content-Type d'apres l'extension.
    Defaut: application/octet-stream (le navigateur telecharge au lieu
    d'interpreter — comportement sur pour un type inconnu)
*/
 
std::string mime_type(const std::string &path);
