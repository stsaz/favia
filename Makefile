# favia build

ROOT_DIR := ..
FAVIA_DIR := $(ROOT_DIR)/favia
FFBASE_DIR := $(ROOT_DIR)/ffbase
FFSYS_DIR := $(ROOT_DIR)/ffsys
FFAUDIO_DIR := $(ROOT_DIR)/ffaudio
APP_DIR := favia-0

include $(FFBASE_DIR)/conf.mk

CFLAGS += -std=c99 -fPIC \
	-g \
	-Wno-narrowing \
	-MMD -MP \
	-DFFBASE_MEM_ASSERT \
	-I$(FAVIA_DIR)/src -I$(FFBASE_DIR) -I$(FFSYS_DIR)

ifeq "$(DEBUG)" "1"
	CFLAGS += -DFF_DEBUG -O0 -Werror
else
	CFLAGS += -O3 -fno-strict-aliasing
endif

ifeq "$(ASAN)" "1"
	CFLAGS += -fsanitize=address
	LINKFLAGS += -fsanitize=address
endif

default: build
ifneq "$(DEBUG)" "1"
	$(SUBMAKE) strip-debug
endif
	$(SUBMAKE) app

-include $(wildcard *.d)

VLIB3_DIR := vlib3
SDL_DIR := $(VLIB3_DIR)/SDL3-3.2.28/include
FFMPEG_DIR := $(VLIB3_DIR)/FFmpeg-n7.1.3

include $(FAVIA_DIR)/src/a/Makefile
include $(FAVIA_DIR)/src/v/Makefile
include $(FAVIA_DIR)/src/core/Makefile
include $(FAVIA_DIR)/src/exe/Makefile

build: $(EXE)

strip-debug: $(addsuffix .debug,$(EXE) $(LIBS))
%.debug: %
	$(OBJCOPY) --only-keep-debug $< $@
	$(STRIP) $<
	$(OBJCOPY) --add-gnu-debuglink=$@ $<
	touch $@

app:
	mkdir -p $(APP_DIR)
	cp -a \
		$(EXE) libfavia.$(SO) $(LIBS) $(LIBS_3PT) \
		$(FAVIA_DIR)/README.md \
		$(FAVIA_DIR)/res/favia.rgba \
		$(APP_DIR)/

ifeq "$(OS)" "windows"
	mv $(APP_DIR)/README.md $(APP_DIR)/README.txt
	unix2dos $(APP_DIR)/README.txt
endif

PKG_VER := $(FAV_VER_STR)
PKG_ARCH := $(CPU)
ifeq "$(OS)" "linux"
	ifeq "$(CPU)" "amd64"
		PKG_ARCH := x86_64
	else
		PKG_ARCH := aarch64
	endif
else ifeq "$(OS)" "windows"
	PKG_ARCH := x64
endif
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

INST_ROOT := install-root

PKG_DEB := favia_$(PKG_VER)_$(CPU).deb
deb: $(PKG_DEB)
$(PKG_DEB): $(INST_ROOT) $(FAVIA_DIR)/installer/deb/control
	mkdir -p $(INST_ROOT)/DEBIAN
	sed -e "s/__FAVIA_VERSION__/$(PKG_VER)/" \
		-e "s/__ARCH__/$(CPU)/" \
		$(FAVIA_DIR)/installer/deb/control > $(INST_ROOT)/DEBIAN/control
	dpkg-deb -v --build $(INST_ROOT) $@
	rm -rf $(INST_ROOT)/DEBIAN

rpm: $(INST_ROOT) $(FAVIA_DIR)/installer/rpm/nfpm.yaml
	sed -e "s/__FAVIA_VERSION__/$(PKG_VER)/" \
		-e "s/__ARCH__/$(PKG_ARCH)/" \
		$(FAVIA_DIR)/installer/rpm/nfpm.yaml > $(INST_ROOT)/nfpm.yaml
	cd $(INST_ROOT) && nfpm pkg --config nfpm.yaml --packager rpm --target ..
	rm $(INST_ROOT)/nfpm.yaml

$(INST_ROOT): $(APP_DIR)
	mkdir -p \
		$@/opt \
		$@/usr/bin \
		$@/usr/share/applications \
		$@/usr/share/bash-completion/completions \
		$@/usr/share/icons/hicolor/scalable/apps
	cp -arv $(APP_DIR) $@/opt/
	ln -sf /opt/favia-0/favia $@/usr/bin/favia
	cp -av $(FAVIA_DIR)/res/favia.desktop $@/usr/share/applications/
	cp -av $(FAVIA_DIR)/res/favia.svg $@/usr/share/icons/hicolor/scalable/apps/
ifeq "$(CPU)" "amd64"
	./favia-0/favia __bash_completion > $@/usr/share/bash-completion/completions/favia
endif

release: default
	$(SUBMAKE) package
	$(SUBMAKE) package-debug
ifeq "$(OS)" "linux"
	$(SUBMAKE) deb
	$(SUBMAKE) rpm
endif

clean:
	rm -f *.o *.d *.so *.dll *.exe
	rm -rf $(APP_DIR) install-root
