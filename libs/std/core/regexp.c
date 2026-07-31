// SPDX-License-Identifier: BSD-3-Clause
// PCRE2-backed regular expressions for Jello Bytes.

#include <jello.h>
#include <jello/jdll.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define REGEXP_MAX_PATTERN_LEN 8192u
#define REGEXP_MAX_CAPTURES 32u
#define REGEXP_MATCH_LIMIT 100000u
#define REGEXP_DEPTH_LIMIT 1000u

JDLL_DEFINE_KIND(regexp);
JDLL_DEFINE_KIND(regexp_match);

typedef struct jdll_regexp {
  pcre2_code* code;
  uint8_t* pattern;
  uint32_t pattern_len;
  uint8_t* flags;
  uint32_t flags_len;
} jdll_regexp;

typedef struct jdll_regexp_capture {
  uint32_t start;
  uint32_t len;
  uint8_t* data;
} jdll_regexp_capture;

typedef struct jdll_regexp_match {
  jdll_regexp* re;
  const uint8_t* hay;
  uint32_t hay_len;
  uint32_t capture_count;
  jdll_regexp_capture* captures;
} jdll_regexp_match;

static void jdll_regexp_fin(void* payload) {
  jdll_regexp* re = (jdll_regexp*)payload;
  if(!re) return;
  if(re->code) pcre2_code_free(re->code);
  free(re->pattern);
  free(re->flags);
  free(re);
}

static void jdll_regexp_match_fin(void* payload) {
  jdll_regexp_match* m = (jdll_regexp_match*)payload;
  if(!m) return;
  if(m->captures) {
    for(uint32_t i = 0; i < m->capture_count; i++) {
      free(m->captures[i].data);
    }
    free(m->captures);
  }
  free(m);
}

static pcre2_match_context* regexp_match_context(void) {
  static pcre2_match_context* ctx = NULL;
  if(!ctx) {
    ctx = pcre2_match_context_create(NULL);
    if(ctx) {
      pcre2_set_match_limit(ctx, REGEXP_MATCH_LIMIT);
      pcre2_set_depth_limit(ctx, REGEXP_DEPTH_LIMIT);
    }
  }
  return ctx;
}

static int regexp_prefix_match(jdll_regexp* re, const uint8_t* hay, uint32_t hay_len) {
  if(!re || !re->code) return 0;
  if(!hay && hay_len) return 0;
  pcre2_match_data* md = pcre2_match_data_create_from_pattern(re->code, NULL);
  if(!md) return 0;
  int rc = pcre2_match(re->code, (PCRE2_SPTR)hay, (PCRE2_SIZE)hay_len, 0, PCRE2_ANCHORED, md,
                       regexp_match_context());
  pcre2_match_data_free(md);
  return rc >= 1;
}

static int regexp_is_special(uint8_t ch) {
  switch(ch) {
    case '.': case '^': case '$': case '*': case '+': case '?':
    case '{': case '}': case '[': case ']': case '\\': case '|':
    case '(': case ')':
      return 1;
    default:
      return 0;
  }
}

static jdll_regexp* regexp_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_regexp*)a->payload;
}

static jdll_regexp_match* match_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_regexp_match*)a->payload;
}

static uint32_t parse_pcre2_options(const uint8_t* flags, uint32_t flags_len) {
  uint32_t opts = PCRE2_UTF;
  for(uint32_t i = 0; i < flags_len; i++) {
    switch(flags[i]) {
      case 'i': opts |= PCRE2_CASELESS; break;
      case 'm': opts |= PCRE2_MULTILINE; break;
      case 's': opts |= PCRE2_DOTALL; break;
      case 'x': opts |= PCRE2_EXTENDED; break;
      case 'U': opts |= PCRE2_UNGREEDY; break;
      default: break;
    }
  }
  return opts;
}

