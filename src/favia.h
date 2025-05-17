/** favia
2025, Simon Zolin */

#define FAV_VER  101

#include <ffsys/base.h>
#include <stdint.h>
typedef unsigned int uint;
typedef unsigned char u_char;

typedef struct fav_track_if fav_track_if;
typedef struct fav_track fav_track;
typedef void (*fav_task_func)(void *param);
typedef struct { void *a[4]; } fav_task;


enum FAV_LOG_LEVEL {
	FAV_LOG_ERROR,
	FAV_LOG_WARN,
	FAV_LOG_INFO,
	FAV_LOG_VERB,
	FAV_LOG_DEBUG,
};
#define syserrlog(...)  core->log(FAV_LOG_ERROR, NULL, __VA_ARGS__) // TODO
#define errlog(...)  core->log(FAV_LOG_ERROR, NULL, __VA_ARGS__)
#define warnlog(...)  core->log(FAV_LOG_WARN, NULL, __VA_ARGS__)
#define infolog(...)  core->log(FAV_LOG_INFO, NULL, __VA_ARGS__)
#define dbglog(...) \
do { \
	if (ff_unlikely(core->conf.debug)) \
		core->log(FAV_LOG_DEBUG, NULL, __VA_ARGS__); \
} while (0)


struct fav_core_conf {
	uint debug;
	void (*log)(uint level, const char *id, const char *format, ...);
	void (*signal)(fav_track *trk, uint cmd, uint flags);
};

enum FAV_TASK {
	FAV_TASK_ADD,
	FAV_TASK_DEL,
};

typedef struct fav_core_if fav_core_if;
struct fav_core_if {
	struct fav_core_conf conf;
	const fav_track_if *track;
	const char *version_str;

	void (*log)(uint level, const char *id, const char *format, ...);
	void (*stop)();

	/** flags: enum FAV_TASK */
	void (*task)(uint flags, fav_task *t, fav_task_func func, void *param);
};


struct fav_track_conf {
	struct {
		const char *url;
		uint64_t seek_msec, until_msec;
	} input;

	struct {
		const char *hw_accel;
	} decoder;

	struct {
		ushort zoom;
	} video;

	struct {
		u_char volume;
		u_char mute :1;
	} audio;

	uint no_display :1;
	uint no_sound :1;
};

static inline const struct fav_track_conf* fav_track_info(const fav_track *t) { return (struct fav_track_conf*)t; }

enum FAV_TRACK_E {
	FAV_TRACK_E_FINISHED,
	FAV_TRACK_E_INIT,
	FAV_TRACK_E_IO,
	FAV_TRACK_E_OTHER,
};

enum FAV_TRACK_CMD {
	FAV_TRACK_STATUS, // enum FAV_TRACK_E
	FAV_TRACK_PAUSE,
	FAV_TRACK_FULLSCREEN,
	FAV_TRACK_ZOOM, // int
	FAV_TRACK_VOLUME, // int
	FAV_TRACK_AUDIO_NEXT,
	FAV_TRACK_SEEK, // int
	FAV_TRACK_NEXT, // int
	FAV_TRACK_STOP, // int
	FAV_TRACK_QUIT,
	FAV_TRACK_WINDOW, // int, int
	FAV_TRACK_SOURCE,
};

struct fav_track_if {
	fav_track* (*create)(struct fav_track_conf *conf);
	void (*close)(fav_track *t);

	/** cmd: enum FAV_TRACK_CMD */
	int (*cmd)(fav_track *t, uint cmd, ...);

	fav_track* (*find)(const void *sdl_wnd);
};
