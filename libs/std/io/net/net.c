// SPDX-License-Identifier: BSD-3-Clause

#include <jello.h>
#include <jello/internal/vm_internal.h>
#include <jello/jdll.h>

#include "net_internal.h"
#include "io_common.h"

#if defined(_WIN32)
#define net_io_fail(c, op) io_fail_winsock(c, op)
#else
#define net_io_fail(c, op) io_fail(c, op)
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

JDLL_DEFINE_KIND(net_addr);
JDLL_DEFINE_KIND(net_socket);

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#endif

static int bytes_to_cstr(const uint8_t* data, uint32_t len, char* out, size_t cap) {
  if(!data || !len || cap < 2) return 0;
  uint32_t n = len < cap - 1u ? len : (uint32_t)(cap - 1u);
  memcpy(out, data, n);
  out[n] = 0;
  return 1;
}

static void net_addr_fin(void* payload) {
  free(payload);
}

static void net_socket_fin(void* payload) {
  jdll_net_socket* s = (jdll_net_socket*)payload;
  if(!s) return;
#if defined(_WIN32)
  if(s->fd >= 0) closesocket((SOCKET)s->fd);
#else
  if(s->fd >= 0) close(s->fd);
#endif
  s->fd = -1;
  free(s);
}

static jdll_net_addr* net_addr_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_net_addr*)a->payload;
}

static jdll_net_socket* net_socket_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_net_socket*)a->payload;
}

static uint32_t net_obj_type_id(const jello_bc_module* m) {
  if(!m) return 0;
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_OBJECT) return i;
  }
  return 0;
}

static uint32_t net_atom_id(const jello_bc_module* m, const char* name) {
  return vm_module_atom_id_or_default(m, name, 0);
}

static uint32_t net_atom_id_ctx(jdlo_ctx* c, const char* name) {
  struct jello_vm* vm = jdl_ctx_vm(c);
  if(vm && vm->running_module) {
    uint32_t aid = net_atom_id(vm->running_module, name);
    if(aid) return aid;
  }
  return net_atom_id(jdl_ctx_module(c), name);
}

static jello_object* net_make_record(jdlo_ctx* c, uint32_t nfields, const char** keys, jello_value* vals) {
  struct jello_vm* vm = jdl_ctx_vm(c);
  const jello_bc_module* m = jdl_ctx_module(c);
  if(!vm || !m) return NULL;
  uint32_t tid = net_obj_type_id(m);
  jello_object* o = jello_object_new(vm, tid);
  if(!o) return NULL;
  for(uint32_t i = 0; i < nfields; i++) {
    uint32_t aid = net_atom_id_ctx(c, keys[i]);
    if(aid) jello_object_set(o, aid, vals[i]);
  }
  return o;
}

