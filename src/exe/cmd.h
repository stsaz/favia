/** favia: CLI
2025, Simon Zolin */

#include <ffsys/std.h>
#include <ffbase/args.h>

static int arg_help(struct exe *x) {
	ffstdout_fmt("%s", "\
Usage:\n\
    favia [OPTIONS] INPUT\n\
\n\
INPUT           File name\n\
\n\
Options:\n\
  -Debug        Enable debug logging\n\
\n\
  -seek TIME    Seek to position (minutes)\n\
  -nodisplay    Don't display video\n\
  -nosound      Don't play audio\n\
");
	return 1;
}

static int arg_seek(struct exe *x, uint pos) {
	x->seek_msec = pos * 60 * 1000;
	return 0;
}

static int arg_input(struct exe *x, const char *s) {
	x->fn = s;
	return 0;
}

#define O(m)  (void*)FF_OFF(struct exe, m)
static const struct ffarg cmd_root[] = {
	{ "-Debug",		'1',	O(debug) },

	{ "-help",		'1',	(void*)arg_help },
	{ "-nodisplay",	'1',	O(no_display) },
	{ "-nosound",	'1',	O(no_sound) },
	{ "-seek",		'u',	(void*)arg_seek },

	{ "\0\1",		's',	(void*)arg_input },
	{}
};
#undef O

int cmd(int argc, char **argv) {
	x->hwaccel = "vaapi";

	uint f = FFARGS_O_PARTIAL | FFARGS_O_DUPLICATES | FFARGS_O_SKIP_FIRST;
	struct ffargs a = {};
	int r = ffargs_process_argv(&a, cmd_root, x, f, argv, argc);
	if (r) {
		exe_errlog("%s", a.error);
		return r;
	}

	if (!x->fn) {
		ffstdout_fmt("%s", "\
Usage:\n\
    favia [OPTIONS] INPUT\n\
Run favia -help for complete help info.\n\
");
		r = 1;
	}
	return r;
}
