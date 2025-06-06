/*
 * Copyright (C) 2018 Yusuke Suzuki <utatane.tea@gmail.com>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

#if ENABLE(DISASSEMBLER) && USE(CAPSTONE)

#include "AssemblyComments.h"
#include "MacroAssemblerCodeRef.h"
#include "Options.h"
#include <capstone/capstone.h>

namespace JSC {

bool tryToDisassemble(const CodePtr<DisassemblyPtrTag>& codePtr, size_t size, void*, void*, const char* prefix, PrintStream& out)
{
    csh handle;
    cs_insn* instructions;

#if CPU(ARM_THUMB2)
    if (cs_open(CS_ARCH_ARM, CS_MODE_THUMB, &handle) != CS_ERR_OK)
        return false;
    if (cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_NOREGNAME) != CS_ERR_OK)
        return false;
#elif CPU(ARM64)
    if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &handle) != CS_ERR_OK)
        return false;
#else
    return false;
#endif

    size_t count = cs_disasm(handle, codePtr.dataLocation<unsigned char*>(), size, codePtr.dataLocation<uintptr_t>(), 0, &instructions);
    if (count > 0) {
        for (size_t i = 0; i < count; ++i) {
            auto& instruction = instructions[i];
            out.printf("%s%#16llx: %s %s", prefix, static_cast<unsigned long long>(instruction.address), instruction.mnemonic, instruction.op_str);
            if (auto str = AssemblyCommentRegistry::singleton().comment(reinterpret_cast<void *>(static_cast<uintptr_t>(instruction.address))))
                out.printf("; %s\n", str->ascii().data());
            else
                out.printf("\n");
        }
        cs_free(instructions, count);
    }
    cs_close(&handle);
    return true;
}

} // namespace JSC

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

#endif // ENABLE(DISASSEMBLER) && USE(CAPSTONE)
