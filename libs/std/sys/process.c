// SPDX-License-Identifier: BSD-3-Clause

#include <jello.h>
#include <jello/jdll.h>

#if defined(_WIN32)

#include "win_pipe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>

JDLL_DEFINE_KIND(process);

typedef struct jdll_process {
  HANDLE process;
  DWORD pid;
  int stdin_w;
  int stdout_r;
  int stderr_r;
  int waited;
  int exit_code;
} jdll_process;

static int bytes_to_buf(const uint8_t* data, uint32_t len, char* out, size_t cap) {
  if(!out || cap < 2) return 0;
  if(!data || !len) {
    out[0] = 0;
    return 1;
  }
  uint32_t n = len < cap - 1u ? len : (uint32_t)(cap - 1u);
  memcpy(out, data, n);
  out[n] = 0;
  return 1;
}

static int parse_command_argv(const char* cmd, char* buf, size_t cap, char** argv, int max_argv) {
  if(!cmd || !buf || !argv || max_argv < 2) return -1;
  size_t len = strlen(cmd);
  if(len >= cap) return -1;
  memcpy(buf, cmd, len + 1u);
  int argc = 0;
  char* p = buf;
  while(*p && argc < max_argv - 1) {
    while(*p == ' ' || *p == '\t') p++;
    if(!*p) break;
    argv[argc++] = p;
    while(*p && *p != ' ' && *p != '\t') p++;
    if(*p) *p++ = 0;
  }
  argv[argc] = NULL;
  return argc > 0 ? argc : -1;
}

static int build_cmdline(char** argv, char* out, size_t cap) {
  size_t off = 0;
  for(int i = 0; argv[i]; i++) {
    if(i > 0 && off + 1 < cap) out[off++] = ' ';
    const char* arg = argv[i];
    int need_quote = strchr(arg, ' ') != NULL || strchr(arg, '\t') != NULL;
    if(need_quote && off + 1 < cap) out[off++] = '"';
    for(size_t j = 0; arg[j] && off + 1 < cap; j++) out[off++] = arg[j];
    if(need_quote && off + 1 < cap) out[off++] = '"';
  }
  if(off >= cap) return -1;
  out[off] = 0;
  return (int)off;
}

static int win_file_exists(const char* path) {
  if(!path || !*path) return 0;
  DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static int win_try_path(char* out, size_t cap, const char* candidate) {
  if(!candidate || !*candidate || strlen(candidate) >= cap) return 0;
  memcpy(out, candidate, strlen(candidate) + 1);
  if(win_file_exists(out)) return 1;
  size_t n = strlen(out);
  if(n + 4 < cap) {
    memcpy(out + n, ".exe", 5);
    if(win_file_exists(out)) return 1;
  }
  return 0;
}

static int win_try_msys_roots(const char* base, char* out, size_t cap) {
  static const char* prefix_vars[] = {"MSYSTEM_PREFIX", "MINGW_PREFIX", NULL};
  for(int i = 0; prefix_vars[i]; i++) {
    const char* prefix = getenv(prefix_vars[i]);
    if(!prefix || !*prefix) continue;
    char candidate[(MAX_PATH * 2) + 32];
    const char* suffixes[] = {"/usr/bin/", "/bin/", NULL};
    for(int s = 0; suffixes[s]; s++) {
      snprintf(candidate, sizeof candidate, "%s%s%s", prefix, suffixes[s], base);
      if(win_try_path(out, cap, candidate)) return 1;
    }
  }
  return 0;
}

static int win_resolve_argv0(const char* argv0, char* out, size_t cap) {
  if(!argv0 || !*argv0) return 0;

  char* file_part = NULL;
  if(SearchPathA(NULL, argv0, ".exe", (DWORD)cap, out, &file_part)) return 1;

  const char* base = strrchr(argv0, '/');
  if(!base) base = strrchr(argv0, '\\');
  base = base ? base + 1 : argv0;
  if(*base) {
    if(SearchPathA(NULL, base, ".exe", (DWORD)cap, out, &file_part)) return 1;
    if(win_try_msys_roots(base, out, cap)) return 1;
  }

  const char* path_env = getenv("PATH");
  if(!path_env) return 0;

  char path_copy[8192];
  if(strlen(path_env) >= sizeof path_copy) return 0;
  memcpy(path_copy, path_env, strlen(path_env) + 1);

  for(char* tok = path_copy; tok; ) {
    char* colon = strchr(tok, ':');
    if(colon) *colon++ = 0;
    if(*tok) {
      char candidate[(MAX_PATH * 2) + 4];
      size_t tl = strlen(tok);
      int needs_sep = tl > 0 && tok[tl - 1] != '/' && tok[tl - 1] != '\\';
      if(argv0[0] == '/' || argv0[0] == '\\') {
        snprintf(candidate, sizeof candidate, "%s%s%s", tok, needs_sep ? "/" : "", base);
        if(win_try_path(out, cap, candidate)) return 1;
      }
      snprintf(candidate, sizeof candidate, "%s%s%s", tok, needs_sep ? "/" : "", argv0);
      if(win_try_path(out, cap, candidate)) return 1;
    }
    tok = colon;
  }
  return 0;
}

static void jdll_process_fin(void* payload) {
  jdll_process* p = (jdll_process*)payload;
  if(!p) return;
  win_close_fd(&p->stdin_w);
  win_close_fd(&p->stdout_r);
  win_close_fd(&p->stderr_r);
  if(p->process) {
    if(!p->waited) {
      WaitForSingleObject(p->process, INFINITE);
      DWORD code = 0;
      if(GetExitCodeProcess(p->process, &code)) p->exit_code = (int)code;
      p->waited = 1;
    }
    CloseHandle(p->process);
    p->process = NULL;
  }
  free(p);
}

static jdll_process* process_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_process*)a->payload;
}

