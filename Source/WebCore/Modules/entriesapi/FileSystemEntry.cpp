/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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
#include "FileSystemEntry.h"

#include "DOMException.h"
#include "DOMFileSystem.h"
#include "Document.h"
#include "DocumentInlines.h"
#include "ErrorCallback.h"
#include "FileSystemDirectoryEntry.h"
#include "FileSystemEntryCallback.h"
#include "ScriptExecutionContext.h"
#include "WindowEventLoop.h"
#include <wtf/FileSystem.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(FileSystemEntry);

FileSystemEntry::FileSystemEntry(ScriptExecutionContext& context, DOMFileSystem& filesystem, const String& virtualPath)
    : ActiveDOMObject(&context)
    , m_filesystem(filesystem)
    , m_name(FileSystem::pathFileName(virtualPath))
    , m_virtualPath(virtualPath)
{
}

FileSystemEntry::~FileSystemEntry() = default;

DOMFileSystem& FileSystemEntry::filesystem() const
{
    return m_filesystem.get();
}

Document* FileSystemEntry::document() const
{
    return downcast<Document>(scriptExecutionContext());
}

void FileSystemEntry::getParent(ScriptExecutionContext& context, RefPtr<FileSystemEntryCallback>&& successCallback, RefPtr<ErrorCallback>&& errorCallback)
{
    if (!successCallback && !errorCallback)
        return;

    filesystem().getParent(context, *this, [pendingActivity = makePendingActivity(*this), successCallback = WTFMove(successCallback), errorCallback = WTFMove(errorCallback)]<typename Result> (Result&& result) mutable {
        RefPtr document = pendingActivity->object().document();
        if (!document)
            return;

        document->checkedEventLoop()->queueTask(TaskSource::Networking, [successCallback = WTFMove(successCallback), errorCallback = WTFMove(errorCallback), result = std::forward<Result>(result), pendingActivity = WTFMove(pendingActivity)] () mutable {
            if (result.hasException()) {
                if (errorCallback)
                    errorCallback->invoke(DOMException::create(result.releaseException()));
                return;
            }
            if (successCallback)
                successCallback->invoke(result.releaseReturnValue());
        });
    });
}


} // namespace WebCore
