/** favia
2025, Simon Zolin */

#pragma once

#define FAV_VER  200

#include <ffsys/base.h>
#include <ffsys/timerqueue.h>
#include <ffbase/string.h>

typedef unsigned long long uint64;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char u_char;

typedef struct fav_track_if fav_track_if;
typedef struct fav_track fav_track;
typedef void (*fav_task_func)(void *param);
typedef struct { void *a[4]; } fav_task;
typedef fftimerqueue_node fav_timer;


enum FAV_LOG_LEVEL {
	FAV_LOG_ERROR,
	FAV_LOG_WARN,
	FAV_LOG_INFO,
	FAV_LOG_VERB,
	FAV_LOG_DEBUG,
	FAV_LOG_EXTRA,

	FAV_LOG_SYSERR = 0x10,
};
#define fav_syserrlog(...)  core->log(NULL, FAV_LOG_ERROR | FAV_LOG_SYSERR, NULL, __VA_ARGS__)
#define fav_errlog(...)  core->log(NULL, FAV_LOG_ERROR, NULL, __VA_ARGS__)
#define fav_warnlog(...)  core->log(NULL, FAV_LOG_WARN, NULL, __VA_ARGS__)
#define fav_infolog(...)  core->log(NULL, FAV_LOG_INFO, NULL, __VA_ARGS__)
#define fav_dbglog(...) \
do { \
	if (ff_unlikely(core->conf.log_level >= FAV_LOG_DEBUG)) \
		core->log(NULL, FAV_LOG_DEBUG, NULL, __VA_ARGS__); \
} while (0)
#define fav_extralog(...) \
do { \
	if (ff_unlikely(core->conf.log_level >= FAV_LOG_EXTRA)) \
		core->log(NULL, FAV_LOG_EXTRA, NULL, __VA_ARGS__); \
} while (0)
#define syserrlog  fav_syserrlog
#define errlog  fav_errlog
#define warnlog  fav_warnlog
#define infolog  fav_infolog
#define dbglog  fav_dbglog
#define extralog  fav_extralog


struct fav_core_conf {
	uint log_level;
	void (*log)(void *log_obj, uint level, const char *id, const char *format, ...);
	void (*signal)(fav_track *trk, uint cmd, uint flags);

	/** Get data from resource file.
	Return empty data on error. */
	ffstr (*resource_load)(const char *name);

	u_char seek_step_sec;
	u_char seek_jump_sec;
	u_char seek_leap_sec;
	u_char seek_leap_pct;
	u_char zoom_by_pct;
	u_char volume_step_pct;
	ushort seek_range_margin_msec;

	const char *move_dir[3];
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

	void (*log)(void *log_obj, uint level, const char *id, const char *format, ...);
	void (*stop)();

	/** flags: enum FAV_TASK */
	void (*task)(uint flags, fav_task *t, fav_task_func func, void *param);

	/** Start/stop a oneshot/periodic timer.
	interval_msec:
		<0: one shot timer;
		>0: periodic timer;
		=0: stop timer */
	void (*timer)(fav_timer *t, int interval_msec, fav_task_func func, void *param);
};


struct fav_track_cu {
	char name[16];
	int (*open)(fav_track *t);
	void (*close)(fav_track *tags);
	/** Return enum FAV_CU */
	int (*process)(fav_track *t);
	int (*ctl)(fav_track *t, uint cmd, uint flags);
};

struct fav_track_conf {
	const struct fav_track_cu **conveyor;

	struct {
		const char *url;
		uint64 seek_msec, until_msec;
	} input;

	struct {
		const char *hw_accel;
		u_char q_size;
	} decoder;

