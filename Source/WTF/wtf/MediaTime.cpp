/*
 * Copyright (C) 2012-2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include <wtf/MediaTime.h>

#include <algorithm>
#include <cstdlib>
#include <wtf/Assertions.h>
#include <wtf/CheckedArithmetic.h>
#include <wtf/Int128.h>
#include <wtf/JSONValues.h>
#include <wtf/MathExtras.h>
#include <wtf/PrintStream.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/TextStream.h>

namespace WTF {

static_assert(std::is_trivially_destructible_v<MediaTime>, "MediaTime should be trivially destructible.");

static uint32_t greatestCommonDivisor(uint32_t a, uint32_t b)
{
    ASSERT(a);
    ASSERT(b);

    if (a == b)
        return a;

    // Euclid's Algorithm
    while (b)
        b = std::exchange(a, b) % b;

    ASSERT(a);
    return a;
}

static bool leastCommonMultiple(uint32_t a, uint32_t b, uint32_t& result)
{
    if (a == b) {
        result = a;
        return true;
    }
    return safeMultiply(a, b / greatestCommonDivisor(a, b), result);
}

static int64_t signum(int64_t val)
{
    return (0 < val) - (val < 0);
}

const uint32_t MediaTime::MaximumTimeScale = 1000000000;

MediaTime MediaTime::createWithFloat(float floatTime)
{
    if (floatTime != floatTime)
        return invalidTime();
    if (std::isinf(floatTime))
        return std::signbit(floatTime) ? negativeInfiniteTime() : positiveInfiniteTime();

    MediaTime value(0, DefaultTimeScale, Valid | DoubleValue);
    value.m_timeValueAsDouble = floatTime;
    return value;
}

MediaTime MediaTime::createWithFloat(float floatTime, uint32_t timeScale)
{
    if (floatTime != floatTime)
        return invalidTime();
    if (std::isinf(floatTime))
        return std::signbit(floatTime) ? negativeInfiniteTime() : positiveInfiniteTime();
    if (floatTime >= maxPlusOne<int64_t>)
        return positiveInfiniteTime();
    if (floatTime < std::numeric_limits<int64_t>::min())
        return negativeInfiniteTime();
    if (!timeScale)
        return std::signbit(floatTime) ? negativeInfiniteTime() : positiveInfiniteTime();

    while (floatTime * timeScale >= maxPlusOne<int64_t>)
        timeScale /= 2;
    return MediaTime(static_cast<int64_t>(floatTime * timeScale), timeScale, Valid);
}

MediaTime MediaTime::createWithDouble(double doubleTime)
{
    if (doubleTime != doubleTime)
        return invalidTime();
    if (std::isinf(doubleTime))
        return std::signbit(doubleTime) ? negativeInfiniteTime() : positiveInfiniteTime();

    MediaTime value(0, DefaultTimeScale, Valid | DoubleValue);
    value.m_timeValueAsDouble = doubleTime;
    return value;
}

MediaTime MediaTime::createWithDouble(double doubleTime, uint32_t timeScale)
{
    if (doubleTime != doubleTime)
        return invalidTime();
    if (std::isinf(doubleTime))
        return std::signbit(doubleTime) ? negativeInfiniteTime() : positiveInfiniteTime();
    if (doubleTime >= maxPlusOne<int64_t>)
        return positiveInfiniteTime();
    if (doubleTime < std::numeric_limits<int64_t>::min())
        return negativeInfiniteTime();
    if (!timeScale)
        return std::signbit(doubleTime) ? negativeInfiniteTime() : positiveInfiniteTime();

    while (doubleTime * timeScale >= maxPlusOne<int64_t>)
        timeScale /= 2;
    return MediaTime(static_cast<int64_t>(std::round(doubleTime * timeScale)), timeScale, Valid);
}

float MediaTime::toFloat() const
{
    if (isInvalid() || isIndefinite())
        return std::numeric_limits<float>::quiet_NaN();
    if (isPositiveInfinite())
        return std::numeric_limits<float>::infinity();
    if (isNegativeInfinite())
        return -std::numeric_limits<float>::infinity();
    if (hasDoubleValue())
        return m_timeValueAsDouble;
    return static_cast<float>(m_timeValue) / m_timeScale;
}

double MediaTime::toDouble() const
{
    if (isInvalid() || isIndefinite())
        return std::numeric_limits<double>::quiet_NaN();
    if (isPositiveInfinite())
        return std::numeric_limits<double>::infinity();
    if (isNegativeInfinite())
        return -std::numeric_limits<double>::infinity();
    if (hasDoubleValue())
        return m_timeValueAsDouble;
    return static_cast<double>(m_timeValue) / m_timeScale;
}

int64_t MediaTime::toMicroseconds() const
{
    if (isInvalid() || isIndefinite())
        return std::numeric_limits<int64_t>::quiet_NaN();
    if (isPositiveInfinite())
        return std::numeric_limits<int64_t>::max();
    if (isNegativeInfinite())
        return std::numeric_limits<int64_t>::min();
    if (hasDoubleValue())
        return m_timeValueAsDouble * 1000000.0;
    auto result = CheckedInt64(m_timeValue / m_timeScale) * 1000000LL + CheckedInt64(m_timeValue % static_cast<int64_t>(m_timeScale) * 1000000LL) / static_cast<int64_t>(m_timeScale);
    if (result.hasOverflowed())
        return m_timeValue < 0 ? std::numeric_limits<int64_t>::min() : std::numeric_limits<int64_t>::max();
    return result.value();
}

MediaTime MediaTime::operator+(const MediaTime& rhs) const
{
    if (rhs.isInvalid() || isInvalid())
        return invalidTime();

    if (rhs.isIndefinite() || isIndefinite())
        return indefiniteTime();

    if (isPositiveInfinite() && rhs.isNegativeInfinite())
        return invalidTime();

    if (isNegativeInfinite() && rhs.isPositiveInfinite())
        return invalidTime();

    if (isPositiveInfinite() || rhs.isPositiveInfinite())
        return positiveInfiniteTime();

    if (isNegativeInfinite() || rhs.isNegativeInfinite())
        return negativeInfiniteTime();

    if (hasDoubleValue() && rhs.hasDoubleValue())
        return MediaTime::createWithDouble(m_timeValueAsDouble + rhs.m_timeValueAsDouble);

    if (hasDoubleValue() || rhs.hasDoubleValue())
        return MediaTime::createWithDouble(toDouble() + rhs.toDouble());

    MediaTime a = *this;
    MediaTime b = rhs;

    uint32_t commonTimeScale;
    if (!leastCommonMultiple(a.m_timeScale, b.m_timeScale, commonTimeScale) || commonTimeScale > MaximumTimeScale)
        commonTimeScale = MaximumTimeScale;
    a.setTimeScale(commonTimeScale);
    b.setTimeScale(commonTimeScale);

    while (!safeAdd(a.m_timeValue, b.m_timeValue, a.m_timeValue)) {
        if (commonTimeScale == 1)
            return a.m_timeValue > 0 ? positiveInfiniteTime() : negativeInfiniteTime();
        commonTimeScale /= 2;
        a.setTimeScale(commonTimeScale);
        b.setTimeScale(commonTimeScale);
    }
    return a;
}

MediaTime MediaTime::operator-(const MediaTime& rhs) const
{
    if (rhs.isInvalid() || isInvalid())
        return invalidTime();

    if (rhs.isIndefinite() || isIndefinite())
        return indefiniteTime();

    if (isPositiveInfinite() && rhs.isPositiveInfinite())
        return invalidTime();

    if (isNegativeInfinite() && rhs.isNegativeInfinite())
        return invalidTime();

    if (isPositiveInfinite() || rhs.isNegativeInfinite())
        return positiveInfiniteTime();

    if (isNegativeInfinite() || rhs.isPositiveInfinite())
        return negativeInfiniteTime();

    if (hasDoubleValue() && rhs.hasDoubleValue())
        return MediaTime::createWithDouble(m_timeValueAsDouble - rhs.m_timeValueAsDouble);

    if (hasDoubleValue() || rhs.hasDoubleValue())
        return MediaTime::createWithDouble(toDouble() - rhs.toDouble());

    MediaTime a = *this;
    MediaTime b = rhs;

    uint32_t commonTimeScale;
    if (!leastCommonMultiple(this->m_timeScale, rhs.m_timeScale, commonTimeScale) || commonTimeScale > MaximumTimeScale)
        commonTimeScale = MaximumTimeScale;
    a.setTimeScale(commonTimeScale);
    b.setTimeScale(commonTimeScale);

    while (!safeSub(a.m_timeValue, b.m_timeValue, a.m_timeValue)) {
        if (commonTimeScale == 1)
            return a.m_timeValue > 0 ? positiveInfiniteTime() : negativeInfiniteTime();
        commonTimeScale /= 2;
        a.setTimeScale(commonTimeScale);
        b.setTimeScale(commonTimeScale);
    }
    return a;
}

MediaTime MediaTime::operator-() const
{
    if (isInvalid())
        return invalidTime();

    if (isIndefinite())
        return indefiniteTime();

    if (isPositiveInfinite())
        return negativeInfiniteTime();

    if (isNegativeInfinite())
        return positiveInfiniteTime();

    MediaTime negativeTime = *this;
    if (negativeTime.hasDoubleValue())
        negativeTime.m_timeValueAsDouble = -negativeTime.m_timeValueAsDouble;
    else
        negativeTime.m_timeValue = -negativeTime.m_timeValue;
    return negativeTime;
}

MediaTime MediaTime::operator*(int32_t rhs) const
{
    if (isInvalid())
        return invalidTime();

    if (isIndefinite())
        return indefiniteTime();

    if (!rhs)
        return zeroTime();

    if (isPositiveInfinite()) {
        if (rhs > 0)
            return positiveInfiniteTime();
        return negativeInfiniteTime();
    }

    if (isNegativeInfinite()) {
        if (rhs > 0)
            return negativeInfiniteTime();
        return positiveInfiniteTime();
    }

    if (hasDoubleValue())
        return MediaTime::createWithDouble(m_timeValueAsDouble * rhs);

    MediaTime a = *this;
    while (!safeMultiply(a.m_timeValue, rhs, a.m_timeValue)) {
        if (a.m_timeScale == 1)
            return signum(a.m_timeValue) == signum(rhs) ? positiveInfiniteTime() : negativeInfiniteTime();
        a.setTimeScale(a.m_timeScale / 2);
    }

    return a;
}

bool MediaTime::operator!() const
{
    return (m_timeFlags == Valid && !m_timeValue)
        || (m_timeFlags == (Valid | DoubleValue) && !m_timeValueAsDouble)
        || isInvalid();
}

MediaTime::operator bool() const
{
    return !(m_timeFlags == Valid && !m_timeValue)
        && !(m_timeFlags == (Valid | DoubleValue) && !m_timeValueAsDouble)
        && !isInvalid();
}

std::weak_ordering operator<=>(const MediaTime& a, const MediaTime& b)
{
    auto andFlags = a.m_timeFlags & b.m_timeFlags;
    if (andFlags & (MediaTime::PositiveInfinite | MediaTime::NegativeInfinite | MediaTime::Indefinite))
        return std::weak_ordering::equivalent;

    auto orFlags = a.m_timeFlags | b.m_timeFlags;
    if (!(orFlags & MediaTime::Valid))
        return std::weak_ordering::equivalent;

    if (!(andFlags & MediaTime::Valid))
        return a.isInvalid() ? std::weak_ordering::greater : std::weak_ordering::less;

    if (orFlags & MediaTime::NegativeInfinite)
        return a.isNegativeInfinite() ? std::weak_ordering::less : std::weak_ordering::greater;

    if (orFlags & MediaTime::PositiveInfinite)
        return a.isPositiveInfinite() ? std::weak_ordering::greater : std::weak_ordering::less;

    if (orFlags & MediaTime::Indefinite)
        return a.isIndefinite() ? std::weak_ordering::greater : std::weak_ordering::less;

    if (andFlags & MediaTime::DoubleValue)
        return weakOrderingCast(a.m_timeValueAsDouble <=> b.m_timeValueAsDouble);

    if (orFlags & MediaTime::DoubleValue)
        return weakOrderingCast(a.toDouble() <=> b.toDouble());

    if ((a.m_timeValue < 0) != (b.m_timeValue < 0))
        return a.m_timeValue < 0 ? std::weak_ordering::less : std::weak_ordering::greater;

    if (!a.m_timeValue && !b.m_timeValue)
        return std::weak_ordering::equivalent;

    if (a.m_timeScale == b.m_timeScale)
        return a.m_timeValue <=> b.m_timeValue;

    if (a.m_timeValue == b.m_timeValue)
        return b.m_timeScale <=> a.m_timeScale;

    if (a.m_timeValue >= 0) {
        if (a.m_timeValue < b.m_timeValue && a.m_timeScale > b.m_timeScale)
            return std::weak_ordering::less;

        if (a.m_timeValue > b.m_timeValue && a.m_timeScale < b.m_timeScale)
            return std::weak_ordering::greater;
    } else {
        if (a.m_timeValue < b.m_timeValue && a.m_timeScale < b.m_timeScale)
            return std::weak_ordering::less;

        if (a.m_timeValue > b.m_timeValue && a.m_timeScale > b.m_timeScale)
            return std::weak_ordering::greater;
    }

    int64_t aFactor;
    int64_t bFactor;
    if (safeMultiply(a.m_timeValue, static_cast<int64_t>(b.m_timeScale), aFactor) && safeMultiply(b.m_timeValue, static_cast<int64_t>(a.m_timeScale), bFactor))
        return aFactor <=> bFactor;

    int64_t bWhole = b.m_timeValue / b.m_timeScale;
    int64_t aWhole = a.m_timeValue / a.m_timeScale;
    if (auto result = aWhole <=> bWhole; is_neq(result))
        return result;

    int64_t bRemain = b.m_timeValue % b.m_timeScale;
    int64_t aRemain = a.m_timeValue % a.m_timeScale;
    aFactor = aRemain * b.m_timeScale;
    bFactor = bRemain * a.m_timeScale;

    return aFactor <=> bFactor;
}

bool MediaTime::isBetween(const MediaTime& a, const MediaTime& b) const
{
    if (a > b)
        return *this > b && *this < a;
    return *this > a && *this < b;
}

const MediaTime& MediaTime::zeroTime()
{
    static const MediaTime time(0, 1, Valid);
    return time;
}

const MediaTime& MediaTime::invalidTime()
{
    static const MediaTime time(-1, 1, 0);
    return time;
}

const MediaTime& MediaTime::positiveInfiniteTime()
{
    static const MediaTime time(0, 1, PositiveInfinite | Valid);
    return time;
}

const MediaTime& MediaTime::negativeInfiniteTime()
{
    static const MediaTime time(-1, 1, NegativeInfinite | Valid);
    return time;
}

const MediaTime& MediaTime::indefiniteTime()
{
    static const MediaTime time(0, 1, Indefinite | Valid);
    return time;
}

MediaTime MediaTime::toTimeScale(uint32_t timeScale, RoundingFlags flags) const
{
    MediaTime result = *this;
    result.setTimeScale(timeScale, flags);
    return result;
}

void MediaTime::setTimeScale(uint32_t timeScale, RoundingFlags flags)
{
    if (hasDoubleValue()) {
        *this = MediaTime::createWithDouble(m_timeValueAsDouble, timeScale);
        return;
    }

    if (!timeScale) {
        *this = m_timeValue < 0 ? negativeInfiniteTime() : positiveInfiniteTime();
        return;
    }

    if (timeScale == m_timeScale)
        return;

    timeScale = std::min(MaximumTimeScale, timeScale);

    Int128 newValue = static_cast<Int128>(m_timeValue) * timeScale;
    int64_t remainder = static_cast<int64_t>(newValue % m_timeScale);
    newValue = newValue / m_timeScale;

    if (newValue < std::numeric_limits<int64_t>::min()) {
        *this = negativeInfiniteTime();
        return;
    }

    if (newValue > std::numeric_limits<int64_t>::max()) {
        *this = positiveInfiniteTime();
        return;
    }

    m_timeValue = static_cast<int64_t>(newValue);
    std::swap(m_timeScale, timeScale);

    if (!remainder)
        return;

    m_timeFlags |= HasBeenRounded;
    switch (flags) {
    case RoundingFlags::HalfAwayFromZero:
        if (static_cast<int64_t>(llabs(remainder)) * 2 >= static_cast<int64_t>(timeScale)) {
            // round up (away from zero)
            if (remainder < 0)
                m_timeValue--;
            else
                m_timeValue++;
        }
        break;

    case RoundingFlags::TowardZero:
        break;

    case RoundingFlags::AwayFromZero:
        if (remainder < 0)
            m_timeValue--;
        else
            m_timeValue++;
        break;

    case RoundingFlags::TowardPositiveInfinity:
        if (remainder > 0)
            m_timeValue++;
        break;
        
    case RoundingFlags::TowardNegativeInfinity:
        if (remainder < 0)
            m_timeValue--;
        break;
    }
}

void MediaTime::dump(PrintStream& out) const
{
    out.print("{");
    if (!hasDoubleValue())
        out.print(m_timeValue, "/", m_timeScale, " = ");
    out.print(toDouble(), "}");
}

String MediaTime::toString() const
{
    auto invalid = isInvalid() ? ", invalid"_s : ""_s;
    if (hasDoubleValue())
        return makeString('{', toDouble(), invalid, '}');
    return makeString('{', m_timeValue, '/', m_timeScale, " = "_s, toDouble(), invalid, '}');
}

Ref<JSON::Object> MediaTime::toJSONObject() const
{
    auto object = JSON::Object::create();

    if (hasDoubleValue()) {
        object->setDouble("value"_s, toDouble());
        return object;
    }

    if (isInvalid())
        object->setBoolean("invalid"_s, true);
    else if (isIndefinite())
        object->setString("value"_s, "NaN"_s);
    else if (isPositiveInfinite())
        object->setString("value"_s, "POSITIVE_INFINITY"_s);
    else if (isNegativeInfinite())
        object->setString("value"_s, "NEGATIVE_INFINITY"_s);
    else
        object->setDouble("value"_s, toDouble());

    object->setDouble("numerator"_s, static_cast<double>(m_timeValue));
    object->setInteger("denominator"_s, m_timeScale);
    object->setInteger("flags"_s, m_timeFlags);

    return object;
}

String MediaTime::toJSONString() const
{
    return toJSONObject()->toJSONString();
}

MediaTime abs(const MediaTime& rhs)
{
    if (rhs.isInvalid())
        return MediaTime::invalidTime();
    if (rhs.isNegativeInfinite() || rhs.isPositiveInfinite())
        return MediaTime::positiveInfiniteTime();
    if (rhs.hasDoubleValue())
        return MediaTime::createWithDouble(std::abs(rhs.m_timeValueAsDouble));

    MediaTime val = rhs;
    val.m_timeValue = std::abs(rhs.m_timeValue);
    return val;
}

String MediaTimeRange::toJSONString() const
{
    auto object = JSON::Object::create();

    object->setObject("start"_s, start.toJSONObject());
    object->setObject("end"_s, end.toJSONObject());

    return object->toJSONString();
}

TextStream& operator<<(TextStream& stream, const MediaTime& time)
{
    return stream << time.toJSONString();
}

MediaTime MediaTime::isolatedCopy() const
{
    return *this;
}

}
