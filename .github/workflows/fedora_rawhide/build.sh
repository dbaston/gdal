#!/bin/sh

set -e

USER=root
export USER

TRAVIS=yes
export TRAVIS

BUILD_NAME=fedora
export BUILD_NAME

CC=clang CXX=clang++ LDFLAGS='-lstdc++' cmake ${GDAL_SOURCE_DIR:=..} \
  -DCMAKE_BUILD_TYPE=Release -DUSE_CCACHE=ON -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_C_FLAGS="-Werror -O1 -D_FORTIFY_SOURCE=2" \
  -DCMAKE_CXX_FLAGS="-std=c++20 -Werror -O1 -D_FORTIFY_SOURCE=2" \
  -DWERROR_DEV_FLAG="-Werror=dev"
make -j$(nproc)
