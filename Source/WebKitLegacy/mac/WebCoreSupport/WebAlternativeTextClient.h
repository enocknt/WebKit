/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
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

#import "CorrectionPanel.h"
#import <WebCore/AlternativeTextClient.h>
#import <wtf/TZoneMalloc.h>

@class WebView;

class WebAlternativeTextClient : public WebCore::AlternativeTextClient {
    WTF_MAKE_TZONE_ALLOCATED(WebAlternativeTextClient);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(WebAlternativeTextClient);
public:
    explicit WebAlternativeTextClient(WebView *);
    virtual ~WebAlternativeTextClient();
#if USE(AUTOCORRECTION_PANEL)
    void showCorrectionAlternative(WebCore::AlternativeTextType, const WebCore::FloatRect& boundingBoxOfReplacedString, const String& replacedString, const String& replacementString, const Vector<String>& alternativeReplacementStrings, WebCore::FrameIdentifier) override;
    void dismissAlternative(WebCore::ReasonForDismissingAlternativeText) override;
    String dismissAlternativeSoon(WebCore::ReasonForDismissingAlternativeText) override;
    void recordAutocorrectionResponse(WebCore::AutocorrectionResponse, const String& replacedString, const String& replacementString) override;
#endif
    void showDictationAlternativeUI(const WebCore::FloatRect& boundingBoxOfDictatedText, WebCore::DictationContext) override;
    void removeDictationAlternatives(WebCore::DictationContext) override;
    Vector<String> dictationAlternatives(WebCore::DictationContext) override;

private:
    WebView *m_webView;

#if USE(AUTOCORRECTION_PANEL)
    CorrectionPanel m_correctionPanel;
#endif
};
