/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2011-2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "HiddenInputType.h"

#include "DOMFormData.h"
#include "ElementInlines.h"
#include "FormController.h"
#include "HTMLInputElement.h"
#include "HTMLNames.h"
#include "InputTypeNames.h"
#include "RenderElement.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(HiddenInputType);

using namespace HTMLNames;

const AtomString& HiddenInputType::formControlType() const
{
    return InputTypeNames::hidden();
}

FormControlState HiddenInputType::saveFormControlState() const
{
    // valueAttributeWasUpdatedAfterParsing() never be true for form controls create by createElement() or cloneNode().
    // It's OK for now because we restore values only to form controls created by parsing.
    ASSERT(element());
    Ref element = *this->element();
    return element->valueAttributeWasUpdatedAfterParsing() ? FormControlState { { AtomString { element->value() } } } : FormControlState { };
}

void HiddenInputType::restoreFormControlState(const FormControlState& state)
{
    ASSERT(element());
    protectedElement()->setAttributeWithoutSynchronization(valueAttr, AtomString { state[0] });
}

RenderPtr<RenderElement> HiddenInputType::createInputRenderer(RenderStyle&&)
{
    ASSERT_NOT_REACHED();
    return nullptr;
}

bool HiddenInputType::accessKeyAction(bool)
{
    return false;
}

bool HiddenInputType::rendererIsNeeded()
{
    return false;
}

bool HiddenInputType::storesValueSeparateFromAttribute()
{
    return false;
}

void HiddenInputType::setValue(const String& sanitizedValue, bool, TextFieldEventBehavior, TextControlSetValueSelection)
{
    ASSERT(element());
    protectedElement()->setAttributeWithoutSynchronization(valueAttr, AtomString { sanitizedValue });
}

bool HiddenInputType::appendFormData(DOMFormData& formData) const
{
    ASSERT(element());
    Ref element = *this->element();
    auto name = element->name();

    if (equalIgnoringASCIICase(name, "_charset_"_s)) {
        formData.append(name, String::fromLatin1(formData.encoding().name()));
        return true;
    }
    InputType::appendFormData(formData);
    if (auto& dirname = element->attributeWithoutSynchronization(dirnameAttr); !dirname.isNull())
        formData.append(dirname, element->directionForFormData());
    return true;
}

bool HiddenInputType::shouldRespectHeightAndWidthAttributes()
{
    return true;
}

bool HiddenInputType::dirAutoUsesValue() const
{
    return true;
}

} // namespace WebCore
