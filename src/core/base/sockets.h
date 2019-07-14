// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef SOCKETS_H
#define SOCKETS_H

#include <arpa/inet.h>

namespace sockets {
///
/// Creates a non-blocking socket file descriptor,
/// abort if any error.
void setNonBlockAndCloseOnExec(int sockfd);
int createTcpNonblockingOrDie(sa_family_t family);
int createUdpNonblockingOrDie(sa_family_t family);

int connect(int sockfd, const struct sockaddr* addr);
int bind(int sockfd, const struct sockaddr* addr);
int listen(int sockfd);
int accept(int sockfd, struct sockaddr_in6* addr);
ssize_t read(int sockfd, void *buf, size_t count);
ssize_t readv(int sockfd, const struct iovec *iov, int iovcnt);
ssize_t write(int sockfd, const void *buf, size_t count);
ssize_t writev(int sockfd, const struct iovec *iov, int iovcnt);
void close(int sockfd);
void shutdownWrite(int sockfd);

void toIpPort(char* buf, size_t size, const struct sockaddr* addr);
void toIp(char* buf, size_t size, const struct sockaddr* addr);
uint16_t toPort(const struct sockaddr* addr);

struct sockaddr* fromIpPort(const char* ip, uint16_t port, struct sockaddr_in* addr);
struct sockaddr* fromIpPort(const char* ip, uint16_t port, struct sockaddr_in6* addr);

int getSocketError(int sockfd);

const struct sockaddr* sockaddr_cast(const struct sockaddr_in* addr);
const struct sockaddr* sockaddr_cast(const struct sockaddr_in6* addr);
struct sockaddr* sockaddr_cast(struct sockaddr_in6* addr);
const struct sockaddr_in* sockaddr_in_cast(const struct sockaddr* addr);
const struct sockaddr_in6* sockaddr_in6_cast(const struct sockaddr* addr);

struct sockaddr_in6 getLocalAddr(int sockfd);
struct sockaddr_in6 getPeerAddr(int sockfd);
bool isSelfConnect(int sockfd);

void setTcpNoDelay(int fd, bool on);
void setKeepAlive(int fd, bool on);
void setReuseAddr(int fd, bool on);
void setReusePort(int fd, bool on);

}

#endif  // SOCKETS_H