	struct {
		ushort zoom;
		uint fullscreen :1;
		uint zoom_lock_wnd :1;
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
#define fav_track_allocT(t, T)  (T*)fav_track_alloc(t, sizeof(T))
static inline void fav_track_free(const fav_track *t, void *ptr) { return ffmem_free(ptr); }

enum FAV_TRACK_E {
	FAV_TRACK_E_FINISHED,
	FAV_TRACK_E_IO,
	FAV_TRACK_E_OTHER,
};

enum FAV_TRACK_START_ARG {
	FAV_TRACK_START_PREV = 0,
	FAV_TRACK_START_NEXT,
	FAV_TRACK_START_FIRST,
	FAV_TRACK_START_LAST,
	FAV_TRACK_START_PGNEXT,
	FAV_TRACK_START_PGPREV,
};

enum FAV_TRACK_SEEK_ARG {
	FAV_TRACK_SEEK_FWD = 0,
	FAV_TRACK_SEEK_REVERSE = 1,
	FAV_TRACK_SEEK_LEAP = 2,
	FAV_TRACK_SEEK_LEAP_PERCENT = 4,
	FAV_TRACK_SEEK_LOOP = 8,
	FAV_TRACK_SEEK_JUMP = 0x10,
};

enum FAV_TRACK_WND_ARG {
	FAV_TRACK_WND_RM = 0,
	FAV_TRACK_WND_ADD = 1,
	FAV_TRACK_WND_NEXT = 2,
	FAV_TRACK_WND_RESIZED = 4,
	FAV_TRACK_WND_SHOWN = 8,
	FAV_TRACK_WND_FULLSCREEN = 0x10,
};

enum FAV_TRACK_VOL_ARG {
	FAV_TRACK_VOL_MUTE = 2,
};

enum FAV_TRACK_ZOOM_ARG {
	FAV_TRACK_ZOOM_OUT = 0,
	FAV_TRACK_ZOOM_IN = 1,
};

enum FAV_TRACK_SRC_ARG {
	FAV_TRACK_SRC_TRASH = 0,
	FAV_TRACK_SRC_MOVE = 0x10,
};

enum FAV_TRACK_CMD {
	/** Notify track status (finished, IO error, etc.) */
	FAV_TRACK_STATUS, // enum FAV_TRACK_E
	/** Toggle pause */
	FAV_TRACK_PAUSE,
	/** Toggle fullscreen */
	FAV_TRACK_FULLSCREEN,
	/** Zoom in/out */
	FAV_TRACK_ZOOM, // enum FAV_TRACK_ZOOM_ARG
	/** Adjust volume / mute */
	FAV_TRACK_VOLUME, // enum FAV_TRACK_VOL_ARG
	/** Switch to next audio stream */
	FAV_TRACK_AUDIO_NEXT,
	/** Set new playback position */
	FAV_TRACK_SEEK, // enum FAV_TRACK_SEEK_ARG
	/** Start playing a file from queue */
	FAV_TRACK_START, // enum FAV_TRACK_START_ARG
	/** Stop playback */
	FAV_TRACK_STOP,
	/** Quit application */
	FAV_TRACK_QUIT,
	/** Window operations (add, remove, next, resize, fullscreen) */
	FAV_TRACK_WINDOW, // enum FAV_TRACK_WND_ARG
	/** Source file operations (trash, move) */
	FAV_TRACK_SOURCE, // enum FAV_TRACK_SRC_ARG
	/** Add file to queue */
	FAV_TRACK_ADD, // char* (transient)
};

struct fav_track_if {
	fav_track* (*create)(struct fav_track_conf *conf);
	void (*close)(fav_track *t);

	/** cmd: enum FAV_TRACK_CMD */
	int (*cmd)(fav_track *t, uint cmd, ...);

	fav_track* (*find)(const void *sdl_wnd);
};


struct fav_q_conf {
	struct fav_track_conf tconf;
	u_char parallel;
	u_char repeat;
};

struct fav_q_if {
	void (*on_change)(void (*f)(uint flags));
	void (*create)(struct fav_q_conf *conf);
	void (*add)(const char **filenames, uint n);
	void (*ins)(const char *fn, uint pos);
	void (*play)(int delta);
	uint (*count)();
	void (*signal)(fav_track *trk, uint cmd, uint flags);
	struct fav_q_conf* (*conf)();
};
