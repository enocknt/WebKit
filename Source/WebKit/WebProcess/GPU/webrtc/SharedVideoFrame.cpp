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
#include "SharedVideoFrame.h"

#if ENABLE(GPU_PROCESS) && PLATFORM(COCOA) && ENABLE(VIDEO)

#include "Logging.h"
#include "RemoteVideoFrameObjectHeap.h"
#include "RemoteVideoFrameProxy.h"
#include <WebCore/CVUtilities.h>
#include <WebCore/IOSurface.h>
#include <WebCore/SharedVideoFrameInfo.h>
#include <WebCore/VideoFrameCV.h>
#include <WebCore/VideoFrameLibWebRTC.h>
#include <wtf/Scope.h>
#include <wtf/TZoneMallocInlines.h>

#if USE(LIBWEBRTC)

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN
#include <webrtc/webkit_sdk/WebKit/WebKitUtilities.h>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

#endif

#include <pal/cf/CoreMediaSoftLink.h>
#include <WebCore/CoreVideoSoftLink.h>

namespace WebKit {
using namespace WebCore;

WTF_MAKE_TZONE_ALLOCATED_IMPL(SharedVideoFrameWriter);

SharedVideoFrameWriter::SharedVideoFrameWriter()
    : m_semaphore(makeUniqueRef<IPC::Semaphore>())
{
}

bool SharedVideoFrameWriter::wait(const Function<void(IPC::Semaphore&)>& newSemaphoreCallback)
{
    if (!m_isSemaphoreInUse) {
        m_isSemaphoreInUse = true;
        newSemaphoreCallback(m_semaphore.get());
        return true;
    }
    return !m_isDisabled && m_semaphore->waitFor(defaultTimeout);
}

bool SharedVideoFrameWriter::allocateStorage(size_t size, NOESCAPE const Function<void(SharedMemory::Handle&&)>& newMemoryCallback)
{
    RefPtr storage = SharedMemory::allocate(size);
    m_storage = storage;
    if (!storage)
        return false;

    auto handle = storage->createHandle(SharedMemory::Protection::ReadOnly);
    if (!handle)
        return false;

    newMemoryCallback(WTFMove(*handle));
    return true;
}

bool SharedVideoFrameWriter::prepareWriting(const SharedVideoFrameInfo& info, NOESCAPE const Function<void(IPC::Semaphore&)>& newSemaphoreCallback, NOESCAPE const Function<void(SharedMemory::Handle&&)>& newMemoryCallback)
{
    if (!info.isReadWriteSupported()) {
        RELEASE_LOG_ERROR(WebRTC, "SharedVideoFrameWriter::prepareWriting not supported");
        return false;
    }

    if (!wait(newSemaphoreCallback)) {
        RELEASE_LOG_ERROR(WebRTC, "SharedVideoFrameReader::writeBuffer wait failed");
        return false;
    }
    m_shouldSignalInCaseOfError = true;

    size_t size = info.storageSize();
    if (!m_storage || m_storage->size() < size) {
        if (!allocateStorage(size, newMemoryCallback)) {
            RELEASE_LOG_ERROR(WebRTC, "SharedVideoFrameReader::writeBuffer allocation failed");
            return false;
        }
    }
    return true;
}

std::optional<SharedVideoFrame> SharedVideoFrameWriter::write(const VideoFrame& frame, NOESCAPE const Function<void(IPC::Semaphore&)>& newSemaphoreCallback, NOESCAPE const Function<void(SharedMemory::Handle&&)>& newMemoryCallback)
{
    auto buffer = writeBuffer(frame, newSemaphoreCallback, newMemoryCallback);
    if (!buffer)
        return { };
    return SharedVideoFrame { frame.presentationTime(), frame.isMirrored(), frame.rotation(), WTFMove(*buffer) };
}

std::optional<SharedVideoFrame::Buffer> SharedVideoFrameWriter::writeBuffer(const VideoFrame& frame, NOESCAPE const Function<void(IPC::Semaphore&)>& newSemaphoreCallback, NOESCAPE const Function<void(SharedMemory::Handle&&)>& newMemoryCallback)
{
    if (auto* frameProxy = dynamicDowncast<RemoteVideoFrameProxy>(frame))
        return frameProxy->newReadReference();

#if USE(LIBWEBRTC)
    if (auto* webrtcFrame = dynamicDowncast<VideoFrameLibWebRTC>(frame))
        return writeBuffer(*webrtcFrame->buffer(), newSemaphoreCallback, newMemoryCallback);
#endif

    return writeBuffer(frame.pixelBuffer(), newSemaphoreCallback, newMemoryCallback);
}

std::optional<SharedVideoFrame::Buffer> SharedVideoFrameWriter::writeBuffer(CVPixelBufferRef pixelBuffer, NOESCAPE const Function<void(IPC::Semaphore&)>& newSemaphoreCallback, NOESCAPE const Function<void(SharedMemory::Handle&&)>& newMemoryCallback, bool canUseIOSurface)
{
    if (!pixelBuffer)
        return { };

    if (canUseIOSurface) {
        if (RetainPtr surface = CVPixelBufferGetIOSurface(pixelBuffer))
            return MachSendRight::adopt(IOSurfaceCreateMachPort(surface.get()));
    }

    auto scope = makeScopeExit([this] { signalInCaseOfError(); });

    RetainPtr pixelBufferToWrite = pixelBuffer;
    if (CVPixelBufferGetPixelFormatType(pixelBuffer) == kCVPixelFormatType_Lossless_420YpCbCr8BiPlanarVideoRange) {
        if (!m_compressedPixelBufferConformer)
            m_compressedPixelBufferConformer = makeUnique<WebCore::PixelBufferConformerCV>(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
        pixelBufferToWrite = m_compressedPixelBufferConformer->convert(pixelBuffer);
        if (!pixelBufferToWrite) {
            RELEASE_LOG_ERROR(WebRTC, "SharedVideoFrameWriter::writeBuffer cannot convert pixel buffer");
            return { };
        }
    }

    auto info = SharedVideoFrameInfo::fromCVPixelBuffer(pixelBufferToWrite.get());
    if (!prepareWriting(info, newSemaphoreCallback, newMemoryCallback))
        return { };

    if (!info.writePixelBuffer(pixelBufferToWrite.get(), m_storage->mutableSpan()))
        return { };

    scope.release();
    return nullptr;
}

#if USE(LIBWEBRTC)
std::optional<SharedVideoFrame::Buffer> SharedVideoFrameWriter::writeBuffer(const webrtc::VideoFrame& frame, NOESCAPE const Function<void(IPC::Semaphore&)>& newSemaphoreCallback, NOESCAPE const Function<void(SharedMemory::Handle&&)>& newMemoryCallback)
{
    if (auto* provider = webrtc::videoFrameBufferProvider(frame))
        return writeBuffer(*static_cast<VideoFrame*>(provider), newSemaphoreCallback, newMemoryCallback);

    if (auto pixelBuffer = adoptCF(webrtc::copyPixelBufferForFrame(frame)))
        return writeBuffer(pixelBuffer.get(), newSemaphoreCallback, newMemoryCallback);

    return writeBuffer(*frame.video_frame_buffer(), newSemaphoreCallback, newMemoryCallback);
}

std::optional<SharedVideoFrame::Buffer> SharedVideoFrameWriter::writeBuffer(webrtc::VideoFrameBuffer& frameBuffer, NOESCAPE const Function<void(IPC::Semaphore&)>& newSemaphoreCallback, NOESCAPE const Function<void(SharedMemory::Handle&&)>& newMemoryCallback)
{
    auto scope = makeScopeExit([this] { signalInCaseOfError(); });

    auto info = SharedVideoFrameInfo::fromVideoFrameBuffer(frameBuffer);
    if (!prepareWriting(info, newSemaphoreCallback, newMemoryCallback))
        return { };

    if (!info.writeVideoFrameBuffer(frameBuffer, m_storage->mutableSpan()))
        return { };

    scope.release();
    return nullptr;
}
#endif

void SharedVideoFrameWriter::signalInCaseOfError()
{
    if (!m_shouldSignalInCaseOfError)
        return;
    m_shouldSignalInCaseOfError = false;
    m_semaphore->signal();
}

void SharedVideoFrameWriter::disable()
{
    m_isDisabled = true;
    m_semaphore->signal();
}

WTF_MAKE_TZONE_ALLOCATED_IMPL(SharedVideoFrameReader);

SharedVideoFrameReader::SharedVideoFrameReader(RefPtr<RemoteVideoFrameObjectHeap>&& objectHeap, const ProcessIdentity& resourceOwner, UseIOSurfaceBufferPool useIOSurfaceBufferPool)
    : m_objectHeap(WTFMove(objectHeap))
    , m_resourceOwner(resourceOwner)
    , m_useIOSurfaceBufferPool(useIOSurfaceBufferPool)
{
}

SharedVideoFrameReader::SharedVideoFrameReader() = default;

SharedVideoFrameReader::~SharedVideoFrameReader() = default;

RetainPtr<CVPixelBufferRef> SharedVideoFrameReader::readBufferFromSharedMemory()
{
    if (!m_storage) {
        RELEASE_LOG_ERROR(WebRTC, "SharedVideoFrameReader::readBufferFromSharedMemory no storage");
        return { };
    }

    auto scope = makeScopeExit([&] {
        if (m_semaphore)
            m_semaphore->signal();
    });

    auto data = m_storage->span();
    auto info = SharedVideoFrameInfo::decode(data);
    if (!info) {
        RELEASE_LOG_ERROR(WebRTC, "SharedVideoFrameReader::readBufferFromSharedMemory decoding failed");
        return { };
    }

    if (!info->isReadWriteSupported()) {
        RELEASE_LOG_ERROR(WebRTC, "SharedVideoFrameReader::readBufferFromSharedMemory not supported");
        return { };
    }

    if (m_storage->size() < info->storageSize()) {
        RELEASE_LOG_ERROR(WebRTC, "SharedVideoFrameReader::readBufferFromSharedMemory storage size mismatch");
        return { };
    }

    auto result = info->createPixelBufferFromMemory(data.subspan(SharedVideoFrameInfoEncodingLength), pixelBufferPool(*info));
    if (result && m_resourceOwner && m_useIOSurfaceBufferPool == UseIOSurfaceBufferPool::Yes)
        setOwnershipIdentityForCVPixelBuffer(result.get(), m_resourceOwner);
    return result;
}

RetainPtr<CVPixelBufferRef> SharedVideoFrameReader::readBuffer(SharedVideoFrame::Buffer&& buffer)
{
    return switchOn(WTFMove(buffer),
    [this](RemoteVideoFrameReadReference&& reference) -> RetainPtr<CVPixelBufferRef> {
        ASSERT(m_objectHeap);
        if (!m_objectHeap) {
            RELEASE_LOG_ERROR(WebRTC, "SharedVideoFrameReader::readBuffer no object heap");
            return nullptr;
        }

        auto sample = m_objectHeap->get(WTFMove(reference));
        if (!sample) {
            RELEASE_LOG_ERROR(WebRTC, "SharedVideoFrameReader::readBuffer no sample");
            return nullptr;
        }

        ASSERT(sample->pixelBuffer());
        return sample->pixelBuffer();
    } , [](MachSendRight&& sendRight) -> RetainPtr<CVPixelBufferRef> {
        auto surface = WebCore::IOSurface::createFromSendRight(WTFMove(sendRight));
        if (!surface) {
            RELEASE_LOG_ERROR(WebRTC, "SharedVideoFrameReader::readBuffer no surface");
            return nullptr;
        }
        return WebCore::createCVPixelBuffer(surface->surface()).value_or(nullptr);
    }, [this](std::nullptr_t representation) -> RetainPtr<CVPixelBufferRef> {
        return readBufferFromSharedMemory();
    }, [this](IntSize size) -> RetainPtr<CVPixelBufferRef> {
        if (m_blackFrameSize != size) {
            m_blackFrameSize = size;
            m_blackFrame = WebCore::createBlackPixelBuffer(m_blackFrameSize.width(), m_blackFrameSize.height(), m_useIOSurfaceBufferPool == UseIOSurfaceBufferPool::Yes);
            if (m_resourceOwner && m_useIOSurfaceBufferPool == UseIOSurfaceBufferPool::Yes)
                setOwnershipIdentityForCVPixelBuffer(m_blackFrame.get(), m_resourceOwner);
        }
        return m_blackFrame.get();
    });
}

RefPtr<VideoFrame> SharedVideoFrameReader::read(SharedVideoFrame&& sharedVideoFrame)
{
    auto pixelBuffer = readBuffer(WTFMove(sharedVideoFrame.buffer));
    if (!pixelBuffer)
        return nullptr;

    return VideoFrameCV::create(sharedVideoFrame.time, sharedVideoFrame.mirrored, sharedVideoFrame.rotation, WTFMove(pixelBuffer));
}

CVPixelBufferPoolRef SharedVideoFrameReader::pixelBufferPool(const SharedVideoFrameInfo& info)
{
    if (m_useIOSurfaceBufferPool == UseIOSurfaceBufferPool::No)
        return nullptr;

    if (!m_bufferPool || m_bufferPoolType != info.bufferType() || m_bufferPoolWidth != info.width() || m_bufferPoolHeight != info.height()) {
        m_bufferPoolType = info.bufferType();
        m_bufferPoolWidth = info.width();
        m_bufferPoolHeight = info.height();
        m_bufferPool = info.createCompatibleBufferPool();
    }

    return m_bufferPool.get();
}

bool SharedVideoFrameReader::setSharedMemory(SharedMemory::Handle&& handle)
{
    m_storage = SharedMemory::map(WTFMove(handle), SharedMemory::Protection::ReadOnly);
    return !!m_storage;
}

}

#endif
