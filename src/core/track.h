/** favia: track
2025, Simon Zolin */

#pragma once
#include <favia.h>
#include <util/ffmpeg.h>
#include <util/util.hpp>

FF_EXTERN const fav_core_if *core;

#include <util/avsync.hpp>

struct qframe {
	struct ffmpeg_frame frame;
	uint64_t ts;
	uint dur;

	void alloc() {
		ffmpeg_frame_init(&frame);
	}
	void destroy() {
		ffmpeg_frame_destroy(&frame);
	}
	void unref() {
		ffmpeg_frame_unref(&frame);
	}
};

#include <util/queue.hpp>

#undef syserrlog
#undef errlog
#undef warnlog
#undef infolog
#undef dbglog
#undef extralog
#define syserrlog(trk, ...)  core->log(FAV_LOG_ERROR, trk->id, __VA_ARGS__) // TODO
#define errlog(trk, ...)  core->log(FAV_LOG_ERROR, trk->id, __VA_ARGS__)
#define warnlog(trk, ...)  core->log(FAV_LOG_WARN, trk->id, __VA_ARGS__)
#define infolog(trk, ...)  core->log(FAV_LOG_INFO, trk->id, __VA_ARGS__)
#define dbglog(trk, ...) \
do { \
	if (ff_unlikely(core->conf.log_level >= FAV_LOG_DEBUG)) \
		core->log(FAV_LOG_DEBUG, trk->id, __VA_ARGS__); \
} while (0)

#define extralog(trk, ...) \
do { \
	if (ff_unlikely(core->conf.log_level >= FAV_LOG_EXTRA)) \
		core->log(FAV_LOG_EXTRA, trk->id, __VA_ARGS__); \
} while (0)

enum {
	FAV_CF_REVERSE = 1,
};

enum FAV_CU {
	FAV_CU_FWD,
	FAV_CU_BACK,
	FAV_CU_ASYNC,
	FAV_CU_ERROR,
	FAV_CU_WARN,
	FAV_CU_DONE,
	FAV_CU_FIN,
};

enum { FAV_F_VIDEO = 1, FAV_F_AUDIO = 2, FAV_F_REDRAW = 4, };

enum trk_state {
	TRK_FIN = 0x10,
	TRK_PAUSED = 0x20,
	TRK_MUTE = 0x40,
};

struct inx;
struct aox;
struct vox;

struct fav_track {
	struct fav_track_conf conf;
	char id[8];

	struct inx *inx;
	struct aox *aox;
	struct vox *vox;
	const void *vo_window;

	uint duration_msec;

	uint video_width, video_height;

	int audio_format; // AV_SAMPLE_FMT_*
	uint audio_rate;
	u_char audio_channels;

	xxffmpeg_dec dec;
	queue *vq, *aq;
	struct avsync sync;
	xxffmpeg_packet pkt;
	uint input_full, have_pkt;
	uint64_t loop_start, loop_end;
	uint arg2;
	uint state; // enum trk_state
	uint redraw :1;
	uint read_fin :1;
	uint audio_stream_switched :1;
	uint next :1;
	uint static_pic :1;
	uint iframe;
	uint want_input;
	uint async_ret;
	uint frame_flags;
	uint cur_pos_msec;
	int error;

	struct {
		struct fav_track_cu units[10];
		uint i, n, f;
		u_char opened[10];
		uint64_t busy_time_nsec[10];
	} conv;
};
