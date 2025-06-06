/*
 * Copyright (C) 2020-2024 Apple Inc. All rights reserved.
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
#include "RTCRtpSFrameTransform.h"

#if ENABLE(WEB_RTC)

#include "ContextDestructionObserverInlines.h"
#include "CryptoKeyRaw.h"
#include "EventTargetInlines.h"
#include "JSDOMConvertBufferSource.h"
#include "JSDOMPromiseDeferred.h"
#include "JSRTCEncodedAudioFrame.h"
#include "JSRTCEncodedVideoFrame.h"
#include "JSWritableStreamSink.h"
#include "Logging.h"
#include "RTCEncodedAudioFrame.h"
#include "RTCEncodedVideoFrame.h"
#include "RTCRtpSFrameTransformErrorEvent.h"
#include "RTCRtpSFrameTransformer.h"
#include "RTCRtpTransformBackend.h"
#include "RTCRtpTransformableFrame.h"
#include "ReadableStream.h"
#include "ReadableStreamSource.h"
#include "SharedBuffer.h"
#include "WritableStream.h"
#include <wtf/EnumTraits.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(RTCRtpSFrameTransform);

Ref<RTCRtpSFrameTransform> RTCRtpSFrameTransform::create(ScriptExecutionContext& context, Options options)
{
    auto result = adoptRef(*new RTCRtpSFrameTransform(context, options));
    result->suspendIfNeeded();
    return result;
}

RTCRtpSFrameTransform::RTCRtpSFrameTransform(ScriptExecutionContext& context, Options options)
    : ActiveDOMObject(&context)
    , m_transformer(RTCRtpSFrameTransformer::create(options.compatibilityMode))
{
    m_transformer->setIsEncrypting(options.role == Role::Encrypt);
    m_transformer->setAuthenticationSize(options.authenticationSize);
}

RTCRtpSFrameTransform::~RTCRtpSFrameTransform() = default;

void RTCRtpSFrameTransform::setEncryptionKey(CryptoKey& key, std::optional<uint64_t> keyId, DOMPromiseDeferred<void>&& promise)
{
    auto algorithm = key.algorithm();
    if (!std::holds_alternative<CryptoKeyAlgorithm>(algorithm)) {
        promise.reject(Exception { ExceptionCode::TypeError, "Invalid key"_s });
        return;
    }

    if (std::get<CryptoKeyAlgorithm>(algorithm).name != "HKDF"_s) {
        promise.reject(Exception { ExceptionCode::TypeError, "Only HKDF is supported"_s });
        return;
    }

    auto& rawKey = downcast<CryptoKeyRaw>(key);
    promise.settle(m_transformer->setEncryptionKey(rawKey.key(), keyId));
}

void RTCRtpSFrameTransform::setCounterForTesting(uint64_t counter)
{
    m_transformer->setCounter(counter);
}

uint64_t RTCRtpSFrameTransform::counterForTesting() const
{
    return m_transformer->counter();
}

uint64_t RTCRtpSFrameTransform::keyIdForTesting() const
{
    return m_transformer->keyId();
}

bool RTCRtpSFrameTransform::isAttached() const
{
    return m_isAttached || (m_readable && m_readable->isLocked()) || (m_writable && m_writable->locked());
}

static RTCRtpSFrameTransformErrorEvent::Type errorTypeFromInformation(const RTCRtpSFrameTransformer::ErrorInformation& errorInformation)
{
    switch (errorInformation.error) {
    case RTCRtpSFrameTransformer::Error::KeyID:
        return RTCRtpSFrameTransformErrorEvent::Type::KeyID;
    case RTCRtpSFrameTransformer::Error::Authentication:
        return RTCRtpSFrameTransformErrorEvent::Type::Authentication;
    case RTCRtpSFrameTransformer::Error::Syntax:
        return RTCRtpSFrameTransformErrorEvent::Type::Syntax;
    default:
        RELEASE_ASSERT_NOT_REACHED();
    }
}

static std::optional<Vector<uint8_t>> processFrame(std::span<const uint8_t> data, RTCRtpSFrameTransformer& transformer, ScriptExecutionContextIdentifier identifier, const ThreadSafeWeakPtr<RTCRtpSFrameTransform>& weakTransform)
{
    auto result = transformer.transform(data);
    if (!result.has_value()) {
        auto errorInformation = WTFMove(result.error());
        errorInformation.message = { };
        RELEASE_LOG_ERROR(WebRTC, "RTCRtpSFrameTransform failed transforming a frame with error %hhu", enumToUnderlyingType(errorInformation.error));
        // Call the error event handler.
        ScriptExecutionContext::postTaskTo(identifier, [errorInformation, weakTransform](auto&&) {
            RefPtr transform = weakTransform.get();
            if (!transform || transform->isContextStopped())
                return;
            if (errorInformation.error == RTCRtpSFrameTransformer::Error::KeyID && transform->hasKey(errorInformation.keyId))
                return;
            transform->dispatchEvent(RTCRtpSFrameTransformErrorEvent::create(Event::CanBubble::No, Event::IsCancelable::No, errorTypeFromInformation(errorInformation)));
        });
        return { };
    }
    return WTFMove(result.value());
}

bool RTCRtpSFrameTransform::hasKey(uint64_t keyID) const
{
    return m_transformer->hasKey(keyID);
}

void RTCRtpSFrameTransform::initializeTransformer(RTCRtpTransformBackend& backend, Side side)
{
    ASSERT(!isAttached());

    RefPtr context = scriptExecutionContext();
    if (!context)
        return;

    m_isAttached = true;
    if (m_readable)
        m_readable->lock();
    if (m_writable)
        m_writable->lock();

    m_transformer->setIsEncrypting(side == Side::Sender);
    m_transformer->setMediaType(backend.mediaType());

    backend.setTransformableFrameCallback([transformer = m_transformer, identifier = context->identifier(), backend = Ref { backend }, weakThis = ThreadSafeWeakPtr { *this }](auto&& frame) {
        auto chunk = frame->data();
        if (!chunk.data() || !chunk.size())
            return;
        auto result = processFrame(chunk, transformer.get(), identifier, weakThis);

        if (!result)
            return;

        frame->setData(result.value().span());

        backend->processTransformedFrame(frame.get());
    });
}

void RTCRtpSFrameTransform::initializeBackendForReceiver(RTCRtpTransformBackend& backend)
{
    initializeTransformer(backend, Side::Receiver);
}

void RTCRtpSFrameTransform::initializeBackendForSender(RTCRtpTransformBackend& backend)
{
    initializeTransformer(backend, Side::Sender);
}

void RTCRtpSFrameTransform::willClearBackend(RTCRtpTransformBackend& backend)
{
    backend.clearTransformableFrameCallback();
}

static void transformFrame(std::span<const uint8_t> data, JSDOMGlobalObject& globalObject, RTCRtpSFrameTransformer& transformer, SimpleReadableStreamSource& source, ScriptExecutionContextIdentifier identifier, const ThreadSafeWeakPtr<RTCRtpSFrameTransform>& weakTransform)
{
    auto result = processFrame(data, transformer, identifier, weakTransform);
    auto buffer = result ? SharedBuffer::create(WTFMove(*result)) : SharedBuffer::create();
    source.enqueue(toJS(&globalObject, &globalObject, buffer->tryCreateArrayBuffer().get()));
}

template<typename Frame>
void transformFrame(Frame& frame, JSDOMGlobalObject& globalObject, RTCRtpSFrameTransformer& transformer, SimpleReadableStreamSource& source, ScriptExecutionContextIdentifier identifier, const ThreadSafeWeakPtr<RTCRtpSFrameTransform>& weakTransform)
{
    Ref vm = globalObject.vm();
    auto rtcFrame = frame.rtcFrame(vm, RTCEncodedFrame::ShouldNeuter::No);
    auto chunk = rtcFrame->data();
    auto result = processFrame(chunk, transformer, identifier, weakTransform);
    std::span<const uint8_t> transformedChunk;
    if (result)
        transformedChunk = result->span();
    rtcFrame->setData(transformedChunk);
    source.enqueue(toJS(&globalObject, &globalObject, frame));
}

ExceptionOr<void> RTCRtpSFrameTransform::createStreams()
{
    auto* globalObject = scriptExecutionContext() ? JSC::jsCast<JSDOMGlobalObject*>(scriptExecutionContext()->globalObject()) : nullptr;
    if (!globalObject)
        return Exception { ExceptionCode::InvalidStateError };

    m_readableStreamSource = SimpleReadableStreamSource::create();
    auto readable = ReadableStream::create(*globalObject, *m_readableStreamSource);
    if (readable.hasException())
        return readable.releaseException();

    auto writable = WritableStream::create(*JSC::jsCast<JSDOMGlobalObject*>(globalObject), SimpleWritableStreamSink::create([transformer = m_transformer, readableStreamSource = m_readableStreamSource, weakThis = ThreadSafeWeakPtr { *this }](auto& context, auto value) -> ExceptionOr<void> {
        if (!context.globalObject())
            return Exception { ExceptionCode::InvalidStateError };
        auto& globalObject = *JSC::jsCast<JSDOMGlobalObject*>(context.globalObject());
        auto scope = DECLARE_THROW_SCOPE(globalObject.vm());

        auto frameConversionResult = convert<IDLUnion<IDLArrayBuffer, IDLArrayBufferView, IDLInterface<RTCEncodedAudioFrame>, IDLInterface<RTCEncodedVideoFrame>>>(globalObject, value);
        if (frameConversionResult.hasException(scope)) [[unlikely]]
            return Exception { ExceptionCode::ExistingExceptionError };
        auto frame = frameConversionResult.releaseReturnValue();

        // We do not want to throw any exception in the transform to make sure we do not error the transform.
        WTF::switchOn(frame, [&](RefPtr<RTCEncodedAudioFrame>& value) {
            transformFrame(*value, globalObject, transformer.get(), *readableStreamSource, context.identifier(), weakThis);
        }, [&](RefPtr<RTCEncodedVideoFrame>& value) {
            transformFrame(*value, globalObject, transformer.get(), *readableStreamSource, context.identifier(), weakThis);
        }, [&](RefPtr<ArrayBuffer>& value) {
            transformFrame(value->span(), globalObject, transformer.get(), *readableStreamSource, context.identifier(), weakThis);
        }, [&](RefPtr<ArrayBufferView>& value) {
            transformFrame(value->span(), globalObject, transformer.get(), *readableStreamSource, context.identifier(), weakThis);
        });
        return { };
    }));
    if (writable.hasException())
        return writable.releaseException();

    m_readable = readable.releaseReturnValue();
    m_writable = writable.releaseReturnValue();
    if (m_isAttached) {
        m_readable->lock();
        m_writable->lock();
    }
    return { };
}

ExceptionOr<RefPtr<ReadableStream>> RTCRtpSFrameTransform::readable()
{
    if (!m_readable) {
        auto result = createStreams();
        if (result.hasException())
            return result.releaseException();
    }
    return m_readable.copyRef();
}

ExceptionOr<RefPtr<WritableStream>> RTCRtpSFrameTransform::writable()
{
    if (!m_writable) {
        auto result = createStreams();
        if (result.hasException())
            return result.releaseException();
    }

    m_hasWritable = true;
    return m_writable.copyRef();
}

bool RTCRtpSFrameTransform::virtualHasPendingActivity() const
{
    return (m_isAttached || m_hasWritable) && hasEventListeners();
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC)