static jdll_regexp* regexp_compile_bytes(jdlo_ctx* c, const uint8_t* pat, uint32_t pat_len,
                                         const uint8_t* flags, uint32_t flags_len) {
  if(!pat && pat_len) {
    jdl_fail(c, "regexp_compile: null pattern");
    return NULL;
  }
  if(pat_len > REGEXP_MAX_PATTERN_LEN) {
    jdl_fail(c, "regexp_compile: pattern too long");
    return NULL;
  }
  uint32_t opts = parse_pcre2_options(flags, flags_len);
  int err = 0;
  PCRE2_SIZE err_off = 0;
  pcre2_code* code = pcre2_compile((PCRE2_SPTR)pat, (PCRE2_SIZE)pat_len, opts, &err, &err_off, NULL);
  if(!code) {
    jdl_fail(c, "regexp_compile: invalid pattern");
    return NULL;
  }
  jdll_regexp* re = (jdll_regexp*)calloc(1, sizeof(*re));
  if(!re) {
    pcre2_code_free(code);
    jdl_fail(c, "regexp_compile: out of memory");
    return NULL;
  }
  re->code = code;
  if(pat_len) {
    re->pattern = (uint8_t*)malloc(pat_len);
    if(!re->pattern) {
      jdll_regexp_fin(re);
      jdl_fail(c, "regexp_compile: out of memory");
      return NULL;
    }
    memcpy(re->pattern, pat, pat_len);
    re->pattern_len = pat_len;
  }
  if(flags_len) {
    re->flags = (uint8_t*)malloc(flags_len);
    if(!re->flags) {
      jdll_regexp_fin(re);
      jdl_fail(c, "regexp_compile: out of memory");
      return NULL;
    }
    memcpy(re->flags, flags, flags_len);
    re->flags_len = flags_len;
  }
  return re;
}

static jdll_regexp_match* regexp_run_match(jdlo_ctx* c, const uint8_t* hay, uint32_t hay_len,
                                           jdll_regexp* re, uint32_t pcre_opts,
                                           PCRE2_SIZE start_offset) {
  if(!re || !re->code) {
    jdl_fail(c, "regexp match: null regexp");
    return NULL;
  }
  if(!hay && hay_len) {
    jdl_fail(c, "regexp match: null haystack");
    return NULL;
  }
  pcre2_match_data* md = pcre2_match_data_create_from_pattern(re->code, NULL);
  if(!md) {
    jdl_fail(c, "regexp match: out of memory");
    return NULL;
  }
  int rc = pcre2_match(re->code, (PCRE2_SPTR)hay, (PCRE2_SIZE)hay_len, start_offset, pcre_opts, md,
                       regexp_match_context());
  if(rc < 1) {
    pcre2_match_data_free(md);
    return NULL;
  }
  if((uint32_t)rc > REGEXP_MAX_CAPTURES) {
    pcre2_match_data_free(md);
    jdl_fail(c, "regexp match: too many captures");
    return NULL;
  }
  PCRE2_SIZE* ovec = pcre2_get_ovector_pointer(md);
  uint32_t cap_count = (uint32_t)rc;
  jdll_regexp_match* out = (jdll_regexp_match*)calloc(1, sizeof(*out));
  if(!out) {
    pcre2_match_data_free(md);
    jdl_fail(c, "regexp match: out of memory");
    return NULL;
  }
  out->re = re;
  out->hay = hay;
  out->hay_len = hay_len;
  out->capture_count = cap_count;
  out->captures = (jdll_regexp_capture*)calloc(cap_count, sizeof(jdll_regexp_capture));
  if(!out->captures) {
    jdll_regexp_match_fin(out);
    pcre2_match_data_free(md);
    jdl_fail(c, "regexp match: out of memory");
    return NULL;
  }
  for(uint32_t i = 0; i < cap_count; i++) {
    PCRE2_SIZE start = ovec[i * 2];
    PCRE2_SIZE end = ovec[i * 2 + 1];
    if(start == (PCRE2_SIZE)-1 || end == (PCRE2_SIZE)-1) {
      out->captures[i].start = 0;
      out->captures[i].len = 0;
      out->captures[i].data = NULL;
      continue;
    }
    if(end < start || end > hay_len) {
      jdll_regexp_match_fin(out);
      pcre2_match_data_free(md);
      jdl_fail(c, "regexp match: bad capture range");
      return NULL;
    }
    uint32_t len = (uint32_t)(end - start);
    out->captures[i].start = (uint32_t)start;
    out->captures[i].len = len;
    out->captures[i].data = NULL;
  }
  pcre2_match_data_free(md);
  return out;
}

