# favia: Core

LIBS += libfavia.so
LIBS_3PT += $(VLIB3_DIR)/libffmpeg.$(SO) \
	$(VLIB3_DIR)/libSDL3.$(SO)

LFAV_CFLAGS := $(CFLAGS) -I$(SDL_DIR) -I$(FFMPEG_DIR)

%.o: $(FAVIA_DIR)/src/core/%.c
	$(C) $(LFAV_CFLAGS) $< -o $@

ifdef FAV_VER_STR
core.o: LFAV_CFLAGS += -DFAV_VER_STR=\"$(FAV_VER_STR)\"
endif

%.o: $(FAVIA_DIR)/src/util/%.c
	$(C) $(LFAV_CFLAGS) $< -o $@

OBJS += core.o queue.o track.o \
	ffmpeg.o

VLIB3_SRC := $(FAVIA_DIR)/vlib3
VLIB3_FLAGS := FAV_VLIB3=$(VLIB3_SRC) OS=$(OS) COMPILER=$(COMPILER) CPU=$(CPU)
depend:
	mkdir -p $(VLIB3_DIR)
	$(MAKE)  -C $(VLIB3_DIR)  -f $(VLIB3_SRC)/ffmpeg/Makefile  -I $(VLIB3_SRC)  $(VLIB3_FLAGS)
	$(MAKE)  -C $(VLIB3_DIR)  -f $(VLIB3_SRC)/SDL/Makefile  -I $(VLIB3_SRC)  $(VLIB3_FLAGS)

LIBFAVIA_LINKFLAGS += $(LINKFLAGS) \
	-L$(VLIB3_DIR) -lffmpeg
ifneq "$(OS)" "linux"
	LIBFAVIA_LINKFLAGS += -lws2_32 -lole32 -static-libgcc
endif

libfavia.$(SO): $(OBJS) $(LIBS) | $(LIBS_3PT)
	$(LINK) -shared $+ $(LIBFAVIA_LINKFLAGS) $(LINK_RPATH_ORIGIN) -o $@
