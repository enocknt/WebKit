/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
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

#include <WebCore/ResourceLoaderIdentifier.h>
#include <wtf/Function.h>
#include <wtf/HashMap.h>

namespace WebKit {

class NetworkResourceLoader;
class NetworkConnectionToWebProcess;

class NetworkResourceLoadMap {
public:
    using MapType = HashMap<WebCore::ResourceLoaderIdentifier, Ref<NetworkResourceLoader>>;
    NetworkResourceLoadMap(Function<void(bool hasUpload)>&&);
    ~NetworkResourceLoadMap();

    bool isEmpty() const { return m_loaders.isEmpty(); }
    bool contains(WebCore::ResourceLoaderIdentifier identifier) const { return m_loaders.contains(identifier); }
    MapType::iterator begin() LIFETIME_BOUND { return m_loaders.begin(); }
    MapType::ValuesIteratorRange values() { return m_loaders.values(); }
    void clear();

    MapType::AddResult add(WebCore::ResourceLoaderIdentifier, Ref<NetworkResourceLoader>&&);
    NetworkResourceLoader* get(WebCore::ResourceLoaderIdentifier) const;
    bool remove(WebCore::ResourceLoaderIdentifier);
    RefPtr<NetworkResourceLoader> take(WebCore::ResourceLoaderIdentifier);

    bool hasUpload() const { return m_hasUpload; }

private:
    void setHasUpload(bool);

    MapType m_loaders;
    bool m_hasUpload { false };
    Function<void(bool hasUpload)> m_hasUploadChangeListener;
};

} // namespace WebKit
