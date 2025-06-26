/******************************************************************************
*
 * Project:  GDAL
 * Purpose:  "extract" step of "gdal pipeline"
 * Author:   Daniel Baston
 *
 ******************************************************************************
 * Copyright (c) 2025, ISciences, LLC
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "gdalalg_raster_extract.h"

#include "cpl_conv.h"
#include "gdal_priv.h"
#include "gdal_alg.h"
#include "ogrsf_frmts.h"

//! @cond Doxygen_Suppress

GDALRasterExtractAlgorithm::GDALRasterExtractAlgorithm(bool standaloneStep)
    : GDALPipelineStepAlgorithm(
          NAME, DESCRIPTION, HELP_URL,
          ConstructorOptions()
              .SetStandaloneStep(standaloneStep)
              .SetOutputFormatCreateCapability(GDAL_DCAP_CREATE)),
      m_skipNoData(false)
{
}

GDALRasterExtractAlgorithm::~GDALRasterExtractAlgorithm() = default;

GDALRasterExtractAlgorithmStandalone::~GDALRasterExtractAlgorithmStandalone() =
    default;

//! @endcond
