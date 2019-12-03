// SPDX-License-Identifier: BSD-3-Clause

#include <jello/jdll.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#if !defined(_WIN32)
#  include <unistd.h>
#endif

static long jdll_random_mod(long span) {
#if defined(_WIN32)
  static int seeded = 0;
  if(!seeded) {
    srand((unsigned)time(NULL));
    seeded = 1;
  }
  return (long)(rand() % span);
#else
  return random() % span;
#endif
}

void jdll_std_random_int(jdlo_ctx* c) {
  int32_t lo = jdl_arg_i32(c, 0);
  int32_t hi = jdl_arg_i32(c, 1);
  if(hi < lo) {
    int32_t t = lo;
    lo = hi;
    hi = t;
  }
  int32_t span = hi - lo + 1;
  if(span <= 0) {
    jdl_return_i32(c, lo);
    return;
  }
  jdl_return_i32(c, lo + (int32_t)jdll_random_mod((long)span));
}
