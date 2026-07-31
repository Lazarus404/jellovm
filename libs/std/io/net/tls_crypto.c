// SPDX-License-Identifier: BSD-3-Clause

#include <jello.h>
#include <jello/internal/vm_internal.h>
#include <jello/jdll.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tls_internal.h"

JDLL_DEFINE_KIND(tls_context);

static void tls_context_fin(void* payload) {
  jdll_tls_context* tc = (jdll_tls_context*)payload;
  if(!tc) return;
  if(tc->ctx) SSL_CTX_free(tc->ctx);
  tc->ctx = NULL;
  free(tc);
}

static jdll_tls_context* tls_context_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_tls_context*)a->payload;
}

static uint32_t net_obj_type_id(const jello_bc_module* m) {
  if(!m) return 0;
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_OBJECT) return i;
  }
  return 0;
}

static uint32_t net_atom_id(const jello_bc_module* m, const char* name) {
  return vm_module_atom_id_or_default(m, name, 0);
}

static uint32_t net_atom_id_ctx(jdlo_ctx* c, const char* name) {
  struct jello_vm* vm = jdl_ctx_vm(c);
  if(vm && vm->running_module) {
    uint32_t aid = net_atom_id(vm->running_module, name);
    if(aid) return aid;
  }
  return net_atom_id(jdl_ctx_module(c), name);
}

static jello_object* make_record(jdlo_ctx* c, uint32_t nfields, const char** keys, jello_value* vals) {
  struct jello_vm* vm = jdl_ctx_vm(c);
  const jello_bc_module* m = jdl_ctx_module(c);
  if(!vm || !m) return NULL;
  jello_object* o = jello_object_new(vm, net_obj_type_id(m));
  if(!o) return NULL;
  for(uint32_t i = 0; i < nfields; i++) {
    uint32_t aid = net_atom_id_ctx(c, keys[i]);
    if(aid) jello_object_set(o, aid, vals[i]);
  }
  return o;
}

static int obj_get_atom_i32(jdlo_ctx* c, jello_object* o, const char* key, int32_t* out) {
  uint32_t aid = net_atom_id_ctx(c, key);
  if(!aid || !o) return 0;
  jello_value v = jello_object_get(o, aid);
  if(!jello_is_i32(v)) return 0;
  *out = jello_as_i32(v);
  return 1;
}

static jello_value obj_get_atom_val(jdlo_ctx* c, jello_object* o, const char* key) {
  uint32_t aid = net_atom_id_ctx(c, key);
  if(!aid || !o) return jello_make_null();
  return jello_object_get(o, aid);
}

static int atom_eq(jdlo_ctx* c, jello_value v, const char* name) {
  if(!jello_is_atom(v)) return 0;
  uint32_t aid = net_atom_id_ctx(c, name);
  return aid && jello_as_atom(v) == aid;
}

void jdll_std_pem_decode(jdlo_ctx* c) {
  const uint8_t* data = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  if(!data || !len) {
    jdl_return_null(c);
    return;
  }
  struct jello_vm* vm = jdl_ctx_vm(c);
  const jello_bc_module* m = jdl_ctx_module(c);
  if(!vm || !m) {
    jdl_fail(c, "pem_decode: no vm");
    return;
  }
  uint32_t list_tid = 0;
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_LIST) {
      list_tid = i;
      break;
    }
  }
  BIO* bio = BIO_new_mem_buf(data, (int)len);
  if(!bio) {
    jdl_fail(c, "pem_decode: oom");
    return;
  }
  jello_list* head = NULL;
  jello_list* tail = NULL;
  uint32_t bytes_tid = 0;
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_BYTES) {
      bytes_tid = i;
      break;
    }
  }
  for(;;) {
    char* name = NULL;
    char* header = NULL;
    unsigned char* der = NULL;
    long der_len = 0;
    if(!PEM_read_bio(bio, &name, &header, &der, &der_len)) break;
    jello_bytes* b = jello_bytes_new(vm, bytes_tid, (uint32_t)der_len);
    if(b && der_len > 0) memcpy(b->data, der, (size_t)der_len);
    OPENSSL_free(name);
    OPENSSL_free(header);
    OPENSSL_free(der);
    jello_list* node = jello_list_cons(vm, list_tid, jello_from_ptr(b), NULL);
    if(!head) {
      head = node;
      tail = node;
    } else {
      tail->tail = node;
      tail = node;
    }
  }
  BIO_free(bio);
  jdl_return_list(c, head);
}