static int read_fd_to_bytes(jdlo_ctx* c, int fd) {
  if(fd < 0) {
    jdl_return_bytes_copy(c, NULL, 0);
    return 1;
  }
  enum { chunk = 65536 };
  uint8_t* buf = (uint8_t*)malloc(chunk);
  if(!buf) {
    jdl_fail(c, "process_read: oom");
    return 0;
  }
  int n = _read(fd, (void*)buf, chunk);
  if(n < 0) {
    free(buf);
    jdl_fail(c, "process_read: read failed");
    return 0;
  }
  if(n == 0) {
    free(buf);
    jdl_return_bytes_copy(c, NULL, 0);
    return 1;
  }
  jdl_return_bytes_copy(c, buf, (uint32_t)n);
  free(buf);
  return 1;
}

static void close_inherited_handle(HANDLE* h) {
  if(!h || !*h) return;
  CloseHandle(*h);
  *h = NULL;
}

void jdll_std_process_run(jdlo_ctx* c) {
  const uint8_t* data = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  char cmd_buf[4096];
  if(!bytes_to_buf(data, len, cmd_buf, sizeof cmd_buf)) {
    jdl_fail(c, "process_run: bad command");
    return;
  }
  char arg_storage[4096];
  char* argv[64];
  if(parse_command_argv(cmd_buf, arg_storage, sizeof arg_storage, argv, 64) < 1) {
    jdl_fail(c, "process_run: empty command");
    return;
  }
  char exe_path[MAX_PATH];
  char cmdline[8192];
  if(!win_resolve_argv0(argv[0], exe_path, sizeof exe_path)) {
    if(!win_resolve_argv0("bash", exe_path, sizeof exe_path)) {
      jdl_fail(c, "process_run: executable not found");
      return;
    }
    char* shell_argv[4];
    shell_argv[0] = exe_path;
    shell_argv[1] = "-lc";
    shell_argv[2] = cmd_buf;
    shell_argv[3] = NULL;
    if(build_cmdline(shell_argv, cmdline, sizeof cmdline) < 0) {
      jdl_fail(c, "process_run: command too long");
      return;
    }
  } else {
    argv[0] = exe_path;
    if(build_cmdline(argv, cmdline, sizeof cmdline) < 0) {
      jdl_fail(c, "process_run: command too long");
      return;
    }
  }

  SECURITY_ATTRIBUTES sa;
  ZeroMemory(&sa, sizeof sa);
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;

  HANDLE stdin_r = NULL;
  HANDLE stdin_w = NULL;
  HANDLE stdout_r = NULL;
  HANDLE stdout_w = NULL;
  HANDLE stderr_r = NULL;
  HANDLE stderr_w = NULL;
  if(!CreatePipe(&stdin_r, &stdin_w, &sa, 0) ||
     !CreatePipe(&stdout_r, &stdout_w, &sa, 0) ||
     !CreatePipe(&stderr_r, &stderr_w, &sa, 0)) {
    jdl_fail(c, "process_run: pipe failed");
    goto fail_pipes;
  }

  if(!SetHandleInformation(stdin_w, HANDLE_FLAG_INHERIT, 0) ||
     !SetHandleInformation(stdout_r, HANDLE_FLAG_INHERIT, 0) ||
     !SetHandleInformation(stderr_r, HANDLE_FLAG_INHERIT, 0)) {
    jdl_fail(c, "process_run: pipe failed");
    goto fail_pipes;
  }

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof si);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = stdin_r;
  si.hStdOutput = stdout_w;
  si.hStdError = stderr_w;

  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof pi);
  if(!CreateProcessA(exe_path, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
    jdl_fail(c, "process_run: CreateProcess failed");
    goto fail_pipes;
  }

  close_inherited_handle(&stdin_r);
  close_inherited_handle(&stdout_w);
  close_inherited_handle(&stderr_w);

  int stdin_crt = _open_osfhandle((intptr_t)stdin_w, _O_WRONLY | _O_BINARY);
  int stdout_crt = _open_osfhandle((intptr_t)stdout_r, _O_RDONLY | _O_BINARY);
  int stderr_crt = _open_osfhandle((intptr_t)stderr_r, _O_RDONLY | _O_BINARY);
  if(stdin_crt < 0 || stdout_crt < 0 || stderr_crt < 0) {
    jdl_fail(c, "process_run: oom");
    TerminateProcess(pi.hProcess, 127);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    goto fail_pipes;
  }
  CloseHandle(pi.hThread);

  jdll_process* p = (jdll_process*)calloc(1, sizeof(*p));
  if(!p) {
    jdl_fail(c, "process_run: oom");
    TerminateProcess(pi.hProcess, 127);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    win_close_fd(&stdin_crt);
    win_close_fd(&stdout_crt);
    win_close_fd(&stderr_crt);
    return;
  }
  p->process = pi.hProcess;
  p->pid = pi.dwProcessId;
  p->stdin_w = stdin_crt;
  p->stdout_r = stdout_crt;
  p->stderr_r = stderr_crt;
  p->waited = 0;
  p->exit_code = 0;
  jdl_return_abstract(c, p, jdll_process_fin);
  return;

fail_pipes:
  close_inherited_handle(&stdin_r);
  close_inherited_handle(&stdin_w);
  close_inherited_handle(&stdout_r);
  close_inherited_handle(&stdout_w);
  close_inherited_handle(&stderr_r);
  close_inherited_handle(&stderr_w);
}

