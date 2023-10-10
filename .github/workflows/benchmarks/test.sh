#!/bin/bash

set -eu

BENCHMARK_STORAGE="file:///tmp"

(source ${GDAL_SOURCE_DIR:=..}/scripts/setdevenv.sh; pytest autotest/benchmark --benchmark-save=ref "--benchmark-storage=${BENCHMARK_STORAGE}" --capture=no -ra -vv)

(cd old_version/gdal/build; source ../scripts/setdevenv.sh; pytest autotest/benchmark --benchmark-compare-fail="mean:5%"  --benchmark-compare=0001_ref "--benchmark-storage=${BENCHMARK_STORAGE}" --capture=no -ra -vv)

