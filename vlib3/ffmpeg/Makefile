# build ffmpeg libs

# URL := https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.1.1.tar.gz
PKG := ../ffmpeg/ffmpeg-7.1.tar.xz
DIR := ffmpeg-7.1
SUBMAKE := +$(MAKE) -f $(firstword $(MAKEFILE_LIST))

default: libavformat.so

$(DIR): | $(PKG)
	tar xf $(PKG)
	chmod 777 $(DIR)

CONFIGURE_FLAGS := --enable-vaapi \
	--enable-hwaccel=h264_vaapi \
	--enable-hwaccel=hevc_vaapi \

$(DIR)/config.h: | $(DIR)
	cd $(DIR) && ./configure --disable-static --enable-shared \
		--disable-doc \
		--disable-programs \
		--disable-autodetect \
		--disable-network \
		--disable-devices \
		--disable-avdevice \
		--disable-avfilter --disable-filters \
		\
		--disable-protocols \
		--enable-protocol=file \
		\
		--disable-demuxers \
		--enable-demuxer=avi \
		--enable-demuxer=m4v \
		--enable-demuxer=matroska \
		--enable-demuxer=mpegts \
		--enable-demuxer=mov \
		\
		--disable-decoders \
		\
		--enable-decoder=av1 \
		--enable-decoder=h264 \
		--enable-decoder=hevc \
		--enable-decoder=mpeg1video \
		--enable-decoder=mpeg2video \
		--enable-decoder=mpeg4 \
		--enable-decoder=mpegvideo \
		--enable-decoder=vp9 \
		--enable-decoder=wmv1 \
		--enable-decoder=wmv2 \
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
		\
		--disable-encoders \
		--disable-muxers \
		$(CONFIGURE_FLAGS)
	sed -i.old 's/LDSOFLAGS=/LDSOFLAGS:='\''-Wl,-rpath,$$$$$$$$ORIGIN'\''/' $(DIR)/ffbuild/config.mak

$(DIR)/libavformat/libavformat.so: $(DIR)/config.h
	V=1 $(MAKE) -C $(DIR)

libavformat.so: | $(DIR)/config.h
	$(SUBMAKE) $(DIR)/libavformat/libavformat.so
	strip $(DIR)/libavutil/libavutil.so \
		$(DIR)/libavcodec/libavcodec.so \
		$(DIR)/libavformat/libavformat.so \
		$(DIR)/libswresample/libswresample.so \
		$(DIR)/libswscale/libswscale.so
	cp -a $(DIR)/libavutil/libavutil.so* \
		$(DIR)/libavcodec/libavcodec.so* \
		$(DIR)/libavformat/libavformat.so* \
		$(DIR)/libswresample/libswresample.so* \
		$(DIR)/libswscale/libswscale.so* \
		.
