#!/usr/bin/env pytest
# -*- coding: utf-8 -*-
###############################################################################
# Project:  GDAL/OGR Test Suite
# Purpose:  'gdal vector unnest' testing
# Author:   Dan Baston
#
###############################################################################
# Copyright (c) 2026, ISciences LLC
#
# SPDX-License-Identifier: MIT
###############################################################################

import string
import sys

import gdaltest
import pytest

from osgeo import gdal, ogr, osr


@pytest.fixture()
def alg():
    return gdal.Algorithm("vector", "unnest")


@pytest.fixture()
def source_with_arrays():

    np = pytest.importorskip("numpy")

    nfeat = 3
    array_length = 3

    src_ds = gdal.GetDriverByName("MEM").CreateVector("")
    src_lyr = src_ds.CreateLayer(
        "test", geom_type=ogr.wkbPoint, srs=osr.SpatialReference(epsg=4326)
    )
    src_lyr.CreateField(ogr.FieldDefn("name", ogr.OFTString))
    src_lyr.CreateField(ogr.FieldDefn("my_real_list", ogr.OFTRealList))
    src_lyr.CreateField(ogr.FieldDefn("my_int_list", ogr.OFTIntegerList))
    src_lyr.CreateField(ogr.FieldDefn("my_int64_list", ogr.OFTInteger64List))
    src_lyr.CreateField(ogr.FieldDefn("my_string_list", ogr.OFTStringList))

    feature = ogr.Feature(src_lyr.GetLayerDefn())
    for i in range(nfeat):
        feature["name"] = f"feat_{i}"
        feature["my_real_list"] = (i ** (np.arange(1, 1 + array_length))).tolist()
        feature["my_int_list"] = ((i + 1) * np.arange(array_length)).tolist()
        if sys.maxsize < (1 << 63) - 1:
            feature["my_int64_list"] = feature["my_int_list"]
        else:
            feature["my_int64_list"] = (2**32 + np.arange(array_length)).tolist()
        feature["my_string_list"] = list(string.ascii_letters[i : i + array_length])
        feature.SetGeometry(ogr.CreateGeometryFromWkt(f"POINT ({i} {2 * i})"))
        src_lyr.CreateFeature(feature)

    return src_ds


def test_gdalalg_vector_unnest_basic(alg, source_with_arrays):

    alg["input"] = source_with_arrays
    alg["field"] = [
        "name",
        "my_real_list",
        "my_int_list",
        "my_int64_list",
        "my_string_list",
    ]
    alg["index-field"] = "index"
    alg["output-format"] = "MEM"

    assert alg.Run()

    out_ds = alg.Output()
    assert out_ds.GetLayerCount() == 1

    src_lyr = source_with_arrays.GetLayer(0)
    out_lyr = out_ds.GetLayer(0)

    assert out_lyr.GetName() == "test"

    assert out_lyr.GetSpatialRef().IsSame(src_lyr.GetSpatialRef())
    assert out_lyr.GetFeatureCount() == 9

    out_defn = out_lyr.GetLayerDefn()
    out_fields = [
        out_defn.GetFieldDefn(i).GetName() for i in range(out_defn.GetFieldCount())
    ]

    assert out_fields == [
        "index",
        "name",
        "my_real_list",
        "my_int_list",
        "my_int64_list",
        "my_string_list",
    ]

    for i, src_feat in enumerate(src_lyr):
        for j in range(3):
            dst_feat = out_lyr.GetNextFeature()

            assert dst_feat["index"] == j
            assert dst_feat["name"] == src_feat["name"]
            assert dst_feat["my_real_list"] == src_feat["my_real_list"][j]
            assert dst_feat["my_int_list"] == src_feat["my_int_list"][j]
            assert dst_feat["my_int64_list"] == src_feat["my_int64_list"][j]
            assert dst_feat["my_string_list"] == src_feat["my_string_list"][j]

            assert dst_feat.GetGeometryRef().ExportToWkt() == f"POINT ({i} {2 * i})"


@pytest.mark.parametrize(
    "field_type",
    (ogr.OFTIntegerList, ogr.OFTInteger64List, ogr.OFTRealList, ogr.OFTStringList),
)
def test_gdalalg_vector_unnest_arrays_unequal_length(alg, field_type):

    src_ds = gdal.GetDriverByName("MEM").CreateVector("")
    src_lyr = src_ds.CreateLayer("test", geom_type=ogr.wkbNone)
    src_lyr.CreateField(ogr.FieldDefn("field_a", field_type))
    src_lyr.CreateField(ogr.FieldDefn("field_b", field_type))

    f = ogr.Feature(src_lyr.GetLayerDefn())

    f["field_a"] = [1, 2, 3]
    f["field_b"] = [4, 5, 6]
    src_lyr.CreateFeature(f)

    f["field_a"] = [7, 8, 9]
    f["field_b"] = [10, 11]
    src_lyr.CreateFeature(f)

    alg["input"] = src_ds
    alg["field"] = ["field_a", "field_b"]
    alg["output-format"] = "MEM"

    with pytest.raises(
        Exception,
        match="Field 'field_b' of source feature 1 does not have enough elements",
    ):
        alg.Run()


@pytest.mark.require_driver("GeoJSON")
def test_gdalalg_vector_unnest_ogrsf(alg, source_with_arrays, tmp_path):

    src_fname = tmp_path / "in.geojson"
    gdal.VectorTranslate(src_fname, source_with_arrays)

    alg["input"] = src_fname
    alg["field"] = ["name", "my_int_list"]

    gdaltest.algorithm_check_ogrsf(alg, tmp_path)
