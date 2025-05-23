/*
 * Copyright (C) 2022-2024 Apple Inc. All rights reserved.
 * Copyright (C) 2023 Igalia S.L
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

#pragma once

#include "WebCodecsAudioData.h"

#if ENABLE(WEB_CODECS)

#include <ExceptionOr.h>
#include <span>

namespace WebCore {

bool isValidAudioDataInit(const WebCodecsAudioData::Init&);

bool isAudioSampleFormatInterleaved(AudioSampleFormat);
size_t computeBytesPerSample(AudioSampleFormat);
ExceptionOr<size_t> computeCopyElementCount(const WebCodecsAudioData&, const WebCodecsAudioData::CopyToOptions&);
AudioSampleFormat audioSampleElementFormat(AudioSampleFormat);
using AudioSampleFormatSpan = Variant<std::span<uint8_t>, std::span<int16_t>, std::span<int32_t>, std::span<float>>;
AudioSampleFormatSpan audioElementSpan(AudioSampleFormat, std::span<uint8_t>);

} // WebCore

#endif
