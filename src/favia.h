/** favia
2025, Simon Zolin */

#include <ffsys/base.h>
#include <stdint.h>
typedef unsigned int uint;
typedef unsigned char u_char;

typedef struct fav_track_if fav_track_if;
typedef struct fav_track fav_track;

enum FAV_LOG_LEVEL {
	FAV_LOG_ERROR,
	FAV_LOG_WARN,
	FAV_LOG_INFO,
	FAV_LOG_VERB,
	FAV_LOG_DEBUG,
};
#define errlog(...)  core->log(FAV_LOG_ERROR, __VA_ARGS__)
#define warnlog(...)  core->log(FAV_LOG_WARN, __VA_ARGS__)
#define dbglog(...) \
do { \
	if (core->debug) \
		core->log(FAV_LOG_DEBUG, __VA_ARGS__); \
} while (0)

struct fav_core_conf {
	void (*log)(uint level, const char *format, ...);
};

typedef struct fav_core_if fav_core_if;
struct fav_core_if {
	const fav_track_if *track;
	const char *version_str;
	uint debug;

	void (*log)(uint level, const char *format, ...);
	void (*stop)();
};

struct fav_track_conf {
	struct {
		const char *url;
		uint64_t seek_msec;
	} input;

	struct {
		const char *hw_accel;
	} decoder;

	uint no_display :1;
	uint no_sound :1;
};

enum FAV_TRACK_CMD {
	FAV_TRACK_STATUS,
	FAV_TRACK_PAUSE_TOGGLE,
	FAV_TRACK_FULLSCREEN_TOGGLE,
	FAV_TRACK_ZOOM,
	FAV_TRACK_VOLUME,
};

struct fav_track_if {
	fav_track* (*create)(struct fav_track_conf *conf);
	void (*stop)(fav_track *t);

	/** cmd: enum FAV_TRACK_CMD */
	int (*cmd)(fav_track *t, uint cmd, ...);

	void (*seek_by)(fav_track *t, int64_t msec);
};
