/** favia: Core
2025, Simon Zolin */

#include <favia.h>

#define TIMER_INT_MS  1
#define TIMER_UI_MS  20

#define ZZKQ_LOG_SYSERR  (FAV_LOG_ERROR | FAV_LOG_SYSERR)
#define ZZKQ_LOG_ERR  FAV_LOG_ERROR
#define ZZKQ_LOG_DEBUG  FAV_LOG_DEBUG
#define ZZKQ_LOG_EXTRA  FAV_LOG_EXTRA

#define FFTASKQUEUE_LOG_DEBUG  FAV_LOG_EXTRA

#include <util/kq-tq.h>
#include <util/kq-timer.h>
#include <ffsys/time.h>
#include <ffsys/timer.h>
#include <ffbase/atomic.h>

const fav_core_if *core;
#include <ui.h>

struct fav_core {
	struct zzkq kq;
	fav_task task;

	fftaskqueue tq;
	struct zzkq_tq kq_tq;

	struct zzkq_timer kq_timer;
	fftimerqueue tmq;
	fftime now;
	uint now_msec;
	fav_timer ui_tmr, job_tmr;
};

static struct fav_core *cx;
extern void tracks_init();
extern void tracks_destroy();
extern int tracks_run();
extern void vomo_destroy();
extern void q_init();


static void core_task(uint flags, fav_task *ft, fav_task_func func, void *param)
{
	fftask *t = (void*)ft;
	if (flags == FAV_TASK_DEL) {
		fftaskqueue_del(&cx->tq, (fftask*)t);
		return;
	}

	t->handler = func;
	t->param = param;
	dbglog("task add: %p %p", t, func);
	zzkq_tq_post(&cx->kq_tq, t);
}

static void core_timer(fav_timer *t, int interval_msec, fav_task_func func, void *param)
{
	dbglog("timer: %p %d %p %p", t, interval_msec, func, param);

	if (interval_msec == 0) {
		fftimerqueue_remove(&cx->tmq, t);
		return;
	}

	fftimerqueue_add(&cx->tmq, t, cx->now_msec, interval_msec, func, param);
}

static void tmr_func(void *param)
{
	fftimer_consume(cx->kq_timer.timer);
	cx->now = fftime_monotonic();
	cx->now_msec = fftime_to_msec(&cx->now);
	uint n = fftimerqueue_process(&cx->tmq, cx->now_msec);
	if (n != 0)
		dbglog("processed %u timers", n);
}

static void core_stop()
{
	zzkq_stop(&cx->kq);
}

#ifndef FAV_VER_STR
	#define FAV_VER_STR  "test"
#endif

extern const fav_track_if tif;
static fav_core_if cif = {
	.version_str = FAV_VER_STR,
	.track = &tif,
	.stop = core_stop,
	.task = core_task,
	.timer = core_timer,
};

fav_core_if* core_init(struct fav_core_conf *conf)
{
	cif.log = conf->log;
	cif.conf = *conf;
	core = &cif;
	cx = ffmem_new(struct fav_core);
	zzkq_init(&cx->kq);
	fftimerqueue_init(&cx->tmq);
	zzkq_timer_init(&cx->kq_timer);

	struct zzkq_conf kc = {
		.log = {
			.level = cif.conf.log_level,
			.func = cif.conf.log,
			.obj = NULL,
			.ctx = "core",
		},
		.max_objects = 100,
		.events_wait = 64,
	};
	if (zzkq_create(&cx->kq, &kc))
		return NULL;

	fftaskqueue_init(&cx->tq);
	cx->tq.log.level = cif.conf.log_level;
	cx->tq.log.func = cif.conf.log;
	cx->tq.log.obj = NULL;
	cx->tq.log.ctx = "core";
	if (zzkq_tq_attach(&cx->kq_tq, cx->kq.kq, &cx->tq)) {
		syserrlog("zzkq_tq_attach");
		return NULL;
	}

	if (zzkq_timer_create(&cx->kq_timer)) {
		syserrlog("timer create");
		return NULL;
	}
	if (zzkq_timer_start(&cx->kq_timer, cx->kq.kq, TIMER_INT_MS, tmr_func, cx)) {
		syserrlog("timer start");
		return NULL;
	}
	dbglog("ktimer: %umsec", TIMER_INT_MS);

	tmr_func(NULL);
	tracks_init();
	q_init();
	return &cif;
}

void core_destroy()
{
	zzkq_tq_detach(&cx->kq_tq, cx->kq.kq);
	zzkq_timer_destroy(&cx->kq_timer, cx->kq.kq);
	zzkq_destroy(&cx->kq);
	tracks_destroy();
	vomo_destroy();
	ffmem_free(cx);  cx = NULL;
}

static void ui_tmr(void *param)
{
	user_events();
}

static void jobs_run(void *param)
{
	int n;
	for (;;) {
		n = tracks_run();
		if (n < 2 * TIMER_INT_MS * 1000) {
			// Near a track's deadline, retry after delay
			extralog("sleep %uus", n);
			ffthread_usleep(n);
		} else {
			break; // No immediate work to do -> return to KQ
		}

		fftime t = fftime_monotonic();
		fftime_sub(&t, &cx->now);
		uint budget_usec = TIMER_UI_MS * 1000;
		if (fftime_to_usec(&t) >= budget_usec)
			break;
	}

	n = (n >= 1000) ? n / 1000 : 1;
	n = ffmin(n, TIMER_UI_MS);
	core_timer(&cx->job_tmr, -n, jobs_run, cx);
}

int core_run()
{
	core_timer(&cx->ui_tmr, TIMER_UI_MS, ui_tmr, cx);
	core_task(0, &cx->task, jobs_run, cx);
	return zzkq_run(&cx->kq);
}
