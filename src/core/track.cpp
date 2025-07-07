/** favia
2025, Simon Zolin */

#include <favia.h>

FF_EXTERN const fav_core_if *core;

#include <util/ffmpeg.h>
#include <util/SDL.hpp>
#include <audio.hpp>
#include <util/util.hpp>
#include <util/unix-shell.h>
#include <core/avsync.hpp>
#include <ffsys/file.h>
#include <ffsys/std.h>

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


#undef syserrlog
#undef errlog
#undef warnlog
#undef infolog
#undef dbglog
#define syserrlog(trk, ...)  core->log(FAV_LOG_ERROR, trk->id, __VA_ARGS__) // TODO
#define errlog(trk, ...)  core->log(FAV_LOG_ERROR, trk->id, __VA_ARGS__)
#define warnlog(trk, ...)  core->log(FAV_LOG_WARN, trk->id, __VA_ARGS__)
#define infolog(trk, ...)  core->log(FAV_LOG_INFO, trk->id, __VA_ARGS__)
#define dbglog(trk, ...) \
do { \
	if (ff_unlikely(core->conf.debug)) \
		core->log(FAV_LOG_DEBUG, trk->id, __VA_ARGS__); \
} while (0)


#define SEEK_STEP_SEC  5
#define SEEK_LEAP_SEC  60
#define SEEK_LEAP_PCT  5
#define AVQ_SIZE_MAX  128

struct tracks {
	xxvec tracks; // fav_track*[]
	uint gid;
};
static struct tracks *xt;

FF_EXTERN void tracks_init() {
	xt = ffmem_new(struct tracks);
}

static int sdl_init_complete;

static const char* time_print(uint64_t msec, char *buf, size_t cap) {
	uint h = msec / 3600000,  m = (msec / 60000) % 60,  s = (msec / 1000) % 60,  ms = msec % 1000;
	ffsz_format(buf, cap, "%u:%02u:%02u.%03u"
		, h, m, s, ms);
	return buf;
}

struct fav_track {
	struct fav_track_conf conf;

	xxfile input;
	xxffmpeg_dec dec;
	queue *vq, *aq;
	xxsdl v;
	audio a;
	struct avsync sync;
	xxffmpeg_packet pkt;
	uint input_full, have_pkt, finished;
	int error;
	char id[8];

	uint64_t prev_pos_sec, loop_start, loop_end;
	uint avolume;
	uint vzoom;
	uint paused;
	uint amute :1;
	uint redraw :1;

	fav_track(){}
	~fav_track() {
		if (vq)
			vq->free();
		if (aq)
			aq->free();

		if (conf.url_transient)
			ffmem_free((char*)conf.input.url);
	}

	static int input_read(void *opaque, uint8_t *buf, int size)
	{
		fav_track *t = (fav_track*)opaque;
		int r = t->input.read(buf, size);
		if (r == 0)
			return AVERROR_EOF;
		else if (r < 0)
			return AVERROR(errno);
		return r;
	}

	static int seek_method(int w) {
		switch (w) {
		case SEEK_CUR: return FFFILE_SEEK_CURRENT;
		case SEEK_END: return FFFILE_SEEK_END;
		}
		return FFFILE_SEEK_BEGIN;
	}
	static int64_t input_seek(void *opaque, int64_t pos, int whence)
	{
		fav_track *t = (fav_track*)opaque;

		if (whence == AVSEEK_SIZE) {
			return t->input.info().size();
		}

		int64_t r = t->input.seek(pos, seek_method(whence));
		if (r < 0)
			return AVERROR(errno);
		return r;
	}

	int init() {
		prev_pos_sec = loop_start = loop_end = ~0ULL;
		avolume = 100;
		vzoom = 100;

		if (input.open(conf.input.url, FFFILE_READONLY).null()) {
			syserrlog(this, "Input open: %s", conf.input.url);
			return 1;
		}

		if (!dec.open(input_read, input_seek, this)) {
			errlog(this, "Decoder open: %s", dec.error());
			return 1;
		}
		assert(dec.have_video());

		char buf[64];
		infolog(this, "\"%s\"  %.02FMB  %s  %u streams"
			, conf.input.url
			, (double)xxfile::info(conf.input.url).size() / (1024 * 1024)
			, time_print(dec.duration(), buf, sizeof(buf))
			, dec.streams());

		if (!dec.hwaccel_enable(conf.decoder.hw_accel))
			warnlog(this, "HW decoding is inactive");

		sync.reset();

		if (!conf.no_display) {
			if (!sdl_init_complete) {
				sdl_init_complete = 1;
				if (sdl_init(1)) {
					errlog(this, "SDL init");
					return 1;
				}
			}

			if (!v.open(dec.video_width(), dec.video_height(), xxpath(conf.input.url).name().ptr)) {
				errlog(this, "Video renderer: %s", v.error());
				return 1;
			}

			if (conf.video.zoom)
				this->zoom(conf.video.zoom);

			v.show();
		}

		conf.no_sound = !dec.have_audio();
		if (!conf.no_sound) {
			conf.no_sound = !a.open(dec);
			sync.audio((a.buf_len_msec) ? a.buf_len_msec : 500);
			if (conf.audio.volume)
				this->volume(conf.audio.volume);
			if (conf.audio.mute)
				this->mute_toggle();
		}

		vq = queue_alloc(16);
		aq = queue_alloc(16);

		if (conf.input.seek_msec)
			this->seek(conf.input.seek_msec);
		return 0;
	}

