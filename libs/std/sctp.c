// SPDX-License-Identifier: BSD-3-Clause

#include <jello.h>
#include <jello/internal/vm_internal.h>
#include <jello/jdll.h>

#include "net_internal.h"
#include "io_common.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <poll.h>
#endif

#include <usrsctp.h>

#if defined(_WIN32)
#include <stdio.h>
#include "win_pipe.h"
#include <io.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

JDLL_DEFINE_KIND(sctp_socket);

typedef struct sctp_pending_pkt {
  uint8_t* data;
  size_t len;
  struct sctp_rcvinfo rcv;
  int msg_flags;
  struct sctp_pending_pkt* next;
} sctp_pending_pkt;

typedef struct jdll_sctp_socket {
  struct socket* so;
  int poll_rfd;
  int poll_wfd;
  int listening;
  int bound_port;
#if defined(_WIN32)
  struct jdll_sctp_socket* pending_accept;
  sctp_pending_pkt* pending_head;
  sctp_pending_pkt* pending_tail;
#endif
} jdll_sctp_socket;

static void sctp_socket_fin(void* payload);
#if defined(_WIN32)
static jdll_sctp_socket* sctp_socket_new_win(int listening);
#endif

#define SCTP_UDP_PORT_DEFAULT 9899

static int sctp_encaps_port = SCTP_UDP_PORT_DEFAULT;

static void sctp_set_remote_encaps(struct socket* so) {
  struct sctp_udpencaps encaps;
  memset(&encaps, 0, sizeof encaps);
  encaps.sue_port = htons((uint16_t)sctp_encaps_port);
  encaps.sue_address.ss_family = AF_INET;
  (void)usrsctp_setsockopt(so, IPPROTO_SCTP, SCTP_REMOTE_UDP_ENCAPS_PORT, &encaps, (socklen_t)sizeof encaps);
  encaps.sue_address.ss_family = AF_INET6;
  (void)usrsctp_setsockopt(so, IPPROTO_SCTP, SCTP_REMOTE_UDP_ENCAPS_PORT, &encaps, (socklen_t)sizeof encaps);
}

static void sctp_enable_rcvinfo(struct socket* so) {
  int on = 1;
  (void)usrsctp_setsockopt(so, IPPROTO_SCTP, SCTP_RECVRCVINFO, &on, (socklen_t)sizeof on);
}

#if defined(_WIN32)
static int sctp_wsa_inited = 0;
static SOCKET sctp_loop_udp[2] = {INVALID_SOCKET, INVALID_SOCKET};
static HANDLE sctp_loop_threads[2] = {NULL, NULL};
static int sctp_loop_ready = 0;

static int sctp_ensure_wsa(void) {
  if(sctp_wsa_inited) return 1;
  WSADATA wsa;
  if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
  sctp_wsa_inited = 1;
  return 1;
}

static int sctp_conn_output(void* addr, void* buf, size_t length, uint8_t tos, uint8_t set_df) {
  (void)tos;
  (void)set_df;
  SOCKET* fdp = (SOCKET*)addr;
  if(!fdp || *fdp == INVALID_SOCKET) return WSAEINVAL;
  if(send(*fdp, (const char*)buf, (int)length, 0) == SOCKET_ERROR) return (int)WSAGetLastError();
  return 0;
}

static DWORD WINAPI sctp_tunnel_recv(void* arg) {
  SOCKET* fdp = (SOCKET*)arg;
  char buf[65536];
  if(!fdp) return 0;
  for(;;) {
    if(*fdp == INVALID_SOCKET) break;
    int n = recv(*fdp, buf, (int)sizeof buf, 0);
    if(n <= 0) break;
    usrsctp_conninput(fdp, buf, (size_t)n, 0);
  }
  return 0;
}

static int sctp_loop_udp_link(SOCKET a, SOCKET b) {
  struct sockaddr_in pa, pb;
  int la = (int)sizeof pa;
  int lb = (int)sizeof pb;
  if(a == INVALID_SOCKET || b == INVALID_SOCKET) return 0;
  if(getsockname(a, (struct sockaddr*)&pa, &la) != 0) return 0;
  if(getsockname(b, (struct sockaddr*)&pb, &lb) != 0) return 0;
  pa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  pb.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(a, (struct sockaddr*)&pb, (socklen_t)sizeof pb) == SOCKET_ERROR) return 0;
  if(connect(b, (struct sockaddr*)&pa, (socklen_t)sizeof pa) == SOCKET_ERROR) return 0;
  return 1;
}

static int sctp_loop_udp_init(void) {
  if(sctp_loop_ready) return 1;
  if(!sctp_ensure_wsa()) return 0;
  uint16_t server_port = (uint16_t)sctp_encaps_port;
  uint16_t client_port = (uint16_t)(sctp_encaps_port > 1 ? sctp_encaps_port - 1 : sctp_encaps_port + 1);
  uint16_t ports[2] = { server_port, client_port };
  for(int i = 0; i < 2; i++) {
    sctp_loop_udp[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sctp_loop_udp[i] == INVALID_SOCKET) return 0;
    int on = 1;
    (void)setsockopt(sctp_loop_udp[i], SOL_SOCKET, SO_REUSEADDR, (const char*)&on, (int)sizeof on);
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sin.sin_port = htons(ports[i]);
    if(bind(sctp_loop_udp[i], (struct sockaddr*)&sin, (socklen_t)sizeof sin) == SOCKET_ERROR) return 0;
  }
  if(!sctp_loop_udp_link(sctp_loop_udp[0], sctp_loop_udp[1])) return 0;
  for(int i = 0; i < 2; i++) {
    sctp_loop_threads[i] = CreateThread(NULL, 0, sctp_tunnel_recv, &sctp_loop_udp[i], 0, NULL);
    if(!sctp_loop_threads[i]) return 0;
  }
  for(int i = 0; i < 2; i++) {
    usrsctp_register_address((void*)&sctp_loop_udp[i]);
  }
  sctp_loop_ready = 1;
  return 1;
}

