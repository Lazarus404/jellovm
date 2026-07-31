// SPDX-License-Identifier: BSD-3-Clause
// UTF-8 operations on Jello Bytes (Neko utf8.c / unicode.c subset).

#include <jello/jdll.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t utf8_len_tbl[16] = {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 2, 2, 3, 4};

static int utf8_char_len(uint8_t b) {
  return (int)utf8_len_tbl[b >> 4];
}

static int utf8_valid_cp(uint32_t cp) {
  return cp <= 0x10FFFFu && (cp < 0xD800u || cp > 0xDFFFu);
}

static int utf8_decode_at(const uint8_t* s, uint32_t len, uint32_t off, uint32_t* cp, uint32_t* adv) {
  if(off >= len) return 0;
  uint8_t b0 = s[off];
  int n = utf8_char_len(b0);
  if(n <= 0 || off + (uint32_t)n > len) return 0;
  uint32_t c = 0;
  if(n == 1) {
    c = b0;
  } else if(n == 2) {
    uint8_t b1 = s[off + 1];
    if((b1 & 0xC0) != 0x80) return 0;
    c = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
    if(c < 0x80) return 0;
  } else if(n == 3) {
    uint8_t b1 = s[off + 1], b2 = s[off + 2];
    if((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return 0;
    c = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(b1 & 0x3F) << 6) | (uint32_t)(b2 & 0x3F);
    if(c < 0x800) return 0;
  } else {
    uint8_t b1 = s[off + 1], b2 = s[off + 2], b3 = s[off + 3];
    if((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return 0;
    c = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(b1 & 0x3F) << 12) | ((uint32_t)(b2 & 0x3F) << 6) |
        (uint32_t)(b3 & 0x3F);
    if(c < 0x10000) return 0;
  }
  if(!utf8_valid_cp(c)) return 0;
  *cp = c;
  *adv = (uint32_t)n;
  return 1;
}

static int utf8_validate_bytes(const uint8_t* s, uint32_t len) {
  uint32_t i = 0;
  while(i < len) {
    uint32_t cp = 0, adv = 0;
    if(!utf8_decode_at(s, len, i, &cp, &adv)) return 0;
    i += adv;
  }
  return 1;
}

static uint32_t utf8_codepoint_count(const uint8_t* s, uint32_t len) {
  uint32_t i = 0, n = 0;
  while(i < len) {
    uint32_t cp = 0, adv = 0;
    if(!utf8_decode_at(s, len, i, &cp, &adv)) return UINT32_MAX;
    i += adv;
    n++;
  }
  return n;
}

static int utf8_encode(uint32_t cp, uint8_t* out, uint32_t cap, uint32_t* nout) {
  if(!utf8_valid_cp(cp) || !out || !nout) return 0;
  if(cp < 0x80) {
    if(cap < 1) return 0;
    out[0] = (uint8_t)cp;
    *nout = 1;
    return 1;
  }
  if(cp < 0x800) {
    if(cap < 2) return 0;
    out[0] = (uint8_t)(0xC0 | (cp >> 6));
    out[1] = (uint8_t)(0x80 | (cp & 0x3F));
    *nout = 2;
    return 1;
  }
  if(cp < 0x10000) {
    if(cap < 3) return 0;
    out[0] = (uint8_t)(0xE0 | (cp >> 12));
    out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (uint8_t)(0x80 | (cp & 0x3F));
    *nout = 3;
    return 1;
  }
  if(cap < 4) return 0;
  out[0] = (uint8_t)(0xF0 | (cp >> 18));
  out[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (uint8_t)(0x80 | (cp & 0x3F));
  *nout = 4;
  return 1;
}

static int utf8_byte_offset_at_cp(const uint8_t* s, uint32_t len, int32_t cp_index, uint32_t* byte_off) {
  if(cp_index < 0) return 0;
  uint32_t i = 0;
  for(int32_t k = 0; k < cp_index; k++) {
    uint32_t cp = 0, adv = 0;
    if(!utf8_decode_at(s, len, i, &cp, &adv)) return 0;
    i += adv;
  }
  if(i > len) return 0;
  *byte_off = i;
  return 1;
}

void jdll_std_unicode_validate(jdlo_ctx* c) {
  const uint8_t* s = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  if(!s && len) {
    jdl_fail(c, "unicode_validate: null bytes");
    return;
  }
  jdl_return_bool(c, utf8_validate_bytes(s, len));
}

void jdll_std_unicode_length(jdlo_ctx* c) {
  const uint8_t* s = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  if(!s && len) {
    jdl_fail(c, "unicode_length: null bytes");
    return;
  }
  uint32_t n = utf8_codepoint_count(s, len);
  if(n == UINT32_MAX) {
    jdl_fail(c, "unicode_length: invalid utf8");
    return;
  }
  jdl_return_i32(c, (int32_t)n);
}

void jdll_std_unicode_get(jdlo_ctx* c) {
  const uint8_t* s = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  int32_t idx = jdl_arg_i32(c, 1);
  if(!s && len) {
    jdl_fail(c, "unicode_get: null bytes");
    return;
  }
  if(idx < 0) {
    jdl_fail(c, "unicode_get: negative index");
    return;
  }
  uint32_t off = 0;
  if(!utf8_byte_offset_at_cp(s, len, idx, &off)) {
    jdl_fail(c, "unicode_get: index out of range");
    return;
  }
  uint32_t cp = 0, adv = 0;
  if(!utf8_decode_at(s, len, off, &cp, &adv)) {
    jdl_fail(c, "unicode_get: invalid utf8");
    return;
  }
  jdl_return_i32(c, (int32_t)cp);
}

void jdll_std_unicode_compare(jdlo_ctx* c) {
  const uint8_t* a = jdl_arg_bytes_data(c, 0);
  uint32_t alen = jdl_arg_bytes_len(c, 0);
  const uint8_t* b = jdl_arg_bytes_data(c, 1);
  uint32_t blen = jdl_arg_bytes_len(c, 1);
  if((!a && alen) || (!b && blen)) {
    jdl_fail(c, "unicode_compare: null bytes");
    return;
  }
  uint32_t ia = 0, ib = 0;
  for(;;) {
    uint32_t ca = 0, cb = 0, aa = 0, ab = 0;
    int ha = ia < alen ? utf8_decode_at(a, alen, ia, &ca, &aa) : 0;
    int hb = ib < blen ? utf8_decode_at(b, blen, ib, &cb, &ab) : 0;
    if(!ha && !hb) {
      jdl_return_i32(c, 0);
      return;
    }
    if(!ha) {
      jdl_return_i32(c, -1);
      return;
    }
    if(!hb) {
      jdl_return_i32(c, 1);
      return;
    }
    if(ca != cb) {
      jdl_return_i32(c, ca < cb ? -1 : 1);
      return;
    }
    ia += aa;
    ib += ab;
  }
}

void jdll_std_unicode_sub(jdlo_ctx* c) {
  const uint8_t* s = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  int32_t pos = jdl_arg_i32(c, 1);
  int32_t slen = jdl_arg_i32(c, 2);
  if(!s && len) {
    jdl_fail(c, "unicode_sub: null bytes");
    return;
  }
  if(pos < 0 || slen < 0) {
    jdl_fail(c, "unicode_sub: negative pos/len");
    return;
  }
  if(slen == 0) {
    jdl_return_bytes_copy(c, NULL, 0);
    return;
  }
  uint32_t start = 0;
  if(!utf8_byte_offset_at_cp(s, len, pos, &start)) {
    jdl_fail(c, "unicode_sub: pos out of range");
    return;
  }
  uint32_t i = start;
  for(int32_t k = 0; k < slen; k++) {
    uint32_t cp = 0, adv = 0;
    if(!utf8_decode_at(s, len, i, &cp, &adv)) {
      jdl_fail(c, "unicode_sub: invalid utf8");
      return;
    }
    i += adv;
  }
  if(i < start || i > len) {
    jdl_fail(c, "unicode_sub: slice out of range");
    return;
  }
  jdl_return_bytes_copy(c, s + start, i - start);
}

JDLL_DEFINE_KIND(utf8_buf);

typedef struct {
  uint8_t* data;
  uint32_t len;
  uint32_t cap;
  uint32_t chars;
} unicode_buf;

static void unicode_buf_fin(void* payload) {
  unicode_buf* b = (unicode_buf*)payload;
  if(!b) return;
  free(b->data);
  free(b);
}

static unicode_buf* unicode_buf_from_arg(jdlo_ctx* c, int index) {
  void* payload = jdl_arg_abstract_payload(c, index);
  if(!payload) return NULL;
  return (unicode_buf*)payload;
}

static int unicode_buf_grow(unicode_buf* b, uint32_t need) {
  if(b->len + need <= b->cap) return 1;
  uint32_t cap = b->cap ? b->cap : 8;
  while(cap < b->len + need) {
    if(cap > 0x7FFFFFFFu) return 0;
    cap *= 2;
  }
  uint8_t* next = (uint8_t*)realloc(b->data, cap);
  if(!next) return 0;
  b->data = next;
  b->cap = cap;
  return 1;
}

void jdll_std_unicode_from_codepoint(jdlo_ctx* c) {
  int32_t cp = jdl_arg_i32(c, 0);
  uint8_t out[4];
  uint32_t n = 0;
  if(!utf8_encode((uint32_t)cp, out, 4, &n)) {
    jdl_fail(c, "unicode_from_codepoint: invalid codepoint");
    return;
  }
  jdl_return_bytes_copy(c, out, n);
}

void jdll_std_unicode_buf_new(jdlo_ctx* c) {
  int32_t cap = jdl_arg_i32(c, 0);
  if(cap < 0) {
    jdl_fail(c, "unicode_buf_new: negative capacity");
    return;
  }
  uint32_t initial = cap == 0 ? 8u : (uint32_t)cap;
  unicode_buf* b = (unicode_buf*)calloc(1, sizeof(unicode_buf));
  if(!b) {
    jdl_fail(c, "unicode_buf_new: out of memory");
    return;
  }
  b->data = (uint8_t*)malloc(initial);
  if(!b->data) {
    free(b);
    jdl_fail(c, "unicode_buf_new: out of memory");
    return;
  }
  b->cap = initial;
  jdl_return_abstract(c, b, unicode_buf_fin);
}

void jdll_std_unicode_buf_add(jdlo_ctx* c) {
  unicode_buf* b = unicode_buf_from_arg(c, 0);
  int32_t cp = jdl_arg_i32(c, 1);
  if(!b) {
    jdl_fail(c, "unicode_buf_add: null buffer");
    return;
  }
  uint8_t out[4];
  uint32_t n = 0;
  if(!utf8_encode((uint32_t)cp, out, 4, &n)) {
    jdl_fail(c, "unicode_buf_add: invalid codepoint");
    return;
  }
  if(!unicode_buf_grow(b, n)) {
    jdl_fail(c, "unicode_buf_add: out of memory");
    return;
  }
  memcpy(b->data + b->len, out, n);
  b->len += n;
  b->chars += 1;
  jdl_return_bool(c, 1);
}

void jdll_std_unicode_buf_bytes(jdlo_ctx* c) {
  unicode_buf* b = unicode_buf_from_arg(c, 0);
  if(!b) {
    jdl_fail(c, "unicode_buf_bytes: null buffer");
    return;
  }
  jdl_return_bytes_copy(c, b->data, b->len);
}

void jdll_std_unicode_buf_length(jdlo_ctx* c) {
  unicode_buf* b = unicode_buf_from_arg(c, 0);
  if(!b) {
    jdl_fail(c, "unicode_buf_length: null buffer");
    return;
  }
  jdl_return_i32(c, (int32_t)b->chars);
}

void jdll_std_unicode_buf_size(jdlo_ctx* c) {
  unicode_buf* b = unicode_buf_from_arg(c, 0);
  if(!b) {
    jdl_fail(c, "unicode_buf_size: null buffer");
    return;
  }
  jdl_return_i32(c, (int32_t)b->len);
}
