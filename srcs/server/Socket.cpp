#include "server/Socket.hpp"
#include "server/IServerCallback.hpp"
#include "utils/Logger.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Stores the bind host/port; the actual socket is created in setup().
 */
Socket::Socket(const std::string &host, const std::string &port)
    : _fd(-1), _host(host), _port(port), _peer_addr_len(sizeof(_peer_addr)) {}

/*
 * Ensures the listening fd is closed.
 */
Socket::~Socket()
{
  if (this->_fd != -1)
  {
    close(this->_fd);
    this->_fd = -1;
  }
}

/*
 * Creates a non-blocking listening socket and binds it to the configured host/port.
 */
void Socket::setup()
{
  int port_num = std::atoi(this->_port.c_str());
  if (port_num < 1 || port_num > 65535)
    throw std::runtime_error("invalid port: " + this->_port);

  struct addrinfo hints, *res, *p;
  int status;

  std::memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM; // TCP
  hints.ai_flags = AI_PASSIVE; // Causes bind() to autofill the IP

  const char *node = NULL;
  if (!this->_host.empty() && this->_host != "0.0.0.0" && this->_host != "*")
    node = this->_host.c_str();
  if (node == NULL)
    hints.ai_flags = AI_PASSIVE;
  else
    hints.ai_flags = 0;

  // getaddrinfo() returns a linked list of addrinfo structs; try each until one succeeds
  // It helps you to convert hostname + port into a sockaddr struct that can be used in bind() and connect()
  if ((status = getaddrinfo(node, this->_port.c_str(), &hints, &res)) != 0)
  {
    throw std::runtime_error("getaddrinfo error: " +
                             std::string(gai_strerror(status)));
  }

  int last_errno = 0;
  for (p = res; p != NULL; p = p->ai_next)
  {
    this->_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (this->_fd == -1)
    {
      last_errno = errno;
      continue;
    }
    // set different flags on the socket to make it non-blocking and close-on-exec
    int flags = fcntl(this->_fd, F_GETFD, 0);
    if (flags != -1)
    {
      fcntl(this->_fd, F_SETFD, flags | FD_CLOEXEC); // CGI Protection
    }

    flags = fcntl(this->_fd, F_GETFL, 0);
    if (flags == -1)
    {
      last_errno = errno;
      close(this->_fd);
      this->_fd = -1;
      continue;
    }
    if (fcntl(this->_fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
      last_errno = errno;
      close(this->_fd);
      this->_fd = -1;
      continue;
    }

    int yes = 1;
    // Allow reuse of the address (SO_REUSEADDR) to avoid "Address already in use" errors on restart
    if (setsockopt(this->_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) ==
        -1)
    {
      last_errno = errno;
      close(this->_fd);
      this->_fd = -1;
      continue;
    }

    // Bind the socket to the address and port
    if (bind(this->_fd, p->ai_addr, p->ai_addrlen) == -1)
    {
      last_errno = errno;
      close(this->_fd);
      this->_fd = -1;
      continue;
    }
    break;
  }

  freeaddrinfo(res);

  if (p == NULL)
    throw std::runtime_error("failed to bind to any address: " +
                             std::string(std::strerror(last_errno)));

  // Start listening for incoming connections
  if (listen(this->_fd, SOMAXCONN) == -1)
    throw std::runtime_error("listen error: " +
                             std::string(std::strerror(errno)));
}

/*
 * Accepts a new client and configures it as non-blocking and close-on-exec.
 */
int Socket::acceptConnection()
{
  int new_fd =
      accept(this->_fd, reinterpret_cast<struct sockaddr *>(&this->_peer_addr),
             &this->_peer_addr_len);

  if (new_fd == -1)
  {
    return -1;
  }
  int flags = fcntl(new_fd, F_GETFD, 0);
  if (flags != -1)
  {
    fcntl(new_fd, F_SETFD, flags | FD_CLOEXEC); // CGI Protection
  }
  flags = fcntl(new_fd, F_GETFL, 0);
  if (flags == -1)
  {
    close(new_fd);
    throw std::runtime_error("fcntl error: " +
                             std::string(std::strerror(errno)));
  }
  if (fcntl(new_fd, F_SETFL, flags | O_NONBLOCK) == -1)
  {
    close(new_fd);
    throw std::runtime_error("fcntl error: " +
                             std::string(std::strerror(errno)));
  }
  return new_fd;
}

/*
 * Returns the listening socket fd.
 */
int Socket::getFd() const { return this->_fd; }

/*
 * Handles EPOLLIN on the listening socket by accepting all queued clients.
 */
void Socket::handle(uint32_t event, IServerCallback &callback)
{
  if (event & (EPOLLERR | EPOLLHUP))
  {
    Logger::log(Logger::ERROR,
      "On Socket: " + to_string(this->_fd) +
      " Epoll error/hangup event: " + to_string(event));
      return;
    }
    
    while (true)
  {
    int client_fd = -1;
    try
    {
      client_fd = this->acceptConnection();
      if (client_fd == -1)
      {
        // No more clients to accept (EAGAIN/EWOULDBLOCK)
        break;
      }
      callback.onNewConnection(client_fd, this->_fd);
    }
    catch (const std::exception &e)
    {
      if (client_fd != -1)
      {
        Logger::log(
            Logger::ERROR,
            "Error after accepting connection fd: " + to_string(client_fd) +
                " (socket already closed in acceptConnection) error: " +
                std::string(e.what()));
        close(client_fd);
      }
      else
        Logger::log(Logger::ERROR,
                    "Failed to accept new connection on listening socket fd: " +
                        to_string(this->_fd) + " error: " + std::string(e.what()));
      break;
    }
  }
}
