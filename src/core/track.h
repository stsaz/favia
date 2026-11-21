/** favia: track
2025, Simon Zolin */

#pragma once
#include <favia.h>

extern const fav_core_if *core;

#undef syserrlog
#undef errlog
#undef warnlog
#undef infolog
#undef dbglog
#undef extralog
#define syserrlog(trk, ...)  core->log(NULL, FAV_LOG_ERROR | FAV_LOG_SYSERR, trk->id, __VA_ARGS__)
#define errlog(trk, ...)  core->log(NULL, FAV_LOG_ERROR, trk->id, __VA_ARGS__)
#define warnlog(trk, ...)  core->log(NULL, FAV_LOG_WARN, trk->id, __VA_ARGS__)
#define infolog(trk, ...)  core->log(NULL, FAV_LOG_INFO, trk->id, __VA_ARGS__)
#define dbglog(trk, ...) \
do { \
	if (ff_unlikely(core->conf.log_level >= FAV_LOG_DEBUG)) \
		core->log(NULL, FAV_LOG_DEBUG, trk->id, __VA_ARGS__); \
} while (0)

#define extralog(trk, ...) \
do { \
	if (ff_unlikely(core->conf.log_level >= FAV_LOG_EXTRA)) \
		core->log(NULL, FAV_LOG_EXTRA, trk->id, __VA_ARGS__); \
} while (0)

enum {
	FAV_CF_REVERSE = 1,
};

enum FAV_CU {
	FAV_CU_FWD,
	FAV_CU_BACK,
	FAV_CU_ASYNC,
	FAV_CU_ERROR,
	FAV_CU_DONE,
	FAV_CU_FIN,
};

enum { FAV_F_VIDEO = 1, FAV_F_AUDIO = 2, FAV_F_REDRAW = 4, };

enum trk_state {
	TRK_FIN = 0x10,
	TRK_PAUSED = 0x20,
	TRK_MUTE = 0x40,
};

struct fav_track {
	struct fav_track_conf conf;
	char id[8];

	struct inx *inx;
	struct aox *aox;
	struct vox *vox;
	struct syx *syx;
	const void *vo_window;

	uint duration_msec;

	uint video_width, video_height;

	int audio_format; // AV_SAMPLE_FMT_*
	uint audio_rate;
	u_char audio_channels;

	struct ffmpeg_dec *dec;
	struct avqueue *vq, *aq;
	struct avsync *sync;
	struct ffmpeg_packet *pkt;
	uint64 cur_pos_msec;
	uint input_full, have_pkt;
	uint64 loop_start, loop_end;
	uint arg2;
	uint state; // enum trk_state
	uint redraw :1;
	uint read_fin :1;
	uint audio_stream_switched :1;
	uint static_pic :1;
	uint stop :1;
	uint next :1;
	uint picture :1;
	uint iframe;
	uint want_input;
	uint async_ret;
	uint frame_flags;
	int error;

	struct {
		char text[1024];
		fav_timer timer;
	} vo;

	struct {
		struct fav_track_cu units[10];
		uint i, n, f;
		u_char opened[10];
		uint64 busy_time_nsec[10];
	} conv;
};

extern void trk_text_reset_timer(fav_track *t);

#define trk_text(t, fmt, ...) \
do { \
	ffsz_format((t)->vo.text, sizeof((t)->vo.text), fmt, ##__VA_ARGS__); \
	trk_text_reset_timer(t); \
} while (0)
