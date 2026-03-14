/** favia: executor
2025, Simon Zolin */

#include <favia.h>
#include <util/util.hpp>
#include <util/log.h>
#include <ffsys/signal.h>
#include <ffsys/globals.h>

FF_EXTERN fav_core_if* core_init(struct fav_core_conf *conf);
FF_EXTERN void core_destroy();
FF_EXTERN int core_run();
FF_EXTERN const struct fav_q_if qif;
FF_EXTERN int dir_read(const char *fn, uint ins_pos, uint flags);
static void exe_signal(fav_track *trk, uint cmd, uint flags);

struct exe {
	fav_core_if *core;
	fav_task task;
	uint exit_code;
	struct zzlog log;
	const struct fav_q_if *q;

	const char *cmd_line;
	const char *hwaccel;
	u_char autodir;
	u_char debug;
	u_char fullscreen;
	u_char mute;
	u_char no_display;
	u_char no_sound;
	u_char pause_on_end;
	u_char perf;
	u_char repeat;
	uint parallel;
	uint volume;
	uint zoom;
	uint64_t seek_msec, until_msec;
	struct ffargs *cmd;
	xxvec input; // char*[]
};
static struct exe *x;

#include <exe/log.h>
#include <exe/cmd.hpp>

static void core_open()
{
	struct fav_core_conf cc = {
#ifdef FF_DEBUG
		.log_level = (x->debug) ? FAV_LOG_EXTRA : FAV_LOG_VERB,
#else
		.log_level = (x->debug) ? FAV_LOG_DEBUG : FAV_LOG_VERB,
#endif
		.log = exe_log,
		.signal = exe_signal,

		.seek_step_sec = 5,
		.seek_leap_sec = 60,
		.seek_leap_pct = 5,
		.zoom_by_pct = 10,
		.volume_step_pct = 5,
		.seek_range_margin_msec = 500,
	};
	cc.move_dir[0] = "1";
	cc.move_dir[1] = "2";
	cc.move_dir[2] = "3";
	x->core = core_init(&cc);
}

#ifdef FF_WIN
#define OS_NAME "windows"
#else
#define OS_NAME "linux"
#endif
static void version_print()
{
	ffstdout_fmt("favia v%s (" OS_NAME "-" "amd64" ")\n"
		, x->core->version_str);
}

static void sig(struct ffsig_info *i)
{
	x->core->stop();
}

static void signals()
{
	static const uint sigs[] = { FFSIG_INT };
	ffsig_subscribe(sig, sigs, FF_COUNT(sigs));
}

FF_EXTERN const struct fav_track_cu
	trk_cu_guard,
	trk_cu_read,
	trk_cu_decode,
	trk_cu_sync,
	trk_cu_vo,
	trk_cu_ao,
	trk_cu_until;

static const struct fav_track_cu* trk_cu_set_play[] = {
	&trk_cu_guard,
	&trk_cu_read,

	/*
	(!packet && !read_fin) ? BACK
	(!packet && read_fin) ? fin=1
	(want_input && fin) ? FIN
	FWD
	*/
	&trk_cu_decode,

	/*
	(want_input) ? BACK
	(redraw || audio || video) ? FWD
	(input_full || fin || paused) ? ASYNC
	BACK
	*/
	&trk_cu_sync,

	/*
	want_input=q_empty
	(complete) ? input_full&=~(A || V)
	*/
	&trk_cu_vo,
	&trk_cu_ao,

	&trk_cu_until,
	NULL,
};

static void exe_q_changed(uint flags)
{
	switch (flags) {
	case '.':
		x->exit_code = 0;
		x->core->stop();
	}
}

static void exe_iq_start(void *param)
{
	exe *x = (exe*)param;
	x->q = &qif;
	x->q->on_change(exe_q_changed);

	struct fav_q_conf qc = {
		.tconf = {
			.conveyor = trk_cu_set_play,
			.input = {
				.seek_msec = x->seek_msec,
				.until_msec = (x->until_msec) ? x->until_msec : ~0ULL,
			},
			.decoder = {
				.hw_accel = x->hwaccel,
				.q_size = 4,
			},
			.video = {
				.zoom = (ushort)x->zoom,
				.fullscreen = x->fullscreen,
			},
			.audio = {
				.volume = (u_char)x->volume,
				.mute = x->mute,
			},
			.no_display = x->no_display,
			.no_sound = x->no_sound,
			.pause_on_end = x->pause_on_end,
			.print_time = x->perf,
		},
		.parallel = x->parallel,
		.repeat = x->repeat,
	};
	x->q->create(&qc);
	x->q->add((const char**)x->input.ptr, x->input.len);

	if (x->input.len && x->autodir) {
		dir_read(xxvec().copy(xxpath(*x->input.at<char*>(0)).path()).strz(), 1, 0); // add all files from the source file's directory
	}

	x->q->play(0);
}

static void exe_signal(fav_track *trk, uint cmd, uint flags)
{
	switch (cmd) {
	case FAV_TRACK_START:
	case FAV_TRACK_WINDOW:
		x->q->signal(trk, cmd, flags);
		break;

	case FAV_TRACK_QUIT:
		x->exit_code = 0;
		x->core->stop();
	}
}

int main(int argc, char **argv)
{
	x = ffmem_new(struct exe);
	x->exit_code = 1;
	logs(&x->log);

#ifdef FF_WIN
	x->cmd_line = ffsz_alloc_wtou(GetCommandLineW());
#endif
	if (cmd(argc, argv, x->cmd_line)) goto end;

	core_open();
	version_print();
	signals();

	x->core->task(FAV_TASK_ADD, &x->task, exe_iq_start, x);
	core_run();

end:
	core_destroy();
	x->~exe();
	ffmem_free(x);
	return x->exit_code;
}
