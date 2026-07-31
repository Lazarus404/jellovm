// SPDX-License-Identifier: BSD-3-Clause
#ifndef JELLOVM_STD_TLS_INTERNAL_H
#define JELLOVM_STD_TLS_INTERNAL_H

#include <openssl/ssl.h>

typedef struct jdll_tls_context {
  SSL_CTX* ctx;
  int is_server;
  int is_dtls;
  int verify;
  char sni[256];
} jdll_tls_context;

typedef struct jdll_tls_stream {
  SSL* ssl;
  int fd;
  int is_dtls;
  int owns_fd;
} jdll_tls_stream;

#endif
