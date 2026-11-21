# favia: Audio modules

A_CFLAGS := -I$(FFAUDIO_DIR)
OBJS += ao.o
ifeq "$(OS)" "linux"
	OBJS += ffaudio-pulse.o
	LINKFLAGS += -lpulse
else
	OBJS += ffaudio-wasapi.o
endif

%.o: $(FAVIA_DIR)/src/a/%.c
	$(C) $(CFLAGS) $(A_CFLAGS) $< -o $@

ffaudio-%.o: $(FFAUDIO_DIR)/ffaudio/%.c
	$(C) $(CFLAGS) $(A_CFLAGS) $< -o $@
