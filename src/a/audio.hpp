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

struct audio;
struct fav_cu {
	int (*process)(struct audio *a);
};

static uint a_init_count;
extern const char _fav_cu[][14];

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
		if (ab)
			delete ab;
		if (audio && --a_init_count == 0)
			audio->uninit();
	}

	bool open(ffaudio_conf &ac, bool interleaved) {
		int r;

#ifdef FF_WIN
		audio = &ffwasapi;
#else
		audio = &ffpulse;
#endif

		ffaudio_init_conf conf = {};
		conf.app_name = "favia";
		if (a_init_count++ == 0 && audio->init(&conf)) {
			fav_errlog("Audio subsystem init: %s", conf.error);
			return 0;
		}

		ab = new xxffaudio_play_buf(audio);
		ac.app_name = "favia";
		ac.buffer_length_msec = 500;
		r = ab->open(&ac, FFAUDIO_O_NONBLOCK);
		// TODO if (r == FFAUDIO_EFORMAT)
		// 	r = ab->open(&ac, FFAUDIO_O_NONBLOCK);
		if (r) {
			fav_errlog("audio device open: %d: %s", r, ab->error());
			return 0;
		}
		this->buf_len_msec = ac.buffer_length_msec;

		iaf.format = ac.format;
		iaf.channels = ac.channels;
		iaf.rate = ac.sample_rate;
		iaf.interleaved = interleaved;
		oaf = iaf;
		oaf.interleaved = 1;

		assert(iaf.channels <= 8);
		if (pcm_convert(&oaf, NULL, &iaf, NULL, 0)) {
			fav_errlog("audio conversion not supported");
			return 0;
		}

		gain = 1;
		sample_size = pcm_f_bits(iaf.format)/8 * iaf.channels;
		return 1;
	}

	static int aconv_process(struct audio *a) {
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
			fav_errlog("audio I/O: %s", a->ab->error());
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

	int write(ffstr data) {
		int r;
		input1 = data;

		for (;;) {
			const struct fav_cu *cu = conveyor + icu;
			int r = cu->process(this);
			fav_dbglog("CU#%u:  %s  output:%L"
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
