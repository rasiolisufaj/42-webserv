# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: thodavid <thodavid@learner.42.tech>        +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2026/05/05 15:49:06 by thodavid          #+#    #+#              #
#    Updated: 2026/05/05 15:49:09 by thodavid         ###   ########.fr        #
#                                                                              #
# **************************************************************************** #


#####
#Var#
#####
NAME		= webserv
FLAGS		= -Wall -Wextra -Werror -std=c++98 -MMD -MP
DEP			= $(OBJ:.o=.d)
SRCDIR		= srcs
OBJDIR		= obj
INCLUDES	= -I.

#####
#Src#
#####
SRC			= 	main \
				http/RequestLine \
				http/Headers
SRCS		= $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(SRC)))
OBJ			= $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))

#######
#Rules#
#######
all: $(NAME)

$(NAME): $(OBJ)
		c++ $(FLAGS) $(OBJ) -o $(NAME)
		@echo "✅ ready ✅"

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
		@mkdir -p $(dir $@)
		c++ $(FLAGS) $(INCLUDES) -c $< -o $@

-include $(DEP)

clean:
		rm -rf $(OBJDIR)

fclean: clean
		rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
