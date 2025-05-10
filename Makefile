# favia build

ROOT_DIR := ..
FAVIA_DIR := $(ROOT_DIR)/favia
FFBASE_DIR := $(ROOT_DIR)/ffbase
FFSYS_DIR := $(ROOT_DIR)/ffsys
FFAUDIO_DIR := $(ROOT_DIR)/ffaudio
VLIB3_DIR := $(FAVIA_DIR)/vlib3/_linux-amd64
SDL_DIR := $(VLIB3_DIR)/SDL3-3.2.10/include
FFMPEG_DIR := $(VLIB3_DIR)/ffmpeg-7.1

include $(FFBASE_DIR)/conf.mk

CFLAGS := -g \
	-MMD -MP \
	-I$(FAVIA_DIR)/src -I$(FFBASE_DIR) -I$(FFSYS_DIR) -I$(FFAUDIO_DIR) -I$(SDL_DIR) -I$(FFMPEG_DIR)
CFLAGS += -O0

LINKFLAGS := -L$(FAVIA_DIR)/vlib3/_linux-amd64 \
	-lavutil -lavcodec -lavformat -lSDL3 -lpulse

default: build
ifneq "$(DEBUG)" "1"
	$(SUBMAKE) strip-debug
endif
	$(SUBMAKE) app

-include $(wildcard *.d)

%.o: $(FAVIA_DIR)/src/core/%.cpp
	$(CXX) $(CFLAGS) $< -o $@

%.o: $(FAVIA_DIR)/src/core/%.c
	$(C) $(CFLAGS) $< -o $@
ifdef FAV_VERSION_STR
core.o: CFLAGS += -DFAV_VERSION_STR=\"$(FAV_VERSION_STR)\"
endif

%.o: $(FAVIA_DIR)/src/exe/%.c
	$(C) $(CFLAGS) $< -o $@

%.o: $(FAVIA_DIR)/src/util/%.c
	$(C) $(CFLAGS) $< -o $@

ffaudio-%.o: $(FFAUDIO_DIR)/ffaudio/%.c $(FFAUDIO_DIR)/ffaudio/audio.h
	$(C) $(CFLAGS) $< -o $@

favia: exe.o core.o track.o \
		ffmpeg.o \
		ffaudio-pulse.o
	$(LINKXX) $+ $(LINKFLAGS) $(LINK_RPATH_ORIGIN) -o $@

ifeq "$(TARGETS)" ""
override TARGETS := favia
endif
build: $(TARGETS)

strip-debug: $(addsuffix .debug,$(TARGETS))
%.debug: %
	$(OBJCOPY) --only-keep-debug $< $@
	$(STRIP) $<
	$(OBJCOPY) --add-gnu-debuglink=$@ $<
	touch $@

app:
	mkdir -p favia-0
	cp -a \
		$(FAVIA_DIR)/README.md \
		favia \
		$(VLIB3_DIR)/libavutil.so* \
		$(VLIB3_DIR)/libavcodec.so* \
		$(VLIB3_DIR)/libavformat.so* \
		$(VLIB3_DIR)/libswscale.so* \
		$(VLIB3_DIR)/libswresample.so* \
		$(VLIB3_DIR)/libSDL3.so* \
		favia-0/

PKG_VER := test
PKG_ARCH := $(CPU)
PKG_PACKER := tar -c --owner=0 --group=0 --numeric-owner -v --zstd -f
PKG_EXT := tar.zst
ifeq "$(OS)" "windows"
	PKG_PACKER := zip -r -v
	PKG_EXT := zip
endif
PKG_NAME := favia-$(PKG_VER)-$(OS)-$(PKG_ARCH).$(PKG_EXT)
package: $(PKG_NAME)
$(PKG_NAME): favia-0
	$(PKG_PACKER) $@ $<

PKG_DEBUG_NAME := favia-$(PKG_VER)-$(OS)-$(PKG_ARCH)-debug.$(PKG_EXT)
$(PKG_DEBUG_NAME):
	$(PKG_PACKER) $@ *.debug
package-debug: $(PKG_DEBUG_NAME)

release: default
	$(SUBMAKE) package
	$(SUBMAKE) package-debug