	void pause_toggle() {
		this->paused = !this->paused;
		this->a.pause(this->paused);
		if (!this->paused)
			this->sync.reset();
	}

	void seek(uint64_t pos_msec) {
		char buf[64];
		infolog(this, "Seek: %s", time_print(pos_msec, buf, sizeof(buf)));
		if (!conf.no_sound)
			a.clear();
		sync.reset();
		vq->reset();
		aq->reset();
		dec.seek(pos_msec * 1000);
		this->have_pkt = 0;
		this->input_full = 0;
		this->finished = 0;
	}

	void zoom(uint n) {
		vzoom = n;
		infolog(this, "Zoom: %u%%", vzoom);
		uint w = dec.video_width() * vzoom / 100;
		uint h = dec.video_height() * vzoom / 100;
		v.window_size(w, h);
		v.texture_rect(0, 0, w, h);
	}

	void fullscreen_toggle() {
		uint w = this->dec.video_width(), h = this->dec.video_height();
		uint fs = this->v.fullscreen();
		this->v.fullscreen(!fs);
		if (fs) {
			w = w * this->vzoom / 100;
			h = h * this->vzoom / 100;
		}
		this->v.texture_rect(0, 0, w, h);
		this->redraw = 1;
	}

	void mute_toggle() {
		amute = !amute;
		infolog(this, "Mute: %u", amute);
		a.volume((amute) ? 0 : avolume);
	}

	void volume(uint n) {
		avolume = n;
		infolog(this, "Volume: %u%%", avolume);
		a.volume(avolume);
	}

	void audio_stream_switch() {
		int r;
		if ((r = dec.audio_stream_switch())) {
			if (r < 0)
				warnlog(this, "Switching audio streams: %s", dec.error());
			return;
		}

		infolog(this, "Switched to next audio stream");

		aq->reset();
		vq->reset();
		input_full = 0;
		have_pkt = 0;

		if (conf.no_sound)
			return;

		a.~audio();
		ffmem_zero_obj(&a);
		new (&a) audio();
		conf.no_sound = !a.open(dec);
		a.volume(avolume);

		sync.reset();
		sync.master = 0;
		if (!conf.no_sound)
			sync.audio((a.buf_len_msec) ? a.buf_len_msec : 500);
	}

	uint iframe;
	void pkt_log(const xxffmpeg_packet &pkt) {
		double tb = (pkt.stream_index() == dec.video_stream) ? dec.video_time_base() : dec.audio_time_base();
		dbglog(this, "frame #%u  stream:%u  pts:%u  ts:%u  size:%u  dur:%u"
			, this->iframe++, pkt.stream_index(), pkt.pts()
			, (int)(tb * pkt.pts() * 1000000)
			, pkt.size(), pkt.duration());
	}

	void pos_print(uint64_t pos_sec) {
		if (pos_sec != this->prev_pos_sec) {
			this->prev_pos_sec = pos_sec;
			char buf1[64], buf2[64];
			ffstdout_fmt("\r[%s / %s]"
				, time_print(pos_sec * 1000, buf1, sizeof(buf1))
				, time_print(dec.duration(), buf2, sizeof(buf2)));
		}
	}

	bool until(uint64_t pos_msec) {
		if (pos_msec >= this->conf.input.until_msec) {
			dbglog(this, "'until' time reached");
			return 1;
		}
		return 0;
	}

