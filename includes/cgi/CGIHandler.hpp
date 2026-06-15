#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include "cgi/CgiJob.hpp"
#include "server/Connection.hpp"
#include "http/RequestContext.hpp"
#include "config/ServerConfig.hpp"
#include <map>
#include <string>

class CgiHandler
{
private:
  /*
   * Utility class: prevent instantiation.
   */
  CgiHandler();

  /*
   * Converts a C++ env map into a NULL-terminated envp array for execve().
   * Returns:
   * - newly allocated envp array
   */
  static char **allocateEnv(const std::map<std::string, std::string> &envMap);

  /*
   * Frees an envp array created by allocateEnv().
   */
  static void freeEnv(char **envp);

public:
  /*
   * Builds a CgiJob from the routed request + matched location.
   * Resolves the interpreter by the script's file extension (loc.cgi map),
   * the script path/working dir, the request body, and the CGI/1.1 env.
   * Returns:
   * - true  if the target maps to a CGI interpreter (job is populated)
   * - false if this is not a CGI request (job is left untouched)
   */
  static bool buildJob(const RequestContext &ctx, const ServerConfig &server,
                       const Location &loc, CgiJob &job);

  /*
   * Starts a CGI process and wires it to the connection via non-blocking pipes.
   * Returns:
   * - true on success, false on failure (HTTP layer should respond 500)
   */
  static bool execute(Connection *conn, const CgiJob &job);
};

#endif
