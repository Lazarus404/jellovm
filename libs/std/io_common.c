// SPDX-License-Identifier: BSD-3-Clause

#include "io_common.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <errno.h>
#else
#include <errno.h>
#endif

int io_last_errno(void) {
#if defined(_WIN32)
  return WSAGetLastError();
#else
  return errno;
#endif
}

const char* io_errno_code(int err) {
  switch(err) {
#if defined(_WIN32)
    case WSAEWOULDBLOCK: return "EWOULDBLOCK";
    case WSAEINPROGRESS: return "EINPROGRESS";
    case WSAECONNRESET: return "ECONNRESET";
    case WSAECONNREFUSED: return "ECONNREFUSED";
    case WSAETIMEDOUT: return "ETIMEDOUT";
    case WSAEADDRINUSE: return "EADDRINUSE";
    case EPIPE: return "EPIPE";
    case WSAENOTCONN: return "ENOTCONN";
    case WSAESHUTDOWN: return "ESHUTDOWN";
    case WSAEMSGSIZE: return "EMSGSIZE";
    case WSAEHOSTUNREACH: return "EHOSTUNREACH";
    case WSAENETUNREACH: return "ENETUNREACH";
#else
    case EWOULDBLOCK: return "EWOULDBLOCK";
#if EAGAIN != EWOULDBLOCK
    case EAGAIN: return "EAGAIN";
#endif
    case EINPROGRESS: return "EINPROGRESS";
    case ECONNRESET: return "ECONNRESET";
    case ECONNREFUSED: return "ECONNREFUSED";
    case ETIMEDOUT: return "ETIMEDOUT";
    case EADDRINUSE: return "EADDRINUSE";
    case EPIPE: return "EPIPE";
    case ENOTCONN: return "ENOTCONN";
    case ESHUTDOWN: return "ESHUTDOWN";
    case EMSGSIZE: return "EMSGSIZE";
    case EHOSTUNREACH: return "EHOSTUNREACH";
    case ENETUNREACH: return "ENETUNREACH";
#endif
    default: return "EIO";
  }
}

void io_fail_code(jdlo_ctx* c, const char* op, const char* code) {
  static char msg[128];
  snprintf(msg, sizeof msg, "io:%s %s", code, op);
  jdl_fail(c, msg);
}

void io_fail_errno(jdlo_ctx* c, const char* op, int err) {
  io_fail_code(c, op, io_errno_code(err));
}

void io_fail(jdlo_ctx* c, const char* op) {
  io_fail_errno(c, op, io_last_errno());
}

static void io_copy_bytes_arg(char* dst, size_t cap, jdlo_ctx* c, int index) {
  const uint8_t* data = jdl_arg_bytes_data(c, index);
  uint32_t len = jdl_arg_bytes_len(c, index);
  if(!data || len == 0) {
    if(cap) dst[0] = '\0';
    return;
  }
  if(len >= cap) len = (uint32_t)(cap - 1);
  memcpy(dst, data, len);
  dst[len] = '\0';
}

void jdll_std_io_trap(jdlo_ctx* c) {
  char code[64];
  char op[64];
  io_copy_bytes_arg(code, sizeof code, c, 0);
  io_copy_bytes_arg(op, sizeof op, c, 1);
  if(!code[0] || !op[0]) {
    jdl_fail(c, "io_trap: bad args");
    return;
  }
  io_fail_code(c, op, code);
}
