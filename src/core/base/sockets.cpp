// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "sockets.h"
#include "Endian.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>  // snprintf
#include <strings.h>  // bzero
#include <sys/socket.h>
#include <sys/uio.h>  // readv
#include <netinet/tcp.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>


void sockets::setNonBlockAndCloseOnExec(int sockfd)
{
  // non-block
  int flags = ::fcntl(sockfd, F_GETFL, 0);
  flags |= O_NONBLOCK;
  int ret = ::fcntl(sockfd, F_SETFL, flags);
  // FIXME check

  // close-on-exec
  flags = ::fcntl(sockfd, F_GETFD, 0);
  flags |= FD_CLOEXEC;
  ret = ::fcntl(sockfd, F_SETFD, flags);
  // FIXME check

  (void)ret;
}


const struct sockaddr* sockets::sockaddr_cast(const struct sockaddr_in6* addr)
{
  return static_cast<const struct sockaddr*>(static_cast<const void*>(addr));
}

struct sockaddr* sockets::sockaddr_cast(struct sockaddr_in6* addr)
{
  return static_cast<struct sockaddr*>(static_cast<void*>(addr));
}

const struct sockaddr* sockets::sockaddr_cast(const struct sockaddr_in* addr)
{
  return static_cast<const struct sockaddr*>(static_cast<const void*>(addr));
}

const struct sockaddr_in* sockets::sockaddr_in_cast(const struct sockaddr* addr)
{
  return static_cast<const struct sockaddr_in*>(static_cast<const void*>(addr));
}

const struct sockaddr_in6* sockets::sockaddr_in6_cast(const struct sockaddr* addr)
{
  return static_cast<const struct sockaddr_in6*>(static_cast<const void*>(addr));
}

int sockets::createTcpNonblockingOrDie(sa_family_t family)
{
  int sockfd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
  if (sockfd < 0)
  {
    fprintf(stderr, "sockets::createTcpNonblockingOrDie\n");
    abort();
  }
  setNonBlockAndCloseOnExec(sockfd);
  return sockfd;
}
int sockets::createUdpNonblockingOrDie(sa_family_t family)
{
  int sockfd = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
  if (sockfd < 0)
  {
    fprintf(stderr, "sockets::createUdpNonblockingOrDie\n");
    abort();
  }
  setNonBlockAndCloseOnExec(sockfd);
  return sockfd;
}

int sockets::connect(int sockfd, const struct sockaddr* addr)
{
  if (addr == NULL) {
    return -1;
  }
  if (addr->sa_family == AF_INET) {
    return ::connect(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in)));
  } else {
    return ::connect(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
  }
}




int sockets::bind(int sockfd, const struct sockaddr* addr)
{
  int ret = -1;
  if (addr == NULL) {
    return -1;
  }
  if (addr->sa_family == AF_INET) {
    ret = ::bind(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in)));
  } else if (addr->sa_family == AF_INET6) {
    ret = ::bind(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
  }
  if (ret < 0)
  {
    fprintf(stderr, "sockets::bind error\n");
    return -1;
  } else {
    return 0;
  }
}

int sockets::listen(int sockfd)
{
  int ret = ::listen(sockfd, SOMAXCONN);
  if (ret < 0)
  {
    fprintf(stderr, "sockets::listen error\n");
    return -1;
  } else {
    return 0;
  }
}

int sockets::accept(int sockfd, struct sockaddr_in6* addr)
{
  socklen_t addrlen = static_cast<socklen_t>(sizeof *addr);
  int connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
  setNonBlockAndCloseOnExec(connfd);

  if (connfd < 0)
  {
    int savedErrno = errno;
    fprintf(stderr, "sockets::accept\n");
    switch (savedErrno)
    {
      case EAGAIN:
      case ECONNABORTED:
      case EINTR:
      case EPROTO: // ???
      case EPERM:
      case EMFILE: // per-process lmit of open file desctiptor ???
        // expected errors
        errno = savedErrno;
        break;
      case EBADF:
      case EFAULT:
      case EINVAL:
      case ENFILE:
      case ENOBUFS:
      case ENOMEM:
      case ENOTSOCK:
      case EOPNOTSUPP:
        // unexpected errors
        fprintf(stderr, "sockets::accept unexpected error %d\n", savedErrno);
        // assert(false);
        break;
      default:
        fprintf(stderr, "sockets::accept unknown error %d\n", savedErrno);
        // assert(false);
        break;
    }
  }
  return connfd;
}


ssize_t sockets::read(int sockfd, void *buf, size_t count)
{
  return ::read(sockfd, buf, count);
}

ssize_t sockets::readv(int sockfd, const struct iovec *iov, int iovcnt)
{
  return ::readv(sockfd, iov, iovcnt);
}

ssize_t sockets::write(int sockfd, const void *buf, size_t count)
{
  return ::write(sockfd, buf, count);
}
ssize_t sockets::writev(int sockfd, const struct iovec *iov, int iovcnt)
{
  return ::writev(sockfd, iov, iovcnt);
}

void sockets::close(int sockfd)
{
  if (::close(sockfd) < 0)
  {
    fprintf(stderr, "sockets::close\n");
  }
}

