/** favia: executor
2025, Simon Zolin */

#include <favia.h>
#include <util/util.hpp>
#include <ffsys/signal.h>
#include <ffsys/globals.h>

FF_EXTERN fav_core_if* core_init(struct fav_core_conf *conf);
FF_EXTERN void core_destroy();
FF_EXTERN int core_run();
static fav_core_if *core;
static void exe_signal(fav_track *trk, uint cmd, uint flags);

#include <exe/log.h>

struct exe {
	fav_task task;
	uint cursor, n_tracks;
	uint exit_code;

	const char *hwaccel;
	u_char debug;
	u_char mute;
	u_char no_display;
	u_char no_sound;
	u_char pause_on_end;
	u_char repeat;
	uint parallel;
	uint volume;
	uint zoom;
	uint64_t seek_msec, until_msec;
	xxvec input; // const char*[]
	struct ffargs *cmd;

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

#include <exe/cmd.hpp>

static void core_open() {
	struct fav_core_conf cc = {
		.debug = x->debug,
		.log = exe_log,
		.signal = exe_signal,
	};
	core = core_init(&cc);
}

static void version_print() {
	ffstdout_fmt("favia v%s (" "linux" "-" "amd64" ")\n"
		, core->version_str);
}

static void sig(struct ffsig_info *i) {
	core->stop();
}

static void signals() {
	static const uint sigs[] = { FFSIG_INT };
	ffsig_subscribe(sig, sigs, FF_COUNT(sigs));
}

static fav_track* trk_new(const char *url) {
	struct fav_track_conf tc = {
		.input = {
			.url = url,
			.seek_msec = x->seek_msec,
			.until_msec = (x->until_msec) ? x->until_msec : ~0ULL,
		},
		.decoder = {
			.hw_accel = x->hwaccel,
		},
		.video = {
			.zoom = (ushort)x->zoom,
		},
		.audio = {
			.volume = (u_char)x->volume,
			.mute = x->mute,
		},
		.no_display = x->no_display,
		.no_sound = x->no_sound,
		.pause_on_end = x->pause_on_end,
	};
	return core->track->create(&tc);
}

static void exe_iq_start(void *param) {
	exe *x = (exe*)param;

	for (;;) {
		fav_track *t = trk_new(*x->input.at<char*>(x->cursor));
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
		core->stop();
		return;
	}

	if (next)
		exe_iq_start(x);
}

int main(int argc, char **argv)
{
	int r = 1;
	x = ffmem_new(struct exe);
	if (cmd(argc, argv)) goto end;

	core_open();
	version_print();
	signals();

	core->task(FAV_TASK_ADD, &x->task, exe_iq_start, x);
	core_run();
	r = x->exit_code;

end:
	core_destroy();
	x->~exe();
	ffmem_free(x);
	return r;
}