static void* sctp_win_server_ulp(void) {
  return (void*)&sctp_loop_udp[0];
}

static void* sctp_win_client_ulp(void) {
  return (void*)&sctp_loop_udp[1];
}

static void sctp_win_fill_sconn(struct sockaddr_conn* sconn, void* ulp, int port) {
  memset(sconn, 0, sizeof(*sconn));
#ifdef HAVE_SCONN_LEN
  sconn->sconn_len = (uint8_t)sizeof(*sconn);
#endif
  sconn->sconn_family = AF_CONN;
  sconn->sconn_port = htons((uint16_t)port);
  sconn->sconn_addr = ulp;
}

static int sctp_win_bind_conn(void* ulp, struct socket* so, int port) {
  struct sockaddr_conn sconn;
  sctp_win_fill_sconn(&sconn, ulp, port);
  return usrsctp_bind(so, (struct sockaddr*)&sconn, (socklen_t)sizeof sconn);
}

static int sctp_win_connect_conn(void* ulp, struct socket* so, int port) {
  struct sockaddr_conn sconn;
  sctp_win_fill_sconn(&sconn, ulp, port);
  return usrsctp_connect(so, (struct sockaddr*)&sconn, (socklen_t)sizeof sconn);
}

static int sctp_addr_is_loopback(const jdll_net_addr* addr) {
  if(!addr) return 0;
  if(addr->ss.ss_family == AF_INET) {
    const struct sockaddr_in* sin = (const struct sockaddr_in*)&addr->ss;
    return sin->sin_addr.s_addr == htonl(INADDR_LOOPBACK);
  }
  if(addr->ss.ss_family == AF_INET6) {
    const struct sockaddr_in6* sin6 = (const struct sockaddr_in6*)&addr->ss;
    return IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr) != 0;
  }
  return 0;
}

static void sctp_signal_poll(jdll_sctp_socket* s);

static void sctp_trace(const char* fmt, ...) {
  if(!getenv("SCTP_TRACE")) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  fflush(stderr);
}

static void sctp_pending_clear(jdll_sctp_socket* s) {
  if(!s) return;
  while(s->pending_head) {
    sctp_pending_pkt* pkt = s->pending_head;
    s->pending_head = pkt->next;
    free(pkt->data);
    free(pkt);
  }
  s->pending_tail = NULL;
}

static void sctp_pending_push(jdll_sctp_socket* s, void* data, size_t len, const struct sctp_rcvinfo* rcv, int flags) {
  if(!s || !data || len == 0) {
    if(data) free(data);
    return;
  }
  sctp_pending_pkt* pkt = (sctp_pending_pkt*)calloc(1, sizeof(*pkt));
  if(!pkt) {
    free(data);
    return;
  }
  pkt->data = (uint8_t*)data;
  pkt->len = len;
  if(rcv) pkt->rcv = *rcv;
  pkt->msg_flags = flags;
  if(s->pending_tail) s->pending_tail->next = pkt;
  else s->pending_head = pkt;
  s->pending_tail = pkt;
  sctp_trace("sctp_pending_push: sock=%p len=%zu listening=%d", (void*)s, len, s->listening);
  sctp_signal_poll(s);
}

static int sctp_receive_cb(struct socket* sock, union sctp_sockstore addr, void* data,
                           size_t datalen, struct sctp_rcvinfo rcv, int flags, void* ulp_info) {
  (void)sock;
  (void)addr;
  jdll_sctp_socket* s = (jdll_sctp_socket*)ulp_info;
  if(!s) {
    if(data) free(data);
    return 1;
  }
  if(!data) return 1;
  if(flags & MSG_NOTIFICATION) {
    free(data);
    return 1;
  }
  sctp_pending_push(s, data, datalen, &rcv, flags);
  return 1;
}

static struct socket* sctp_open_socket(jdll_sctp_socket* owner) {
  struct socket* so = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, sctp_receive_cb, NULL, 0, owner);
  if(!so) return NULL;
  return so;
}
#endif

static int sctp_inited = 0;

static void sctp_ensure_init(void) {
  if(sctp_inited) return;
#if defined(_WIN32)
  (void)sctp_ensure_wsa();
  usrsctp_init(0, sctp_conn_output, NULL);
  if(!sctp_loop_udp_init()) return;
  usrsctp_sysctl_set_sctp_ecn_enable(0);
  sctp_inited = 1;
#else
  usrsctp_init((uint16_t)sctp_encaps_port, NULL, NULL);
  usrsctp_sysctl_set_sctp_blackhole(2);
  usrsctp_sysctl_set_sctp_no_csum_on_loopback(0);
  sctp_inited = 1;
#endif
}

static void sctp_prepare_sockaddr(struct sockaddr_storage* ss, socklen_t* len) {
#if defined(HAVE_SIN_LEN) || defined(__APPLE__)
  if(ss->ss_family == AF_INET) {
    struct sockaddr_in* sin = (struct sockaddr_in*)ss;
    sin->sin_len = (uint8_t)sizeof(*sin);
    *len = (socklen_t)sizeof(*sin);
  } else if(ss->ss_family == AF_INET6) {
    struct sockaddr_in6* sin6 = (struct sockaddr_in6*)ss;
    sin6->sin6_len = (uint8_t)sizeof(*sin6);
    *len = (socklen_t)sizeof(*sin6);
  }
#else
  (void)ss;
  (void)len;
#endif
}

#if !defined(_WIN32)
static struct socket* sctp_open_socket(int family) {
  return usrsctp_socket(family, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL);
}
#endif

