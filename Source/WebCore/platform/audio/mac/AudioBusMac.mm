/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#import "config.h"

#if ENABLE(WEB_AUDIO)

#import "AudioBus.h"

#import "AudioFileReader.h"
#import <wtf/cocoa/SpanCocoa.h>

@interface WebCoreAudioBundleClass : NSObject
@end

@implementation WebCoreAudioBundleClass
@end

namespace WebCore {

RefPtr<AudioBus> AudioBus::loadPlatformResource(const char* name, float sampleRate)
{
    @autoreleasepool {
        NSBundle *bundle = [NSBundle bundleForClass:[WebCoreAudioBundleClass class]];
        NSURL *audioFileURL = [bundle URLForResource:[NSString stringWithUTF8String:name] withExtension:@"wav" subdirectory:@"audio"];
        if (NSData *audioData = [NSData dataWithContentsOfURL:audioFileURL options:NSDataReadingMappedIfSafe error:nil])
            return createBusFromInMemoryAudioFile(span(audioData), false, sampleRate);
    }

    ASSERT_NOT_REACHED();
    return nullptr;
}

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO)
