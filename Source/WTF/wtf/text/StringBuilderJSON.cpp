/*
 * Copyright (C) 2010-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2017 Yusuke Suzuki <utatane.tea@gmail.com>. All rights reserved.
 * Copyright (C) 2017 Mozilla Foundation. All rights reserved.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "config.h"
#include <wtf/text/StringBuilderJSON.h>

#include <wtf/text/EscapedFormsForJSON.h>
#include <wtf/text/ParsingUtilities.h>
#include <wtf/text/WTFString.h>

namespace WTF {

void StringBuilder::appendQuotedJSONString(const String& string)
{
    if (hasOverflowed())
        return;

    // Make sure we have enough buffer space to append this string for worst case without reallocating.
    // The 2 is for the '"' quotes on each end.
    // The 6 is the worst case for a single code unit that could be encoded as \uNNNN.
    CheckedInt32 stringLength = string.length();
    stringLength *= 6;
    stringLength += 2;
    if (stringLength.hasOverflowed()) {
        didOverflow();
        return;
    }

    // We need to use saturatedSum<uint32_t>() below instead of saturatedSum<int32_t>
    // because INT_MAX is a valid capacity value. If stringLengthValue is greater than
    // INT_MAX, but is saturated to INT_MAX, then we'll end up allocating an INT_MAX
    // sized buffer and try to write beyond it resulting in a crash due to std:::span.
    // Ideally, we should fail with an overflow instead.
    //
    // Using saturatedSum<uint32_t>(), the sum can be:
    // 1. less or equal to INT_MAX.
    // 2. greater than INT_MAX but not be saturated.
    // 3. be saturated at UINT_MAX.
    // For (1), things work like normal as expected (limited only by available memory).
    // For (2), the extendBufferForAppending calls will reject the new capacity because
    // the underlying buffer is backed by a StringImpl, which is capped at a max length
    // of INT_MAX.
    // For (3), the saturated value of UINT_MAX will be rejected just like (2).
    auto stringLengthValue = static_cast<uint32_t>(stringLength.value());

    if (is8Bit() && string.is8Bit()) {
        if (auto output = extendBufferForAppending<LChar>(saturatedSum<uint32_t>(m_length, stringLengthValue)); output.data()) {
            output = output.first(stringLengthValue);
            consume(output) = '"';
            appendEscapedJSONStringContent(output, string.span8());
            consume(output) = '"';
            if (!output.empty())
                shrink(m_length - output.size());
        }
    } else {
        if (auto output = extendBufferForAppendingWithUpconvert(saturatedSum<uint32_t>(m_length, stringLengthValue)); output.data()) {
            output = output.first(stringLengthValue);
            consume(output) = '"';
            if (string.is8Bit())
                appendEscapedJSONStringContent(output, string.span8());
            else
                appendEscapedJSONStringContent(output, string.span16());
            consume(output) = '"';
            if (!output.empty())
                shrink(m_length - output.size());
        }
    }
}

} // namespace WTF
