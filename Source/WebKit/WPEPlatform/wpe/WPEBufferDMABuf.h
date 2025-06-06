/*
 * Copyright (C) 2023 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WPEBufferDMABuf_h
#define WPEBufferDMABuf_h

#if !defined(__WPE_PLATFORM_H_INSIDE__) && !defined(BUILDING_WEBKIT)
#error "Only <wpe/wpe-platform.h> can be included directly."
#endif

#include <glib-object.h>
#include <wpe/WPEDefines.h>
#include <wpe/WPEBuffer.h>

G_BEGIN_DECLS

#define WPE_TYPE_BUFFER_DMA_BUF (wpe_buffer_dma_buf_get_type())
WPE_API G_DECLARE_FINAL_TYPE (WPEBufferDMABuf, wpe_buffer_dma_buf, WPE, BUFFER_DMA_BUF, WPEBuffer)

WPE_API WPEBufferDMABuf *wpe_buffer_dma_buf_new          (WPEDisplay      *display,
                                                          int              width,
                                                          int              height,
                                                          guint32          format,
                                                          guint32          n_planes,
                                                          int             *fds,
                                                          guint32         *offsets,
                                                          guint32         *strides,
                                                          guint64          modifier);
WPE_API guint32          wpe_buffer_dma_buf_get_format   (WPEBufferDMABuf *buffer);
WPE_API guint32          wpe_buffer_dma_buf_get_n_planes (WPEBufferDMABuf* buffer);
WPE_API int              wpe_buffer_dma_buf_get_fd       (WPEBufferDMABuf *buffer,
                                                          guint32          plane);
WPE_API guint32          wpe_buffer_dma_buf_get_offset   (WPEBufferDMABuf *buffer,
                                                          guint32          plane);
WPE_API guint32          wpe_buffer_dma_buf_get_stride   (WPEBufferDMABuf *buffer,
                                                          guint32          plane);
WPE_API guint64          wpe_buffer_dma_buf_get_modifier (WPEBufferDMABuf *buffer);
WPE_API void             wpe_buffer_dma_buf_set_rendering_fence  (WPEBufferDMABuf *buffer,
                                                                  int              fd);
WPE_API int              wpe_buffer_dma_buf_get_rendering_fence  (WPEBufferDMABuf *buffer);
WPE_API int              wpe_buffer_dma_buf_take_rendering_fence (WPEBufferDMABuf *buffer);
WPE_API void             wpe_buffer_dma_buf_set_release_fence    (WPEBufferDMABuf *buffer,
                                                                  int              fd);
WPE_API int              wpe_buffer_dma_buf_get_release_fence    (WPEBufferDMABuf *buffer);
WPE_API int              wpe_buffer_dma_buf_take_release_fence   (WPEBufferDMABuf *buffer);

G_END_DECLS

#endif /* WPEBuffer_h */
