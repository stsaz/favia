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

enum {
	FAV_CF_REVERSE = 1,
};

enum FAV_CU {
	FAV_CU_FWD,
	FAV_CU_BACK,
	FAV_CU_ASYNC,
	FAV_CU_ERROR,
};
static const char _fav_cu[][14] = {
	"FAV_CU_FWD",
	"FAV_CU_BACK",
	"FAV_CU_ASYNC",
	"FAV_CU_ERROR",
};

struct audio;
struct fav_cu {
	int (*process)(struct audio *a);
};

static int format_ffa_avf(int avf, bool *interleaved)
{
	static const struct {
		int av;
		ushort ffa, il;
	} map[] = {
		{ AV_SAMPLE_FMT_S16, FFAUDIO_F_INT16, 1 },
		{ AV_SAMPLE_FMT_S16P, FFAUDIO_F_INT16, 0 },
		{ AV_SAMPLE_FMT_FLT, FFAUDIO_F_FLOAT32, 1 },
		{ AV_SAMPLE_FMT_FLTP, FFAUDIO_F_FLOAT32, 0 },
	};
	for (uint i = 0;  i < FF_COUNT(map);  i++) {
		if (avf == map[i].av) {
			*interleaved = map[i].il;
			return map[i].ffa;
		}
	}
	return -1;
}

static uint a_init_count;

struct audio {
	const ffaudio_interface *audio;
	xxffaudio_play_buf *ab;
	struct pcm_af iaf, oaf;
	float buf[64*1024]; // TODO
	uint buf_len_msec, sample_size, icu;
	ffstr input1, input, output, adev_input;
	double gain;
	uint cflags;

	~audio() {
		delete ab;
		if (audio && --a_init_count == 0)
			audio->uninit();
	}

	bool open(const xxffmpeg_dec &dec) {
		int r;
		bool interleaved;
		if (0 > (r = format_ffa_avf(dec.audio_format(), &interleaved))) {
			errlog("AVF audio format is not supported: %d", dec.audio_format());
			return 0;
		}

		audio = &ffpulse;

		ffaudio_init_conf conf = {};
		conf.app_name = "favia";
		if (a_init_count++ == 0 && audio->init(&conf)) {
			errlog("Audio subsystem init: %s", conf.error);
			return 0;
		}

		ab = new xxffaudio_play_buf(audio);
		ffaudio_conf ac = {};
		ac.app_name = "favia";
		ac.format = r;
		ac.sample_rate = dec.audio_rate();
		ac.channels = dec.audio_channels();
		ac.buffer_length_msec = 500;
		r = ab->open(&ac, FFAUDIO_O_NONBLOCK);
		// TODO if (r == FFAUDIO_EFORMAT)
		// 	r = ab->open(&ac, FFAUDIO_O_NONBLOCK);
		if (r) {
			errlog("audio device open: %d: %s", r, ab->error());
			return 0;
		}
		this->buf_len_msec = ac.buffer_length_msec;

		iaf.format = ac.format;
		iaf.channels = dec.audio_channels();
		iaf.rate = dec.audio_rate();
		iaf.interleaved = interleaved;
		oaf = iaf;
		oaf.interleaved = 1;

		assert(iaf.channels <= 8);
		if (pcm_convert(&oaf, NULL, &iaf, NULL, 0)) {
			errlog("audio conversion not supported");
			return 0;
		}

		gain = 1;
		sample_size = pcm_f_bits(iaf.format)/8 * iaf.channels;
		return 1;
	}

	static int aconv_process(struct audio *a) {
		if (a->cflags & FAV_CF_REVERSE)
			return FAV_CU_BACK;

		const void *in = a->input1.ptr;
		if (!a->iaf.interleaved) {
			const void *aa[8];
			for (uint i = 0;  i < a->iaf.channels;  i++) {
				aa[i] = ((void**)in)[i];
			}
			in = aa;
		}
		pcm_convert(&a->oaf, a->buf, &a->iaf, in, a->input1.len / a->sample_size);
		ffstr_set(&a->output, a->buf, a->input1.len);
		return FAV_CU_FWD;
	}
	static constexpr struct fav_cu aconv_cu = { aconv_process };

	static int avol_process(struct audio *a) {
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
	static constexpr struct fav_cu avol_cu = { avol_process };

	static int adev_process(struct audio *a) {
		if (!(a->cflags & FAV_CF_REVERSE))
			a->adev_input = a->input;

		int r = a->ab->write(a->adev_input.ptr, a->adev_input.len);
		if (r < 0) {
			errlog("audio I/O: %s", a->ab->error());
			return FAV_CU_ERROR;
		} else if (r != a->adev_input.len) {
			ffstr_shift(&a->adev_input, r);
			return FAV_CU_ASYNC;
		}
		return FAV_CU_BACK;
	}
	static constexpr struct fav_cu adev_cu = { adev_process };

	static constexpr struct fav_cu conveyor[] = {
		aconv_cu,
		avol_cu,
		adev_cu,
		{},
	};

	int write(AVFrame *f) {
		int r;
		bool interleaved;
		if (0 > (r = format_ffa_avf(f->format, &interleaved))) {
			errlog("AV audio format is not supported: %d", f->format);
			return -1;
		}
		assert(r == iaf.format);
		assert(interleaved == iaf.interleaved);
		assert(f->sample_rate == iaf.rate);
		assert(f->ch_layout.nb_channels == iaf.channels);

		ffstr_set(&input1, (interleaved) ? (char*)f->data[0] : (char*)f->data, f->nb_samples * sample_size);

		for (;;) {
			const struct fav_cu *cu = conveyor + icu;
			int r = cu->process(this);
			dbglog("CU#%u:  %s  output:%L"
				, icu, _fav_cu[r], output.len);
			cflags = 0;
			switch (r) {
			case FAV_CU_FWD:
				if (!conveyor[icu + 1].process) {
					assert(0);
					return -1;
				}
				input = output;
				ffstr_null(&output);
				icu++;
				break;

			case FAV_CU_BACK:
				if (icu == 0)
					return 0;
				ffstr_null(&input);
				ffstr_null(&output);
				icu--;
				cflags = FAV_CF_REVERSE;
				break;

			case FAV_CU_ASYNC:
				return 1;

			default:
				assert(0);
				return -1;
			}
		}
	}

	void clear() {
		ab->stop();
		ab->clear();
	}

	void pause(bool paused) {
		if (paused)
			ab->stop();
		else
			ab->start();
	}

	void volume(uint percent) {
		double db;
		if (percent <= 100)
			db = vol2db(percent, 48);
		else
			db = vol2db_inc(percent - 100, 125 - 100, 6);
		gain = db_gain(db);
	}
};
