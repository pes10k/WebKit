/*
 * Copyright (C) 2012, 2014 Patrick Gansterer <paroga@paroga.com>
 * Copyright (C) 2012 the V8 project authors. All rights reserved.
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
#include <wtf/GregorianDateTime.h>

#include <wtf/DateMath.h>

#if OS(WINDOWS)
#include <windows.h>
#else
#include <time.h>
#endif

namespace WTF {

GregorianDateTime::GregorianDateTime(double ms, LocalTimeOffset localTime)
{
    if (std::isfinite(ms)) {
        WTF::Int64Milliseconds timeClipped(static_cast<int64_t>(ms));
        int32_t days = msToDays(timeClipped);
        int32_t timeInDayMS = timeInDay(timeClipped, days);
        auto [year, month, day] = yearMonthDayFromDays(days);
        int32_t hour = timeInDayMS / (60 * 60 * 1000);
        int32_t minute = (timeInDayMS / (60 * 1000)) % 60;
        int32_t second = (timeInDayMS / 1000) % 60;
        setSecond(second);
        setMinute(minute);
        setHour(hour);
        setWeekDay(WTF::weekDay(days));
        setYearDay(dayInYear(year, month, day));
        setMonthDay(day);
        setMonth(month);
        setYear(year);
    }
    setIsDST(localTime.isDST);
    setUTCOffsetInMinute(localTime.offset / WTF::Int64Milliseconds::msPerMinute);
}

void GregorianDateTime::setToCurrentLocalTime()
{
#if OS(WINDOWS)
    SYSTEMTIME systemTime;
    GetLocalTime(&systemTime);
    TIME_ZONE_INFORMATION timeZoneInformation;
    DWORD timeZoneId = GetTimeZoneInformation(&timeZoneInformation);

    LONG bias = 0;
    if (timeZoneId != TIME_ZONE_ID_INVALID) {
        bias = timeZoneInformation.Bias;
        if (timeZoneId == TIME_ZONE_ID_DAYLIGHT)
            bias += timeZoneInformation.DaylightBias;
        else if ((timeZoneId == TIME_ZONE_ID_STANDARD) || (timeZoneId == TIME_ZONE_ID_UNKNOWN))
            bias += timeZoneInformation.StandardBias;
        else
            ASSERT(0);
    }

    m_year = systemTime.wYear;
    m_month = systemTime.wMonth - 1;
    m_monthDay = systemTime.wDay;
    m_yearDay = dayInYear(m_year, m_month, m_monthDay);
    m_weekDay = Weekday { systemTime.wDayOfWeek };
    m_hour = systemTime.wHour;
    m_minute = systemTime.wMinute;
    m_second = systemTime.wSecond;
    m_utcOffsetInMinute = -bias;
    m_isDST = timeZoneId == TIME_ZONE_ID_DAYLIGHT ? 1 : 0;
#else
    tm localTM;
    time_t localTime = time(0);
#if HAVE(LOCALTIME_R)
    localtime_r(&localTime, &localTM);
#else
    localtime_s(&localTime, &localTM);
#endif

    m_year = localTM.tm_year + 1900;
    m_month = localTM.tm_mon;
    m_monthDay = localTM.tm_mday;
    m_yearDay = localTM.tm_yday;
    m_weekDay = Weekday { unsignedCast(localTM.tm_wday) };
    m_hour = localTM.tm_hour;
    m_minute = localTM.tm_min;
    m_second = localTM.tm_sec;
    m_isDST = localTM.tm_isdst;
#if HAVE(TM_GMTOFF)
    m_utcOffsetInMinute = localTM.tm_gmtoff / secondsPerMinute;
#else
    m_utcOffsetInMinute = calculateLocalTimeOffset(localTime * msPerSecond).offset / msPerMinute;
#endif
#endif
}

} // namespace WTF