static int net_set_nonblocking(int fd) {
#if defined(_WIN32)
  u_long mode = 1;
  return ioctlsocket((SOCKET)fd, FIONBIO, &mode) == 0;
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if(flags < 0) return 0;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static int net_parse_host_port(const char* s, char* host, size_t hcap, int* port) {
  if(!s || !host || !port) return 0;
  const char* colon = strrchr(s, ':');
  if(!colon || colon == s) return 0;
  size_t hlen = (size_t)(colon - s);
  if(hlen >= hcap) return 0;
  memcpy(host, s, hlen);
  host[hlen] = 0;
  *port = atoi(colon + 1);
  return *port > 0 && *port <= 65535;
}

static jdll_net_addr* net_addr_from_host_port(const char* host, int port) {
  jdll_net_addr* a = (jdll_net_addr*)calloc(1, sizeof(*a));
  if(!a) return NULL;
  char hostbuf[256];
  snprintf(hostbuf, sizeof hostbuf, "%s", host ? host : "");
  if(hostbuf[0] == '[') {
    char* end = strchr(hostbuf, ']');
    if(end && end > hostbuf + 1) {
      *end = 0;
      memmove(hostbuf, hostbuf + 1, (size_t)(end - hostbuf - 1));
      hostbuf[end - hostbuf - 1] = 0;
    }
  }
  snprintf(a->host, sizeof a->host, "%s", hostbuf);
  a->port = port;
  char portstr[16];
  snprintf(portstr, sizeof portstr, "%d", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = 0;
  hints.ai_protocol = 0;
  struct addrinfo* res = NULL;
  if(getaddrinfo(hostbuf, portstr, &hints, &res) != 0 || !res) {
    free(a);
    return NULL;
  }
  a->len = (socklen_t)res->ai_addrlen;
  memcpy(&a->ss, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  return a;
}

#if defined(_WIN32)

static int net_wsa_inited = 0;

static int net_ensure_wsa(void) {
  if(net_wsa_inited) return 1;
  WSADATA wsa;
  if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
  net_wsa_inited = 1;
  return 1;
}

static int net_last_would_block(void) {
  int e = io_winsock_errno();
  return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}

static int net_wait_connected(int fd, int timeout_ms) {
  if(timeout_ms < 0) timeout_ms = 10000;
  fd_set wfds;
  FD_ZERO(&wfds);
  FD_SET((SOCKET)fd, &wfds);
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  if(select(0, NULL, &wfds, NULL, &tv) <= 0) return -1;
  int err = 0;
  int elen = (int)sizeof err;
  if(getsockopt((SOCKET)fd, SOL_SOCKET, SO_ERROR, (char*)&err, &elen) != 0) return 0;
  if(err == 0) return 1;
  WSASetLastError(err);
  return 0;
}

#else

static int net_last_would_block(void) {
  int e = io_last_errno();
  return e == EAGAIN || e == EWOULDBLOCK || e == EINPROGRESS;
}

static int net_wait_connected(int fd, int timeout_ms) {
  if(timeout_ms < 0) timeout_ms = 10000;
  int64_t deadline_ms = 0;
  struct timespec ts;
  if(clock_gettime(CLOCK_MONOTONIC, &ts) == 0) deadline_ms = (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000 + timeout_ms;
  struct pollfd pfd = { .fd = fd, .events = POLLOUT };
  for(;;) {
    int remain = timeout_ms;
    if(deadline_ms > 0) {
      if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
      int64_t now_ms = (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
      remain = (int)(deadline_ms - now_ms);
      if(remain <= 0) return -1;
    }
    int pr = poll(&pfd, 1, remain);
    if(pr < 0) {
      if(errno == EINTR) continue;
      return 0;
    }
    if(pr == 0) return -1;
    int err = 0;
    socklen_t elen = sizeof err;
    if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) != 0) return 0;
    if(err == 0) return 1;
    if(err == EINPROGRESS) continue;
    errno = err;
    return 0;
  }
}

#endif

void jdll_std_addr_parse(jdlo_ctx* c) {
#if defined(_WIN32)
  if(!net_ensure_wsa()) {
    jdl_fail(c, "addr_parse: WSAStartup failed");
    return;
  }
#endif
  const uint8_t* data = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  char s[512];
  if(!bytes_to_cstr(data, len, s, sizeof s)) {
    jdl_fail(c, "addr_parse: bad input");
    return;
  }
  char host[256];
  int port = 0;
  if(!net_parse_host_port(s, host, sizeof host, &port)) {
    jdl_fail(c, "addr_parse: expected host:port");
    return;
  }
  jdll_net_addr* a = net_addr_from_host_port(host, port);
  if(!a) {
    jdl_fail(c, "addr_parse: resolve failed");
    return;
  }
  jdl_return_abstract(c, a, net_addr_fin);
}

void jdll_std_addr_resolve(jdlo_ctx* c) {
#if defined(_WIN32)
  if(!net_ensure_wsa()) {
    jdl_fail(c, "addr_resolve: WSAStartup failed");
    return;
  }
#endif
  const uint8_t* data = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  int32_t port = jdl_arg_i32(c, 1);
  char host[256];
  if(!bytes_to_cstr(data, len, host, sizeof host) || port < 0 || port > 65535) {
    jdl_fail(c, "addr_resolve: bad host or port");
    return;
  }
  jdll_net_addr* a = net_addr_from_host_port(host, (int)port);
  if(!a) {
    jdl_fail(c, "addr_resolve: DNS failed");
    return;
  }
  jdl_return_abstract(c, a, net_addr_fin);
}

void jdll_std_addr_host(jdlo_ctx* c) {
  jdll_net_addr* addr = net_addr_from_arg(c, 0);
  if(!addr) {
    jdl_fail(c, "addr_host: bad address");
    return;
  }
  const char* host = addr->host;
  jdl_return_bytes_copy(c, (const uint8_t*)host, (uint32_t)strlen(host));
}

void jdll_std_addr_port(jdlo_ctx* c) {
  jdll_net_addr* addr = net_addr_from_arg(c, 0);
  if(!addr) {
    jdl_fail(c, "addr_port: bad address");
    return;
  }
  jdl_return_i32(c, (int32_t)addr->port);
}

void jdll_std_addr_is_abstract(jdlo_ctx* c) {
  jdl_return_bool(c, jdl_is_abstract(jdl_arg_value(c, 0)));
}

static jdll_net_socket* net_socket_new(int fd, net_sock_kind kind, int listening) {
  jdll_net_socket* s = (jdll_net_socket*)calloc(1, sizeof(*s));
  if(!s) {
#if defined(_WIN32)
    closesocket((SOCKET)fd);
#else
    close(fd);
#endif
    return NULL;
  }
  s->fd = fd;
  s->kind = kind;
  s->listening = listening;
  (void)net_set_nonblocking(fd);
  return s;
}

static jello_object* net_sockaddr_record(jdlo_ctx* c, struct sockaddr_storage* ss) {
  char host[256];
  host[0] = 0;
  int port = 0;
  if(ss->ss_family == AF_INET) {
    struct sockaddr_in* sin = (struct sockaddr_in*)ss;
    inet_ntop(AF_INET, &sin->sin_addr, host, sizeof host);
    port = (int)ntohs(sin->sin_port);
  } else if(ss->ss_family == AF_INET6) {
    struct sockaddr_in6* sin6 = (struct sockaddr_in6*)ss;
    inet_ntop(AF_INET6, &sin6->sin6_addr, host, sizeof host);
    port = (int)ntohs(sin6->sin6_port);
  } else {
    return NULL;
  }
  struct jello_vm* vm = jdl_ctx_vm(c);
  const jello_bc_module* m = jdl_ctx_module(c);
  if(!vm || !m) return NULL;
  uint32_t bytes_tid = 0;
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_BYTES) {
      bytes_tid = i;
      break;
    }
  }
  jello_bytes* host_b = jello_bytes_new(vm, bytes_tid, (uint32_t)strlen(host));
  if(host_b && host[0]) memcpy(host_b->data, host, strlen(host));
  const char* keys[] = {"host", "port"};
  jello_value vals[2];
  vals[0] = host_b ? jello_from_ptr(host_b) : jello_make_null();
  vals[1] = jello_make_i32(port);
  return net_make_record(c, 2, keys, vals);
}

static void net_tcp_connect_impl(jdlo_ctx* c, jdll_net_addr* addr, int32_t timeout_ms) {
  if(!addr) {
    jdl_fail(c, "tcp_connect: bad address");
    return;
  }
#if defined(_WIN32)
  if(!net_ensure_wsa()) {
    jdl_fail(c, "tcp_connect: WSAStartup failed");
    return;
  }
#endif
  int fd = (int)socket(addr->ss.ss_family, SOCK_STREAM, IPPROTO_TCP);
  if(fd < 0) {
    net_io_fail(c, "tcp_connect");
    return;
  }
  (void)net_set_nonblocking(fd);
  int rc = connect(fd, (struct sockaddr*)&addr->ss, addr->len);
  if(rc != 0 && !net_last_would_block()) {
#if defined(_WIN32)
    closesocket((SOCKET)fd);
#else
    close(fd);
#endif
    net_io_fail(c, "tcp_connect");
    return;
  }
  if(rc != 0) {
    int wr = net_wait_connected(fd, timeout_ms < 0 ? 10000 : (int)timeout_ms);
    if(wr != 1) {
#if defined(_WIN32)
      closesocket((SOCKET)fd);
#else
      close(fd);
#endif
      if(wr < 0) io_fail_code(c, "tcp_connect", "ETIMEDOUT");
      else net_io_fail(c, "tcp_connect");
      return;
    }
  }
  jdll_net_socket* s = net_socket_new(fd, NET_SOCK_TCP, 0);
  if(!s) {
    jdl_fail(c, "tcp_connect: oom");
    return;
  }
  jdl_return_abstract(c, s, net_socket_fin);
}

static int net_bind_socket(jdll_net_addr* addr, int socktype, net_sock_kind kind) {
  if(!addr) return -1;
#if defined(_WIN32)
  if(!net_ensure_wsa()) return -1;
#endif
  int fd = (int)socket(addr->ss.ss_family, socktype, socktype == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP);
  if(fd < 0) return -1;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
  if(bind(fd, (struct sockaddr*)&addr->ss, addr->len) != 0) {
#if defined(_WIN32)
    closesocket((SOCKET)fd);
#else
    close(fd);
#endif
    return -1;
  }
  (void)kind;
  return fd;
}

void jdll_std_udp_bind(jdlo_ctx* c) {
  jdll_net_addr* addr = net_addr_from_arg(c, 0);
  if(!addr) {
    jdl_fail(c, "udp_bind: bad address");
    return;
  }
  int fd = net_bind_socket(addr, SOCK_DGRAM, NET_SOCK_UDP);
  if(fd < 0) {
    net_io_fail(c, "udp_bind");
    return;
  }
  jdll_net_socket* s = net_socket_new(fd, NET_SOCK_UDP, 0);
  if(!s) {
    jdl_fail(c, "udp_bind: oom");
    return;
  }
  jdl_return_abstract(c, s, net_socket_fin);
}

void jdll_std_udp_try_recv_from(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  int32_t max = jdl_arg_i32(c, 1);
  if(!sock || sock->kind != NET_SOCK_UDP || max <= 0) {
    jdl_fail(c, "udp_try_recv_from: bad socket or size");
    return;
  }
  if(max > 65507) max = 65507;
  uint8_t* buf = (uint8_t*)malloc((size_t)max);
  if(!buf) {
    jdl_fail(c, "udp_try_recv_from: oom");
    return;
  }
  struct sockaddr_storage peer;
  socklen_t peerlen = sizeof peer;
  ssize_t n =
#if defined(_WIN32)
      recvfrom(sock->fd, (char*)buf, (int)max, 0, (struct sockaddr*)&peer, &peerlen);
#else
      recvfrom(sock->fd, buf, (size_t)max, 0, (struct sockaddr*)&peer, &peerlen);
#endif
  if(n < 0) {
    free(buf);
    if(net_last_would_block()) {
      jdl_return_null(c);
      return;
    }
    net_io_fail(c, "udp_try_recv_from");
    return;
  }

  char host[256];
  host[0] = 0;
  int port = 0;
  if(peer.ss_family == AF_INET) {
    struct sockaddr_in* sin = (struct sockaddr_in*)&peer;
    inet_ntop(AF_INET, &sin->sin_addr, host, sizeof host);
    port = (int)ntohs(sin->sin_port);
  } else if(peer.ss_family == AF_INET6) {
    struct sockaddr_in6* sin6 = (struct sockaddr_in6*)&peer;
    inet_ntop(AF_INET6, &sin6->sin6_addr, host, sizeof host);
    port = (int)ntohs(sin6->sin6_port);
  }

  struct jello_vm* vm = jdl_ctx_vm(c);
  const jello_bc_module* m = jdl_ctx_module(c);
  if(!vm || !m) {
    free(buf);
    jdl_fail(c, "udp_try_recv_from: no vm");
    return;
  }
  uint32_t bytes_tid = 0;
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_BYTES) {
      bytes_tid = i;
      break;
    }
  }
  jello_bytes* payload = jello_bytes_new(vm, bytes_tid, (uint32_t)n);
  if(!payload) {
    free(buf);
    jdl_fail(c, "udp_try_recv_from: oom");
    return;
  }
  memcpy(payload->data, buf, (size_t)n);
  free(buf);

  jello_bytes* peer_b = jello_bytes_new(vm, bytes_tid, (uint32_t)strlen(host));
  if(peer_b && host[0]) memcpy(peer_b->data, host, strlen(host));

  const char* keys[] = {"payload", "peer", "port"};
  jello_value vals[3];
  vals[0] = jello_from_ptr(payload);
  vals[1] = peer_b ? jello_from_ptr(peer_b) : jello_make_null();
  vals[2] = jello_make_i32(port);
  jello_object* rec = net_make_record(c, 3, keys, vals);
  if(!rec) {
    jdl_fail(c, "udp_try_recv_from: record oom");
    return;
  }
  jdl_return_object(c, rec);
}

