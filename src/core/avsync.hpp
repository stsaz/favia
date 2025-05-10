/** favia
2025, Simon Zolin */

#include <ffsys/perf.h>

struct avsync {
	uint64_t rt_last[2] // real time of the last frame
		, ts_cur[2] // AV time of the last frame
		, ts_next[2] // AV time of the next frame
		, a_sig_next // real time of the next audio update signal
		;
	uint a_active;

	uint master, a_buf_usec;

	avsync() { ffmem_zero_obj(this); }
	void reset() {
		ffmem_zero(this, FF_OFF(struct avsync, master));
	}

	uint64_t pos() const {
		if (master == 1)
			return ffmax((int64_t)(ts_next[1] - a_buf_usec), 0);
		return ts_cur[0];
	}

	void audio(uint buf_len_msec) {
		a_buf_usec = buf_len_msec * 1000;
		master = 1;
	}

	void start() {
		if (!a_active)
			dbglog("audio started");
		fftime t = fftime_monotonic();
		uint64_t rt_now = fftime_to_usec(&t);
		rt_last[0] = rt_last[1] = rt_now;
		a_active = 1;
		a_sig_next = rt_now + a_buf_usec/4; // update audio 4 times per buffer
	}

	void set(uint64_t ts, uint dur, uint flags) {
		int i = !!(flags & 1);

		if (ts < ts_next[i]) {
			dbglog("fix non-monotonic PTS: %U -> %U", ts, ts_next[i]);
			ts = ts_next[i];
		}

		ts_cur[i] = ts;
		ts_next[i] = ts + dur;

		fftime t = fftime_monotonic();
		rt_last[i] = fftime_to_usec(&t);
	}

	/**
	timeout_msec: time to wait when there are no events
	Return bitmask of the streams that need action */
	int read(int *timeout_usec) {
		if (!a_active)
			return 2; // keep filling audio buffer until it's full

		fftime t = fftime_monotonic();
		uint64_t rt_now = fftime_to_usec(&t);

		uint64_t rtj = rt_now - rt_last[master] + pos();
		dbglog("rtj:%D[%D,%D]  vts:%U-%U  ats:%U-%U"
			, rtj, rtj - ts_cur[0], rtj - ts_cur[1]
			, ts_cur[0], ts_next[0]
			, ts_cur[1], ts_next[1]);

		uint suspend[2] = { ~0U, ~0U };
		int r = 0;

		if (rtj >= ts_next[0]) {
			r |= 1;
		} else {
			suspend[0] = ts_next[0] - rtj;
		}

		if (rt_now >= a_sig_next) {
			r |= 2;
		} else {
			suspend[1] = a_sig_next - rt_now;
		}

		if (!r) {
			*timeout_usec = ffmin(suspend[0], suspend[1]);
		}

		return r;
	}
};
