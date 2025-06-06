/*
 * Copyright (c) 2021 Apple Inc. All rights reserved.
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

#ifndef PAS_LOCAL_ALLOCATOR_KIND
#define PAS_LOCAL_ALLOCATOR_KIND

#include "pas_utils.h"

PAS_BEGIN_EXTERN_C;

enum pas_local_allocator_kind {
    pas_local_allocator_decommitted_kind,
    pas_local_allocator_stopped_allocator_kind,
    pas_local_allocator_allocator_kind,
    pas_local_allocator_stopped_view_cache_kind,
    pas_local_allocator_view_cache_kind
};

typedef enum pas_local_allocator_kind pas_local_allocator_kind;

static inline const char* pas_local_allocator_kind_get_string(pas_local_allocator_kind kind)
{
    switch (kind) {
    case pas_local_allocator_decommitted_kind:
        return "decommitted";
    case pas_local_allocator_stopped_allocator_kind:
        return "stopped_allocator";
    case pas_local_allocator_allocator_kind:
        return "allocator";
    case pas_local_allocator_stopped_view_cache_kind:
        return "stopped_view_cache";
    case pas_local_allocator_view_cache_kind:
        return "view_cache";
    }
    PAS_ASSERT_NOT_REACHED();
    return NULL;
}

static inline bool pas_local_allocator_kind_is_stopped(pas_local_allocator_kind kind)
{
    switch (kind) { 
    case pas_local_allocator_decommitted_kind:
    case pas_local_allocator_stopped_allocator_kind:
    case pas_local_allocator_stopped_view_cache_kind:
        return true;
    case pas_local_allocator_allocator_kind:
    case pas_local_allocator_view_cache_kind:
        return false;
    }
    PAS_ASSERT_NOT_REACHED();
    return false;
}

PAS_END_EXTERN_C;

#endif /* PAS_LOCAL_ALLOCATOR_KIND */

