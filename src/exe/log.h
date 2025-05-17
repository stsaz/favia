/** favia: logger
2025, Simon Zolin */

#include <ffsys/std.h>

static void exe_log(uint level, const char *id, const char *format, ...) {
	static const char levels[][8] = {
		"ERROR",
		"WARN ",
		"INFO ",
		"VERB ",
		"DEBUG",
	};

	char buf[1024];
	uint cap = sizeof(buf) - 4;
	ssize_t r = 0, r2;

	r += _ffs_copyz(buf + r, cap - r, levels[level]);
	buf[r++] = ' ';

	if (id) {
		r += _ffs_copyz(buf + r, cap - r, id);
		buf[r++] = ' ';
	}

	va_list va;
	va_start(va, format);
	r2 = ffs_formatv(buf + r, cap - r, format, va);
	va_end(va);
	if (r2 > 0)
		r += r2;

	buf[r++] = '\r';
	buf[r++] = '\n';
	ffstdout_write(buf, r);
}

#define exe_errlog(...)  exe_log(FAV_LOG_ERROR, NULL, __VA_ARGS__)
#define exe_warnlog(...)  exe_log(FAV_LOG_WARN, NULL, __VA_ARGS__)
#define exe_dbglog(...) \
do { \
	if (ff_unlikely(x->debug)) \
		exe_log(FAV_LOG_DEBUG, NULL, __VA_ARGS__); \
} while (0)
