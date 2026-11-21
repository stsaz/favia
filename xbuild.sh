#!/bin/bash
# favia: cross-build for Linux/AMD64 | Windows/AMD64

OS=${OS:-linux}
CPU=${CPU:-amd64}
ARGS=${@@Q}

IMAGE_NAME=favia-debtx-builder
CONTAINER_NAME=favia_debtx_build
if [[ "$OS" == "windows" ]]; then
	IMAGE_NAME=favia-win64-builder
	CONTAINER_NAME=favia_win64_build
fi

set -xe

FAVIA_DIR="$(dirname "$0")"

if ! podman container exists $CONTAINER_NAME ; then
	if ! podman image exists $IMAGE_NAME ; then
		# Create builder image
		podman build -t $IMAGE_NAME -f "$FAVIA_DIR"/builder/Dockerfile.$OS-$CPU .
	fi

	# Create builder container
	podman create --attach --tty \
		-v "$(pwd)":/build:Z \
		-v "$FAVIA_DIR/..":/src:Z \
		--workdir /build \
		--name $CONTAINER_NAME \
		$IMAGE_NAME \
		sleep 3600
fi

if ! podman container top $CONTAINER_NAME ; then
	# Start container in background
	podman start --attach $CONTAINER_NAME &
	# Wait until the container is ready
	while ! podman container top $CONTAINER_NAME ; do
		sleep .5
	done
fi

ODIR=_$OS-$CPU
ARGS_OS=""
JOBS=$(nproc)
if [[ "$OS" == "windows" ]]; then
	ARGS_OS="OS=windows"
fi

# Prepare build script
cat >build.sh <<EOF
set -xe

mkdir -p $ODIR
export CMAKE_BUILD_PARALLEL_LEVEL=$JOBS
make \
 -C $ODIR \
 -f /src/favia/Makefile \
 ROOT_DIR=/src \
 COMPILER=clang \
 $ARGS_OS \
 depend

export PATH=\$PATH:/usr/lib/llvm-19/bin
make -j$JOBS \
 -C $ODIR \
 -f /src/favia/Makefile \
 ROOT_DIR=/src \
 COMPILER=clang \
 $ARGS_OS \
 $ARGS
EOF

# Build inside the container
podman exec $CONTAINER_NAME \
	bash build.sh
