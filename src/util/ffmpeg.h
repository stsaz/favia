/** ffmpeg wrapper
2025, Simon Zolin */

#pragma once
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#ifndef FF_EXTERN
#ifdef __cplusplus
	#define FF_EXTERN extern "C"
#else
	#define FF_EXTERN extern
#endif
#endif

typedef struct ffmpeg_packet ffmpeg_packet;
struct ffmpeg_packet {
	AVPacket *pkt;
};

typedef struct ffmpeg_frame ffmpeg_frame;
struct ffmpeg_frame {
	AVFrame *frame;
};

FF_EXTERN void ffmpeg_packet_init(ffmpeg_packet *p);
FF_EXTERN void ffmpeg_packet_destroy(ffmpeg_packet *p);
#define ffmpeg_packet_stream_index(p)  ((p)->pkt->stream_index)
static inline uint64_t ffmpeg_packet_pts(const ffmpeg_packet *p) {
	return (p->pkt->pts != AV_NOPTS_VALUE) ? p->pkt->pts : p->pkt->dts;
}
#define ffmpeg_packet_size(p)  ((p)->pkt->size)
// pts
#define ffmpeg_packet_duration(p)  ((p)->pkt->duration)

FF_EXTERN void ffmpeg_frame_init(ffmpeg_frame *f);
FF_EXTERN void ffmpeg_frame_destroy(ffmpeg_frame *f);
FF_EXTERN void ffmpeg_frame_unref(ffmpeg_frame *f);
#define ffmpeg_frame_key(f)  !!((f)->frame->flags & AV_FRAME_FLAG_KEY)

typedef struct ffmpeg_dec ffmpeg_dec;
struct ffmpeg_dec {
	AVFormatContext *fmt;
	AVCodecContext *vcodecx, *acodecx;
	const AVCodec *vcodec, *acodec;
	int video_stream, audio_stream;

	struct SwsContext *swsx;
	AVFrame *sws_frame;

	AVBufferRef *hw_device_ctx;
	enum AVPixelFormat hw_pix_fmt, sw_pix_fmt;
	AVFrame *hw_frame;
	int reading_frames;

	const char *err_func;
	int error_code;
	char error_buf[64];
};

typedef int (*ffmpeg_io_read)(void *opaque, uint8_t *buf, int buf_size);
typedef int64_t (*ffmpeg_io_seek)(void *opaque, int64_t offset, int whence);

enum FFMPEG_E {
	FFMPEG_E_OTHER = -1,
	FFMPEG_E_HWDEC = -2,
};

FF_EXTERN void ffmpeg_config(unsigned log_level);
FF_EXTERN void ffmpeg_dec_init(ffmpeg_dec *d);
FF_EXTERN void ffmpeg_dec_destroy(ffmpeg_dec *d);
FF_EXTERN const char* ffmpeg_dec_error(ffmpeg_dec *d);
FF_EXTERN int ffmpeg_dec_open(ffmpeg_dec *d, ffmpeg_io_read read, ffmpeg_io_seek seek, void *opaque, unsigned flags);

#define ffmpeg_dec_format_name(d)  (d)->fmt->iformat->name
#define ffmpeg_dec_streams(d)  (d)->fmt->nb_streams

// msec
static inline uint64_t ffmpeg_dec_duration(const ffmpeg_dec *d) {
	return (d->fmt->duration != AV_NOPTS_VALUE) ? d->fmt->duration / 1000 : 0;
}

FF_EXTERN int ffmpeg_dec_seek(ffmpeg_dec *d, uint64_t pos);

/**
Return 0: success; 1: EOF; <0: error */
FF_EXTERN int ffmpeg_dec_pkt_read(ffmpeg_dec *d, ffmpeg_packet *p);

/**
Return 0: success; 1: packet decoding is complete; <0: error */
FF_EXTERN int ffmpeg_dec_video_decode(ffmpeg_dec *d, ffmpeg_packet *p, ffmpeg_frame *f);

FF_EXTERN int ffmpeg_dec_audio_decode(ffmpeg_dec *d, ffmpeg_packet *p, ffmpeg_frame *f);
FF_EXTERN int ffmpeg_dec_video_convert(ffmpeg_dec *d, ffmpeg_frame *f);
FF_EXTERN int ffmpeg_dec_hwaccel_enable(ffmpeg_dec *d, const char *hwaccel);
FF_EXTERN int ffmpeg_dec_audio_stream_switch(ffmpeg_dec *d);

#define ffmpeg_dec_have_video(d)  !!(d)->vcodecx
#define ffmpeg_dec_video_width(d)  (d)->vcodecx->width
#define ffmpeg_dec_video_height(d)  (d)->vcodecx->height
// AV_CODEC_ID_*
#define ffmpeg_dec_video_codec(d)  (d)->vcodecx->codec_id
#define ffmpeg_dec_video_codec_name(d)  (d)->vcodec->name
static inline double ffmpeg_dec_video_time_base(const ffmpeg_dec *d) { return av_q2d(d->fmt->streams[d->video_stream]->time_base); }

#define ffmpeg_dec_picture(d)  ((d)->fmt->duration == AV_NOPTS_VALUE)

#define ffmpeg_dec_have_audio(d)  !!(d)->acodecx
#define ffmpeg_dec_audio_codec_name(d)  (d)->acodec->name
// AV_SAMPLE_FMT_*
#define ffmpeg_dec_audio_format(d)  (d)->acodecx->sample_fmt
#define ffmpeg_dec_audio_rate(d)  (d)->acodecx->sample_rate
#define ffmpeg_dec_audio_channels(d)  (d)->acodecx->ch_layout.nb_channels
static inline double ffmpeg_dec_audio_time_base(const ffmpeg_dec *d) { return av_q2d(d->fmt->streams[d->audio_stream]->time_base); }
