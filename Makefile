# Nombre del ejecutable
NAME        = webserv

# Compilador y Flags (Requeridas por el subject)
CXX         = c++
CXXFLAGS    = -Wall -Wextra -Werror -std=c++98

# Directorios de la estructura que me pasaste
SRC_DIR     = srcs
INC_DIR     = includes
OBJ_DIR     = objs

# Archivos fuente (Añadimos el main y tu parte del parsing)
# He incluido la ruta exacta según tu árbol de directorios
SRCS        = $(SRC_DIR)/main.cpp \
              $(SRC_DIR)/config/ConfigParser.cpp

# Transformación de archivos .cpp a .o en la carpeta objs
OBJS        = $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# Flags de inclusión para que el compilador encuentre tus .hpp
CPPFLAGS    = -I$(INC_DIR)

# Colores para una terminal más bonita (Opcional, muy común en 42)
GREEN       = \033[0;32m
RESET       = \033[0m

# Regla principal
all: $(NAME)

# Creación del ejecutable
$(NAME): $(OBJS)
	@$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)
	@ echo -e "$(GREEN)Webserv compilado con éxito$(RESET)"

# Regla para compilar los archivos objeto
# Se encarga de crear las subcarpetas necesarias dentro de objs/
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# Limpieza de archivos objeto
clean:
	@rm -rf $(OBJ_DIR)
	@echo "Objetos eliminados"

# Limpieza total (objetos y ejecutable)
fclean: clean
	@rm -f $(NAME)
	@echo "Ejecutable eliminado"

# Re-compilación total
re: fclean all

.PHONY: all clean fclean re