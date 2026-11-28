/** favia: video output CU
2025, Simon Zolin */

#include <core/track.h>
const fav_core_if *core;
#include <util/SDL.h>
#include <util/util.h>
#include <util/avsync.h>
#include <util/avqueue.h>
#include <ffsys/std.h>
#include <ffsys/path.h>

struct vomo {
	uint init;
	sdlctx v;
};

static struct vomo *vom;

static int vomo_init()
{
	if (!vom) {
		vom = ffmem_new(struct vomo);
	}

	if (!vom->init) {
		vom->init = 1;
		if (sdl_init(1)) {
			fav_errlog("SDL init");
			return 1;
		}
	}
	return 0;
}

void vomo_destroy()
{
	if (!vom) return;

	sdl_destroy(&vom->v);
	ffmem_zero_obj(&vom->v);
}

static void v_obj_move(sdlctx *dst, sdlctx *src)
{
	*dst = *src;
	ffmem_zero_obj(src);
}

static SDL_PixelFormat format_sdl_av(int av_format, SDL_BlendMode *blendmode)
{
	*blendmode = (av_format == AV_PIX_FMT_RGB32
			|| av_format == AV_PIX_FMT_RGB32_1
			|| av_format == AV_PIX_FMT_BGR32
			|| av_format == AV_PIX_FMT_BGR32_1)
		? SDL_BLENDMODE_BLEND
		: SDL_BLENDMODE_NONE;

	static const struct {
		enum AVPixelFormat av_format;
		SDL_PixelFormat sdl_format;
	} texture_format_map[] = {
		{ AV_PIX_FMT_RGB8,		SDL_PIXELFORMAT_RGB332 },
		{ AV_PIX_FMT_RGB444,	SDL_PIXELFORMAT_XRGB4444 },
		{ AV_PIX_FMT_RGB555,	SDL_PIXELFORMAT_XRGB1555 },
		{ AV_PIX_FMT_BGR555,	SDL_PIXELFORMAT_XBGR1555 },
		{ AV_PIX_FMT_RGB565,	SDL_PIXELFORMAT_RGB565 },
		{ AV_PIX_FMT_BGR565,	SDL_PIXELFORMAT_BGR565 },
		{ AV_PIX_FMT_RGB24,		SDL_PIXELFORMAT_RGB24 },
		{ AV_PIX_FMT_BGR24,		SDL_PIXELFORMAT_BGR24 },
		{ AV_PIX_FMT_0RGB32,	SDL_PIXELFORMAT_XRGB8888 },
		{ AV_PIX_FMT_0BGR32,	SDL_PIXELFORMAT_XBGR8888 },
		{ AV_PIX_FMT_NE(RGB0, 0BGR), SDL_PIXELFORMAT_RGBX8888 },
		{ AV_PIX_FMT_NE(BGR0, 0RGB), SDL_PIXELFORMAT_BGRX8888 },
		{ AV_PIX_FMT_RGB32,		SDL_PIXELFORMAT_ARGB8888 },
		{ AV_PIX_FMT_RGB32_1,	SDL_PIXELFORMAT_RGBA8888 },
		{ AV_PIX_FMT_BGR32,		SDL_PIXELFORMAT_ABGR8888 },
		{ AV_PIX_FMT_BGR32_1,	SDL_PIXELFORMAT_BGRA8888 },
		{ AV_PIX_FMT_YUV420P,	SDL_PIXELFORMAT_IYUV },
		{ AV_PIX_FMT_YUYV422,	SDL_PIXELFORMAT_YUY2 },
		{ AV_PIX_FMT_UYVY422,	SDL_PIXELFORMAT_UYVY },
		{ AV_PIX_FMT_NV12,		SDL_PIXELFORMAT_NV12 },
	};

	for (uint i = 0;  i < FF_COUNT(texture_format_map);  i++) {
		if (av_format == texture_format_map[i].av_format)
			return texture_format_map[i].sdl_format;
	}
	return SDL_PIXELFORMAT_UNKNOWN;
}

static struct sdl_frame frame_sdl_av(const AVFrame *avf)
{
	struct sdl_frame sf = {
		.height = avf->height,
		.width = avf->width,
	};
	sf.len[0] = avf->linesize[0];
	sf.len[1] = avf->linesize[1];
	sf.len[2] = avf->linesize[2];
	sf.data[0] = avf->data[0];
	sf.data[1] = avf->data[1];
	sf.data[2] = avf->data[2];

