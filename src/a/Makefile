# favia: Audio modules

CFLAGS += -I$(FFAUDIO_DIR)
CXXFLAGS += -I$(FFAUDIO_DIR)
ifeq "$(OS)" "linux"
	OBJS += ffaudio-pulse.o
	LINKXXFLAGS += -lpulse
else
	OBJS += ffaudio-wasapi.o
endif

ffaudio-%.o: $(FFAUDIO_DIR)/ffaudio/%.c $(FFAUDIO_DIR)/ffaudio/audio.h
	$(C) $(CFLAGS) $< -o $@
