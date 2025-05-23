/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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
#include "LibWebRTCProviderCocoa.h"

#if USE(LIBWEBRTC)

#include "DeprecatedGlobalSettings.h"
#include "MediaCapabilitiesInfo.h"
#include "VP9UtilitiesCocoa.h"

#include <webrtc/api/create_peerconnection_factory.h>

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN

#include <webrtc/webkit_sdk/WebKit/WebKitDecoder.h>
#include <webrtc/webkit_sdk/WebKit/WebKitEncoder.h>
#include <webrtc/webkit_sdk/WebKit/WebKitVP9Decoder.h>

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

#include <wtf/MainThread.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/darwin/WeakLinking.h>

WTF_WEAK_LINK_FORCE_IMPORT(webrtc::CreatePeerConnectionFactory);

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(LibWebRTCProviderCocoa);

UniqueRef<WebRTCProvider> WebRTCProvider::create()
{
    return makeUniqueRef<LibWebRTCProviderCocoa>();
}

LibWebRTCProviderCocoa::~LibWebRTCProviderCocoa()
{
}

std::unique_ptr<webrtc::VideoDecoderFactory> LibWebRTCProviderCocoa::createDecoderFactory()
{
    ASSERT(isMainThread());

    if (!webRTCAvailable())
        return nullptr;

    auto vp9Support = isSupportingVP9Profile2() ? webrtc::WebKitVP9::Profile0And2 : isSupportingVP9Profile0() ? webrtc::WebKitVP9::Profile0 : webrtc::WebKitVP9::Off;
    return webrtc::createWebKitDecoderFactory(isSupportingH265() ? webrtc::WebKitH265::On : webrtc::WebKitH265::Off, vp9Support, vp9HardwareDecoderAvailable() ? webrtc::WebKitVP9VTB::On : webrtc::WebKitVP9VTB::Off, isSupportingAV1() ? webrtc::WebKitAv1::On : webrtc::WebKitAv1::Off);
}

std::unique_ptr<webrtc::VideoEncoderFactory> LibWebRTCProviderCocoa::createEncoderFactory()
{
    ASSERT(isMainThread());

    if (!webRTCAvailable())
        return nullptr;

    auto vp9Support = isSupportingVP9Profile2() ? webrtc::WebKitVP9::Profile0And2 : isSupportingVP9Profile0() ? webrtc::WebKitVP9::Profile0 : webrtc::WebKitVP9::Off;
    return webrtc::createWebKitEncoderFactory(isSupportingH265() ? webrtc::WebKitH265::On : webrtc::WebKitH265::Off, vp9Support, isSupportingAV1() ? webrtc::WebKitAv1::On : webrtc::WebKitAv1::Off);
}

std::optional<MediaCapabilitiesInfo> LibWebRTCProviderCocoa::computeVPParameters(const VideoConfiguration& configuration)
{
    return WebCore::computeVPParameters(configuration, isSupportingVP9HardwareDecoder());
}

bool LibWebRTCProviderCocoa::isVPSoftwareDecoderSmooth(const VideoConfiguration& configuration)
{
    return WebCore::isVPSoftwareDecoderSmooth(configuration);
}

bool WebRTCProvider::webRTCAvailable()
{
#if PLATFORM(IOS) || PLATFORM(VISION)
    ASSERT_WITH_MESSAGE(!!webrtc::CreatePeerConnectionFactory, "Failed to find or load libwebrtc");
    return true;
#else
    return !!webrtc::CreatePeerConnectionFactory;
#endif
}

void LibWebRTCProvider::registerWebKitVP9Decoder()
{
    if (webRTCAvailable())
        webrtc::registerWebKitVP9Decoder();
}

} // namespace WebCore

#endif // USE(LIBWEBRTC)
