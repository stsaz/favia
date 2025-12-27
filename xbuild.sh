#!/bin/bash

# favia: cross-build on Linux for Linux/Windows

IMAGE_NAME=favia-debianbw-builder
CONTAINER_NAME=favia_debianBW_build
BUILD_TARGET=linux
if test "$OS" == "" ; then
	OS=linux
fi
if test "$OS" == "windows" ; then
	IMAGE_NAME=favia-win64-builder
	CONTAINER_NAME=favia_win64_build
	BUILD_TARGET=mingw64
fi
ARGS=${@@Q}
JOBS=16

set -xe

if ! test -d "../favia" ; then
	exit 1
fi

image_linux() {
	cat <<EOF | podman build -t $IMAGE_NAME -f - .
FROM debian:bookworm-slim
RUN apt update && \
 apt install -y \
  make
RUN apt install -y \
 curl \
 cmake \
 nasm
RUN apt install -y \
 gcc g++
RUN apt install -y \
 libva-dev
EOF
}

image_windows() {
	cat <<EOF | podman build -t $IMAGE_NAME -f - .
FROM debian:bookworm-slim
RUN apt update && \
 apt install -y \
  make
RUN apt install -y \
 curl \
 cmake \
 nasm
RUN apt install -y \
 gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64
EOF
}

if ! podman container exists $CONTAINER_NAME ; then
	if ! podman image exists $IMAGE_NAME ; then
		# Create builder image
		image_$OS
	fi

	# Create builder container
	podman create --attach --tty \
	 -v `pwd`/..:/src \
	 --workdir /src/favia \
	 --name $CONTAINER_NAME \
	 $IMAGE_NAME \
	 bash ./build_$BUILD_TARGET.sh
fi

if ! podman container top $CONTAINER_NAME ; then
	cat >build_$BUILD_TARGET.sh <<EOF
sleep 600
EOF
	# Start container in background
	podman start --attach $CONTAINER_NAME &
	# Wait until the container is ready
	sleep .5
	while ! podman container top $CONTAINER_NAME ; do
		sleep .5
	done
fi

# Prepare build script

ODIR=_linux-amd64
ARGS_OS=""

if test "$OS" == "windows" ; then
	ODIR=_windows-amd64
	ARGS_OS="OS=windows \
COMPILER=gcc \
CROSS_PREFIX=x86_64-w64-mingw32-"
fi

cat >build_$BUILD_TARGET.sh <<EOF
set -xe

mkdir -p vlib3/$ODIR

make -j$JOBS \
 -C vlib3/$ODIR \
 -f ../ffmpeg/Makefile \
 -I .. \
 $ARGS_OS

export CMAKE_BUILD_PARALLEL_LEVEL=$JOBS
make \
 -C vlib3/$ODIR \
 -f ../SDL/Makefile \
 -I .. \
 $ARGS_OS

mkdir -p $ODIR
make -j$JOBS \
 -C $ODIR \
 -f ../Makefile \
 ROOT_DIR=../.. \
 $ARGS_OS \
 $ARGS

# cp -au /usr/lib/x86_64-linux-gnu/libasan.so.8* /src/favia/_linux-amd64/favia-0/
EOF

# Build inside the container
podman exec $CONTAINER_NAME \
 bash ./build_$BUILD_TARGET.sh
