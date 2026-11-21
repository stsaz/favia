/** favia: utils
2025, Simon Zolin */

#pragma once
#include <time.h>
#include <ffsys/dirscan.h>
#include <ffbase/string.h>

static inline const char* time_print(uint64 msec, char *buf, size_t cap)
{
	uint h = msec / 3600000,  m = (msec / 60000) % 60,  s = (msec / 1000) % 60,  ms = msec % 1000;
	ffsz_format(buf, cap, "%u:%02u:%02u.%03u"
		, h, m, s, ms);
	return buf;
}

#define INT32_MAKE1616(h, l)  ((((uint)(h) & 0xffff) << 16) | ((l) & 0xffff))
#define INT32_LO16(i)  ((i) & 0xffff)
#define INT32_HI16(i)  (((i) >> 16) & 0xffff)

#ifdef FF_WIN
static inline int ffthread_usleep(unsigned usec)
{
	Sleep(usec / 1000);
	return 0;
}

#else
static inline int ffthread_usleep(unsigned usec)
{
	struct timespec ts = {
		.tv_sec = usec / 1000000,
		.tv_nsec = (usec % 1000000) * 1000,
	};
	return nanosleep(&ts, NULL);
}
#endif


#define DIGIT(c)  (c >= '0' && c <= '9')

/** Compare file names (case-insensitive, number-aware).
Examples:
* "2" < "10"
* "a" < "B" */
static int filename_icmp_nat(const char *a, const char *b)
{
	int first_diff = 0;
	ffuint ca, cb;
	ffsize i = 0, z, ea, eb;
	for (;;  i++) {
		ca = (ffbyte)a[i], cb = (ffbyte)b[i];

		if (ca != cb) {
			if (DIGIT(ca)) {
				if (DIGIT(cb))
					goto numbers;

				if (!cb)
					return 1; // 0 > ""

				if (i && DIGIT(a[i-1]))
					return 1; // 11 > 1a

				return -1; // 1 < a

			} else if (DIGIT(cb)) {
				if (!ca)
					return -1; // a < a1

				if (i && DIGIT(a[i-1]))
					return -1; // 1a < 11

				return 1; // a > 1
			}

			first_diff = (ca < cb) ? -1 : 1; // A < a

			if (ca >= 'A' && ca <= 'Z')
				ca |= 0x20;
			if (cb >= 'A' && cb <= 'Z')
				cb |= 0x20;
			if (ca != cb)
				return (ca < cb) ? -1 : 1; // Aa < Bb

		} else if (ca == '\0') {
			return first_diff;
		}
	}

numbers:
	ea = i, eb = i;
	while (DIGIT(a[ea]))
		ea++;
	while (DIGIT(b[eb]))
		eb++;
	if (ea == eb)
		return (ca < cb) ? -1 : 1; // Numbers of same length: 0 < 1

	if (ca != '0' && cb != '0')
		return (ea < eb) ? -1 : 1; // 1 < 10

	z = i;
	while (z && a[z-1] == '0')
		z--; // Skip zeros backward
	if (z && DIGIT(a[z-1]))
		return (ea < eb) ? -1 : 1; // Non-zero prefix: 101 < 1001

	// Zero-prefixed numbers of different length

	z = i;
	while (a[i] == '0')
		i++;
	while (b[z] == '0')
		z++;

	ea -= i;
	eb -= z;
	if (ea != eb)
		return (ea < eb) ? -1 : 1; // 01 < 010

	a += i;
	b += z;
	for (i = 0;  i < ea;  i++) {
		if (a[i] != b[i])
			return (a[i] < b[i]) ? -1 : 1; // 1 < 02
	}

	return (ca < cb) ? -1 : 1; // 01 < 1
}

#undef DIGIT

static inline int dirscan_cmp_nat(const void *a, const void *b, void *udata)
{
	const ffdirscan *ds = udata;
	uint off1 = *(uint*)a, off2 = *(uint*)b;
	return filename_icmp_nat((char*)ds->names + off1, (char*)ds->names + off2);
}

/** Sort entries: natural (number-aware) order. */
static inline void dirscan_sort(ffdirscan *ds)
{
	ffsort(ffdirscan_index(ds), ffdirscan_count(ds), sizeof(uint), dirscan_cmp_nat, ds);
}
