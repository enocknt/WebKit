/*
 * Copyright (C) 2023 Igalia S.L
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

#pragma once

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "GRefPtrGStreamer.h"
#include "VideoDecoder.h"

#include <wtf/TZoneMalloc.h>

namespace WebCore {

class GStreamerInternalVideoDecoder;

class GStreamerVideoDecoder final : public VideoDecoder {
    WTF_MAKE_TZONE_ALLOCATED(GStreamerVideoDecoder);

public:
    static void create(const String& codecName, const Config&, CreateCallback&&, OutputCallback&&);

    GStreamerVideoDecoder(const String& codecName, const Config&, OutputCallback&&, GRefPtr<GstElement>&&);
    ~GStreamerVideoDecoder();

private:
    Ref<DecodePromise> decode(EncodedFrame&&) final;
    Ref<GenericPromise> flush() final;
    void reset() final;
    void close() final;

    Ref<GStreamerInternalVideoDecoder> m_internalDecoder;
};

}

#endif // ENABLE(VIDEO) && USE(GSTREAMER)