	if (avf->linesize[0] < 0
		&& avf->linesize[1] < 0
		&& avf->linesize[2] < 0) {

		sf.len[0] = -avf->linesize[0];
		sf.len[1] = -avf->linesize[1];
		sf.len[2] = -avf->linesize[2];
		sf.data[0] = avf->data[0] + avf->linesize[0] * (avf->height - 1);
		sf.data[1] = avf->data[1] + avf->linesize[1] * (AV_CEIL_RSHIFT(avf->height, 1) - 1);
		sf.data[2] = avf->data[2] + avf->linesize[2] * (AV_CEIL_RSHIFT(avf->height, 1) - 1);
	}

	return sf;
}

struct vox {
	sdlctx v;
	uint draw :1;
	uint64_t prev_pos_sec;
};

static void vo_fullscreen_toggle(struct vox *x, fav_track *t)
{
	sdl_fullscreen(&x->v, !x->v.fullscreen);
}

static void vo_size(struct vox *x, fav_track *t, uint rw, uint rh)
{
	uint xx = 0, y = 0, w = t->video_width, h = t->video_height;
	double ratio = (double)w / h;
	double rr = (double)rw / rh;
	// ar  rr     res
	// 2x1 1x1 -> 1x0.5
	// 1x2 1x1 -> 0.5x1
	if (rr < ratio)
		rh = (double)rw / ratio;
	else
		rw = (double)rh * ratio;

	if (x->v.fullscreen) {
		sdl_screen_center(&xx, &y, rw, rh);
	}

	sdl_texture_rect(&x->v, xx, y, rw, rh);
}

static void vo_close(fav_track *t)
{
	struct vox *x = t->vox;

	if (!t->stop && !vom->v.window) {
		v_obj_move(&vom->v, &x->v);
	}

	sdl_destroy(&x->v);
	fav_track_free(t, x);
}

static int vo_open(fav_track *t)
{
	if (vomo_init())
		return FAV_CU_ERROR;

	struct vox *x = fav_track_allocT(t, struct vox);
	x->prev_pos_sec = ~0ULL;
	t->vox = x;

	uint w = t->video_width;
	uint h = t->video_height;
	sdl_screen_clamp(&w, &h, 0);
	// Note: window border is not accounted for!

	ffstr name;
	ffpath_splitpath_str(FFSTR_Z(t->conf.input.url), NULL, &name);
	const char *title = name.ptr;
	if (vom->v.window) {
		v_obj_move(&x->v, &vom->v);
		if (x->v.fullscreen) {
			sdl_screen_size(&w, &h);
			vo_size(x, t, w, h);
		} else if (t->conf.video.zoom_lock_wnd) {
			sdl_wnd_size(&x->v, &w, &h);
			vo_size(x, t, w, h);
		} else {
			sdl_window_size(&x->v, w, h);
			sdl_texture_rect(&x->v, 0, 0, w, h);
		}
		x->draw = 1;
		sdl_title(&x->v, title);
	} else {
		ffstr icon = core->conf.resource_load("favia.rgba");
		struct sdl_conf conf = {
			.width = w,
			.height = h,
			.title = title,
			.icon = (struct sdl_str){ icon.len, icon.ptr },
		};
		if (sdl_open(&x->v, &conf)) {
			errlog(t, "Video renderer: %s", sdl_error(&x->v));
			vo_close(t);
			return FAV_CU_ERROR;
		}
		if (t->conf.video.zoom_lock_wnd) {
			uint zw = t->video_width * t->conf.video.zoom / 100;
			uint zh = t->video_height * t->conf.video.zoom / 100;
			sdl_window_size(&x->v, zw, zh);
			sdl_texture_rect(&x->v, 0, 0, zw, zh);
		}
	}
	t->vo_window = x->v.window;

	if (t->conf.video.fullscreen)
		vo_fullscreen_toggle(x, t);

	sdl_show(&x->v);
	return FAV_CU_FWD;
}

