/** SDL wrapper
2025, Simon Zolin */

#pragma once
#include <SDL3/SDL.h>
#undef main // Prevent SDL from overriding main()

#define LOG(fmt, ...)
#ifdef FF_DEBUG
	#include <ffsys/std.h>
	#undef LOG
	#define LOG(fmt, ...) \
		fflog(fmt, ##__VA_ARGS__)
#endif

static inline int sdl_init(unsigned init)
{
	if (!init) {
		SDL_Quit();
		return 0;
	}

	return !SDL_Init(SDL_INIT_VIDEO);
}

static inline int sdl_read_events(SDL_Event *events, unsigned cap, SDL_Window *windows[])
{
	SDL_PumpEvents();
	int n = SDL_PeepEvents(events, cap, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST);
	if (windows) {
		for (int i = 0;  i < n;  i++) {
			windows[i] = SDL_GetWindowFromEvent(events + i);
		}
	}
	return n;
}

struct sdl_frame {
	unsigned height, width;
	const unsigned char *data[3];
	unsigned len[3];
};

typedef struct sdlctx sdlctx;
struct sdlctx {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	SDL_FRect rect;
	int fullscreen;
	const char *err_func;
};

#define ERR(c, s)  (c)->err_func = s, -1

static inline const char* sdl_error(const sdlctx *c)
{
	return c->err_func;
}

static inline void sdl_destroy(sdlctx *c)
{
	SDL_DestroyWindow(c->window);
	SDL_DestroyRenderer(c->renderer);
	SDL_DestroyTexture(c->texture);
}

static inline void sdl_screen_center(unsigned *x, unsigned *y, unsigned w, unsigned h)
{
	SDL_DisplayID display = SDL_GetPrimaryDisplay();
	SDL_Rect r;
	SDL_GetDisplayBounds(display, &r);
	*x = (r.w > w) ? (r.w - w) / 2 : 0;
	*y = (r.h > h) ? (r.h - h) / 2 : 0;
}

static inline void sdl_screen_size(unsigned *w, unsigned *h)
{
	SDL_DisplayID display = SDL_GetPrimaryDisplay();
	SDL_Rect r;
	SDL_GetDisplayBounds(display, &r);
	*w = r.w;
	*h = r.h;
}

static inline unsigned sdl_screen_clamp(unsigned *w, unsigned *h, unsigned flags)
{
	SDL_DisplayID display = SDL_GetPrimaryDisplay();
	SDL_Rect r;
	SDL_GetDisplayUsableBounds(display, &r);

	double z = 1, zh = 1;
	if (*w > r.w)
		z = (double)r.w / *w;
	if (*h > r.h)
		zh = (double)r.h / *h;
	if (z > zh)
		z = zh;
	*w = z * *w;
	*h = z * *h;
	return z * 100;
}

struct sdl_str {
	size_t len;
	const void *ptr;
};

struct sdl_conf {
	unsigned width, height;
	const char *title;
	const char *renderer;
	struct sdl_str icon;
};

static inline int sdl_open(sdlctx *c, struct sdl_conf *conf)
{
	unsigned f = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
	if (!(c->window = SDL_CreateWindow(conf->title, conf->width, conf->height, f)))
		return ERR(c, "SDL_CreateWindow()");

	if (conf->icon.len) {
		SDL_Surface *s = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
		if (!s)
			return ERR(c, "SDL_CreateSurface()");
		SDL_memcpy(s->pixels, conf->icon.ptr, conf->icon.len);
		SDL_SetWindowIcon(c->window, s);
		SDL_DestroySurface(s);
	}

	if (!(c->renderer = SDL_CreateRenderer(c->window, conf->renderer)))
		return ERR(c, "SDL_CreateRenderer()");
	LOG("SDL renderer: %s", SDL_GetRendererName(c->renderer));

	SDL_FRect r = {
		.x = 0,
		.y = 0,
		.w = conf->width,
		.h = conf->height,
	};
	c->rect = r;
	return 0;
}

static int _sdl_texture_convert(sdlctx *c, SDL_PixelFormat format, const struct sdl_frame *frame)
{
	bool r;
	switch (format) {
	case SDL_PIXELFORMAT_IYUV:
		r = SDL_UpdateYUVTexture(c->texture, NULL
			, frame->data[0], frame->len[0]
			, frame->data[1], frame->len[1]
			, frame->data[2], frame->len[2]);
		break;

	case SDL_PIXELFORMAT_NV12:
		r = SDL_UpdateNVTexture(c->texture, NULL
			, frame->data[0], frame->len[0]
			, frame->data[1], frame->len[1]);
		break;

	default:
		r = SDL_UpdateTexture(c->texture, NULL, frame->data[0], frame->len[0]);
		break;
	}

	if (!r)
		return ERR(c, "SDL_UpdateTexture()");
	return 0;
}

static inline void sdl_clear(sdlctx *c)
{
	SDL_SetRenderDrawColor(c->renderer, 0, 0, 0, 255);
	SDL_RenderClear(c->renderer);
}

/** Render video frame. */
static inline int sdl_video(sdlctx *c, const struct sdl_frame *frame, SDL_PixelFormat format, SDL_BlendMode blendmode)
{
	if (!(c->texture
		&& frame->width == c->texture->w
		&& frame->height == c->texture->h
		&& format == c->texture->format)) {
		SDL_DestroyTexture(c->texture);
		if (!(c->texture = SDL_CreateTexture(c->renderer, format, SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height)))
			return ERR(c, "SDL_CreateTexture()");
		if (!SDL_SetTextureBlendMode(c->texture, blendmode))
			return ERR(c, "SDL_SetTextureBlendMode()");
	}

	if (_sdl_texture_convert(c, format, frame))
		return -1;

	SDL_RenderTexture(c->renderer, c->texture, NULL, &c->rect);
	return 0;
}

struct sdl_text {
	uint margin;
	uint scale;
};

enum SDL_TEXT_F {
	SDL_TEXT_BOTTOM_RIGHT = 1,
};

/** Display text.
flags: enum SDL_TEXT_F */
static inline int sdl_text(sdlctx *c, const char *text, const struct sdl_text *conf, uint flags)
{
	uint sz, w, x = 0, y = 0;
	uint scale = (conf->scale) ?: 1;
	sz = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
	w = sz * ffsz_len(text);
	if (flags & SDL_TEXT_BOTTOM_RIGHT) {
		x = ffmax((int)c->rect.x + c->rect.w - w * scale - conf->margin, 0);
		y = ffmax((int)c->rect.y + c->rect.h - sz * scale - conf->margin, 0);
		x /= scale;
		y /= scale;
	}

	if (scale != 1)
		SDL_SetRenderScale(c->renderer, scale, scale);

	SDL_SetRenderDrawColor(c->renderer, 255, 255, 255, 255);
	int r = !SDL_RenderDebugText(c->renderer, x, y, text);

	if (scale != 1)
		SDL_SetRenderScale(c->renderer, 1, 1);
	return r;
}

static inline void sdl_present(sdlctx *c)
{
	SDL_RenderPresent(c->renderer);
}

#define sdl_window_size(c, w, h) \
	SDL_SetWindowSize((c)->window, w, h)

static inline void sdl_fullscreen(sdlctx *c, int enable)
{
	c->fullscreen = enable;
	SDL_SetWindowFullscreen(c->window, enable);
}

static inline void sdl_wnd_size(sdlctx *c, unsigned *w, unsigned *h)
{
	SDL_GetWindowSize(c->window, (int*)w, (int*)h);
}

#define sdl_title(c, title) \
	SDL_SetWindowTitle((c)->window, title)

#define sdl_show(c) \
	SDL_ShowWindow((c)->window)

#define sdl_raise(c) \
	SDL_RaiseWindow((c)->window)

static inline void sdl_texture_rect(sdlctx *c, unsigned x, unsigned y, unsigned w, unsigned h)
{
	c->rect.x = x;
	c->rect.y = y;
	c->rect.w = w;
	c->rect.h = h;
}

#undef ERR
