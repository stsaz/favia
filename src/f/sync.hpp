/** favia: AV sync CU
2025, Simon Zolin */

static int cu_sync_open(fav_track *t)
{
	t->sync.reset();
	return FAV_CU_FWD;
}

static int cu_sync(fav_track *t)
{
	if (t->want_input) {
		if (t->state & TRK_FIN) {
			dbglog(t, "finished");
			return FAV_CU_FIN;
		}
		return FAV_CU_BACK;
	}

	int r = 0;
	int n = 0x7fffffff;
	if (!(t->state & TRK_PAUSED)) {
		r = t->sync.read(&n);
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
	switch (cmd) {
	case FAV_TRACK_PAUSE:
		if (!(t->state & TRK_PAUSED))
			t->sync.reset();
		break;

	case FAV_TRACK_SEEK:
		t->sync.reset();
		break;

	default:
		return -1;
	}
	return 0;
}

FF_EXTERN const struct fav_track_cu trk_cu_sync = { "sync", cu_sync_open, NULL, cu_sync, cu_sync_ctl };
