/*
 * Copyright (C) 2021-2022 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "SharedWorker.h"

#include "ClientOrigin.h"
#include "ContentSecurityPolicy.h"
#include "Document.h"
#include "DocumentInlines.h"
#include "EventNames.h"
#include "EventTargetInterfaces.h"
#include "Logging.h"
#include "MessageChannel.h"
#include "MessagePort.h"
#include "OriginAccessPatterns.h"
#include "ResourceError.h"
#include "SecurityOrigin.h"
#include "SharedWorkerObjectConnection.h"
#include "SharedWorkerProvider.h"
#include "TrustedType.h"
#include "WorkerOptions.h"
#include <JavaScriptCore/IdentifiersFactory.h>
#include <wtf/CheckedArithmetic.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>

#if ENABLE(CONTENT_EXTENSIONS)
#include "ResourceMonitor.h"
#endif

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(SharedWorker);

#define SHARED_WORKER_RELEASE_LOG(fmt, ...) RELEASE_LOG(SharedWorker, "%p - [identifier=%" PUBLIC_LOG_STRING "] SharedWorker::" fmt, this, identifier().toString().utf8().data(), ##__VA_ARGS__)
#define SHARED_WORKER_RELEASE_LOG_ERROR(fmt, ...) RELEASE_LOG_ERROR(SharedWorker, "%p - [identifier=%" PUBLIC_LOG_STRING "] SharedWorker::" fmt, this, identifier().toString().utf8().data(), ##__VA_ARGS__)

static HashMap<SharedWorkerObjectIdentifier, WeakRef<SharedWorker, WeakPtrImplWithEventTargetData>>& allSharedWorkers()
{
    ASSERT(isMainThread());
    static NeverDestroyed<HashMap<SharedWorkerObjectIdentifier, WeakRef<SharedWorker, WeakPtrImplWithEventTargetData>>> allSharedWorkers;
    return allSharedWorkers;
}

SharedWorker* SharedWorker::fromIdentifier(SharedWorkerObjectIdentifier identifier)
{
    return allSharedWorkers().get(identifier);
}

static inline SharedWorkerObjectConnection* mainThreadConnection()
{
    return SharedWorkerProvider::singleton().sharedWorkerConnection();
}

ExceptionOr<Ref<SharedWorker>> SharedWorker::create(Document& document, Variant<RefPtr<TrustedScriptURL>, String>&& scriptURLString, std::optional<Variant<String, WorkerOptions>>&& maybeOptions)
{
    auto compliantScriptURLString = trustedTypeCompliantString(document, WTFMove(scriptURLString), "SharedWorker constructor"_s);
    if (compliantScriptURLString.hasException())
        return compliantScriptURLString.releaseException();

    if (!mainThreadConnection())
        return Exception { ExceptionCode::NotSupportedError, "Shared workers are not supported"_s };

    if (!document.hasBrowsingContext())
        return Exception { ExceptionCode::InvalidStateError, "No browsing context"_s };

    auto url = document.completeURL(compliantScriptURLString.releaseReturnValue());
    if (!url.isValid())
        return Exception { ExceptionCode::SyntaxError, "Invalid script URL"_s };

    CheckedPtr contentSecurityPolicy = document.contentSecurityPolicy();
    if (contentSecurityPolicy)
        contentSecurityPolicy->upgradeInsecureRequestIfNeeded(url, ContentSecurityPolicy::InsecureRequestType::Load);

    WorkerOptions options;
    if (maybeOptions) {
        WTF::switchOn(*maybeOptions, [&] (const String& name) {
            options.name = name;
        }, [&] (const WorkerOptions& optionsFromVariant) {
            options = optionsFromVariant;
        });
    }

    auto channel = MessageChannel::create(document);
    auto transferredPort = channel->port2().disentangle();

    ClientOrigin clientOrigin { document.topOrigin().data(), document.securityOrigin().data() };
    SharedWorkerKey key { clientOrigin, url, options.name };

    auto sharedWorker = adoptRef(*new SharedWorker(document, key, channel->port1()));
    sharedWorker->suspendIfNeeded();

    if (auto exception = validateURL(document, url)) {
        if (!document.settings().workerAsynchronousURLErrorHandlingEnabled())
            return Exception { ExceptionCode::SecurityError, "URL of the shared worker is cross-origin"_s };
        sharedWorker->m_isActive = false;
        sharedWorker->queueTaskToDispatchEvent(sharedWorker.get(), TaskSource::DOMManipulation, Event::create(eventNames().errorEvent, Event::CanBubble::No, Event::IsCancelable::Yes));
        return sharedWorker;
    }

    mainThreadConnection()->requestSharedWorker(key, sharedWorker->identifier(), WTFMove(transferredPort), options);
    return sharedWorker;
}

SharedWorker::SharedWorker(Document& document, const SharedWorkerKey& key, Ref<MessagePort>&& port)
    : ActiveDOMObject(&document)
    , m_key(key)
    , m_port(WTFMove(port))
    , m_identifierForInspector(makeString("SharedWorker:"_s, Inspector::IdentifiersFactory::createIdentifier()))
    , m_blobURLExtension({ m_key.url.protocolIsBlob() ? m_key.url : URL(), document.topOrigin().data() }) // Keep blob URL alive until the worker has finished loading.
#if ENABLE(CONTENT_EXTENSIONS)
    , m_resourceMonitor(document.resourceMonitorIfExists())
#endif
{
    SHARED_WORKER_RELEASE_LOG("SharedWorker:");
    allSharedWorkers().add(identifier(), *this);
}

SharedWorker::~SharedWorker()
{
    ASSERT(allSharedWorkers().get(identifier()) == this);
    SHARED_WORKER_RELEASE_LOG("~SharedWorker:");
    allSharedWorkers().remove(identifier());
    ASSERT(!m_isActive);
}

ScriptExecutionContext* SharedWorker::scriptExecutionContext() const
{
    return ActiveDOMObject::scriptExecutionContext();
}

enum EventTargetInterfaceType SharedWorker::eventTargetInterface() const
{
    return EventTargetInterfaceType::SharedWorker;
}

void SharedWorker::didFinishLoading(const ResourceError& error)
{
    SHARED_WORKER_RELEASE_LOG("finishLoading: success=%d", error.isNull());
    if (!error.isNull()) {
        queueTaskToDispatchEvent(*this, TaskSource::DOMManipulation, Event::create(eventNames().errorEvent, Event::CanBubble::No, Event::IsCancelable::Yes));
        m_isActive = false;
    }
    m_blobURLExtension.clear();
}

bool SharedWorker::virtualHasPendingActivity() const
{
    return m_isActive;
}

void SharedWorker::stop()
{
    SHARED_WORKER_RELEASE_LOG("stop:");
    m_isActive = false;
    mainThreadConnection()->sharedWorkerObjectIsGoingAway(m_key, identifier());
}

void SharedWorker::suspend(ReasonForSuspension reason)
{
    if (reason == ReasonForSuspension::BackForwardCache) {
        mainThreadConnection()->suspendForBackForwardCache(m_key, identifier());
        m_isSuspendedForBackForwardCache = true;
    }
}

void SharedWorker::resume()
{
    if (m_isSuspendedForBackForwardCache) {
        mainThreadConnection()->resumeForBackForwardCache(m_key, identifier());
        m_isSuspendedForBackForwardCache = false;
    }
}

void SharedWorker::reportNetworkUsage(size_t bytesTransferredOverNetwork)
{
#if ENABLE(CONTENT_EXTENSIONS)
    CheckedSize delta = bytesTransferredOverNetwork - m_bytesTransferredOverNetwork;
    ASSERT(!delta.hasOverflowed());

    if (delta) {
        if (RefPtr resourceMonitor = m_resourceMonitor) {
            RELEASE_LOG(ResourceMonitoring, "[identifier=%" PUBLIC_LOG_STRING "] SharedWorker::reportNetworkUsage to ResourceMonitor: %zu bytes", identifier().toString().utf8().data(), delta.value());
            resourceMonitor->addNetworkUsage(delta);
        }
    }
#endif

    m_bytesTransferredOverNetwork = bytesTransferredOverNetwork;
}

#undef SHARED_WORKER_RELEASE_LOG
#undef SHARED_WORKER_RELEASE_LOG_ERROR

} // namespace WebCore
