// SPDX-License-Identifier: BSD-3-Clause
#ifndef JELLOVM_STD_IO_COMMON_H
#define JELLOVM_STD_IO_COMMON_H

#include <jello/jdll.h>

const char* io_errno_code(int err);
/* Prefer WSAGetLastError on Windows (Winsock). Use io_errno_value for usrsctp/CRT. */
int io_winsock_errno(void);
int io_errno_value(void);
int io_last_errno(void);
void io_fail(jdlo_ctx* c, const char* op);
void io_fail_errno(jdlo_ctx* c, const char* op, int err);
void io_fail_code(jdlo_ctx* c, const char* op, const char* code);
void io_fail_winsock(jdlo_ctx* c, const char* op);

#endif
