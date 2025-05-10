/** favia
2025, Simon Zolin */

#include <ffaudio/audio.h>
#include <ffaudio/pcm-convert.h>
#include <assert.h>

struct audio {
	const ffaudio_interface *audio;
	xxffaudio_play_buf *ab;
	struct pcm_af iaf, oaf;
	float buf[64*1024];
	uint offset, buf_len_msec;

	~audio() {
		delete ab;
		if (audio)
			audio->uninit();
	}

	static int format_ffa_av(uint af, bool *interleaved)
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
			if (af == map[i].av) {
				*interleaved = map[i].il;
				return map[i].ffa;
			}
		}
		return -1;
	}

	bool open(const xxffmpeg_dec &dec) {
		int r;
		bool interleaved;
		if (0 > (r = format_ffa_av(dec.audio_format(), &interleaved))) {
			errlog("AV audio format is not supported: %d", dec.audio_format());
			return 0;
		}

		audio = &ffpulse;

		ffaudio_init_conf conf = {};
		conf.app_name = "favia";
		if (audio->init(&conf)) {
			errlog("Audio subsystem init");
			return 0;
		}

		ab = new xxffaudio_play_buf(audio);
		ffaudio_conf bufconf = {};
		bufconf.app_name = "favia";
		bufconf.format = r;
		bufconf.sample_rate = dec.audio_rate();
		bufconf.channels = dec.audio_channels();
		bufconf.buffer_length_msec = 500;
		r = ab->open(&bufconf, FFAUDIO_O_NONBLOCK);
		// if (r == FFAUDIO_EFORMAT)
		// 	r = ab->open(&bufconf, FFAUDIO_O_NONBLOCK);
		if (r) {
			errlog("audio device open: %d: %s", r, ab->error());
			return 0;
		}
		this->buf_len_msec = bufconf.buffer_length_msec;

		iaf.format = bufconf.format;
		iaf.channels = dec.audio_channels();
		iaf.rate = dec.audio_rate();
		iaf.interleaved = interleaved;
		oaf = iaf;
		oaf.interleaved = 1;
		return 1;
	}

	int write(AVFrame *f) {
		int r;
		bool interleaved;
		if (0 > (r = format_ffa_av(f->format, &interleaved))) {
			errlog("AV audio format is not supported: %d", f->format);
			return -1;
		}
		assert(r == iaf.format);
		assert(interleaved == iaf.interleaved);
		assert(f->sample_rate == iaf.rate);
		assert(f->ch_layout.nb_channels == iaf.channels);
		void *p;
		uint n;
		if (!iaf.interleaved) {
			const void *a[8];
			assert(iaf.channels < FF_COUNT(a));
			for (uint i = 0;  i < iaf.channels;  i++) {
				a[i] = (char*)f->data[0] + offset / iaf.channels;
			}
			if (pcm_convert(&oaf, buf, &iaf, a, f->nb_samples - (offset / (iaf.channels * pcm_f_bits(oaf.format)/8)))) {
				errlog("audio conversion not supported");
				return -1;
			}
			n = f->nb_samples * oaf.channels * pcm_f_bits(oaf.format)/8;
			p = buf;
		} else {
			p = f->data[0] + offset;
			n = f->linesize[0];
		}
		n -= offset;
		r = ab->write(p, n);
		if (r == 0) {
			return 1;
		} else if (r <= 0) {
			errlog("audio I/O: %s", ab->error());
			return -1;
		}
		if (r != n) {
			offset += r;
			return 1;
		}
		offset = 0;
		return 0;
	}

	void clear() {
		ab->stop();
		ab->clear();
		offset = 0;
	}

	void pause(bool paused) {
		if (paused)
			ab->stop();
		else
			ab->start();
	}
};
