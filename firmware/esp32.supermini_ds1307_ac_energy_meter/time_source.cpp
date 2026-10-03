#include "time_source.h"
#include "config.h"

#include <esp_timer.h>
#include <sys/time.h>

namespace time_source {

static bool s_known = false;

void begin() {
  setenv("TZ", TZ_INFO, 1);
  tzset();
}

uint64_t monotonic_us() {
  return (uint64_t)esp_timer_get_time();
}

time_t wall_time() {
  if (!s_known) return 0;
  time_t now;
  time(&now);
  return now;
}

bool wall_clock_known() {
  return s_known;
}

// Broken-down calendar fields -> epoch, treating them as UTC (a timegm()
// replacement; newlib has none). Pure arithmetic on purpose: the old approach
// flipped the process-wide TZ to "UTC0" around a mktime() call, and every other
// task calling localtime_r() during that window (the relay tick deciding open
// vs. off hours, the sampling task's day number) saw UTC instead of local time.
// days_from_civil() is Howard Hinnant's proleptic-Gregorian algorithm.
static int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}
static time_t utc_from_fields(int y, int mo, int d, int h, int mi, int se) {
  return (time_t)(days_from_civil(y, mo, d) * 86400LL + h * 3600LL + mi * 60LL + se);
}

bool set_wall_clock(time_t epoch) {
  if (epoch < 1700000000) return false;  // sanity: must be after 2023-11
  struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
  settimeofday(&tv, nullptr);
  s_known = true;
  return true;
}

// Parse "YYYY-MM-DDTHH:MM:SS" with optional "Z" or "+HH:MM"/"-HH:MM" suffix.
time_t parse_iso8601(const char *s) {
  if (!s) return 0;
  struct tm t = {};
  int y, mo, d, h, mi, se;
  if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
  t.tm_year = y - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = d;
  t.tm_hour = h;
  t.tm_min = mi;
  t.tm_sec = se;
  // timegm-style: interpret as UTC, then apply explicit offset if present.
  // Use mktime + adjust by current timezone offset to get a UTC epoch.
  time_t local_epoch = mktime(&t);  // mktime treats t as local time per TZ env
  if (local_epoch == (time_t)-1) return 0;

  // Find offset suffix.
  const char *p = s + 19;
  // skip fractional seconds if present
  if (*p == '.') {
    while (*p && *p != 'Z' && *p != '+' && *p != '-') ++p;
  }
  int off_sec = 0;
  bool have_offset = false;
  if (*p == 'Z') {
    off_sec = 0;
    have_offset = true;
  } else if (*p == '+' || *p == '-') {
    int sign = (*p == '+') ? 1 : -1;
    int oh = 0, om = 0;
    if (sscanf(p + 1, "%2d:%2d", &oh, &om) == 2) {
      off_sec = sign * (oh * 3600 + om * 60);
      have_offset = true;
    }
  }
  if (have_offset) {
    // The parsed Y-M-D H:M:S is the wall time in the named offset, so
    // UTC = wall - offset.
    return utc_from_fields(y, mo, d, h, mi, se) - off_sec;
  }
  return local_epoch;
}

String iso8601_now() {
  if (!s_known) return String();
  time_t now;
  time(&now);
  struct tm lt;
  localtime_r(&now, &lt);

  // Local wall components read back as if they were UTC, minus the true
  // epoch, is the local UTC offset (DST included).
  time_t as_utc = utc_from_fields(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                                  lt.tm_hour, lt.tm_min, lt.tm_sec);
  long off = (long)(as_utc - now);

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &lt);
  char tz[8];
  long abs_off = off < 0 ? -off : off;
  snprintf(tz, sizeof(tz), "%c%02ld:%02ld",
           off < 0 ? '-' : '+', abs_off / 3600, (abs_off % 3600) / 60);
  return String(buf) + tz;
}

uint32_t local_day_number() {
  if (!s_known) return 0;
  time_t now;
  time(&now);
  struct tm lt;
  localtime_r(&now, &lt);
  // Days since epoch in local time.
  // tm_yday + a year-base offset is enough for monotonic day comparison if we add tm_year * 366.
  return (uint32_t)((lt.tm_year + 1900) * 366 + lt.tm_yday);
}

}  // namespace time_source
