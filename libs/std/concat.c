// SPDX-License-Identifier: BSD-3-Clause

#include <jello/jdll.h>

#include <stdlib.h>
#include <string.h>

void jdll_std_bytes_concat(jdlo_ctx* c) {
  int n = jdl_arg_count(c);
  uint32_t total = 0;
  for(int i = 0; i < n; i++) {
    if(!jdl_is_bytes(jdl_arg_value(c, i))) {
      jdl_fail(c, "bytes_concat: expected bytes");
      return;
    }
    total += jdl_arg_bytes_len(c, i);
  }
  if(total == 0) {
    jdl_return_bytes_copy(c, NULL, 0);
    return;
  }
  uint8_t* buf = (uint8_t*)malloc(total);
  if(!buf) {
    jdl_fail(c, "bytes_concat: out of memory");
    return;
  }
  uint32_t off = 0;
  for(int i = 0; i < n; i++) {
    uint32_t len = jdl_arg_bytes_len(c, i);
    const uint8_t* data = jdl_arg_bytes_data(c, i);
    if(len) memcpy(buf + off, data, len);
    off += len;
  }
  jdl_return_bytes_copy(c, buf, total);
  free(buf);
}
