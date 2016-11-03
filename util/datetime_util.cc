// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <ctime>

#include "util/datetime_util.h"

namespace cobalt {
namespace util {

// The algorithm in this file was copied from
// http://howardhinnant.github.io/date_algorithms.html
// See that site for further explanation.

// Recall that a year is a leap year if it is a multiple of 4 that is not
// a multiple of 100 unless it is a multiple of 400. Thus the pattern of
// leap years is periodic with period 400 years.

// There are 146097 days in every 400 year era:
// 146097 = 365 * 400 + 100 - 3. Of the integers in the range [0, 399]
// there are 100 multiples of 4 but 3 of those are multiples of 100
// and not multiples of 400, namely 100, 200 and 300.
static const uint32_t kNumDaysPerEra = 146097;

// The algorithm below uses an epoch of March 1, year 0
// whereas our API uses an epoch of January 1, 1970.
// kEpochOffset is the number of days from 0-3-1 to 1970-1-1
// proof:
// The number of days from 0-3-1 to 2000-3-1 is 5*kNumDaysPerEra
// The number of days from 1970-3-1 to 2000-3-1 is 30*365 + 8
// because the following years were leap years: '72 '76 '80 '84 '88 '92 '96 2000
// The number of days from 1970-1-1 to 1970-3-1 is 59.
static const uint32_t kEpochOffset =
    kNumDaysPerEra * 5L - 30L * 365L - 8L - 59L;

uint32_t CalendarDateToDayIndex(const CalendarDate& calendar_date) {
  // This implementation was copied from
  // http://howardhinnant.github.io/date_algorithms.html#days_from_civil.
  // This same code is also used in the function DayOrdinal() in
  // Google's civil_time.cc.
  //
  // There is an ostensibly simpler but less portable solution to this
  // problem presented in CalendarDateToDayIndexAltImpl() in
  // datetime_util_test.cc.

  if (calendar_date.year < 1970 || calendar_date.year >= 10000 ||
    calendar_date.month < 1 || calendar_date.month > 12 ||
    calendar_date.day_of_month < 1 || calendar_date.day_of_month > 31) {
    return kInvalidDayIndex;
  }

  // This algorithm counts years as beginning on March 1. Convert to that now.
  // This trick has the advantage that a leap day is the last day of the year.
  const uint32_t year = calendar_date.year - (calendar_date.month <= 2 ? 1 : 0);

  // Which 400-year era is it?
  const uint32_t era = year / 400;

  // The year of the era is the year mod 400.
  const uint32_t yoe = year - era * 400;

  // Now we compute the day of the year, where March 1 is day number 1.
  // The main idea is the following trick which uses integer division to
  // capture the set of months that have 30 instead of 31 days. Let n be a month
  // number where n = 1 means March, n = 2 means April, etc. Notice that
  // For n = 1...10, using integer division,  we have that
  // (3n+2)/5 = 1, 1, 2, 2, 3, 4, 4, 5, 5, 6. It follows that the expression
  // (153*n + 2)/5 yields the number of days from March 1 through the end
  // of month number n. To see this note that (153*n + 2)/5 = 30n + (3n+2)/5.
  // Plugging in to the formula we have
  // n=1 (March)  -> 30 +   1
  // n=2 (April)  -> 30*2 + 1
  // n=3 (May)    -> 30*3 + 2
  // n=4 (June)   -> 30*4 + 2
  // n=5 (July)   -> 30*5 + 3
  // n=6 (August) -> 30*6 + 4
  // etc.
  const uint32_t doy =
      (153 * (calendar_date.month + (calendar_date.month > 2 ? -3 : 9)) + 2) / 5
      + calendar_date.day_of_month - 1;

  // Now we compute the day of the era. This is relatively easy using
  // the formula for leap years described at the top of this file.
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;


  // shift epoch from 0-03-01 to 1970-01-01
  return era * kNumDaysPerEra + doe - kEpochOffset;
}

CalendarDate DayIndexToCalendarDate(uint32_t day_index) {
  // This function is an inverse to CalendarDateToDayIndex.
  // But because gmtime_r is a standard function unlike timegm, we
  // use the more straightforward implementation in this direction.
  time_t unix_time = day_index * kNumUnixSecondsPerDay;
  struct tm time_info;
  gmtime_r(&unix_time, &time_info);
  CalendarDate calendar_date;
  calendar_date.day_of_month = time_info.tm_mday;
  calendar_date.month = time_info.tm_mon + 1;
  calendar_date.year = time_info.tm_year + 1900;
  return calendar_date;
}

}  // namespace util
}  // namespace cobalt

