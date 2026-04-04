/** favia: process events from SDL window
2025, Simon Zolin */

#include <ffsys/perf.h>
#include <util/SDL.hpp>
#include <util/util.h>
#include <ffbase/stringz.h>

struct ui {
	uint flags;
	uint64_t mlclick_ts;
};
static struct ui ui;

struct ui_key {
	int key, mod, cmd, arg1;
};
static struct ui_key ui_keymap[] = {
	{ SDLK_DOWN,		0,								FAV_TRACK_VOLUME, 0 },
	{ SDLK_LEFT, 		0,								FAV_TRACK_SEEK, FAV_TRACK_SEEK_REVERSE },
	{ SDLK_LEFT, 		SDL_KMOD_ALT,					FAV_TRACK_SEEK, FAV_TRACK_SEEK_JUMP | FAV_TRACK_SEEK_REVERSE },
	{ SDLK_LEFT, 		SDL_KMOD_CTRL,					FAV_TRACK_SEEK, FAV_TRACK_SEEK_LEAP | FAV_TRACK_SEEK_REVERSE },
	{ SDLK_LEFT, 		SDL_KMOD_CTRL | SDL_KMOD_SHIFT,	FAV_TRACK_SEEK, FAV_TRACK_SEEK_LEAP_PERCENT | FAV_TRACK_SEEK_REVERSE },
	{ SDLK_RIGHT, 		0,								FAV_TRACK_SEEK, FAV_TRACK_SEEK_FWD },
	{ SDLK_RIGHT, 		SDL_KMOD_ALT,					FAV_TRACK_SEEK, FAV_TRACK_SEEK_JUMP },
	{ SDLK_RIGHT, 		SDL_KMOD_CTRL,					FAV_TRACK_SEEK, FAV_TRACK_SEEK_LEAP },
	{ SDLK_RIGHT, 		SDL_KMOD_CTRL | SDL_KMOD_SHIFT,	FAV_TRACK_SEEK, FAV_TRACK_SEEK_LEAP_PERCENT },
	{ SDLK_UP,			0,								FAV_TRACK_VOLUME, 1 },

	{ SDLK_EQUALS,		SDL_KMOD_CTRL | SDL_KMOD_SHIFT,	FAV_TRACK_WINDOW, FAV_TRACK_WND_ADD },
	{ SDLK_EQUALS,		SDL_KMOD_SHIFT,					FAV_TRACK_ZOOM, 1 },
	{ SDLK_MINUS,		0,								FAV_TRACK_ZOOM, 0 },
	{ SDLK_MINUS,		SDL_KMOD_CTRL,					FAV_TRACK_WINDOW, FAV_TRACK_WND_RM },
	{ SDLK_SPACE,		0,								FAV_TRACK_PAUSE, 0 },
	{ SDLK_TAB,			0,								FAV_TRACK_WINDOW, FAV_TRACK_WND_NEXT },
	{ SDLK_DELETE,		SDL_KMOD_SHIFT,					FAV_TRACK_SOURCE, FAV_TRACK_SRC_TRASH },
	{ SDLK_LEFTBRACKET,	0,								FAV_TRACK_SEEK, FAV_TRACK_SEEK_LOOP },
	{ SDLK_RIGHTBRACKET,0,								FAV_TRACK_SEEK, FAV_TRACK_SEEK_LOOP | FAV_TRACK_SEEK_REVERSE },

	{ SDLK_HOME,		0,								FAV_TRACK_START, FAV_TRACK_START_FIRST },
	{ SDLK_END,			0,								FAV_TRACK_START, FAV_TRACK_START_LAST },
	{ SDLK_PAGEUP,		0,								FAV_TRACK_START, FAV_TRACK_START_PGPREV },
	{ SDLK_PAGEDOWN,	0,								FAV_TRACK_START, FAV_TRACK_START_PGNEXT },

	{ SDLK_KP_MINUS,	0,								FAV_TRACK_ZOOM, 0 },
	{ SDLK_KP_MINUS,	SDL_KMOD_CTRL,					FAV_TRACK_WINDOW, FAV_TRACK_WND_RM },
	{ SDLK_KP_PLUS,		0,								FAV_TRACK_ZOOM, 1 },
	{ SDLK_KP_PLUS,		SDL_KMOD_CTRL,					FAV_TRACK_WINDOW, FAV_TRACK_WND_ADD },

