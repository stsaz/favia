/** favia: CLI
2025, Simon Zolin */

#include <ffsys/std.h>
#include <ffbase/args.h>

static void help_info_write(const char *sz)
{
	ffstr s = FFSTR_Z(sz), l, k;
	ffvec v = {};

	const char *clr = FFSTD_CLR_B(FFSTD_PURPLE);
	while (s.len) {
		ffstr_splitby(&s, '`', &l, &s);
		ffstr_splitby(&s, '`', &k, &s);
		if (x->log.use_color) {
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

static int arg_help(struct args *a)
{
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
  `-autodir`          Enqueue sibling files\n\
  `-nodisplay`        Don't display video\n\
  `-nosound`          Don't play audio\n\
\n\
  `-perf`             Print perf counters\n\
");
	x->exit_code = 0;
	return 1;
}

static void help_short()
{
	ffstdout_fmt("%s", "\
Usage:\n\
    favia [OPTIONS] INPUT\n\
Run favia -help for complete help info.\n\
");
}

static int arg_seek(struct args *a, ffstr s)
{
	ffdatetime dt = {};
	if (s.len != fftime_fromstr1(&dt, s.ptr, s.len, FFTIME_HMS_MSEC_VAR))
		return _ffargs_err(a->cmd, 1, "incorrect time value '%S'", &s);

	fftime t;
	fftime_join1(&t, &dt);
	a->seek_msec = fftime_to_msec(&t);
	return 0;
}

static int arg_until(struct args *a, ffstr s)
{
	ffdatetime dt = {};
	if (s.len != fftime_fromstr1(&dt, s.ptr, s.len, FFTIME_HMS_MSEC_VAR))
		return _ffargs_err(a->cmd, 1, "incorrect time value '%S'", &s);

	fftime t;
	fftime_join1(&t, &dt);
	a->until_msec = fftime_to_msec(&t);
	return 0;
}

static int arg_input(struct args *a, const char *s)
{
	if (s[0] == '-')
		return _ffargs_err(a->cmd, 1, "unknown option '%s'. Use '-h' for usage info.", s);

	*ffvec_pushT(&a->input, const char*) = s;
	return 0;
}

static int bash_completion();

#define O(m)  (void*)FF_OFF(struct args, m)
static const struct ffarg cmd_root[] = {
	{ "-Debug",		'1',	O(debug) },

	{ "-autodir",	'1',	O(autodir) },
	{ "-h",			'1',	(void*)arg_help },
	{ "-help",		'1',	(void*)arg_help },
	{ "-hwaccel",	's',	O(hwaccel) },
	{ "-mute",		'1',	O(mute) },
	{ "-nodisplay",	'1',	O(no_display) },
	{ "-nosound",	'1',	O(no_sound) },
	{ "-parallel",	'u',	O(parallel) },
	{ "-perf",		'1',	O(perf) },
	{ "-poe",		'1',	O(pause_on_end) },
	{ "-repeat",	'1',	O(repeat) },
	{ "-seek",		'S',	(void*)arg_seek },
	{ "-until",		'S',	(void*)arg_until },
	{ "-volume",	'u',	O(volume) },
	{ "-zoom",		'u',	O(zoom) },

	{ "__bash_completion",0,bash_completion },

	{ "\0\1",		's',	(void*)arg_input },
	{}
};
#undef O

int cmd(int argc, char **argv, const char *cmd_line)
{
	uint f = FFARGS_O_PARTIAL | FFARGS_O_DUPLICATES | FFARGS_O_SKIP_FIRST;
	struct ffargs a = {};
	x->args.cmd = &a;
	x->args.zoom = 100;
	int r;

#ifdef FF_WIN
	r = ffargs_process_line(&a, cmd_root, &x->args, f, cmd_line);
#else
	r = ffargs_process_argv(&a, cmd_root, &x->args, f, argv, argc);
#endif

	if (r) {
		if (x->exit_code)
			exe_errlog("%s", a.error);
		return r;
	}

	if (!x->args.input.len) {
		help_short();
		r = 1;
	}
	return r;
}

/** Print the bash-completion script to stdout */
static int bash_completion()
{
	static const char templ_s_s[] = "\
_favia_completions() {\n\
    local cur=\"${COMP_WORDS[COMP_CWORD]}\"\n\
    local opts=\"\"\n\
\n\
    if [[ ${COMP_CWORD} -eq 1 ]]; then\n\
        opts=\"%s\"\n\
    fi\n\
\n\
    compopt -o filenames\n\
    if [[ -n \"$opts\" ]]; then\n\
        mapfile -t COMPREPLY < <(compgen -W \"$opts\" -- \"$cur\")\n\
    else\n\
        mapfile -t COMPREPLY < <(compgen -f -- \"$cur\")\n\
    fi\n\
}\n\
\n\
complete -F _favia_completions favia\n\
";

	ffvec buf = {}, cmds = {}, params = {};

	for (const struct ffarg *a = cmd_root;  a->name[0];  a++) {
		if (a->name[0] == '_')
			continue;

		ffvec_addfmt(&cmds, "%s ", a->name);
	}
	cmds.len--;
	((char*)cmds.ptr)[cmds.len] = '\0';

	ffvec_addfmt(&buf, templ_s_s, cmds.ptr, params.len ? params.ptr : "");
	ffvec_free(&cmds);
	ffvec_free(&params);

	ffstdout_write(buf.ptr, buf.len);
	ffvec_free(&buf);
	x->exit_code = 0;
	return 1;
}
