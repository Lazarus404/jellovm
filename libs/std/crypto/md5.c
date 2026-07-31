// SPDX-License-Identifier: BSD-3-Clause
// MD5 over raw bytes (16-byte digest). Ported from Neko std/md5.c (digest core only).

#include <jello/jdll.h>

#include <stdint.h>
#include <string.h>

typedef struct {
  uint32_t total[2];
  uint32_t state[4];
  uint8_t buffer[64];
} md5_context;

#define GET_UINT32(n, b, i)                       \
  do {                                            \
    (n) = ((uint32_t)(b)[(i)]) | ((uint32_t)(b)[(i) + 1] << 8) | \
          ((uint32_t)(b)[(i) + 2] << 16) | ((uint32_t)(b)[(i) + 3] << 24); \
  } while(0)

#define PUT_UINT32(n, b, i)          \
  do {                               \
    (b)[(i)] = (uint8_t)(n);         \
    (b)[(i) + 1] = (uint8_t)((n) >> 8); \
    (b)[(i) + 2] = (uint8_t)((n) >> 16); \
    (b)[(i) + 3] = (uint8_t)((n) >> 24); \
  } while(0)

static void md5_starts(md5_context* ctx) {
  ctx->total[0] = ctx->total[1] = 0;
  ctx->state[0] = 0x67452301u;
  ctx->state[1] = 0xEFCDAB89u;
  ctx->state[2] = 0x98BADCFEu;
  ctx->state[3] = 0x10325476u;
}

static void md5_process(md5_context* ctx, const uint8_t data[64]) {
  uint32_t X[16], A, B, C, D;
  for(int i = 0; i < 16; i++) GET_UINT32(X[i], data, i * 4);

#define S(x, n) (((x) << (n)) | (((x) & 0xFFFFFFFFu) >> (32 - (n))))
#define P(a, b, c, d, k, s, t) \
  do { a += F(b, c, d) + X[k] + (t); a = S(a, s) + b; } while(0)

  A = ctx->state[0];
  B = ctx->state[1];
  C = ctx->state[2];
  D = ctx->state[3];

#define F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
  P(A, B, C, D, 0, 7, 0xD76AA478u);
  P(D, A, B, C, 1, 12, 0xE8C7B756u);
  P(C, D, A, B, 2, 17, 0x242070DBu);
  P(B, C, D, A, 3, 22, 0xC1BDCEEEu);
  P(A, B, C, D, 4, 7, 0xF57C0FAFu);
  P(D, A, B, C, 5, 12, 0x4787C62Au);
  P(C, D, A, B, 6, 17, 0xA8304613u);
  P(B, C, D, A, 7, 22, 0xFD469501u);
  P(A, B, C, D, 8, 7, 0x698098D8u);
  P(D, A, B, C, 9, 12, 0x8B44F7AFu);
  P(C, D, A, B, 10, 17, 0xFFFF5BB1u);
  P(B, C, D, A, 11, 22, 0x895CD7BEu);
  P(A, B, C, D, 12, 7, 0x6B901122u);
  P(D, A, B, C, 13, 12, 0xFD987193u);
  P(C, D, A, B, 14, 17, 0xA679438Eu);
  P(B, C, D, A, 15, 22, 0x49B40821u);
#undef F
#define F(x, y, z) ((y) ^ ((z) & ((x) ^ (y))))
  P(A, B, C, D, 1, 5, 0xF61E2562u);
  P(D, A, B, C, 6, 9, 0xC040B340u);
  P(C, D, A, B, 11, 14, 0x265E5A51u);
  P(B, C, D, A, 0, 20, 0xE9B6C7AAu);
  P(A, B, C, D, 5, 5, 0xD62F105Du);
  P(D, A, B, C, 10, 9, 0x02441453u);
  P(C, D, A, B, 15, 14, 0xD8A1E681u);
  P(B, C, D, A, 4, 20, 0xE7D3FBC8u);
  P(A, B, C, D, 9, 5, 0x21E1CDE6u);
  P(D, A, B, C, 14, 9, 0xC33707D6u);
  P(C, D, A, B, 3, 14, 0xF4D50D87u);
  P(B, C, D, A, 8, 20, 0x455A14EDu);
  P(A, B, C, D, 13, 5, 0xA9E3E905u);
  P(D, A, B, C, 2, 9, 0xFCEFA3F8u);
  P(C, D, A, B, 7, 14, 0x676F02D9u);
  P(B, C, D, A, 12, 20, 0x8D2A4C8Au);
#undef F
#define F(x, y, z) ((x) ^ (y) ^ (z))
  P(A, B, C, D, 5, 4, 0xFFFA3942u);
  P(D, A, B, C, 8, 11, 0x8771F681u);
  P(C, D, A, B, 11, 16, 0x6D9D6122u);
  P(B, C, D, A, 14, 23, 0xFDE5380Cu);
  P(A, B, C, D, 1, 4, 0xA4BEEA44u);
  P(D, A, B, C, 4, 11, 0x4BDECFA9u);
  P(C, D, A, B, 7, 16, 0xF6BB4B60u);
  P(B, C, D, A, 10, 23, 0xBEBFBC70u);
  P(A, B, C, D, 13, 4, 0x289B7EC6u);
  P(D, A, B, C, 0, 11, 0xEAA127FAu);
  P(C, D, A, B, 3, 16, 0xD4EF3085u);
  P(B, C, D, A, 6, 23, 0x04881D05u);
  P(A, B, C, D, 9, 4, 0xD9D4D039u);
  P(D, A, B, C, 12, 11, 0xE6DB99E5u);
  P(C, D, A, B, 15, 16, 0x1FA27CF8u);
  P(B, C, D, A, 2, 23, 0xC4AC5665u);
#undef F
#define F(x, y, z) ((y) ^ ((x) | ~(z)))
  P(A, B, C, D, 0, 6, 0xF4292244u);
  P(D, A, B, C, 7, 10, 0x432AFF97u);
  P(C, D, A, B, 14, 15, 0xAB9423A7u);
  P(B, C, D, A, 5, 21, 0xFC93A039u);
  P(A, B, C, D, 12, 6, 0x655B59C3u);
  P(D, A, B, C, 3, 10, 0x8F0CCC92u);
  P(C, D, A, B, 10, 15, 0xFFEFF47Du);
  P(B, C, D, A, 1, 21, 0x85845DD1u);
  P(A, B, C, D, 8, 6, 0x6FA87E4Fu);
  P(D, A, B, C, 15, 10, 0xFE2CE6E0u);
  P(C, D, A, B, 6, 15, 0xA3014314u);
  P(B, C, D, A, 13, 21, 0x4E0811A1u);
  P(A, B, C, D, 4, 6, 0xF7537E82u);
  P(D, A, B, C, 11, 10, 0xBD3AF235u);
  P(C, D, A, B, 2, 15, 0x2AD7D2BBu);
  P(B, C, D, A, 9, 21, 0xEB86D391u);
#undef F

  ctx->state[0] += A;
  ctx->state[1] += B;
  ctx->state[2] += C;
  ctx->state[3] += D;
#undef P
#undef S
}