static int sctp_pick_ephemeral_port(void) {
  static int next = 0;
  if(next == 0) next = 40000 + (int)(time(NULL) & 0x1fff);
  next++;
  if(next > 60000) next = 40000;
  return next;
}

static void sctp_set_port(struct sockaddr_storage* ss, socklen_t* len, int port) {
  sctp_prepare_sockaddr(ss, len);
  if(ss->ss_family == AF_INET) {
    ((struct sockaddr_in*)ss)->sin_port = htons((uint16_t)port);
  } else if(ss->ss_family == AF_INET6) {
    ((struct sockaddr_in6*)ss)->sin6_port = htons((uint16_t)port);
  }
}

int net_port_probe_sctp(int port, int family) {
  if(port < 1 || port > 65535) return 0;
  if(family != AF_INET && family != AF_INET6) return 0;
  sctp_ensure_init();
#if defined(_WIN32)
  if(!sctp_loop_udp_init()) return 0;
  jdll_sctp_socket* s = sctp_socket_new_win(1);
  if(!s) return 0;
  int ok = sctp_win_bind_conn(sctp_win_server_ulp(), s->so, port) == 0;
  sctp_socket_fin(s);
  return ok;
#else
  struct socket* so = sctp_open_socket(family);
  if(!so) return 0;
  struct sockaddr_storage ss;
  memset(&ss, 0, sizeof ss);
  socklen_t len;
  if(family == AF_INET) {
    struct sockaddr_in* sin = (struct sockaddr_in*)&ss;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(INADDR_ANY);
    sin->sin_port = htons((uint16_t)port);
    len = (socklen_t)sizeof(*sin);
  } else {
    struct sockaddr_in6* sin6 = (struct sockaddr_in6*)&ss;
    sin6->sin6_family = AF_INET6;
    sin6->sin6_addr = in6addr_any;
    sin6->sin6_port = htons((uint16_t)port);
    len = (socklen_t)sizeof(*sin6);
  }
  sctp_prepare_sockaddr(&ss, &len);
  int ok = usrsctp_bind(so, (struct sockaddr*)&ss, len) == 0;
  usrsctp_close(so);
  return ok;
#endif
}

static jdll_net_addr* net_addr_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_net_addr*)a->payload;
}

static jdll_sctp_socket* sctp_socket_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_sctp_socket*)a->payload;
}

static int sctp_set_nonblocking(int fd) {
#if defined(_WIN32)
  (void)fd;
  return 1;
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if(flags < 0) return 0;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static int sctp_last_would_block(void) {
  int e = io_errno_value();
#if defined(_WIN32)
  if(e == 0) e = io_winsock_errno();
  return e == EWOULDBLOCK || e == EINPROGRESS || e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
  return e == EAGAIN || e == EWOULDBLOCK || e == EINPROGRESS;
#endif
}

static void sctp_io_fail(jdlo_ctx* c, const char* op) {
#if defined(_WIN32)
  int err = io_errno_value();
  if(err == 0) err = io_winsock_errno();
  io_fail_errno(c, op, err);
#else
  io_fail_errno(c, op, io_errno_value());
#endif
}

static int sctp_make_poll_pipe(jdll_sctp_socket* s) {
  s->poll_rfd = -1;
  s->poll_wfd = -1;
#if defined(_WIN32)
  if(win_pipe_create(&s->poll_rfd, &s->poll_wfd) != 0) return 0;
#else
  int fds[2];
  if(pipe(fds) != 0) return 0;
  s->poll_rfd = fds[0];
  s->poll_wfd = fds[1];
#endif
  (void)sctp_set_nonblocking(s->poll_rfd);
  (void)sctp_set_nonblocking(s->poll_wfd);
  return 1;
}

static void sctp_drain_poll(jdll_sctp_socket* s) {
  if(!s || s->poll_rfd < 0) return;
#if defined(_WIN32)
  HANDLE h = win_fd_handle(s->poll_rfd);
  if(h == INVALID_HANDLE_VALUE || h == (HANDLE)-1) return;
  char buf[64];
  for(;;) {
    DWORD avail = 0;
    if(!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL) || avail == 0) break;
    DWORD chunk = avail > (DWORD)sizeof buf ? (DWORD)sizeof buf : avail;
    DWORD got = 0;
    if(!ReadFile(h, buf, chunk, &got, NULL) || got == 0) break;
  }
#else
  char buf[64];
  for(;;) {
    ssize_t n = read(s->poll_rfd, buf, sizeof buf);
    if(n <= 0) break;
  }
#endif
}

static void sctp_signal_poll(jdll_sctp_socket* s) {
  if(!s || s->poll_wfd < 0) return;
  char b = 1;
#if defined(_WIN32)
  (void)_write(s->poll_wfd, &b, 1);
#else
  (void)write(s->poll_wfd, &b, 1);
#endif
  sctp_trace("sctp_signal_poll: sock=%p listening=%d rfd=%d", (void*)s, s->listening, s->poll_rfd);
}

static void sctp_upcall(struct socket* so, void* arg, int flags) {
  (void)so;
  (void)flags;
  sctp_signal_poll((jdll_sctp_socket*)arg);
}

static void sctp_win_set_peer_mtu(struct socket* so) {
  struct sctp_paddrparams paddrparams;
  memset(&paddrparams, 0, sizeof paddrparams);
  paddrparams.spp_address.ss_family = AF_CONN;
  paddrparams.spp_flags = SPP_PMTUD_DISABLE;
  paddrparams.spp_pathmtu = 9000;
  (void)usrsctp_setsockopt(so, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS, &paddrparams, (socklen_t)sizeof paddrparams);
}

#if defined(_WIN32)
static jdll_sctp_socket* sctp_listen_sockets[8];
static int sctp_listen_socket_count = 0;
static jdll_sctp_socket* sctp_tracked_sockets[32];
static int sctp_tracked_socket_count = 0;