void jdll_std_x509_parse(jdlo_ctx* c) {
  const uint8_t* data = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  if(!data || !len) {
    jdl_fail(c, "x509_parse: empty cert");
    return;
  }
  const unsigned char* p = data;
  X509* cert = d2i_X509(NULL, &p, (long)len);
  if(!cert) {
    BIO* bio = BIO_new_mem_buf(data, (int)len);
    if(bio) cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    if(bio) BIO_free(bio);
  }
  if(!cert) {
    jdl_fail(c, "x509_parse: parse failed");
    return;
  }
  char subj[256] = "";
  char iss[256] = "";
  X509_NAME* sn = X509_get_subject_name(cert);
  X509_NAME* in = X509_get_issuer_name(cert);
  if(sn) X509_NAME_oneline(sn, subj, (int)sizeof subj);
  if(in) X509_NAME_oneline(in, iss, (int)sizeof iss);
  X509_free(cert);

  struct jello_vm* vm = jdl_ctx_vm(c);
  const jello_bc_module* m = jdl_ctx_module(c);
  uint32_t bytes_tid = 0;
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_BYTES) {
      bytes_tid = i;
      break;
    }
  }
  jello_bytes* subj_b = jello_bytes_new(vm, bytes_tid, (uint32_t)strlen(subj));
  if(subj_b && subj[0]) memcpy(subj_b->data, subj, strlen(subj));
  jello_bytes* iss_b = jello_bytes_new(vm, bytes_tid, (uint32_t)strlen(iss));
  if(iss_b && iss[0]) memcpy(iss_b->data, iss, strlen(iss));
  const char* keys[] = {"subject", "issuer"};
  jello_value vals[2];
  vals[0] = subj_b ? jello_from_ptr(subj_b) : jello_make_null();
  vals[1] = iss_b ? jello_from_ptr(iss_b) : jello_make_null();
  jello_object* rec = make_record(c, 2, keys, vals);
  if(!rec) {
    jdl_fail(c, "x509_parse: oom");
    return;
  }
  jdl_return_object(c, rec);
}

void jdll_std_x509_verify_chain(jdlo_ctx* c) {
  const uint8_t* data = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  jdll_tls_context* tc = tls_context_from_arg(c, 1);
  if(!data || !len || !tc || !tc->ctx) {
    jdl_fail(c, "x509_verify_chain: bad args");
    return;
  }
  const unsigned char* p = data;
  X509* cert = d2i_X509(NULL, &p, (long)len);
  if(!cert) {
    jdl_return_bool(c, 0);
    return;
  }
  X509_STORE* store = SSL_CTX_get_cert_store(tc->ctx);
  X509_STORE_CTX* sctx = X509_STORE_CTX_new();
  int ok = 0;
  if(sctx && store && X509_STORE_CTX_init(sctx, store, cert, NULL)) {
    ok = X509_verify_cert(sctx) == 1;
  }
  if(sctx) X509_STORE_CTX_free(sctx);
  X509_free(cert);
  jdl_return_bool(c, ok);
}

void jdll_std_tls_context_new(jdlo_ctx* c) {
  (void)OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
  jello_object* opts = jdl_arg_object(c, 0);
  const jello_bc_module* m = jdl_ctx_module(c);
  if(!opts || !m) {
    jdl_fail(c, "tls_context_new: expected options object");
    return;
  }
  int is_server = atom_eq(c, obj_get_atom_val(c, opts, "role"), "server");
  int is_dtls = atom_eq(c, obj_get_atom_val(c, opts, "transport"), "datagram");
  int32_t verify = 1;
  (void)obj_get_atom_i32(c, opts, "verify", &verify);

  const SSL_METHOD* method = NULL;
  if(is_dtls) {
    method = is_server ? DTLS_server_method() : DTLS_client_method();
  } else {
    method = is_server ? TLS_server_method() : TLS_client_method();
  }
  SSL_CTX* ctx = SSL_CTX_new(method);
  if(!ctx) {
    jdl_fail(c, "tls_context_new: SSL_CTX_new failed");
    return;
  }
  if(verify) {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_default_verify_paths(ctx);
  } else {
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
  }

  jdll_tls_context* tc = (jdll_tls_context*)calloc(1, sizeof(*tc));
  if(!tc) {
    SSL_CTX_free(ctx);
    jdl_fail(c, "tls_context_new: oom");
    return;
  }
  tc->ctx = ctx;
  tc->is_server = is_server;
  tc->is_dtls = is_dtls;
  tc->verify = verify;
  jdl_return_abstract(c, tc, tls_context_fin);
}

