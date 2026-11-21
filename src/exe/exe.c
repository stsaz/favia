/** favia: executor
2025, Simon Zolin */

#include <favia.h>
#include <util/log.h>
#include <ffsys/path.h>
#include <ffsys/signal.h>
#include <ffsys/process.h>
#include <ffsys/globals.h>

extern fav_core_if* core_init(struct fav_core_conf *conf);
extern void core_destroy();
extern int core_run();
extern const struct fav_q_if qif;
extern int dir_read(const char *fn, uint ins_pos, uint flags);
static void exe_signal(fav_track *trk, uint cmd, uint flags);
extern int autodir_read(const char *dir_path, ffstr file_name);

struct args {
	const char *hwaccel;
	u_char autodir;
	u_char debug;
	u_char mute;
	u_char no_display;
	u_char no_sound;
	u_char pause_on_end;
	u_char perf;
	u_char repeat;
	uint parallel;
	uint volume;
	uint zoom;
	uint64 seek_msec, until_msec;
	struct ffargs *cmd;
	ffvec input; // char*[]
};

struct exe {
	fav_core_if *core;
	fav_task task;
	uint exit_code;
	struct zzlog log;
	const struct fav_q_if *q;

	char	fn_buf[128], *fn;
	ffstr	root_dir;

	const char *cmd_line;
	u_char fullscreen;
	struct args args;
};
static struct exe *x;

static void exe_free(struct exe *x)
{
	ffvec_free(&x->args.input);
	ffmem_free(x);
}

static int conf(const char *argv_0)
{
	x->fn = ffmem_alloc(4*1024);
	const char *p;
	if (!(p = ffps_filename(x->fn, 4*1024, argv_0)))
		return -1;

	uint n = ffsz_len(p);
	if (n + 1 <= sizeof(x->fn_buf)) {
		ffmem_copy(x->fn_buf, p, n + 1);
		ffmem_free(x->fn);
		x->fn = NULL;
		p = x->fn_buf;
	}

	if (ffpath_splitpath_str(FFSTR_N(p, n), &x->root_dir, NULL) < 0)
		return -1;
	x->root_dir.len++;
	return 0;
}

#include <exe/log.h>
#include <exe/cmd.h>

static ffstr res_load(const char *name)
{
	char *fn = ffsz_allocfmt("%S%s"
		, &x->root_dir, name);
	ffvec d = {};
	fffile_readwhole(fn, &d, 100*1024*1024);
	ffmem_free(fn);
	return *(ffstr*)&d;
}

static void core_open()
{
	struct fav_core_conf cc = {
#ifdef FF_DEBUG
		.log_level = (x->args.debug) ? FAV_LOG_EXTRA : FAV_LOG_VERB,
#else
		.log_level = (x->args.debug) ? FAV_LOG_DEBUG : FAV_LOG_VERB,
#endif
		.log = exe_log,
		.signal = exe_signal,
		.resource_load = res_load,

		.seek_step_sec = 5,
		.seek_jump_sec = 15,
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

extern const struct fav_track_cu
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
	struct exe *x = param;
	x->q = &qif;
	x->q->on_change(exe_q_changed);

	struct fav_q_conf qc = {
		.tconf = {
			.conveyor = trk_cu_set_play,
			.input = {
				.seek_msec = x->args.seek_msec,
				.until_msec = (x->args.until_msec) ? x->args.until_msec : ~0ULL,
			},
			.decoder = {
				.hw_accel = x->args.hwaccel,
				.q_size = 4,
			},
			.video = {
				.zoom = (ushort)x->args.zoom,
				.fullscreen = x->fullscreen,
			},
			.audio = {
				.volume = (u_char)x->args.volume,
				.mute = x->args.mute,
			},
			.no_display = x->args.no_display,
			.no_sound = x->args.no_sound,
			.pause_on_end = x->args.pause_on_end,
			.print_time = x->args.perf,
		},
		.parallel = x->args.parallel,
		.repeat = x->args.repeat,
	};
	x->q->create(&qc);

	const char **in = x->args.input.ptr;
	if (x->args.input.len && x->args.autodir) {
		ffstr path, name;
		ffpath_splitpath_str(FFSTR_Z(in[0]), &path, &name);
		if (!path.len)
			path = FFSTR_Z(".");
		char *pathz = ffsz_dupstr(&path);
		autodir_read(pathz, name); // Add all files from the first input file's directory
		ffmem_free(pathz);
		x->q->add(in + 1, x->args.input.len - 1);
	} else {
		x->q->add(in, x->args.input.len);
	}

	x->q->play(0);
}

static void exe_signal(fav_track *trk, uint cmd, uint flags)
{
	switch (cmd) {
	case FAV_TRACK_QUIT:
		x->exit_code = 0;
		x->core->stop();
		break;

	default:
		x->q->signal(trk, cmd, flags);
	}
}

int main(int argc, char **argv)
{
	x = ffmem_new(struct exe);
	x->exit_code = 1;
	logs(&x->log);
	if (conf(argv[0])) goto end;

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
	if (x->core)
		core_destroy();
	exe_free(x);
	return x->exit_code;
}
