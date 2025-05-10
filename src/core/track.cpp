/** favia
2025, Simon Zolin */

#include <favia.h>

FF_EXTERN const fav_core_if *core;

#include <util/ffmpeg.h>
#include <util/SDL.hpp>
#include <audio.hpp>
#include <core/avsync.hpp>

struct qframe {
	struct ffmpeg_frame frame;
	uint64_t ts;
	uint dur;

	void alloc() {
		ffmpeg_frame_init(&frame);
	}
	void destroy() {
		ffmpeg_frame_destroy(&frame);
	}
	void unref() {
		ffmpeg_frame_unref(&frame);
	}
};

#include <core/queue.hpp>

int user_events(fav_track *t);

struct fav_track {
	xxffmpeg_dec dec;
	queue *vq, *aq;
	xxsdl v;
	audio a;
	struct avsync sync;
	int error;

	uint vzoom;
	uint quit;
	uint paused :1;
	uint seek_complete :1;

	struct fav_track_conf conf;

	fav_track(){}
	~fav_track() {
		if (vq)
			vq->free();
		if (aq)
			aq->free();
	}

	int init() {
		vzoom = 100;
		if (!dec.open(conf.input.url))
			return 1;
		if (!dec.hwaccel_enable(conf.decoder.hw_accel))
			warnlog("HW decoding is inactive");

		if (!conf.no_display) {
			if (!v.open())
				return 1;
			if (!v.config(dec.video_width(), dec.video_height(), conf.input.url))
				return 1;
			v.show();
		}

		if (!conf.no_sound) {
			conf.no_sound = !a.open(dec);
			sync.audio(a.buf_len_msec);
		}

		vq = queue_alloc(64);
		aq = queue_alloc(64);

		if (conf.input.seek_msec) {
			seek(conf.input.seek_msec);
		}
		return 0;
	}

	void seek(uint64_t pos_msec) {
		dbglog("seek:%U", pos_msec);
		a.clear();
		sync.reset();
		vq->reset();
		aq->reset();
		dec.seek(pos_msec * 1000);
		seek_complete = 1;
	}

	uint iframe;
	void pkt_log(const xxffmpeg_packet &pkt) {
		double tb = (pkt.stream_index() == dec.video_stream) ? dec.video_time_base() : dec.audio_time_base();
		dbglog("frame #%u  stream:%u  pts:%u  ts:%u  size:%u  dur:%u"
			, iframe++, pkt.stream_index(), pkt.pts()
			, (int)(tb * pkt.pts() * 1000000)
			, pkt.size(), pkt.duration());
	}

