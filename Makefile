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
				http/ChunkDecod \
				http/Headers \
				http/HttpStatus \
				http/MimeTypes \
				http/Request \
				http/RequestContext \
				http/RequestLine \
				http/RequestTarget \
				http/Response \
				http/ResponseBuilder \
				http/ResponseBuilderError \
				http/ResponseBuilderGet \
				http/ResponseBuilderWrite \
				http/utils_http \
				config/ConfigMatcher \
				config/ConfigParser \
				config/RouteContext \
				config/ServerConfig \
				cgi/CGIHandler \
				server/CgiManager \
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

re: fclean all

test: $(NAME)
	@c++ $(CXXFLAGS) $(INCLUDES) tests/test_main.cpp $(filter-out obj/main.o,$(OBJ)) -o tests/test_main
	@echo "Running tests..."
	@./tests/test_main

.PHONY: all clean fclean re test