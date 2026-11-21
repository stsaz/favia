/** favia: AV packet reading CU
2025, Simon Zolin */

#include <util/ffmpeg.h>
#include <ffsys/file.h>

struct inx {
	fffd input;
	ffmpeg_dec dec;
	ffmpeg_packet pkt;
	fav_track *trk;
};

static int input_read(void *opaque, uint8_t *buf, int size)
{
	struct inx *x = (struct inx*)opaque;
	int r = fffile_read(x->input, buf, size);
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
		return fffile_size(x->input);
	}

	dbglog(x->trk, "read: seek: %xD", pos);
	int64_t r = fffile_seek(x->input, pos, seek_method(whence));
	if (r < 0)
		return AVERROR(errno);
	return r;
}

static void cu_in_close(fav_track *t)
{
	struct inx *x = t->inx;
	ffmpeg_packet_destroy(&x->pkt);
	ffmpeg_dec_destroy(&x->dec);
	fffile_close(x->input);
	fav_track_free(t, x);
}

static int cu_in_open(fav_track *t)
{
	struct inx *x = fav_track_allocT(t, struct inx);
	x->input = FFFILE_NULL;
	ffmpeg_dec_init(&x->dec);
	ffmpeg_packet_init(&x->pkt);
	x->trk = t;
	t->inx = x;
	t->dec = &x->dec;
	t->pkt = &x->pkt;

	if (FFFILE_NULL == (x->input = fffile_open(t->conf.input.url, FFFILE_READONLY))) {
		syserrlog(t, "Input open: %s", t->conf.input.url);
		cu_in_close(t);
		return FAV_CU_ERROR;
	}
	dbglog(t, "opened file: %s", t->conf.input.url);

	static u_char config;
	if (!config) {
		config = 1;
		if (core->conf.log_level < FAV_LOG_DEBUG)
			ffmpeg_config(AV_LOG_QUIET);
	}

	if (ffmpeg_dec_open(t->dec, input_read, input_seek, x, 0)) {
		errlog(t, "Decoder open: %s", ffmpeg_dec_error(t->dec));
		cu_in_close(t);
		return FAV_CU_ERROR;
	}
	t->picture = ffmpeg_dec_picture(t->dec);
	assert(ffmpeg_dec_have_video(t->dec));

	t->duration_msec = ffmpeg_dec_duration(t->dec);

	t->video_width = ffmpeg_dec_video_width(t->dec);
	t->video_height = ffmpeg_dec_video_height(t->dec);

	if (ffmpeg_dec_have_audio(t->dec)) {
		t->audio_format = ffmpeg_dec_audio_format(t->dec);
		t->audio_rate = ffmpeg_dec_audio_rate(t->dec);
		t->audio_channels = ffmpeg_dec_audio_channels(t->dec);
	}
	if (!t->audio_rate)
		t->conf.no_sound = 1;
	t->static_pic = ffmpeg_dec_picture(t->dec);

	char buf[64];
	infolog(t, "\"%s\"  %.02FMB  %s  %s  %u streams"
		, t->conf.input.url
		, (double)fffile_size(x->input) / (1024 * 1024)
		, ffmpeg_dec_format_name(t->dec)
		, time_print(t->duration_msec, buf, sizeof(buf))
		, ffmpeg_dec_streams(t->dec));

	if (ffmpeg_dec_have_video(t->dec)) {
		infolog(t, "Video: %s %ux%u"
			, ffmpeg_dec_video_codec_name(t->dec)
			, t->video_width, t->video_height
			);
	}

	if (ffmpeg_dec_have_audio(t->dec)) {
		infolog(t, "Audio: %s %uHz %u channels"
			, ffmpeg_dec_audio_codec_name(t->dec)
			, t->audio_rate
			, t->audio_channels
			);
	}

	if (t->conf.input.seek_msec) {
		char buf[64];
		dbglog(t, "seek: %s", time_print(t->conf.input.seek_msec, buf, sizeof(buf)));
		ffmpeg_dec_seek(t->dec, t->conf.input.seek_msec * 1000);
	}

	return FAV_CU_FWD;
}

static void cu_in_pkt_log(fav_track *t, const ffmpeg_packet *pkt)
{
	double tb = (ffmpeg_packet_stream_index(pkt) == t->dec->video_stream) ? ffmpeg_dec_video_time_base(t->dec)
		: (ffmpeg_packet_stream_index(pkt) == t->dec->audio_stream) ? ffmpeg_dec_audio_time_base(t->dec)
		: 0;
	dbglog(t, "frame #%u  stream:%u  pts:%u  ts:%u  size:%u  dur:%u"
		, t->iframe++, ffmpeg_packet_stream_index(pkt), ffmpeg_packet_pts(pkt)
		, (int)(tb * ffmpeg_packet_pts(pkt) * 1000000)
		, ffmpeg_packet_size(pkt), ffmpeg_packet_duration(pkt));
}

static int cu_in_read(fav_track *t)
{
	struct inx *x = t->inx;
	int r;
	if ((r = ffmpeg_dec_pkt_read(t->dec, &x->pkt))) {
		if (r < 0) {
			errlog(t, "input read: %s", ffmpeg_dec_error(t->dec));
			return FAV_CU_ERROR;
		}
		t->read_fin = 1;
		t->have_pkt = 0;
		return FAV_CU_FWD;
	}
	t->have_pkt = 1;
	cu_in_pkt_log(t, &x->pkt);
	return FAV_CU_FWD;
}

static int cu_in_ctl(fav_track *t, uint cmd, uint flags)
{
	int r;
	switch (cmd) {
	case FAV_TRACK_SEEK: {
		r = (flags & FAV_TRACK_SEEK_JUMP) ? core->conf.seek_jump_sec
			: (flags & FAV_TRACK_SEEK_LEAP) ? core->conf.seek_leap_sec
			: (flags & FAV_TRACK_SEEK_LEAP_PERCENT) ? t->duration_msec / 1000 * core->conf.seek_leap_pct / 100
			: core->conf.seek_step_sec;
		if (flags & FAV_TRACK_SEEK_REVERSE)
			r = -r;
		uint64_t pos_msec = ffmax((int64_t)t->cur_pos_msec + r * 1000, 0);

		char buf[64];
		time_print(pos_msec, buf, sizeof(buf));
		dbglog(t, "Seek: %s", buf);
		ffmpeg_dec_seek(t->dec, pos_msec * 1000);
		trk_text(t, "%s", buf);
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

const struct fav_track_cu trk_cu_read = { "input", cu_in_open, cu_in_close, cu_in_read, cu_in_ctl };