	uint input_full, have_pkt;
	xxffmpeg_packet pkt;
	int run() {
		int r, n;
		qframe *f;

		if (!vq && init())
			goto err;

		if (this->seek_complete) {
			this->seek_complete = 0;
			have_pkt = 0;
			input_full = 0;
		}

		if (this->paused)
			return 0x7fffffff;

		for (;;) {

			r = sync.read(&n);
			dbglog("r:%u  VQ:%u  AQ:%u", r, vq->length(), aq->length());
			if (!r) {
				// no action needed
				if (input_full)
					return n;
				break;
			}

			int want_input = 0;

			if ((r & 1) && (f = vq->read())) {

				if (input_full & 1) {
					input_full &= ~1;
				}

				if (!conf.no_display) {
					if (!v.display(f->frame.frame)) {
						errlog("display: %s", v.error());
						goto err;
					}
				}
				sync.set(f->ts, f->dur, 0);
				f->unref();

			} else if (r & 1) {
				want_input |= 1;
			}

			if ((r & 2) && (f = aq->peek())) {

				int complete = conf.no_sound;
				if (!conf.no_sound) {
					if (!(r = a.write(f->frame.frame))) {
						complete = 1;
					} else if (r == 1) {
						sync.start();
					}
				}

				if (complete) {
					sync.set(f->ts, f->dur, 1);
					f = aq->read();
					f->unref();

					if (input_full & 2) {
						input_full &= ~2;
					}
				}

			} else if (r & 2) {
				want_input |= 2;
			}

			if (want_input) {
				dbglog("VQ or AQ is empty");
				if ((want_input & 1) && (input_full & 2)) {
					warnlog("AQ is too small");
					have_pkt = 0; // TODO
				}
				if ((want_input & 2) && (input_full & 1)) {
					warnlog("VQ is too small");
					have_pkt = 0; // TODO
				}
				break;
			}
		}

		// Process next packet

		if (!have_pkt) {
			if (!dec.read(&pkt)) {
				errlog("input read: %s", dec.error());
				goto err;
			}
			this->pkt_log(pkt);
		}
		have_pkt = 0;

		if (pkt.stream_index() == dec.video_stream) {
			f = vq->push();
			if (!f) {
				dbglog("video queue full");
				input_full = 1;
				have_pkt = 1;
			} else {
				if ((r = dec.video_decode(pkt, &f->frame)) < 0) {
					errlog("Video packet decode: %s", dec.error());
					vq->pop();
				} else {
					f->ts = dec.video_time_base() * pkt.pts() * 1000000;
					f->dur = dec.video_time_base() * pkt.duration() * 1000000;
				}
			}

		} else if (pkt.stream_index() == dec.audio_stream) {
			f = aq->push();
			if (!f) {
				dbglog("audio queue full");
				input_full = 2;
				have_pkt = 1;
			} else {
				if ((r = dec.audio_decode(pkt, &f->frame)) < 0) {
					errlog("Audio packet decode: %s", dec.error());
					aq->pop();
				} else {
					f->ts = dec.audio_time_base() * pkt.pts() * 1000000;
					f->dur = dec.audio_time_base() * pkt.duration() * 1000000;
				}
			}
		}

		return 0;

	err:
		this->error = 1;
		core->stop();
		return -1;
	}
};

FF_EXTERN int track_run(fav_track *t) {
	return t->run();
}

#include <new>

static fav_track* track_create(struct fav_track_conf *conf) {
	fav_track *t = new(ffmem_new(fav_track)) fav_track;
	t->conf = *conf;
	return t;
}

static void track_stop(fav_track *t) {
}

static int track_cmd(fav_track *t, uint cmd, ...) {
	va_list va;
	va_start(va, cmd);
	uint flags = va_arg(va, uint);
	va_end(va);

	switch (cmd) {
	case FAV_TRACK_STATUS:
		return t->error;

	case FAV_TRACK_PAUSE_TOGGLE:
		t->paused = !t->paused;
		t->a.pause(t->paused);
		if (!t->paused)
			t->sync.reset();
		break;

	case FAV_TRACK_FULLSCREEN_TOGGLE: {
		uint w = t->dec.video_width();
		uint h = t->dec.video_height();
		t->v.fullscreen(!t->v.fullscreen());
		if (!t->v.fullscreen()) {
			w = t->dec.video_width() * t->vzoom / 100;
			h = t->dec.video_height() * t->vzoom / 100;
		}
		t->v.texture_rect(0, 0, w, h);
		break;
	}

	case FAV_TRACK_ZOOM: {
		if (t->v.fullscreen())
			break;
		if (flags)
			t->vzoom = ffmin(t->vzoom + 25, 400);
		else
			t->vzoom = ffmax((int)t->vzoom - 25, 25);
		uint w = t->dec.video_width() * t->vzoom / 100;
		uint h = t->dec.video_height() * t->vzoom / 100;
		t->v.window_size(w, h);
		t->v.texture_rect(0, 0, w, h);
		break;
	}

	default:
		return -1;
	}

	return 0;
}

static void track_seek_by(fav_track *t, int64_t msec) {
	t->seek(t->sync.pos()/1000 + msec);
}

FF_EXTERN const fav_track_if tif = {
	track_create,
	track_stop,
	track_cmd,
	track_seek_by,
};
