/** favia
2025, Simon Zolin */

#include <favia.h>
#include <util/taskqueue.h>
#include <ffbase/atomic.h>

struct fav_core {
	fftaskqueue tq;
	uint stop;
};

static struct fav_core *cx;
const fav_core_if *core;
FF_EXTERN void tracks_init();
FF_EXTERN int tracks_run();

#include <ui.h>

#ifdef FF_WIN
static inline int ffthread_usleep(unsigned usec)
{
	Sleep(usec / 1000);
	return 0;
}

#else
static inline int ffthread_usleep(unsigned usec)
{
	struct timespec ts = {
		.tv_sec = usec / 1000000,
		.tv_nsec = (usec % 1000000) * 1000,
	};
	return nanosleep(&ts, NULL);
}
#endif

int core_run() {
	dbglog("entering worker loop");
	while (!FFINT_READONCE(cx->stop)) {

		fftaskqueue_run(&cx->tq);

		int n = tracks_run();
		if (n > 0) {
			n = ffmin(n, 20000);
			dbglog("sleep %uus", n);
			ffthread_usleep(n);
		}

		if (user_events()) {
			return 0;
		}
	}
	dbglog("leaving worker loop");
	return 0;
}

static void core_stop() {
	dbglog("core stop");
	FFINT_WRITEONCE(cx->stop, 1);
}

static void core_task(uint flags, fav_task *t, fav_task_func func, void *param) {
	if (fftaskqueue_active(&cx->tq, (fftask*)t))
		fftaskqueue_del(&cx->tq, (fftask*)t);

	if (flags == FAV_TASK_DEL) {
		return;
	}

	fftaskqueue_post4(&cx->tq, (fftask*)t, func, param);
	dbglog("task add: %p %p", t, func);
}

#ifndef FAV_VERSION_STR
	#define FAV_VERSION_STR  "0-test"
#endif

extern const fav_track_if tif;
static fav_core_if cif = {
	.version_str = FAV_VERSION_STR,
	.track = &tif,
	.stop = core_stop,
	.task = core_task,
};

fav_core_if* core_init(struct fav_core_conf *conf) {
	cif.log = conf->log;
	cif.conf = *conf;
	cx = ffmem_new(struct fav_core);
	fftaskqueue_init(&cx->tq);
	core = &cif;
	tracks_init();
	return &cif;
}

void core_destroy() {
	ffmem_free(cx);  cx = NULL;
}
