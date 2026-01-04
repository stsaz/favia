/** favia
2025, Simon Zolin */

#ifdef _WIN32
#include <util/windows-shell.h>
#else
#include <util/unix-shell.h>
#endif
#include <core/track.h>
#include <util/util.h>

#include <f/in.hpp>
#include <v/vd.hpp>
#include <f/sync.hpp>
#include <v/vo.hpp>
#include <a/ao.hpp>
#include <f/until.hpp>

// enum FAV_CU
const char _fav_cu[][14] = {
	"FAV_CU_FWD",
	"FAV_CU_BACK",
	"FAV_CU_ASYNC",
	"FAV_CU_ERROR",
	"FAV_CU_WARN",
	"FAV_CU_DONE",
	"FAV_CU_FIN",
};

struct tracks {
	xxvec tracks; // fav_track*[]
	uint gid;
};
static struct tracks *xt;

FF_EXTERN void tracks_init()
{
	xt = ffmem_new(struct tracks);
}

static fav_track* track_create(struct fav_track_conf *conf)
{
	fav_track *t = new(ffmem_new(fav_track)) fav_track;
	t->conf = *conf;
	xt->gid++;
	ffsz_format(t->id, sizeof(t->id), "*%u", xt->gid);

	for (uint i = 0;  conf->conveyor[i];  i++) {
		t->conv.units[i] = *conf->conveyor[i];
		t->conv.n++;
	}
	assert(t->conv.n <= FF_COUNT(t->conv.opened));

	*xt->tracks.push<fav_track*>() = t;
	return t;
}

static void trk_conveyor_close(fav_track *t)
{
	for (int i = (int)t->conv.n - 1;  i >= 0;  i--) {
		const struct fav_track_cu *cu = &t->conv.units[i];
		if (t->conv.opened[i]
			&& cu->close) {
			dbglog(t, "closing '%s'", cu->name);
			t->conv.i = i;
			cu->close(t);
		}
	}
}

/** Print the time we spent inside each CU */
static void track_busytime_print(fav_track *t)
{
	xxvec buf;
	buf.add_f("busy time: ");

	for (int i = (int)t->conv.n - 1;  i >= 0;  i--) {
		const struct fav_track_cu *cu = &t->conv.units[i];
		uint64_t nsec = t->conv.busy_time_nsec[i];
		if (!nsec)
			continue;
		uint sec = nsec / 1000000000;
		uint usec = nsec % 1000000000 / 1000;
		buf.add_f("%s: %u.%06u, "
			, cu->name, sec, usec);
	}
	buf.len -= FFS_LEN(", ");

	infolog(t, "%S", &buf);
}

static void track_close(fav_track *t)
{
	fav_track **it;
	FFSLICE_WALK(&xt->tracks, it) {
		if (t == *it) {
			xt->tracks.rm_swap<fav_track*>(it, 1);
			break;
		}
	}

	if (t->conf.print_time)
		track_busytime_print(t);

	trk_conveyor_close(t);
	if (t->conf.url_transient)
		ffmem_free((char*)t->conf.input.url);
	t->~fav_track();
	ffmem_free(t);
}

static fav_track* tracks_next(fav_track *t)
{
	if (xt->tracks.len <= 1)
		return NULL;

	fav_track **it;
	FFSLICE_WALK(&xt->tracks, it) {
		if (*it == t) {
			uint i = it - (fav_track**)xt->tracks.ptr;
			i++;
			if (i == xt->tracks.len)
				i = 0;
			return *xt->tracks.at<fav_track*>(i);
		}
	}
	return NULL;
}

static int track_cmd(fav_track *t, uint cmd, ...)
{
	int r;
	va_list va;
	va_start(va, cmd);

	if (cmd == FAV_TRACK_ADD) {
		char *fn = va_arg(va, char*);
		va_end(va);
		fav_track_conf tc = t->conf;
		tc.input.url = fn;
		tc.url_transient = 1;
		track_create(&tc);
		return 0;
	}

	uint flags = va_arg(va, uint);
	uint arg2 = va_arg(va, uint);
	va_end(va);

	switch (cmd) {

	case FAV_TRACK_NEXT:
	case FAV_TRACK_STOP:
	case FAV_TRACK_QUIT:
	case FAV_TRACK_WINDOW:
		if (cmd == FAV_TRACK_WINDOW && (flags & FAV_TRACK_WND_SHOWN)) {
			t->redraw = 1;
			break;

		} else if (cmd == FAV_TRACK_WINDOW && (flags & FAV_TRACK_WND_RESIZED)) {
			t->arg2 = arg2;
			break;

		} else if (cmd == FAV_TRACK_WINDOW && (flags & FAV_TRACK_WND_NEXT)) {
			t = tracks_next(t);
			if (!t)
				return 0;
			break;
		}

		core->conf.signal(t, cmd, flags);
		break;

	case FAV_TRACK_SOURCE:
#ifdef FF_WIN
		if (!ffui_file_del(&t->conf.input.url, 1, FFUI_FILE_TRASH))
#else
		if (!ffui_glib_trash(t->conf.input.url, NULL))
#endif
			infolog(t, "Moved to Trash: %s", t->conf.input.url);
		break;

	case FAV_TRACK_STATUS:
		return t->error;

	case FAV_TRACK_PAUSE:
		if (!(t->state & TRK_PAUSED))
			t->state |= TRK_PAUSED;
		else
			t->state &= ~TRK_PAUSED;
		break;

	case FAV_TRACK_SEEK:
		if (flags & FAV_TRACK_SEEK_LOOP) {
			if (!(flags & FAV_TRACK_SEEK_REVERSE)) {
				t->loop_start = ffmax((int64_t)t->sync.pos() - (uint)core->conf.seek_range_margin_msec * 1000, 0) / 1000;
			} else {
				t->loop_end = (t->sync.pos() + (uint)core->conf.seek_range_margin_msec * 1000) / 1000;
				char buf1[64], buf2[64];
				infolog(t, "range: \"%s\"  %s %s"
					, t->conf.input.url
					, time_print(t->loop_start, buf1, sizeof(buf1))
					, time_print(t->loop_end, buf2, sizeof(buf2)));
			}
			return 0;
		}

		break;
	}

	for (uint i = 0;  i < t->conv.n;  i++) {
		const struct fav_track_cu *cu = &t->conv.units[i];
		if (cu->ctl) {
			extralog(t, "ctl %xu: %s", cmd, cu->name);
			cu->ctl(t, cmd, flags);
		}
	}

	return 0;
}

