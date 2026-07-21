// SPDX-License-Identifier: BSD-3-Clause

#include <jello.h>
#include <jello/internal/vm_internal.h>
#include <jello/jdll.h>

#include "net_internal.h"
#include "io_common.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <poll.h>
#endif

#include <usrsctp.h>

#if defined(_WIN32)
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

typedef struct jdll_sctp_socket {
  struct socket* so;
  int poll_rfd;
  int poll_wfd;
  int listening;
  int bound_port;
} jdll_sctp_socket;

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

static int sctp_ensure_wsa(void) {
  if(sctp_wsa_inited) return 1;
  WSADATA wsa;
  if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
  sctp_wsa_inited = 1;
  return 1;
}
#endif

static int sctp_inited = 0;

static void sctp_ensure_init(void) {
  if(sctp_inited) return;
#if defined(_WIN32)
  (void)sctp_ensure_wsa();
#endif
  usrsctp_init((uint16_t)sctp_encaps_port, NULL, NULL);
  usrsctp_sysctl_set_sctp_blackhole(2);
  usrsctp_sysctl_set_sctp_no_csum_on_loopback(0);
  sctp_inited = 1;
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
  struct socket* so = usrsctp_socket(family, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL);
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
  int e = io_last_errno();
#if defined(_WIN32)
  return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
  return e == EAGAIN || e == EWOULDBLOCK || e == EINPROGRESS;
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
  char buf[64];
  for(;;) {
#if defined(_WIN32)
    int n = _read(s->poll_rfd, buf, (unsigned)sizeof buf);
#else
    ssize_t n = read(s->poll_rfd, buf, sizeof buf);
#endif
    if(n <= 0) break;
  }
}

static void sctp_signal_poll(jdll_sctp_socket* s) {
  if(!s || s->poll_wfd < 0) return;
  char b = 1;
#if defined(_WIN32)
  (void)_write(s->poll_wfd, &b, 1);
#else
  (void)write(s->poll_wfd, &b, 1);
#endif
}

static void sctp_upcall(struct socket* so, void* arg, int flags) {
  (void)so;
  (void)flags;
  sctp_signal_poll((jdll_sctp_socket*)arg);
}

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
  sctp_set_remote_encaps(so);
  sctp_enable_rcvinfo(so);
  usrsctp_set_upcall(so, sctp_upcall, s);
  return s;
}

static void sctp_socket_fin(void* payload) {
  jdll_sctp_socket* s = (jdll_sctp_socket*)payload;
  if(!s) return;
  if(s->so) {
    usrsctp_set_upcall(s->so, NULL, NULL);
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

static int sctp_wait_so_error(jdll_sctp_socket* s, int timeout_ms) {
  if(!s || !s->so || timeout_ms <= 0) return 0;
  if(timeout_ms < 0) timeout_ms = 10000;
  int64_t deadline = sctp_now_ms() + timeout_ms;
  for(;;) {
    int remain = (int)(deadline - sctp_now_ms());
    if(remain <= 0) return -1;
#if defined(_WIN32)
    if(win_poll_fd(s->poll_rfd, 1, remain) <= 0) continue;
#else
    struct pollfd pfd = { .fd = s->poll_rfd, .events = POLLIN };
    int pr = poll(&pfd, 1, remain);
    if(pr < 0) {
      if(errno == EINTR) continue;
      return 0;
    }
    if(pr == 0) continue;
#endif
    sctp_drain_poll(s);
    int err = 0;
    socklen_t elen = (socklen_t)sizeof err;
    if(usrsctp_getsockopt(s->so, SOL_SOCKET, SO_ERROR, &err, &elen) != 0) return 0;
    if(err == 0) return 1;
#if defined(_WIN32)
    if(err != WSAEINPROGRESS) return 0;
#else
    if(err != EINPROGRESS) return 0;
#endif
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
  if(!addr) {
    jdl_fail(c, "sctp_connect: bad address");
    return;
  }
  struct sockaddr_storage ss = addr->ss;
  socklen_t len = addr->len;
  sctp_prepare_sockaddr(&ss, &len);
  struct socket* so = usrsctp_socket(ss.ss_family, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL);
  if(!so) {
    io_fail(c, "sctp_connect");
    return;
  }
  jdll_sctp_socket* s = sctp_socket_new(so, 0);
  if(!s) {
    jdl_fail(c, "sctp_connect: oom");
    return;
  }
  int rc = usrsctp_connect(s->so, (struct sockaddr*)&ss, len);
  if(rc < 0 && !sctp_last_would_block()) {
    io_fail(c, "sctp_connect");
    sctp_socket_fin(s);
    return;
  }
  if(rc < 0) {
    int wr = sctp_wait_so_error(s, timeout_ms < 0 ? 10000 : (int)timeout_ms);
    if(wr != 1) {
      if(wr < 0) io_fail_code(c, "sctp_connect", "ETIMEDOUT");
      else io_fail(c, "sctp_connect");
      sctp_socket_fin(s);
      return;
    }
  }
  jdl_return_abstract(c, s, sctp_socket_fin);
}

void jdll_std_sctp_bind(jdlo_ctx* c) {
  sctp_ensure_init();
  jdll_net_addr* addr = net_addr_from_arg(c, 0);
  if(!addr) {
    jdl_fail(c, "sctp_bind: bad address");
    return;
  }
  struct sockaddr_storage ss = addr->ss;
  socklen_t len = addr->len;
  sctp_prepare_sockaddr(&ss, &len);
  struct socket* so = usrsctp_socket(ss.ss_family, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, NULL);
  if(!so) {
    io_fail(c, "sctp_bind");
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
    io_fail(c, "sctp_bind");
    sctp_socket_fin(s);
    return;
  }
  if(usrsctp_listen(s->so, 128) != 0) {
    io_fail(c, "sctp_bind");
    sctp_socket_fin(s);
    return;
  }
  jdl_return_abstract(c, s, sctp_socket_fin);
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
  sctp_drain_poll(sock);
  struct socket* cso = usrsctp_accept(sock->so, NULL, NULL);
  if(!cso) {
    if(sctp_last_would_block()) {
      jdl_return_null(c);
      return;
    }
    io_fail(c, "sctp_try_accept");
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
  sctp_drain_poll(sock);
  uint8_t* buf = (uint8_t*)malloc((size_t)max);
  if(!buf) {
    jdl_fail(c, "sctp_try_recv: oom");
    return;
  }
  struct sctp_rcvinfo rcv;
  unsigned int infotype = 0;
  socklen_t infolen = (socklen_t)sizeof rcv;
  int msg_flags = 0;
  ssize_t n = usrsctp_recvv(sock->so, buf, (size_t)max, NULL, NULL, &rcv, &infolen, &infotype, &msg_flags);
  if(n < 0) {
    free(buf);
    if(sctp_last_would_block()) {
      jdl_return_null(c);
      return;
    }
    io_fail(c, "sctp_try_recv");
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
  struct sctp_sndinfo snd;
  memset(&snd, 0, sizeof snd);
  snd.snd_sid = (uint16_t)stream;
  snd.snd_ppid = htonl((uint32_t)ppid);
  snd.snd_flags = SCTP_EOR;
  ssize_t n = usrsctp_sendv(sock->so, data, len, NULL, 0, &snd, (socklen_t)sizeof snd, SCTP_SENDV_SNDINFO, 0);
  if(n < 0) {
    if(sctp_last_would_block()) {
      jdl_return_i32(c, 0);
      return;
    }
    io_fail(c, "sctp_try_send");
    return;
  }
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
