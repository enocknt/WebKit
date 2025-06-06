/*
 * Copyright (c) 2021-2024 Apple Inc. All rights reserved.
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
#include "WGSL.h"

#include "ASTIdentifierExpression.h"
#include "AttributeValidator.h"
#include "BoundsCheck.h"
#include "CallGraph.h"
#include "EntryPointRewriter.h"
#include "GlobalSorting.h"
#include "GlobalVariableRewriter.h"
#include "MangleNames.h"
#include "Metal/MetalCodeGenerator.h"
#include "Parser.h"
#include "PhaseTimer.h"
#include "PointerRewriter.h"
#include "TypeCheck.h"
#include "VisibilityValidator.h"
#include "WGSLShaderModule.h"

namespace WGSL {

#define CHECK_PASS(pass, ...) \
    dumpASTBetweenEachPassIfNeeded(shaderModule, "AST before " # pass); \
    auto maybe##pass##Failure = [&]() { \
        PhaseTimer phaseTimer(#pass, phaseTimes); \
        return pass(__VA_ARGS__); \
    }(); \
    if (maybe##pass##Failure) \
        return { *maybe##pass##Failure };

#define RUN_PASS(pass, ...) \
    do { \
        PhaseTimer phaseTimer(#pass, phaseTimes); \
        dumpASTBetweenEachPassIfNeeded(shaderModule, "AST before " # pass); \
        pass(__VA_ARGS__); \
    } while (0)

#define RUN_PASS_WITH_RESULT(name, pass, ...) \
    dumpASTBetweenEachPassIfNeeded(shaderModule, "AST before " # pass); \
    auto name = [&]() { \
        PhaseTimer phaseTimer(#pass, phaseTimes); \
        return pass(__VA_ARGS__); \
    }();

Variant<SuccessfulCheck, FailedCheck> staticCheck(const String& wgsl, const std::optional<SourceMap>&, const Configuration& configuration)
{
    PhaseTimes phaseTimes;
    auto shaderModule = makeUniqueRef<ShaderModule>(wgsl, configuration);

    CHECK_PASS(parse, shaderModule);
    CHECK_PASS(reorderGlobals, shaderModule);
    CHECK_PASS(typeCheck, shaderModule);
    CHECK_PASS(validateAttributes, shaderModule);
    RUN_PASS(buildCallGraph, shaderModule);
    CHECK_PASS(validateIO, shaderModule);
    CHECK_PASS(validateVisibility, shaderModule);
    RUN_PASS(mangleNames, shaderModule);

    Vector<Warning> warnings { };
    return Variant<SuccessfulCheck, FailedCheck>(WTF::InPlaceType<SuccessfulCheck>, WTFMove(warnings), WTFMove(shaderModule));
}

SuccessfulCheck::SuccessfulCheck(SuccessfulCheck&&) = default;

SuccessfulCheck::SuccessfulCheck(Vector<Warning>&& messages, UniqueRef<ShaderModule>&& shader)
    : warnings(WTFMove(messages))
    , ast(WTFMove(shader))
{
}

SuccessfulCheck::~SuccessfulCheck() = default;

inline Variant<PrepareResult, Error> prepareImpl(ShaderModule& shaderModule, const HashMap<String, PipelineLayout*>& pipelineLayouts)
{
    CompilationScope compilationScope(shaderModule);

    PhaseTimes phaseTimes;
    auto result = [&]() -> Variant<PrepareResult, Error> {
        PhaseTimer phaseTimer("prepare total", phaseTimes);

        HashMap<String, Reflection::EntryPointInformation> entryPoints;

        RUN_PASS(insertBoundsChecks, shaderModule);
        RUN_PASS(rewritePointers, shaderModule);
        RUN_PASS(rewriteEntryPoints, shaderModule, pipelineLayouts);
        CHECK_PASS(rewriteGlobalVariables, shaderModule, pipelineLayouts, entryPoints);

        dumpASTAtEndIfNeeded(shaderModule);

        return { PrepareResult { WTFMove(entryPoints), WTFMove(compilationScope) } };
    }();

    logPhaseTimes(phaseTimes);

    return result;
}

Variant<String, Error> generate(ShaderModule& shaderModule, PrepareResult& prepareResult, HashMap<String, ConstantValue>& constantValues, DeviceState&& deviceState)
{
    PhaseTimes phaseTimes;
    String result;
    if (auto maybeError = shaderModule.validateOverrides(constantValues))
        return { *maybeError };
    {
        PhaseTimer phaseTimer("generateMetalCode", phaseTimes);
        result = Metal::generateMetalCode(shaderModule, prepareResult, constantValues, WTFMove(deviceState));
    }
    logPhaseTimes(phaseTimes);
    return { result };
}

Variant<PrepareResult, Error> prepare(ShaderModule& ast, const HashMap<String, PipelineLayout*>& pipelineLayouts)
{
    return prepareImpl(ast, pipelineLayouts);
}

Variant<PrepareResult, Error> prepare(ShaderModule& ast, const String& entryPointName, PipelineLayout* pipelineLayout)
{
    HashMap<String, PipelineLayout*> pipelineLayouts;
    pipelineLayouts.add(entryPointName, pipelineLayout);
    return prepareImpl(ast, pipelineLayouts);
}

std::optional<ConstantValue> evaluate(const AST::Expression& expression, const HashMap<String, ConstantValue>& constants)
{
    std::optional<ConstantValue> result;
    if (auto constantValue = expression.constantValue())
        result = *constantValue;
    auto* maybeIdentifierExpression = dynamicDowncast<const AST::IdentifierExpression>(expression);
    if (!maybeIdentifierExpression)
        return result;
    auto it = constants.find(maybeIdentifierExpression->identifier());
    if (it == constants.end())
        return result;
    const_cast<AST::Expression&>(expression).setConstantValue(it->value);
    return it->value;
}

}