void jdll_std_process_pid(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p || p->pid == 0) {
    jdl_return_i32(c, 0);
    return;
  }
  jdl_return_i32(c, (int32_t)p->pid);
}

void jdll_std_process_wait(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p || !p->process) {
    jdl_fail(c, "process_wait: invalid handle");
    return;
  }
  if(p->waited) {
    jdl_return_i32(c, (int32_t)p->exit_code);
    return;
  }
  if(WaitForSingleObject(p->process, INFINITE) != WAIT_OBJECT_0) {
    jdl_fail(c, "process_wait: wait failed");
    return;
  }
  DWORD code = 0;
  if(!GetExitCodeProcess(p->process, &code)) {
    jdl_fail(c, "process_wait: GetExitCodeProcess failed");
    return;
  }
  p->waited = 1;
  p->exit_code = (int)code;
  jdl_return_i32(c, (int32_t)p->exit_code);
}

void jdll_std_process_kill(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p || !p->process) {
    jdl_return_bool(c, 0);
    return;
  }
  jdl_return_bool(c, TerminateProcess(p->process, 1));
}

void jdll_std_process_read_stdout(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p) {
    jdl_fail(c, "process_read_stdout: invalid handle");
    return;
  }
  (void)read_fd_to_bytes(c, p->stdout_r);
}

void jdll_std_process_read_stderr(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p) {
    jdl_fail(c, "process_read_stderr: invalid handle");
    return;
  }
  (void)read_fd_to_bytes(c, p->stderr_r);
}

