/** favia
2025, Simon Zolin */

#ifdef _WIN32
#include <util/windows-shell.h>
#else
#include <util/unix-shell.h>
#endif
#include <ffsys/error.h>
#include <core/track.h>
#include <util/util.h>
extern const fav_core_if *core;
#include <f/in.h>
#include <v/vd.h>
#include <f/sync.h>
#include <f/until.h>
#include <ffsys/dir.h>
#include <ffsys/path.h>

// enum FAV_CU
const char _fav_cu[][14] = {
	"FAV_CU_FWD",
	"FAV_CU_BACK",
	"FAV_CU_ASYNC",
	"FAV_CU_ERROR",
	"FAV_CU_DONE",
	"FAV_CU_FIN",
};

struct tracks {
	ffvec tracks; // fav_track*[]
	uint gid;
};
static struct tracks *xt;

void tracks_init()
{
	xt = ffmem_new(struct tracks);
}

void tracks_destroy()
{
	ffvec_free(&xt->tracks);
	ffmem_free(xt);
}

static fav_track* track_create(struct fav_track_conf *conf)
{
	fav_track *t = ffmem_new(fav_track);
	t->conf = *conf;
	xt->gid++;
	ffsz_format(t->id, sizeof(t->id), "*%u", xt->gid);

	for (uint i = 0;  conf->conveyor[i];  i++) {
		t->conv.units[i] = *conf->conveyor[i];
		t->conv.n++;
	}
	assert(t->conv.n <= FF_COUNT(t->conv.opened));

	*ffvec_pushT(&xt->tracks, fav_track*) = t;
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
	ffvec buf = {};
	ffvec_addsz(&buf, "busy time: ");

	for (int i = (int)t->conv.n - 1;  i >= 0;  i--) {
		const struct fav_track_cu *cu = &t->conv.units[i];
		uint64_t nsec = t->conv.busy_time_nsec[i];
		if (!nsec)
			continue;
		uint sec = nsec / 1000000000;
		uint usec = nsec % 1000000000 / 1000;
		ffvec_addfmt(&buf, "%s: %u.%06u, "
			, cu->name, sec, usec);
	}
	buf.len -= FFS_LEN(", ");

	infolog(t, "%S", &buf);
	ffvec_free(&buf);
}

static void track_close(fav_track *t)
{
	const fav_track **p = (const fav_track**)xt->tracks.ptr;
	for (uint i = 0;  i < xt->tracks.len;  i++) {
		if (t == p[i]) {
			ffslice_rmswapT((ffslice*)&xt->tracks, i, 1, fav_track*);
			break;
		}
	}

	if (t->conf.print_time)
		track_busytime_print(t);

	trk_conveyor_close(t);
	if (t->conf.url_transient)
		ffmem_free((char*)t->conf.input.url);
	core->timer(&t->vo.timer, 0, NULL, 0);
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
			return *ffslice_itemT(&xt->tracks, i, fav_track*);
		}
	}
	return NULL;
}

static int trk_src_move(fav_track *t, uint i)
{
	int rc = -1;
	ffstr dir, name;
	ffpath_splitpath_str(FFSTR_Z(t->conf.input.url), &dir, &name);
	if (dir.len)
		dir.len++;
	const char *move_dir = core->conf.move_dir[i];

	char *oname = ffsz_allocfmt("%S%s/%S", &dir, move_dir, &name), *dir_path = NULL;
	if (fffile_rename(t->conf.input.url, oname)) {
		dir_path = ffsz_allocfmt("%S%s%Z", &dir, move_dir);
		if (ffdir_make(dir_path)) {
			warnlog(t, "dir create: %s: %s", dir_path, fferr_strptr(fferr_last()));
			goto end;
		}
		if (fffile_rename(t->conf.input.url, oname)) {
			warnlog(t, "file move: %s: %s", oname, fferr_strptr(fferr_last()));
			goto end;
		}
	}

	rc = 0;
	infolog(t, "File moved: %s", oname);

end:
	ffmem_free(oname);
	ffmem_free(dir_path);
	return rc;
}

static int trk_src_trash(fav_track *t)
{
#ifdef FF_WIN
	if (ffui_file_del(&t->conf.input.url, 1, FFUI_FILE_TRASH)) {
		warnlog(t, "moving file to trash: %s", fferr_strptr(fferr_last()));
		return -1;
	}
#else
	const char *e;
	if (ffui_glib_trash(t->conf.input.url, &e)) {
		warnlog(t, "moving file to trash: %s", e);
		return -1;
	}
#endif

	infolog(t, "Moved to Trash: %s", t->conf.input.url);
	return 0;
}

static void seek_loop(fav_track *t, uint start)
{
	if (start) {
		t->loop_start = ffmax((int64_t)t->cur_pos_msec - (uint)core->conf.seek_range_margin_msec, 0);
	} else {
		t->loop_end = t->cur_pos_msec + (uint)core->conf.seek_range_margin_msec;
		char buf1[64], buf2[64];
		infolog(t, "range: \"%s\"  %s %s"
			, t->conf.input.url
			, time_print(t->loop_start, buf1, sizeof(buf1))
			, time_print(t->loop_end, buf2, sizeof(buf2)));
	}
}