void jdll_std_regexp_compile(jdlo_ctx* c) {
  jdl_check_bytes(c, 0);
  jdl_check_bytes(c, 1);
  const uint8_t* pat = jdl_arg_bytes_data(c, 0);
  uint32_t pat_len = jdl_arg_bytes_len(c, 0);
  const uint8_t* flags = jdl_arg_bytes_data(c, 1);
  uint32_t flags_len = jdl_arg_bytes_len(c, 1);
  jdll_regexp* re = regexp_compile_bytes(c, pat, pat_len, flags, flags_len);
  if(!re) return;
  jdl_return_abstract(c, re, jdll_regexp_fin);
}

void jdll_std_regexp_source(jdlo_ctx* c) {
  jdl_check_abstract(c, 0);
  jdll_regexp* re = regexp_from_arg(c, 0);
  if(!re) {
    jdl_fail(c, "regexp_source: null handle");
    return;
  }
  jdl_return_bytes_copy(c, re->pattern, re->pattern_len);
}

void jdll_std_regexp_flags(jdlo_ctx* c) {
  jdl_check_abstract(c, 0);
  jdll_regexp* re = regexp_from_arg(c, 0);
  if(!re) {
    jdl_fail(c, "regexp_flags: null handle");
    return;
  }
  jdl_return_bytes_copy(c, re->flags, re->flags_len);
}

void jdll_std_regexp_match_prefix(jdlo_ctx* c) {
  jdl_check_bytes(c, 0);
  jdl_check_abstract(c, 1);
  const uint8_t* hay = jdl_arg_bytes_data(c, 0);
  uint32_t hay_len = jdl_arg_bytes_len(c, 0);
  jdll_regexp* re = regexp_from_arg(c, 1);
  jdll_regexp_match* m = regexp_run_match(c, hay, hay_len, re, PCRE2_ANCHORED, 0);
  if(!m) {
    jdl_return_null(c);
    return;
  }
  jdl_return_abstract(c, m, jdll_regexp_match_fin);
}

void jdll_std_regexp_match(jdlo_ctx* c) {
  jdl_check_bytes(c, 0);
  jdl_check_abstract(c, 1);
  const uint8_t* hay = jdl_arg_bytes_data(c, 0);
  uint32_t hay_len = jdl_arg_bytes_len(c, 0);
  jdll_regexp* re = regexp_from_arg(c, 1);
  jdll_regexp_match* m = regexp_run_match(c, hay, hay_len, re, 0, 0);
  if(!m) {
    jdl_return_null(c);
    return;
  }
  jdl_return_abstract(c, m, jdll_regexp_match_fin);
}

void jdll_std_regexp_match_prefix_p(jdlo_ctx* c) {
  jdl_check_bytes(c, 0);
  jdl_check_abstract(c, 1);
  const uint8_t* hay = jdl_arg_bytes_data(c, 0);
  uint32_t hay_len = jdl_arg_bytes_len(c, 0);
  jdll_regexp* re = regexp_from_arg(c, 1);
  jdl_return_bool(c, regexp_prefix_match(re, hay, hay_len));
}

