/** favia: executor
2025, Simon Zolin */

#include <favia.h>
#include <exe/log.h>
#include <ffsys/signal.h>
#include <ffsys/globals.h>

extern fav_core_if* core_init();
extern void core_destroy();
extern void fav_core_run(fav_track *trk);

struct exe {
	const char *fn;
	const char *hwaccel;
	u_char debug;
	u_char no_display;
	u_char no_sound;
	uint64_t seek_msec;

	fav_track *trk;
};
static struct exe *x;
static fav_core_if *core;

#include <exe/cmd.h>

static void version_print()
{
	ffstdout_fmt("favia v%s (" "linux" "-" "amd64" ")\n"
		, core->version_str);
}

static void sig(struct ffsig_info *i)
{
	core->track->stop(x->trk);
	core->stop();
}

int main(int argc, char **argv)
{
	int r = 1;
	lg = ffmem_new(struct logger);
	x = ffmem_new(struct exe);
	if (cmd(argc, argv)) goto end;
	lg->debug = x->debug;

	struct fav_core_conf cc = {
		.log = exe_log,
	};
	core = core_init(&cc);
	core->debug = x->debug;

	version_print();
	static const uint sigs[] = { FFSIG_INT };
	ffsig_subscribe(sig, sigs, FF_COUNT(sigs));

	struct fav_track_conf conf = {
		.input.url = x->fn,
		.input.seek_msec = x->seek_msec,
		.decoder.hw_accel = x->hwaccel,
		.no_display = x->no_display,
		.no_sound = x->no_sound,
	};
	x->trk = core->track->create(&conf);

	fav_core_run(x->trk);
	r = core->track->cmd(x->trk, FAV_TRACK_STATUS);

end:
	core_destroy();
	ffmem_free(x);
	return r;
}
