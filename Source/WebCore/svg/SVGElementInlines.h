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

#pragma once

#include "ElementInlines.h"
#include "SVGAnimatedString.h"
#include "SVGElement.h"
#include "SVGPropertyRegistry.h"

namespace WebCore {

inline void SVGElement::detachAllProperties()
{
    propertyRegistry().detachAllProperties();
}

inline void SVGElement::setAnimatedSVGAttributesAreDirty()
{
    ensureUniqueElementData().setAnimatedSVGAttributesAreDirty(true);
}

inline void SVGElement::setPresentationalHintStyleIsDirty()
{
    ensureUniqueElementData().setPresentationalHintStyleIsDirty(true);
    invalidateStyle();
}

inline AtomString SVGElement::className() const
{
    return AtomString { m_className->currentValue() };
}

inline bool Element::hasTagName(const SVGQualifiedName& tagName) const
{
    return ContainerNode::hasTagName(tagName);
}

inline bool Node::hasTagName(const SVGQualifiedName& name) const
{
    auto* svgElement = dynamicDowncast<SVGElement>(*this);
    return svgElement && svgElement->hasTagName(name);
}

}