void jdll_std_regexp_full_match(jdlo_ctx* c) {
  jdl_check_bytes(c, 0);
  jdl_check_abstract(c, 1);
  const uint8_t* hay = jdl_arg_bytes_data(c, 0);
  uint32_t hay_len = jdl_arg_bytes_len(c, 0);
  jdll_regexp* re = regexp_from_arg(c, 1);
  if(!regexp_prefix_match(re, hay, hay_len)) {
    jdl_return_bool(c, 0);
    return;
  }
  pcre2_match_data* md = pcre2_match_data_create_from_pattern(re->code, NULL);
  if(!md) {
    jdl_fail(c, "regexp_full_match: out of memory");
    return;
  }
  int rc = pcre2_match(re->code, (PCRE2_SPTR)hay, (PCRE2_SIZE)hay_len, 0, PCRE2_ANCHORED, md,
                       regexp_match_context());
  int ok = 0;
  if(rc >= 1) {
    PCRE2_SIZE* ovec = pcre2_get_ovector_pointer(md);
    ok = ovec[1] - ovec[0] == (PCRE2_SIZE)hay_len;
  }
  pcre2_match_data_free(md);
  jdl_return_bool(c, ok);
}

void jdll_std_regexp_match_len(jdlo_ctx* c) {
  jdl_check_abstract(c, 0);
  jdll_regexp_match* m = match_from_arg(c, 0);
  if(!m || m->capture_count == 0) {
    jdl_return_i32(c, 0);
    return;
  }
  jdl_return_i32(c, (int32_t)m->captures[0].len);
}

void jdll_std_regexp_capture_count(jdlo_ctx* c) {
  jdl_check_abstract(c, 0);
  jdll_regexp_match* m = match_from_arg(c, 0);
  if(!m) {
    jdl_return_i32(c, 0);
    return;
  }
  jdl_return_i32(c, (int32_t)m->capture_count);
}

void jdll_std_regexp_capture_bytes(jdlo_ctx* c) {
  jdl_check_abstract(c, 0);
  jdl_check_i32(c, 1);
  jdll_regexp_match* m = match_from_arg(c, 0);
  int32_t idx = jdl_arg_i32(c, 1);
  if(!m || idx < 0 || (uint32_t)idx >= m->capture_count) {
    jdl_return_bytes_copy(c, NULL, 0);
    return;
  }
  jdll_regexp_capture* cap = &m->captures[(uint32_t)idx];
  if(cap->data) {
    jdl_return_bytes_copy(c, cap->data, cap->len);
    return;
  }
  if(m->hay && cap->len) {
    jdl_return_bytes_copy(c, m->hay + cap->start, cap->len);
    return;
  }
  jdl_return_bytes_copy(c, NULL, 0);
}

void jdll_std_regexp_capture_start(jdlo_ctx* c) {
  jdl_check_abstract(c, 0);
  jdl_check_i32(c, 1);
  jdll_regexp_match* m = match_from_arg(c, 0);
  int32_t idx = jdl_arg_i32(c, 1);
  if(!m || idx < 0 || (uint32_t)idx >= m->capture_count) {
    jdl_return_i32(c, -1);
    return;
  }
  jdl_return_i32(c, (int32_t)m->captures[(uint32_t)idx].start);
}

void jdll_std_regexp_capture_named(jdlo_ctx* c) {
  jdl_check_abstract(c, 0);
  jdl_check_bytes(c, 1);
  jdll_regexp_match* m = match_from_arg(c, 0);
  const uint8_t* name = jdl_arg_bytes_data(c, 1);
  uint32_t name_len = jdl_arg_bytes_len(c, 1);
  if(!m || !m->re || !m->re->code || !name || !name_len) {
    jdl_return_bytes_copy(c, NULL, 0);
    return;
  }
  char* nbuf = (char*)malloc(name_len + 1u);
  if(!nbuf) {
    jdl_fail(c, "regexp_capture_named: out of memory");
    return;
  }
  memcpy(nbuf, name, name_len);
  nbuf[name_len] = '\0';
  int num = pcre2_substring_number_from_name(m->re->code, (PCRE2_SPTR)nbuf);
  free(nbuf);
  if(num < 0 || (uint32_t)num >= m->capture_count) {
    jdl_return_bytes_copy(c, NULL, 0);
    return;
  }
  jdll_regexp_capture* cap = &m->captures[(uint32_t)num];
  if(cap->data) {
    jdl_return_bytes_copy(c, cap->data, cap->len);
    return;
  }
  if(m->hay && cap->len) {
    jdl_return_bytes_copy(c, m->hay + cap->start, cap->len);
    return;
  }
  jdl_return_bytes_copy(c, NULL, 0);
}

