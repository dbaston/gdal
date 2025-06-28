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

#ifndef GDALALG_RASTER_EXTRACT_INCLUDED
#define GDALALG_RASTER_EXTRACT_INCLUDED

#include "gdalalg_abstract_pipeline.h"

//! @cond Doxygen_Suppress

/************************************************************************/
/*                      GDALRasterExtractAlgorithm                      */
/************************************************************************/

class GDALRasterExtractAlgorithm /* non final */
    : public GDALPipelineStepAlgorithm
{
  public:
    static constexpr const char *NAME = "extract";
    static constexpr const char *DESCRIPTION =
        "Extract pixel values from a raster dataset";
    static constexpr const char *HELP_URL =
        "/programs/gdal_raster_extract.html";

    explicit GDALRasterExtractAlgorithm(bool standaloneStep = false);

    ~GDALRasterExtractAlgorithm() override;

    bool IsNativelyStreamingCompatible() const override
    {
        return false;
    }

    int GetInputType() const override
    {
        return GDAL_OF_RASTER;
    }

    int GetOutputType() const override
    {
        return GDAL_OF_VECTOR;
    }

  private:
    bool RunStep(GDALPipelineStepRunContext &ctxt) override;
    bool RunImpl(GDALProgressFunc pfnProgress, void *pProgressData) override;

    std::string m_geomTypeName;
    bool m_skipNoData;
    bool m_includeXY;
    bool m_includeRowCol;
};

/************************************************************************/
/*                 GDALRasterExtractAlgorithmStandalone              */
/************************************************************************/

class GDALRasterExtractAlgorithmStandalone final
    : public GDALRasterExtractAlgorithm
{
  public:
    GDALRasterExtractAlgorithmStandalone()
        : GDALRasterExtractAlgorithm(/* standaloneStep = */ true)
    {
    }

    ~GDALRasterExtractAlgorithmStandalone() override;
};

//! @endcond

#endif