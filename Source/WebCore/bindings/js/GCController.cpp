/*
 * Copyright (C) 2007-2021 Apple Inc. All rights reserved.
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
#include "GCController.h"

#include "CommonVM.h"
#include "JSHTMLDocument.h"
#include "Location.h"
#include "WorkerGlobalScope.h"
#include <JavaScriptCore/Heap.h>
#include <JavaScriptCore/HeapSnapshotBuilder.h>
#include <JavaScriptCore/JSLock.h>
#include <JavaScriptCore/VM.h>
#include <pal/Logging.h>
#include <wtf/FileHandle.h>
#include <wtf/FileSystem.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/StdLibExtras.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {
using namespace JSC;

static void collect()
{
    JSLockHolder lock(commonVM());
    commonVM().heap.collectNow(Async, CollectionScope::Full);
}

WTF_MAKE_TZONE_ALLOCATED_IMPL(GCController);

GCController& GCController::singleton()
{
    static NeverDestroyed<GCController> controller;
    return controller;
}

GCController::GCController()
    : m_GCTimer(*this, &GCController::gcTimerFired)
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        PAL::registerNotifyCallback("com.apple.WebKit.dumpGCHeap"_s, [] {
            GCController::singleton().dumpHeap();
        });
    });
}

void GCController::garbageCollectSoon()
{
    JSLockHolder lock(commonVM());
    commonVM().heap.reportAbandonedObjectGraph();
}

void GCController::garbageCollectOnNextRunLoop()
{
    if (!m_GCTimer.isActive())
        m_GCTimer.startOneShot(0_s);
}

void GCController::gcTimerFired()
{
    collect();
}

void GCController::garbageCollectNow()
{
    JSLockHolder lock(commonVM());
    if (!commonVM().heap.currentThreadIsDoingGCWork()) {
        commonVM().heap.collectNow(Sync, CollectionScope::Full);
        WTF::releaseFastMallocFreeMemory();
    }
}

void GCController::garbageCollectNowIfNotDoneRecently()
{
    JSLockHolder lock(commonVM());
    if (!commonVM().heap.currentThreadIsDoingGCWork())
        commonVM().heap.collectNowFullIfNotDoneRecently(Async);
}

void GCController::garbageCollectOnAlternateThreadForDebugging(bool waitUntilDone)
{
    auto thread = Thread::create("WebCore: GCController"_s, &collect, ThreadType::GarbageCollection);

    if (waitUntilDone) {
        thread->waitForCompletion();
        return;
    }

    thread->detach();
}

void GCController::setJavaScriptGarbageCollectorTimerEnabled(bool enable)
{
    commonVM().heap.setGarbageCollectionTimerEnabled(enable);
}

void GCController::deleteAllCode(DeleteAllCodeEffort effort)
{
    JSLockHolder lock(commonVM());
    commonVM().deleteAllCode(effort);
}

void GCController::deleteAllLinkedCode(DeleteAllCodeEffort effort)
{
    JSLockHolder lock(commonVM());
    commonVM().deleteAllLinkedCode(effort);
}

void GCController::dumpHeapForVM(VM& vm)
{
    auto [tempFilePath, fileHandle] = FileSystem::openTemporaryFile("GCHeap"_s);
    if (!fileHandle) {
        WTFLogAlways("Dumping GC heap failed to open temporary file");
        return;
    }

    JSLockHolder lock(vm);
    sanitizeStackForVM(vm);

    String jsonData;
    {
        DeferGCForAWhile deferGC(vm); // Prevent concurrent GC from interfering with the full GC that the snapshot does.

        HeapSnapshotBuilder snapshotBuilder(vm.ensureHeapProfiler(), HeapSnapshotBuilder::SnapshotType::GCDebuggingSnapshot);
        snapshotBuilder.buildSnapshot();

        jsonData = snapshotBuilder.json();
    }

    CString utf8String = jsonData.utf8();

    fileHandle.write(byteCast<uint8_t>(utf8String.span()));
    WTFLogAlways("Dumped GC heap to %s%s", tempFilePath.utf8().data(), isMainThread() ? "" : " for Worker");
}

void GCController::dumpHeap()
{
    dumpHeapForVM(commonVM());
    WorkerGlobalScope::dumpGCHeapForWorkers();
}

} // namespace WebCore