static void sctp_track_listener(jdll_sctp_socket* s);
static void sctp_untrack_listener(jdll_sctp_socket* s);
static void sctp_track_socket(jdll_sctp_socket* s);
static void sctp_untrack_socket(jdll_sctp_socket* s);
static void sctp_probe_listeners(void);
static void sctp_probe_sockets(void);
static void sctp_stash_listener_accepts(void);
#endif

void jello_sctp_pump_timers(void);

#if defined(_WIN32)
static jdll_sctp_socket* sctp_socket_new_win(int listening) {
  jdll_sctp_socket* s = (jdll_sctp_socket*)calloc(1, sizeof(*s));
  if(!s) return NULL;
  s->listening = listening ? 1 : 0;
  struct socket* so = sctp_open_socket(s);
  if(!so) {
    free(s);
    return NULL;
  }
  s->so = so;
  if(!sctp_make_poll_pipe(s)) {
    usrsctp_close(so);
    free(s);
    return NULL;
  }
  usrsctp_set_non_blocking(so, 1);
  sctp_win_set_peer_mtu(so);
  sctp_enable_rcvinfo(so);
  usrsctp_set_upcall(so, sctp_upcall, s);
  sctp_track_socket(s);
  return s;
}
#endif

static jdll_sctp_socket* sctp_socket_new(struct socket* so, int listening) {
  jdll_sctp_socket* s = (jdll_sctp_socket*)calloc(1, sizeof(*s));
  if(!s) {
    if(so) usrsctp_close(so);
    return NULL;
  }
  s->so = so;
  s->listening = listening ? 1 : 0;
  if(!sctp_make_poll_pipe(s)) {
    usrsctp_close(so);
    free(s);
    return NULL;
  }
  usrsctp_set_non_blocking(so, 1);
#if defined(_WIN32)
  sctp_win_set_peer_mtu(so);
#else
  sctp_set_remote_encaps(so);
#endif
  sctp_enable_rcvinfo(so);
  usrsctp_set_upcall(so, sctp_upcall, s);
#if defined(_WIN32)
  usrsctp_set_ulpinfo(so, s);
  sctp_track_socket(s);
#endif
  return s;
}

static void sctp_socket_fin(void* payload) {
  jdll_sctp_socket* s = (jdll_sctp_socket*)payload;
  if(!s) return;
#if defined(_WIN32)
  if(s->listening) sctp_untrack_listener(s);
  sctp_untrack_socket(s);
#endif
  if(s->so) {
    usrsctp_set_upcall(s->so, NULL, NULL);
#if defined(_WIN32)
    if(s->pending_accept) {
      sctp_socket_fin(s->pending_accept);
      s->pending_accept = NULL;
    }
    sctp_pending_clear(s);
#endif
    usrsctp_close(s->so);
    s->so = NULL;
  }
#if defined(_WIN32)
  win_close_fd(&s->poll_rfd);
  win_close_fd(&s->poll_wfd);
#else
  if(s->poll_rfd >= 0) close(s->poll_rfd);
  if(s->poll_wfd >= 0) close(s->poll_wfd);
#endif
  s->poll_rfd = -1;
  s->poll_wfd = -1;
  free(s);
}

static int64_t sctp_now_ms(void) {
#if defined(_WIN32)
  return (int64_t)GetTickCount64();
#else
  struct timespec ts;
  if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
#endif
}

#if defined(_WIN32)
static void sctp_track_listener(jdll_sctp_socket* s) {
  if(!s || !s->listening) return;
  for(int i = 0; i < sctp_listen_socket_count; i++) {
    if(sctp_listen_sockets[i] == s) return;
  }
  if(sctp_listen_socket_count < (int)(sizeof sctp_listen_sockets / sizeof sctp_listen_sockets[0])) {
    sctp_listen_sockets[sctp_listen_socket_count++] = s;
  }
}

static void sctp_untrack_listener(jdll_sctp_socket* s) {
  if(!s) return;
  for(int i = 0; i < sctp_listen_socket_count; i++) {
    if(sctp_listen_sockets[i] != s) continue;
    sctp_listen_sockets[i] = sctp_listen_sockets[sctp_listen_socket_count - 1];
    sctp_listen_socket_count--;
    return;
  }
}

static void sctp_track_socket(jdll_sctp_socket* s) {
  if(!s) return;
  for(int i = 0; i < sctp_tracked_socket_count; i++) {
    if(sctp_tracked_sockets[i] == s) return;
  }
  if(sctp_tracked_socket_count < (int)(sizeof sctp_tracked_sockets / sizeof sctp_tracked_sockets[0])) {
    sctp_tracked_sockets[sctp_tracked_socket_count++] = s;
  }
}

static void sctp_untrack_socket(jdll_sctp_socket* s) {
  if(!s) return;
  for(int i = 0; i < sctp_tracked_socket_count; i++) {
    if(sctp_tracked_sockets[i] != s) continue;
    sctp_tracked_sockets[i] = sctp_tracked_sockets[sctp_tracked_socket_count - 1];
    sctp_tracked_socket_count--;
    return;
  }
}

static void sctp_stash_accepted(jdll_sctp_socket* ls, struct socket* cso) {
  if(!ls || !cso || ls->pending_accept) return;
  jdll_sctp_socket* conn = sctp_socket_new(cso, 0);
  if(!conn) {
    usrsctp_close(cso);
    return;
  }
  ls->pending_accept = conn;
  sctp_trace("sctp_stash: listener=%p conn=%p", (void*)ls, (void*)conn);
  sctp_signal_poll(ls);
}