static void vo_pos_print(struct vox *x, fav_track *t, uint64_t pos_sec)
{
	if (pos_sec != x->prev_pos_sec) {
		x->prev_pos_sec = pos_sec;
		char buf1[64], buf2[64];
		ffstdout_fmt("\r[%s / %s]"
			, time_print(pos_sec * 1000, buf1, sizeof(buf1))
			, time_print(t->duration_msec, buf2, sizeof(buf2)));
	}
}

static int vo_display(fav_track *t)
{
	struct vox *x = (struct vox*)t->vox;
	struct qframe *f;
	bool complete = 0;

	if (t->conv.f & FAV_CF_REVERSE)
		return FAV_CU_BACK;

	if (!(t->frame_flags & FAV_F_VIDEO))
		return FAV_CU_FWD;

	if (x->draw) {
		x->draw = 0;
		t->frame_flags |= FAV_F_REDRAW;
	}

	if (!(t->frame_flags & FAV_F_REDRAW)) {
		if (core->track->avq_length(t->vq) < 2) {
			t->want_input |= FAV_F_VIDEO;
			return FAV_CU_FWD;
		}

		// Draw next frame
		core->track->avq_next(t->vq);
		t->input_full &= ~FAV_F_VIDEO;
	}

	if (!(f = core->track->avq_peek(t->vq))) {
		t->want_input |= FAV_F_VIDEO;
		return FAV_CU_FWD;
	}

	if (!t->conf.no_display) {
		SDL_BlendMode blendmode;
		SDL_PixelFormat format = format_sdl_av(f->f.frame->format, &blendmode);
		if (format == SDL_PIXELFORMAT_UNKNOWN) {
			if (core->track->video_convert(t, f)) {
				complete = 1;
				goto end;
			}
			format = format_sdl_av(f->f.frame->format, &blendmode);
		}
		dbglog(t, "AV-pixel-format:0x%xu  SDL-pixel-format:0x%xu", f->f.frame->format, format);

		sdl_clear(&x->v);
		struct sdl_frame sf = frame_sdl_av(f->f.frame);
		if (sdl_video(&x->v, &sf, format, blendmode)) {
			errlog(t, "display: %s", sdl_error(&x->v));
			complete = 1;
		}
	} else {
		sdl_clear(&x->v);
	}

	if (*t->vo.text) {
		struct sdl_text tc = {
			.margin = 5,
			.scale = 2,
		};
		sdl_text(&x->v, t->vo.text, &tc, SDL_TEXT_BOTTOM_RIGHT);
	}

	sdl_present(&x->v);

end:
	t->cur_pos_msec = avsync_frame(t->sync, f->ts, f->dur, 0) / 1000;
	vo_pos_print(x, t, f->ts / 1000000);

	if (complete) {
		core->track->avq_next(t->vq);
		t->input_full &= ~FAV_F_VIDEO;
	}
	return FAV_CU_FWD;
}

static int vo_ctl(fav_track *t, uint cmd, uint flags)
{
	struct vox *x = (struct vox*)t->vox;
	switch (cmd) {
	case FAV_TRACK_WINDOW:
		if (flags & FAV_TRACK_WND_RESIZED) {
			vo_size(x, t, INT32_LO16(t->arg2), INT32_HI16(t->arg2));
			break;
		}

		if (flags & FAV_TRACK_WND_FULLSCREEN) {
			vo_fullscreen_toggle(x, t);
			break;
		}

		if (flags & FAV_TRACK_WND_NEXT) {
			sdl_raise(&x->v);
		}
		break;

	case FAV_TRACK_ZOOM: {
		if (x->v.fullscreen)
			break;

		uint zw = t->video_width * t->conf.video.zoom / 100;
		uint zh = t->video_height * t->conf.video.zoom / 100;
		sdl_window_size(&x->v, zw, zh);
		break;
	}

	default:
		return 1;
	}
	return 0;
}

static const struct fav_track_cu trk_cu_vo = { "v-display", vo_open, vo_close, vo_display, vo_ctl };

static const void* fav_provide()
{
	return &trk_cu_vo;
}

static const struct fav_mod fav_mod_vo = {
	fav_provide
};

FF_EXPORT const struct fav_mod* fav_mod_vo_init(const fav_core_if *_core)
{
	core = _core;
	return &fav_mod_vo;
}
