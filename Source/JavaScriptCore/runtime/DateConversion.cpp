/*
 * Copyright (C) 2012 Patrick Gansterer <paroga@paroga.com>
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
#include "DateConversion.h"

#include "JSDateMath.h"
#include <wtf/Assertions.h>
#include <wtf/DateMath.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/WTFString.h>

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace JSC {

template<int width>
static inline void appendNumber(StringBuilder& builder, int value)
{
    if (value < 0) {
        builder.append('-');
        value = -value;
    }
    String valueString = String::number(value);
    int fillingZerosCount = width - valueString.length();
    for (int i = 0; i < fillingZerosCount; ++i)
        builder.append('0');
    builder.append(valueString);
}

template<>
void appendNumber<2>(StringBuilder& builder, int value)
{
    ASSERT(0 <= value && value <= 99);
    builder.append(static_cast<char>('0' + value / 10));
    builder.append(static_cast<char>('0' + value % 10));
}

String formatDateTime(const GregorianDateTime& t, DateTimeFormat format, bool asUTCVariant, DateCache& dateCache)
{
    bool appendDate = format & DateTimeFormatDate;
    bool appendTime = format & DateTimeFormatTime;

    StringBuilder builder;

    if (appendDate) {
        builder.append(WTF::weekdayName(t.weekDay()));

        if (asUTCVariant) {
            builder.append(", "_s);
            appendNumber<2>(builder, t.monthDay());
            builder.append(' ', WTF::monthName[t.month()]);
        } else {
            builder.append(' ', WTF::monthName[t.month()], ' ');
            appendNumber<2>(builder, t.monthDay());
        }
        builder.append(' ');
        appendNumber<4>(builder, t.year());
    }

    if (appendDate && appendTime)
        builder.append(' ');

    if (appendTime) {
        appendNumber<2>(builder, t.hour());
        builder.append(':');
        appendNumber<2>(builder, t.minute());
        builder.append(':');
        appendNumber<2>(builder, t.second());
        builder.append(" GMT"_s);

        if (!asUTCVariant) {
            int offset = std::abs(t.utcOffsetInMinute());
            builder.append(t.utcOffsetInMinute() < 0 ? '-' : '+');
            appendNumber<2>(builder, offset / 60);
            appendNumber<2>(builder, offset % 60);
            String timeZoneName = dateCache.timeZoneDisplayName(t.isDST());
            if (!timeZoneName.isEmpty())
                builder.append(" ("_s, timeZoneName, ')');
        }
    }

    return builder.toString().impl();
}

} // namespace JSC

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