void jdll_std_process_write_stdin(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p || p->stdin_w < 0) {
    jdl_return_bool(c, 0);
    return;
  }
  const uint8_t* data = jdl_arg_bytes_data(c, 1);
  uint32_t len = jdl_arg_bytes_len(c, 1);
  if(len == 0) {
    jdl_return_bool(c, 1);
    return;
  }
  if(!data) {
    jdl_return_bool(c, 0);
    return;
  }
  size_t off = 0;
  while(off < len) {
    int n = _write(p->stdin_w, data + off, (unsigned)(len - (uint32_t)off));
    if(n < 0) {
      jdl_return_bool(c, 0);
      return;
    }
    if(n == 0) {
      jdl_return_bool(c, 0);
      return;
    }
    off += (size_t)n;
  }
  jdl_return_bool(c, 1);
}

void jdll_std_process_close_stdin(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p) {
    jdl_return_bool(c, 0);
    return;
  }
  win_close_fd(&p->stdin_w);
  jdl_return_bool(c, 1);
}

#else

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

JDLL_DEFINE_KIND(process);

typedef struct jdll_process {
  pid_t pid;
  int stdin_w;
  int stdout_r;
  int stderr_r;
  int waited;
  int exit_code;
} jdll_process;

static int bytes_to_buf(const uint8_t* data, uint32_t len, char* out, size_t cap) {
  if(!out || cap < 2) return 0;
  if(!data || !len) {
    out[0] = 0;
    return 1;
  }
  uint32_t n = len < cap - 1u ? len : (uint32_t)(cap - 1u);
  memcpy(out, data, n);
  out[n] = 0;
  return 1;
}

static int parse_command_argv(const char* cmd, char* buf, size_t cap, char** argv, int max_argv) {
  if(!cmd || !buf || !argv || max_argv < 2) return -1;
  size_t len = strlen(cmd);
  if(len >= cap) return -1;
  memcpy(buf, cmd, len + 1u);
  int argc = 0;
  char* p = buf;
  while(*p && argc < max_argv - 1) {
    while(*p == ' ' || *p == '\t') p++;
    if(!*p) break;
    argv[argc++] = p;
    while(*p && *p != ' ' && *p != '\t') p++;
    if(*p) *p++ = 0;
  }
  argv[argc] = NULL;
  return argc > 0 ? argc : -1;
}

static int normalize_exit_status(int status) {
  if(WIFEXITED(status)) return WEXITSTATUS(status);
  if(WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 255;
}

static void close_fd(int* fd) {
  if(!fd || *fd < 0) return;
  close(*fd);
  *fd = -1;
}

static void jdll_process_fin(void* payload) {
  jdll_process* p = (jdll_process*)payload;
  if(!p) return;
  close_fd(&p->stdin_w);
  close_fd(&p->stdout_r);
  close_fd(&p->stderr_r);
  if(p->pid > 0 && !p->waited) {
    int status = 0;
    (void)waitpid(p->pid, &status, 0);
    p->waited = 1;
    p->exit_code = normalize_exit_status(status);
  }
  free(p);
}

static jdll_process* process_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_process*)a->payload;
}

static int read_fd_to_bytes(jdlo_ctx* c, int fd) {
  if(fd < 0) {
    jdl_return_bytes_copy(c, NULL, 0);
    return 1;
  }
  enum { chunk = 65536 };
  uint8_t* buf = (uint8_t*)malloc(chunk);
  if(!buf) {
    jdl_fail(c, "process_read: oom");
    return 0;
  }
  ssize_t n = read(fd, buf, chunk);
  if(n < 0) {
    free(buf);
    jdl_fail(c, "process_read: read failed");
    return 0;
  }
  if(n == 0) {
    free(buf);
    jdl_return_bytes_copy(c, NULL, 0);
    return 1;
  }
  jdl_return_bytes_copy(c, buf, (uint32_t)n);
  free(buf);
  return 1;
}