static int regexp_append_bytes(uint8_t** buf, uint32_t* cap, uint32_t* len,
                               const uint8_t* data, uint32_t data_len) {
  if(!data_len) return 1;
  if(!data) return 0;
  uint64_t need = (uint64_t)(*len) + (uint64_t)data_len;
  if(need > UINT32_MAX) return 0;
  if(need > *cap) {
    uint32_t new_cap = *cap ? *cap : 64u;
    while((uint64_t)new_cap < need) {
      if(new_cap > UINT32_MAX / 2u) {
        new_cap = (uint32_t)need;
        break;
      }
      new_cap *= 2u;
    }
    uint8_t* nb = (uint8_t*)realloc(*buf, new_cap);
    if(!nb) return 0;
    *buf = nb;
    *cap = new_cap;
  }
  memcpy(*buf + *len, data, data_len);
  *len += data_len;
  return 1;
}

static int regexp_append_capture(pcre2_match_data* md, int rc, const uint8_t* hay,
                                 uint32_t hay_len, uint32_t group, uint8_t** buf,
                                 uint32_t* cap, uint32_t* len) {
  if(group >= (uint32_t)rc) return 1;
  PCRE2_SIZE* ovec = pcre2_get_ovector_pointer(md);
  PCRE2_SIZE start = ovec[group * 2];
  PCRE2_SIZE end = ovec[group * 2 + 1];
  if(end < start || start == (PCRE2_SIZE)-1 || end == (PCRE2_SIZE)-1) return 1;
  if(end > hay_len) return 0;
  return regexp_append_bytes(buf, cap, len, hay + start, (uint32_t)(end - start));
}

static int regexp_append_replacement(pcre2_match_data* md, int rc, const uint8_t* hay,
                                     uint32_t hay_len, const uint8_t* repl, uint32_t repl_len,
                                     uint8_t** buf, uint32_t* cap, uint32_t* len) {
  uint32_t i = 0;
  while(i < repl_len) {
    if(repl[i] == '$' && i + 1 < repl_len) {
      if(repl[i + 1] == '$') {
        if(!regexp_append_bytes(buf, cap, len, (const uint8_t*)"$", 1)) return 0;
        i += 2;
        continue;
      }
      if(repl[i + 1] >= '0' && repl[i + 1] <= '9') {
        uint32_t group = (uint32_t)(repl[i + 1] - '0');
        i += 2;
        while(i < repl_len && repl[i] >= '0' && repl[i] <= '9') {
          group = group * 10u + (uint32_t)(repl[i] - '0');
          if(group >= REGEXP_MAX_CAPTURES) break;
          i++;
        }
        if(!regexp_append_capture(md, rc, hay, hay_len, group, buf, cap, len)) return 0;
        continue;
      }
    }
    if(!regexp_append_bytes(buf, cap, len, repl + i, 1)) return 0;
    i++;
  }
  return 1;
}

