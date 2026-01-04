/** favia: AV decoding CU
2025, Simon Zolin */

#define AVQ_SIZE_MAX  128

static int cu_avdec_open(fav_track *t)
{
	if (!t->dec.hwaccel_enable(t->conf.decoder.hw_accel))
		warnlog(t, "HW decoding is inactive: %s", t->dec.error());

	t->vq = queue_alloc(t->conf.decoder.q_size);
	t->aq = queue_alloc(t->conf.decoder.q_size);
	return FAV_CU_FWD;
}

static void cu_avdec_close(fav_track *t)
{
	t->vq->free();
	t->aq->free();
}

static int cu_av_decode(fav_track *t)
{
	int r;
	qframe *f;

	if (t->want_input) {
		if (t->state & TRK_FIN) {
			dbglog(t, "finished");
			return FAV_CU_FIN;
		}

		dbglog(t, "VQ or AQ is empty");

		if ((t->want_input & FAV_F_VIDEO) && (t->input_full & FAV_F_AUDIO)) {
			if (t->aq->cap < AVQ_SIZE_MAX) {
				t->aq = queue_realloc(t->aq, ffmin(t->aq->cap * 2, AVQ_SIZE_MAX));
				t->input_full &= ~FAV_F_AUDIO;
			} else {
				warnlog(t, "need very large AQ");
				t->have_pkt = 0;
			}
		}

		if ((t->want_input & FAV_F_AUDIO) && (t->input_full & FAV_F_VIDEO)) {
			if (t->vq->cap < AVQ_SIZE_MAX) {
				t->vq = queue_realloc(t->vq, ffmin(t->vq->cap * 2, AVQ_SIZE_MAX));
				t->input_full &= ~FAV_F_VIDEO;
			} else {
				warnlog(t, "need very large VQ");
				t->have_pkt = 0;
			}
		}

		t->want_input = 0;
	}

	if (!t->have_pkt)
		return (t->read_fin) ? FAV_CU_FWD : FAV_CU_BACK;

	if (t->pkt.stream_index() == t->dec.video_stream) {

		if (!(f = t->vq->push())) {
			dbglog(t, "video queue full");
			t->input_full = FAV_F_VIDEO;
			return FAV_CU_FWD;
		}

		if ((r = t->dec.video_decode(t->pkt, &f->frame))) {
			t->vq->pop();
			if (r > 0)
				goto done; // this packet is completely processed
			errlog(t, "Video packet decode: %s", t->dec.error());
			return FAV_CU_WARN;
		}

		f->ts = t->dec.video_time_base() * t->pkt.pts() * 1000000;
		f->dur = t->dec.video_time_base() * t->pkt.duration() * 1000000;

	} else if (t->pkt.stream_index() == t->dec.audio_stream
		&& !t->conf.no_sound) {

		if (!(f = t->aq->push())) {
			dbglog(t, "audio queue full");
			t->input_full = FAV_F_AUDIO;
			return FAV_CU_FWD;
		}

		if ((r = t->dec.audio_decode(t->pkt, &f->frame))) {
			t->aq->pop();
			if (r > 0)
				goto done; // this packet is completely processed
			errlog(t, "Audio packet decode: %s", t->dec.error());
			return FAV_CU_WARN;
		}

		f->ts = t->dec.audio_time_base() * t->pkt.pts() * 1000000;
		f->dur = t->dec.audio_time_base() * t->pkt.duration() * 1000000;

	} else {
		return FAV_CU_BACK;
	}

	return FAV_CU_FWD;

done:
	if (t->read_fin) {
		t->state |= TRK_FIN; // TODO
		return FAV_CU_FWD;
	}
	return FAV_CU_BACK;
}

static int cu_av_ctl(fav_track *t, uint cmd, uint flags)
{
	int r;
	switch (cmd) {
	case FAV_TRACK_SEEK:
		t->vq->reset();
		t->aq->reset();
		t->input_full = 0;
		t->want_input = 0;
		t->state &= ~TRK_FIN;
		break;

	case FAV_TRACK_AUDIO_NEXT:
		if ((r = t->dec.audio_stream_switch())) {
			if (r < 0)
				warnlog(t, "Switching audio streams: %s", t->dec.error());
			break;
		}

		infolog(t, "Switched to next audio stream");

		t->aq->reset();
		t->vq->reset();
		t->input_full = 0;
		t->have_pkt = 0;
		t->audio_stream_switched = 1;
		break;

	default:
		return -1;
	}
	return 0;
}

FF_EXTERN const struct fav_track_cu trk_cu_decode = { "decode", cu_avdec_open, cu_avdec_close, cu_av_decode, cu_av_ctl };