	int run() {
		int r, n;
		qframe *f;

		if (!vq && init()) {
			return done(FAV_TRACK_E_INIT);
		}

		if (this->paused && !this->redraw)
			return 0x7fffffff;

		for (;;) {

			r = 0;
			if (!this->paused) {
				r = sync.read(&n);
				dbglog(this, "r:%u  VQ:%u  AQ:%u", r, vq->length(), aq->length());
			}

			if (this->redraw) {
				this->redraw = 0;
				r |= 1;
			}

			if (!r) {
				// no action needed
				if (input_full || finished)
					return n;
				break;
			}

			int want_input = 0;

			if ((r & 1) && (f = vq->peek())) {

				int rm = !this->paused;
				if (!conf.no_display) {
					if (!v.display(f->frame.frame)) {
						errlog(this, "display: %s", v.error());
						rm = 1;
					}
				}

				sync.frame(f->ts, f->dur, 0);

				if (rm) {
					f->unref();
					vq->read();
					if (input_full & 1) {
						input_full &= ~1;
					}
				}

				if (until(f->ts / 1000))
					return done(0);

				pos_print(f->ts / 1000000);

			} else if (r & 1) {
				want_input |= 1;
			}

			if ((r & 2) && (f = aq->peek())) {

				int complete = conf.no_sound;
				if (!conf.no_sound) {
					if (!(r = a.write(f->frame.frame))) {
						complete = 1;
					} else if (r == 1) {
						sync.a_start();
					}
				}

				if (complete) {
					sync.frame(f->ts, f->dur, 1);
					f = aq->read();
					f->unref();

					if (input_full & 2) {
						input_full &= ~2;
					}

					if (until(f->ts / 1000)) // TODO
						return done(0);
				}

			} else if (r & 2) {
				want_input |= 2;
			}

			if (want_input) {
				dbglog(this, "VQ or AQ is empty");

				if ((finished & 3) == 3) {
					dbglog(this, "finished");

					if (this->conf.pause_on_end) {
						this->pause_toggle();
						return 0;
					}

					return done(0);
				}

				if ((want_input & 1) && (input_full & 2)) {
					if (aq->cap < AVQ_SIZE_MAX) {
						aq = queue_realloc(aq, ffmin(aq->cap * 2, AVQ_SIZE_MAX));
						input_full &= ~2;
					} else {
						warnlog(this, "need very large AQ");
						have_pkt = 0;
					}
				}

				if ((want_input & 2) && (input_full & 1)) {
					if (vq->cap < AVQ_SIZE_MAX) {
						vq = queue_realloc(vq, ffmin(vq->cap * 2, AVQ_SIZE_MAX));
						input_full &= ~1;
					} else {
						warnlog(this, "need very large VQ");
						have_pkt = 0;
					}
				}

				break;
			}
		}

		// Process next packet

		if (finished)
			return 0;

		if (!have_pkt) {
			if ((r = dec.read(&pkt))) {
				if (r < 0) {
					errlog(this, "input read: %s", dec.error());
					return done(FAV_TRACK_E_IO);
				}
				finished = 3;
				return 0;
			}
			this->pkt_log(pkt);
		}
		have_pkt = 0;

		for (;;) {
			if (pkt.stream_index() == dec.video_stream) {
				if (!(f = vq->push())) {
					dbglog(this, "video queue full");
					input_full = 1;
					have_pkt = 1;
					return 0;
				}

				if ((r = dec.video_decode(pkt, &f->frame))) {
					vq->pop();
					if (r > 0)
						break; // this packet is completely processed
					errlog(this, "Video packet decode: %s", dec.error());
					return 0;
				}

				f->ts = dec.video_time_base() * pkt.pts() * 1000000;
				f->dur = dec.video_time_base() * pkt.duration() * 1000000;

			} else if (pkt.stream_index() == dec.audio_stream) {
				if (!(f = aq->push())) {
					dbglog(this, "audio queue full");
					input_full = 2;
					have_pkt = 1;
					return 0;
				}

				if ((r = dec.audio_decode(pkt, &f->frame))) {
					aq->pop();
					if (r > 0)
						break; // this packet is completely processed
					errlog(this, "Audio packet decode: %s", dec.error());
					return 0;
				}

				f->ts = dec.audio_time_base() * pkt.pts() * 1000000;
				f->dur = dec.audio_time_base() * pkt.duration() * 1000000;

			} else {
				break;
			}
		}

		return 0;
	}

	int done(int e) {
		this->error = e;
		core->conf.signal(this, FAV_TRACK_STOP, 1);
		return -1;
	}
};

static fav_track* track_create(struct fav_track_conf *conf) {
	fav_track *t = new(ffmem_new(fav_track)) fav_track;
	t->conf = *conf;
	xt->gid++;
	ffsz_format(t->id, sizeof(t->id), "*%u", xt->gid);
	*xt->tracks.push<fav_track*>() = t;
	return t;
}

static void track_close(fav_track *t) {
	fav_track **it;
	FFSLICE_WALK(&xt->tracks, it) {
		if (t == *it) {
			xt->tracks.rm_swap<fav_track*>(it, 1);
			break;
		}
	}

	t->~fav_track();
	ffmem_free(t);
}

