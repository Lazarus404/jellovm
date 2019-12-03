// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#ifndef JELLO_BYTECODE_CHECK_H
#define JELLO_BYTECODE_CHECK_H

#include <jello.h>

// Instruction validation that depends on opcode semantics.
jello_bc_result jello_bc_validate_insn(const jello_bc_module* m,
                                      const jello_type_id* reg_types,
                                      const jello_insn* ins,
                                      uint32_t nregs,
                                      uint32_t pc,
                                      uint32_t ninsns,
                                      uint32_t nfuncs);

// Post-parse validation that needs visibility of the full instruction stream
// (e.g. jump tables / multi-insn encoding constraints).
jello_bc_result jello_bc_validate_function_semantics(const jello_bc_module* m,
                                                    const jello_type_id* reg_types,
                                                    const jello_insn* insns,
                                                    uint32_t nregs,
                                                    uint32_t ninsns);

#endif /* JELLO_BYTECODE_CHECK_H */

