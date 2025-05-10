# build SDL

# URL := https://github.com/libsdl-org/SDL/releases/download/release-3.2.12/SDL3-3.2.12.tar.gz
PKG := ../SDL/SDL3-3.2.10.tar.gz
DIR := SDL3-3.2.10
SUBMAKE := +$(MAKE) -f $(firstword $(MAKEFILE_LIST))

default: libSDL3.so

$(DIR): | $(PKG)
	tar xf $(PKG)

$(DIR)/build/libSDL3.so: | $(DIR)
	cd $(DIR) && cmake -S . -B build \
		-DSDL_SHARED=ON -DSDL_STATIC=OFF
	cd $(DIR) && cmake --build build

libSDL3.so: | $(DIR)
	$(SUBMAKE) $(DIR)/build/libSDL3.so
	strip $(DIR)/build/libSDL3.so
	cp -a $(DIR)/build/libSDL3.so* .
