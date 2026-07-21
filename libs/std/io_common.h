// SPDX-License-Identifier: BSD-3-Clause
#ifndef JELLOVM_STD_IO_COMMON_H
#define JELLOVM_STD_IO_COMMON_H

#include <jello/jdll.h>

const char* io_errno_code(int err);
int io_last_errno(void);
void io_fail(jdlo_ctx* c, const char* op);
void io_fail_errno(jdlo_ctx* c, const char* op, int err);
void io_fail_code(jdlo_ctx* c, const char* op, const char* code);

#endif
