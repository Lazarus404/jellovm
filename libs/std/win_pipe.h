// SPDX-License-Identifier: BSD-3-Clause
// Win32 pipe + poll helpers for std.jdll (task.c, process.c).

#ifndef JELLOVM_STD_WIN_PIPE_H
#define JELLOVM_STD_WIN_PIPE_H

#if defined(_WIN32)

#include <fcntl.h>
#include <io.h>
#include <windows.h>

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

static inline int win_poll_fd(int fd, int events, int timeout_ms) {
  if(fd < 0) return -1;

  HANDLE h = win_fd_handle(fd);
  if(h == INVALID_HANDLE_VALUE) return -1;

  ULONGLONG start = GetTickCount64();
  for(;;) {
    DWORD avail = 0;
    if(!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) return -1;

    int out = 0;
    if((events & 1) && avail > 0) out |= 1;
    if(events & 2) out |= 2; /* pipe writes rarely block on small payloads */
    if(out != 0) return out;

    if(timeout_ms == 0) return 0;
    if(timeout_ms > 0) {
      ULONGLONG now = GetTickCount64();
      if(now - start >= (ULONGLONG)timeout_ms) return 0;
    }
    Sleep(1);
  }
}

#endif /* _WIN32 */

#endif /* JELLOVM_STD_WIN_PIPE_H */