static fav_track* track_find(const void *sdl_wnd)
{
	fav_track **it;
	FFSLICE_WALK(&xt->tracks, it) {
		if (sdl_wnd == (*it)->vo_window)
			return *it;
	}
	return NULL;
}

static int trk_init(fav_track *t)
{
	t->loop_start = t->loop_end = ~0ULL;

	for (uint i = 0;  i < t->conv.n;  i++) {
		const struct fav_track_cu *cu = &t->conv.units[i];
		dbglog(t, "opening '%s'", cu->name);
		t->conv.opened[i] = 1;
		if (cu->open) {

			fftime t1, t2;
			if (ff_unlikely(t->conf.print_time))
				t1 = fftime_monotonic();

			int r = cu->open(t);

			if (ff_unlikely(t->conf.print_time)) {
				t2 = fftime_monotonic();
				fftime_sub(&t2, &t1);
				t->conv.busy_time_nsec[i] += t2.sec * 1000000 + t2.nsec;
			}

			switch (r) {
			case FAV_CU_FWD:
				break;
			case FAV_CU_ERROR:
				return -1;
			default:
				assert(0);
				return -1;
			}
		}
	}
	return 0;
}

static int track_run(fav_track *t)
{
	if (!t->conv.opened[0]) {
		if (trk_init(t))
			return FAV_CU_ERROR;
		return FAV_CU_FWD;
	}

	for (;;) {
		const struct fav_track_cu *cu = &t->conv.units[t->conv.i];
		extralog(t, "calling '%s'", cu->name);

		fftime t1, t2;
		if (ff_unlikely(t->conf.print_time))
			t1 = fftime_monotonic();

		int r = cu->process(t);

		if (ff_unlikely(t->conf.print_time)) {
			t2 = fftime_monotonic();
			fftime_sub(&t2, &t1);
			t->conv.busy_time_nsec[t->conv.i] += t2.sec * 1000000 + t2.nsec;
		}

		extralog(t, " '%s' returned:  %s", cu->name, _fav_cu[r]);

		switch (r) {
		case FAV_CU_FWD:
			if (t->conv.i + 1 < t->conv.n) {
				t->conv.f &= ~FAV_CF_REVERSE;
				t->conv.i++;
				break;
			}
			t->conv.f |= FAV_CF_REVERSE;
			break;

		case FAV_CU_BACK:
			if (t->conv.i == 0) {
				errlog(t, "the first CU asks for more data");
				return -1;
			}
			t->conv.f |= FAV_CF_REVERSE;
			t->conv.i--;
			break;

		case FAV_CU_ASYNC:
			t->conv.f &= ~FAV_CF_REVERSE;
			return t->async_ret;

		case FAV_CU_FIN:
			goto done;

		case FAV_CU_WARN:
			return 0;

		case FAV_CU_ERROR:
			t->error = FAV_TRACK_E_OTHER;
			goto done;

		default:
			assert(0);
			return -1;
		}
	}

done:
	if (t->conf.pause_on_end
		|| t->dec.picture()) {
		t->state |= TRK_PAUSED;
		return 0x7fffffff;
	}

	core->conf.signal(t, FAV_TRACK_STOP, 1);
	return -1;
}

FF_EXTERN int tracks_run()
{
	int n = 0x7fffffff;
	fav_track **it;
	FFSLICE_WALK(&xt->tracks, it) {
		int r = track_run(*it);
		if (r < 0)
			return r;
		n = ffmin(r, n);
	}
	return n;
}

FF_EXTERN const fav_track_if tif = {
	track_create,
	track_close,
	track_cmd,
	track_find,
};
