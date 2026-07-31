// SPDX-License-Identifier: BSD-3-Clause

#include <jello.h>
#include <jello/jdll.h>

#include "net_internal.h"
#include "tls_internal.h"
#include "io_common.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <errno.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <unistd.h>
#endif

JDLL_DEFINE_KIND(tls_stream);

static jdll_tls_context* tls_context_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_tls_context*)a->payload;
}

static jdll_net_socket* net_socket_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_net_socket*)a->payload;
}

static jdll_net_addr* net_addr_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_net_addr*)a->payload;
}

static jdll_tls_stream* tls_stream_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_tls_stream*)a->payload;
}

static void tls_close_owned_fd(jdll_tls_stream* ts) {
  if(!ts || !ts->owns_fd || ts->fd < 0) return;
#if defined(_WIN32)
  closesocket((SOCKET)ts->fd);
#else
  close(ts->fd);
#endif
  ts->fd = -1;
}

static void tls_stream_fin(void* payload) {
  jdll_tls_stream* ts = (jdll_tls_stream*)payload;
  if(!ts) return;
  if(ts->ssl) {
    SSL_shutdown(ts->ssl);
    SSL_free(ts->ssl);
    ts->ssl = NULL;
  }
  tls_close_owned_fd(ts);
  free(ts);
}

static void tls_io_fail_ssl(jdlo_ctx* c, const char* op, SSL* ssl, int ret) {
  int err = SSL_get_error(ssl, ret);
  if(err == SSL_ERROR_SYSCALL) {
    if(ret == 0) io_fail_code(c, op, "ECONNRESET");
    else io_fail(c, op);
    return;
  }
  if(err == SSL_ERROR_SSL) {
    unsigned long e = ERR_get_error();
    if(e) io_fail_code(c, op, "SSL_ERROR");
    else io_fail_code(c, op, "SSL_ERROR");
    return;
  }
  io_fail_code(c, op, "SSL_ERROR");
}

static jdll_tls_stream* tls_stream_new(jdll_tls_context* tc, jdll_net_socket* sock, jdll_net_addr* peer) {
  if(!tc || !tc->ctx || !sock || sock->fd < 0) return NULL;
  SSL* ssl = SSL_new(tc->ctx);
  if(!ssl) return NULL;
  SSL_set_fd(ssl, sock->fd);
  if(peer && peer->host[0]) SSL_set1_host(ssl, peer->host);
  else if(tc->sni[0]) SSL_set_tlsext_host_name(ssl, tc->sni);
  if(tc->is_server) SSL_set_accept_state(ssl);
  else SSL_set_connect_state(ssl);

  jdll_tls_stream* ts = (jdll_tls_stream*)calloc(1, sizeof(*ts));
  if(!ts) {
    SSL_free(ssl);
    return NULL;
  }
  ts->ssl = ssl;
  ts->fd = sock->fd;
  ts->is_dtls = tc->is_dtls;
  ts->owns_fd = 1;
  sock->fd = -1;
  return ts;
}

static jdll_tls_stream* tls_wrap(jdlo_ctx* c, int server_side, int is_dtls) {
  jdll_tls_context* tc = tls_context_from_arg(c, 0);
  jdll_net_socket* sock = net_socket_from_arg(c, 1);
  jdll_net_addr* peer = is_dtls && !server_side && jdl_arg_count(c) > 2 ? net_addr_from_arg(c, 2) : NULL;
  if(!tc || !sock || tc->is_dtls != is_dtls) {
    jdl_fail(c, "tls wrap: bad context or socket");
    return NULL;
  }
  if(server_side != tc->is_server) {
    jdl_fail(c, "tls wrap: role mismatch");
    return NULL;
  }
  return tls_stream_new(tc, sock, peer);
}

void jdll_std_tls_stream_client(jdlo_ctx* c) {
  jdll_tls_stream* ts = tls_wrap(c, 0, 0);
  if(!ts) return;
  jdl_return_abstract(c, ts, tls_stream_fin);
}

void jdll_std_tls_stream_server(jdlo_ctx* c) {
  jdll_tls_stream* ts = tls_wrap(c, 1, 0);
  if(!ts) return;
  jdl_return_abstract(c, ts, tls_stream_fin);
}

void jdll_std_dtls_stream_client(jdlo_ctx* c) {
  jdll_tls_stream* ts = tls_wrap(c, 0, 1);
  if(!ts) return;
  jdl_return_abstract(c, ts, tls_stream_fin);
}

void jdll_std_dtls_stream_server(jdlo_ctx* c) {
  jdll_tls_stream* ts = tls_wrap(c, 1, 1);
  if(!ts) return;
  jdl_return_abstract(c, ts, tls_stream_fin);
}

