#!/bin/sh

set -e

USER=root
export USER

TRAVIS=yes
export TRAVIS

BUILD_NAME=alpine
export BUILD_NAME

cmake ${GDAL_SOURCE_DIR:=..} \
  -DCMAKE_BUILD_TYPE=Release -DUSE_CCACHE=ON -DCMAKE_INSTALL_PREFIX=/usr \
  -DIconv_INCLUDE_DIR=/usr/include/gnu-libiconv \
  -DIconv_LIBRARY=/usr/lib/libiconv.so \
  -DCMAKE_C_FLAGS=-Werror -DCMAKE_CXX_FLAGS=-Werror -DWERROR_DEV_FLAG="-Werror=dev"
make -j$(nproc)