void jdll_std_udp_send_to(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  const uint8_t* data = jdl_arg_bytes_data(c, 1);
  uint32_t len = jdl_arg_bytes_len(c, 1);
  jdll_net_addr* addr = net_addr_from_arg(c, 2);
  int32_t port = jdl_arg_i32(c, 3);
  if(!sock || sock->kind != NET_SOCK_UDP || !data || !addr) {
    jdl_fail(c, "udp_send_to: bad args");
    return;
  }
  struct sockaddr_storage ss = addr->ss;
  socklen_t slen = addr->len;
  if(port > 0) {
    if(ss.ss_family == AF_INET) {
      ((struct sockaddr_in*)&ss)->sin_port = htons((uint16_t)port);
    } else if(ss.ss_family == AF_INET6) {
      ((struct sockaddr_in6*)&ss)->sin6_port = htons((uint16_t)port);
    }
  }
  ssize_t n =
#if defined(_WIN32)
      sendto(sock->fd, (const char*)data, (int)len, 0, (struct sockaddr*)&ss, slen);
#else
      sendto(sock->fd, data, len, 0, (struct sockaddr*)&ss, slen);
#endif
  if(n < 0) {
    if(net_last_would_block()) {
      jdl_return_i32(c, 0);
      return;
    }
    net_io_fail(c, "udp_send_to");
    return;
  }
  jdl_return_i32(c, (int32_t)n);
}

