// SPDX-License-Identifier: BSD-3-Clause
// Neko std/date.c subset — epoch seconds as I32, strftime formatting on Bytes.

#include <jello/jdll.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

static int jdll_gmtime_r(const time_t* sec, struct tm* out) {
#if defined(_WIN32)
  return gmtime_s(out, sec);
#else
  return gmtime_r(sec, out) == NULL ? -1 : 0;
#endif
}

static int jdll_localtime_r(const time_t* sec, struct tm* out) {
#if defined(_WIN32)
  return localtime_s(out, sec);
#else
  return localtime_r(sec, out) == NULL ? -1 : 0;
#endif
}

static const char* DEFAULT_FMT = "%Y-%m-%d %H:%M:%S";

static int bytes_to_cstr(const uint8_t* data, uint32_t len, char* out, size_t cap) {
  if(!out || cap < 2) return 0;
  if(!data || !len) {
    out[0] = 0;
    return 1;
  }
  uint32_t n = len < cap - 1u ? len : (uint32_t)(cap - 1u);
  memcpy(out, data, n);
  out[n] = 0;
  return 1;
}

static int32_t parse_date_new(const char* s) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_isdst = -1;
  size_t n = strlen(s);
  if(n == 0) return (int32_t)time(NULL);
  if(n == 19) {
    if(sscanf(s, "%4d-%2d-%2d %2d:%2d:%2d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min,
             &t.tm_sec) != 6) {
      return -1;
    }
    t.tm_year -= 1900;
    t.tm_mon--;
    return (int32_t)mktime(&t);
  }
  if(n == 10) {
    if(sscanf(s, "%4d-%2d-%2d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3) return -1;
    t.tm_year -= 1900;
    t.tm_mon--;
    return (int32_t)mktime(&t);
  }
  if(n == 8) {
    int h = 0, m = 0, sec = 0;
    if(sscanf(s, "%2d:%2d:%2d", &h, &m, &sec) != 3) return -1;
    return (int32_t)(sec + m * 60 + h * 3600);
  }
  return -1;
}

static void format_time(jdlo_ctx* c, time_t d, const char* fmt, int use_utc) {
  char buf[128];
  struct tm t;
  if(use_utc) {
    if(jdll_gmtime_r(&d, &t) != 0) {
      jdl_fail(c, "date_utc_format: invalid time");
      return;
    }
  } else if(jdll_localtime_r(&d, &t) != 0) {
    jdl_fail(c, "date_format: invalid time");
    return;
  }
  if(strftime(buf, sizeof buf, fmt, &t) == 0) {
    jdl_fail(c, "date_format: strftime failed");
    return;
  }
  jdl_return_bytes_copy(c, (const uint8_t*)buf, (uint32_t)strlen(buf));
}

void jdll_std_date_now(jdlo_ctx* c) {
  jdl_return_i32(c, (int32_t)time(NULL));
}

void jdll_std_date_new(jdlo_ctx* c) {
  char s[64];
  if(!bytes_to_cstr(jdl_arg_bytes_data(c, 0), jdl_arg_bytes_len(c, 0), s, sizeof s)) {
    jdl_fail(c, "date_new: bad path buffer");
    return;
  }
  int32_t t = parse_date_new(s);
  if(t < 0) {
    jdl_fail(c, "date_new: invalid date format");
    return;
  }
  jdl_return_i32(c, t);
}

void jdll_std_date_format(jdlo_ctx* c) {
  int32_t secs = jdl_arg_i32(c, 0);
  const uint8_t* fmtb = jdl_arg_bytes_data(c, 1);
  uint32_t fmtlen = jdl_arg_bytes_len(c, 1);
  char fmt[128];
  const char* fmtstr = DEFAULT_FMT;
  if(fmtb && fmtlen) {
    if(!bytes_to_cstr(fmtb, fmtlen, fmt, sizeof fmt)) {
      jdl_fail(c, "date_format: format too long");
      return;
    }
    fmtstr = fmt;
  }
  format_time(c, (time_t)secs, fmtstr, 0);
}

void jdll_std_date_utc_format(jdlo_ctx* c) {
  int32_t secs = jdl_arg_i32(c, 0);
  const uint8_t* fmtb = jdl_arg_bytes_data(c, 1);
  uint32_t fmtlen = jdl_arg_bytes_len(c, 1);
  char fmt[128];
  const char* fmtstr = DEFAULT_FMT;
  if(fmtb && fmtlen) {
    if(!bytes_to_cstr(fmtb, fmtlen, fmt, sizeof fmt)) {
      jdl_fail(c, "date_utc_format: format too long");
      return;
    }
    fmtstr = fmt;
  }
  format_time(c, (time_t)secs, fmtstr, 1);
}

void jdll_std_date_get_tz(jdlo_ctx* c) {
  int32_t secs = jdl_arg_i32(c, 0);
  time_t raw = (time_t)secs;
  struct tm local;
  struct tm gmt;
  if(jdll_localtime_r(&raw, &local) != 0 || jdll_gmtime_r(&raw, &gmt) != 0) {
    jdl_fail(c, "date_get_tz: invalid time");
    return;
  }
  int diff = (local.tm_hour - gmt.tm_hour) * 60 + (local.tm_min - gmt.tm_min);
  if(gmt.tm_year > local.tm_year || gmt.tm_yday > local.tm_yday) diff -= 24 * 60;
  else if(gmt.tm_year < local.tm_year || gmt.tm_yday < local.tm_yday) diff += 24 * 60;
  jdl_return_i32(c, (int32_t)diff);
}

void jdll_std_date_set_hour(jdlo_ctx* c) {
  int32_t secs = jdl_arg_i32(c, 0);
  int h = jdl_arg_i32(c, 1);
  int m = jdl_arg_i32(c, 2);
  int s = jdl_arg_i32(c, 3);
  time_t d = (time_t)secs;
  struct tm t;
  if(jdll_localtime_r(&d, &t) != 0) {
    jdl_fail(c, "date_set_hour: invalid time");
    return;
  }
  t.tm_hour = h;
  t.tm_min = m;
  t.tm_sec = s;
  d = mktime(&t);
  if(d == (time_t)-1) {
    jdl_fail(c, "date_set_hour: mktime failed");
    return;
  }
  jdl_return_i32(c, (int32_t)d);
}

void jdll_std_date_set_day(jdlo_ctx* c) {
  int32_t secs = jdl_arg_i32(c, 0);
  int y = jdl_arg_i32(c, 1);
  int m = jdl_arg_i32(c, 2);
  int dday = jdl_arg_i32(c, 3);
  time_t d = (time_t)secs;
  struct tm t;
  if(jdll_localtime_r(&d, &t) != 0) {
    jdl_fail(c, "date_set_day: invalid time");
    return;
  }
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = dday;
  d = mktime(&t);
  if(d == (time_t)-1) {
    jdl_fail(c, "date_set_day: mktime failed");
    return;
  }
  jdl_return_i32(c, (int32_t)d);
}
