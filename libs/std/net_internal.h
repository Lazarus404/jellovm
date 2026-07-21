// SPDX-License-Identifier: BSD-3-Clause
#ifndef JELLOVM_STD_NET_INTERNAL_H
#define JELLOVM_STD_NET_INTERNAL_H

#include <stdint.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

typedef struct jdll_net_addr {
  struct sockaddr_storage ss;
  socklen_t len;
  char host[256];
  int port;
} jdll_net_addr;

typedef enum {
  NET_SOCK_UDP = 1,
  NET_SOCK_TCP = 2,
} net_sock_kind;

typedef struct jdll_net_socket {
  int fd;
  net_sock_kind kind;
  int listening;
} jdll_net_socket;

// Port availability probe (implemented in net.c / sctp.c).
int net_port_probe_sctp(int port, int family);

#endif
