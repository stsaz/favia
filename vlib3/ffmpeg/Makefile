# build ffmpeg libs

include $(FAV_VLIB3)/../../ffbase/conf.mk
CURL := curl -L

URL := https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.1.3.tar.gz
PKG := $(FAV_VLIB3)/ffmpeg/$(notdir $(URL))
PKG_CSUM := e0b04c4b43d7e6d67cb6710334fb513adf13ac860532f30e1d0ac4c231f232fb
DIR := FFmpeg-n7.1.3

NV_CODEC_URL := https://github.com/FFmpeg/nv-codec-headers/releases/download/n13.1.15.0/nv-codec-headers-13.1.15.0.tar.gz
NV_CODEC_PKG := $(FAV_VLIB3)/ffmpeg/$(notdir $(NV_CODEC_URL))
NV_CODEC_PKG_CSUM := 52532ceade3d5c1af62624986f13cf01b63c910576b08c0c278756c5e4b41ad0
NV_CODEC_DIR := nv-codec-headers-13.1.15.0

default: libffmpeg.$(SO)

$(PKG):
	$(CURL) -o $(PKG) $(URL)

$(DIR): $(PKG)
	echo "$(PKG_CSUM) *$(PKG)" | $(SHA256SUM) -c -
	tar xf $(PKG)
	chmod 777 $(DIR)
	touch $@

ifeq "$(OS)" "linux"
$(NV_CODEC_PKG):
	$(CURL) -o $(NV_CODEC_PKG) $(NV_CODEC_URL)

$(NV_CODEC_DIR): $(NV_CODEC_PKG)
	echo "$(NV_CODEC_PKG_CSUM) *$(NV_CODEC_PKG)" | $(SHA256SUM) -c -
	tar xf $(NV_CODEC_PKG)
	chmod 777 $(NV_CODEC_DIR)
	touch $@
endif

CONFIGURE_FLAGS :=
ifeq "$(OS)" "windows"
	CONFIGURE_FLAGS += --cross-prefix=x86_64-w64-mingw32- --target-os=mingw64 --arch=amd64
endif

CONFIGURE_FLAGS += --disable-static --enable-shared \
	--disable-doc \
	--disable-programs \
	--disable-autodetect \
	--disable-network \
	--disable-devices \
	--disable-avdevice \
	--disable-avfilter --disable-filters \
	--disable-encoders \
	--disable-demuxers \
	--disable-muxers \
	--disable-decoders \
	--disable-hwaccels \
	--disable-protocols

CONFIGURE_FLAGS += \
	--enable-protocol=file \
	\
	--enable-demuxer=asf \
	--enable-demuxer=asf_o \
	--enable-demuxer=avi \
	--enable-demuxer=m4v \
	--enable-demuxer=matroska \
	--enable-demuxer=mov \
	--enable-demuxer=mpegts \
	\
	--enable-decoder=av1 \
	--enable-decoder=h264 \
	--enable-decoder=hevc \
	--enable-decoder=mpeg1video \
	--enable-decoder=mpeg2video \
	--enable-decoder=mpeg4 \
	--enable-decoder=mpegvideo \
	--enable-decoder=vp9 \
	--enable-decoder=wmv3 \
	\
	--enable-decoder=aac \
	--enable-decoder=aac_fixed \
	--enable-decoder=aac_latm \
	--enable-decoder=ac3 \
	--enable-decoder=ac3_fixed \
	--enable-decoder=alac \
	--enable-decoder=flac \
	--enable-decoder=mp3 \
	--enable-decoder=mp3float \
	--enable-decoder=opus \
	--enable-decoder=vorbis \
	--enable-decoder=wmav2

CONFIGURE_FLAGS += \
	--enable-demuxer=image_bmp_pipe \
	--enable-demuxer=image_gif_pipe \
	--enable-demuxer=image_jpeg_pipe \
	--enable-demuxer=image_png_pipe \
	--enable-demuxer=image_webp_pipe \
	\
	--enable-decoder=bmp \
	--enable-decoder=gif \
	--enable-decoder=mjpeg \
	--enable-decoder=webp

