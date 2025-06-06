/*
 * Copyright (C) 2019-2023 Apple Inc. All rights reserved.
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
#include "WasmGlobal.h"

#if ENABLE(WEBASSEMBLY)

#include "JSCJSValueInlines.h"
#include "JSWebAssemblyGlobal.h"
#include "JSWebAssemblyHelpers.h"
#include "JSWebAssemblyRuntimeError.h"
#include "WasmTypeDefinitionInlines.h"
#include <wtf/StdLibExtras.h>
#include <wtf/TZoneMallocInlines.h>

namespace JSC { namespace Wasm {

WTF_MAKE_TZONE_ALLOCATED_IMPL(Global);

JSValue Global::get(JSGlobalObject* globalObject) const
{
    VM& vm = globalObject->vm();
    auto throwScope = DECLARE_THROW_SCOPE(vm);

    switch (m_type.kind) {
    case TypeKind::I32:
        return jsNumber(std::bit_cast<int32_t>(static_cast<uint32_t>(m_value.m_primitive)));
    case TypeKind::I64:
        RELEASE_AND_RETURN(throwScope, JSBigInt::makeHeapBigIntOrBigInt32(globalObject, static_cast<int64_t>(m_value.m_primitive)));
    case TypeKind::F32:
        return jsNumber(purifyNaN(static_cast<double>(std::bit_cast<float>(static_cast<uint32_t>(m_value.m_primitive)))));
    case TypeKind::F64:
        return jsNumber(purifyNaN(std::bit_cast<double>(m_value.m_primitive)));
    case TypeKind::V128:
        throwException(globalObject, throwScope, createJSWebAssemblyRuntimeError(globalObject, vm, "Cannot get value of v128 global"_s));
        return { };
    case TypeKind::Exn:
    case TypeKind::Externref:
    case TypeKind::Funcref:
    case TypeKind::Ref:
    case TypeKind::RefNull: {
        if (isExnref(m_type)) [[unlikely]] {
            throwException(globalObject, throwScope, createJSWebAssemblyRuntimeError(globalObject, vm, "Cannot get value of exnref global"_s));
            return { };
        }
        return m_value.m_externref.get();
    }
    default:
        return jsUndefined();
    }
}

void Global::set(JSGlobalObject* globalObject, JSValue argument)
{
    VM& vm = globalObject->vm();
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    ASSERT(m_mutability != Wasm::Immutable);
    switch (m_type.kind) {
    case TypeKind::I32: {
        int32_t value = argument.toInt32(globalObject);
        RETURN_IF_EXCEPTION(throwScope, void());
        m_value.m_primitive = static_cast<uint64_t>(static_cast<uint32_t>(value));
        break;
    }
    case TypeKind::I64: {
        int64_t value = argument.toBigInt64(globalObject);
        RETURN_IF_EXCEPTION(throwScope, void());
        m_value.m_primitive = static_cast<uint64_t>(value);
        break;
    }
    case TypeKind::F32: {
        float value = argument.toFloat(globalObject);
        RETURN_IF_EXCEPTION(throwScope, void());
        m_value.m_primitive = static_cast<uint64_t>(std::bit_cast<uint32_t>(value));
        break;
    }
    case TypeKind::F64: {
        double value = argument.toNumber(globalObject);
        RETURN_IF_EXCEPTION(throwScope, void());
        m_value.m_primitive = std::bit_cast<uint64_t>(value);
        break;
    }
    case TypeKind::V128: {
        throwTypeError(globalObject, throwScope, "Cannot set value of v128 global"_s);
        return;
    }
    case Wasm::TypeKind::Ref:
    case Wasm::TypeKind::RefNull:
    case Wasm::TypeKind::Externref:
    case Wasm::TypeKind::Funcref: {
        if (isExternref(m_type)) {
            RELEASE_ASSERT(m_owner);
            if (!m_type.isNullable() && argument.isNull()) {
                throwTypeError(globalObject, throwScope, "Non-null Externref cannot be null"_s);
                return;
            }
            m_value.m_externref.set(m_owner->vm(), m_owner, argument);
        } else if (isFuncref(m_type) || (isRefWithTypeIndex(m_type) && TypeInformation::get(m_type.index).is<FunctionSignature>())) {
            RELEASE_ASSERT(m_owner);
            WebAssemblyFunction* wasmFunction = nullptr;
            WebAssemblyWrapperFunction* wasmWrapperFunction = nullptr;
            if (!isWebAssemblyHostFunction(argument, wasmFunction, wasmWrapperFunction) && (!m_type.isNullable() || !argument.isNull())) {
                throwTypeError(globalObject, throwScope, "Argument value did not match the reference type"_s);
                return;
            }

            if (isRefWithTypeIndex(m_type) && !argument.isNull()) {
                Wasm::TypeIndex paramIndex = m_type.index;
                Wasm::TypeIndex argIndex = wasmFunction ? wasmFunction->typeIndex() : wasmWrapperFunction->typeIndex();
                if (paramIndex != argIndex) {
                    throwTypeError(globalObject, throwScope, "Argument value did not match the reference type"_s);
                    return;
                }
            }
            m_value.m_externref.set(m_owner->vm(), m_owner, argument);
        } else if (isExnref(m_type)) {
            throwTypeError(globalObject, throwScope, "Cannot set value of exnref global"_s);
            return;
        } else {
            JSValue internref = Wasm::internalizeExternref(argument);
            if (!Wasm::TypeInformation::castReference(internref, m_type.isNullable(), m_type.index)) {
                // FIXME: provide a better error message here
                // https://bugs.webkit.org/show_bug.cgi?id=247746
                throwTypeError(globalObject, throwScope, "Argument value did not match the reference type"_s);
                return;
            }
            m_value.m_externref.set(m_owner->vm(), m_owner, internref);
        }
        break;
    }
    default:
        RELEASE_ASSERT_NOT_REACHED();
    }
}

template<typename Visitor>
void Global::visitAggregateImpl(Visitor& visitor)
{
    if (isRefType(m_type)) {
        RELEASE_ASSERT(m_owner);
        visitor.append(m_value.m_externref);
    }
}

DEFINE_VISIT_AGGREGATE(Global);

} } // namespace JSC::Global

#endif // ENABLE(WEBASSEMBLY)
