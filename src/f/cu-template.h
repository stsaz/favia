
struct template {
	
};

static void cu_template_close(fav_track *t)
{
	struct template *x = t->template;
	fav_track_free(t, x);
}

static int cu_template_open(fav_track *t)
{
	struct template *x = (struct template*)fav_track_alloc(t, sizeof(struct template));
	t->xxx = x;
	return FAV_CU_FWD;
}

static int cu_template_process(fav_track *t)
{
	return FAV_CU_FWD;
}

FF_EXTERN const struct fav_track_cu trk_cu_template = { "template", cu_template_open, cu_template_close, cu_template_process };
