/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigParser.hpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lellanos <lellanos@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/27 13:25:56 by lellanos          #+#    #+#             */
/*   Updated: 2026/04/27 15:55:11 by lellanos         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef ConfigParser_hpp
#define ConfigParser_hpp

#include <fstream>
#include <string>
#include <iostream>
#include <cstring>
#include <sys/stat.h>



class ConfigParser
{
    private:
    
    public:
        ConfigParser();
        ~ConfigParser();
        int readFile(std::string path);  
};

#endif