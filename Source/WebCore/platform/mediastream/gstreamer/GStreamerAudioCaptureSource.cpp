/*
 * Copyright (C) 2018 Metrological Group B.V.
 * Copyright (C) 2020 Igalia S.L.
 * Author: Thibault Saunier <tsaunier@igalia.com>
 * Author: Alejandro G. Castro  <alex@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
#include "GStreamerAudioCaptureSource.h"

#include "GStreamerAudioData.h"
#include "GStreamerAudioStreamDescription.h"
#include "GStreamerCaptureDeviceManager.h"

#include <wtf/NeverDestroyed.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

static DoubleCapabilityRange defaultVolumeCapability()
{
    return { 0.0, 1.0 };
}
const static RealtimeMediaSourceCapabilities::EchoCancellation defaultEchoCancellationCapability = RealtimeMediaSourceCapabilities::EchoCancellation::OnOrOff;

GST_DEBUG_CATEGORY(webkit_audio_capture_source_debug);
#define GST_CAT_DEFAULT webkit_audio_capture_source_debug

class GStreamerAudioCaptureSourceFactory : public AudioCaptureFactory {
public:
    CaptureSourceOrError createAudioCaptureSource(const CaptureDevice& device, MediaDeviceHashSalts&& hashSalts, const MediaConstraints* constraints, std::optional<PageIdentifier>) final
    {
        // Here, like in GStreamerVideoCaptureSource, we could rely on the DesktopPortal and
        // PipeWireCaptureDeviceManager, but there is no audio desktop portal yet. See
        // https://github.com/flatpak/xdg-desktop-portal/discussions/1142.
        return GStreamerAudioCaptureSource::create(String { device.persistentId() }, WTFMove(hashSalts), constraints);
    }
private:
    CaptureDeviceManager& audioCaptureDeviceManager() final { return GStreamerAudioCaptureDeviceManager::singleton(); }
    const Vector<CaptureDevice>& speakerDevices() const final { return GStreamerAudioCaptureDeviceManager::singleton().speakerDevices(); }
};

static GStreamerAudioCaptureSourceFactory& libWebRTCAudioCaptureSourceFactory()
{
    static NeverDestroyed<GStreamerAudioCaptureSourceFactory> factory;
    return factory.get();
}

CaptureSourceOrError GStreamerAudioCaptureSource::create(String&& deviceID, MediaDeviceHashSalts&& hashSalts, const MediaConstraints* constraints)
{
    auto device = GStreamerAudioCaptureDeviceManager::singleton().gstreamerDeviceWithUID(deviceID);
    if (!device) {
        auto errorMessage = makeString("GStreamerAudioCaptureSource::create(): GStreamer did not find the device: "_s, deviceID, '.');
        return CaptureSourceOrError({ WTFMove(errorMessage), MediaAccessDenialReason::PermissionDenied });
    }

    auto source = adoptRef(*new GStreamerAudioCaptureSource(WTFMove(*device), WTFMove(hashSalts)));

    if (constraints) {
        if (auto result = source->applyConstraints(*constraints))
            return CaptureSourceOrError(CaptureSourceError { result->invalidConstraint });
    }
    return CaptureSourceOrError(WTFMove(source));
}

AudioCaptureFactory& GStreamerAudioCaptureSource::factory()
{
    return libWebRTCAudioCaptureSourceFactory();
}

GStreamerAudioCaptureSource::GStreamerAudioCaptureSource(GStreamerCaptureDevice&& device, MediaDeviceHashSalts&& hashSalts)
    : RealtimeMediaSource(device, WTFMove(hashSalts))
    , m_capturer(adoptRef(*new GStreamerAudioCapturer(WTFMove(device))))
{
    ensureGStreamerInitialized();

    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_audio_capture_source_debug, "webkitaudiocapturesource", 0, "WebKit Audio Capture Source.");
    });

    auto& singleton = GStreamerAudioCaptureDeviceManager::singleton();
    singleton.registerCapturer(m_capturer.copyRef());
}

GStreamerAudioCaptureSource::~GStreamerAudioCaptureSource()
{
    auto& singleton = GStreamerAudioCaptureDeviceManager::singleton();
    singleton.unregisterCapturer(*m_capturer);
}

void GStreamerAudioCaptureSource::captureEnded()
{
    m_capturer->stop();
    captureFailed();
}

void GStreamerAudioCaptureSource::captureDeviceUpdated(const GStreamerCaptureDevice& device)
{
    setName(AtomString { device.label() });
    setPersistentId(device.persistentId());
    configurationChanged();
}

std::pair<GstClockTime, GstClockTime> GStreamerAudioCaptureSource::queryCaptureLatency() const
{
    if (!m_capturer)
        return { GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE };

    return m_capturer->queryLatency();
}

void GStreamerAudioCaptureSource::startProducingData()
{
    m_capturer->setupPipeline();
    m_capturer->setSampleRate(sampleRate());
    m_capturer->setSinkAudioCallback([this](auto&& sample, auto&& presentationTime) {
        auto bufferSize = gst_buffer_get_size(gst_sample_get_buffer(sample.get()));
        GStreamerAudioData frames(WTFMove(sample));
        GStreamerAudioStreamDescription description(frames.getAudioInfo());
        audioSamplesAvailable(presentationTime, frames, description, bufferSize / description.getInfo().bpf);
    });
    m_capturer->start();
}

void GStreamerAudioCaptureSource::stopProducingData()
{
    m_capturer->stop();
}

const RealtimeMediaSourceCapabilities& GStreamerAudioCaptureSource::capabilities()
{
    if (m_capabilities)
        return m_capabilities.value();

    uint i;
    auto caps = m_capturer->caps();
    int minSampleRate = 0, maxSampleRate = 0;
    for (i = 0; i < gst_caps_get_size(caps.get()); i++) {
        int capabilityMinSampleRate = 0, capabilityMaxSampleRate = 0;
        GstStructure* str = gst_caps_get_structure(caps.get(), i);

        // Only accept raw audio for now.
        if (!gst_structure_has_name(str, "audio/x-raw"))
            continue;

        gst_structure_get(str, "rate", GST_TYPE_INT_RANGE, &capabilityMinSampleRate, &capabilityMaxSampleRate, nullptr);
        if (i > 0) {
            minSampleRate = std::min(minSampleRate, capabilityMinSampleRate);
            maxSampleRate = std::max(maxSampleRate, capabilityMaxSampleRate);
        } else {
            minSampleRate = capabilityMinSampleRate;
            maxSampleRate = capabilityMaxSampleRate;
        }
    }

    RealtimeMediaSourceCapabilities capabilities(settings().supportedConstraints());
    capabilities.setDeviceId(hashedId());
    capabilities.setEchoCancellation(defaultEchoCancellationCapability);
    capabilities.setVolume(defaultVolumeCapability());
    capabilities.setSampleRate({ minSampleRate, maxSampleRate });
    m_capabilities = WTFMove(capabilities);

    return m_capabilities.value();
}

void GStreamerAudioCaptureSource::settingsDidChange(OptionSet<RealtimeMediaSourceSettings::Flag> settings)
{
    if (settings.contains(RealtimeMediaSourceSettings::Flag::SampleRate))
        m_capturer->setSampleRate(sampleRate());
}

const RealtimeMediaSourceSettings& GStreamerAudioCaptureSource::settings()
{
    if (!m_currentSettings) {
        RealtimeMediaSourceSettings settings;
        settings.setDeviceId(hashedId());

        RealtimeMediaSourceSupportedConstraints supportedConstraints;
        supportedConstraints.setSupportsDeviceId(true);
        supportedConstraints.setSupportsEchoCancellation(true);
        supportedConstraints.setSupportsVolume(true);
        supportedConstraints.setSupportsSampleRate(true);
        settings.setSupportedConstraints(supportedConstraints);

        m_currentSettings = WTFMove(settings);
    }

    m_currentSettings->setVolume(volume());
    m_currentSettings->setSampleRate(sampleRate());
    m_currentSettings->setEchoCancellation(echoCancellation());

    return m_currentSettings.value();
}

bool GStreamerAudioCaptureSource::interrupted() const
{
    if (m_capturer->pipeline())
        return m_capturer->isInterrupted() || RealtimeMediaSource::interrupted();

    return RealtimeMediaSource::interrupted();
}

void GStreamerAudioCaptureSource::setInterruptedForTesting(bool isInterrupted)
{
    m_capturer->setInterrupted(isInterrupted);
    RealtimeMediaSource::setInterruptedForTesting(isInterrupted);
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
