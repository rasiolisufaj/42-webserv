# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: thodavid <thodavid@student.42.fr>          +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2026/05/05 15:49:06 by thodavid          #+#    #+#              #
#    Updated: 2026/06/02 13:52:42 by thodavid         ###   ########.fr        #
#                                                                              #
# **************************************************************************** #


#####
# Var
#####

NAME		= webserv
CXX			= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98 -MMD -MP

SRCDIR		= srcs
OBJDIR		= obj
INCLUDES	= -Iincludes

#####
# Src
#####

SRC			=	main \
				http/RequestLine \
				http/Headers \
				http/Request \
				http/Response \
				http/ChunkDecod \
				http/RequestTarget \
				http/utils_http \
				http/RequestContext \
				config/ConfigParser \
				config/ServerConfig \
				cgi/Cgi \
				server/Connection \
				server/EpollWrapper \
				server/Server \
				server/SessionManager \
				server/Socket \
				utils/Logger \

SRCS		= $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(SRC)))
OBJ			= $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))
DEP			= $(OBJ:.o=.d)

#######
# Rules
#######

all: $(NAME)

$(NAME): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $(NAME)
	@echo "✅ ready ✅"

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

-include $(DEP)

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

test: $(NAME)
	@c++ $(CXXFLAGS) $(INCLUDES) tests/test_main.cpp $(filter-out obj/main.o,$(OBJ)) -o tests/test_main
	@echo "Running tests..."
	@./tests/test_main

.PHONY: all clean fclean re test