static int tls_load_pem_bytes(SSL_CTX* ctx, const uint8_t* data, uint32_t len, int kind) {
  BIO* bio = BIO_new_mem_buf(data, (int)len);
  if(!bio) return 0;
  int ok = 0;
  if(kind == 0 || kind == 1) {
    X509* cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    if(cert) ok = SSL_CTX_use_certificate(ctx, cert) == 1;
    if(cert) X509_free(cert);
  } else {
    EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    if(key) ok = SSL_CTX_use_PrivateKey(ctx, key) == 1;
    if(key) EVP_PKEY_free(key);
  }
  BIO_free(bio);
  return ok;
}

void jdll_std_tls_context_set_ca(jdlo_ctx* c) {
  jdll_tls_context* tc = tls_context_from_arg(c, 0);
  const uint8_t* data = jdl_arg_bytes_data(c, 1);
  uint32_t len = jdl_arg_bytes_len(c, 1);
  if(!tc || !tc->ctx || !data) {
    jdl_fail(c, "tls_context_set_ca: bad args");
    return;
  }
  BIO* bio = BIO_new_mem_buf(data, (int)len);
  if(!bio) {
    jdl_fail(c, "tls_context_set_ca: oom");
    return;
  }
  X509* cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
  BIO_free(bio);
  if(!cert) {
    jdl_fail(c, "tls_context_set_ca: parse failed");
    return;
  }
  X509_STORE* store = SSL_CTX_get_cert_store(tc->ctx);
  int ok = store && X509_STORE_add_cert(store, cert) == 1;
  X509_free(cert);
  if(ok && !tc->is_server) {
    tc->verify = 1;
    SSL_CTX_set_verify(tc->ctx, SSL_VERIFY_PEER, NULL);
  }
  jdl_return_bool(c, ok);
}

void jdll_std_tls_context_set_cert_chain(jdlo_ctx* c) {
  jdll_tls_context* tc = tls_context_from_arg(c, 0);
  const uint8_t* data = jdl_arg_bytes_data(c, 1);
  uint32_t len = jdl_arg_bytes_len(c, 1);
  if(!tc || !tc->ctx || !data) {
    jdl_fail(c, "tls_context_set_cert_chain: bad args");
    return;
  }
  jdl_return_bool(c, tls_load_pem_bytes(tc->ctx, data, len, 1));
}

void jdll_std_tls_context_set_private_key(jdlo_ctx* c) {
  jdll_tls_context* tc = tls_context_from_arg(c, 0);
  const uint8_t* data = jdl_arg_bytes_data(c, 1);
  uint32_t len = jdl_arg_bytes_len(c, 1);
  if(!tc || !tc->ctx || !data) {
    jdl_fail(c, "tls_context_set_private_key: bad args");
    return;
  }
  jdl_return_bool(c, tls_load_pem_bytes(tc->ctx, data, len, 2));
}

void jdll_std_tls_context_set_alpn(jdlo_ctx* c) {
  jdll_tls_context* tc = tls_context_from_arg(c, 0);
  const uint8_t* data = jdl_arg_bytes_data(c, 1);
  uint32_t len = jdl_arg_bytes_len(c, 1);
  if(!tc || !tc->ctx || tc->is_dtls) {
    jdl_fail(c, "tls_context_set_alpn: ALPN not supported for DTLS v1");
    return;
  }
  if(!data || !len) {
    jdl_return_bool(c, 0);
    return;
  }
  unsigned char alpn[256];
  alpn[0] = (unsigned char)len;
  memcpy(alpn + 1, data, len > 255 ? 255 : len);
  int ok = SSL_CTX_set_alpn_protos(tc->ctx, alpn, (unsigned int)(len > 255 ? 256 : len + 1)) == 0;
  jdl_return_bool(c, ok ? 1 : 0);
}

void jdll_std_tls_context_set_sni(jdlo_ctx* c) {
  jdll_tls_context* tc = tls_context_from_arg(c, 0);
  const uint8_t* data = jdl_arg_bytes_data(c, 1);
  uint32_t len = jdl_arg_bytes_len(c, 1);
  if(!tc || !tc->ctx || !data || !len) {
    jdl_fail(c, "tls_context_set_sni: bad args");
    return;
  }
  char host[256];
  uint32_t n = len < 255 ? len : 255;
  memcpy(host, data, n);
  host[n] = 0;
  snprintf(tc->sni, sizeof tc->sni, "%s", host);
  jdl_return_bool(c, 1);
}