void jdll_std_regexp_replace(jdlo_ctx* c) {
  jdl_check_bytes(c, 0);
  jdl_check_abstract(c, 1);
  jdl_check_bytes(c, 2);
  const uint8_t* hay = jdl_arg_bytes_data(c, 0);
  uint32_t hay_len = jdl_arg_bytes_len(c, 0);
  jdll_regexp* re = regexp_from_arg(c, 1);
  const uint8_t* repl = jdl_arg_bytes_data(c, 2);
  uint32_t repl_len = jdl_arg_bytes_len(c, 2);
  if(!re || !re->code) {
    jdl_fail(c, "regexp_replace: null regexp");
    return;
  }
  if(!hay && hay_len) {
    jdl_fail(c, "regexp_replace: null haystack");
    return;
  }
  if(!repl && repl_len) {
    jdl_fail(c, "regexp_replace: null replacement");
    return;
  }
  pcre2_match_data* md = pcre2_match_data_create_from_pattern(re->code, NULL);
  if(!md) {
    jdl_fail(c, "regexp_replace: out of memory");
    return;
  }
  uint8_t* out = NULL;
  uint32_t out_cap = 0;
  uint32_t out_len = 0;
  uint32_t offset = 0;
  int replaced = 0;
  while(offset <= hay_len) {
    int rc = pcre2_match(re->code, (PCRE2_SPTR)hay, (PCRE2_SIZE)hay_len, (PCRE2_SIZE)offset, 0,
                         md, regexp_match_context());
    if(rc < 1) break;
    if((uint32_t)rc > REGEXP_MAX_CAPTURES) {
      pcre2_match_data_free(md);
      free(out);
      jdl_fail(c, "regexp_replace: too many captures");
      return;
    }
    PCRE2_SIZE* ovec = pcre2_get_ovector_pointer(md);
    PCRE2_SIZE mstart = ovec[0];
    PCRE2_SIZE mend = ovec[1];
    if(mend < mstart || mend > hay_len || mstart < offset) {
      pcre2_match_data_free(md);
      free(out);
      jdl_fail(c, "regexp_replace: bad match range");
      return;
    }
    if(!regexp_append_bytes(&out, &out_cap, &out_len, hay + offset, (uint32_t)(mstart - offset))) {
      pcre2_match_data_free(md);
      free(out);
      jdl_fail(c, "regexp_replace: out of memory");
      return;
    }
    if(!regexp_append_replacement(md, rc, hay, hay_len, repl, repl_len, &out, &out_cap, &out_len)) {
      pcre2_match_data_free(md);
      free(out);
      jdl_fail(c, "regexp_replace: out of memory");
      return;
    }
    replaced = 1;
    offset = (uint32_t)mend;
    if(mstart == mend) {
      if(offset >= hay_len) break;
      offset++;
    }
  }
  pcre2_match_data_free(md);
  if(!replaced) {
    free(out);
    jdl_return_bytes_copy(c, hay, hay_len);
    return;
  }
  if(!regexp_append_bytes(&out, &out_cap, &out_len, hay + offset, hay_len - offset)) {
    free(out);
    jdl_fail(c, "regexp_replace: out of memory");
    return;
  }
  jdl_return_bytes_copy(c, out, out_len);
  free(out);
}

void jdll_std_regexp_escape_literal(jdlo_ctx* c) {
  jdl_check_bytes(c, 0);
  const uint8_t* data = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  if(!data || !len) {
    jdl_return_bytes_copy(c, data, len);
    return;
  }
  uint32_t cap = len * 2u + 1u;
  uint8_t* buf = (uint8_t*)malloc(cap);
  if(!buf) {
    jdl_fail(c, "regexp_escape_literal: out of memory");
    return;
  }
  uint32_t out = 0;
  for(uint32_t i = 0; i < len; i++) {
    if(regexp_is_special(data[i])) buf[out++] = '\\';
    buf[out++] = data[i];
  }
  jdl_return_bytes_copy(c, buf, out);
  free(buf);
}

void jdll_std_regexp_advance(jdlo_ctx* c) {
  jdl_check_bytes(c, 0);
  jdl_check_abstract(c, 1);
  const uint8_t* hay = jdl_arg_bytes_data(c, 0);
  uint32_t hay_len = jdl_arg_bytes_len(c, 0);
  jdll_regexp_match* m = match_from_arg(c, 1);
  if(!m || m->capture_count == 0) {
    jdl_return_bytes_copy(c, hay, hay_len);
    return;
  }
  uint32_t skip = m->captures[0].len;
  if(skip >= hay_len) {
    jdl_return_bytes_copy(c, NULL, 0);
    return;
  }
  jdl_return_bytes_copy(c, hay + skip, hay_len - skip);
}
