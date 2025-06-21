#!/bin/bash

# favia: cross-build on Linux for Debian-bookworm

IMAGE_NAME=favia-debianbw-builder
CONTAINER_NAME=favia_debianBW_build

set -xe

if ! test -d "../favia" ; then
	exit 1
fi

if ! podman container exists $CONTAINER_NAME ; then
	if ! podman image exists $IMAGE_NAME ; then

		# Create builder image
		cat <<EOF | podman build -t $IMAGE_NAME -f - .
FROM debian:bookworm-slim
RUN apt update && \
 apt install -y \
  make
RUN apt install -y \
 gcc g++
RUN apt install -y \
 libva-dev
EOF
	fi

	# Create builder container
	podman create --attach --tty \
	 -v `pwd`/..:/src \
	 --name $CONTAINER_NAME \
	 $IMAGE_NAME \
	 bash -c 'cd /src/favia && source ./build_linux.sh'
fi

if ! podman container top $CONTAINER_NAME ; then
	cat >build_linux.sh <<EOF
sleep 600
EOF
	# Start container in background
	podman start --attach $CONTAINER_NAME &
	sleep .5
	while ! podman container top $CONTAINER_NAME ; do
		sleep .5
	done
fi

# Prepare build script
cat >build_linux.sh <<EOF
set -xe

mkdir -p vlib3/_linux-amd64

make -j16 \
 -C vlib3/_linux-amd64 \
 -f ../ffmpeg/Makefile

export CMAKE_BUILD_PARALLEL_LEVEL=16
make \
 -C vlib3/_linux-amd64 \
 -f ../SDL/Makefile

mkdir -p _linux-amd64
make -j8 \
 -C _linux-amd64 \
 -f ../Makefile \
 ROOT_DIR=../.. \
 $@
EOF

# Build inside the container
podman exec $CONTAINER_NAME \
 bash -c 'cd /src/favia && source ./build_linux.sh'
