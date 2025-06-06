/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "ScriptExecutionContext.h"
#include <wtf/Vector.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class DOMURL;
class ScriptExecutionContext;
template<typename> class ExceptionOr;

class URLSearchParams : public RefCounted<URLSearchParams> {
public:
    ~URLSearchParams();

    static ExceptionOr<Ref<URLSearchParams>> create(Variant<Vector<Vector<String>>, Vector<KeyValuePair<String, String>>, String>&&);
    static Ref<URLSearchParams> create(const String& string, DOMURL* associatedURL)
    {
        return adoptRef(*new URLSearchParams(string, associatedURL));
    }

    size_t size() const { return m_pairs.size(); }

    void append(const String& name, const String& value);
    void remove(const String& name, const String& value = { });
    String get(const String& name) const;
    Vector<String> getAll(const String& name) const;
    bool has(const String& name, const String& value = { }) const;
    void set(const String& name, const String& value);
    String toString() const;
    void updateFromAssociatedURL();
    void sort();

    class Iterator {
    public:
        explicit Iterator(URLSearchParams&);
        std::optional<KeyValuePair<String, String>> next();

    private:
        const Ref<URLSearchParams> m_target;
        size_t m_index { 0 };
    };
    Iterator createIterator(ScriptExecutionContext*) { return Iterator { *this }; }

private:
    const Vector<KeyValuePair<String, String>>& pairs() const { return m_pairs; }
    URLSearchParams(const String&, DOMURL*);
    URLSearchParams(const Vector<KeyValuePair<String, String>>&);
    void updateURL();

    WeakPtr<DOMURL> m_associatedURL;
    using PairType = KeyValuePair<String, String>;
    Vector<PairType> m_pairs;
};

} // namespace WebCore