static void sctp_probe_sockets(void) {
  sctp_probe_listeners();
  for(int i = 0; i < sctp_tracked_socket_count; i++) {
    jdll_sctp_socket* s = sctp_tracked_sockets[i];
    if(!s || !s->so || s->listening) continue;
    if(s->pending_head) {
      sctp_signal_poll(s);
      continue;
    }
    int events = usrsctp_get_events(s->so);
    if(events < 0) continue;
    if(events & (SCTP_EVENT_READ | SCTP_EVENT_WRITE)) sctp_signal_poll(s);
  }
}

static void sctp_probe_listeners(void) {
  for(int i = 0; i < sctp_listen_socket_count; i++) {
    jdll_sctp_socket* s = sctp_listen_sockets[i];
    if(!s || !s->so) continue;
    if(s->pending_accept) {
      sctp_signal_poll(s);
      continue;
    }
    jello_sctp_pump_timers();
    struct socket* cso = usrsctp_accept(s->so, NULL, NULL);
    if(cso) sctp_stash_accepted(s, cso);
    int events = usrsctp_get_events(s->so);
    if(events < 0) continue;
    if(events & (SCTP_EVENT_READ | SCTP_EVENT_WRITE)) sctp_signal_poll(s);
  }
}

static void sctp_stash_listener_accepts(void) {
  jello_sctp_pump_timers();
  usrsctp_handle_timers(10);
  for(int i = 0; i < sctp_listen_socket_count; i++) {
    jdll_sctp_socket* ls = sctp_listen_sockets[i];
    if(!ls || !ls->so || ls->pending_accept) continue;
    struct socket* cso = usrsctp_accept(ls->so, NULL, NULL);
    if(cso) {
      sctp_stash_accepted(ls, cso);
    } else {
      sctp_trace("sctp_stash: listener=%p accept failed errno=%d", (void*)ls, io_errno_value());
    }
  }
}
#endif

void jello_sctp_pump_timers(void) {
  if(!sctp_inited) return;
  static int64_t last_ms = 0;
  int64_t now = sctp_now_ms();
  if(last_ms == 0) {
    last_ms = now;
    return;
  }
  int64_t delta = now - last_ms;
  if(delta <= 0) return;
  if(delta > 60000) delta = 60000;
  usrsctp_handle_timers((uint32_t)delta);
  last_ms = now;
#if defined(_WIN32)
  sctp_probe_sockets();
#endif
}

static int sctp_connect_in_progress(int err) {
  if(err == 0) return 0;
#if defined(_WIN32)
  return err == EINPROGRESS || err == EWOULDBLOCK || err == WSAEINPROGRESS || err == WSAEWOULDBLOCK;
#else
  return err == EINPROGRESS || err == EWOULDBLOCK;
#endif
}

static int sctp_wait_connected(jdll_sctp_socket* s, int timeout_ms) {
  if(!s || !s->so || timeout_ms <= 0) return 0;
  if(timeout_ms < 0) timeout_ms = 10000;
  int64_t deadline = sctp_now_ms() + timeout_ms;
  int64_t last_timer = sctp_now_ms();
  for(;;) {
    int64_t now = sctp_now_ms();
    int elapsed = (int)(now - last_timer);
    if(elapsed > 0) {
      usrsctp_handle_timers((uint32_t)elapsed);
#if defined(_WIN32)
      sctp_probe_sockets();
#endif
      last_timer = now;
    }
    int remain = (int)(deadline - now);
    if(remain <= 0) return -1;

    int events = usrsctp_get_events(s->so);
    if(events < 0) return 0;
    if(events & SCTP_EVENT_ERROR) return 0;
    if(events & SCTP_EVENT_WRITE) return 1;

    int err = 0;
    socklen_t elen = (socklen_t)sizeof err;
    if(usrsctp_getsockopt(s->so, SOL_SOCKET, SO_ERROR, (char*)&err, &elen) == 0) {
      if(err != 0 && !sctp_connect_in_progress(err)) {
#if defined(_WIN32)
        WSASetLastError(err);
#else
        errno = err;
#endif
        return 0;
      }
    }

    int slice = remain > 10 ? 10 : remain;
#if defined(_WIN32)
    if(s->poll_rfd >= 0) (void)win_poll_fd(s->poll_rfd, 1, slice);
    else Sleep((DWORD)slice);
#else
    if(s->poll_rfd >= 0) {
      struct pollfd pfd = { .fd = s->poll_rfd, .events = POLLIN };
      (void)poll(&pfd, 1, slice);
    } else {
      struct timespec ts = { .tv_sec = slice / 1000, .tv_nsec = (long)(slice % 1000) * 1000000L };
      (void)nanosleep(&ts, NULL);
    }
#endif
    sctp_drain_poll(s);
  }
}

static uint32_t sctp_obj_type_id(const jello_bc_module* m) {
  if(!m) return 0;
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_OBJECT) return i;
  }
  return 0;
}

static uint32_t sctp_atom_id(const jello_bc_module* m, const char* name) {
  return vm_module_atom_id_or_default(m, name, 0);
}

static uint32_t sctp_atom_id_ctx(jdlo_ctx* c, const char* name) {
  struct jello_vm* vm = jdl_ctx_vm(c);
  if(vm && vm->running_module) {
    uint32_t aid = sctp_atom_id(vm->running_module, name);
    if(aid) return aid;
  }
  return sctp_atom_id(jdl_ctx_module(c), name);
}

static jello_object* sctp_make_record(jdlo_ctx* c, uint32_t nfields, const char** keys, jello_value* vals) {
  struct jello_vm* vm = jdl_ctx_vm(c);
  const jello_bc_module* m = jdl_ctx_module(c);
  if(!vm || !m) return NULL;
  jello_object* o = jello_object_new(vm, sctp_obj_type_id(m));
  if(!o) return NULL;
  for(uint32_t i = 0; i < nfields; i++) {
    uint32_t aid = sctp_atom_id_ctx(c, keys[i]);
    if(aid) jello_object_set(o, aid, vals[i]);
  }
  return o;
}

