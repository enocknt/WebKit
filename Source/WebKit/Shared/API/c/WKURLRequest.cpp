/*
 * Copyright (C) 2010, 2011 Apple Inc. All rights reserved.
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
#include "WKURLRequest.h"

#include "APIURLRequest.h"
#include "WKAPICast.h"
#include "WKData.h"
#include <wtf/StdLibExtras.h>
#include <wtf/URL.h>

WKTypeID WKURLRequestGetTypeID()
{
    return WebKit::toAPI(API::URLRequest::APIType);
}

WKURLRequestRef WKURLRequestCreateWithWKURL(WKURLRef url)
{
    return WebKit::toAPILeakingRef(API::URLRequest::create(URL { WebKit::toImpl(url)->string() }));
}

WKURLRef WKURLRequestCopyURL(WKURLRequestRef requestRef)
{
    return WebKit::toCopiedURLAPI(WebKit::toImpl(requestRef)->resourceRequest().url());
}

WKURLRef WKURLRequestCopyFirstPartyForCookies(WKURLRequestRef requestRef)
{
    return WebKit::toCopiedURLAPI(WebKit::toImpl(requestRef)->resourceRequest().firstPartyForCookies());
}

WKStringRef WKURLRequestCopyHTTPMethod(WKURLRequestRef requestRef)
{
    return WebKit::toCopiedAPI(WebKit::toImpl(requestRef)->resourceRequest().httpMethod());
}

WKURLRequestRef WKURLRequestCopySettingHTTPBody(WKURLRequestRef requestRef, WKDataRef body)
{
    WebCore::ResourceRequest requestCopy(WebKit::toImpl(requestRef)->resourceRequest());
    requestCopy.setHTTPBody(WebCore::FormData::create(WKDataGetSpan(body)));
    return WebKit::toAPILeakingRef(API::URLRequest::create(requestCopy));
}

void WKURLRequestSetDefaultTimeoutInterval(double timeoutInterval)
{
    API::URLRequest::setDefaultTimeoutInterval(timeoutInterval);
}
