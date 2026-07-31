// SPDX-License-Identifier: BSD-3-Clause

#include <jello/jdll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void jdll_std_time(jdlo_ctx* c) {
  jdl_return_i32(c, (int32_t)time(NULL));
}

void jdll_std_get_env(jdlo_ctx* c) {
  const uint8_t* key = jdl_arg_bytes_data(c, 0);
  uint32_t klen = jdl_arg_bytes_len(c, 0);
  if(!key || !klen) {
    jdl_return_bytes_copy(c, (const uint8_t*)"", 0);
    return;
  }
  char buf[256];
  uint32_t n = klen < 255u ? klen : 255u;
  memcpy(buf, key, n);
  buf[n] = 0;
  const char* val = getenv(buf);
  if(!val) {
    jdl_return_bytes_copy(c, (const uint8_t*)"", 0);
    return;
  }
  jdl_return_bytes_copy(c, (const uint8_t*)val, (uint32_t)strlen(val));
}

void jdll_std_sleep(jdlo_ctx* c) {
  int32_t sec = jdl_arg_i32(c, 0);
  if(sec > 0) {
    if(sec > 3600) sec = 3600;
    sleep((unsigned int)sec);
  }
  jdl_return_i32(c, sec > 0 ? sec : 0);
}
