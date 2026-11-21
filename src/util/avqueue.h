/** favia: AV frame queue
2025, Simon Zolin */

#pragma once
#include <util/ffmpeg.h>

struct qframe {
	ffmpeg_frame f;
	uint64_t ts;
	uint dur;
};

struct avqueue {
	uint r, w, cap, mask;
	struct qframe data[0];
};

static inline void avqueue_free(struct avqueue *q)
{
	for (uint i = 0;  i < q->cap;  i++) {
		ffmpeg_frame_destroy(&q->data[i].f);
	}
	ffmem_alignfree(q);
}

static inline uint avqueue_length(const struct avqueue *q)
{
	return q->w - q->r;
}

static inline struct qframe* avqueue_push(struct avqueue *q)
{
	uint used = q->w - q->r;
	if (used == q->cap)
		return NULL;
	uint i = q->w++ & q->mask;
	return q->data + i;
}

static inline void avqueue_pop(struct avqueue *q)
{
	q->w--;
	assert(q->w >= q->r);
}

static inline struct qframe* avqueue_read(struct avqueue *q)
{
	uint used = q->w - q->r;
	if (used == 0)
		return NULL;
	uint i = q->r++ & q->mask;
	return q->data + i;
}

static inline struct qframe* avqueue_peek(struct avqueue *q)
{
	uint used = q->w - q->r;
	if (used == 0)
		return NULL;
	uint i = q->r & q->mask;
	return q->data + i;
}

static inline void avqueue_reset(struct avqueue *q)
{
	struct qframe *f;
	while ((f = avqueue_read(q))) {
		ffmpeg_frame_unref(&f->f);
	}
	q->w = q->r = 0;
}

static inline struct avqueue* avqueue_alloc(uint n)
{
	uint nn = sizeof(struct avqueue) + n * sizeof(struct qframe);
	struct avqueue *q = (struct avqueue*)ffmem_align(nn, 64);
	ffmem_zero(q, nn);
	q->r = q->w = 0;
	q->cap = n;
	q->mask = n - 1;
	for (uint i = 0;  i < n;  i++) {
		ffmpeg_frame_init(&q->data[i].f);
	}
	return q;
}

static inline struct avqueue* avqueue_realloc(struct avqueue *q, uint n)
{
	uint len = q->w - q->r, n_right, i;
	if (n < len)
		return NULL;

	uint nn = sizeof(struct avqueue) + n * sizeof(struct qframe);
	struct avqueue *nq = (struct avqueue*)ffmem_align(nn, 64);
	ffmem_zero(nq, nn);
	nq->r = nq->w = 0;
	nq->cap = n;
	nq->mask = n - 1;
	if (!nq)
		return NULL;

	i = q->r & q->mask;
	n_right = ffmin(len, q->cap - i);
	ffmem_copy(nq->data, q->data + i, n_right * sizeof(struct qframe));
	ffmem_copy(nq->data + n_right, q->data, (len - n_right) * sizeof(struct qframe));
	nq->w = len;

	for (uint i = len;  i < n;  i++) {
		ffmpeg_frame_init(&nq->data[i].f);
	}

	ffmem_alignfree(q);
	return nq;
}
