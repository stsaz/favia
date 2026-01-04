/** favia
2025, Simon Zolin */

#define FAV_VER  105

#include <ffsys/base.h>
#include <stdint.h>
typedef unsigned int uint;
typedef unsigned short ushort;
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
	FAV_LOG_EXTRA,
};
#define fav_syserrlog(...)  core->log(FAV_LOG_ERROR, NULL, __VA_ARGS__) // TODO
#define fav_errlog(...)  core->log(FAV_LOG_ERROR, NULL, __VA_ARGS__)
#define fav_warnlog(...)  core->log(FAV_LOG_WARN, NULL, __VA_ARGS__)
#define fav_infolog(...)  core->log(FAV_LOG_INFO, NULL, __VA_ARGS__)
#define fav_dbglog(...) \
do { \
	if (ff_unlikely(core->conf.log_level >= FAV_LOG_DEBUG)) \
		core->log(FAV_LOG_DEBUG, NULL, __VA_ARGS__); \
} while (0)
#define syserrlog  fav_syserrlog
#define errlog  fav_errlog
#define warnlog  fav_warnlog
#define infolog  fav_infolog
#define dbglog  fav_dbglog


struct fav_core_conf {
	uint log_level;
	void (*log)(uint level, const char *id, const char *format, ...);
	void (*signal)(fav_track *trk, uint cmd, uint flags);

	u_char seek_step_sec;
	u_char seek_leap_sec;
	u_char seek_leap_pct;
	u_char zoom_by_pct;
	u_char volume_step_pct;
	ushort seek_range_margin_msec;
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


struct fav_track_cu {
	char name[16];
	int (*open)(fav_track *t);
	void (*close)(fav_track *t);
	int (*process)(fav_track *t);
	int (*ctl)(fav_track *t, uint cmd, uint flags);
};

struct fav_track_conf {
	const struct fav_track_cu **conveyor;

	struct {
		const char *url;
		uint64_t seek_msec, until_msec;
	} input;

	struct {
		const char *hw_accel;
		u_char q_size;
	} decoder;

	struct {
		ushort zoom;
		uint fullscreen :1;
	} video;

	struct {
		u_char volume;
		u_char mute :1;
	} audio;

	uint no_display :1;
	uint no_sound :1;
	uint url_transient :1;
	uint pause_on_end :1;
	uint print_time :1;
};

static inline const struct fav_track_conf* fav_track_info(const fav_track *t) { return (struct fav_track_conf*)t; }
static inline void* fav_track_alloc(const fav_track *t, uint n) { return ffmem_calloc(1, n); }
static inline void fav_track_free(const fav_track *t, void *ptr) { return ffmem_free(ptr); }

enum FAV_TRACK_E {
	FAV_TRACK_E_FINISHED,
	FAV_TRACK_E_IO,
	FAV_TRACK_E_OTHER,
};

enum FAV_TRACK_CMD_ARG {
	FAV_TRACK_SEEK_REVERSE = 1,
	FAV_TRACK_SEEK_LEAP = 2,
	FAV_TRACK_SEEK_LEAP_PERCENT = 4,
	FAV_TRACK_SEEK_LOOP = 8,
	FAV_TRACK_WND_NEXT = 2,
	FAV_TRACK_WND_RESIZED = 4,
	FAV_TRACK_WND_SHOWN = 8,
	FAV_TRACK_VOL_MUTE = 2,
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
	FAV_TRACK_ADD, // char* (transient)
};

struct fav_track_if {
	fav_track* (*create)(struct fav_track_conf *conf);
	void (*close)(fav_track *t);

	/** cmd: enum FAV_TRACK_CMD */
	int (*cmd)(fav_track *t, uint cmd, ...);

	fav_track* (*find)(const void *sdl_wnd);
};