void jdll_std_sctp_set_encaps_port(jdlo_ctx* c) {
  int32_t port = jdl_arg_i32(c, 0);
  if(port <= 0 || port > 65535) {
    jdl_fail(c, "sctp_set_encaps_port: bad port");
    return;
  }
  if(sctp_inited) {
    if(sctp_encaps_port == (int)port) {
      jdl_return_bool(c, 1);
      return;
    }
    jdl_fail(c, "sctp_set_encaps_port: already initialized");
    return;
  }
  sctp_encaps_port = (int)port;
  jdl_return_bool(c, 1);
}

static void sctp_connect_impl(jdlo_ctx* c, jdll_net_addr* addr, int32_t timeout_ms) {
  sctp_ensure_init();
#if defined(_WIN32)
  if(!sctp_inited) {
    sctp_io_fail(c, "sctp_connect: init");
    return;
  }
#endif
  if(!addr) {
    jdl_fail(c, "sctp_connect: bad address");
    return;
  }
#if defined(_WIN32)
  if(!sctp_addr_is_loopback(addr)) {
    jdl_fail(c, "sctp_connect: Windows supports loopback only");
    return;
  }
  int target_port = addr->port;
  if(target_port <= 0) {
    jdl_fail(c, "sctp_connect: bad address");
    return;
  }
  jdll_sctp_socket* s = sctp_socket_new_win(0);
  if(!s) {
    sctp_io_fail(c, "sctp_connect");
    return;
  }
  if(sctp_win_bind_conn(sctp_win_client_ulp(), s->so, target_port) != 0) {
    sctp_io_fail(c, "sctp_connect: bind");
    sctp_socket_fin(s);
    return;
  }
  usrsctp_set_non_blocking(s->so, 0);
  errno = 0;
  WSASetLastError(0);
  int rc = sctp_win_connect_conn(sctp_win_client_ulp(), s->so, target_port);
  usrsctp_set_non_blocking(s->so, 1);
  if(rc != 0) {
    int err = io_errno_value();
    if(err == 0) err = io_winsock_errno();
    io_fail_errno(c, "sctp_connect", err != 0 ? err : 1);
    sctp_socket_fin(s);
    return;
  }
  sctp_stash_listener_accepts();
  jdl_return_abstract(c, s, sctp_socket_fin);
#else
  struct sockaddr_storage ss = addr->ss;
  socklen_t len = addr->len;
  sctp_prepare_sockaddr(&ss, &len);
  struct socket* so = sctp_open_socket(ss.ss_family);
  if(!so) {
    sctp_io_fail(c, "sctp_connect");
    return;
  }
  jdll_sctp_socket* s = sctp_socket_new(so, 0);
  if(!s) {
    jdl_fail(c, "sctp_connect: oom");
    return;
  }
  int rc = usrsctp_connect(s->so, (struct sockaddr*)&ss, len);
  if(rc < 0) {
    int err = io_errno_value();
    if(err != 0 && !sctp_last_would_block()) {
      io_fail_errno(c, "sctp_connect", err);
      sctp_socket_fin(s);
      return;
    }
  }
  {
    int wr = sctp_wait_connected(s, timeout_ms < 0 ? 10000 : (int)timeout_ms);
    if(wr != 1) {
      if(wr < 0) io_fail_code(c, "sctp_connect", "ETIMEDOUT");
      else sctp_io_fail(c, "sctp_connect");
      sctp_socket_fin(s);
      return;
    }
  }
  jdl_return_abstract(c, s, sctp_socket_fin);
#endif
}

void jdll_std_sctp_bind(jdlo_ctx* c) {
  sctp_ensure_init();
#if defined(_WIN32)
  if(!sctp_inited) {
    sctp_io_fail(c, "sctp_bind: init");
    return;
  }
#endif
  jdll_net_addr* addr = net_addr_from_arg(c, 0);
  if(!addr) {
    jdl_fail(c, "sctp_bind: bad address");
    return;
  }
#if defined(_WIN32)
  if(!sctp_addr_is_loopback(addr) && addr->port != 0) {
    /* Ephemeral bind on 0.0.0.0 is used by port probe; loopback tests use 127.0.0.1. */
    struct sockaddr_in* sin = (struct sockaddr_in*)&addr->ss;
    if(sin->sin_addr.s_addr != htonl(INADDR_ANY)) {
      jdl_fail(c, "sctp_bind: Windows supports loopback only");
      return;
    }
  }
  jdll_sctp_socket* s = sctp_socket_new_win(1);
  if(!s) {
    sctp_io_fail(c, "sctp_bind: socket");
    return;
  }
  int port = addr->port;
  int bound = 0;
  for(int attempt = 0; attempt < 64 && !bound; attempt++) {
    if(port == 0) port = sctp_pick_ephemeral_port();
    if(sctp_win_bind_conn(sctp_win_server_ulp(), s->so, port) == 0) {
      bound = 1;
      s->bound_port = port;
      break;
    }
    port = 0;
  }
  if(!bound) {
    sctp_io_fail(c, "sctp_bind: bind");
    sctp_socket_fin(s);
    return;
  }
  if(usrsctp_listen(s->so, 128) != 0) {
    sctp_io_fail(c, "sctp_bind: listen");
    sctp_socket_fin(s);
    return;
  }
  sctp_track_listener(s);
  jdl_return_abstract(c, s, sctp_socket_fin);
#else
  struct sockaddr_storage ss = addr->ss;
  socklen_t len = addr->len;
  sctp_prepare_sockaddr(&ss, &len);
  struct socket* so = sctp_open_socket(ss.ss_family);
  if(!so) {
    sctp_io_fail(c, "sctp_bind: socket");
    return;
  }
  jdll_sctp_socket* s = sctp_socket_new(so, 1);
  if(!s) {
    jdl_fail(c, "sctp_bind: oom");
    return;
  }
  int port = addr->port;
  int bound = 0;
  for(int attempt = 0; attempt < 64 && !bound; attempt++) {
    if(port == 0) port = sctp_pick_ephemeral_port();
    sctp_set_port(&ss, &len, port);
    if(usrsctp_bind(s->so, (struct sockaddr*)&ss, len) == 0) {
      bound = 1;
      s->bound_port = port;
      break;
    }
    port = 0;
  }
  if(!bound) {
    sctp_io_fail(c, "sctp_bind: bind");
    sctp_socket_fin(s);
    return;
  }
  if(usrsctp_listen(s->so, 128) != 0) {
    sctp_io_fail(c, "sctp_bind: listen");
    sctp_socket_fin(s);
    return;
  }
  jdl_return_abstract(c, s, sctp_socket_fin);
#endif
}

