/** favia: executor
2025, Simon Zolin */

#include <favia.h>
#include <util/util.hpp>
#include <util/log.h>
#include <ffsys/signal.h>
#include <ffsys/dirscan.h>
#include <ffsys/globals.h>
#include <ffbase/fntree.h>

FF_EXTERN fav_core_if* core_init(struct fav_core_conf *conf);
FF_EXTERN void core_destroy();
FF_EXTERN int core_run();
static fav_core_if *core;
static void exe_signal(fav_track *trk, uint cmd, uint flags);

struct exe {
	fav_task task;
	uint cursor, n_tracks;
	uint exit_code;
	struct zzlog log;

	const char *cmd_line;
	const char *hwaccel;
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
	xxvec input; // const char*[]
	struct ffargs *cmd;

	int dir_read(const char *fn, uint ins_pos) {
		int rc = 1;
		ffdirscan ds = {};
		fntree_block *root = NULL, *blk;
		char *fpath = NULL;
		fntree_cursor cur = {};

		if (ffdirscan_open(&ds, fn, 0))
			goto end;

		if (!(root = fntree_from_dirscan(FFSTR_Z(fn), &ds, 0)))
			goto end;
		blk = root;
		ffdirscan_close(&ds);

		for (;;) {
			fntree_entry *e;
			if (!(e = fntree_cur_next_r_ctx(&cur, &blk)))
				break;

			ffstr path = fntree_path(blk);
			ffstr name = fntree_name(e);
			ffmem_free(fpath);
			fpath = ffsz_allocfmt("%S%c%S", &path, FFPATH_SLASH, &name);

			xxfileinfo fi;
			if (fffile_info_path(fpath, &fi.info))
				continue;
			if (fi.dir()) {
				ffmem_zero_obj(&ds);
				if (ffdirscan_open(&ds, fpath, 0))
					continue;

				ffstr_setz(&path, fpath);
				if (!(blk = fntree_from_dirscan(path, &ds, 0)))
					continue;
				ffdirscan_close(&ds);

				fntree_attach(e, blk);
				continue;
			}

			this->input.insert<char*>(fpath, ins_pos++);
			dbglog("input queue: add \"%s\"", fpath);
			fpath = NULL;
		}

		rc = 0;

	end:
		ffmem_free(fpath);
		ffdirscan_close(&ds);
		fntree_free_all(root);
		return rc;
	}

	const char* input_get() {
		while (this->cursor < this->input.len) {
			const char *fn = *this->input.at<char*>(this->cursor);
			if (xxfile::info(fn).dir()) {
				this->input.remove<char*>(this->cursor, 1);
				dir_read(fn, this->cursor);
				continue;
			}
			return fn;
		}
		return NULL;
	}

	int cursor_move(int delta) {
		int i = cursor + delta;
		if (i < 0 || i >= input.len) {
			if (!repeat)
				return 1;
			if (i < 0)
				i = input.len - 1;
			else
				i = 0;
		}
		cursor = i;
		dbglog("cursor: %u", cursor);
		return 0;
	}
};
static struct exe *x;

#include <exe/log.h>
#include <exe/cmd.hpp>

static void core_open() {
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
	core = core_init(&cc);
}

#ifdef FF_WIN
#define OS_NAME "windows"
#else
#define OS_NAME "linux"
#endif
static void version_print() {
	ffstdout_fmt("favia v%s (" OS_NAME "-" "amd64" ")\n"
		, core->version_str);
}

static void sig(struct ffsig_info *i) {
	core->stop();
}

static void signals() {
	static const uint sigs[] = { FFSIG_INT };
	ffsig_subscribe(sig, sigs, FF_COUNT(sigs));
}

FF_EXTERN const struct fav_track_cu
	trk_cu_read,
	trk_cu_decode,
	trk_cu_sync,
	trk_cu_vo,
	trk_cu_ao,
	trk_cu_until;

static const struct fav_track_cu* trk_cu_set_play[] = {
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

static fav_track* trk_new(const char *url) {
	struct fav_track_conf tc = {
		.conveyor = trk_cu_set_play,
		.input = {
			.url = url,
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
	};
	return core->track->create(&tc);
}

static void exe_iq_start(void *param) {
	exe *x = (exe*)param;

	for (;;) {
		const char *fn = x->input_get();
		if (!fn)
			break;

		fav_track *t = trk_new(fn);
		x->n_tracks++;
		if (!x->parallel)
			break;

		if (x->n_tracks == x->parallel
			|| x->cursor_move(1))
			break;
	}
}

static void exe_signal(fav_track *trk, uint cmd, uint flags) {
	uint next = 0, close = 1;
	switch (cmd) {
	case FAV_TRACK_NEXT:
		if (x->cursor_move((flags & 1) ? 1 : -1))
			return;
		next = 1; // stop this track, start next
		break;

	case FAV_TRACK_FULLSCREEN:
		x->fullscreen = !!(flags & 1);
		return;

	case FAV_TRACK_WINDOW:
		if (!x->parallel)
			x->parallel = 1;
		x->parallel += (flags & 1) ? 1 : -1;
		if (x->parallel <= 1)
			x->parallel = 0;
		if (x->n_tracks < x->parallel) {
			next = 1; // add window
			close = 0;
		} else if (x->n_tracks == 1 || x->n_tracks == x->parallel) {
			return;
		} else {
			// remove window
		}
		break;

	case FAV_TRACK_STOP:
		if (!flags) {
			if (x->parallel)
				x->parallel--;
			if (!x->parallel) {
				x->exit_code = core->track->cmd(trk, FAV_TRACK_STATUS);
				cmd = FAV_TRACK_QUIT;
				break;
			}
			// stop this track
		} else {
			if (x->cursor_move(1)) {
				x->exit_code = core->track->cmd(trk, FAV_TRACK_STATUS);
				cmd = FAV_TRACK_QUIT;
				break;
			}
			next = 1; // stop this track, start next
		}
		break;
	}

	if (close) {
		core->track->close(trk);
		x->n_tracks--;
	}

	switch (cmd) {
	case FAV_TRACK_QUIT:
		x->exit_code = 0;
		core->stop();
		return;
	}

	if (next)
		exe_iq_start(x);
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

	core->task(FAV_TASK_ADD, &x->task, exe_iq_start, x);
	core_run();

end:
	core_destroy();
	x->~exe();
	ffmem_free(x);
	return x->exit_code;
}