void jdll_std_udp_poll_fd(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  if(!sock) {
    jdl_fail(c, "udp_poll_fd: bad socket");
    return;
  }
  jdl_return_i32(c, sock->fd);
}

void jdll_std_udp_close(jdlo_ctx* c) {
  jdl_close_abstract(c, 0);
}

void jdll_std_tcp_connect(jdlo_ctx* c) {
  jdll_net_addr* addr = net_addr_from_arg(c, 0);
  net_tcp_connect_impl(c, addr, -1);
}

void jdll_std_tcp_connect_timeout(jdlo_ctx* c) {
  jdll_net_addr* addr = net_addr_from_arg(c, 0);
  int32_t timeout_ms = jdl_arg_i32(c, 1);
  net_tcp_connect_impl(c, addr, timeout_ms);
}

void jdll_std_tcp_listen(jdlo_ctx* c) {
  jdll_net_addr* addr = net_addr_from_arg(c, 0);
  int32_t backlog = jdl_arg_i32(c, 1);
  if(!addr) {
    jdl_fail(c, "tcp_listen: bad address");
    return;
  }
  if(backlog <= 0) backlog = 128;
  int fd = net_bind_socket(addr, SOCK_STREAM, NET_SOCK_TCP);
  if(fd < 0) {
    net_io_fail(c, "tcp_listen");
    return;
  }
  if(listen(fd, backlog) != 0) {
#if defined(_WIN32)
    closesocket((SOCKET)fd);
#else
    close(fd);
#endif
    net_io_fail(c, "tcp_listen");
    return;
  }
  jdll_net_socket* s = net_socket_new(fd, NET_SOCK_TCP, 1);
  if(!s) {
    jdl_fail(c, "tcp_listen: oom");
    return;
  }
  jdl_return_abstract(c, s, net_socket_fin);
}

