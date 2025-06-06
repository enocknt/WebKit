/* GStreamer ClearKey common encryption decryptor
 *
 * Copyright (C) 2013 YouView TV Ltd. <alex.ashley@youview.com>
 * Copyright (C) 2016 Metrological
 * Copyright (C) 2016 Igalia S.L
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#include "config.h"
#include "WebKitCommonEncryptionDecryptorGStreamer.h"

#if ENABLE(ENCRYPTED_MEDIA) && USE(GSTREAMER)

#include "CDMProxy.h"
#include "GStreamerCommon.h"
#include "GStreamerEMEUtilities.h"
#include "GUniquePtrGStreamer.h"
#include <wtf/Condition.h>
#include <wtf/PrintStream.h>
#include <wtf/RunLoop.h>
#include <wtf/Scope.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/glib/WTFGType.h>

using WebCore::CDMProxy;

// Instances of this class are tied to the decryptor lifecycle. They can't be alive after the decryptor has been destroyed.
class CDMProxyDecryptionClientImplementation : public WebCore::CDMProxyDecryptionClient {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(CDMProxyDecryptionClientImplementation);
public:
    CDMProxyDecryptionClientImplementation(WebKitMediaCommonEncryptionDecrypt* decryptor)
        : m_decryptor(decryptor) { }
    virtual bool isAborting()
    {
        return webKitMediaCommonEncryptionDecryptIsAborting(m_decryptor);
    }
    virtual ~CDMProxyDecryptionClientImplementation() = default;
private:
    WebKitMediaCommonEncryptionDecrypt* m_decryptor;
};

enum DecryptionState {
    Idle,
    Decrypting,
    FlushPending
};

#define WEBKIT_MEDIA_CENC_DECRYPT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), WEBKIT_TYPE_MEDIA_CENC_DECRYPT, WebKitMediaCommonEncryptionDecryptPrivate))
struct _WebKitMediaCommonEncryptionDecryptPrivate {
    RefPtr<CDMProxy> cdmProxy;

    // Protect the access to the structure members.
    Lock lock;
    Condition condition;

    bool isFlushing { false };
    bool isStopped { true };
    std::unique_ptr<CDMProxyDecryptionClientImplementation> cdmProxyDecryptionClientImplementation;
    enum DecryptionState decryptionState { DecryptionState::Idle };
};

static constexpr Seconds MaxSecondsToWaitForCDMProxy = 5_s;

static void constructed(GObject*);
static GstStateChangeReturn changeState(GstElement*, GstStateChange transition);
static GstCaps* transformCaps(GstBaseTransform*, GstPadDirection, GstCaps*, GstCaps*);
static gboolean acceptCaps(GstBaseTransform*, GstPadDirection, GstCaps*);
static GstFlowReturn transformInPlace(GstBaseTransform*, GstBuffer*);
static gboolean sinkEventHandler(GstBaseTransform*, GstEvent*);
static void setContext(GstElement*, GstContext*);

GST_DEBUG_CATEGORY(webkit_media_common_encryption_decrypt_debug_category);
#define GST_CAT_DEFAULT webkit_media_common_encryption_decrypt_debug_category

WEBKIT_DEFINE_TYPE(WebKitMediaCommonEncryptionDecrypt, webkit_media_common_encryption_decrypt, GST_TYPE_BASE_TRANSFORM)

static void webkit_media_common_encryption_decrypt_class_init(WebKitMediaCommonEncryptionDecryptClass* klass)
{
    GObjectClass* gobjectClass = G_OBJECT_CLASS(klass);
    gobjectClass->constructed = constructed;

    GST_DEBUG_CATEGORY_INIT(webkit_media_common_encryption_decrypt_debug_category,
        "webkitcenc", 0, "Common Encryption base class");

    GstElementClass* elementClass = GST_ELEMENT_CLASS(klass);
    elementClass->change_state = GST_DEBUG_FUNCPTR(changeState);
    elementClass->set_context = GST_DEBUG_FUNCPTR(setContext);

    GstBaseTransformClass* baseTransformClass = GST_BASE_TRANSFORM_CLASS(klass);
    baseTransformClass->transform_ip = GST_DEBUG_FUNCPTR(transformInPlace);
    baseTransformClass->transform_caps = GST_DEBUG_FUNCPTR(transformCaps);
    baseTransformClass->accept_caps = GST_DEBUG_FUNCPTR(acceptCaps);
    baseTransformClass->transform_ip_on_passthrough = FALSE;
    baseTransformClass->passthrough_on_same_caps = TRUE;
    baseTransformClass->sink_event = GST_DEBUG_FUNCPTR(sinkEventHandler);
}

static void constructed(GObject* object)
{
    G_OBJECT_CLASS(webkit_media_common_encryption_decrypt_parent_class)->constructed(object);

    GstBaseTransform* base = GST_BASE_TRANSFORM(object);
    gst_base_transform_set_in_place(base, TRUE);
    gst_base_transform_set_passthrough(base, FALSE);
    gst_base_transform_set_gap_aware(base, FALSE);

    WebKitMediaCommonEncryptionDecrypt* self = WEBKIT_MEDIA_CENC_DECRYPT(base);
    WebKitMediaCommonEncryptionDecryptPrivate* priv = WEBKIT_MEDIA_CENC_DECRYPT_GET_PRIVATE(self);
    priv->cdmProxyDecryptionClientImplementation = makeUnique<CDMProxyDecryptionClientImplementation>(self);
}

static GstCaps* transformCaps(GstBaseTransform* base, GstPadDirection direction, GstCaps* caps, GstCaps* filter)
{
    if (direction == GST_PAD_UNKNOWN)
        return nullptr;

    GST_DEBUG_OBJECT(base, "direction: %s, caps: %" GST_PTR_FORMAT " filter: %" GST_PTR_FORMAT, (direction == GST_PAD_SRC) ? "src" : "sink", caps, filter);

    GstCaps* transformedCaps = gst_caps_new_empty();
    WebKitMediaCommonEncryptionDecrypt* self = WEBKIT_MEDIA_CENC_DECRYPT(base);
    WebKitMediaCommonEncryptionDecryptClass* klass = WEBKIT_MEDIA_CENC_DECRYPT_GET_CLASS(self);

    unsigned size = gst_caps_get_size(caps);
    for (unsigned i = 0; i < size; ++i) {
        GstStructure* incomingStructure = gst_caps_get_structure(caps, i);
        GUniquePtr<GstStructure> outgoingStructure = nullptr;

        if (direction == GST_PAD_SINK) {
            bool canDoPassthrough = false;
            if (!gst_structure_has_field(incomingStructure, "original-media-type")) {
                // Let's check compatibility with src pad caps to see if we can do passthrough.
                GRefPtr<GstCaps> srcPadTemplateCaps = adoptGRef(gst_pad_get_pad_template_caps(GST_BASE_TRANSFORM_SRC_PAD(base)));
                unsigned srcPadTemplateCapsSize = gst_caps_get_size(srcPadTemplateCaps.get());
                for (unsigned j = 0; !canDoPassthrough && j < srcPadTemplateCapsSize; ++j)
                    canDoPassthrough = gst_structure_can_intersect(incomingStructure, gst_caps_get_structure(srcPadTemplateCaps.get(), j));

                if (!canDoPassthrough)
                    continue;
            }

            outgoingStructure = GUniquePtr<GstStructure>(gst_structure_copy(incomingStructure));

            if (!canDoPassthrough) {
                auto originalMediaTypeView = WebCore::gstStructureGetString(outgoingStructure.get(), "original-media-type"_s);
                RELEASE_ASSERT(originalMediaTypeView);
                auto originalMediaType = originalMediaTypeView.utf8();
                gst_structure_set_name(outgoingStructure.get(), originalMediaType.data());
            }

            // Filter out the DRM related fields from the down-stream caps.
            gst_structure_remove_fields(outgoingStructure.get(), "protection-system", "original-media-type", "encryption-algorithm", "encoding-scope", "cipher-mode", nullptr);
        } else {
            outgoingStructure = GUniquePtr<GstStructure>(gst_structure_copy(incomingStructure));
            if (!gst_base_transform_is_passthrough(base)) {
                // Filter out the video related fields from the up-stream caps,
                // because they are not relevant to the input caps of this element and
                // can cause caps negotiation failures with adaptive bitrate streams.
                gst_structure_remove_fields(outgoingStructure.get(), "base-profile", "codec_data", "height", "framerate", "level", "pixel-aspect-ratio", "profile", "rate", "width", nullptr);

                auto nameView = WebCore::gstStructureGetName(incomingStructure);
                auto name = nameView.utf8();
                gst_structure_set(outgoingStructure.get(), "protection-system", G_TYPE_STRING, klass->protectionSystemId(self),
                    "original-media-type", G_TYPE_STRING, name.data() , nullptr);

                // GST_PROTECTION_UNSPECIFIED_SYSTEM_ID was added in the GStreamer
                // developement git master which will ship as version 1.16.0.
                gst_structure_set_name(outgoingStructure.get(), !g_strcmp0(klass->protectionSystemId(self),
                    GST_PROTECTION_UNSPECIFIED_SYSTEM_ID) ? "application/x-webm-enc" : "application/x-cenc");
            }
        }

        bool duplicate = false;
        unsigned size = gst_caps_get_size(transformedCaps);

        for (unsigned index = 0; !duplicate && index < size; ++index) {
            GstStructure* structure = gst_caps_get_structure(transformedCaps, index);
            if (gst_structure_is_equal(structure, outgoingStructure.get()))
                duplicate = true;
        }

        if (!duplicate)
            gst_caps_append_structure(transformedCaps, outgoingStructure.release());
    }

    if (filter) {
        GstCaps* intersection;

        GST_DEBUG_OBJECT(base, "Using filter caps %" GST_PTR_FORMAT, filter);
        intersection = gst_caps_intersect_full(transformedCaps, filter, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(transformedCaps);
        transformedCaps = intersection;
    }

    GST_DEBUG_OBJECT(base, "returning %" GST_PTR_FORMAT, transformedCaps);
    return transformedCaps;
}

static gboolean acceptCaps(GstBaseTransform* trans, GstPadDirection direction, GstCaps* caps)
{
    gboolean result = GST_BASE_TRANSFORM_CLASS(webkit_media_common_encryption_decrypt_parent_class)->accept_caps(trans, direction, caps);

    if (result || direction == GST_PAD_SRC)
        return result;

    GRefPtr<GstCaps> srcPadTemplateCaps = adoptGRef(gst_pad_get_pad_template_caps(GST_BASE_TRANSFORM_SRC_PAD(trans)));
    result = gst_caps_can_intersect(caps, srcPadTemplateCaps.get());
    GST_TRACE_OBJECT(trans, "attempted to match %" GST_PTR_FORMAT " with the src pad template caps %" GST_PTR_FORMAT " to see if we can go passthrough mode, result %s",
        caps, srcPadTemplateCaps.get(), boolForPrinting(result));

    return result;
}

static GstFlowReturn transformInPlace(GstBaseTransform* base, GstBuffer* buffer)
{
    WebKitMediaCommonEncryptionDecrypt* self = WEBKIT_MEDIA_CENC_DECRYPT(base);
    WebKitMediaCommonEncryptionDecryptPrivate* priv = WEBKIT_MEDIA_CENC_DECRYPT_GET_PRIVATE(self);

    GstProtectionMeta* protectionMeta = reinterpret_cast<GstProtectionMeta*>(gst_buffer_get_protection_meta(buffer));
    if (!protectionMeta) {
        GST_TRACE_OBJECT(self, "Buffer %p does not contain protection meta, not decrypting", buffer);
        return GST_FLOW_OK;
    }

    Locker locker { priv->lock };

    if (priv->isFlushing) {
        GST_DEBUG_OBJECT(self, "Decryption aborted because of flush");
        return GST_FLOW_FLUSHING;
    }

    // The CDM instance needs to be negotiated before we can begin decryption.
    if (!priv->cdmProxy) {
        GST_DEBUG_OBJECT(self, "CDM not available, going to wait for it");
        priv->condition.waitFor(priv->lock, MaxSecondsToWaitForCDMProxy, [priv] {
            return priv->isFlushing || priv->cdmProxy || priv->isStopped;
        });
        // Note that waitFor() releases the lock internally while it waits, so isFlushing may have been changed.
        if (priv->isFlushing) {
            GST_DEBUG_OBJECT(self, "Decryption aborted because of flush");
            return GST_FLOW_FLUSHING;
        }
        if (priv->isStopped) {
            GST_DEBUG_OBJECT(self, "Element is closing");
            return GST_FLOW_OK;
        }
        if (!priv->cdmProxy) {
            GST_ELEMENT_ERROR(self, STREAM, FAILED, ("CDMProxy was not retrieved in time"), (nullptr));
            return GST_FLOW_NOT_SUPPORTED;
        }
        GST_DEBUG_OBJECT(self, "CDM now available with address %p", priv->cdmProxy.get());
    }

    // We are still going to be locked when running this, so no need to lock here.
    auto scopeExit = makeScopeExit([buffer, protectionMeta, priv] {
        gst_buffer_remove_meta(buffer, reinterpret_cast<GstMeta*>(protectionMeta));
        priv->decryptionState = DecryptionState::Idle;
    });

    bool isCbcs = false;
    if (auto cipherMode = WebCore::gstStructureGetString(protectionMeta->info, "cipher-mode"_s))
        isCbcs = WTF::equalIgnoringASCIICase(cipherMode, "cbcs"_s);

    auto ivSizeFromMeta = WebCore::gstStructureGet<unsigned>(protectionMeta->info, "iv_size"_s);
    if (!ivSizeFromMeta) {
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Failed to get iv_size"), (nullptr));
        return GST_FLOW_NOT_SUPPORTED;
    }

    unsigned ivSize;
    if (*ivSizeFromMeta)
        ivSize = *ivSizeFromMeta;
    else {
        auto constantIvSize = WebCore::gstStructureGet<unsigned>(protectionMeta->info, "constant_iv_size"_s);
        if (!constantIvSize) {
            GST_ELEMENT_ERROR(self, STREAM, FAILED, ("No iv_size and failed to get constant_iv_size"), (nullptr));
            ASSERT(isCbcs);
            return GST_FLOW_NOT_SUPPORTED;
        }
        ivSize = *constantIvSize;
    }

    auto isEncrypted = WebCore::gstStructureGet<bool>(protectionMeta->info, "encrypted"_s);
    if (!isEncrypted) {
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Failed to get encrypted flag"), (nullptr));
        return GST_FLOW_NOT_SUPPORTED;
    }

    if (!ivSize || !*isEncrypted) {
        GST_TRACE_OBJECT(self, "iv size %u, encrypted %s, bailing out OK as unencrypted", ivSize, boolForPrinting(*isEncrypted));
        return GST_FLOW_OK;
    }

    GST_DEBUG_OBJECT(base, "protection meta: %" GST_PTR_FORMAT, protectionMeta->info);

    auto subSampleCountFromMeta = WebCore::gstStructureGet<unsigned>(protectionMeta->info, "subsample_count"_s);
    // cbcs could not include the subsample_count.
    if (!subSampleCountFromMeta && !isCbcs) {
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Failed to get subsample_count"), (nullptr));
        return GST_FLOW_NOT_SUPPORTED;
    }

    const GValue* value;
    GstBuffer* subSamplesBuffer = nullptr;
    auto subSampleCount = subSampleCountFromMeta.value_or(0);
    if (subSampleCount) {
        value = gst_structure_get_value(protectionMeta->info, "subsamples");
        if (!value) {
            GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Failed to get subsamples"), (nullptr));
            return GST_FLOW_NOT_SUPPORTED;
        }
        subSamplesBuffer = gst_value_get_buffer(value);
        if (!subSamplesBuffer) {
            GST_ELEMENT_ERROR(self, STREAM, FAILED, ("There is no subsamples buffer, but a positive subsample count"), (nullptr));
            return GST_FLOW_NOT_SUPPORTED;
        }
    }

    value = gst_structure_get_value(protectionMeta->info, "kid");

    if (!value) {
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Failed to get key id for buffer"), (nullptr));
        return GST_FLOW_NOT_SUPPORTED;
    }
    GstBuffer* keyIDBuffer = gst_value_get_buffer(value);

    value = gst_structure_get_value(protectionMeta->info, "iv");
    if (!value) {
        GST_ELEMENT_ERROR(self, STREAM, FAILED, ("Failed to get IV for sample"), (nullptr));
        return GST_FLOW_NOT_SUPPORTED;
    }

    GstBuffer* ivBuffer = gst_value_get_buffer(value);
    WebKitMediaCommonEncryptionDecryptClass* klass = WEBKIT_MEDIA_CENC_DECRYPT_GET_CLASS(self);

    ASSERT(priv->decryptionState == DecryptionState::Idle);
    // Value is set back to Idle in the scopeExit.
    priv->decryptionState = DecryptionState::Decrypting;

    GST_TRACE_OBJECT(self, "decrypting");

    bool didDecryptionSucceed;

    // Temporarily release the lock while we don't need to access priv. The lower level API is used
    // in order to avoid creating several scopes with different Locker instances in each one.
    {
        DropLockForScope noLockScope { locker };
        didDecryptionSucceed = klass->decrypt(self, ivBuffer, keyIDBuffer, buffer, subSampleCount, subSamplesBuffer);
    }

    // Accessing priv members again.
    if (priv->isFlushing || priv->decryptionState == DecryptionState::FlushPending) {
        GST_DEBUG_OBJECT(self, "flushing, bailing out");
        return GST_FLOW_FLUSHING;
    }

    if (!didDecryptionSucceed) {
        GST_ELEMENT_ERROR(self, STREAM, DECRYPT, ("Decryption failed"), (nullptr));
        return GST_FLOW_NOT_SUPPORTED;
    }

    return GST_FLOW_OK;
}

static bool isCDMProxyAvailable(WebKitMediaCommonEncryptionDecrypt* self)
{
    WebKitMediaCommonEncryptionDecryptPrivate* priv = WEBKIT_MEDIA_CENC_DECRYPT_GET_PRIVATE(self);
    Locker locker { priv->lock };
    return priv->cdmProxy;
}

static CDMProxy* getCDMProxyFromGstContext(WebKitMediaCommonEncryptionDecrypt* self)
{
    GRefPtr<GstContext> context = adoptGRef(gst_element_get_context(GST_ELEMENT(self), "drm-cdm-proxy"));
    CDMProxy* proxy = nullptr;

    // According to the GStreamer documentation, if we can't find the context, we should run a downstream query, then an upstream one and then send a bus
    // message. In this case that does not make a lot of sense since only the app (player) answers it, meaning that no query is going to solve it. A message
    // could be helpful but the player sets the context as soon as it gets the CDMInstance and if it does not have it, we have no way of asking for one as it is
    // something provided by crossplatform code. This means that we won't be able to answer the bus request in any way either. Summing up, neither queries nor bus
    // requests are useful here.
    if (context) {
        const GValue* value = gst_structure_get_value(gst_context_get_structure(context.get()), "cdm-proxy");
        if (value) {
            proxy = reinterpret_cast<CDMProxy*>(g_value_get_pointer(value));
            if (proxy) {
                GST_DEBUG_OBJECT(self, "received a new CDM proxy instance %p, refcount %u", proxy, proxy->refCount());
                return proxy;
            }
        }
        GST_TRACE_OBJECT(self, "CDMProxy not available in the context");
    }
    return nullptr;
}

static void attachCDMProxy(WebKitMediaCommonEncryptionDecrypt* self, CDMProxy* proxy)
{
    WebKitMediaCommonEncryptionDecryptPrivate* priv = WEBKIT_MEDIA_CENC_DECRYPT_GET_PRIVATE(self);
    WebKitMediaCommonEncryptionDecryptClass* klass = WEBKIT_MEDIA_CENC_DECRYPT_GET_CLASS(self);

    Locker locker { priv->lock };
    GST_DEBUG_OBJECT(self, "Attaching CDMProxy %p", proxy);
    priv->cdmProxy = proxy;
    klass->cdmProxyAttached(self, priv->cdmProxy);
    priv->condition.notifyOne();
}

static gboolean installCDMProxyIfNotAvailable(WebKitMediaCommonEncryptionDecrypt* self)
{
    if (!isCDMProxyAvailable(self)) {
        gboolean result = FALSE;

        CDMProxy* proxy = getCDMProxyFromGstContext(self);
        if (proxy) {
            attachCDMProxy(self, proxy);
            result = TRUE;
        } else {
            GST_ERROR_OBJECT(self, "Failed to retrieve CDMProxy from context");
            result = FALSE;
        }
        return result;
    }

    return TRUE;
}

static gboolean sinkEventHandler(GstBaseTransform* trans, GstEvent* event)
{
    WebKitMediaCommonEncryptionDecrypt* self = WEBKIT_MEDIA_CENC_DECRYPT(trans);
    WebKitMediaCommonEncryptionDecryptPrivate* priv = WEBKIT_MEDIA_CENC_DECRYPT_GET_PRIVATE(self);

    // FIXME: https://bugs.webkit.org/show_bug.cgi?id=191355
    // We should be handling protection events in this class in
    // addition to out-of-band data. In regular playback, after a
    // preferred system ID context is set, any future protection
    // events will not be handled by the demuxer, so they must be
    // handled in here.
    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB: {
        GST_DEBUG_OBJECT(self, "Custom Downstream OOB %" GST_PTR_FORMAT, event);

        if (gst_event_has_name(event, "attempt-to-decrypt")) {
            GST_DEBUG_OBJECT(self, "Handling attempt-to-decrypt");
            gboolean result = installCDMProxyIfNotAvailable(self);
            gst_event_unref(event);
            return result;
        }
        // Let event propagate.
        break;
    }
    case GST_EVENT_FLUSH_START:
        GST_DEBUG_OBJECT(self, "Flush-start");
        {
            Locker locker { priv->lock };
            bool isCdmProxyAttached = priv->cdmProxy;
            priv->isFlushing = true;
            ASSERT(priv->decryptionState == DecryptionState::Idle || priv->decryptionState == DecryptionState::Decrypting);
            if (priv->decryptionState == DecryptionState::Decrypting)
                priv->decryptionState = DecryptionState::FlushPending;
            if (isCdmProxyAttached) {
                locker.unlockEarly();
                priv->cdmProxy->abortWaitingForKey();
            } else
                priv->condition.notifyOne();
        }
        break;
    case GST_EVENT_FLUSH_STOP:
        GST_DEBUG_OBJECT(self, "Flush-stop");
        {
            Locker locker { priv->lock };
            priv->isFlushing = false;
        }
        break;
    default:
        break;
    }

    return GST_BASE_TRANSFORM_CLASS(webkit_media_common_encryption_decrypt_parent_class)->sink_event(trans, event);
}

bool webKitMediaCommonEncryptionDecryptIsAborting(WebKitMediaCommonEncryptionDecrypt* self)
{
    WebKitMediaCommonEncryptionDecryptPrivate* priv = WEBKIT_MEDIA_CENC_DECRYPT_GET_PRIVATE(self);
    Locker locker { priv->lock };
    return priv->isFlushing || priv->isStopped;
}

WeakPtr<WebCore::CDMProxyDecryptionClient> webKitMediaCommonEncryptionDecryptGetCDMProxyDecryptionClient(WebKitMediaCommonEncryptionDecrypt* self)
{
    WebKitMediaCommonEncryptionDecryptPrivate* priv = WEBKIT_MEDIA_CENC_DECRYPT_GET_PRIVATE(self);
    return *priv->cdmProxyDecryptionClientImplementation;
}

static GstStateChangeReturn changeState(GstElement* element, GstStateChange transition)
{
    WebKitMediaCommonEncryptionDecrypt* self = WEBKIT_MEDIA_CENC_DECRYPT(element);
    WebKitMediaCommonEncryptionDecryptPrivate* priv = WEBKIT_MEDIA_CENC_DECRYPT_GET_PRIVATE(self);

    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED: {
        GST_DEBUG_OBJECT(self, "READY->PAUSED");

        Locker locker(priv->lock);
        priv->isStopped = false;
        break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_READY: {
        // We need to do this here instead of after the , otherwise we won't be able to break the wait.
        GST_DEBUG_OBJECT(self, "PAUSED->READY");

        Locker locker(priv->lock);
        priv->isStopped = true;
        priv->condition.notifyOne();
        if (priv->cdmProxy)
            priv->cdmProxy->abortWaitingForKey();
        break;
    }
    default:
        break;
    }

    GstStateChangeReturn result = GST_ELEMENT_CLASS(webkit_media_common_encryption_decrypt_parent_class)->change_state(element, transition);

    // Add post-transition code here.

    return result;
}

static void setContext(GstElement* element, GstContext* context)
{
    WebKitMediaCommonEncryptionDecrypt* self = WEBKIT_MEDIA_CENC_DECRYPT(element);
    WebKitMediaCommonEncryptionDecryptPrivate* priv = WEBKIT_MEDIA_CENC_DECRYPT_GET_PRIVATE(self);
    WebKitMediaCommonEncryptionDecryptClass* klass = WEBKIT_MEDIA_CENC_DECRYPT_GET_CLASS(self);

    if (gst_context_has_context_type(context, "drm-cdm-proxy")) {
        const GValue* value = gst_structure_get_value(gst_context_get_structure(context), "cdm-proxy");
        Locker locker { priv->lock };
        priv->cdmProxy = value ? reinterpret_cast<CDMProxy*>(g_value_get_pointer(value)) : nullptr;
        GST_DEBUG_OBJECT(self, "received new CDMInstance %p", priv->cdmProxy.get());
        klass->cdmProxyAttached(self, priv->cdmProxy);
        return;
    }

    GST_ELEMENT_CLASS(webkit_media_common_encryption_decrypt_parent_class)->set_context(element, context);
}

#undef GST_CAT_DEFAULT

#endif // ENABLE(ENCRYPTED_MEDIA) && USE(GSTREAMER)
