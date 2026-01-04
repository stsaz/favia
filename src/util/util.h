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
