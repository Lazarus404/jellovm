// SPDX-License-Identifier: BSD-3-Clause

#include <jello.h>
#include <jello/jdll.h>

#if defined(_WIN32)

#include "win_pipe.h"

#include <stdlib.h>
#include <io.h>

JDLL_DEFINE_KIND(wakeup);

typedef struct jdll_wakeup {
  int read_fd;
  int write_fd;
} jdll_wakeup;

static jdll_wakeup* wakeup_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_wakeup*)a->payload;
}

static void jdll_wakeup_fin(void* payload) {
  jdll_wakeup* w = (jdll_wakeup*)payload;
  if(!w) return;
  win_close_fd(&w->read_fd);
  win_close_fd(&w->write_fd);
  free(w);
}

void jdll_std_poll_fd(jdlo_ctx* c) {
  int fd = jdl_arg_i32(c, 0);
  int events = jdl_arg_i32(c, 1);
  int timeout_ms = jdl_arg_i32(c, 2);
  if(fd < 0) {
    jdl_fail(c, "poll_fd: bad fd");
    return;
  }

  int rc = win_poll_fd(fd, events, timeout_ms);
  if(rc < 0) {
    jdl_fail(c, "poll_fd: poll failed");
    return;
  }
  jdl_return_i32(c, rc);
}

void jdll_std_wakeup_new(jdlo_ctx* c) {
  int read_fd = -1;
  int write_fd = -1;
  if(win_pipe_create(&read_fd, &write_fd) != 0) {
    jdl_fail(c, "wakeup_new: pipe failed");
    return;
  }

  jdll_wakeup* w = (jdll_wakeup*)calloc(1, sizeof(*w));
  if(!w) {
    win_close_fd(&read_fd);
    win_close_fd(&write_fd);
    jdl_fail(c, "wakeup_new: oom");
    return;
  }
  w->read_fd = read_fd;
  w->write_fd = write_fd;
  jdl_return_abstract(c, w, jdll_wakeup_fin);
}

void jdll_std_wakeup_poll_fd(jdlo_ctx* c) {
  jdll_wakeup* w = wakeup_from_arg(c, 0);
  if(!w || w->read_fd < 0) {
    jdl_fail(c, "wakeup_poll_fd: invalid handle");
    return;
  }
  jdl_return_i32(c, w->read_fd);
}

void jdll_std_wakeup_signal(jdlo_ctx* c) {
  jdll_wakeup* w = wakeup_from_arg(c, 0);
  if(!w || w->write_fd < 0) {
    jdl_fail(c, "wakeup_signal: invalid handle");
    return;
  }
  char byte = 1;
  int n = _write(w->write_fd, &byte, 1);
  jdl_return_bool(c, n == 1);
}

void jdll_std_wakeup_drain(jdlo_ctx* c) {
  jdll_wakeup* w = wakeup_from_arg(c, 0);
  if(!w || w->read_fd < 0) {
    jdl_fail(c, "wakeup_drain: invalid handle");
    return;
  }
  char buf[64];
  int n = _read(w->read_fd, buf, (unsigned)sizeof buf);
  if(n < 0) {
    jdl_fail(c, "wakeup_drain: read failed");
    return;
  }
  jdl_return_bool(c, n > 0);
}

#else

#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

JDLL_DEFINE_KIND(wakeup);

typedef struct jdll_wakeup {
  int read_fd;
  int write_fd;
} jdll_wakeup;

static jdll_wakeup* wakeup_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_wakeup*)a->payload;
}

static void jdll_wakeup_fin(void* payload) {
  jdll_wakeup* w = (jdll_wakeup*)payload;
  if(!w) return;
  if(w->read_fd >= 0) close(w->read_fd);
  if(w->write_fd >= 0) close(w->write_fd);
  w->read_fd = -1;
  w->write_fd = -1;
  free(w);
}

void jdll_std_poll_fd(jdlo_ctx* c) {
  int fd = jdl_arg_i32(c, 0);
  int events = jdl_arg_i32(c, 1);
  int timeout_ms = jdl_arg_i32(c, 2);
  if(fd < 0) {
    jdl_fail(c, "poll_fd: bad fd");
    return;
  }

  short ev = 0;
  if(events & 1) ev |= POLLIN;
  if(events & 2) ev |= POLLOUT;
  struct pollfd pfd = {.fd = fd, .events = ev, .revents = 0};
  int rc = poll(&pfd, 1, timeout_ms);
  if(rc < 0) {
    jdl_fail(c, "poll_fd: poll failed");
    return;
  }
  if(rc == 0) {
    jdl_return_i32(c, 0);
    return;
  }

  int out = 0;
  if(pfd.revents & POLLIN) out |= 1;
  if(pfd.revents & POLLOUT) out |= 2;
  if(pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) out |= 4;
  jdl_return_i32(c, out);
}

void jdll_std_wakeup_new(jdlo_ctx* c) {
  int fds[2];
  if(pipe(fds) != 0) {
    jdl_fail(c, "wakeup_new: pipe failed");
    return;
  }

  jdll_wakeup* w = (jdll_wakeup*)calloc(1, sizeof(*w));
  if(!w) {
    close(fds[0]);
    close(fds[1]);
    jdl_fail(c, "wakeup_new: oom");
    return;
  }
  w->read_fd = fds[0];
  w->write_fd = fds[1];
  jdl_return_abstract(c, w, jdll_wakeup_fin);
}

void jdll_std_wakeup_poll_fd(jdlo_ctx* c) {
  jdll_wakeup* w = wakeup_from_arg(c, 0);
  if(!w || w->read_fd < 0) {
    jdl_fail(c, "wakeup_poll_fd: invalid handle");
    return;
  }
  jdl_return_i32(c, w->read_fd);
}

void jdll_std_wakeup_signal(jdlo_ctx* c) {
  jdll_wakeup* w = wakeup_from_arg(c, 0);
  if(!w || w->write_fd < 0) {
    jdl_fail(c, "wakeup_signal: invalid handle");
    return;
  }
  char byte = 1;
  ssize_t n = write(w->write_fd, &byte, 1);
  jdl_return_bool(c, n == 1);
}

void jdll_std_wakeup_drain(jdlo_ctx* c) {
  jdll_wakeup* w = wakeup_from_arg(c, 0);
  if(!w || w->read_fd < 0) {
    jdl_fail(c, "wakeup_drain: invalid handle");
    return;
  }
  char buf[64];
  ssize_t n = read(w->read_fd, buf, sizeof buf);
  if(n < 0) {
    jdl_fail(c, "wakeup_drain: read failed");
    return;
  }
  jdl_return_bool(c, n > 0);
}

#endif /* !_WIN32 */
