/** favia
2025, Simon Zolin */

#include <favia.h>
#include <ffbase/atomic.h>

struct fav_core {
	uint stop;
};
static struct fav_core *cx;
const fav_core_if *core;
FF_EXTERN int track_run(fav_track *t);

#include <ui.h>

void fav_core_run(fav_track *trk) {
	while (!FFINT_READONCE(cx->stop)) {

		int n = track_run(trk);
		if (n > 0) {
			n = ffmin(n, 20000);
			dbglog("sleep %uus", n);
			usleep(n);
		}

		if (user_events(trk)) {
			break;
		}
	}
}

static void core_stop() {
	FFINT_WRITEONCE(cx->stop, 1);
}

#ifndef FAV_VERSION_STR
	#define FAV_VERSION_STR  "0-test"
#endif

extern const fav_track_if tif;
static fav_core_if cif = {
	.version_str = FAV_VERSION_STR,
	.track = &tif,
	.stop = core_stop,
};

fav_core_if* core_init(struct fav_core_conf *conf) {
	cif.log = conf->log;
	cx = ffmem_new(struct fav_core);
	core = &cif;
	return &cif;
}

void core_destroy() {
	ffmem_free(cx);  cx = NULL;
}
