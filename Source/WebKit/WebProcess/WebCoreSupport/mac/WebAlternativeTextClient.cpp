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

#include "config.h"
#include "WebAlternativeTextClient.h"

#include "MessageSenderInlines.h"
#include "WebPage.h"
#include "WebPageProxyMessages.h"

namespace WebKit {
using namespace WebCore;

WebAlternativeTextClient::WebAlternativeTextClient(WebPage* webPage)
    : m_page(webPage)
{
}

WebAlternativeTextClient::~WebAlternativeTextClient()
{
#if USE(AUTOCORRECTION_PANEL)
    if (m_page)
        m_page->send(Messages::WebPageProxy::DismissCorrectionPanel(ReasonForDismissingAlternativeText::Ignored));
#endif
}

#if USE(AUTOCORRECTION_PANEL)
void WebAlternativeTextClient::showCorrectionAlternative(AlternativeTextType type, const FloatRect& boundingBoxOfReplacedString, const String& replacedString, const String& replacementString, const Vector<String>& alternativeReplacementStrings, FrameIdentifier rootFrameID)
{
    m_page->send(Messages::WebPageProxy::ShowCorrectionPanel(type, boundingBoxOfReplacedString, replacedString, replacementString, alternativeReplacementStrings, rootFrameID));
}

void WebAlternativeTextClient::dismissAlternative(ReasonForDismissingAlternativeText reason)
{
    m_page->send(Messages::WebPageProxy::DismissCorrectionPanel(reason));
}

String WebAlternativeTextClient::dismissAlternativeSoon(ReasonForDismissingAlternativeText reason)
{
    auto sendResult = m_page->sendSync(Messages::WebPageProxy::DismissCorrectionPanelSoon(reason));
    auto [result] = sendResult.takeReplyOr(String { });
    return result;
}

void WebAlternativeTextClient::recordAutocorrectionResponse(AutocorrectionResponse response, const String& replacedString, const String& replacementString)
{
    m_page->send(Messages::WebPageProxy::RecordAutocorrectionResponse(response, replacedString, replacementString));
}
#endif

void WebAlternativeTextClient::removeDictationAlternatives(WebCore::DictationContext dictationContext)
{
    m_page->send(Messages::WebPageProxy::RemoveDictationAlternatives(dictationContext));
}

void WebAlternativeTextClient::showDictationAlternativeUI(const WebCore::FloatRect& boundingBoxOfDictatedText, WebCore::DictationContext dictationContext)
{
    m_page->send(Messages::WebPageProxy::ShowDictationAlternativeUI(boundingBoxOfDictatedText, dictationContext));
}

Vector<String> WebAlternativeTextClient::dictationAlternatives(WebCore::DictationContext dictationContext)
{
    auto sendResult = m_page->sendSync(Messages::WebPageProxy::DictationAlternatives(dictationContext));
    auto [result] = sendResult.takeReplyOr(Vector<String> { });
    return result;
}

}