ifeq "$(OS)" "linux"
	CONFIGURE_FLAGS += --enable-zlib \
		--enable-decoder=png

	CONFIGURE_FLAGS += --enable-ffnvcodec \
		--enable-nvdec \
		--extra-cflags=-I../$(NV_CODEC_DIR)/include \
		--enable-hwaccel=av1_nvdec \
		--enable-hwaccel=h264_nvdec \
		--enable-hwaccel=hevc_nvdec \
		--enable-hwaccel=hevc_nvdec \
		--enable-hwaccel=mjpeg_nvdec \
		--enable-hwaccel=mpeg1_nvdec \
		--enable-hwaccel=mpeg2_nvdec \
		--enable-hwaccel=mpeg4_nvdec \
		--enable-hwaccel=vp9_nvdec \
		--enable-hwaccel=wmv3_nvdec \
		--enable-hwaccel=wmv3_nvdec

	CONFIGURE_FLAGS += --enable-vaapi \
		--enable-hwaccel=mjpeg_vaapi \
		--enable-hwaccel=h264_vaapi \
		--enable-hwaccel=hevc_vaapi
else
	CONFIGURE_FLAGS += --enable-d3d11va \
		--enable-hwaccel=h264_d3d11va \
		--enable-hwaccel=h264_d3d11va2 \
		--enable-hwaccel=hevc_d3d11va \
		--enable-hwaccel=hevc_d3d11va2 \
		--enable-dxva2 \
		--enable-hwaccel=h264_dxva2 \
		--enable-hwaccel=hevc_dxva2
endif

ifeq "$(OS)" "linux"
$(DIR)/config.h: | $(DIR) $(NV_CODEC_DIR)
	cd $(NV_CODEC_DIR) && sed 's#@@PREFIX@@#$(abspath $(NV_CODEC_DIR))#' ffnvcodec.pc.in > ffnvcodec.pc
	cd $(DIR) && PKG_CONFIG_PATH=$(abspath $(NV_CODEC_DIR)) ./configure $(CONFIGURE_FLAGS)
else
$(DIR)/config.h: | $(DIR)
	cd $(DIR) && ./configure $(CONFIGURE_FLAGS)
endif

LINKFLAGS += -shared
ifeq "$(OS)" "linux"
	LINKFLAGS += -Wl,-Bsymbolic -Wl,-z,noexecstack -lm -lva-drm -lva -lz
else
	LINKFLAGS += -lole32 -lbcrypt
endif

link:
	$(LINK) $(shell find $(DIR) -name '*.o') $(LINKFLAGS) -o libffmpeg.$(SO)

libffmpeg.$(SO): | $(DIR)/config.h
	V=1 $(MAKE) -C $(DIR)
	$(RM) $(DIR)/libswresample/log2_tab.o \
		$(DIR)/libavformat/log2_tab.o \
		$(DIR)/libavcodec/log2_tab.o \
		$(DIR)/libswscale/log2_tab.o \
		$(DIR)/libavcodec/reverse.o \
		$(DIR)/libavcodec/to_upper4.o \
		$(DIR)/libavcodec/mpegaudiotabs.o \
		$(DIR)/libavcodec/mpeg4audio_sample_rates.o \
		$(DIR)/libavcodec/ac3_channel_layout_tab.o
	$(RM) $(DIR)/libavformat/avformatres.o \
		$(DIR)/libswresample/swresampleres.o \
		$(DIR)/libavutil/avutilres.o \
		$(DIR)/libswscale/swscaleres.o \
		$(DIR)/libavcodec/avcodecres.o
	$(SUBMAKE) link
	$(SUBMAKE) $@.debug

%.debug: %
	$(OBJCOPY) --only-keep-debug $< $@
	$(STRIP) $<
	$(OBJCOPY) --add-gnu-debuglink=$@ $<
	touch $@
