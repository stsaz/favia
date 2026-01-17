/** favia: AV packet reading CU
2025, Simon Zolin */

struct inx {
	xxfile input;
	fav_track *trk;
};

static int input_read(void *opaque, uint8_t *buf, int size)
{
	struct inx *x = (struct inx*)opaque;
	int r = x->input.read(buf, size);
	if (r == 0) {
		dbglog(x->trk, "read: finished");
		return AVERROR_EOF;
	} else if (r < 0) {
		syserrlog(x->trk, "read");
		return AVERROR(errno);
	}
	dbglog(x->trk, "read %u bytes", r);
	return r;
}

static int seek_method(int w)
{
	switch (w) {
	case SEEK_CUR: return FFFILE_SEEK_CURRENT;
	case SEEK_END: return FFFILE_SEEK_END;
	}
	return FFFILE_SEEK_BEGIN;
}

static int64_t input_seek(void *opaque, int64_t pos, int whence)
{
	struct inx *x = (struct inx*)opaque;

	if (whence == AVSEEK_SIZE) {
		return x->input.info().size();
	}

	dbglog(x->trk, "read: seek: %xD", pos);
	int64_t r = x->input.seek(pos, seek_method(whence));
	if (r < 0)
		return AVERROR(errno);
	return r;
}

static void cu_in_close(fav_track *t)
{
	struct inx *x = t->inx;
	x->~inx();
	fav_track_free(t, x);
}

static int cu_in_open(fav_track *t)
{
	struct inx *x = fav_track_allocT(t, struct inx);
	new (x) (struct inx);
	x->trk = t;
	t->inx = x;

	if (x->input.open(t->conf.input.url, FFFILE_READONLY).null()) {
		syserrlog(t, "Input open: %s", t->conf.input.url);
		cu_in_close(t);
		return FAV_CU_ERROR;
	}

	if (!t->dec.open(input_read, input_seek, x)) {
		errlog(t, "Decoder open: %s", t->dec.error());
		cu_in_close(t);
		return FAV_CU_ERROR;
	}
	assert(t->dec.have_video());

	t->duration_msec = t->dec.duration();

	t->video_width = t->dec.video_width();
	t->video_height = t->dec.video_height();

	if (t->dec.have_audio()) {
		t->audio_format = t->dec.audio_format();
		t->audio_rate = t->dec.audio_rate();
		t->audio_channels = t->dec.audio_channels();
	}
	t->conf.no_sound = !t->audio_rate;

	char buf[64];
	infolog(t, "\"%s\"  %.02FMB  %s  %u streams"
		, t->conf.input.url
		, (double)x->input.info().size() / (1024 * 1024)
		, time_print(t->duration_msec, buf, sizeof(buf))
		, t->dec.streams());

	if (t->conf.input.seek_msec) {
		char buf[64];
		dbglog(t, "seek: %s", time_print(t->conf.input.seek_msec, buf, sizeof(buf)));
		t->dec.seek(t->conf.input.seek_msec * 1000);
	}

	return FAV_CU_FWD;
}

static void cu_in_pkt_log(fav_track *t, const xxffmpeg_packet &pkt)
{
	double tb = (pkt.stream_index() == t->dec.video_stream) ? t->dec.video_time_base() : t->dec.audio_time_base();
	dbglog(t, "frame #%u  stream:%u  pts:%u  ts:%u  size:%u  dur:%u"
		, t->iframe++, pkt.stream_index(), pkt.pts()
		, (int)(tb * pkt.pts() * 1000000)
		, pkt.size(), pkt.duration());
}

static int cu_in_read(fav_track *t)
{
	int r;
	if ((r = t->dec.read(&t->pkt))) {
		if (r < 0) {
			errlog(t, "input read: %s", t->dec.error());
			return FAV_CU_ERROR;
		}
		t->read_fin = 1;
		t->have_pkt = 0;
		return FAV_CU_FWD;
	}
	t->have_pkt = 1;
	cu_in_pkt_log(t, t->pkt);
	return FAV_CU_FWD;
}

static int cu_in_ctl(fav_track *t, uint cmd, uint flags)
{
	int r;
	switch (cmd) {
	case FAV_TRACK_SEEK: {
		if (flags & FAV_TRACK_SEEK_LEAP_PERCENT)
			r = t->duration_msec / 1000 * core->conf.seek_leap_pct / 100;
		else
			r = !(flags & FAV_TRACK_SEEK_LEAP) ? core->conf.seek_step_sec : core->conf.seek_leap_sec;
		if (flags & FAV_TRACK_SEEK_REVERSE)
			r = -r;
		uint64_t pos_msec = t->sync.pos() / 1000 + r * 1000;

		char buf[64];
		infolog(t, "Seek: %s", time_print(pos_msec, buf, sizeof(buf)));
		t->dec.seek(pos_msec * 1000);
		t->have_pkt = 0;
		t->read_fin = 0;
		t->conv.i = 0;
		break;
	}

	default:
		return -1;
	}
	return 0;
}

FF_EXTERN const struct fav_track_cu trk_cu_read = { "input", cu_in_open, cu_in_close, cu_in_read, cu_in_ctl };
