/** favia
2025, Simon Zolin */

#include <ffaudio/audio.h>
#include <ffaudio/pcm-convert.h>
#include <ffaudio/pcm-gain.h>
#include <ffbase/string.h>
#include <assert.h>

/** Convert volume knob position to dB value. */
#define vol2db(pos, db_min) \
	(((pos) != 0) ? (log10(pos) * (db_min)/2 /*log10(100)*/ - (db_min)) : -100)

#define vol2db_inc(pos, pos_max, db_max) \
	(pow(10, (double)(pos) / (pos_max)) / 10 * (db_max))

/* gain = 10 ^ (db / 20) */
#define db_gain(db)  pow(10, (double)(db) / 20)

struct audio {
	const ffaudio_interface *aif;
	ffaudio_buf *ab;
	struct pcm_af iaf, oaf;
	float buf[64*1024]; // TODO
	uint buf_len_msec, sample_size, icu;
	ffstr input1, input, output, adev_input;
	double gain;
	uint cflags;
};

struct fav_cu {
	int (*process)(struct audio *a);
};

static uint a_init_count;

// enum FAV_CU
static const char _fav_cu[][14] = {
	"FAV_CU_FWD",
	"FAV_CU_BACK",
	"FAV_CU_ASYNC",
	"FAV_CU_ERROR",
	"FAV_CU_DONE",
	"FAV_CU_FIN",
};

extern const struct fav_cu conveyor[4];

static inline void audio_close(struct audio *a)
{
	if (a->ab)
		a->aif->free(a->ab);
	if (a->aif && --a_init_count == 0)
		a->aif->uninit();
}

static inline ffbool audio_open(struct audio *a, ffaudio_conf *ac, ffbool interleaved)
{
	int r;

#ifdef FF_WIN
	a->aif = &ffwasapi;
#else
	a->aif = &ffpulse;
#endif

	ffaudio_init_conf conf = {};
	conf.app_name = "favia";
	if (a_init_count++ == 0 && a->aif->init(&conf)) {
		fav_errlog("Audio subsystem init: %s", conf.error);
		return 0;
	}

	a->ab = a->aif->alloc();
	ac->app_name = "favia";
	ac->buffer_length_msec = 500;
	r = a->aif->open(a->ab, ac, FFAUDIO_PLAYBACK | FFAUDIO_O_NONBLOCK);
	// TODO if (r == FFAUDIO_EFORMAT)
	// 	r = a->aif->open(a->ab, &ac, FFAUDIO_PLAYBACK | FFAUDIO_O_NONBLOCK);
	if (r) {
		fav_errlog("audio device open: %d: %s", r, a->aif->error(a->ab));
		return 0;
	}
	a->buf_len_msec = ac->buffer_length_msec;

	a->iaf.format = ac->format;
	a->iaf.channels = ac->channels;
	a->iaf.rate = ac->sample_rate;
	a->iaf.interleaved = interleaved;
	a->oaf = a->iaf;
	a->oaf.interleaved = 1;

	assert(a->iaf.channels <= 8);
	if (pcm_convert(&a->oaf, NULL, &a->iaf, NULL, 0)) {
		fav_errlog("audio conversion not supported");
		return 0;
	}

	a->gain = 1;
	a->sample_size = pcm_f_bits(a->iaf.format)/8 * a->iaf.channels;
	return 1;
}

static inline int audio_aconv_process(struct audio *a)
{
	if (a->cflags & FAV_CF_REVERSE)
		return FAV_CU_BACK;

	const void *aa[8];
	const void *in = a->input1.ptr;
	if (!a->iaf.interleaved) {
		for (uint i = 0;  i < a->iaf.channels;  i++) {
			aa[i] = ((void**)in)[i];
		}
		in = aa;
	}
	pcm_convert(&a->oaf, a->buf, &a->iaf, in, a->input1.len / a->sample_size);
	ffstr_set(&a->output, a->buf, a->input1.len);
	return FAV_CU_FWD;
}

static inline int audio_avol_process(struct audio *a)
{
	if (a->cflags & FAV_CF_REVERSE)
		return FAV_CU_BACK;

	a->output = a->input;
	if (a->gain != 1) {
		if (pcm_gain(&a->oaf, a->gain, a->input.ptr, a->buf, a->input.len / a->sample_size)) {
			assert(0);
			return FAV_CU_ERROR;
		}
		ffstr_set(&a->output, a->buf, a->input.len);
	}
	return FAV_CU_FWD;
}

static inline int audio_adev_process(struct audio *a)
{
	if (!(a->cflags & FAV_CF_REVERSE))
		a->adev_input = a->input;

	int r = a->aif->write(a->ab, a->adev_input.ptr, a->adev_input.len);
	if (r < 0) {
		fav_errlog("audio I/O: %s", a->aif->error(a->ab));
		return FAV_CU_ERROR;
	} else if (r != a->adev_input.len) {
		ffstr_shift(&a->adev_input, r);
		return FAV_CU_ASYNC;
	}
	return FAV_CU_BACK;
}

static inline int audio_write(struct audio *a, ffstr data)
{
	int r;
	a->input1 = data;

	for (;;) {
		const struct fav_cu *cu = conveyor + a->icu;
		int r = cu->process(a);
		fav_dbglog("CU#%u:  %s  output:%L"
			, a->icu, _fav_cu[r], a->output.len);
		a->cflags = 0;
		switch (r) {
		case FAV_CU_FWD:
			if (!conveyor[a->icu + 1].process) {
				assert(0);
				return -1;
			}
			a->input = a->output;
			ffstr_null(&a->output);
			a->icu++;
			break;

		case FAV_CU_BACK:
			if (a->icu == 0)
				return 0;
			ffstr_null(&a->input);
			ffstr_null(&a->output);
			a->icu--;
			a->cflags = FAV_CF_REVERSE;
			break;

		case FAV_CU_ASYNC:
			return 1;

		default:
			assert(0);
			return -1;
		}
	}
}

static inline void audio_clear(struct audio *a)
{
	a->aif->stop(a->ab);
	a->aif->clear(a->ab);
}

static inline void audio_pause(struct audio *a, ffbool paused)
{
	if (paused)
		a->aif->stop(a->ab);
	else
		a->aif->start(a->ab);
}

static inline void audio_volume(struct audio *a, uint percent)
{
	double db;
	if (percent <= 100)
		db = vol2db(percent, 48);
	else
		db = vol2db_inc(percent - 100, 125 - 100, 6);
	a->gain = db_gain(db);
}
