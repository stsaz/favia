/** favia: AV synchronization
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
};

static inline uint64_t avsync_now()
{
	fftime t = fftime_monotonic();
	return fftime_to_usec(&t);
}

static inline void avsync_reset(struct avsync *s)
{
	ffmem_zero(s, FF_OFF(struct avsync, master));
	s->a_sig_next = ~0ULL;
}

// usec
static inline uint64_t avsync_pos(const struct avsync *s)
{
	if (s->master == 1)
		return ffmax((int64_t)(s->ts_next[1] - s->a_buf_usec), 0);
	return s->ts_cur[0];
}

static inline void avsync_audio(struct avsync *s, uint buf_len_msec)
{
	s->a_buf_usec = buf_len_msec * 1000;
	s->master = 1;
}

static inline void avsync_a_start(struct avsync *s)
{
	if (!s->a_active)
		fav_dbglog("audio started");
	uint64_t rt_now = avsync_now();
	s->rt_last[0] = s->rt_last[1] = rt_now;
	s->a_sig_next = rt_now + s->a_buf_usec/4; // update audio 4 times per buffer
	s->a_active = 1;
}

// ts, dur: usec
static inline uint64_t avsync_frame(struct avsync *s, uint64_t ts, uint dur, uint flags) {
	int i = !!(flags & 1);

	if (ts < s->ts_next[i]) {
		fav_dbglog("fix non-monotonic PTS: %U -> %U", ts, s->ts_next[i]);
		ts = s->ts_next[i];
	}

	s->ts_cur[i] = ts;
	s->ts_next[i] = ts + dur;

	s->rt_last[i] = avsync_now();
	return ts;
}

/**
timeout_usec: time to wait when there are no events
Return bitmask of the streams that need action */
static inline int avsync_read(struct avsync *s, int *timeout_usec)
{
	if (s->master == 1 && !s->a_active)
		return 2; // keep filling audio buffer until it's full

	uint64_t rt_now = avsync_now();

	uint64_t rtj = rt_now - s->rt_last[s->master] + avsync_pos(s);
	fav_dbglog("rtj:%D[%D,%D]  vts:%U-%U  ats:%U-%U"
		, rtj, rtj - s->ts_cur[0], rtj - s->ts_cur[1]
		, s->ts_cur[0], s->ts_next[0]
		, s->ts_cur[1], s->ts_next[1]);

	uint suspend[2] = { ~0U, ~0U };
	int r = 0;

	if (rtj >= s->ts_next[0]) {
		r |= 1;
	} else {
		suspend[0] = s->ts_next[0] - rtj;
	}

	if (rt_now >= s->a_sig_next) {
		r |= 2;
	} else {
		suspend[1] = s->a_sig_next - rt_now;
	}

	if (!r) {
		*timeout_usec = ffmin(suspend[0], suspend[1]);
	}

	return r;
}
