#include "cgi/CGIHandler.hpp"
#include "utils/Logger.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

/*
 * Builds a CgiJob from a routed request. The interpreter is selected by the
 * script's file extension via loc.cgi; if no extension matches, this is not a
 * CGI request and we return false without touching `job`.
 *
 * The script is run with the child's working directory set to the directory
 * that contains it (see CgiHandler::execute), so script_path is stored as the
 * bare filename while the full filesystem path is exported via PATH_TRANSLATED.
 */
bool CgiHandler::buildJob(const RequestContext &ctx, const ServerConfig &server,
                          const Location &loc, CgiJob &job)
{
  const std::string &url_path = ctx.path();

  // Resolve the extension from the request path (e.g. "/cgi-bin/x.py" -> ".py").
  size_t dot = url_path.rfind('.');
  if (dot == std::string::npos)
    return false;
  std::string ext = url_path.substr(dot);
  // Guard against the last dot living in a directory segment ("/a.b/c").
  if (ext.find('/') != std::string::npos)
    return false;

  std::map<std::string, std::string>::const_iterator it = loc.cgi.find(ext);
  if (it == loc.cgi.end())
    return false;

  // Effective document root: the location's own root, or the server root if the
  // location doesn't override it.
  const std::string &root = loc.root.empty() ? server.root : loc.root;

  // Full filesystem path to the script, then split into dir + filename.
  std::string full = root + url_path;
  size_t slash = full.rfind('/');
  std::string dir = (slash != std::string::npos) ? full.substr(0, slash) : ".";
  std::string file = (slash != std::string::npos) ? full.substr(slash + 1) : full;

  job.interpreter = it->second;
  job.script_path = file;          // argv[1], relative to working_dir after chdir
  job.working_dir = dir;
  job.method = ctx.method();
  job.query_string = ctx.query();
  job.url_path = url_path;

  const std::string &body = ctx.body();
  job.body.assign(body.begin(), body.end());

  // CGI/1.1 environment.
  std::map<std::string, std::string> &env = job.env;
  env["GATEWAY_INTERFACE"] = "CGI/1.1";
  env["SERVER_PROTOCOL"] = ctx.request().requestLine().version();
  env["SERVER_SOFTWARE"] = "Webserv/1.0";
  env["REQUEST_METHOD"] = ctx.method();
  env["CONTENT_LENGTH"] = to_string(job.body.size());
  env["PATH_TRANSLATED"] = full;
  env["SCRIPT_FILENAME"] = full;
  env["SCRIPT_NAME"] = url_path;
  env["QUERY_STRING"] = ctx.query();
  env["PATH_INFO"] = "";
  env["REMOTE_ADDR"] = "127.0.0.1";
  env["SERVER_PORT"] = ctx.effectiveHost().port;
  env["SERVER_NAME"] = ctx.effectiveHost().name;
  if (ctx.headers().hasHeader("Content-Type"))
    env["CONTENT_TYPE"] = ctx.headers().getHeader("Content-Type");
  else
    env["CONTENT_TYPE"] = "";

  return true;
}

/*
 * Allocates a NULL-terminated envp array in the "KEY=VALUE" form for execve().
 */
char **
CgiHandler::allocateEnv(const std::map<std::string, std::string> &envMap)
{
  char **envp = new char *[envMap.size() + 1];
  size_t i = 0;
  for (std::map<std::string, std::string>::const_iterator it = envMap.begin();
       it != envMap.end(); ++it)
  {
    std::string entry = it->first + "=" + it->second;
    envp[i] = new char[entry.size() + 1];
    std::strcpy(envp[i], entry.c_str());
    ++i;
  }
  envp[i] = NULL;
  return envp;
}

/*
 * Frees an envp array created by allocateEnv().
 */
void CgiHandler::freeEnv(char **envp)
{
  if (!envp)
    return;
  for (size_t i = 0; envp[i] != NULL; ++i)
  {
    delete[] envp[i];
  }
  delete[] envp;
}

/*
 * Forks and execs a CGI script, wiring stdin/stdout to non-blocking pipes.
 *
 * On success, the created pipe fds and pid are stored in the Connection so the
 * Server can drive the CGI I/O with epoll.
 */
bool CgiHandler::execute(Connection *conn, const CgiJob &job)
{
  int pipe_in[2];
  int pipe_out[2];

  // 1. Pipe Creation
  if (pipe(pipe_in) < 0 || pipe(pipe_out) < 0)
  {
    Logger::log(Logger::ERROR, "CGI pipe creation failed.");
    return false;
  }

  // 2. Enforce Non-Blocking on the Parent's ends of the pipes
  fcntl(pipe_in[1], F_SETFL, O_NONBLOCK);
  fcntl(pipe_out[0], F_SETFL, O_NONBLOCK);

  // Increase pipe buffer size to 1 MB for throughput (Linux-specific, best-effort)
#ifdef F_SETPIPE_SZ
  fcntl(pipe_in[1], F_SETPIPE_SZ, 1048576);
  fcntl(pipe_out[0], F_SETPIPE_SZ, 1048576);
#endif

  int flags = fcntl(pipe_in[1], F_GETFD, 0);
  fcntl(pipe_in[1], F_SETFD, flags | FD_CLOEXEC);

  flags = fcntl(pipe_out[0], F_GETFD, 0);
  fcntl(pipe_out[0], F_SETFD, flags | FD_CLOEXEC);

  // 3. Fork
  pid_t pid = fork();
  if (pid < 0)
  {
    Logger::log(Logger::ERROR, "CGI fork failed.");
    close(pipe_in[0]);
    close(pipe_in[1]);
    close(pipe_out[0]);
    close(pipe_out[1]);
    return false;
  }

  if (pid == 0)
  {
    // --- CHILD PROCESS ---

    // Redirect standard streams to pipes
    dup2(pipe_in[0], STDIN_FILENO);
    dup2(pipe_out[1], STDOUT_FILENO);

    // Close all pipe FDs inherited from parent to prevent EOF hangs
    close(pipe_in[0]);
    close(pipe_in[1]);
    close(pipe_out[0]);
    close(pipe_out[1]);

    // Shift to the CGI script's directory for relative file access
    if (chdir(job.working_dir.c_str()) != 0)
    {
      std::exit(1);
    }
    
    char **envp = allocateEnv(job.env);

    char *argv[3];
    argv[0] = new char[job.interpreter.size() + 1];
    std::strcpy(argv[0], job.interpreter.c_str());

    argv[1] = new char[job.script_path.size() + 1];
    std::strcpy(argv[1], job.script_path.c_str());

    argv[2] = NULL;

    // Execute script
    execve(argv[0], argv, envp);

    // Architectural Fallback: If execve returns, it failed catastrophically.
    // We MUST free the dynamically allocated memory before exiting to satisfy
    // leak checkers.
    freeEnv(envp);
    delete[] argv[0];
    delete[] argv[1];
    std::exit(1);
  }
  else
  {
    // --- PARENT PROCESS ---

    // Close the child's ends of the pipes
    close(pipe_in[0]);
    close(pipe_out[1]);

    // Map the OS resources to the Connection state
    conn->cgi_in_fd = pipe_in[1];
    conn->cgi_out_fd = pipe_out[0];
    conn->cgi_pid = pid;
    conn->cgi_start_time = std::time(NULL);

    // Formally transition the state machine
    conn->setState(Connection::CGI_WRITING_BODY);

    return true;
  }
}
