/** favia: AV decoding CU
2025, Simon Zolin */

#include <util/avqueue.h>

#define AVQ_SIZE_MAX  128

static int cu_avdec_open(fav_track *t)
{
	if (!t->static_pic
		&& ffmpeg_dec_hwaccel_enable(t->dec, t->conf.decoder.hw_accel))
		warnlog(t, "HW decoding is inactive: %s", ffmpeg_dec_error(t->dec));

	t->vq = avqueue_alloc(t->conf.decoder.q_size);
	t->aq = avqueue_alloc(t->conf.decoder.q_size);
	return FAV_CU_FWD;
}

static void cu_avdec_close(fav_track *t)
{
	avqueue_free(t->vq);
	avqueue_free(t->aq);
}

static int cu_av_decode(fav_track *t)
{
	int r;
	struct qframe *f;
	ffmpeg_packet *pkt = t->pkt;

	if (t->want_input) {
		dbglog(t, "VQ or AQ is empty");

		if ((t->want_input & FAV_F_VIDEO) && (t->input_full & FAV_F_AUDIO)) {
			if (t->aq->cap < AVQ_SIZE_MAX) {
				t->aq = avqueue_realloc(t->aq, ffmin(t->aq->cap * 2, AVQ_SIZE_MAX));
				t->input_full &= ~FAV_F_AUDIO;
			} else {
				warnlog(t, "need very large AQ");
				t->have_pkt = 0;
			}
		}

		if ((t->want_input & FAV_F_AUDIO) && (t->input_full & FAV_F_VIDEO)) {
			if (t->vq->cap < AVQ_SIZE_MAX) {
				t->vq = avqueue_realloc(t->vq, ffmin(t->vq->cap * 2, AVQ_SIZE_MAX));
				t->input_full &= ~FAV_F_VIDEO;
			} else {
				warnlog(t, "need very large VQ");
				t->have_pkt = 0;
			}
		}

		t->want_input = 0;
	}

	if (!t->have_pkt)
		goto done;

	if (ffmpeg_packet_stream_index(pkt) == t->dec->video_stream) {

		if (!(f = core->track->avq_push(t->vq))) {
			dbglog(t, "video queue full");
			t->input_full = FAV_F_VIDEO;
			return FAV_CU_FWD;
		}

		if ((r = ffmpeg_dec_video_decode(t->dec, pkt, &f->f))) {
			avqueue_pop(t->vq);
			if (r > 0)
				goto done; // this packet is completely processed
			warnlog(t, "Video packet decode: %s", ffmpeg_dec_error(t->dec));
			goto done;
		}

		f->ts = ffmpeg_dec_video_time_base(t->dec) * ffmpeg_packet_pts(pkt) * 1000000;
		f->dur = ffmpeg_dec_video_time_base(t->dec) * ffmpeg_packet_duration(pkt) * 1000000;

	} else if (ffmpeg_packet_stream_index(pkt) == t->dec->audio_stream
		&& !t->conf.no_sound) {

		if (!(f = core->track->avq_push(t->aq))) {
			dbglog(t, "audio queue full");
			t->input_full = FAV_F_AUDIO;
			return FAV_CU_FWD;
		}

		if ((r = ffmpeg_dec_audio_decode(t->dec, pkt, &f->f))) {
			avqueue_pop(t->aq);
			if (r > 0)
				goto done; // this packet is completely processed
			warnlog(t, "Audio packet decode: %s", ffmpeg_dec_error(t->dec));
			goto done;
		}

		f->ts = ffmpeg_dec_audio_time_base(t->dec) * ffmpeg_packet_pts(pkt) * 1000000;
		f->dur = ffmpeg_dec_audio_time_base(t->dec) * ffmpeg_packet_duration(pkt) * 1000000;

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
		avqueue_reset(t->vq);
		avqueue_reset(t->aq);
		t->input_full = 0;
		t->want_input = 0;
		t->state &= ~TRK_FIN;
		break;

	case FAV_TRACK_AUDIO_NEXT:
		if ((r = ffmpeg_dec_audio_stream_switch(t->dec))) {
			if (r < 0)
				warnlog(t, "Switching audio streams: %s", ffmpeg_dec_error(t->dec));
			break;
		}

		infolog(t, "Switched to next audio stream");

		avqueue_reset(t->aq);
		avqueue_reset(t->vq);
		t->input_full = 0;
		t->have_pkt = 0;
		t->audio_stream_switched = 1;
		break;

	default:
		return -1;
	}
	return 0;
}

const struct fav_track_cu trk_cu_decode = { "decode", cu_avdec_open, cu_avdec_close, cu_av_decode, cu_av_ctl };