static int track_cmd(fav_track *t, uint cmd, ...)
{
	int r;
	va_list va;
	va_start(va, cmd);

	if (cmd == FAV_TRACK_ADD) {
		char *fn = va_arg(va, char*);
		va_end(va);
		struct fav_track_conf tc = t->conf;
		tc.input.url = fn;
		tc.url_transient = 1;
		track_create(&tc);
		return 0;
	}

	uint flags = va_arg(va, uint);
	uint arg2 = va_arg(va, uint);
	va_end(va);

	switch (cmd) {
	case FAV_TRACK_STOP:
		t->stop = 1;
		track_close(t);
		return 0;

	case FAV_TRACK_START:
	case FAV_TRACK_QUIT:
		core->conf.signal(t, cmd, flags);
		break;

	case FAV_TRACK_ZOOM:
		core->conf.signal(t, cmd, flags);
		break;

	case FAV_TRACK_WINDOW:
		if (flags & FAV_TRACK_WND_SHOWN) {
			t->redraw = 1;

		} else if (flags & FAV_TRACK_WND_RESIZED) {
			t->arg2 = arg2;

		} else if (flags & FAV_TRACK_WND_NEXT) {
			t = tracks_next(t);
			if (!t)
				return 0;

		} else if (flags == FAV_TRACK_WND_ADD
			|| flags == FAV_TRACK_WND_RM) {
			core->conf.signal(t, cmd, flags);
		}
		break;

	case FAV_TRACK_SOURCE:
		if (flags & FAV_TRACK_SRC_MOVE) {
			if (trk_src_move(t, flags - FAV_TRACK_SRC_MOVE))
				return 0;

		} else if (flags == FAV_TRACK_SRC_TRASH) {
			if (trk_src_trash(t))
				return 0;
		}
		break;

	case FAV_TRACK_STATUS:
		return t->error;

	case FAV_TRACK_PAUSE:
		if (!(t->state & TRK_PAUSED)) {
			t->state |= TRK_PAUSED;
			trk_text(t, "%s", "paused");
		} else {
			t->state &= ~TRK_PAUSED;
		}
		break;

	case FAV_TRACK_SEEK:
		if (flags & FAV_TRACK_SEEK_LOOP) {
			seek_loop(t, !(flags & FAV_TRACK_SEEK_REVERSE));
			return 0;
		}

		if (t->static_pic
			&& (flags == FAV_TRACK_SEEK_FWD || flags == FAV_TRACK_SEEK_REVERSE)) {
			// Left/Right arrows -> navigate to prev/next image
			core->conf.signal(t, FAV_TRACK_START, (flags == FAV_TRACK_SEEK_FWD) ? FAV_TRACK_START_NEXT : FAV_TRACK_START_PREV);
			return 0;
		}

		break;

	case FAV_TRACK_VOLUME:
		if (t->static_pic) {
			// Mouse Wheel Up/Down -> navigate to prev/next image
			core->conf.signal(t, FAV_TRACK_START, (flags == FAV_TRACK_SEEK_FWD) ? FAV_TRACK_START_NEXT : FAV_TRACK_START_PREV);
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
		t->conv.opened[i] = 1;
	}
	return 0;
}

static int track_run(fav_track *t)
{
	if (!t->conv.opened[0]) {
		if (trk_init(t))
			return -1;
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
		|| t->picture) {
		t->state |= TRK_PAUSED;
		return 0x7fffffff;
	}
	return -1;
}

/**
Return the N of microseconds to sleep before next action is needed */
int tracks_run()
{
	int n = 0x7fffffff;
	for (uint i = 0;  i < xt->tracks.len;) {
		fav_track **v = xt->tracks.ptr;
		int r = track_run(v[i]);
		if (r < 0) {
			track_close(v[i]);
			continue;
		}
		n = ffmin(r, n);
		i++;
	}
	return n;
}

static int track_video_convert(fav_track *t, struct qframe *f)
{
	int r = ffmpeg_dec_video_convert(t->dec, &f->f);
	if (r)
		errlog(t, "Video frame convert: %s", ffmpeg_dec_error(t->dec));
	return r;
}

static void trk_text_reset(void *param)
{
	fav_track *t = param;
	t->vo.text[0] = '\0';
}

void trk_text_reset_timer(fav_track *t)
{
	core->timer(&t->vo.timer, -2000, trk_text_reset, t);
}

void track_text(fav_track *t, const char *format, ...)
{
	va_list va;
	va_start(va, format);
	ffsz_format(t->vo.text, sizeof(t->vo.text), format, va);
	va_end(va);
	core->timer(&t->vo.timer, -2000, trk_text_reset, t);
}

static uint track_avq_length(struct avqueue *q)
{
	return avqueue_length(q);
}

static struct qframe* track_avq_push(struct avqueue *q)
{
	return avqueue_push(q);
}

static struct qframe* track_avq_peek(struct avqueue *q)
{
	return avqueue_peek(q);
}

static void track_avq_next(struct avqueue *q)
{
	struct qframe *f = avqueue_read(q);
	ffmpeg_frame_unref(&f->f);
}

const fav_track_if tif = {
	track_create,
	track_close,
	track_cmd,
	track_find,
	track_text,
	track_video_convert,

	track_avq_length,
	track_avq_push,
	track_avq_peek,
	track_avq_next,
};
