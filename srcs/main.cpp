#include "config/ConfigParser.hpp"
#include "http/Request.hpp"
#include "cgi/Cgi.hpp"

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

        // const std::vector<ServerConfig> &all_servers = Parser.getServers();

        // for (size_t i = 0; i < all_servers.size(); i++)
        // {
        //     all_servers[i].print_servers();
        // }

        /***************/
        /*RASIOL'S PART*/
        /***************/

        //....


        /**************/
        /*THOMA'S PART*/
        /**************/
        std::cout << "\n+++REQUEST PARSER LOG+++\n";
        std::string good_requestTest = "POST /cgi-bin/test.py HTTP/1.1\r\nHost: localhost:8080\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello";
        std::string bad_requestTest = "POST /cgi-bin/test.py HTTP/1.1\r\nHost: localhost:8080\r\nConnection: close\r\n\r\nhello";
        /*TEST REQUEST*/
        Request rqs;
        //rqs.parse(good_requestTest);
        rqs.parse(good_requestTest);

        RequestLine rl;
        rl = rqs.requestLine();
        std::cout << rl.target() << "\n> ";            //   => "/login"
        std::cout << rl.version() << "\n> ";           //   => "HTTP/1.1"

        Headers hd;
        hd = rqs.headers();
        std::cout << hd.getHeader("host") << "\n> ";   //   => "localhost:8080"
        std::cout << hd.getContentLength() << "\n> ";   //   => 5
        std::cout << hd.wantsClose() << "\n> ";         //   => true

        std::cout << rqs.body() << '\n'; // => "hello"

            
        /*
        rl.parse("GET /index.html HTTP/1.1"); //valide
        rl.parse("GET index.html HTTP/1.1"); //invalide
        
        hd.parseLine("Host: api.interactivtrading.com"); //valide
        hd.parseLine("Ho st: api.interactivtrading.com"); //invalide
        */

        //CGI PART
       std::cout << "\n+++CGI LOG+++\n";
       Cgi my_cgi;
       Location loc;
       std::map<std::string, std::string> map_env = my_cgi.setUpEnvironment(rqs);
       //my_cgi.execute(rqs);
       //my_cgi.print_env(env);
        char **envp = my_cgi.convertMapToEnvp(map_env);
        char **argv = my_cgi.setupArgv(loc, map_env);
        pid_t pid = my_cgi.execute(argv, envp);
        
        
    }


    catch (std::runtime_error &e)
    {
        std::cout << "Error: " << e.what() << '\n';
        return (EXIT_FAILURE);
    }
    return (EXIT_SUCCESS);
}