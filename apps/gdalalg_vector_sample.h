/******************************************************************************
 *
 * Project:  GDAL
 * Purpose:  "sample" step of "vector pipeline"
 * Author:   Dan Baston
 *
 ******************************************************************************
 * Copyright (c) 2025, ISciences LLC
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#ifndef GDALALG_VECTOR_SAMPLE_INCLUDED
#define GDALALG_VECTOR_SAMPLE_INCLUDED

#include "gdalalg_vector_pipeline.h"

#include <optional>

//! @cond Doxygen_Suppress

/************************************************************************/
/*                     GDALVectorSampleAlgorithm                        */
/************************************************************************/

class GDALVectorSampleAlgorithm /* non final */
    : public GDALVectorPipelineStepAlgorithm
{
  public:
    static constexpr const char *NAME = "sample";
    static constexpr const char *DESCRIPTION =
        "Select a random sample from a dataset";
    static constexpr const char *HELP_URL = "/programs/gdal_vector_sample.html";

    explicit GDALVectorSampleAlgorithm(bool standaloneStep = false);

  private:
    bool RunStep(GDALPipelineStepRunContext &ctxt) override;

    std::string m_activeLayer{};
    int m_numSampledFeatures{};
    int m_seed{-1};
};

/************************************************************************/
/*                 GDALVectorSampleAlgorithmStandalone                  */
/************************************************************************/

class GDALVectorSampleAlgorithmStandalone final
    : public GDALVectorSampleAlgorithm
{
  public:
    GDALVectorSampleAlgorithmStandalone()
        : GDALVectorSampleAlgorithm(/* standaloneStep = */ true)
    {
    }

    ~GDALVectorSampleAlgorithmStandalone() override;
};

//! @endcond

#endif /* GDALALG_VECTOR_SAMPLE_INCLUDED */
