/** favia: CLI
2025, Simon Zolin */

#include <ffsys/std.h>
#include <ffbase/args.h>

static void help_info_write(const char *sz)
{
	ffstr s = FFSTR_INITZ(sz), l, k;
	ffvec v = {};

	const char *clr = FFSTD_CLR_B(FFSTD_PURPLE);
	while (s.len) {
		ffstr_splitby(&s, '`', &l, &s);
		ffstr_splitby(&s, '`', &k, &s);
		if (x->use_color) {
			ffvec_addfmt(&v, "%S%s%S%s"
				, &l, clr, &k, FFSTD_CLR_RESET);
		} else {
			ffvec_addfmt(&v, "%S%S"
				, &l, &k);
		}
	}

	ffstdout_write(v.ptr, v.len);
	ffvec_free(&v);
}

static int arg_help(struct exe *x) {
	help_info_write("\
Usage:\n\
    favia [OPTIONS] INPUT...\n\
\n\
INPUT               File or directory\n\
\n\
Options:\n\
  `-Debug`            Enable debug logging\n\
\n\
  `-hwaccel` STR      Use specific hardware video decoding API\n\
  `-seek` TIME        Seek to time: [[HH:]MM:]SS[.MSC]\n\
  `-until` TIME       Stop at time\n\
  `-repeat`           Repeat all input files\n\
  `-poe`              Pause at the end\n\
\n\
  `-zoom` PERCENT     Zoom window\n\
\n\
  `-mute`             Mute\n\
  `-volume` PERCENT   Set audio volume\n\
\n\
  `-parallel` N       Play N files in parallel\n\
  `-nodisplay`        Don't display video\n\
  `-nosound`          Don't play audio\n\
");
	return 1;
}

static int arg_seek(struct exe *x, ffstr s) {
	ffdatetime dt = {};
	if (s.len != fftime_fromstr1(&dt, s.ptr, s.len, FFTIME_HMS_MSEC_VAR))
		return _ffargs_err(x->cmd, 1, "incorrect time value '%S'", &s);

	x->seek_msec = xxtime(dt).to_msec();
	return 0;
}

static int arg_until(struct exe *x, ffstr s) {
	ffdatetime dt = {};
	if (s.len != fftime_fromstr1(&dt, s.ptr, s.len, FFTIME_HMS_MSEC_VAR))
		return _ffargs_err(x->cmd, 1, "incorrect time value '%S'", &s);

	x->until_msec = xxtime(dt).to_msec();
	return 0;
}

static int arg_input(struct exe *x, const char *s) {
	if (s[0] == '-')
		return _ffargs_err(x->cmd, 1, "unknown option '%s'. Use '-h' for usage info.", s);

	*x->input.push<const char*>() = s;
	return 0;
}

#define O(m)  (void*)FF_OFF(struct exe, m)
static const struct ffarg cmd_root[] = {
	{ "-Debug",		'1',	O(debug) },

	{ "-help",		'1',	(void*)arg_help },
	{ "-hwaccel",	's',	O(hwaccel) },
	{ "-mute",		'1',	O(mute) },
	{ "-nodisplay",	'1',	O(no_display) },
	{ "-nosound",	'1',	O(no_sound) },
	{ "-parallel",	'u',	O(parallel) },
	{ "-poe",		'1',	O(pause_on_end) },
	{ "-repeat",	'1',	O(repeat) },
	{ "-seek",		'S',	(void*)arg_seek },
	{ "-until",		'S',	(void*)arg_until },
	{ "-volume",	'u',	O(volume) },
	{ "-zoom",		'u',	O(zoom) },

	{ "\0\1",		's',	(void*)arg_input },
	{}
};
#undef O

int cmd(int argc, char **argv, const char *cmd_line) {
	x->hwaccel = "vaapi";
#ifdef FF_WIN
	x->hwaccel = "d3d11va";
#endif

	uint f = FFARGS_O_PARTIAL | FFARGS_O_DUPLICATES | FFARGS_O_SKIP_FIRST;
	struct ffargs a = {};
	x->cmd = &a;
	int r;

#ifdef FF_WIN
	r = ffargs_process_line(&a, cmd_root, x, f, cmd_line);
#else
	r = ffargs_process_argv(&a, cmd_root, x, f, argv, argc);
#endif

	if (r) {
		exe_errlog("%s", a.error);
		return r;
	}

	if (!x->input.len) {
		ffstdout_fmt("%s", "\
Usage:\n\
    favia [OPTIONS] INPUT\n\
Run favia -help for complete help info.\n\
");
		r = 1;
	}
	return r;
}
