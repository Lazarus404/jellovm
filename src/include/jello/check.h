/**
 * Copyright 2017 - Jahred Love
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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

