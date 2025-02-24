/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if USE(CG)

#include <pal/spi/cg/CoreGraphicsSPI.h>
#include <wtf/SoftLinking.h>

#if HAVE(CG_CONTEXT_SET_OWNER_IDENTITY)
#include <mach/mach_types.h>
#endif

SOFT_LINK_FRAMEWORK_FOR_SOURCE_WITH_EXPORT(PAL, CoreGraphics, PAL_EXPORT)

#if HAVE(CG_CONTEXT_SET_OWNER_IDENTITY)
SOFT_LINK_FUNCTION_FOR_SOURCE_WITH_EXPORT(PAL, CoreGraphics, CGContextSetOwnerIdentity, void, (CGContextRef context, task_id_token_t owner), (context, owner), PAL_EXPORT)
#endif

#if PLATFORM(MAC)
SOFT_LINK_FUNCTION_MAY_FAIL_FOR_SOURCE_WITH_EXPORT(PAL, CoreGraphics, CGWindowListCreateImage, CGImageRef, (CGRect screenBounds, CGWindowListOption listOption, CGWindowID windowID, CGWindowImageOption imageOption), (screenBounds, listOption, windowID, imageOption), PAL_EXPORT)
#endif

#if HAVE(IOSURFACE)
SOFT_LINK_FUNCTION_MAY_FAIL_FOR_SOURCE_WITH_EXPORT(PAL, CoreGraphics, CGIOSurfaceContextInvalidateSurface, void, (CGContextRef context), (context), PAL_EXPORT)
#endif

#endif
