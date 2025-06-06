/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
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

#import "config.h"
#import "RecurringPaymentRequest.h"

#if HAVE(PASSKIT_RECURRING_PAYMENTS)

#import <WebCore/ApplePayRecurringPaymentRequest.h>
#import <WebCore/PaymentSummaryItems.h>
#import <wtf/RetainPtr.h>

#import <pal/cocoa/PassKitSoftLink.h>

namespace WebKit {
using namespace WebCore;

RetainPtr<PKRecurringPaymentRequest> platformRecurringPaymentRequest(const ApplePayRecurringPaymentRequest& webRecurringPaymentRequest)
{
    auto pkRecurringPaymentRequest = adoptNS([PAL::allocPKRecurringPaymentRequestInstance() initWithPaymentDescription:webRecurringPaymentRequest.paymentDescription.createNSString().get() regularBilling:platformRecurringSummaryItem(webRecurringPaymentRequest.regularBilling) managementURL:adoptNS([[NSURL alloc] initWithString:webRecurringPaymentRequest.managementURL.createNSString().get()]).get()]);
    if (auto& trialBilling = webRecurringPaymentRequest.trialBilling)
        [pkRecurringPaymentRequest setTrialBilling:platformRecurringSummaryItem(*trialBilling)];
    if (auto& billingAgreement = webRecurringPaymentRequest.billingAgreement; !billingAgreement.isNull())
        [pkRecurringPaymentRequest setBillingAgreement:billingAgreement.createNSString().get()];
    if (auto& tokenNotificationURL = webRecurringPaymentRequest.tokenNotificationURL; !tokenNotificationURL.isNull())
        [pkRecurringPaymentRequest setTokenNotificationURL:adoptNS([[NSURL alloc] initWithString:tokenNotificationURL.createNSString().get()]).get()];
    return pkRecurringPaymentRequest;
}

} // namespace WebKit

#endif // HAVE(PASSKIT_RECURRING_PAYMENTS)