static int tls_drive_handshake(jdll_tls_stream* ts) {
  int rc = SSL_do_handshake(ts->ssl);
  if(rc == 1) return 1;
  int err = SSL_get_error(ts->ssl, rc);
  if(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
  return -1;
}

void jdll_std_tls_handshake(jdlo_ctx* c) {
  jdll_tls_stream* ts = tls_stream_from_arg(c, 0);
  if(!ts || ts->is_dtls) {
    jdl_fail(c, "tls_handshake: bad stream");
    return;
  }
  int rc = tls_drive_handshake(ts);
  if(rc < 0) {
    tls_io_fail_ssl(c, "tls_handshake", ts->ssl, rc);
    return;
  }
  jdl_return_i32(c, rc);
}

void jdll_std_dtls_handshake(jdlo_ctx* c) {
  jdll_tls_stream* ts = tls_stream_from_arg(c, 0);
  if(!ts || !ts->is_dtls) {
    jdl_fail(c, "dtls_handshake: bad stream");
    return;
  }
  int rc = tls_drive_handshake(ts);
  if(rc < 0) {
    tls_io_fail_ssl(c, "dtls_handshake", ts->ssl, rc);
    return;
  }
  jdl_return_i32(c, rc);
}

static void tls_try_read_impl(jdlo_ctx* c, jdll_tls_stream* ts, int32_t max) {
  if(max > 1024 * 1024) max = 1024 * 1024;
  uint8_t* buf = (uint8_t*)malloc((size_t)max);
  if(!buf) {
    jdl_fail(c, "tls_try_read: oom");
    return;
  }
  int n = SSL_read(ts->ssl, buf, (int)max);
  if(n <= 0) {
    free(buf);
    int err = SSL_get_error(ts->ssl, n);
    if(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      jdl_return_null(c);
      return;
    }
    if(n == 0 || err == SSL_ERROR_ZERO_RETURN) {
      jdl_return_bytes_copy(c, NULL, 0);
      return;
    }
    tls_io_fail_ssl(c, "tls_try_read", ts->ssl, n);
    return;
  }
  jdl_return_bytes_copy(c, buf, (uint32_t)n);
  free(buf);
}

void jdll_std_tls_try_read(jdlo_ctx* c) {
  jdll_tls_stream* ts = tls_stream_from_arg(c, 0);
  int32_t max = jdl_arg_i32(c, 1);
  if(!ts || !ts->ssl || ts->is_dtls || max <= 0) {
    jdl_fail(c, "tls_try_read: bad args");
    return;
  }
  tls_try_read_impl(c, ts, max);
}

void jdll_std_dtls_try_read(jdlo_ctx* c) {
  jdll_tls_stream* ts = tls_stream_from_arg(c, 0);
  int32_t max = jdl_arg_i32(c, 1);
  if(!ts || !ts->ssl || !ts->is_dtls || max <= 0) {
    jdl_fail(c, "dtls_try_read: bad args");
    return;
  }
  tls_try_read_impl(c, ts, max);
}

static void tls_try_write_impl(jdlo_ctx* c, jdll_tls_stream* ts, const uint8_t* data, uint32_t len) {
  int n = SSL_write(ts->ssl, data, (int)len);
  if(n <= 0) {
    int err = SSL_get_error(ts->ssl, n);
    if(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      jdl_return_i32(c, 0);
      return;
    }
    tls_io_fail_ssl(c, "tls_try_write", ts->ssl, n);
    return;
  }
  jdl_return_i32(c, (int32_t)n);
}

void jdll_std_tls_try_write(jdlo_ctx* c) {
  jdll_tls_stream* ts = tls_stream_from_arg(c, 0);
  const uint8_t* data = jdl_arg_bytes_data(c, 1);
  uint32_t len = jdl_arg_bytes_len(c, 1);
  if(!ts || !ts->ssl || ts->is_dtls || !data) {
    jdl_fail(c, "tls_try_write: bad args");
    return;
  }
  tls_try_write_impl(c, ts, data, len);
}

void jdll_std_dtls_try_write(jdlo_ctx* c) {
  jdll_tls_stream* ts = tls_stream_from_arg(c, 0);
  const uint8_t* data = jdl_arg_bytes_data(c, 1);
  uint32_t len = jdl_arg_bytes_len(c, 1);
  if(!ts || !ts->ssl || !ts->is_dtls || !data) {
    jdl_fail(c, "dtls_try_write: bad args");
    return;
  }
  tls_try_write_impl(c, ts, data, len);
}

void jdll_std_tls_poll_fd(jdlo_ctx* c) {
  jdll_tls_stream* ts = tls_stream_from_arg(c, 0);
  if(!ts) {
    jdl_fail(c, "tls_poll_fd: bad stream");
    return;
  }
  jdl_return_i32(c, ts->fd);
}

void jdll_std_dtls_poll_fd(jdlo_ctx* c) {
  jdll_std_tls_poll_fd(c);
}

void jdll_std_tls_close(jdlo_ctx* c) {
  jdl_close_abstract(c, 0);
  jdl_return_bool(c, 1);
}

void jdll_std_dtls_close(jdlo_ctx* c) {
  jdll_std_tls_close(c);
}