void jdll_std_tcp_accept(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  if(!sock || sock->kind != NET_SOCK_TCP || !sock->listening) {
    jdl_fail(c, "tcp_accept: bad listener");
    return;
  }
  int cfd = (int)accept(sock->fd, NULL, NULL);
  if(cfd < 0) {
    if(net_last_would_block()) {
      jdl_return_null(c);
      return;
    }
    net_io_fail(c, "tcp_accept");
    return;
  }
  jdll_net_socket* client = net_socket_new(cfd, NET_SOCK_TCP, 0);
  if(!client) {
    jdl_fail(c, "tcp_accept: oom");
    return;
  }
  jdl_return_abstract(c, client, net_socket_fin);
}

void jdll_std_tcp_try_read(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  int32_t max = jdl_arg_i32(c, 1);
  if(!sock || sock->kind != NET_SOCK_TCP || sock->listening || max <= 0) {
    jdl_fail(c, "tcp_try_read: bad socket");
    return;
  }
  if(max > 1024 * 1024) max = 1024 * 1024;
  uint8_t* buf = (uint8_t*)malloc((size_t)max);
  if(!buf) {
    jdl_fail(c, "tcp_try_read: oom");
    return;
  }
  ssize_t n =
#if defined(_WIN32)
      recv(sock->fd, (char*)buf, (int)max, 0);
#else
      recv(sock->fd, buf, (size_t)max, 0);
#endif
  if(n < 0) {
    free(buf);
    if(net_last_would_block()) {
      jdl_return_null(c);
      return;
    }
    net_io_fail(c, "tcp_try_read");
    return;
  }
  if(n == 0) {
    free(buf);
    jdl_return_bytes_copy(c, NULL, 0);
    return;
  }
  jdl_return_bytes_copy(c, buf, (uint32_t)n);
  free(buf);
}

