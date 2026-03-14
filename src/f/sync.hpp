/** favia: AV sync CU
2025, Simon Zolin */

#include <util/avsync.hpp>

struct syx {
	avsync sync;
};

static int cu_sync_open(fav_track *t)
{
	struct syx *x = fav_track_allocT(t, struct syx);
	t->syx = x;
	t->sync = &x->sync;
	x->sync.reset();
	return FAV_CU_FWD;
}

static void cu_sync_close(fav_track *t)
{
	struct syx *x = t->syx;
	x->~syx();
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
		r = x->sync.read(&n);
		dbglog(t, "r:%u  VQ:%u  AQ:%u", r, t->vq->length(), t->aq->length());
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
			x->sync.reset();
		break;

	case FAV_TRACK_SEEK:
		x->sync.reset();
		break;

	default:
		return -1;
	}
	return 0;
}

FF_EXTERN const struct fav_track_cu trk_cu_sync = { "sync", cu_sync_open, cu_sync_close, cu_sync, cu_sync_ctl };