static void md5_update(md5_context* ctx, const uint8_t* input, uint32_t length) {
  uint32_t left, fill;
  if(!length) return;
  left = ctx->total[0] & 0x3Fu;
  fill = 64u - left;
  ctx->total[0] += length;
  if(ctx->total[0] < length) ctx->total[1]++;
  if(left && length >= fill) {
    memcpy(ctx->buffer + left, input, fill);
    md5_process(ctx, ctx->buffer);
    length -= fill;
    input += fill;
    left = 0;
  }
  while(length >= 64u) {
    md5_process(ctx, input);
    length -= 64u;
    input += 64u;
  }
  if(length) memcpy(ctx->buffer + left, input, length);
}

static const uint8_t md5_padding[64] = { 0x80 };

static void md5_finish(md5_context* ctx, uint8_t digest[16]) {
  uint32_t last = ctx->total[0] & 0x3Fu;
  uint32_t padn = (last < 56u) ? (56u - last) : (120u - last);
  uint8_t msglen[8];
  uint32_t low = ctx->total[0] << 3;
  uint32_t high = (ctx->total[0] >> 29) | (ctx->total[1] << 3);
  PUT_UINT32(low, msglen, 0);
  PUT_UINT32(high, msglen, 4);
  md5_update(ctx, md5_padding, padn);
  md5_update(ctx, msglen, 8);
  PUT_UINT32(ctx->state[0], digest, 0);
  PUT_UINT32(ctx->state[1], digest, 4);
  PUT_UINT32(ctx->state[2], digest, 8);
  PUT_UINT32(ctx->state[3], digest, 12);
}

void jdll_std_md5(jdlo_ctx* c) {
  const uint8_t* data = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  md5_context ctx;
  uint8_t digest[16];
  md5_starts(&ctx);
  if(len && data) md5_update(&ctx, data, len);
  md5_finish(&ctx, digest);
  jdl_return_bytes_copy(c, digest, 16);
}
