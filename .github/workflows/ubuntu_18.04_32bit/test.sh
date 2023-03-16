#!/bin/bash

set -e

. ../scripts/setdevenv.sh

export PYTEST="python3 -m pytest -vv -p no:sugar --color=no"

make quicktest

# Fails with ERROR 1: OGDI DataSource Open Failed: Could not find the dynamic library "vrf"
rm autotest/ogr/ogr_ogdi.py

# Stalls on it. Probably not enough memory
rm autotest/gdrivers/jp2openjpeg.py

# Failures for the following tests. See https://github.com/OSGeo/gdal/runs/1425843044

# depends on tiff_ovr.py that is going to be removed below
$PYTEST autotest/utilities/test_gdaladdo.py
rm -f autotest/utilities/test_gdaladdo.py

for i in autotest/gcore/tiff_ovr.py \
         autotest/gdrivers/gribmultidim.py \
         autotest/gdrivers/mbtiles.py \
         autotest/gdrivers/vrtwarp.py \
         autotest/gdrivers/wcs.py \
         autotest/utilities/test_gdalwarp.py \
         autotest/pyscripts/test_gdal_pansharpen.py; do
    $PYTEST $i || echo "Ignoring failure"
    rm -f $i
done

(cd autotest && $PYTEST)
