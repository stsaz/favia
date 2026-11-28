# favia: Audio modules

LIBS += ao.so

%.o: $(FAVIA_DIR)/src/a/%.c
	$(C) $(CFLAGS) -I$(FFAUDIO_DIR) -I$(SDL_DIR) -I$(FFMPEG_DIR) $< -o $@

ffaudio-%.o: $(FFAUDIO_DIR)/ffaudio/%.c
	$(C) $(CFLAGS) -I$(FFAUDIO_DIR) $< -o $@

_=
ifeq "$(OS)" "linux"
	AO_OBJS := ffaudio-pulse.o
ao.$(SO): LINKFLAGS += -lpulse
else
	AO_OBJS := ffaudio-wasapi.o
endif

ao.$(SO): ao.o $(AO_OBJS)
	$(LINK) -shared $+ $(LINKFLAGS) -lm -o $@
