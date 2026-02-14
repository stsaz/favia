/** favia: video output CU
2025, Simon Zolin */

#include <util/SDL.hpp>
#include <util/util.h>
#include <ffsys/std.h>

struct vomo {
	uint init;
	xxsdl v;
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

static void v_obj_move(struct xxsdl *dst, struct xxsdl *src)
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
	xxsdl v;
	uint vzoom;
	uint draw :1;
	uint64_t prev_pos_sec;
};

static void cu_v_zoom(struct vox *x, fav_track *t, uint n)
{
	x->vzoom = n;
	infolog(t, "Zoom: %u%%", x->vzoom);
	uint w = t->video_width * x->vzoom / 100;
	uint h = t->video_height * x->vzoom / 100;
	x->v.window_size(w, h);
}

static void cu_v_fullscreen_toggle(struct vox *x, fav_track *t)
{
	x->v.fullscreen(!x->v.fullscreen());
}

static void cu_v_size(struct vox *x, fav_track *t, uint rw, uint rh)
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

	if (x->v.fullscreen()) {
		sdl_screen_center(&xx, &y, rw, rh);
	}

	x->v.texture_rect(xx, y, rw, rh);
}

static void cu_v_close(fav_track *t)
{
	struct vox *x = t->vox;

	if (!vom->v.window && t->next) {
		v_obj_move(&vom->v, &x->v);
	}

	x->~vox();
	fav_track_free(t, x);
}

static int cu_v_open(fav_track *t)
{
	if (t->conf.no_display)
		return FAV_CU_FWD;

	if (vomo_init()) {
		return FAV_CU_ERROR;
	}

	struct vox *x = fav_track_allocT(t, struct vox);
	new (x) (struct vox);
	x->prev_pos_sec = ~0ULL;
	t->vox = x;

	uint w = t->video_width;
	uint h = t->video_height;
	x->vzoom = sdl_screen_clamp(&w, &h, 0);
	// Note: window border is not accounted for!

	const char *title = xxpath(t->conf.input.url).name().ptr;
	if (vom->v.window) {
		v_obj_move(&x->v, &vom->v);
		if (!x->v.fullscreen()) {
			x->v.window_size(w, h);
			x->v.texture_rect(0, 0, w, h);
		} else {
			x->v.fullscreen_size(&w, &h);
			cu_v_size(x, t, w, h);
		}
		x->draw = 1;
		x->v.title(title);
	} else {
		if (!x->v.open(w, h, title)) {
			errlog(t, "Video renderer: %s", x->v.error());
			cu_v_close(t);
			return FAV_CU_ERROR;
		}
	}
	t->vo_window = x->v.window;

	if (t->conf.video.fullscreen)
		cu_v_fullscreen_toggle(x, t);
	else if (t->conf.video.zoom)
		cu_v_zoom(x, t, t->conf.video.zoom);

	x->v.show();
	return FAV_CU_FWD;
}

static void cu_v_pos_print(struct vox *x, fav_track *t, uint64_t pos_sec)
{
	if (pos_sec != x->prev_pos_sec) {
		x->prev_pos_sec = pos_sec;
		char buf1[64], buf2[64];
		ffstdout_fmt("\r[%s / %s]"
			, time_print(pos_sec * 1000, buf1, sizeof(buf1))
			, time_print(t->duration_msec, buf2, sizeof(buf2)));
	}
}

static int cu_v_display(fav_track *t)
{
	struct vox *x = (struct vox*)t->vox;
	qframe *f;
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
		if (t->vq->length() < 2) {
			t->want_input |= FAV_F_VIDEO;
			return FAV_CU_FWD;
		}

		// Draw next frame
		f = t->vq->read();
		f->unref();
		t->input_full &= ~FAV_F_VIDEO;
	}

	if (!(f = t->vq->peek())) {
		t->want_input |= FAV_F_VIDEO;
		return FAV_CU_FWD;
	}

	if (!t->conf.no_display) {
		SDL_BlendMode blendmode;
		SDL_PixelFormat format = format_sdl_av(f->frame.frame->format, &blendmode);
		if (format == SDL_PIXELFORMAT_UNKNOWN) {
			if (!t->dec.video_convert(&f->frame)) {
				errlog(t, "Video frame convert: %s", t->dec.error());
				complete = 1;
				goto end;
			}
			format = format_sdl_av(f->frame.frame->format, &blendmode);
		}
		dbglog(t, "AV-pixel-format:0x%xu  SDL-pixel-format:0x%xu", f->frame.frame->format, format);

		struct sdl_frame sf = frame_sdl_av(f->frame.frame);
		if (!x->v.display(&sf, format, blendmode)) {
			errlog(t, "display: %s", x->v.error());
			complete = 1;
		}
	}

end:
	t->sync.frame(f->ts, f->dur, 0);

	t->cur_pos_msec = f->ts / 1000;
	cu_v_pos_print(x, t, f->ts / 1000000);

	if (complete) {
		f->unref();
		t->vq->read();
		t->input_full &= ~FAV_F_VIDEO;
	}
	return FAV_CU_FWD;
}

static int cu_v_ctl(fav_track *t, uint cmd, uint flags)
{
	struct vox *x = (struct vox*)t->vox;
	int r;
	switch (cmd) {
	case FAV_TRACK_WINDOW:
		if (flags & FAV_TRACK_WND_RESIZED) {
			cu_v_size(x, t, INT32_LO16(t->arg2), INT32_HI16(t->arg2));
			break;
		}

		if (flags & FAV_TRACK_WND_FULLSCREEN) {
			cu_v_fullscreen_toggle(x, t);
			break;
		}

		if (flags & FAV_TRACK_WND_NEXT) {
			x->v.present();
		}
		break;

	case FAV_TRACK_ZOOM:
		if (x->v.fullscreen())
			break;

		if (flags)
			r = ffmin(x->vzoom + core->conf.zoom_by_pct, 400);
		else
			r = ffmax((int)x->vzoom - core->conf.zoom_by_pct, 10);
		r = ffint_align_floor(r, 10);
		cu_v_zoom(x, t, r);
		break;

	default:
		return 1;
	}
	return 0;
}

FF_EXTERN const struct fav_track_cu trk_cu_vo = { "v-display", cu_v_open, cu_v_close, cu_v_display, cu_v_ctl };
