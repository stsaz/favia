/** favia: logger
2025, Simon Zolin */

#include <ffsys/std.h>

static void exe_log(void *log_obj, uint level, const char *id, const char *format, ...)
{
	va_list va;
	va_start(va, format);
	uint64 tid = 0;
	zzlog_printv(&x->log, level, NULL, tid, NULL, id, format, va);
	va_end(va);
}

#define exe_errlog(...)  exe_log(NULL, FAV_LOG_ERROR, NULL, NULL, __VA_ARGS__)
#define exe_warnlog(...)  exe_log(NULL, FAV_LOG_WARN, NULL, NULL, __VA_ARGS__)
#define exe_dbglog(...) \
do { \
	if (ff_unlikely(x->log_level >= FAV_LOG_DEBUG)) \
		exe_log(NULL, FAV_LOG_DEBUG, NULL, NULL, __VA_ARGS__); \
} while (0)

static void logs(struct zzlog *l)
{
	static const char levels[][8] = {
		"ERROR ",
		"WARN  ",
		"INFO  ",
		"INFO  ",
		"DEBUG ",
		"DEBUG+",
	};
	ffmem_copy(l->levels, levels, sizeof(levels));

	static const char colors[][8] = {
		/*FAV_LOG_ERROR*/	FFSTD_CLR(FFSTD_RED),
		/*FAV_LOG_WARN*/	FFSTD_CLR(FFSTD_YELLOW),
		/*FAV_LOG_INFO*/	"",
		/*FAV_LOG_VERB*/	"",
		/*FAV_LOG_DEBUG*/	"",
		/*FAV_LOG_EXTRA*/	FFSTD_CLR_I(FFSTD_BLUE),
	};
	ffmem_copy(l->colors, colors, sizeof(colors));

	l->fd = ffstdout;
	int r = ffstd_attr(l->fd, FFSTD_VTERM, FFSTD_VTERM);
	l->use_color = !r;
	l->fd_file = (r < 0);
}
