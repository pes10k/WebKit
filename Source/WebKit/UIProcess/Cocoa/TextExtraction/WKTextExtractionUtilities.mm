/*
 * Copyright (C) 2024 Apple Inc. All rights reserved.
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
#import "WKTextExtractionUtilities.h"

#if USE(APPLE_INTERNAL_SDK) || (!PLATFORM(WATCHOS) && !PLATFORM(APPLETV))

#import "WKTextExtractionItem.h"
#import <WebCore/TextExtraction.h>
#import <wtf/cocoa/VectorCocoa.h>

namespace WebKit {
using namespace WebCore;

inline static WKTextExtractionContainer containerType(TextExtraction::ContainerType type)
{
    switch (type) {
    case TextExtraction::ContainerType::Root:
        return WKTextExtractionContainerRoot;
    case TextExtraction::ContainerType::ViewportConstrained:
        return WKTextExtractionContainerViewportConstrained;
    case TextExtraction::ContainerType::List:
        return WKTextExtractionContainerList;
    case TextExtraction::ContainerType::ListItem:
        return WKTextExtractionContainerListItem;
    case TextExtraction::ContainerType::BlockQuote:
        return WKTextExtractionContainerBlockQuote;
    case TextExtraction::ContainerType::Article:
        return WKTextExtractionContainerArticle;
    case TextExtraction::ContainerType::Section:
        return WKTextExtractionContainerSection;
    case TextExtraction::ContainerType::Nav:
        return WKTextExtractionContainerNav;
    case TextExtraction::ContainerType::Button:
        return WKTextExtractionContainerButton;
    }
}

inline static RetainPtr<WKTextExtractionTextItem> createWKTextItem(const TextExtraction::TextItemData& data, CGRect rectInWebView, NSArray<WKTextExtractionItem *> *children)
{
    RetainPtr<WKTextExtractionEditable> editable;
    if (data.editable) {
        editable = adoptNS([[WKTextExtractionEditable alloc]
            initWithLabel:data.editable->label.createNSString().get()
            placeholder:data.editable->placeholder.createNSString().get()
            isSecure:static_cast<BOOL>(data.editable->isSecure)
            isFocused:static_cast<BOOL>(data.editable->isFocused)]);
    }

    auto selectedRange = NSMakeRange(NSNotFound, 0);
    if (auto range = data.selectedRange) {
        if (range->location + range->length <= data.content.length()) [[likely]]
            selectedRange = NSMakeRange(range->location, range->length);
    }

    auto links = createNSArray(data.links, [&](auto& linkAndRange) -> RetainPtr<WKTextExtractionLink> {
        auto& [url, range] = linkAndRange;
        if (range.location + range.length > data.content.length()) [[unlikely]]
            return { };

        RetainPtr nsURL = url.createNSURL();
        if (!nsURL)
            return { };

        return adoptNS([[WKTextExtractionLink alloc] initWithURL:nsURL.get() range:NSMakeRange(range.location, range.length)]);
    });

    return adoptNS([[WKTextExtractionTextItem alloc]
        initWithContent:data.content.createNSString().get()
        selectedRange:selectedRange
        links:links.get()
        editable:editable.get()
        rectInWebView:rectInWebView
        children:children]);
}

inline static RetainPtr<WKTextExtractionItem> createItemWithChildren(const TextExtraction::Item& item, const RootViewToWebViewConverter& converter, NSArray<WKTextExtractionItem *> *children)
{
    auto rectInWebView = converter(item.rectInRootView);
    return WTF::switchOn(item.data,
        [&](const TextExtraction::TextItemData& data) -> RetainPtr<WKTextExtractionItem> {
            return createWKTextItem(data, rectInWebView, children);
        }, [&](const TextExtraction::ScrollableItemData& data) -> RetainPtr<WKTextExtractionItem> {
            return adoptNS([[WKTextExtractionScrollableItem alloc] initWithContentSize:data.contentSize rectInWebView:rectInWebView children:children]);
        }, [&](const TextExtraction::ImageItemData& data) -> RetainPtr<WKTextExtractionItem> {
            return adoptNS([[WKTextExtractionImageItem alloc] initWithName:data.name.createNSString().get() altText:data.altText.createNSString().get() rectInWebView:rectInWebView children:children]);
        }, [&](TextExtraction::ContainerType type) -> RetainPtr<WKTextExtractionItem> {
            return adoptNS([[WKTextExtractionContainerItem alloc] initWithContainer:containerType(type) rectInWebView:rectInWebView children:children]);
        }
    );
}

static RetainPtr<WKTextExtractionItem> createItemRecursive(const TextExtraction::Item& item, const RootViewToWebViewConverter& converter)
{
    return createItemWithChildren(item, converter, createNSArray(item.children, [&](auto& child) {
        return createItemRecursive(child, converter);
    }).get());
}

RetainPtr<WKTextExtractionItem> createItem(const TextExtraction::Item& item, RootViewToWebViewConverter&& converter)
{
    if (!std::holds_alternative<TextExtraction::ContainerType>(item.data)) {
        ASSERT_NOT_REACHED();
        return nil;
    }

    if (std::get<TextExtraction::ContainerType>(item.data) != TextExtraction::ContainerType::Root) {
        ASSERT_NOT_REACHED();
        return nil;
    }

    return createItemRecursive(item, WTFMove(converter));
}

} // namespace WebKit

#endif // USE(APPLE_INTERNAL_SDK) || (!PLATFORM(WATCHOS) && !PLATFORM(APPLETV))
