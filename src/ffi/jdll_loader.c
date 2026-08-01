// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/jdll_internal.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <unistd.h>
#endif

#define JDLL_ABI_MAGIC "JLYDLLABI1"

typedef struct {
  char* library_id;
  uint32_t n_exports;
  char** export_names;
  char** export_syms;
  uint8_t* ret_kinds;
  uint8_t* arities;
  uint8_t** arg_kinds;
  uint8_t* arg_kind_lens;
} jdll_abi_parsed;

static void free_jdll_abi(jdll_abi_parsed* abi);

typedef struct {
  char** items;
  uint32_t len;
  uint32_t cap;
} str_list;

static void sl_push(str_list* sl, char* s) {
  if(sl->len + 1u > sl->cap) {
    uint32_t nc = sl->cap ? sl->cap * 2u : 8u;
    char** ni = (char**)realloc(sl->items, (size_t)nc * sizeof(char*));
    if(!ni) { free(s); return; }
    sl->items = ni;
    sl->cap = nc;
  }
  sl->items[sl->len++] = s;
}

static void sl_free(str_list* sl) {
  if(!sl) return;
  for(uint32_t i = 0; i < sl->len; i++) free(sl->items[i]);
  free(sl->items);
  sl->items = NULL;
  sl->len = sl->cap = 0;
}

static int path_is_sep(char c) {
#if defined(_WIN32)
  return c == '/' || c == '\\';
#else
  return c == '/';
#endif
}

static const char* path_last_sep(const char* path) {
  if(!path) return NULL;
  const char* last = NULL;
  for(const char* p = path; *p; p++) {
    if(path_is_sep(*p)) last = p;
  }
  return last;
}

