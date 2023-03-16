#!/bin/sh

set -eu

CC=icc CXX=icx cmake ${GDAL_SOURCE_DIR:=..} -DCMAKE_BUILD_TYPE=Release -DUSE_CCACHE=ON
make -j$(nproc)

