// SPDX-License-Identifier: BSD-3-Clause

#include <jello.h>
#include <jello/jdll.h>

#include <stdint.h>

#if defined(_WIN32)

#include "win_pipe.h"

void jdll_std_monotonic_ms(jdlo_ctx* c) {
  ULONGLONG ms = GetTickCount64();
  if(ms > (ULONGLONG)INT32_MAX) ms = (ULONGLONG)INT32_MAX;
  jdl_return_i32(c, (int32_t)ms);
}

void jdll_std_poll_many(jdlo_ctx* c) {
  jello_array* fds_arr = jdl_arg_array(c, 0);
  int32_t events = jdl_arg_i32(c, 1);
  int32_t timeout_ms = jdl_arg_i32(c, 2);
  if(!fds_arr) {
    jdl_fail(c, "poll_many: expected array");
    return;
  }

  uint32_t n = jdl_array_len(fds_arr);
  if(n == 0) {
    jdl_return_i32(c, 0);
    return;
  }

  ULONGLONG start = GetTickCount64();
  for(;;) {
    for(uint32_t i = 0; i < n; i++) {
      jello_value v = jdl_array_get(fds_arr, i);
      if(!jello_is_i32(v)) continue;
      int fd = (int)jello_as_i32(v);
      int rc = win_poll_fd(fd, events, 0);
      if(rc < 0) {
        jdl_fail(c, "poll_many: poll failed");
        return;
      }
      if(rc != 0) {
        jdl_return_i32(c, (int32_t)(i + 1));
        return;
      }
    }

    if(timeout_ms == 0) {
      jdl_return_i32(c, 0);
      return;
    }
    if(timeout_ms > 0) {
      ULONGLONG now = GetTickCount64();
      if(now - start >= (ULONGLONG)timeout_ms) {
        jdl_return_i32(c, 0);
        return;
      }
    }
    Sleep(1);
  }
}

#else

#include <errno.h>
#include <poll.h>
#include <time.h>

void jdll_std_monotonic_ms(jdlo_ctx* c) {
  struct timespec ts;
  if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    jdl_fail(c, "monotonic_ms: clock_gettime failed");
    return;
  }
  int64_t ms = (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
  if(ms > INT32_MAX) ms = INT32_MAX;
  jdl_return_i32(c, (int32_t)ms);
}

void jdll_std_poll_many(jdlo_ctx* c) {
  jello_array* fds_arr = jdl_arg_array(c, 0);
  int32_t events = jdl_arg_i32(c, 1);
  int32_t timeout_ms = jdl_arg_i32(c, 2);
  if(!fds_arr) {
    jdl_fail(c, "poll_many: expected array");
    return;
  }

  uint32_t n = jdl_array_len(fds_arr);
  if(n == 0) {
    jdl_return_i32(c, 0);
    return;
  }
  if(n > 256) {
    jdl_fail(c, "poll_many: too many fds");
    return;
  }

  struct pollfd pfds[256];
  short ev = 0;
  if(events & 1) ev |= POLLIN;
  if(events & 2) ev |= POLLOUT;

  for(uint32_t i = 0; i < n; i++) {
    jello_value v = jdl_array_get(fds_arr, i);
    if(!jello_is_i32(v)) {
      jdl_fail(c, "poll_many: fd array must contain I32");
      return;
    }
    pfds[i].fd = (int)jello_as_i32(v);
    pfds[i].events = ev;
    pfds[i].revents = 0;
  }

  int rc = poll(pfds, (nfds_t)n, timeout_ms);
  if(rc < 0) {
    jdl_fail(c, "poll_many: poll failed");
    return;
  }
  if(rc == 0) {
    jdl_return_i32(c, 0);
    return;
  }

  for(uint32_t i = 0; i < n; i++) {
    if(pfds[i].revents & (POLLIN | POLLOUT | POLLERR | POLLHUP | POLLNVAL)) {
      jdl_return_i32(c, (int32_t)(i + 1));
      return;
    }
  }
  jdl_return_i32(c, 0);
}

#endif /* !_WIN32 */
