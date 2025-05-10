/** favia
2025, Simon Zolin */

struct queue {
	uint r, w, cap, mask;
	qframe data[0];

	void free() {
		for (uint i = 0;  i < this->cap;  i++) {
			this->data[i].destroy();
		}
		ffmem_free(this);
	}

	uint length() { return w - r; }

	void reset() {
		qframe *f;
		while ((f = read())) {
			f->unref();
		}
		this->w = this->r = 0;
	}

	qframe* push() {
		uint used = this->w - this->r;
		if (used == this->cap)
			return NULL;
		uint i = this->w++ & this->mask;
		return this->data + i;
	}

	void pop() {
		this->w--;
		assert(this->w >= this->r);
	}

	qframe* read() {
		uint used = this->w - this->r;
		if (used == 0)
			return NULL;
		uint i = this->r++ & this->mask;
		return this->data + i;
	}

	qframe* peek() {
		uint used = this->w - this->r;
		if (used == 0)
			return NULL;
		uint i = this->r & this->mask;
		return this->data + i;
	}
};

static struct queue* queue_alloc(uint n) {
	uint nn = sizeof(struct queue) + n * sizeof(struct qframe);
	struct queue *q = (struct queue*)ffmem_alloc(nn);
	ffmem_zero(q, nn);
	q->cap = n;
	q->mask = n - 1;
	for (uint i = 0;  i < n;  i++) {
		q->data[i].alloc();
	}
	return q;
}