static int path_is_file(const char* path) {
  struct stat st;
#if defined(_WIN32)
  return path && stat(path, &st) == 0 && (st.st_mode & _S_IFREG) != 0;
#else
  return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static char* path_join2(const char* a, const char* b) {
  size_t la = strlen(a), lb = strlen(b);
  int need_slash = (la > 0 && !path_is_sep(a[la - 1]));
  size_t n = la + lb + (size_t)need_slash + 1u;
  char* out = (char*)malloc(n);
  if(!out) return NULL;
  if(need_slash) {
#if defined(_WIN32)
    snprintf(out, n, "%s\\%s", a, b);
#else
    snprintf(out, n, "%s/%s", a, b);
#endif
  } else {
    snprintf(out, n, "%s%s", a, b);
  }
  return out;
}

static char* path_dirname_dup(const char* path) {
  if(!path) return NULL;
  const char* slash = path_last_sep(path);
  if(!slash) {
    char* dot = (char*)malloc(2);
    if(dot) { dot[0] = '.'; dot[1] = 0; }
    return dot;
  }
  size_t n = (size_t)(slash - path);
  if(n == 0) n = 1;
  char* out = (char*)malloc(n + 1u);
  if(!out) return NULL;
  memcpy(out, path, n);
  out[n] = 0;
  return out;
}

#if defined(_WIN32)
static void* jdll_dlopen(const char* path) {
  return (void*)LoadLibraryA(path);
}

static void* jdll_dlsym(void* handle, const char* sym) {
  if(!handle) return NULL;
  FARPROC p = GetProcAddress((HMODULE)handle, sym);
  void* out = NULL;
  memcpy(&out, &p, sizeof(out));
  return out;
}

static void jdll_dlerror_msg(char* buf, size_t cap) {
  if(!buf || cap == 0) return;
  DWORD err = GetLastError();
  if(err == 0) {
    snprintf(buf, cap, "LoadLibrary failed");
    return;
  }
  DWORD n = FormatMessageA(
    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL, err, 0, buf, (DWORD)cap, NULL);
  if(n == 0) {
    snprintf(buf, cap, "Win32 error %lu", (unsigned long)err);
    return;
  }
  while(n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ')) {
    buf[--n] = 0;
  }
}
#else
static void* jdll_dlopen(const char* path) {
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void* jdll_dlsym(void* handle, const char* sym) {
  return dlsym(handle, sym);
}

static void jdll_dlerror_msg(char* buf, size_t cap) {
  const char* dl_err = dlerror();
  snprintf(buf, cap, "%s", dl_err ? dl_err : "dlopen failed");
}
#endif

const char* jello_discovery_entry_path(const char* argv_entry) {
  const char* env = getenv("JELLO_ENTRY_PATH");
  if(env && *env) return env;
  return (argv_entry && *argv_entry) ? argv_entry : ".";
}

static void collect_jdll_roots_c(const char* entry_path, str_list* out) {
  const char* env = getenv("JELLO_JDLL_PATH");
  if(env && *env) {
    const char* p = env;
#if defined(_WIN32)
    const char path_sep = ';';
#else
    const char path_sep = ':';
#endif
    while(*p) {
      const char* sep = strchr(p, path_sep);
      size_t len = sep ? (size_t)(sep - p) : strlen(p);
      if(len) {
        char* part = (char*)malloc(len + 1u);
        if(part) {
          memcpy(part, p, len);
          part[len] = 0;
          sl_push(out, part);
        }
      }
      if(!sep) break;
      p = sep + 1;
    }
  }
  char* dir = path_dirname_dup(entry_path);
  if(!dir) return;
  sl_push(out, path_join2(dir, "jdll"));
  sl_push(out, strdup(dir));
  char* cur = strdup(dir);
  free(dir);
  for(int i = 0; i < 12 && cur; i++) {
    char* slash = (char*)path_last_sep(cur);
    if(!slash || slash == cur) break;
    *slash = 0;
    sl_push(out, path_join2(cur, "jdll"));
    if(strcmp(cur, ".") == 0) break;
  }
  free(cur);
}

static char* resolve_jdll_key(const char* entry_path, const char* key) {
  str_list roots = {0};
  collect_jdll_roots_c(entry_path, &roots);
  char rel[512];
  snprintf(rel, sizeof rel, "%s.jdll", key);
  char* found = NULL;
  for(uint32_t i = 0; i < roots.len; i++) {
    char* cand = path_join2(roots.items[i], rel);
    if(cand && path_is_file(cand)) {
      found = cand;
      break;
    }
    free(cand);
  }
  sl_free(&roots);
  return found;
}

static int rd_u32le(const uint8_t* b, size_t blen, size_t* i, uint32_t* out) {
  if(*i + 4 > blen) return 0;
  *out = (uint32_t)b[*i] | ((uint32_t)b[*i + 1] << 8) | ((uint32_t)b[*i + 2] << 16) | ((uint32_t)b[*i + 3] << 24);
  *i += 4;
  return 1;
}

static int rd_str(const uint8_t* b, size_t blen, size_t* i, char** out) {
  uint32_t len = 0;
  if(!rd_u32le(b, blen, i, &len) || *i + len > blen) return 0;
  char* s = (char*)malloc((size_t)len + 1u);
  if(!s) return 0;
  memcpy(s, b + *i, len);
  s[len] = 0;
  *i += len;
  *out = s;
  return 1;
}

static int parse_jdll_abi_bytes(const uint8_t* data, size_t blen, jdll_abi_parsed* abi) {
  memset(abi, 0, sizeof(*abi));
  size_t magic_len = strlen(JDLL_ABI_MAGIC) + 1u;
  if(blen < magic_len || memcmp(data, JDLL_ABI_MAGIC, magic_len) != 0) return 0;
  size_t i = magic_len;
  uint32_t version = 0;
  if(!rd_u32le(data, blen, &i, &version) || version != 1u) return 0;
  if(!rd_str(data, blen, &i, &abi->library_id)) return 0;
  if(!rd_u32le(data, blen, &i, &abi->n_exports)) return 0;
  abi->export_names = (char**)calloc(abi->n_exports, sizeof(char*));
  abi->export_syms = (char**)calloc(abi->n_exports, sizeof(char*));
  abi->ret_kinds = (uint8_t*)calloc(abi->n_exports, sizeof(uint8_t));
  abi->arities = (uint8_t*)calloc(abi->n_exports, sizeof(uint8_t));
  abi->arg_kinds = (uint8_t**)calloc(abi->n_exports, sizeof(uint8_t*));
  abi->arg_kind_lens = (uint8_t*)calloc(abi->n_exports, sizeof(uint8_t));
  if(!abi->export_names || !abi->export_syms || !abi->ret_kinds || !abi->arities || !abi->arg_kinds ||
     !abi->arg_kind_lens) {
    return 0;
  }
  for(uint32_t e = 0; e < abi->n_exports; e++) {
    if(!rd_str(data, blen, &i, &abi->export_names[e]) || !rd_str(data, blen, &i, &abi->export_syms[e])) {
      return 0;
    }
    if(i + 2 > blen) return 0;
    abi->ret_kinds[e] = data[i++];
    abi->arities[e] = data[i++];
    if(abi->arities[e] == JDLL_ABI_VARARGS_ARITY) {
      if(i + 1 > blen) return 0;
      abi->arg_kinds[e] = (uint8_t*)malloc(1);
      if(!abi->arg_kinds[e]) return 0;
      abi->arg_kinds[e][0] = data[i++];
      abi->arg_kind_lens[e] = 1;
    } else if(abi->arities[e] == JDLL_ABI_MIXED_VARARGS_ARITY) {
      if(i + 1 > blen) return 0;
      uint8_t n = data[i++];
      if(n == 0 || i + n > blen) return 0;
      abi->arg_kinds[e] = (uint8_t*)malloc(n);
      if(!abi->arg_kinds[e]) return 0;
      memcpy(abi->arg_kinds[e], data + i, n);
      abi->arg_kind_lens[e] = n;
      i += n;
    } else if(abi->arities[e]) {
      abi->arg_kinds[e] = (uint8_t*)malloc(abi->arities[e]);
      if(!abi->arg_kinds[e]) return 0;
      if(i + abi->arities[e] > blen) return 0;
      memcpy(abi->arg_kinds[e], data + i, abi->arities[e]);
      abi->arg_kind_lens[e] = abi->arities[e];
      i += abi->arities[e];
    }
  }
  return 1;
}

static int parse_jdll_abi_file(const char* path, jdll_abi_parsed* abi) {
  FILE* f = fopen(path, "rb");
  if(!f) return 0;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if(sz < 0) { fclose(f); return 0; }
  rewind(f);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if(!data) { fclose(f); return 0; }
  if(fread(data, 1, (size_t)sz, f) != (size_t)sz) {
    free(data);
    fclose(f);
    return 0;
  }
  fclose(f);
  int ok = parse_jdll_abi_bytes(data, (size_t)sz, abi);
  if(!ok) free_jdll_abi(abi);
  free(data);
  return ok;
}

static int parse_jdll_abi_embedded(const char* jdll_path, jdll_abi_parsed* abi) {
  FILE* f = fopen(jdll_path, "rb");
  if(!f) return 0;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if(sz < 0) { fclose(f); return 0; }
  rewind(f);
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if(!data) { fclose(f); return 0; }
  if(fread(data, 1, (size_t)sz, f) != (size_t)sz) {
    free(data);
    fclose(f);
    return 0;
  }
  fclose(f);

  size_t magic_len = strlen(JDLL_ABI_MAGIC) + 1u;
  int ok = 0;
  for(size_t off = 0; off + magic_len <= (size_t)sz; off++) {
    if(memcmp(data + off, JDLL_ABI_MAGIC, magic_len) != 0) continue;
    if(parse_jdll_abi_bytes(data + off, (size_t)sz - off, abi)) {
      ok = 1;
      break;
    }
    free_jdll_abi(abi);
  }
  free(data);
  return ok;
}

static int load_jdll_abi(const char* jdll_path, jdll_abi_parsed* abi) {
  char abi_path[1024];
  snprintf(abi_path, sizeof abi_path, "%s.abi", jdll_path);
  if(path_is_file(abi_path)) return parse_jdll_abi_file(abi_path, abi);
  return parse_jdll_abi_embedded(jdll_path, abi);
}

static void free_jdll_abi(jdll_abi_parsed* abi) {
  if(!abi) return;
  free(abi->library_id);
  if(abi->export_names) {
    for(uint32_t i = 0; i < abi->n_exports; i++) {
      free(abi->export_names[i]);
      free(abi->export_syms[i]);
      free(abi->arg_kinds[i]);
    }
  }
  free(abi->export_names);
  free(abi->export_syms);
  free(abi->ret_kinds);
  free(abi->arities);
  free(abi->arg_kind_lens);
  free(abi->arg_kinds);
  memset(abi, 0, sizeof(*abi));
}

static uint32_t find_atom_id(const jello_bc_module* m, const char* name) {
  for(uint32_t i = 0; i < m->natoms; i++) {
    if(m->atoms[i] && strcmp(m->atoms[i], name) == 0) return i;
  }
  return UINT32_MAX;
}

int jello_jdll_fill_exports(exec_ctx* ctx, uint32_t exports_reg, uint32_t key_reg) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  call_frame* fr = ctx->fr;

  jello_jdll_pin_plugin_api();

  jello_object* exports = (jello_object*)vm_load_ptr(&fr->rf, exports_reg);
  jello_bytes* keyb = (jello_bytes*)vm_load_ptr(&fr->rf, key_reg);
  if(!exports || !keyb) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "jdll init: bad args");
    return 0;
  }

  char key[256];
  uint32_t klen = keyb->length < 255u ? keyb->length : 255u;
  memcpy(key, keyb->data, klen);
  key[klen] = 0;

  const char* entry = vm->entry_path ? vm->entry_path : ".";
  char* jdll_path = resolve_jdll_key(entry, key);
  if(!jdll_path) {
    char msg[320];
    snprintf(msg, sizeof msg, "jdll '%s' not found", key);
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, msg);
    return 0;
  }

  jdll_abi_parsed abi;
  if(!load_jdll_abi(jdll_path, &abi)) {
    free(jdll_path);
    char msg[320];
    snprintf(msg, sizeof msg, "jdll '%s': ABI missing or invalid", key);
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, msg);
    return 0;
  }

  void* handle = NULL;
  static pthread_mutex_t jdll_dlopen_mu = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&jdll_dlopen_mu);
  handle = jdll_dlopen(jdll_path);
  pthread_mutex_unlock(&jdll_dlopen_mu);
  if(!handle) {
    char errbuf[384];
    jdll_dlerror_msg(errbuf, sizeof errbuf);
    char msg[512];
    snprintf(msg, sizeof msg, "jdll '%s': %s", key, errbuf);
    free(jdll_path);
    free_jdll_abi(&abi);
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, msg);
    return 0;
  }
  free(jdll_path);

  char* lib_id = strdup(abi.library_id);
  if(!lib_id) {
    free_jdll_abi(&abi);
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "jdll init: oom");
    return 0;
  }

  for(uint32_t i = 0; i < abi.n_exports; i++) {
    uint32_t atom_id = find_atom_id(m, abi.export_names[i]);
    if(atom_id == UINT32_MAX) continue;
    void* sym = jdll_dlsym(handle, abi.export_syms[i]);
    jdll_export_fn fn;
    memcpy(&fn, &sym, sizeof(fn));
    if(!fn) {
      char msg[512];
      snprintf(msg, sizeof msg, "jdll '%s': missing symbol %s", key, abi.export_syms[i]);
      free(lib_id);
      free_jdll_abi(&abi);
      (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, msg);
      return 0;
    }

    char* export_name = strdup(abi.export_names[i]);
    char* export_sym = strdup(abi.export_syms[i]);
    if(!export_name || !export_sym) {
      free(export_name);
      free(export_sym);
      free(lib_id);
      free_jdll_abi(&abi);
      (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "jdll init: oom");
      return 0;
    }

    jdll_prim_caps caps = {
      .fn = fn,
      .ret_kind = abi.ret_kinds[i],
      .arity = abi.arities[i],
      .library_id = lib_id,
      .export_name = export_name,
      .export_sym = export_sym,
    };
    if(abi.arities[i] == JDLL_ABI_VARARGS_ARITY) {
      if(abi.arg_kinds[i]) caps.arg_kinds[0] = abi.arg_kinds[i][0];
    } else if(abi.arities[i] == JDLL_ABI_MIXED_VARARGS_ARITY && abi.arg_kinds[i]) {
      uint8_t ntypes = abi.arg_kind_lens[i];
      if(ntypes < 2) {
        char msg[384];
        snprintf(msg, sizeof msg, "jdll '%s': export '%s' mixed varargs needs fixed prefix + elem types",
                 key, abi.export_names[i]);
        free(export_name);
        free(export_sym);
        free(lib_id);
        free_jdll_abi(&abi);
        (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, msg);
        return 0;
      }
      uint8_t nfixed = (uint8_t)(ntypes - 1u);
      if(nfixed > 6u) {
        char msg[384];
        snprintf(msg, sizeof msg,
                 "jdll '%s': export '%s' mixed varargs fixed prefix too long (max 6)",
                 key, abi.export_names[i]);
        free(export_name);
        free(export_sym);
        free(lib_id);
        free_jdll_abi(&abi);
        (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, msg);
        return 0;
      }
      caps.arg_kinds[0] = nfixed;
      memcpy(&caps.arg_kinds[1], abi.arg_kinds[i], ntypes);
    } else if(abi.arities[i] && abi.arg_kinds[i]) {
      memcpy(caps.arg_kinds, abi.arg_kinds[i], abi.arities[i]);
    }

    jello_function* jfn = jello_closure_new_raw(
      vm, 10u, JELLO_FUNC_INDEX_JDLL_PRIM, 0, (uint32_t)sizeof(caps), (const uint8_t*)&caps);
    if(!jfn) continue;
    jello_value fv = jello_from_ptr(jfn);
    jello_gc_push_root(vm, fv);
    jello_object_set(exports, atom_id, fv);
    jello_gc_pop_roots(vm, 1);
  }

  free_jdll_abi(&abi);
  /* Keep library loaded for process lifetime (handle intentionally leaked). */
  (void)handle;
  return 1;
}

char* jello_jdll_resolve_library(const char* entry_path, const char* library_id) {
  if(!library_id) return NULL;
  const char* entry = entry_path ? entry_path : ".";
  return resolve_jdll_key(entry, library_id);
}