void jdll_std_sctp_connect(jdlo_ctx* c) {
  jdll_net_addr* addr = net_addr_from_arg(c, 0);
  sctp_connect_impl(c, addr, -1);
}

void jdll_std_sctp_connect_timeout(jdlo_ctx* c) {
  jdll_net_addr* addr = net_addr_from_arg(c, 0);
  int32_t timeout_ms = jdl_arg_i32(c, 1);
  sctp_connect_impl(c, addr, timeout_ms);
}

void jdll_std_sctp_try_accept(jdlo_ctx* c) {
  jdll_sctp_socket* sock = sctp_socket_from_arg(c, 0);
  if(!sock || !sock->so || !sock->listening) {
    jdl_fail(c, "sctp_try_accept: bad listener");
    return;
  }
  sctp_trace("sctp_try_accept: enter listener=%p pending=%p", (void*)sock, (void*)sock->pending_accept);
#if defined(_WIN32)
  if(sock->pending_accept) {
    jdll_sctp_socket* client = sock->pending_accept;
    sock->pending_accept = NULL;
    sctp_drain_poll(sock);
    sctp_trace("sctp_try_accept: using pending conn=%p listener=%p", (void*)client, (void*)sock);
    jdl_return_abstract(c, client, sctp_socket_fin);
    return;
  }
#endif
  struct socket* cso = NULL;
  for(int attempt = 0; attempt < 64; attempt++) {
    jello_sctp_pump_timers();
    cso = usrsctp_accept(sock->so, NULL, NULL);
    if(cso) break;
    if(!sctp_last_would_block()) {
      sctp_io_fail(c, "sctp_try_accept");
      return;
    }
#if defined(_WIN32)
    int events = usrsctp_get_events(sock->so);
    if(events & (SCTP_EVENT_READ | SCTP_EVENT_WRITE)) break;
#endif
    usrsctp_handle_timers(10);
  }
  sctp_drain_poll(sock);
  if(!cso) {
    if(sctp_last_would_block()) {
      jdl_return_null(c);
      return;
    }
    sctp_io_fail(c, "sctp_try_accept");
    return;
  }
  jdll_sctp_socket* client = sctp_socket_new(cso, 0);
  if(!client) {
    jdl_fail(c, "sctp_try_accept: oom");
    return;
  }
  jdl_return_abstract(c, client, sctp_socket_fin);
}

void jdll_std_sctp_try_recv(jdlo_ctx* c) {
  jdll_sctp_socket* sock = sctp_socket_from_arg(c, 0);
  int32_t max = jdl_arg_i32(c, 1);
  if(!sock || !sock->so || sock->listening || max <= 0) {
    jdl_fail(c, "sctp_try_recv: bad args");
    return;
  }
  if(max > 65535) max = 65535;
#if defined(_WIN32)
  jello_sctp_pump_timers();
  sctp_pending_pkt* pkt = sock->pending_head;
  if(!pkt) {
    sctp_probe_sockets();
    pkt = sock->pending_head;
  }
  if(!pkt) {
    sctp_trace("sctp_try_recv: empty sock=%p listening=%d", (void*)sock, sock->listening);
    sctp_drain_poll(sock);
    jdl_return_null(c);
    return;
  }
  sctp_trace("sctp_try_recv: got pkt len=%zu sock=%p", pkt->len, (void*)sock);
  sock->pending_head = pkt->next;
  if(!sock->pending_head) sock->pending_tail = NULL;
  sctp_drain_poll(sock);
  if(pkt->msg_flags & MSG_NOTIFICATION) {
    free(pkt->data);
    free(pkt);
    jdl_return_null(c);
    return;
  }
  if((size_t)pkt->len > (size_t)max) {
    free(pkt->data);
    free(pkt);
    jdl_fail(c, "sctp_try_recv: buffer too small");
    return;
  }
  struct jello_vm* vm = jdl_ctx_vm(c);
  const jello_bc_module* m = jdl_ctx_module(c);
  if(!vm || !m) {
    free(pkt->data);
    free(pkt);
    jdl_fail(c, "sctp_try_recv: no vm");
    return;
  }
  uint32_t bytes_tid = 0;
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_BYTES) {
      bytes_tid = i;
      break;
    }
  }
  jello_bytes* payload_b = jello_bytes_new(vm, bytes_tid, (uint32_t)pkt->len);
  if(!payload_b) {
    free(pkt->data);
    free(pkt);
    jdl_fail(c, "sctp_try_recv: oom");
    return;
  }
  memcpy(payload_b->data, pkt->data, pkt->len);
  int32_t sid = (int32_t)pkt->rcv.rcv_sid;
  int32_t ppid_host = (int32_t)ntohl((uint32_t)pkt->rcv.rcv_ppid);
  free(pkt->data);
  free(pkt);
  jello_value payload = jello_from_ptr(payload_b);
  jello_value stream = jello_make_i32(sid);
  jello_value ppid = jello_make_i32(ppid_host);
  const char* keys[] = { "payload", "stream", "ppid" };
  jello_value vals[] = { payload, stream, ppid };
  jello_object* rec = sctp_make_record(c, 3, keys, vals);
  if(!rec) {
    jdl_fail(c, "sctp_try_recv: oom");
    return;
  }
  jdl_return_object(c, rec);
  return;
