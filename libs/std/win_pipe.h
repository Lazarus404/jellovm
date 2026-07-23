// SPDX-License-Identifier: BSD-3-Clause
// Win32 pipe + poll helpers for std.jdll (task.c, process.c, poll.c).
//
// EventLoop mixes two fd kinds on Windows:
//   - CRT pipe fds from wakeup_new (_open_osfhandle + PeekNamedPipe)
//   - Winsock SOCKET values stored as int (UDP/TCP/TLS poll_fd + select)

#ifndef JELLOVM_STD_WIN_PIPE_H
#define JELLOVM_STD_WIN_PIPE_H

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <stdint.h>

static inline int win_close_fd(int* fd) {
  if(!fd || *fd < 0) return 0;
  (void)_close(*fd);
  *fd = -1;
  return 1;
}

static inline int win_pipe_create(int* read_fd, int* write_fd) {
  if(!read_fd || !write_fd) return -1;
  *read_fd = -1;
  *write_fd = -1;

  SECURITY_ATTRIBUTES sa;
  ZeroMemory(&sa, sizeof sa);
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;

  HANDLE read_h = NULL;
  HANDLE write_h = NULL;
  if(!CreatePipe(&read_h, &write_h, &sa, 0)) return -1;

  int read_crt = _open_osfhandle((intptr_t)read_h, _O_RDONLY | _O_BINARY);
  int write_crt = _open_osfhandle((intptr_t)write_h, _O_WRONLY | _O_BINARY);
  if(read_crt < 0 || write_crt < 0) {
    if(read_crt >= 0) (void)_close(read_crt);
    else CloseHandle(read_h);
    if(write_crt >= 0) (void)_close(write_crt);
    else CloseHandle(write_h);
    return -1;
  }

  *read_fd = read_crt;
  *write_fd = write_crt;
  return 0;
}

static inline HANDLE win_fd_handle(int fd) {
  if(fd < 0) return INVALID_HANDLE_VALUE;
  return (HANDLE)_get_osfhandle(fd);
}

static inline int win_ensure_wsa(void) {
  static int inited = 0;
  if(inited) return 1;
  WSADATA wsa;
  if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
  inited = 1;
  return 1;
}

/* Match net.c: SOCKET values are stored in `int` and cast back the same way. */
static inline SOCKET win_fd_as_socket(int fd) {
  return (SOCKET)(UINT_PTR)fd;
}

static inline int win_fd_is_socket(int fd) {
  if(fd < 0 || !win_ensure_wsa()) return 0;
  SOCKET s = win_fd_as_socket(fd);
  int type = 0;
  int len = (int)sizeof(type);
  return getsockopt(s, SOL_SOCKET, SO_TYPE, (char*)&type, &len) == 0;
}

static inline int win_fd_is_crt_pipe(int fd) {
  HANDLE h = win_fd_handle(fd);
  if(h == INVALID_HANDLE_VALUE || h == (HANDLE)-1) return 0;
  return GetFileType(h) == FILE_TYPE_PIPE;
}

static inline int win_poll_socket(SOCKET s, int events, int timeout_ms) {
  if(s == INVALID_SOCKET) return -1;
  if(!win_ensure_wsa()) return -1;

  fd_set rfds;
  fd_set wfds;
  fd_set efds;
  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  FD_ZERO(&efds);
  if(events & 1) FD_SET(s, &rfds);
  if(events & 2) FD_SET(s, &wfds);
  FD_SET(s, &efds);

  struct timeval tv;
  struct timeval* ptv = NULL;
  if(timeout_ms >= 0) {
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ptv = &tv;
  }

  /* nfds is ignored on Windows; first arg may be 0. */
  int rc = select(0, (events & 1) ? &rfds : NULL, (events & 2) ? &wfds : NULL, &efds, ptv);
  if(rc < 0) return -1;
  if(rc == 0) return 0;

  int out = 0;
  if((events & 1) && (FD_ISSET(s, &rfds) || FD_ISSET(s, &efds))) out |= 1;
  if((events & 2) && FD_ISSET(s, &wfds)) out |= 2;
  if(out == 0 && FD_ISSET(s, &efds)) out |= 1;
  return out;
}

static inline int win_poll_pipe(HANDLE h, int events, int timeout_ms) {
  ULONGLONG start = GetTickCount64();
  for(;;) {
    DWORD avail = 0;
    if(!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
      DWORD err = GetLastError();
      /* Closed/broken pipe: treat as readable so callers can drain/observe hangup. */
      if(err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
        return (events & 1) ? 1 : 0;
      }
      return -1;
    }

    int out = 0;
    /* Wakeup registers the pipe *read* end; only POLLIN (data/hangup) is meaningful.
       Do not report POLLOUT — EventLoop asks for POLLIN|POLLOUT and would spin forever. */
    if((events & 1) && avail > 0) out |= 1;
    if(out != 0) return out;

    if(timeout_ms == 0) return 0;
    if(timeout_ms > 0) {
      ULONGLONG now = GetTickCount64();
      if(now - start >= (ULONGLONG)timeout_ms) return 0;
    }
    Sleep(1);
  }
}

static inline int win_poll_fd(int fd, int events, int timeout_ms) {
  if(fd < 0) return -1;

  /* CRT wakeup pipes: valid _get_osfhandle + FILE_TYPE_PIPE.
     Check before sockets so a numeric collision with a SOCKET value still
     treats the CRT fd as a pipe (EventLoop wakeup). */
  if(win_fd_is_crt_pipe(fd)) {
    HANDLE h = win_fd_handle(fd);
    if(h == INVALID_HANDLE_VALUE || h == (HANDLE)-1) return -1;
    return win_poll_pipe(h, events, timeout_ms);
  }

  /* Winsock sockets returned by udp/tcp/tls poll_fd (raw SOCKET stored as int). */
  if(win_fd_is_socket(fd)) {
    return win_poll_socket(win_fd_as_socket(fd), events, timeout_ms);
  }

  return -1;
}

#endif /* _WIN32 */

#endif /* JELLOVM_STD_WIN_PIPE_H */
