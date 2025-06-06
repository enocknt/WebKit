/*
 * Copyright (C) 2012-2023 Apple Inc. All rights reserved.
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
#include "DFGDisassembler.h"

#if ENABLE(DFG_JIT)

#include "CodeBlockWithJITType.h"
#include "DFGGraph.h"
#include "Disassembler.h"
#include "JSCJSValueInlines.h"
#include "LinkBuffer.h"
#include <wtf/TZoneMallocInlines.h>

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace JSC { namespace DFG {

WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED_IMPL(Disassembler);

Disassembler::Disassembler(Graph& graph)
    : m_graph(graph)
{
    m_dumpContext.graph = &m_graph;
    m_labelForBlockIndex.grow(graph.numBlocks());
}

void Disassembler::dump(PrintStream& out, LinkBuffer& linkBuffer)
{
    Vector<DumpedOp> ops = createDumpList(linkBuffer);
    for (unsigned i = 0; i < ops.size(); ++i)
        out.print(ops[i].text);
}

void Disassembler::dump(LinkBuffer& linkBuffer)
{
    dump(WTF::dataFile(), linkBuffer);
}

void Disassembler::reportToProfiler(Profiler::Compilation* compilation, LinkBuffer& linkBuffer)
{
    Vector<DumpedOp> ops = createDumpList(linkBuffer);
    
    for (unsigned i = 0; i < ops.size(); ++i) {
        Profiler::OriginStack stack;
        
        if (ops[i].codeOrigin.isSet())
            stack = Profiler::OriginStack(*m_graph.m_vm.m_perBytecodeProfiler, m_graph.m_codeBlock, ops[i].codeOrigin);
        
        compilation->addDescription(Profiler::CompiledBytecode(stack, ops[i].text));
    }
}

void Disassembler::dumpHeader(PrintStream& out, LinkBuffer& linkBuffer)
{
    out.print("Generated DFG JIT code for ", CodeBlockWithJITType(m_graph.m_codeBlock, JITType::DFGJIT), ", instructions size = ", m_graph.m_codeBlock->instructionsSize(), ":\n");
    out.print("    Optimized with execution counter = ", m_graph.m_profiledBlock->baselineExecuteCounter(), "\n");
    out.print("    Code at [", RawPointer(linkBuffer.debugAddress()), ", ", RawPointer(static_cast<char*>(linkBuffer.debugAddress()) + linkBuffer.size()), "):\n");
}

void Disassembler::append(Vector<Disassembler::DumpedOp>& result, StringPrintStream& out, CodeOrigin& previousOrigin)
{
    result.append(DumpedOp(previousOrigin, out.toCString()));
    previousOrigin = CodeOrigin();
    out.reset();
}

Vector<Disassembler::DumpedOp> Disassembler::createDumpList(LinkBuffer& linkBuffer)
{
    StringPrintStream out;
    Vector<DumpedOp> result;
    
    CodeOrigin previousOrigin = CodeOrigin();
    dumpHeader(out, linkBuffer);
    append(result, out, previousOrigin);
    
    m_graph.ensureCPSDominators();
    m_graph.ensureCPSNaturalLoops();
    
    const char* prefix = "    ";
    const char* disassemblyPrefix = "        ";
    
    Node* lastNode = nullptr;
    MacroAssembler::Label previousLabel = m_startOfCode;
    for (size_t blockIndex = 0; blockIndex < m_graph.numBlocks(); ++blockIndex) {
        BasicBlock* block = m_graph.block(blockIndex);
        if (!block)
            continue;
        dumpDisassembly(out, disassemblyPrefix, linkBuffer, previousLabel, m_labelForBlockIndex[blockIndex], lastNode);
        append(result, out, previousOrigin);
        m_graph.dumpBlockHeader(out, prefix, block, Graph::DumpLivePhisOnly, &m_dumpContext);
        append(result, out, previousOrigin);
        Node* lastNodeForDisassembly = block->at(0);
        for (size_t i = 0; i < block->size(); ++i) {
            MacroAssembler::Label currentLabel;
            UncheckedKeyHashMap<Node*, MacroAssembler::Label>::iterator iter = m_labelForNode.find(block->at(i));
            if (iter != m_labelForNode.end())
                currentLabel = iter->value;
            else {
                // Dump the last instruction by using the first label of the next block
                // as the end point. This case is hit either during peephole compare
                // optimizations (the Branch won't have its own label) or if we have a
                // forced OSR exit.
                if (blockIndex + 1 < m_graph.numBlocks())
                    currentLabel = m_labelForBlockIndex[blockIndex + 1];
                else
                    currentLabel = m_endOfMainPath;
            }
            dumpDisassembly(out, disassemblyPrefix, linkBuffer, previousLabel, currentLabel, lastNodeForDisassembly);
            append(result, out, previousOrigin);
            previousOrigin = block->at(i)->origin.semantic;
            if (m_graph.dumpCodeOrigin(out, prefix, lastNode, block->at(i), &m_dumpContext)) {
                append(result, out, previousOrigin);
                previousOrigin = block->at(i)->origin.semantic;
            }
            m_graph.dump(out, prefix, block->at(i), &m_dumpContext);
            lastNode = block->at(i);
            lastNodeForDisassembly = block->at(i);
        }
    }
    dumpDisassembly(out, disassemblyPrefix, linkBuffer, previousLabel, m_endOfMainPath, lastNode);
    append(result, out, previousOrigin);
    out.print(prefix, "(End Of Main Path)\n");
    append(result, out, previousOrigin);
    dumpDisassembly(out, disassemblyPrefix, linkBuffer, previousLabel, m_endOfCode, nullptr);
    append(result, out, previousOrigin);
    m_dumpContext.dump(out, prefix);
    append(result, out, previousOrigin);
    
    return result;
}

void Disassembler::dumpDisassembly(PrintStream& out, const char* prefix, LinkBuffer& linkBuffer, MacroAssembler::Label& previousLabel, MacroAssembler::Label currentLabel, Node* context)
{
    size_t prefixLength = strlen(prefix);
    int amountOfNodeWhiteSpace;
    if (!context)
        amountOfNodeWhiteSpace = 0;
    else
        amountOfNodeWhiteSpace = Graph::amountOfNodeWhiteSpace(context);
    Vector<char> prefixBuffer(prefixLength + amountOfNodeWhiteSpace + 1);
    memcpy(prefixBuffer.mutableSpan().data(), prefix, prefixLength);
    for (int i = 0; i < amountOfNodeWhiteSpace; ++i)
        prefixBuffer[i + prefixLength] = ' ';
    prefixBuffer[prefixLength + amountOfNodeWhiteSpace] = 0;
    
    void* codeStart = linkBuffer.entrypoint<DisassemblyPtrTag>().untaggedPtr();
    void* codeEnd = std::bit_cast<uint8_t*>(codeStart) +  linkBuffer.size();

    CodeLocationLabel<DisassemblyPtrTag> start = linkBuffer.locationOf<DisassemblyPtrTag>(previousLabel);
    CodeLocationLabel<DisassemblyPtrTag> end = linkBuffer.locationOf<DisassemblyPtrTag>(currentLabel);
    previousLabel = currentLabel;
    ASSERT(end.dataLocation<uintptr_t>() >= start.dataLocation<uintptr_t>());
    disassemble(start, end.dataLocation<uintptr_t>() - start.dataLocation<uintptr_t>(), codeStart, codeEnd, prefixBuffer.span().data(), out);
}

} } // namespace JSC::DFG

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

#endif // ENABLE(DFG_JIT)
