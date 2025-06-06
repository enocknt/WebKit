/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2020 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(WEB_AUDIO)

#include "AudioListener.h"

#include "AudioBus.h"
#include "AudioParam.h"
#include "AudioUtilities.h"
#include "ExceptionOr.h"

namespace WebCore {

AudioListener::AudioListener(BaseAudioContext& context)
    : m_positionX(AudioParam::create(context, "positionX"_s, 0.0, -FLT_MAX, FLT_MAX, AutomationRate::ARate))
    , m_positionY(AudioParam::create(context, "positionY"_s, 0.0, -FLT_MAX, FLT_MAX, AutomationRate::ARate))
    , m_positionZ(AudioParam::create(context, "positionZ"_s, 0.0, -FLT_MAX, FLT_MAX, AutomationRate::ARate))
    , m_forwardX(AudioParam::create(context, "forwardX"_s, 0.0, -FLT_MAX, FLT_MAX, AutomationRate::ARate))
    , m_forwardY(AudioParam::create(context, "forwardY"_s, 0.0, -FLT_MAX, FLT_MAX, AutomationRate::ARate))
    , m_forwardZ(AudioParam::create(context, "forwardZ"_s, -1.0, -FLT_MAX, FLT_MAX, AutomationRate::ARate))
    , m_upX(AudioParam::create(context, "upX"_s, 0.0, -FLT_MAX, FLT_MAX, AutomationRate::ARate))
    , m_upY(AudioParam::create(context, "upY"_s, 1.0, -FLT_MAX, FLT_MAX, AutomationRate::ARate))
    , m_upZ(AudioParam::create(context, "upZ"_s, 0.0, -FLT_MAX, FLT_MAX, AutomationRate::ARate))
    , m_positionXValues(AudioUtilities::renderQuantumSize)
    , m_positionYValues(AudioUtilities::renderQuantumSize)
    , m_positionZValues(AudioUtilities::renderQuantumSize)
    , m_forwardXValues(AudioUtilities::renderQuantumSize)
    , m_forwardYValues(AudioUtilities::renderQuantumSize)
    , m_forwardZValues(AudioUtilities::renderQuantumSize)
    , m_upXValues(AudioUtilities::renderQuantumSize)
    , m_upYValues(AudioUtilities::renderQuantumSize)
    , m_upZValues(AudioUtilities::renderQuantumSize)
{
}

AudioListener::~AudioListener() = default;

bool AudioListener::hasSampleAccurateValues() const
{
    return m_positionX->hasSampleAccurateValues()
        || m_positionY->hasSampleAccurateValues()
        || m_positionZ->hasSampleAccurateValues()
        || m_forwardX->hasSampleAccurateValues()
        || m_forwardY->hasSampleAccurateValues()
        || m_forwardZ->hasSampleAccurateValues()
        || m_upX->hasSampleAccurateValues()
        || m_upY->hasSampleAccurateValues()
        || m_upZ->hasSampleAccurateValues();
}

bool AudioListener::shouldUseARate() const
{
    return m_positionX->automationRate() == AutomationRate::ARate
        || m_positionY->automationRate() == AutomationRate::ARate
        || m_positionZ->automationRate() == AutomationRate::ARate
        || m_forwardX->automationRate() == AutomationRate::ARate
        || m_forwardY->automationRate() == AutomationRate::ARate
        || m_forwardZ->automationRate() == AutomationRate::ARate
        || m_upX->automationRate() == AutomationRate::ARate
        || m_upY->automationRate() == AutomationRate::ARate
        || m_upZ->automationRate() == AutomationRate::ARate;
}

void AudioListener::updateValuesIfNeeded(size_t framesToProcess)
{
    if (!positionX().context())
        return;

    double currentTime = positionX().context()->currentTime();
    if (m_lastUpdateTime != currentTime) {
        // Time has changed. Update all of the automation values now.
        m_lastUpdateTime = currentTime;

        ASSERT(framesToProcess <= m_positionXValues.size());
        ASSERT(framesToProcess <= m_positionYValues.size());
        ASSERT(framesToProcess <= m_positionZValues.size());
        ASSERT(framesToProcess <= m_forwardXValues.size());
        ASSERT(framesToProcess <= m_forwardYValues.size());
        ASSERT(framesToProcess <= m_forwardZValues.size());
        ASSERT(framesToProcess <= m_upXValues.size());
        ASSERT(framesToProcess <= m_upYValues.size());
        ASSERT(framesToProcess <= m_upZValues.size());

        positionX().calculateSampleAccurateValues(m_positionXValues.span().first(framesToProcess));
        positionY().calculateSampleAccurateValues(m_positionYValues.span().first(framesToProcess));
        positionZ().calculateSampleAccurateValues(m_positionZValues.span().first(framesToProcess));

        forwardX().calculateSampleAccurateValues(m_forwardXValues.span().first(framesToProcess));
        forwardY().calculateSampleAccurateValues(m_forwardYValues.span().first(framesToProcess));
        forwardZ().calculateSampleAccurateValues(m_forwardZValues.span().first(framesToProcess));

        upX().calculateSampleAccurateValues(m_upXValues.span().first(framesToProcess));
        upY().calculateSampleAccurateValues(m_upYValues.span().first(framesToProcess));
        upZ().calculateSampleAccurateValues(m_upZValues.span().first(framesToProcess));
    }
}

void AudioListener::updateDirtyState()
{
    ASSERT(!isMainThread());

    auto lastPosition = std::exchange(m_lastPosition, position());
    m_isPositionDirty = lastPosition != m_lastPosition;

    auto lastOrientation = std::exchange(m_lastOrientation, orientation());
    m_isOrientationDirty = lastOrientation != m_lastOrientation;

    auto lastUpVector = std::exchange(m_lastUpVector, upVector());
    m_isUpVectorDirty = lastUpVector != m_lastUpVector;
}

std::span<const float> AudioListener::positionXValues(size_t framesToProcess)
{
    updateValuesIfNeeded(framesToProcess);
    return m_positionXValues.span();
}

std::span<const float> AudioListener::positionYValues(size_t framesToProcess)
{
    updateValuesIfNeeded(framesToProcess);
    return m_positionYValues.span();
}

std::span<const float> AudioListener::positionZValues(size_t framesToProcess)
{
    updateValuesIfNeeded(framesToProcess);
    return m_positionZValues.span();
}

std::span<const float> AudioListener::forwardXValues(size_t framesToProcess)
{
    updateValuesIfNeeded(framesToProcess);
    return m_forwardXValues.span();
}

std::span<const float> AudioListener::forwardYValues(size_t framesToProcess)
{
    updateValuesIfNeeded(framesToProcess);
    return m_forwardYValues.span();
}

std::span<const float> AudioListener::forwardZValues(size_t framesToProcess)
{
    updateValuesIfNeeded(framesToProcess);
    return m_forwardZValues.span();
}

std::span<const float> AudioListener::upXValues(size_t framesToProcess)
{
    updateValuesIfNeeded(framesToProcess);
    return m_upXValues.span();
}

std::span<const float> AudioListener::upYValues(size_t framesToProcess)
{
    updateValuesIfNeeded(framesToProcess);
    return m_upYValues.span();
}

std::span<const float> AudioListener::upZValues(size_t framesToProcess)
{
    updateValuesIfNeeded(framesToProcess);
    return m_upZValues.span();
}

ExceptionOr<void> AudioListener::setPosition(float x, float y, float z)
{
    ASSERT(isMainThread());
    if (!m_positionX->context())
        return { };

    double now = m_positionX->context()->currentTime();

    auto result = m_positionX->setValueAtTime(x, now);
    if (result.hasException())
        return result.releaseException();
    result = m_positionY->setValueAtTime(y, now);
    if (result.hasException())
        return result.releaseException();
    result = m_positionZ->setValueAtTime(z, now);
    if (result.hasException())
        return result.releaseException();

    return { };
}

FloatPoint3D AudioListener::position() const
{
    return FloatPoint3D { m_positionX->value(), m_positionY->value(), m_positionZ->value() };
}

ExceptionOr<void> AudioListener::setOrientation(float x, float y, float z, float upX, float upY, float upZ)
{
    ASSERT(isMainThread());
    if (!m_forwardX->context())
        return { };

    double now = m_forwardX->context()->currentTime();

    auto result = m_forwardX->setValueAtTime(x, now);
    if (result.hasException())
        return result.releaseException();
    result = m_forwardY->setValueAtTime(y, now);
    if (result.hasException())
        return result.releaseException();
    result = m_forwardZ->setValueAtTime(z, now);
    if (result.hasException())
        return result.releaseException();
    result = m_upX->setValueAtTime(upX, now);
    if (result.hasException())
        return result.releaseException();
    result = m_upY->setValueAtTime(upY, now);
    if (result.hasException())
        return result.releaseException();
    result = m_upZ->setValueAtTime(upZ, now);
    if (result.hasException())
        return result.releaseException();

    return { };
}

FloatPoint3D AudioListener::orientation() const
{
    return FloatPoint3D { m_forwardX->value(), m_forwardY->value(), m_forwardZ->value() };
}

FloatPoint3D AudioListener::upVector() const
{
    return FloatPoint3D { m_upX->value(), m_upY->value(), m_upZ->value() };
}

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO)