#else
  uint8_t* buf = (uint8_t*)malloc((size_t)max);
  if(!buf) {
    jdl_fail(c, "sctp_try_recv: oom");
    return;
  }
  struct sctp_rcvinfo rcv;
  unsigned int infotype = 0;
  socklen_t infolen = (socklen_t)sizeof rcv;
  int msg_flags = 0;
  ssize_t n = -1;
  for(int attempt = 0; attempt < 64; attempt++) {
    jello_sctp_pump_timers();
    infotype = 0;
    infolen = (socklen_t)sizeof rcv;
    msg_flags = 0;
    n = usrsctp_recvv(sock->so, buf, (size_t)max, NULL, NULL, &rcv, &infolen, &infotype, &msg_flags);
    if(n >= 0) break;
    if(!sctp_last_would_block()) {
      free(buf);
      sctp_io_fail(c, "sctp_try_recv");
      return;
    }
    usrsctp_handle_timers(10);
  }
  sctp_drain_poll(sock);
  if(n < 0) {
    free(buf);
    if(sctp_last_would_block()) {
      jdl_return_null(c);
      return;
    }
    sctp_io_fail(c, "sctp_try_recv");
    return;
  }
  if(msg_flags & MSG_NOTIFICATION) {
    free(buf);
    jdl_return_null(c);
    return;
  }
  int32_t sid = 0;
  int32_t ppid_host = 0;
  if(infotype == SCTP_RECVV_RCVINFO) {
    sid = (int32_t)rcv.rcv_sid;
    ppid_host = (int32_t)ntohl((uint32_t)rcv.rcv_ppid);
  }
  struct jello_vm* vm = jdl_ctx_vm(c);
  const jello_bc_module* m = jdl_ctx_module(c);
  if(!vm || !m) {
    free(buf);
    jdl_fail(c, "sctp_try_recv: no vm");
    return;
  }
  uint32_t bytes_tid = 0;
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_BYTES) {
      bytes_tid = i;
      break;
    }
  }
  jello_bytes* payload_b = jello_bytes_new(vm, bytes_tid, (uint32_t)n);
  if(!payload_b) {
    free(buf);
    jdl_fail(c, "sctp_try_recv: oom");
    return;
  }
  memcpy(payload_b->data, buf, (size_t)n);
  free(buf);
  jello_value payload = jello_from_ptr(payload_b);
  jello_value stream = jello_make_i32(sid);
  jello_value ppid = jello_make_i32(ppid_host);
  const char* keys[] = { "payload", "stream", "ppid" };
  jello_value vals[] = { payload, stream, ppid };
  jello_object* rec = sctp_make_record(c, 3, keys, vals);
  if(!rec) {
    jdl_fail(c, "sctp_try_recv: oom");
    return;
  }
  jdl_return_object(c, rec);
#endif
}

void jdll_std_sctp_try_send(jdlo_ctx* c) {
  jdll_sctp_socket* sock = sctp_socket_from_arg(c, 0);
  const uint8_t* data = jdl_arg_bytes_data(c, 1);
  uint32_t len = jdl_arg_bytes_len(c, 1);
  int32_t stream = jdl_arg_i32(c, 2);
  int32_t ppid = jdl_arg_i32(c, 3);
  if(!sock || !sock->so || sock->listening || !data) {
    jdl_fail(c, "sctp_try_send: bad args");
    return;
  }
  jello_sctp_pump_timers();
  struct sctp_sndinfo snd;
  memset(&snd, 0, sizeof snd);
  snd.snd_sid = (uint16_t)stream;
  snd.snd_ppid = htonl((uint32_t)ppid);
  snd.snd_flags = SCTP_EOR;
  ssize_t n = usrsctp_sendv(sock->so, data, len, NULL, 0, &snd, (socklen_t)sizeof snd, SCTP_SENDV_SNDINFO, 0);
  if(n < 0 && sctp_last_would_block()) {
    usrsctp_handle_timers(10);
    jello_sctp_pump_timers();
    n = usrsctp_sendv(sock->so, data, len, NULL, 0, &snd, (socklen_t)sizeof snd, SCTP_SENDV_SNDINFO, 0);
  }
  if(n < 0) {
    if(sctp_last_would_block()) {
      jdl_return_i32(c, 0);
      return;
    }
    sctp_io_fail(c, "sctp_try_send");
    return;
  }
#if defined(_WIN32)
  sctp_probe_sockets();
  sctp_trace("sctp_try_send: n=%zd sock=%p errno=%d", n, (void*)sock, io_errno_value());
#endif
  jdl_return_i32(c, (int32_t)n);
}

void jdll_std_sctp_poll_fd(jdlo_ctx* c) {
  jdll_sctp_socket* sock = sctp_socket_from_arg(c, 0);
  if(!sock || sock->poll_rfd < 0) {
    jdl_fail(c, "sctp_poll_fd: bad socket");
    return;
  }
  jdl_return_i32(c, sock->poll_rfd);
}

void jdll_std_sctp_close(jdlo_ctx* c) {
  jdl_close_abstract(c, 0);
  jdl_return_bool(c, 1);
}

void jdll_std_sctp_bound_port(jdlo_ctx* c) {
  jdll_sctp_socket* sock = sctp_socket_from_arg(c, 0);
  if(!sock || !sock->so || sock->bound_port <= 0) {
    jdl_fail(c, "sctp_bound_port: bad socket");
    return;
  }
  jdl_return_i32(c, (int32_t)sock->bound_port);
}
