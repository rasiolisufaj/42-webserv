#include "config/ConfigParser.hpp"
#include "http/Request.hpp"
#include "cgi/Cgi.hpp"
#include <fcntl.h>
#include "http/RequestContext.hpp"
#include <iostream>

int main(int ac, char **av)
{
    try
    {
        if (ac != 2)
            throw(std::runtime_error("Enter an argument and try again\n===BYE WEBSERV==="));
        std::cout << "\n===WebServ is running===\n\n";

        /**************/
        /*LENY'S PART*/
        /**************/
        std::cout << "\n+++CONFIG PARSER LOG+++\n";

        ConfigParser Parser;

        if (Parser.parseConfig(av[1]) != 0)
            return (EXIT_FAILURE);
        
        const std::vector<ServerConfig> &all_servers = Parser.getServers();
        const ServerConfig &my_server = all_servers[0];
        const Location &loc = my_server.locations[0];
        
        /***************/
        /*RASIOL'S PART*/
        /***************/

        //....

        /**************/
        /*THOMA'S PART*/
        /**************/
     
    Request request;
    RequestContext ctx;

    std::string raw =
        "GET /index.html?x=1 HTTP/1.1\r\n"
        "Host: site-a.com:8080\r\n"
        "User-Agent: curl/8.5.0\r\n"
        "\r\n";

    std::string raw_get =
    "GET /cgi-bin/cgi_post.py?Name=Leny&proyect=Webserv HTTP/1.1\r\n"
    "Host: localhost:8080\r\n"
    "User-Agent: curl/8.5.0\r\n"
    "\r\n"; // Termina con doble salto de línea, vacío por abajo

    std::string raw_post =
    "POST /cgi-bin/cgi_post.py HTTP/1.1\r\n"
    "Host: localhost:8080\r\n"
    "Content-Length: 25\r\n" // Crucial: "Name=Leny&proyect=Webserv" mide 25 bytes
    "Content-Type: application/x-www-form-urlencoded\r\n"
    "\r\n" // <--- Este doble salto de línea separa los headers del cuerpo
    "Name=Leny&proyect=Webserv"; // El cuerpo real que leerá el write()
        
    request.parse(raw_get);

    // e.g : port réellement touché par la socket
    ctx.build(request, 8080);

    std::cout << "local_port: " << ctx.localPort() << "\n";
    std::cout << "host name: " << ctx.effectiveHost().name << "\n";
    std::cout << "host port: " << ctx.effectiveHost().port << "\n";
    std::cout << "method: " << ctx.method() << "\n";
    std::cout << "target: " << ctx.target() << "\n";
    std::cout << "path: " << ctx.path() << "\n";
    std::cout << "query: " << ctx.query() << "\n";
    std::cout << "body size: " << ctx.bodySize() << "\n";

    if (ctx.headers().hasHeader("user-agent"))
        std::cout << "user-agent: " << ctx.headers().getHeader("user-agent") << "\n";



        //CGI PART
       std::cout << "\n+++CGI LOG+++\n";
       Cgi my_cgi;
       
       std::map<std::string, std::string> map_env = my_cgi.setUpEnvironment(ctx, loc);
        char **envp = my_cgi.convertMapToEnvp(map_env);
        char **argv = my_cgi.setupArgv(loc, map_env);
        pid_t pid = my_cgi.execute(argv, envp);
        if (pid > 0)
        {
            close(my_cgi.getFdInWrite()) ;
            char buffer[4096];
            int bytes_read;
            std::string cgi_response = "";
            while ((bytes_read = read(my_cgi.getFdOutRead(), buffer, sizeof(buffer) - 1)) > 0)
            {
                buffer[bytes_read] = '\0';
                cgi_response += buffer;
            }
            std::cout << "\n--- [Padre] Respuesta del CGI recibida: ---\n";
            std::cout << cgi_response << std::endl;
            std::cout << "-------------------------------------------\n";
            close(my_cgi.getFdOutRead());
            int status;
            waitpid(pid, &status, 0);
        }

        my_cgi.free_array(argv);
        my_cgi.free_array(envp);

        std::cout << "--- Check fds open ---" << std::endl;
        for (int fd = 3; fd < 20; fd++) 
        {
            if (fcntl(fd, F_GETFD) != -1) {
                std::cout << " FD [" << fd << "] OPEN " << std::endl;
            }
        }
        std::cout << "------------------------------------" << std::endl;

        }


    catch (std::runtime_error &e)
    {
        std::cout << "Error: " << e.what() << '\n';
        return (EXIT_FAILURE);
    }
    return (EXIT_SUCCESS);
}

// int main()
// {
//     Logger::log(Logger::INFO, "WebServ started successfully.");
//     Logger::log(Logger::DEBUG, "This is a debug message.");
//     Logger::log(Logger::WARNING, "This is a warning message.");
//     Logger::log(Logger::ERROR, "This is an error message.");

//     int fd = 42;
//     double timeout = 3.14;

//     Logger::log(Logger::INFO,
//         "fd = " + to_string(fd));

//     Logger::log(Logger::INFO,
//         "timeout = " + to_string(timeout));

//     std::cout << "\n=== Changing log level to WARNING ===" << std::endl;

//     Logger::setLogLevel(Logger::WARNING);
//     Logger::log(Logger::INFO, "This INFO message should NOT appear.");
//     Logger::log(Logger::DEBUG, "This DEBUG message should NOT appear.");
//     Logger::log(Logger::WARNING, "This WARNING message should appear.");
//     Logger::log(Logger::ERROR, "This ERROR message should appear.");
// }

//e.g context using
/*
#include "http/Request.hpp"
#include "http/RequestContext.hpp"
#include <iostream>

int main(void)
{
    Request request;
    RequestContext ctx;

    std::string raw =
        "GET /index.html?x=1 HTTP/1.1\r\n"
        "Host: site-a.com:8080\r\n"
        "User-Agent: curl/8.5.0\r\n"
        "\r\n";

    request.parse(raw);

    // e.g : port réellement touché par la socket
    ctx.build(request, 8080);

    std::cout << "local_port: " << ctx.localPort() << "\n";
    std::cout << "host name: " << ctx.effectiveHost().name << "\n";
    std::cout << "host port: " << ctx.effectiveHost().port << "\n";
    std::cout << "method: " << ctx.method() << "\n";
    std::cout << "target: " << ctx.target() << "\n";
    std::cout << "path: " << ctx.path() << "\n";
    std::cout << "query: " << ctx.query() << "\n";
    std::cout << "body size: " << ctx.bodySize() << "\n";

    if (ctx.headers().hasHeader("user-agent"))
        std::cout << "user-agent: " << ctx.headers().getHeader("user-agent") << "\n";

    return (0);
}*/


