// SPDX-License-Identifier: BSD-3-Clause  
// Copyright (c) Jahred Love. All rights reserved.

#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <jello/internal/jdll_internal.h>
#include <jello/jdll.h>

#include <stdio.h>
#include <string.h>

#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#define JELLO_JDLL_HAVE_DLADDR 1
#else
#define JELLO_JDLL_HAVE_DLADDR 0
#endif

static int append_str(char* buf, size_t cap, size_t* off, const char* s) {
  if(!buf || cap == 0 || !off || !s) return 0;
  if(*off >= cap) return 0;
  int n = snprintf(buf + *off, cap - *off, "%s", s);
  if(n < 0) return 0;
  if((size_t)n >= cap - *off) {
    *off = cap - 1u;
    buf[*off] = 0;
    return 0;
  }
  *off += (size_t)n;
  return 1;
}

static void append_jdll_frame(char* buf, size_t cap, size_t* off, const jello_vm* vm) {
  if(!vm || !vm->jdll_active_export) return;
  append_str(buf, cap, off, "  jdll ");
  if(vm->jdll_active_library) {
    append_str(buf, cap, off, vm->jdll_active_library);
    append_str(buf, cap, off, ".");
  }
  append_str(buf, cap, off, vm->jdll_active_export);
  if(vm->jdll_active_sym) {
    append_str(buf, cap, off, " (");
    append_str(buf, cap, off, vm->jdll_active_sym);
    append_str(buf, cap, off, ")");
  }
#if JELLO_JDLL_HAVE_DLADDR
  if(vm->jdll_active_fn) {
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if(dladdr(vm->jdll_active_fn, &info) && info.dli_sname) {
      append_str(buf, cap, off, " [native: ");
      append_str(buf, cap, off, info.dli_sname);
      append_str(buf, cap, off, "]");
    }
  }
#endif
  append_str(buf, cap, off, "\n");
}

static const char* trace_source_basename(const char* path) {
  if(!path) return NULL;
  const char* slash = strrchr(path, '/');
  if(!slash) slash = strrchr(path, '\\');
  return slash ? slash + 1 : path;
}

static void format_frame_loc(char* out, size_t cap, const jello_bc_module* mod, const jello_bc_function* f, uint32_t pc) {
  out[0] = 0;
  if(!mod || !f || !f->lines || pc >= f->nlines) return;
  uint32_t loc = f->lines[pc];
  uint16_t line = (uint16_t)(loc & 0xFFFFu);
  if(line == 0u) return;
  if(f->source_file >= mod->nsource_files || !mod->source_files[f->source_file]) return;
  const char* file = trace_source_basename(mod->source_files[f->source_file]);
  uint16_t col = (uint16_t)(loc >> 16);
  if(col > 0u) {
    snprintf(out, cap, "%s:%u:%u", file, (unsigned)line, (unsigned)col);
  } else {
    snprintf(out, cap, "%s:%u", file, (unsigned)line);
  }
}

static void append_bytecode_frames(char* buf, size_t cap, size_t* off, const jello_vm* vm) {
  if(!vm || !vm->call_frames) return;
  const jello_bc_module* mod = vm->running_module;
  call_frame* frames = (call_frame*)vm->call_frames;
  for(uint32_t i = vm->call_frames_len; i > 0; i--) {
    call_frame* fr = &frames[i - 1u];
    if(!fr->f) continue;
    uint32_t fi = UINT32_MAX;
    if(mod && mod->funcs && fr->f >= mod->funcs && fr->f < mod->funcs + mod->nfuncs) {
      fi = (uint32_t)(fr->f - mod->funcs);
    }
    char line[160];
    char locbuf[128];
    locbuf[0] = 0;
    if(fr->pc > 0u) {
      format_frame_loc(locbuf, sizeof locbuf, mod, fr->f, fr->pc - 1u);
    }
    if(fi != UINT32_MAX && locbuf[0] != 0) {
      snprintf(line, sizeof line, "  #%u at %s in func %u\n",
               (unsigned)(vm->call_frames_len - i),
               locbuf,
               (unsigned)fi);
    } else if(fi != UINT32_MAX) {
      snprintf(line, sizeof line, "  #%u at func %u pc %u\n",
               (unsigned)(vm->call_frames_len - i),
               (unsigned)fi,
               (unsigned)fr->pc);
    } else {
      snprintf(line, sizeof line, "  #%u at pc %u\n",
               (unsigned)(vm->call_frames_len - i),
               (unsigned)fr->pc);
    }
    append_str(buf, cap, off, line);
  }
}

int jello_vm_format_stack_trace(const jello_vm* vm, char* buf, size_t cap) {
  if(!buf || cap == 0) return 0;
  buf[0] = 0;
  size_t off = 0;
  append_str(buf, cap, &off, "stack trace:\n");
  append_jdll_frame(buf, cap, &off, vm);
  append_bytecode_frames(buf, cap, &off, vm);
  return (int)off;
}

void jello_vm_print_stack_trace(const jello_vm* vm, FILE* out) {
  if(!vm || !out) return;
  if(vm->stack_trace_buf[0] != 0) {
    fputs(vm->stack_trace_buf, out);
    return;
  }
  char trace[1024];
  if(jello_vm_format_stack_trace(vm, trace, sizeof trace) > 0) {
    fputs(trace, out);
  }
}

void jello_jdll_set_active_prim(jello_vm* vm, const jdll_prim_caps* caps) {
  if(!vm || !caps) return;
  vm->jdll_active_library = caps->library_id;
  vm->jdll_active_export = caps->export_name;
  vm->jdll_active_sym = caps->export_sym;
  void* fn_addr = NULL;
  memcpy(&fn_addr, &caps->fn, sizeof(fn_addr));
  vm->jdll_active_fn = fn_addr;
}

void jello_jdll_clear_active_prim(jello_vm* vm) {
  if(!vm) return;
  vm->jdll_active_library = NULL;
  vm->jdll_active_export = NULL;
  vm->jdll_active_sym = NULL;
  vm->jdll_active_fn = NULL;
}

int jdl_stack_trace(jdlo_ctx* c, char* buf, int buflen) {
  if(!c || !c->xctx || !buf || buflen <= 0) return 0;
  return jello_vm_format_stack_trace(c->xctx->vm, buf, (size_t)buflen);
}