	{ SDLK_F1,			0,								FAV_TRACK_SOURCE, FAV_TRACK_SRC_MOVE + 0 },
	{ SDLK_F2,			0,								FAV_TRACK_SOURCE, FAV_TRACK_SRC_MOVE + 1 },
	{ SDLK_F3,			0,								FAV_TRACK_SOURCE, FAV_TRACK_SRC_MOVE + 2 },

	{ SDLK_A,			0,								FAV_TRACK_AUDIO_NEXT, 0 },
	{ SDLK_F,			0,								FAV_TRACK_WINDOW, FAV_TRACK_WND_FULLSCREEN },
	{ SDLK_M,			0,								FAV_TRACK_VOLUME, FAV_TRACK_VOL_MUTE },
	{ SDLK_N,			0,								FAV_TRACK_START, FAV_TRACK_START_NEXT },
	{ SDLK_P,			0,								FAV_TRACK_START, FAV_TRACK_START_PREV },
	{ SDLK_Q,			0,								FAV_TRACK_QUIT, 0 },
};

enum {
	UIF_CTRL = 1,
};

static uint key_find(int k, int m, int *arg1)
{
	for (uint i = 0;  i < FF_COUNT(ui_keymap);  i++) {
		const struct ui_key *uk = &ui_keymap[i];
		if (k == uk->key
			&& (!(m | uk->mod)
				|| ((m & uk->mod)
					&& !(m & ~uk->mod)))) {
			*arg1 = uk->arg1;
			return uk->cmd;
		}
	}
	return ~0U;
}

int user_events() {
	SDL_Event ev[32];
	SDL_Window *wnd[32];
	int n = sdl_read_events(ev, FF_COUNT(ev), wnd);
	for (int i = 0;  i < n;  i++) {
		const SDL_Event *e = ev + i;
		dbglog("SDL event from %p: type:0x%xu", wnd[i], e->type);

		uint cmd = ~0U;
		int arg1 = 0, arg2 = 0;
		void *arg1_ptr = NULL;
		switch (e->type) {
		case SDL_EVENT_KEY_DOWN:

			if ((cmd = key_find(e->key.key, e->key.mod, &arg1)) != ~0U)
				goto process;

			switch (e->key.key) {

			case SDLK_LCTRL:
			case SDLK_RCTRL:
				ui.flags |= UIF_CTRL;  break;

			default:
				warnlog("No such key binding (0x%xu)", e->key.key);
			}
			break;

		case SDL_EVENT_KEY_UP:
			switch (e->key.key) {
			case SDLK_LCTRL:
			case SDLK_RCTRL:
				ui.flags &= ~UIF_CTRL;  break;
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			switch (e->button.button) {
			case SDL_BUTTON_LEFT: {
				fftime t = fftime_monotonic();
				uint64_t ts = fftime_to_msec(&t);
				if (ts < ui.mlclick_ts + 400) {
					cmd = FAV_TRACK_WINDOW;
					arg1 = FAV_TRACK_WND_FULLSCREEN;
					ui.mlclick_ts = 0;
				} else {
					ui.mlclick_ts = ts;
				}
				break;
			}
			case SDL_BUTTON_RIGHT:
				cmd = FAV_TRACK_PAUSE;  break;
			}
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			if (ui.flags & UIF_CTRL) {
				cmd = FAV_TRACK_ZOOM, arg1 = (e->wheel.y >= 0);
				break;
			}
			cmd = FAV_TRACK_VOLUME, arg1 = (e->wheel.y >= 0);
			break;

		case SDL_EVENT_DROP_FILE:
			cmd = FAV_TRACK_ADD;  arg1_ptr = ffsz_dup(e->drop.data);  break;

		case SDL_EVENT_WINDOW_RESIZED:
			cmd = FAV_TRACK_WINDOW, arg1 = FAV_TRACK_WND_RESIZED, arg2 = INT32_MAKE1616(e->display.data2, e->display.data1);  break;

		case SDL_EVENT_WINDOW_EXPOSED:
			cmd = FAV_TRACK_WINDOW, arg1 = FAV_TRACK_WND_SHOWN;  break;

		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			cmd = FAV_TRACK_STOP;  break;
		}

process:
		if (cmd != ~0U) {
			if (!wnd[i])
				continue;
			fav_track *t = core->track->find(wnd[i]);
			if (!t)
				continue; // the window is in cache
			if (cmd == FAV_TRACK_ADD)
				core->track->cmd(t, cmd, arg1_ptr);
			else
				core->track->cmd(t, cmd, arg1, arg2);
		}
	}
	return 0;
}
