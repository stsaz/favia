/** ffmpeg wrapper
2025, Simon Zolin */

#include "ffmpeg.h"

#define ERR(d, func)  d->err_func = func, -1

void ffmpeg_packet_init(ffmpeg_packet *p) {
	p->pkt = av_packet_alloc();
}

void ffmpeg_packet_destroy(ffmpeg_packet *p) {
	av_packet_free(&p->pkt);
}


void ffmpeg_frame_init(ffmpeg_frame *f) {
	f->frame = av_frame_alloc();
}

void ffmpeg_frame_destroy(ffmpeg_frame *f) {
	av_frame_free(&f->frame);
}

void ffmpeg_frame_unref(ffmpeg_frame *f) {
	av_frame_unref(f->frame);
}


void ffmpeg_dec_init(ffmpeg_dec *d) {
}

void ffmpeg_dec_destroy(ffmpeg_dec *d) {
	av_buffer_unref(&d->hw_device_ctx);
	av_frame_free(&d->hw_frame);

	avcodec_free_context(&d->vcodecx);
	avcodec_free_context(&d->acodecx);
	avformat_close_input(&d->fmt);
}

static int dec_open(ffmpeg_dec *d, int *stream, AVCodecContext **decx, const AVCodec **codec, enum AVMediaType type)
{
	int r;
	if ((r = av_find_best_stream(d->fmt, type, -1, -1, codec, 0)) < 0)
		return 0;
	*stream = r;

	if (!(*decx = avcodec_alloc_context3(*codec)))
		return ERR(d, "avcodec_alloc_context3()");

	const AVStream *stm = d->fmt->streams[*stream];
	if ((r = avcodec_parameters_to_context(*decx, stm->codecpar)) < 0)
		return ERR(d, "avcodec_parameters_to_context()");

	if ((r = avcodec_open2(*decx, *codec, NULL)) < 0)
		return ERR(d, "avcodec_open2()");
	return 0;
}

int ffmpeg_dec_open(ffmpeg_dec *d, const char *fn) {
	d->hw_pix_fmt = -1;
	d->video_stream = d->audio_stream = -1;

	if (avformat_open_input(&d->fmt, fn, NULL, NULL) < 0)
		return ERR(d, "avformat_open_input()");

	if (avformat_find_stream_info(d->fmt, NULL) < 0)
		return ERR(d, "avformat_find_stream_info()");

	if (dec_open(d, &d->video_stream, &d->vcodecx, &d->vcodec, AVMEDIA_TYPE_VIDEO) < 0)
		return -1;
	if (dec_open(d, &d->audio_stream, &d->acodecx, &d->acodec, AVMEDIA_TYPE_AUDIO) < 0)
		return -1;
	if (d->video_stream < 0 && d->audio_stream < 0)
		return -1;
	return 0;
}

int ffmpeg_dec_seek(ffmpeg_dec *d, uint64_t pos) {
	if (avformat_seek_file(d->fmt, -1, pos, pos, pos, 0) < 0)
		return ERR(d, "avformat_seek_file()");
	return 0;
}

int ffmpeg_dec_pkt_read(ffmpeg_dec *d, ffmpeg_packet *p) {
	av_packet_unref(p->pkt);
	return !(av_read_frame(d->fmt, p->pkt) >= 0);
}

static enum AVPixelFormat _hw_pix_fmt;
static enum AVPixelFormat get_format_hw(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
	for (const enum AVPixelFormat *p = pix_fmts;  *p != -1;  p++) {
		if (*p == _hw_pix_fmt)
			return *p;
	}

	return AV_PIX_FMT_NONE;
}

int ffmpeg_dec_hwaccel_enable(ffmpeg_dec *d, const char *hwaccel) {
	enum AVHWDeviceType type = av_hwdevice_find_type_by_name(hwaccel);
	if (type == AV_HWDEVICE_TYPE_NONE)
		return ERR(d, "av_hwdevice_find_type_by_name()");

	for (uint i = 0;; i++) {
		const AVCodecHWConfig *config = avcodec_get_hw_config(d->vcodec, i);
		if (!config)
			return ERR(d, "avcodec_get_hw_config()");
		if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
			&& config->device_type == type) {
			d->hw_pix_fmt = config->pix_fmt;
			_hw_pix_fmt = config->pix_fmt;
			break;
		}
	}

	int r;
	if ((r = av_hwdevice_ctx_create(&d->hw_device_ctx, type, NULL, NULL, 0)) < 0)
		return ERR(d, "av_hwdevice_ctx_create()");

	d->vcodecx->hw_device_ctx = av_buffer_ref(d->hw_device_ctx);
	d->vcodecx->get_format  = get_format_hw;
	d->hw_frame = av_frame_alloc();
	return 0;
}

int ffmpeg_dec_video_decode(ffmpeg_dec *d, ffmpeg_packet *p, ffmpeg_frame *f) {
	int r;
	if ((r = avcodec_send_packet(d->vcodecx, p->pkt)) < 0)
		return ERR(d, "avcodec_send_packet()");

	av_frame_unref(f->frame);
	r = avcodec_receive_frame(d->vcodecx, f->frame);
	if (r == AVERROR_EOF)
		return 1;
	else if (r < 0)
		return ERR(d, "avcodec_receive_frame()");

	if (f->frame->format == d->hw_pix_fmt) {
		av_frame_unref(d->hw_frame);
		d->hw_frame->format = AV_PIX_FMT_YUV420P;
		if ((r = av_hwframe_transfer_data(d->hw_frame, f->frame, 0)) < 0) {
			av_frame_unref(f->frame);
			return ERR(d, "av_hwframe_transfer_data()");
		}
		void *tmp = f->frame;
		f->frame = d->hw_frame;
		d->hw_frame = tmp;
	}

	return 0;
}

int ffmpeg_dec_audio_decode(ffmpeg_dec *d, ffmpeg_packet *p, ffmpeg_frame *f) {
	int r;
	if ((r = avcodec_send_packet(d->acodecx, p->pkt)) < 0)
		return ERR(d, "avcodec_send_packet()");

	av_frame_unref(f->frame);
	r = avcodec_receive_frame(d->acodecx, f->frame);
	if (r == AVERROR_EOF)
		return 1;
	else if (r < 0)
		return ERR(d, "avcodec_receive_frame()");

	return 0;
}