void jdll_std_tcp_try_write(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  const uint8_t* data = jdl_arg_bytes_data(c, 1);
  uint32_t len = jdl_arg_bytes_len(c, 1);
  if(!sock || sock->kind != NET_SOCK_TCP || sock->listening || !data) {
    jdl_fail(c, "tcp_try_write: bad args");
    return;
  }
  ssize_t n =
#if defined(_WIN32)
      send(sock->fd, (const char*)data, (int)len, 0);
#else
      send(sock->fd, data, len, 0);
#endif
  if(n < 0) {
    if(net_last_would_block()) {
      jdl_return_i32(c, 0);
      return;
    }
    net_io_fail(c, "tcp_try_write");
    return;
  }
  jdl_return_i32(c, (int32_t)n);
}

void jdll_std_tcp_poll_fd(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  if(!sock) {
    jdl_fail(c, "tcp_poll_fd: bad socket");
    return;
  }
  jdl_return_i32(c, sock->fd);
}

void jdll_std_tcp_bound_port(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  if(!sock) {
    jdl_fail(c, "tcp_bound_port: bad socket");
    return;
  }
  struct sockaddr_storage ss;
  socklen_t len = sizeof ss;
  if(getsockname(sock->fd, (struct sockaddr*)&ss, &len) != 0) {
    net_io_fail(c, "tcp_bound_port");
    return;
  }
  int port = 0;
  if(ss.ss_family == AF_INET) {
    port = ntohs(((struct sockaddr_in*)&ss)->sin_port);
  } else if(ss.ss_family == AF_INET6) {
    port = ntohs(((struct sockaddr_in6*)&ss)->sin6_port);
  } else {
    jdl_fail(c, "tcp_bound_port: unsupported address family");
    return;
  }
  jdl_return_i32(c, (int32_t)port);
}

