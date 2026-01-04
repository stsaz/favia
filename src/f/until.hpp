/** favia: 'until' parameter handler
2025, Simon Zolin */

static int cu_until(fav_track *t)
{
	if (t->conv.f & FAV_CF_REVERSE)
		return FAV_CU_BACK;

	if (t->cur_pos_msec >= t->conf.input.until_msec) {
		dbglog(t, "'until' time reached");
		return FAV_CU_FIN;
	}
	return FAV_CU_FWD;
}

FF_EXTERN const struct fav_track_cu trk_cu_until = { "until", NULL, NULL, cu_until };
