/** favia: process events from SDL window
2025, Simon Zolin */

#include <SDL3/SDL.h>

#define SEEK_STEP_SEC  10
#define SEEK_LEAP_SEC  60

struct ui {
	uint flags;
};
static struct ui ui;

int user_events(fav_track *t) {
	SDL_PumpEvents();
	SDL_Event ev[32];
	int n = SDL_PeepEvents(ev, FF_COUNT(ev), SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST);
	for (int i = 0;  i < n;  i++) {
		const SDL_Event *e = ev + i;
		dbglog("SDL event: type:0x%xu", e->type);
		switch (e->type) {
		case SDL_EVENT_KEY_DOWN:
			switch (e->key.key) {

			case SDLK_LCTRL:
			case SDLK_RCTRL:
				ui.flags |= 1;  break;

			case SDLK_LSHIFT:
			case SDLK_RSHIFT:
				ui.flags |= 2;  break;

			case SDLK_SPACE:
				core->track->cmd(t, FAV_TRACK_PAUSE_TOGGLE);  break;

			case SDLK_LEFT:
			case SDLK_RIGHT: {
				int r = !(ui.flags & 1) ? SEEK_STEP_SEC : SEEK_LEAP_SEC;
				if (e->key.key == SDLK_LEFT)
					r = -r;
				core->track->seek_by(t, r*1000);
				break;
			}

			case SDLK_DOWN:
				core->track->cmd(t, FAV_TRACK_VOLUME, 0);  break;
			case SDLK_UP:
				core->track->cmd(t, FAV_TRACK_VOLUME, 1);  break;

			case SDLK_F:
				core->track->cmd(t, FAV_TRACK_FULLSCREEN_TOGGLE);  break;

			case SDLK_MINUS:
			case SDLK_KP_MINUS:
				core->track->cmd(t, FAV_TRACK_ZOOM, 0);  break;
			case SDLK_EQUALS:
				if (!(ui.flags & 2))
					break;
				// fallthrough
			case SDLK_KP_PLUS:
				core->track->cmd(t, FAV_TRACK_ZOOM, 1);  break;

			case SDLK_Q:
				core->stop();
				return -1;

			default:
				warnlog("No such key binding (0x%xu)", e->key.key);
			}
			break;

		case SDL_EVENT_KEY_UP:
			switch (e->key.key) {
			case SDLK_LCTRL:
			case SDLK_RCTRL:
				ui.flags &= ~1;  break;

			case SDLK_LSHIFT:
			case SDLK_RSHIFT:
				ui.flags &= ~2;  break;
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			switch (e->button.button) {
			case SDL_BUTTON_LEFT:
				core->track->cmd(t, FAV_TRACK_FULLSCREEN_TOGGLE);  break;
			case SDL_BUTTON_RIGHT:
				core->track->cmd(t, FAV_TRACK_PAUSE_TOGGLE);  break;
			}
			break;

		// case SDL_EVENT_MOUSE_WHEEL:
		// 	core->track->cmd(t, FAV_TRACK_VOLUME, 0);
		// 	core->track->cmd(t, FAV_TRACK_VOLUME, 1);

		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			core->stop();
			return -1;
		}
	}
	return 0;
}
