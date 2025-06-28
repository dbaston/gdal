#!/usr/bin/env pytest
# -*- coding: utf-8 -*-
###############################################################################
# Project:  GDAL/OGR Test Suite
# Purpose:  'gdal raster calc' testing
# Author:   Daniel Baston
#
###############################################################################
# Copyright (c) 2025, ISciences LLC
#
# SPDX-License-Identifier: MIT
###############################################################################

import pytest

from osgeo import gdal, ogr

gdal.UseExceptions()


@pytest.fixture()
def extract():
    reg = gdal.GetGlobalAlgorithmRegistry()
    raster = reg.InstantiateAlg("raster")
    return raster.InstantiateSubAlgorithm("extract")


def test_gdalalg_raster_skip_nodata(extract, tmp_vsimem):

    extract["input"] = "../gcore/data/nodata_byte.tif"
    extract["output"] = tmp_vsimem / "out.gpkg"
    extract["skip-nodata"] = True

    assert extract.Run()

    with gdal.OpenEx(tmp_vsimem / "out.gpkg", gdal.OF_VECTOR) as ds:
        assert ds.GetLayerCount() == 1
        lyr = ds.GetLayer(0)
        assert lyr.GetFeatureCount() == 380


@pytest.mark.parametrize("geom_type", ("Point", "Polygon", "None"))
def test_gdalalg_raster_extract_geom_type(extract, tmp_path, geom_type):

    extract["input"] = "../gcore/data/byte.tif"
    extract["output"] = tmp_path / "out.gpkg"
    extract["geometry-type"] = geom_type

    assert extract.Run()

    with gdal.OpenEx(tmp_path / "out.gpkg", gdal.OF_VECTOR) as ds:
        assert ds.GetLayerCount() == 1
        lyr = ds.GetLayer(0)
        assert lyr.GetFeatureCount() == 400
        assert ogr.GeometryTypeToName(lyr.GetGeomType()) == geom_type


def test_gdalalg_raster_extract_geom_type_invalid(extract):

    with pytest.raises(Exception, match="Invalid value .* 'geometry-type'"):
        extract["geometry-type"] = "LineString"
