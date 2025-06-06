/*
 * Copyright (C) 2021-2023 Apple Inc. All rights reserved.
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

#pragma once

#include "GPUOrigin2DDict.h"
#include "HTMLCanvasElement.h"
#include "HTMLImageElement.h"
#include "HTMLVideoElement.h"
#include "ImageBitmap.h"
#include "ImageData.h"
#include "OffscreenCanvas.h"
#include "WebCodecsVideoFrame.h"
#include "WebGPUImageCopyExternalImage.h"
#include <optional>
#include <wtf/RefPtr.h>

namespace WebCore {

struct GPUImageCopyExternalImage {
    using SourceType = Variant<RefPtr<ImageBitmap>,
#if ENABLE(VIDEO) && ENABLE(WEB_CODECS)
    RefPtr<ImageData>, RefPtr<HTMLImageElement>, RefPtr<HTMLVideoElement>, RefPtr<WebCodecsVideoFrame>,
#endif
#if ENABLE(OFFSCREEN_CANVAS)
    RefPtr<OffscreenCanvas>,
#endif
    RefPtr<HTMLCanvasElement>>;

    WebGPU::ImageCopyExternalImage convertToBacking() const
    {
        return {
            // FIXME: Handle the canvas element.
            origin ? std::optional { WebCore::convertToBacking(*origin) } : std::nullopt,
            flipY,
        };
    }

    SourceType source;
    std::optional<GPUOrigin2D> origin;
    bool flipY { false };
};

}
