/** favia: queue
2026, Simon Zolin */

#include <core/track.h>
#include <util/util.h>
#include <ffbase/vector.h>
#include <ffbase/fntree.h>

struct queue {
	struct fav_q_conf conf;
	void (*changed)(uint flags);
	uint cursor, n_tracks;
	ffvec input; // const char*[]
};

static struct queue *q;

static void q_on_change(void (*f)(uint flags))
{
	q->changed = f;
}

void q_init()
{
	q = ffmem_new(struct queue);
}

static void q_create(struct fav_q_conf *qc)
{
	q->conf = *qc;
}

static uint q_count()
{
	return q->input.len;
}

static void q_add(const char **filenames, uint n)
{
	ffvec_addT(&q->input, filenames, n, char*);
}

static void q_ins(const char *fn, uint pos)
{
	ffvec_insert(&q->input, pos, &fn, 1, sizeof(char*));
}

static int _dir_read(const char *dir_path, uint ins_pos, uint flags, ffstr sel_file_name)
{
	int rc = 1;
	ffdirscan ds = {};
	fntree_block *root = NULL, *blk;
	char *fpath = NULL;
	fntree_cursor cur = {};

	if (ffdirscan_open(&ds, dir_path, FFDIRSCAN_NOSORT))
		goto end;
	dirscan_sort(&ds);

	if (!(root = fntree_from_dirscan(FFSTR_Z(dir_path), &ds, 0)))
		goto end;
	blk = root;
	ffdirscan_close(&ds);

	for (;;) {
		fntree_entry *e;
		if (!(e = fntree_cur_next_r_ctx(&cur, &blk)))
			break;

		ffstr path = fntree_path(blk);
		ffstr name = fntree_name(e);
		ffmem_free(fpath);
		fpath = ffsz_allocfmt("%S%c%S", &path, FFPATH_SLASH, &name);

		fffileinfo fi;
		if (fffile_info_path(fpath, &fi))
			continue;
		if (fffile_isdir(fffileinfo_attr(&fi))) {
			if (!(flags & 1))
				continue;
			ffmem_zero_obj(&ds);
			if (ffdirscan_open(&ds, fpath, FFDIRSCAN_NOSORT))
				continue;
			dirscan_sort(&ds);

			ffstr_setz(&path, fpath);
			if (!(blk = fntree_from_dirscan(path, &ds, 0)))
				continue;
			ffdirscan_close(&ds);

			fntree_attach(e, blk);
			continue;
		}

		if (sel_file_name.len && ffstr_eq2(&name, &sel_file_name))
			q->cursor = ins_pos;

		q_ins(fpath, ins_pos++);
		fav_dbglog("input queue: add \"%s\"", fpath);
		fpath = NULL;
	}

	rc = 0;

end:
	ffmem_free(fpath);
	ffdirscan_close(&ds);
	fntree_free_all(root);
	return rc;
}

int dir_read(const char *dir_path, uint ins_pos, uint flags)
{
	return _dir_read(dir_path, ins_pos, flags, FFSTR_Z(""));
}

int autodir_read(const char *dir_path, ffstr file_name)
{
	return _dir_read(dir_path, 0, 0, file_name);
}

static const char* input_get()
{
	while (q->cursor < q->input.len) {
		const char *fn = *ffslice_itemT(&q->input, q->cursor, char*);
		fffileinfo fi = {};
		fffile_info_path(fn, &fi);
		if (fffile_isdir(fffileinfo_attr(&fi))) {
			ffslice_rm((ffslice*)&q->input, q->cursor, 1, sizeof(char*));
			dir_read(fn, q->cursor, 1);
			continue;
		}
		return fn;
	}
	return NULL;
}

static int cursor_move(int delta)
{
	if (delta == 0x80000000) {
		q->cursor = 0;
		return 0;
	} else if (delta == 0x7fffffff) {
		q->cursor = ffmax((int)q->input.len - 1, 0);
		return 0;
	}

	int i = q->cursor + delta;
	if (!q->conf.repeat) {
		i = ffmax(i, 0);
		i = ffmin(i, q->input.len - 1);
		if (i < 0 || i == q->cursor)
			return 1;
	}
	if (i < 0 || i >= q->input.len) {
		if (i < 0)
			i = q->input.len - 1;
		else
			i = 0;
	}
	q->cursor = i;
	fav_dbglog("cursor: %u", q->cursor);
	return 0;
}

