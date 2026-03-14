# favia build

ROOT_DIR := ..
FAVIA_DIR := $(ROOT_DIR)/favia
FFBASE_DIR := $(ROOT_DIR)/ffbase
FFSYS_DIR := $(ROOT_DIR)/ffsys
FFAUDIO_DIR := $(ROOT_DIR)/ffaudio
APP_DIR := favia-0

include $(FFBASE_DIR)/conf.mk

VLIB3_DIR := $(FAVIA_DIR)/vlib3/_$(OS)-amd64
SDL_DIR := $(VLIB3_DIR)/SDL3-3.2.28/include
FFMPEG_DIR := $(VLIB3_DIR)/FFmpeg-n7.1.3
EXE := favia$(DOTEXE)

CFLAGS := -g \
	-Wno-narrowing \
	-MMD -MP \
	-DFFBASE_MEM_ASSERT \
	-I$(FAVIA_DIR)/src -I$(FFBASE_DIR) -I$(FFSYS_DIR) -I$(SDL_DIR) -I$(FFMPEG_DIR)

ifeq "$(DEBUG)" "1"
	CFLAGS += -DFF_DEBUG -O0 -Werror
else
	CFLAGS += -O3 -fno-strict-aliasing
endif

ifeq "$(ASAN)" "1"
	CFLAGS += -fsanitize=address
	LINKXXFLAGS += -fsanitize=address
endif

CXXFLAGS := $(CFLAGS) \
	-fno-exceptions -fno-rtti
LINKXXFLAGS += -L$(VLIB3_DIR) \
	-lffmpeg -lSDL3

ifeq "$(OS)" "linux"
else
	LINKXXFLAGS += -lole32 -static-libgcc -static-libstdc++
endif

default: build
ifneq "$(DEBUG)" "1"
	$(SUBMAKE) strip-debug
endif
	$(SUBMAKE) app

-include $(wildcard *.d)

%.o: $(FAVIA_DIR)/src/core/%.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

%.o: $(FAVIA_DIR)/src/core/%.c
	$(C) $(CFLAGS) $< -o $@
ifdef FAV_VERSION_STR
core.o: CFLAGS += -DFAV_VERSION_STR=\"$(FAV_VERSION_STR)\"
endif

%.o: $(FAVIA_DIR)/src/exe/%.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

%.o: $(FAVIA_DIR)/src/util/%.c
	$(C) $(CFLAGS) $< -o $@

OBJS := exe.o core.o queue.o track.o \
	ffmpeg.o

include $(FAVIA_DIR)/src/a/Makefile

$(EXE): $(OBJS)
	$(LINKXX) $+ $(LINKXXFLAGS) $(LINK_RPATH_ORIGIN) -o $@

ifeq "$(TARGETS)" ""
override TARGETS := $(EXE)
endif
build: $(TARGETS)

strip-debug: $(addsuffix .debug,$(TARGETS))
%.debug: %
	$(OBJCOPY) --only-keep-debug $< $@
	$(STRIP) $<
	$(OBJCOPY) --add-gnu-debuglink=$@ $<
	touch $@

app:
	mkdir -p $(APP_DIR)
	cp -a \
		$(EXE) \
		$(VLIB3_DIR)/libffmpeg.$(SO) \
		$(VLIB3_DIR)/libSDL3.$(SO) \
		$(FAVIA_DIR)/README.md \
		$(APP_DIR)/

ifeq "$(OS)" "windows"
	mv $(APP_DIR)/README.md $(APP_DIR)/README.txt
	unix2dos $(APP_DIR)/README.txt
endif

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