void jdll_std_tcp_close(jdlo_ctx* c) {
  jdl_close_abstract(c, 0);
  jdl_return_bool(c, 1);
}

void jdll_std_tcp_shutdown(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  int32_t how = jdl_arg_i32(c, 1);
  if(!sock || sock->fd < 0 || sock->kind != NET_SOCK_TCP) {
    jdl_fail(c, "tcp_shutdown: bad socket");
    return;
  }
#if defined(_WIN32)
  if(shutdown((SOCKET)sock->fd, how) != 0) {
    net_io_fail(c, "tcp_shutdown");
    return;
  }
#else
  if(shutdown(sock->fd, how) != 0) {
    net_io_fail(c, "tcp_shutdown");
    return;
  }
#endif
  jdl_return_bool(c, 1);
}

void jdll_std_tcp_local_addr(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  if(!sock || sock->fd < 0) {
    jdl_fail(c, "tcp_local_addr: bad socket");
    return;
  }
  struct sockaddr_storage ss;
  socklen_t len = sizeof ss;
  if(getsockname(sock->fd, (struct sockaddr*)&ss, &len) != 0) {
    net_io_fail(c, "tcp_local_addr");
    return;
  }
  jello_object* rec = net_sockaddr_record(c, &ss);
  if(!rec) {
    jdl_fail(c, "tcp_local_addr: oom");
    return;
  }
  jdl_return_object(c, rec);
}

void jdll_std_tcp_remote_addr(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  if(!sock || sock->fd < 0 || sock->listening) {
    jdl_fail(c, "tcp_remote_addr: bad socket");
    return;
  }
  struct sockaddr_storage ss;
  socklen_t len = sizeof ss;
  if(getpeername(sock->fd, (struct sockaddr*)&ss, &len) != 0) {
    net_io_fail(c, "tcp_remote_addr");
    return;
  }
  jello_object* rec = net_sockaddr_record(c, &ss);
  if(!rec) {
    jdl_fail(c, "tcp_remote_addr: oom");
    return;
  }
  jdl_return_object(c, rec);
}

void jdll_std_tcp_set_nodelay(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  int on = jdl_arg_bool(c, 1) ? 1 : 0;
  if(!sock || sock->fd < 0 || sock->kind != NET_SOCK_TCP) {
    jdl_fail(c, "tcp_set_nodelay: bad socket");
    return;
  }
  if(setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, (socklen_t)sizeof on) != 0) {
    net_io_fail(c, "tcp_set_nodelay");
    return;
  }
  jdl_return_bool(c, 1);
}

void jdll_std_tcp_set_keepalive(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  int on = jdl_arg_bool(c, 1) ? 1 : 0;
  if(!sock || sock->fd < 0 || sock->kind != NET_SOCK_TCP) {
    jdl_fail(c, "tcp_set_keepalive: bad socket");
    return;
  }
  if(setsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&on, (socklen_t)sizeof on) != 0) {
    net_io_fail(c, "tcp_set_keepalive");
    return;
  }
  jdl_return_bool(c, 1);
}

void jdll_std_udp_set_broadcast(jdlo_ctx* c) {
  jdll_net_socket* sock = net_socket_from_arg(c, 0);
  int on = jdl_arg_bool(c, 1) ? 1 : 0;
  if(!sock || sock->fd < 0 || sock->kind != NET_SOCK_UDP) {
    jdl_fail(c, "udp_set_broadcast: bad socket");
    return;
  }
  if(setsockopt(sock->fd, SOL_SOCKET, SO_BROADCAST, (const char*)&on, (socklen_t)sizeof on) != 0) {
    net_io_fail(c, "udp_set_broadcast");
    return;
  }
  jdl_return_bool(c, 1);
}

