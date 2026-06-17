#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include "cgi/CgiJob.hpp"
#include "server/Connection.hpp"
#include "http/RequestContext.hpp"
#include "config/ServerConfig.hpp"
#include <map>
#include <string>

// Static utility: the pure fork/exec side of CGI (CgiManager drives the event loop).
class CgiHandler
{
private:
  CgiHandler();  // utility class: no instances

  // Converts an env map into a NULL-terminated envp array for execve().
  static char **allocateEnv(const std::map<std::string, std::string> &envMap);

  // Frees an envp array created by allocateEnv().
  static void freeEnv(char **envp);

public:
  // Populates a CgiJob from the routed request + matched location: resolves the
  // interpreter by file extension (loc.cgi), script path/working dir, body, and
  // CGI/1.1 env. Returns false (job untouched) for a non-CGI target.
  static bool buildJob(const RequestContext &ctx, const ServerConfig &server,
                       const Location &loc, CgiJob &job);

  // Forks the CGI child and wires it to the connection via non-blocking pipes.
  // Returns false on failure (caller responds 500).
  static bool execute(Connection *conn, const CgiJob &job);
};

#endif
