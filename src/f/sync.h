/** favia: AV sync CU
2025, Simon Zolin */

#include <util/avsync.h>

struct syx {
	struct avsync sync;
};

static int cu_sync_open(fav_track *t)
{
	struct syx *x = fav_track_allocT(t, struct syx);
	t->syx = x;
	t->sync = &x->sync;
	avsync_reset(&x->sync);
	return FAV_CU_FWD;
}

static void cu_sync_close(fav_track *t)
{
	struct syx *x = t->syx;
	fav_track_free(t, x);
}

static int cu_sync(fav_track *t)
{
	struct syx *x = t->syx;

	if (t->want_input) {
		if (t->state & TRK_FIN) {
			dbglog(t, "finished");
			return FAV_CU_FIN;
		}
		return FAV_CU_BACK;
	}

	int r = 0;
	int n = 0x7fffffff;
	if (!(t->state & (TRK_PAUSED | TRK_FIN))) {
		r = avsync_read(&x->sync, &n);
		dbglog(t, "r:%u  VQ:%u  AQ:%u", r, avqueue_length(t->vq), avqueue_length(t->aq));
	}

	if (t->redraw) {
		t->redraw = 0;
		dbglog(t, "redraw forced");
		r |= FAV_F_VIDEO | FAV_F_REDRAW;
	}

	if (!r) {
		if (t->input_full || (t->state & (TRK_FIN | TRK_PAUSED))) {
			t->async_ret = n;
			return FAV_CU_ASYNC;
		}
		return FAV_CU_BACK;
	}

	t->frame_flags = r;
	return FAV_CU_FWD;
}

static int cu_sync_ctl(fav_track *t, uint cmd, uint flags)
{
	struct syx *x = t->syx;

	switch (cmd) {
	case FAV_TRACK_PAUSE:
		if (!(t->state & TRK_PAUSED))
			avsync_reset(&x->sync);
		break;

	case FAV_TRACK_SEEK:
		avsync_reset(&x->sync);
		break;

	default:
		return -1;
	}
	return 0;
}

const struct fav_track_cu trk_cu_sync = { "sync", cu_sync_open, cu_sync_close, cu_sync, cu_sync_ctl };