static fav_track* tracks_next(fav_track *t) {
	if (xt->tracks.len <= 1)
		return NULL;

	fav_track **it;
	FFSLICE_WALK(&xt->tracks, it) {
		if (*it == t) {
			uint i = it - (fav_track**)xt->tracks.ptr;
			i++;
			if (i == xt->tracks.len)
				i = 0;
			return *xt->tracks.at<fav_track*>(i);
		}
	}
	return NULL;
}

static int track_cmd(fav_track *t, uint cmd, ...) {
	int r;
	va_list va;
	va_start(va, cmd);

	if (cmd == FAV_TRACK_ADD) {
		char *fn = va_arg(va, char*);
		va_end(va);
		fav_track_conf tc = t->conf;
		tc.input.url = fn;
		tc.url_transient = 1;
		track_create(&tc);
		return 0;
	}

	uint flags = va_arg(va, uint);
	uint arg2 = va_arg(va, uint);
	va_end(va);

	switch (cmd) {

	case FAV_TRACK_NEXT:
	case FAV_TRACK_STOP:
	case FAV_TRACK_QUIT:
	case FAV_TRACK_WINDOW:
		if (cmd == FAV_TRACK_WINDOW && (flags & 8)) {
			t->redraw = 1;
			break;

		} else if (cmd == FAV_TRACK_WINDOW && (flags & 4)) {
			uint rw = arg2 & 0xffff, rh = arg2 >> 16;
			uint w = t->dec.video_width(), h = t->dec.video_height();
			// rw / rh := w / h
			if (w >= h)
				rh = (double)rw / ((double)w / h);
			else
				rw = (double)rh * ((double)w / h);
			t->v.texture_rect(0, 0, rw, rh);
			break;

		} else if (cmd == FAV_TRACK_WINDOW && (flags & 2)) {
			t = tracks_next(t);
			if (t)
				t->v.present();
			break;
		}

		core->conf.signal(t, cmd, flags);
		break;

	case FAV_TRACK_SOURCE:
		if (!ffui_glib_trash(t->conf.input.url, NULL))
			infolog(t, "Moved to Trash: %s", t->conf.input.url);
		break;

	case FAV_TRACK_STATUS:
		return t->error;

	case FAV_TRACK_PAUSE:
		t->pause_toggle();
		break;

	case FAV_TRACK_FULLSCREEN:
		t->fullscreen_toggle();
		break;

	case FAV_TRACK_ZOOM:
		if (t->v.fullscreen())
			break;

		if (flags)
			r = ffmin(t->vzoom + 10, 400);
		else
			r = ffmax((int)t->vzoom - 10, 10);
		t->zoom(r);
		break;

	case FAV_TRACK_VOLUME:
		if (flags & 2) {
			t->mute_toggle();
			break;
		}

		if (flags & 1)
			r = ffmin(t->avolume + 5, 125);
		else
			r = ffmax((int)t->avolume - 5, 0);
		t->volume(r);
		break;

	case FAV_TRACK_AUDIO_NEXT:
		t->audio_stream_switch();  break;

	case FAV_TRACK_SEEK:
		if (flags & (0x10|0x20)) {
			if (flags & 0x10) {
				t->loop_start = ((t->sync.pos() - 500000) / 1000000) * 1000;
			} else {
				t->loop_end = ((t->sync.pos() + 1000000) / 1000000) * 1000;
				char buf1[64], buf2[64];
				infolog(t, "range: \"%s\"  %s %s"
					, t->conf.input.url
					, time_print(t->loop_start, buf1, sizeof(buf1))
					, time_print(t->loop_end, buf2, sizeof(buf2)));
			}
			break;
		}
		if (flags & 4)
			r = t->dec.duration()/1000 * SEEK_LEAP_PCT / 100;
		else
			r = !(flags & 1) ? SEEK_STEP_SEC : SEEK_LEAP_SEC;
		if (flags & 2)
			r = -r;
		t->seek(t->sync.pos()/1000 + r * 1000);
		break;

	default:
		assert(0);
		return -1;
	}

	return 0;
}

static fav_track* track_find(const void *sdl_wnd) {
	fav_track **it;
	FFSLICE_WALK(&xt->tracks, it) {
		if (sdl_wnd == (*it)->v.window)
			return *it;
	}
	return NULL;
}

FF_EXTERN int tracks_run() {
	int n = 0x7fffffff;
	fav_track **it;
	FFSLICE_WALK(&xt->tracks, it) {
		int r = (*it)->run();
		if (r < 0)
			return r;
		n = ffmin(r, n);
	}
	return n;
}

FF_EXTERN const fav_track_if tif = {
	track_create,
	track_close,
	track_cmd,
	track_find,
};
