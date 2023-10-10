#!/usr/bin/env pytest
# -*- coding: utf-8 -*-
###############################################################################
# $Id$
#
# Project:  GDAL/OGR Test Suite
# Purpose:  Benchmarking of GeoTIFF driver
# Author:   Even Rouault <even dot rouault at spatialys.com>
#
###############################################################################
# Copyright (c) 2023, Even Rouault <even dot rouault at spatialys.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
###############################################################################

import array
from threading import Thread

import gdaltest
import pytest

from osgeo import gdal

# Must be set to run the test_XXX functions under the benchmark fixture
pytestmark = pytest.mark.usefixtures("decorate_with_benchmark")


def test_gtiff_byte():
    gdal.Open("../gcore/data/byte.tif")


def test_gtiff_byte_get_srs():
    ds = gdal.Open("../gcore/data/byte.tif")
    ds.GetSpatialRef()


@pytest.mark.parametrize("with_optim", [True, False])
def test_gtiff_multithread_write(with_optim):
    num_threads = gdal.GetNumCPUs()
    nbands = 1
    compression = "DEFLATE"
    buffer_pixel_interleaved = True
    width = 2048
    height = 2048

    nloops = 10 // nbands
    data = array.array("B", [i % 255 for i in range(nbands * width * height)])

    def thread_function(num):
        filename = "/vsimem/tmp%d.tif" % num
        drv = gdal.GetDriverByName("GTiff")
        options = ["TILED=YES", "COMPRESS=" + compression]
        for i in range(nloops):
            ds = drv.Create(filename, width, height, nbands, options=options)
            if not with_optim:
                # Calling ReadRaster() disables the cache bypass write optimization
                ds.GetRasterBand(1).ReadRaster(0, 0, 1, 1)
            if nbands > 1:
                if buffer_pixel_interleaved:
                    # Write pixel-interleaved buffer for maximum efficiency
                    ds.WriteRaster(
                        0,
                        0,
                        width,
                        height,
                        data,
                        buf_pixel_space=nbands,
                        buf_line_space=width * nbands,
                        buf_band_space=1,
                    )
                else:
                    ds.WriteRaster(0, 0, width, height, data)
            else:
                ds.GetRasterBand(1).WriteRaster(0, 0, width, height, data)
        gdal.Unlink(filename)

    with gdaltest.SetCacheMax(width * height * nbands * num_threads):

        # Spawn num_threads running thread_function
        threads_array = []

        for i in range(num_threads):
            t = Thread(
                target=thread_function,
                args=[i],
            )
            t.start()
            threads_array.append(t)

        for t in threads_array:
            t.join()
