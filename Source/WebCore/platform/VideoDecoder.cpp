/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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

#include "config.h"
#include "VideoDecoder.h"

#if USE(LIBWEBRTC) && PLATFORM(COCOA)
#include "LibWebRTCVPXVideoDecoder.h"
#include "WebRTCProvider.h"
#endif

#if USE(GSTREAMER)
#include "VideoDecoderGStreamer.h"
#endif

#include <wtf/UniqueRef.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

VideoDecoder::CreatorFunction VideoDecoder::s_customCreator = nullptr;

void VideoDecoder::setCreatorCallback(CreatorFunction&& function)
{
    s_customCreator = WTFMove(function);
}

bool VideoDecoder::isVPXSupported()
{
#if USE(LIBWEBRTC) && PLATFORM(COCOA)
    return WebRTCProvider::webRTCAvailable();
#elif USE(GSTREAMER)
    return true;
#else
    return false;
#endif
}

Ref<VideoDecoder::CreatePromise> VideoDecoder::create(const String& codecName, const Config& config, OutputCallback&& outputCallback)
{
    CreatePromise::Producer producer;
    Ref promise = producer.promise();
    CreateCallback callback = [producer = WTFMove(producer)] (auto&& result) mutable {
        producer.settle(WTFMove(result));
    };

    if (s_customCreator) {
        s_customCreator(codecName, config, WTFMove(callback), WTFMove(outputCallback));
        return promise;
    }
    createLocalDecoder(codecName, config, WTFMove(callback), WTFMove(outputCallback));
    return promise;
}

#define LE_CHR(a, b, c, d) (((a)<<24) | ((b)<<16) | ((c)<<8) | (d))

String VideoDecoder::fourCCToCodecString(uint32_t fourCC)
{
    switch (fourCC) {
    case LE_CHR('v', 'p', '0', '8'): return "vp8"_s;
    case LE_CHR('v', 'p', '0', '9'): return "vp09.00"_s;
    case LE_CHR('a', 'v', '0', '1'): return "av01."_s;
    default:
            return nullString();
    }
}

void VideoDecoder::createLocalDecoder(const String& codecName, const Config& config, CreateCallback&& callback, OutputCallback&& outputCallback)
{
#if USE(LIBWEBRTC) && PLATFORM(COCOA)
    if (codecName == "vp8"_s) {
        LibWebRTCVPXVideoDecoder::create(LibWebRTCVPXVideoDecoder::Type::VP8, config, WTFMove(callback), WTFMove(outputCallback));
        return;
    }
    if (codecName.startsWith("vp09.00"_s)) {
        LibWebRTCVPXVideoDecoder::create(LibWebRTCVPXVideoDecoder::Type::VP9, config, WTFMove(callback), WTFMove(outputCallback));
        return;
    }
    if (codecName.startsWith("vp09.02"_s)) {
        LibWebRTCVPXVideoDecoder::create(LibWebRTCVPXVideoDecoder::Type::VP9_P2, config, WTFMove(callback), WTFMove(outputCallback));
        return;
    }
#if ENABLE(AV1)
    if (codecName.startsWith("av01."_s)) {
        LibWebRTCVPXVideoDecoder::create(LibWebRTCVPXVideoDecoder::Type::AV1, config, WTFMove(callback), WTFMove(outputCallback));
        return;
    }
#endif
#elif USE(GSTREAMER)
    GStreamerVideoDecoder::create(codecName, config, WTFMove(callback), WTFMove(outputCallback));
    return;
#else
    UNUSED_PARAM(codecName);
    UNUSED_PARAM(config);
    UNUSED_PARAM(outputCallback);
#endif

    callback(makeUnexpected("Not supported"_s));
}

VideoDecoder::VideoDecoder() = default;
VideoDecoder::~VideoDecoder() = default;

}