static void q_play(int delta)
{
	if (delta != 0) {
		if (cursor_move(delta))
			return;
	}

	for (;;) {
		const char *fn = input_get();
		if (!fn)
			break;

		struct fav_track_conf tc = q->conf.tconf;
		tc.input.url = fn;
		fav_track *t = core->track->create(&tc);
		q->n_tracks++;

		ffstr name;
		ffpath_splitpath_str(FFSTR_Z(fn), NULL, &name);
		trk_text(t, "%S", &name);

		if (!q->conf.parallel)
			break;

		if (q->n_tracks == q->conf.parallel
			|| cursor_move(1))
			break;
	}
}

static int cursor_delta(uint flags)
{
	switch (flags) {
	case FAV_TRACK_START_FIRST:
		return 0x80000000;
	case FAV_TRACK_START_LAST:
		return 0x7fffffff;

	case FAV_TRACK_START_PGPREV:
		return -10;
	case FAV_TRACK_START_PGNEXT:
		return 10;

	case FAV_TRACK_START_PREV:
		return -1;
	case FAV_TRACK_START_NEXT:
		return 1;
	}
	return 0;
}

static void q_signal(fav_track *t, uint cmd, uint flags)
{
	switch (cmd) {
	case FAV_TRACK_START:
		if (cursor_move(cursor_delta(flags)))
			return;

		t->next = 1;
		core->track->close(t);
		q_play(0);
		break;

	case FAV_TRACK_ZOOM: {
		uint z = q->conf.tconf.video.zoom;
		if (flags == FAV_TRACK_ZOOM_IN)
			z = ffmin(z + core->conf.zoom_by_pct, 400);
		else
			z = ffmax((int)z - core->conf.zoom_by_pct, 10);
		z = ffint_align_floor(z, 10);
		q->conf.tconf.video.zoom = z;
		q->conf.tconf.video.zoom_lock_wnd = (z != 100);

		t->conf.video.zoom = z;
		t->conf.video.zoom_lock_wnd = (z != 100);
		dbglog(t, "Zoom: %u%%", z);
		trk_text(t, "%u%%", z);
		break;
	}

	case FAV_TRACK_WINDOW:
		if (flags == FAV_TRACK_WND_RM) {
			// remove window
			if (q->n_tracks > 1) {
				t->stop = 1;
				core->track->close(t);
			}
			return;
		}

		// add window
		if (!q->conf.parallel)
			q->conf.parallel = 1;
		q->conf.parallel++;
		q_play(0);
		break;
	}
}

static struct fav_q_conf* q_conf()
{
	return &q->conf;
}

const struct fav_q_if qif = {
	q_on_change,
	q_create,
	q_add,
	q_ins,
	q_play,
	q_count,
	q_signal,
	q_conf,
};


static int qegrd_open(fav_track *t)
{
	return FAV_CU_FWD;
}

static void qegrd_close(fav_track *t)
{
	q->n_tracks--;

	if (t->next) {
		// starting next track
	} else if (t->stop) {
		// user closed a window
		if (q->conf.parallel)
			q->conf.parallel--;
		if (!q->conf.parallel)
			core->conf.signal(NULL, FAV_TRACK_QUIT, 0);
	} else {
		// track finished
		if (!cursor_move(1))
			q_play(0);
		else
			q->changed('.');
	}
}

static int qegrd_process(fav_track *t)
{
	return FAV_CU_FWD;
}

static int qegrd_cmd(fav_track *t, uint cmd, uint flags)
{
	switch (cmd) {
	case FAV_TRACK_SOURCE:
		q_signal(t, FAV_TRACK_START, FAV_TRACK_START_NEXT);
	}
	return 0;
}

const struct fav_track_cu trk_cu_guard = { "q-guard", qegrd_open, qegrd_close, qegrd_process, qegrd_cmd };
