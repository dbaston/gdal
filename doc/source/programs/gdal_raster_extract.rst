.. _gdal_raster_extract:

================================================================================
``gdal raster extract``
================================================================================

.. versionadded:: 3.12

.. only:: html

    Extract raster pixels as tabular / vector data

.. Index:: gdal raster extract

Synopsis
--------

.. program-output:: gdal raster extract --help-doc

Description
-----------

TODO

Examples
--------

.. example::
   :title: Extract pixels having a value less than 150

   .. code-block:: bash

       gdal pipeline read autotest/gcore/data/nodata_byte.tif ! 
                reclassify -m "[-inf, 150)=1; DEFAULT=NO_DATA" !
                extract --geometry-type point --skip-nodata ! 
                write out.shp

.. example::
   :title: Extract pixels within a rasterized

   .. code-block:: bash

      gdal pipeline read autotest/ogr/data/poly.shp ! 
               rasterize -a EAS_ID --extent "478000,4762600,482000,4766000" --resolution "100,100" --all-touched --output-data-type Int32 !
               reclassify -m  "DEFAULT=PASS_THROUGH; 0=NO_DATA" !
               extract --geometry-type polygon --skip-nodata !
               write out.shp
