#include "cgi/CGIHandler.hpp"
#include "utils/Logger.hpp"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Builds a CgiJob from a routed request; returns false (job untouched) when this
// isn't a CGI target. The child chdir's into the script's directory (see execute),
// so script_path is the bare filename and the full path goes via PATH_TRANSLATED.
bool CgiHandler::buildJob(const RequestContext &ctx, const ServerConfig &server,
                          const Location &loc, CgiJob &job)
{
  const std::string &url_path = ctx.path();

  // Resolve the extension from the request path (e.g. "/cgi-bin/x.py" -> ".py").
  size_t dot = url_path.rfind('.');
  if (dot == std::string::npos)
    return false;
  std::string ext = url_path.substr(dot);
  if (ext.find('/') != std::string::npos)  // guard against a dot in a dir segment
    return false;

  std::map<std::string, std::string>::const_iterator it = loc.cgi.find(ext);
  if (it == loc.cgi.end())
    return false;

  const std::string &root = loc.root.empty() ? server.root : loc.root;
  std::string full = root + url_path;

  // The extension maps to an interpreter, but if the script isn't a real file this
  // isn't a runnable target: return false so the static path emits the 404.
  struct stat st;
  if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    return false;

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

  // Export the route's effective upload dir (location over server) so scripts like
  // list_uploads.py find uploads. The child chdir's away, so make a relative config
  // path absolute against the server CWD now.
  std::string upload =
      loc.upload_store.empty() ? server.upload_store : loc.upload_store;
  if (!upload.empty() && upload[0] != '/')
  {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
      upload = std::string(cwd) + "/" + upload;
  }
  env["UPLOAD_STORE"] = upload;

  env["REMOTE_ADDR"] = "127.0.0.1";
  env["SERVER_PORT"] = ctx.effectiveHost().port;
  env["SERVER_NAME"] = ctx.effectiveHost().name;
  if (ctx.headers().hasHeader("Content-Type"))
    env["CONTENT_TYPE"] = ctx.headers().getHeader("Content-Type");
  else
    env["CONTENT_TYPE"] = "";

  // Expose every request header as HTTP_<NAME> (CGI/1.1): uppercase, '-' -> '_'.
  // Content-Type/Length are already their own CGI vars, so skip them.
  const std::map<std::string, std::string> &fields = ctx.headers().fields();
  for (std::map<std::string, std::string>::const_iterator it = fields.begin();
       it != fields.end(); ++it)
  {
    if (it->first == "content-type" || it->first == "content-length")
      continue;
    std::string key = "HTTP_";
    for (size_t i = 0; i < it->first.size(); ++i)
    {
      char c = it->first[i];
      key += (c == '-') ? '_'
                        : static_cast<char>(std::toupper(
                              static_cast<unsigned char>(c)));
    }
    env[key] = it->second;
  }

  return true;
}

// Allocates a NULL-terminated "KEY=VALUE" envp array for execve().
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

// Frees an envp array created by allocateEnv().
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

// Forks and execs the CGI script, wiring stdin/stdout to non-blocking pipes. On
// success stores the pipe fds + pid in the Connection so the loop can drive CGI I/O.
bool CgiHandler::execute(Connection *conn, const CgiJob &job)
{
  int pipe_in[2];
  int pipe_out[2];

  // Created separately so a failure on the second pipe doesn't leak the first.
  if (pipe(pipe_in) < 0)
  {
    Logger::log(Logger::ERROR, "CGI pipe creation failed.");
    return false;
  }
  if (pipe(pipe_out) < 0)
  {
    Logger::log(Logger::ERROR, "CGI pipe creation failed.");
    close(pipe_in[0]);
    close(pipe_in[1]);
    return false;
  }

  // Parent ends: non-blocking, 1 MB buffer (best-effort), and FD_CLOEXEC.
  fcntl(pipe_in[1], F_SETFL, O_NONBLOCK);
  fcntl(pipe_out[0], F_SETFL, O_NONBLOCK);

#ifdef F_SETPIPE_SZ
  fcntl(pipe_in[1], F_SETPIPE_SZ, 1048576);
  fcntl(pipe_out[0], F_SETPIPE_SZ, 1048576);
#endif

  int flags = fcntl(pipe_in[1], F_GETFD, 0);
  fcntl(pipe_in[1], F_SETFD, flags | FD_CLOEXEC);

  flags = fcntl(pipe_out[0], F_GETFD, 0);
  fcntl(pipe_out[0], F_SETFD, flags | FD_CLOEXEC);

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
    // --- CHILD ---
    dup2(pipe_in[0], STDIN_FILENO);
    dup2(pipe_out[1], STDOUT_FILENO);

    // Close every inherited pipe fd so the child sees EOF correctly.
    close(pipe_in[0]);
    close(pipe_in[1]);
    close(pipe_out[0]);
    close(pipe_out[1]);

    // chdir into the script's dir so its relative file access works.
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

    execve(argv[0], argv, envp);

    // execve only returns on failure; free before exiting to keep leak checkers clean.
    freeEnv(envp);
    delete[] argv[0];
    delete[] argv[1];
    std::exit(1);
  }
  else
  {
    // --- PARENT ---
    close(pipe_in[0]);   // child's ends
    close(pipe_out[1]);

    conn->cgi_in_fd = pipe_in[1];
    conn->cgi_out_fd = pipe_out[0];
    conn->cgi_pid = pid;
    conn->cgi_start_time = std::time(NULL);
    conn->setState(Connection::CGI_WRITING_BODY);

    return true;
  }
}