void sockets::shutdownWrite(int sockfd)
{
  if (::shutdown(sockfd, SHUT_WR) < 0)
  {
    fprintf(stderr, "sockets::shutdownWrite\n");
  }
}

void sockets::toIpPort(char* buf, size_t size,
                       const struct sockaddr* addr)
{
  toIp(buf,size, addr);
  size_t end = ::strlen(buf);
  const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
  uint16_t port = sockets::networkToHost16(addr4->sin_port);
  assert(size > end);
  snprintf(buf+end, size-end, ":%u", port);
}

void sockets::toIp(char* buf, size_t size, const struct sockaddr* addr)
{
  if (addr->sa_family == AF_INET)
  {
    assert(size >= INET_ADDRSTRLEN);
    const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
    ::inet_ntop(AF_INET, &addr4->sin_addr, buf, static_cast<socklen_t>(size));
  }
  else if (addr->sa_family == AF_INET6)
  {
    assert(size >= INET6_ADDRSTRLEN);
    const struct sockaddr_in6* addr6 = sockaddr_in6_cast(addr);
    ::inet_ntop(AF_INET6, &addr6->sin6_addr, buf, static_cast<socklen_t>(size));
  }
}
uint16_t sockets::toPort(const struct sockaddr* addr)
{
  const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
  uint16_t port = sockets::networkToHost16(addr4->sin_port);
  return port;
}


struct sockaddr* sockets::fromIpPort(const char* ip, uint16_t port, struct sockaddr_in* addr)
{
  addr->sin_family = AF_INET;
  addr->sin_port = hostToNetwork16(port);
  if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0)
  {
    fprintf(stderr, "ipv4 sockets::fromIpPort\n");
    return NULL;
  } else {
    return (struct sockaddr*)addr;
  }
}

struct sockaddr* sockets::fromIpPort(const char* ip, uint16_t port, struct sockaddr_in6* addr)
{
  addr->sin6_family = AF_INET6;
  addr->sin6_port = hostToNetwork16(port);
  if (::inet_pton(AF_INET6, ip, &addr->sin6_addr) <= 0)
  {
    fprintf(stderr, "ipv6 sockets::fromIpPort\n");
    return NULL;
  } else {
    return (struct sockaddr*)addr;
  }
}

int sockets::getSocketError(int sockfd)
{
  int optval;
  socklen_t optlen = static_cast<socklen_t>(sizeof optval);

  if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
  {
    return errno;
  }
  else
  {
    return optval;
  }
}

struct sockaddr_in6 sockets::getLocalAddr(int sockfd)
{
  struct sockaddr_in6 localaddr;
  bzero(&localaddr, sizeof localaddr);
  socklen_t addrlen = static_cast<socklen_t>(sizeof localaddr);
  if (::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0)
  {
    fprintf(stderr, "sockets::getLocalAddr\n");
  }
  return localaddr;
}

struct sockaddr_in6 sockets::getPeerAddr(int sockfd)
{
  struct sockaddr_in6 peeraddr;
  bzero(&peeraddr, sizeof peeraddr);
  socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
  if (::getpeername(sockfd, sockaddr_cast(&peeraddr), &addrlen) < 0)
  {
    fprintf(stderr, "sockets::getPeerAddr\n");
  }
  return peeraddr;
}


bool sockets::isSelfConnect(int sockfd)
{
  struct sockaddr_in6 localaddr = getLocalAddr(sockfd);
  struct sockaddr_in6 peeraddr = getPeerAddr(sockfd);
  if (localaddr.sin6_family == AF_INET)
  {
    const struct sockaddr_in* laddr4 = reinterpret_cast<struct sockaddr_in*>(&localaddr);
    const struct sockaddr_in* raddr4 = reinterpret_cast<struct sockaddr_in*>(&peeraddr);
    return laddr4->sin_port == raddr4->sin_port
        && laddr4->sin_addr.s_addr == raddr4->sin_addr.s_addr;
  }
  else if (localaddr.sin6_family == AF_INET6)
  {
    return localaddr.sin6_port == peeraddr.sin6_port
        && memcmp(&localaddr.sin6_addr, &peeraddr.sin6_addr, sizeof localaddr.sin6_addr) == 0;
  }
  else
  {
    return false;
  }
}

void sockets::setTcpNoDelay(int fd, bool on)
{
  int optval = on ? 1 : 0;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, static_cast<socklen_t>(sizeof optval));
  // FIXME CHECK
}
void sockets::setKeepAlive(int fd, bool on)
{
  int optval = on ? 1 : 0;
  ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, static_cast<socklen_t>(sizeof optval));
  // FIXME CHECK
}


void sockets::setReuseAddr(int fd, bool on)
{
  int optval = on ? 1 : 0;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, static_cast<socklen_t>(sizeof optval));
  // FIXME CHECK
}

void sockets::setReusePort(int fd, bool on)
{
#ifdef SO_REUSEPORT
  int optval = on ? 1 : 0;
  int ret = ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval, static_cast<socklen_t>(sizeof optval));
  if (ret < 0 && on) {
    fprintf(stderr, "SO_REUSEPORT failed.\n");
  }
#else
  if (on) {
    fprintf(stderr, "SO_REUSEPORT is not supported.\n");
  }
#endif
}

