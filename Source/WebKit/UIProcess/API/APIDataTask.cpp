/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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
#include "APIDataTask.h"

#include "APIDataTaskClient.h"
#include "NetworkProcessProxy.h"
#include "WebPageProxy.h"
#include <WebCore/ResourceError.h>

namespace API {

DataTask::~DataTask() = default;

Ref<DataTaskClient> DataTask::protectedClient() const
{
    return m_client;
}

void DataTask::setClient(Ref<DataTaskClient>&& client)
{
    m_client = WTFMove(client);
}

void DataTask::cancel()
{
    if (m_sessionID && m_identifier) {
        if (RefPtr networkProcess = m_networkProcess.get())
            networkProcess->cancelDataTask(*m_identifier, *m_sessionID);
    }
    m_activity = nullptr;
}

void DataTask::networkProcessCrashed()
{
    m_activity = nullptr;
    protectedClient()->didCompleteWithError(*this, WebCore::internalError(m_originalURL));
}

DataTask::DataTask(std::optional<WebKit::DataTaskIdentifier> identifier, WeakPtr<WebKit::WebPageProxy>&& page, WTF::URL&& originalURL, bool shouldRunAtForegroundPriority)
    : m_identifier(identifier)
    , m_page(WTFMove(page))
    , m_originalURL(WTFMove(originalURL))
    , m_networkProcess(m_page ? WeakPtr { protectedPage()->protectedWebsiteDataStore()->networkProcess() } : nullptr)
    , m_sessionID(m_page ? std::optional<PAL::SessionID> { protectedPage()->sessionID() } : std::nullopt)
    , m_client(DataTaskClient::create())
{
    if (RefPtr networkProcess = m_networkProcess.get())
        m_activity = shouldRunAtForegroundPriority ? networkProcess->protectedThrottler()->foregroundActivity("WKDataTask"_s) : networkProcess->protectedThrottler()->backgroundActivity("WKDataTask"_s);
}

void DataTask::didCompleteWithError(WebCore::ResourceError&& error)
{
    m_activity = nullptr;
    protectedClient()->didCompleteWithError(*this, WTFMove(error));
}

} // namespace API
