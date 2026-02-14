/** favia: utils
2025, Simon Zolin */

#pragma once

static inline const char* time_print(uint64_t msec, char *buf, size_t cap)
{
	uint h = msec / 3600000,  m = (msec / 60000) % 60,  s = (msec / 1000) % 60,  ms = msec % 1000;
	ffsz_format(buf, cap, "%u:%02u:%02u.%03u"
		, h, m, s, ms);
	return buf;
}

#define INT32_MAKE1616(h, l)  ((((uint)(h) & 0xffff) << 16) | ((l) & 0xffff))
#define INT32_LO16(i)  ((i) & 0xffff)
#define INT32_HI16(i)  (((i) >> 16) & 0xffff)
