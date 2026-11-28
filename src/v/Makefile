# favia: Video modules

LIBS += vo.so

VO_CFLAGS := $(CFLAGS) -I$(SDL_DIR) -I$(FFMPEG_DIR)

%.o: $(FAVIA_DIR)/src/v/%.c
	$(C) $(VO_CFLAGS) $< -o $@

%.o: $(FAVIA_DIR)/src/%.c
	$(C) $(LFAV_CFLAGS) $< -o $@

vo.$(SO): vo.o ui.o | $(LIBS_3PT)
	$(LINK) -shared $+ $(LINKFLAGS) $(LINK_RPATH_ORIGIN) -L$(VLIB3_DIR) -lSDL3 -o $@
