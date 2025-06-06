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

#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <wtf/PrintStream.h>
#include <wtf/StdLibExtras.h>

namespace WTF {

WTF_EXPORT_PRIVATE PrintStream& dataFile();
WTF_EXPORT_PRIVATE void setDataFile(const char* path);
WTF_EXPORT_PRIVATE void setDataFile(std::unique_ptr<PrintStream>&&);

WTF_EXPORT_PRIVATE void dataLogFV(const char* format, va_list) WTF_ATTRIBUTE_PRINTF(1, 0);
WTF_EXPORT_PRIVATE void dataLogF(const char* format, ...) WTF_ATTRIBUTE_PRINTF(1, 2);
WTF_EXPORT_PRIVATE void dataLogFString(const char*);

template<typename... Types>
NEVER_INLINE void dataLog(const Types&... values)
{
    dataFile().print(values...);
}

template<typename... Types>
void dataLogLn(const Types&... values)
{
    dataLog(values..., "\n");
}

#define dataLogIf(shouldLog, ...) do { \
        using ShouldLogType = std::decay_t<decltype(shouldLog)>; \
        static_assert(std::is_same_v<ShouldLogType, bool> || std::is_enum_v<ShouldLogType>, "You probably meant to pass a bool or enum as dataLogIf's first parameter"); \
        if (shouldLog) [[unlikely]] \
            dataLog(__VA_ARGS__); \
    } while (0)

#define dataLogLnIf(shouldLog, ...) do { \
        using ShouldLogType = std::decay_t<decltype(shouldLog)>; \
        static_assert(std::is_same_v<ShouldLogType, bool> || std::is_enum_v<ShouldLogType>, "You probably meant to pass a bool or enum as dataLogLnIf's first parameter"); \
        if (shouldLog) [[unlikely]] \
            dataLogLn(__VA_ARGS__); \
    } while (0)

#define dataLogFIf(shouldLog, ...) do { \
        using ShouldLogType = std::decay_t<decltype(shouldLog)>; \
        static_assert(std::is_same_v<ShouldLogType, bool> || std::is_enum_v<ShouldLogType>, "You probably meant to pass a bool or enum as dataLogIf's first parameter"); \
        if (shouldLog) [[unlikely]] \
            dataLogF(__VA_ARGS__); \
    } while (0)

#define dataLogFVIf(shouldLog, ...) do { \
        using ShouldLogType = std::decay_t<decltype(shouldLog)>; \
        static_assert(std::is_same_v<ShouldLogType, bool> || std::is_enum_v<ShouldLogType>, "You probably meant to pass a bool or enum as dataLogIf's first parameter"); \
        if (shouldLog) [[unlikely]] \
            dataLogFV(__VA_ARGS__); \
    } while (0)

#define dataLogFStringIf(shouldLog, ...) do { \
        using ShouldLogType = std::decay_t<decltype(shouldLog)>; \
        static_assert(std::is_same_v<ShouldLogType, bool> || std::is_enum_v<ShouldLogType>, "You probably meant to pass a bool or enum as dataLogIf's first parameter"); \
        if (shouldLog) [[unlikely]] \
            dataLogFString(__VA_ARGS__); \
    } while (0)

} // namespace WTF

using WTF::dataLog;
using WTF::dataLogLn;
using WTF::dataLogF;
using WTF::dataLogFString;
