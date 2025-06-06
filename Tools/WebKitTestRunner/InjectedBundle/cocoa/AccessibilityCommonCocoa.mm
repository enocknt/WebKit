/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2017-2022 Apple Inc. All rights reserved.
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

#import "config.h"
#import "AccessibilityCommonCocoa.h"

#import "AccessibilityTextMarker.h"
#import "JSWrapper.h"
#import "StringFunctions.h"
#import <JavaScriptCore/JSStringRefCF.h>
#import <objc/runtime.h>
#import <wtf/ObjCRuntimeExtras.h>

@implementation NSString (JSStringRefAdditions)

+ (NSString *)stringWithJSStringRef:(JSStringRef)jsStringRef
{
    if (!jsStringRef)
        return nil;

    return adoptCF(JSStringCopyCFString(kCFAllocatorDefault, jsStringRef)).bridgingAutorelease();
}

- (JSRetainPtr<JSStringRef>)createJSStringRef
{
    return adopt(JSStringCreateWithCFString((__bridge CFStringRef)self));
}

@end

namespace WTR {

Class webAccessibilityObjectWrapperClass()
{
    static Class cls = objc_getClass("WebAccessibilityObjectWrapper");
    ASSERT(cls);
    return cls;
}

JSObjectRef makeJSArray(JSContextRef context, NSArray *array)
{
    NSUInteger count = array.count;
IGNORE_WARNINGS_BEGIN("vla")
    JSValueRef arguments[count];
IGNORE_WARNINGS_END
    for (NSUInteger i = 0; i < count; i++)
        arguments[i] = makeValueRefForValue(context, [array objectAtIndex:i]);
    return JSObjectMakeArray(context, count, arguments, nullptr);
}

static JSObjectRef makeJSObject(JSContextRef context, NSDictionary *dictionary)
{
    auto object = JSObjectMake(context, nullptr, nullptr);
    [dictionary enumerateKeysAndObjectsUsingBlock:^(NSString *key, id obj, BOOL *) {
        if (JSValueRef propertyValue = makeValueRefForValue(context, obj))
            JSObjectSetProperty(context, object, [key createJSStringRef].get(), propertyValue, kJSPropertyAttributeNone, nullptr);
    }];
    return object;
}

JSValueRef makeValueRefForValue(JSContextRef context, id value)
{
    if ([value isKindOfClass:[NSString class]])
        return JSValueMakeString(context, [value createJSStringRef].get());
    if ([value isKindOfClass:[NSNumber class]]) {
        if (nsValueHasObjCType<BOOL>((NSValue *)value) || nsValueHasObjCType<char>((NSValue *)value))
            return JSValueMakeBoolean(context, [value boolValue]);
        return JSValueMakeNumber(context, [value doubleValue]);
    }
    if ([value isKindOfClass:webAccessibilityObjectWrapperClass()])
        return toJS(context, WTR::AccessibilityUIElement::create(static_cast<PlatformUIElement>(value)).ptr());
    if ([value isKindOfClass:[NSDictionary class]])
        return makeJSObject(context, value);
    if ([value isKindOfClass:[NSArray class]])
        return makeJSArray(context, value);
#if PLATFORM(MAC)
    if (value && CFGetTypeID((__bridge CFTypeRef)value) == AXTextMarkerGetTypeID()) {
        Ref marker = AccessibilityTextMarker::create(value);
        return JSObjectMake(context, marker->wrapperClass(), marker.ptr());
    }
#endif // PLATFORM(MAC)
    return nullptr;
}

NSDictionary *searchPredicateForSearchCriteria(JSContextRef context, AccessibilityUIElement *startElement, AccessibilityTextMarkerRange* startRange, bool isDirectionNext, unsigned resultsLimit, JSValueRef searchKey, JSStringRef searchText, bool visibleOnly, bool immediateDescendantsOnly)
{
    NSMutableDictionary *parameterizedAttribute = [NSMutableDictionary dictionary];

    if (startElement && startElement->platformUIElement())
        [parameterizedAttribute setObject:startElement->platformUIElement() forKey:@"AXStartElement"];

    if (startRange)
        [parameterizedAttribute setObject:startRange->platformTextMarkerRange() forKey:@"AXStartRange"];

    [parameterizedAttribute setObject:(isDirectionNext) ? @"AXDirectionNext" : @"AXDirectionPrevious" forKey:@"AXDirection"];

    [parameterizedAttribute setObject:@(resultsLimit) forKey:@"AXResultsLimit"];

    if (searchKey) {
        RetainPtr<id> searchKeyParameter;
        if (JSValueIsString(context, searchKey))
            searchKeyParameter = toWTFString(context, searchKey).createNSString();
        else if (JSValueIsObject(context, searchKey)) {
            JSObjectRef searchKeyArray = JSValueToObject(context, searchKey, nullptr);
            unsigned searchKeyArrayLength = arrayLength(context, searchKeyArray);
            for (unsigned i = 0; i < searchKeyArrayLength; ++i) {
                auto searchKey = toWTFString(context, JSObjectGetPropertyAtIndex(context, searchKeyArray, i, nullptr));
                if (!searchKeyParameter)
                    searchKeyParameter = [NSMutableArray array];
                [searchKeyParameter addObject:searchKey.createNSString().get()];
            }
        }
        if (searchKeyParameter)
            [parameterizedAttribute setObject:searchKeyParameter.get() forKey:@"AXSearchKey"];
    }

    if (searchText && JSStringGetLength(searchText))
        [parameterizedAttribute setObject:toWTFString(searchText).createNSString().get() forKey:@"AXSearchText"];

    [parameterizedAttribute setObject:@(visibleOnly) forKey:@"AXVisibleOnly"];
    [parameterizedAttribute setObject:@(immediateDescendantsOnly) forKey:@"AXImmediateDescendantsOnly"];

    return parameterizedAttribute;
}

} // namespace WTR

