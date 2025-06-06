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
#include "JSAbortSignal.h"

#include "WebCoreOpaqueRootInlines.h"

namespace WebCore {

bool JSAbortSignalOwner::isReachableFromOpaqueRoots(JSC::Handle<JSC::Unknown> handle, void*, JSC::AbstractSlotVisitor& visitor, ASCIILiteral* reason)
{
    auto& abortSignal = JSC::jsCast<JSAbortSignal*>(handle.slot()->asCell())->wrapped();
    if (abortSignal.aborted())
        return false;

    if (abortSignal.isFollowingSignal()) {
        if (reason) [[unlikely]]
            *reason = "Is Following Signal"_s;
        return true;
    }

    if (abortSignal.hasAbortEventListener()) {
        if (abortSignal.hasActiveTimeoutTimer()) {
            if (reason) [[unlikely]]
                *reason = "Has Timeout And Abort Event Listener"_s;
            return true;
        }
        if (abortSignal.isDependent()) {
            if (!abortSignal.sourceSignals().isEmptyIgnoringNullReferences()) {
                if (reason) [[unlikely]]
                    *reason = "Has Source Signals And Abort Event Listener"_s;
                return true;
            }
        } else {
            if (reason) [[unlikely]]
                *reason = "Has Abort Event Listener"_s;
            return true;
        }
    }

    return containsWebCoreOpaqueRoot(visitor, abortSignal);
}

template<typename Visitor>
void JSAbortSignal::visitAdditionalChildren(Visitor& visitor)
{
    wrapped().reason().visit(visitor);
}

DEFINE_VISIT_ADDITIONAL_CHILDREN(JSAbortSignal);

} // namespace WebCore