void jdll_std_process_run(jdlo_ctx* c) {
  const uint8_t* data = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  char cmd_buf[4096];
  if(!bytes_to_buf(data, len, cmd_buf, sizeof cmd_buf)) {
    jdl_fail(c, "process_run: bad command");
    return;
  }
  char arg_storage[4096];
  char* argv[64];
  if(parse_command_argv(cmd_buf, arg_storage, sizeof arg_storage, argv, 64) < 1) {
    jdl_fail(c, "process_run: empty command");
    return;
  }

  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  if(pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
    jdl_fail(c, "process_run: pipe failed");
    goto fail_pipes;
  }

  pid_t pid = fork();
  if(pid < 0) {
    jdl_fail(c, "process_run: fork failed");
    goto fail_pipes;
  }
  if(pid == 0) {
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    if(dup2(stdin_pipe[0], STDIN_FILENO) < 0) _exit(127);
    if(dup2(stdout_pipe[1], STDOUT_FILENO) < 0) _exit(127);
    if(dup2(stderr_pipe[1], STDERR_FILENO) < 0) _exit(127);
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    execvp(argv[0], argv);
    _exit(127);
  }

  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  jdll_process* p = (jdll_process*)calloc(1, sizeof(*p));
  if(!p) {
    jdl_fail(c, "process_run: oom");
    (void)kill(pid, SIGTERM);
    (void)waitpid(pid, NULL, 0);
    goto fail_pipes;
  }
  p->pid = pid;
  p->stdin_w = stdin_pipe[1];
  p->stdout_r = stdout_pipe[0];
  p->stderr_r = stderr_pipe[0];
  p->waited = 0;
  p->exit_code = 0;
  jdl_return_abstract(c, p, jdll_process_fin);
  return;

fail_pipes:
  if(stdin_pipe[0] >= 0) close(stdin_pipe[0]);
  if(stdin_pipe[1] >= 0) close(stdin_pipe[1]);
  if(stdout_pipe[0] >= 0) close(stdout_pipe[0]);
  if(stdout_pipe[1] >= 0) close(stdout_pipe[1]);
  if(stderr_pipe[0] >= 0) close(stderr_pipe[0]);
  if(stderr_pipe[1] >= 0) close(stderr_pipe[1]);
}

void jdll_std_process_pid(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p || p->pid <= 0) {
    jdl_return_i32(c, 0);
    return;
  }
  jdl_return_i32(c, (int32_t)p->pid);
}

void jdll_std_process_wait(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p || p->pid <= 0) {
    jdl_fail(c, "process_wait: invalid handle");
    return;
  }
  if(p->waited) {
    jdl_return_i32(c, (int32_t)p->exit_code);
    return;
  }
  int status = 0;
  if(waitpid(p->pid, &status, 0) < 0) {
    jdl_fail(c, "process_wait: waitpid failed");
    return;
  }
  p->waited = 1;
  p->exit_code = normalize_exit_status(status);
  jdl_return_i32(c, (int32_t)p->exit_code);
}

void jdll_std_process_kill(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p || p->pid <= 0) {
    jdl_return_bool(c, 0);
    return;
  }
  jdl_return_bool(c, kill(p->pid, SIGTERM) == 0);
}

void jdll_std_process_read_stdout(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p) {
    jdl_fail(c, "process_read_stdout: invalid handle");
    return;
  }
  (void)read_fd_to_bytes(c, p->stdout_r);
}

void jdll_std_process_read_stderr(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p) {
    jdl_fail(c, "process_read_stderr: invalid handle");
    return;
  }
  (void)read_fd_to_bytes(c, p->stderr_r);
}

void jdll_std_process_write_stdin(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p || p->stdin_w < 0) {
    jdl_return_bool(c, 0);
    return;
  }
  const uint8_t* data = jdl_arg_bytes_data(c, 1);
  uint32_t len = jdl_arg_bytes_len(c, 1);
  if(len == 0) {
    jdl_return_bool(c, 1);
    return;
  }
  if(!data) {
    jdl_return_bool(c, 0);
    return;
  }
  size_t off = 0;
  while(off < len) {
    ssize_t n = write(p->stdin_w, data + off, (size_t)len - off);
    if(n < 0) {
      if(errno == EINTR) continue;
      jdl_return_bool(c, 0);
      return;
    }
    if(n == 0) {
      jdl_return_bool(c, 0);
      return;
    }
    off += (size_t)n;
  }
  jdl_return_bool(c, 1);
}

void jdll_std_process_close_stdin(jdlo_ctx* c) {
  jdll_process* p = process_from_arg(c, 0);
  if(!p) {
    jdl_return_bool(c, 0);
    return;
  }
  close_fd(&p->stdin_w);
  jdl_return_bool(c, 1);
}

#endif /* !_WIN32 */
