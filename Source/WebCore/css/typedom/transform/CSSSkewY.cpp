/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER “AS IS” AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"
#include "CSSSkewY.h"

#include "CSSFunctionValue.h"
#include "CSSNumericFactory.h"
#include "CSSNumericValue.h"
#include "CSSStyleValueFactory.h"
#include "DOMMatrix.h"
#include "ExceptionOr.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(CSSSkewY);

ExceptionOr<Ref<CSSSkewY>> CSSSkewY::create(Ref<CSSNumericValue> ay)
{
    if (!ay->type().matches<CSSNumericBaseType::Angle>())
        return Exception { ExceptionCode::TypeError };
    return adoptRef(*new CSSSkewY(WTFMove(ay)));
}

ExceptionOr<Ref<CSSSkewY>> CSSSkewY::create(Ref<const CSSFunctionValue> cssFunctionValue, Document& document)
{
    if (cssFunctionValue->name() != CSSValueSkewY) {
        ASSERT_NOT_REACHED();
        return CSSSkewY::create(Ref<CSSNumericValue>(CSSNumericFactory::deg(0)));
    }

    if (cssFunctionValue->size() != 1 || !cssFunctionValue->item(0)) {
        ASSERT_NOT_REACHED();
        return Exception { ExceptionCode::TypeError, "Unexpected number of values."_s };
    }

    auto valueOrException = CSSStyleValueFactory::reifyValue(document, *cssFunctionValue->item(0), std::nullopt);
    if (valueOrException.hasException())
        return valueOrException.releaseException();
    RefPtr numericValue = dynamicDowncast<CSSNumericValue>(valueOrException.releaseReturnValue());
    if (!numericValue)
        return Exception { ExceptionCode::TypeError, "Expected a CSSNumericValue."_s };
    return CSSSkewY::create(numericValue.releaseNonNull());
}

CSSSkewY::CSSSkewY(Ref<CSSNumericValue> ay)
    : CSSTransformComponent(Is2D::Yes)
    , m_ay(WTFMove(ay))
{
}

ExceptionOr<void> CSSSkewY::setAy(Ref<CSSNumericValue> ay)
{
    if (!ay->type().matches<CSSNumericBaseType::Angle>())
        return Exception { ExceptionCode::TypeError };

    m_ay = WTFMove(ay);
    return { };
}

void CSSSkewY::serialize(StringBuilder& builder) const
{
    // https://drafts.css-houdini.org/css-typed-om/#serialize-a-cssskewy
    builder.append("skewY("_s);
    m_ay->serialize(builder);
    builder.append(')');
}

ExceptionOr<Ref<DOMMatrix>> CSSSkewY::toMatrix()
{
    RefPtr ay = dynamicDowncast<CSSUnitValue>(m_ay);
    if (!ay)
        return Exception { ExceptionCode::TypeError };

    auto y = ay->convertTo(CSSUnitType::CSS_DEG);
    if (!y)
        return Exception { ExceptionCode::TypeError };

    TransformationMatrix matrix { };
    matrix.skewY(y->value());

    return { DOMMatrix::create(WTFMove(matrix), DOMMatrixReadOnly::Is2D::Yes) };
}

RefPtr<CSSValue> CSSSkewY::toCSSValue() const
{
    auto ay = m_ay->toCSSValue();
    if (!ay)
        return nullptr;
    CSSValueListBuilder arguments;
    arguments.append(ay.releaseNonNull());
    return CSSFunctionValue::create(CSSValueSkewY, WTFMove(arguments));
}

} // namespace WebCore