static void net_close_fd(int fd) {
  if(fd < 0) return;
#if defined(_WIN32)
  closesocket((SOCKET)fd);
#else
  close(fd);
#endif
}

static int net_can_bind_port_family(int port, int socktype, int family) {
  if(port < 1 || port > 65535) return 0;
#if defined(_WIN32)
  if(!net_ensure_wsa()) return 0;
#endif
  int fd = (int)socket(family, socktype, socktype == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP);
  if(fd < 0) return 0;
  struct sockaddr_storage ss;
  memset(&ss, 0, sizeof ss);
  socklen_t len;
  if(family == AF_INET) {
    struct sockaddr_in* sin = (struct sockaddr_in*)&ss;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(INADDR_ANY);
    sin->sin_port = htons((uint16_t)port);
    len = (socklen_t)sizeof(*sin);
  } else if(family == AF_INET6) {
    struct sockaddr_in6* sin6 = (struct sockaddr_in6*)&ss;
    sin6->sin6_family = AF_INET6;
    sin6->sin6_addr = in6addr_any;
    sin6->sin6_port = htons((uint16_t)port);
    len = (socklen_t)sizeof(*sin6);
  } else {
    net_close_fd(fd);
    return 0;
  }
  int ok = bind(fd, (struct sockaddr*)&ss, len) == 0;
  net_close_fd(fd);
  return ok;
}

static int net_family_port_available(int port, int family) {
  if(!net_can_bind_port_family(port, SOCK_DGRAM, family)) return 0;
  if(!net_can_bind_port_family(port, SOCK_STREAM, family)) return 0;
  if(!net_port_probe_sctp(port, family)) return 0;
  return 1;
}

static int net_ipv6_probe_enabled(void) {
  static int cached = -1;
  if(cached >= 0) return cached;
#if defined(_WIN32)
  if(!net_ensure_wsa()) {
    cached = 0;
    return 0;
  }
#endif
  int fd = (int)socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if(fd < 0) {
    cached = 0;
    return 0;
  }
  net_close_fd(fd);
  cached = 1;
  return 1;
}

static int net_port_available(int port) {
  if(!net_family_port_available(port, AF_INET)) return 0;
  if(net_ipv6_probe_enabled() && !net_family_port_available(port, AF_INET6)) return 0;
  return 1;
}

static int net_probe_port_range(int min_port, int max_port, int need_pair) {
  if(min_port < 1) min_port = 1;
  if(max_port > 65535) max_port = 65535;
  if(min_port > max_port) return -1;
  if(need_pair) {
    if(max_port - min_port < 1) return -1;
    for(int pass = 0; pass < 2; pass++) {
      int start = min_port;
      if(pass == 0) {
        if(start & 1) start++;
      } else {
        if(!(start & 1)) start++;
      }
      for(int p = start; p <= max_port - 1; p += 2) {
        if(net_port_available(p) && net_port_available(p + 1)) return p;
      }
    }
    return -1;
  }
  for(int p = min_port; p <= max_port; p++) {
    if(net_port_available(p)) return p;
  }
  return -1;
}

static void net_probe_port_impl(jdlo_ctx* c, int need_pair) {
  int32_t min_port = jdl_arg_i32(c, 0);
  int32_t max_port = jdl_arg_i32(c, 1);
  int found = net_probe_port_range((int)min_port, (int)max_port, need_pair);
  if(found < 0) {
    io_fail_code(c, need_pair ? "net_probe_port_pair" : "net_probe_port", "EADDRNOTAVAIL");
    return;
  }
  jdl_return_i32(c, (int32_t)found);
}

void jdll_std_net_probe_port(jdlo_ctx* c) {
  net_probe_port_impl(c, 0);
}

void jdll_std_net_probe_port_pair(jdlo_ctx* c) {
  net_probe_port_impl(c, 1);
}
