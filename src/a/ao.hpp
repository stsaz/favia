/** favia: audio output CU
2025, Simon Zolin */

#include <a/audio.hpp>
#include <util/avqueue.hpp>

static int format_ffa_avf(int avf, bool *interleaved)
{
	static const struct {
		int av;
		ushort ffa, il;
	} map[] = {
		{ AV_SAMPLE_FMT_S16,	FFAUDIO_F_INT16, 1 },
		{ AV_SAMPLE_FMT_S16P,	FFAUDIO_F_INT16, 0 },
		{ AV_SAMPLE_FMT_FLT,	FFAUDIO_F_FLOAT32, 1 },
		{ AV_SAMPLE_FMT_FLTP,	FFAUDIO_F_FLOAT32, 0 },
	};
	for (uint i = 0;  i < FF_COUNT(map);  i++) {
		if (avf == map[i].av) {
			*interleaved = map[i].il;
			return map[i].ffa;
		}
	}
	return -1;
}

struct aox {
	audio a;
	uint avolume;
};

static bool a_open(struct aox *x, fav_track *t)
{
	int r;
	bool interleaved;
	if (0 > (r = format_ffa_avf(t->audio_format, &interleaved))) {
		errlog(t, "AVF audio format is not supported: %d", t->audio_format);
		return 0;
	}

	ffaudio_conf ac = {};
	ac.format = r;
	ac.sample_rate = t->audio_rate;
	ac.channels = t->audio_channels;
	return x->a.open(ac, interleaved);
}

static void cu_a_mute_toggle(struct aox *x, fav_track *t)
{
	uint m = !(t->state & TRK_MUTE);
	if (m)
		t->state |= TRK_MUTE;
	else
		t->state &= ~TRK_MUTE;
	infolog(t, "Mute: %u", m);
	x->a.volume((m) ? 0 : x->avolume);
}

static void cu_a_volume(struct aox *x, fav_track *t, uint n)
{
	x->avolume = n;
	infolog(t, "Volume: %u%%", x->avolume);
	x->a.volume(x->avolume);
}

static void cu_a_close(fav_track *t)
{
	struct aox *x = t->aox;
	if (!x) return;
	x->~aox();
	fav_track_free(t, x);
}

static int cu_a_open(fav_track *t)
{
	if (t->conf.no_sound)
		return FAV_CU_FWD;

	struct aox *x = fav_track_allocT(t, struct aox);
	new (x) (struct aox);
	// fav_track_free()
	t->aox = x;

	if ((t->conf.no_sound = !a_open(x, t)))
		return FAV_CU_FWD;

	t->sync->audio((x->a.buf_len_msec) ? x->a.buf_len_msec : 500);

	x->avolume = 100;
	if (t->conf.audio.volume)
		cu_a_volume(x, t, t->conf.audio.volume);

	if (t->conf.audio.mute)
		cu_a_mute_toggle(x, t);

	return FAV_CU_FWD;
}

static int cu_a_play(fav_track *t)
{
	struct aox *x = t->aox;
	int r;
	bool complete = 0;
	qframe *f;
	const AVFrame *avf;

	if (t->conv.f & FAV_CF_REVERSE)
		return FAV_CU_BACK;

	if (t->conf.no_sound)
		return FAV_CU_FWD;

	if (t->audio_stream_switched) {
		t->audio_stream_switched = 0;
		x->a.~audio();
		ffmem_zero_obj(&x->a);
		new (&x->a) audio();
		t->conf.no_sound = !a_open(x, t);
		x->a.volume(x->avolume);

		t->sync->reset();
		t->sync->master = 0;
		if (!t->conf.no_sound)
			t->sync->audio((x->a.buf_len_msec) ? x->a.buf_len_msec : 500);
		return FAV_CU_BACK;
	}

	if (!(t->frame_flags & FAV_F_AUDIO))
		return FAV_CU_FWD;

	if (!(f = t->aq->peek())) {
		t->want_input |= FAV_F_AUDIO;
		goto end;
	}

	avf = f->frame;
	bool interleaved;
	if (0 > (r = format_ffa_avf(avf->format, &interleaved))) {
		errlog(t, "AV audio format is not supported: %d", avf->format);
		complete = 1;
		goto end;
	}
	if (!(r == x->a.iaf.format
		&& interleaved == x->a.iaf.interleaved
		&& avf->sample_rate == x->a.iaf.rate
		&& avf->ch_layout.nb_channels == x->a.iaf.channels)) {
		errlog(t, "Audio format has been changed");
		complete = 1;
		goto end;
	}

	{
	xxstr data((interleaved) ? (char*)avf->data[0] : (char*)avf->data, avf->nb_samples * x->a.sample_size);
	if (!(r = x->a.write(data))) {
		complete = 1;
	} else if (r == 1) {
		t->sync->a_start();
	}
	}

	if (complete) {
		t->sync->frame(f->ts, f->dur, 1);
	}

end:
	if (complete) {
		f = t->aq->read();
		if (!t->video_width)
			t->cur_pos_msec = t->sync->pos() / 1000;
		f->unref();
		t->input_full &= ~FAV_F_AUDIO;
	}
	return FAV_CU_FWD;
}

static int cu_a_ctl(fav_track *t, uint cmd, uint flags)
{
	struct aox *x = t->aox;
	if (!x)
		return 0;

	int r;
	switch (cmd) {
	case FAV_TRACK_PAUSE:
		if (!t->conf.no_sound)
			x->a.pause(!!(t->state & TRK_PAUSED));
		break;

	case FAV_TRACK_SEEK:
		if (!(flags & FAV_TRACK_SEEK_LOOP)
			&& !t->conf.no_sound)
			x->a.clear();
		break;

	case FAV_TRACK_VOLUME:
		if (flags & FAV_TRACK_VOL_MUTE) {
			cu_a_mute_toggle(x, t);
			break;
		}

		if (flags & 1)
			r = ffmin(x->avolume + core->conf.volume_step_pct, 125);
		else
			r = ffmax((int)x->avolume - core->conf.volume_step_pct, 0);
		cu_a_volume(x, t, r);
		break;

	default:
		return 1;
	}
	return 0;
}

FF_EXTERN const struct fav_track_cu trk_cu_ao = { "a-play", cu_a_open, cu_a_close, cu_a_play, cu_a_ctl };
