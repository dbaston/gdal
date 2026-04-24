/******************************************************************************
 *
 * Project:  GDAL
 * Purpose:  "unnest" step of "vector pipeline"
 * Author:   Daniel Baston
 *
 ******************************************************************************
 * Copyright (c) 2026, ISciences LLC
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#ifndef GDALALG_VECTOR_UNNEST_INCLUDED
#define GDALALG_VECTOR_UNNEST_INCLUDED

#include "gdalalg_vector_pipeline.h"

//! @cond Doxygen_Suppress

/************************************************************************/
/*                      GDALVectorUnnestAlgorithm                       */
/************************************************************************/

class GDALVectorUnnestAlgorithm /* non final */
    : public GDALVectorPipelineStepAlgorithm
{
  public:
    static constexpr const char *NAME = "unnest";
    static constexpr const char *DESCRIPTION =
        "Unnest array fields of a vector dataset into multiple features.";
    static constexpr const char *HELP_URL = "/programs/gdal_vector_unnest.html";

    explicit GDALVectorUnnestAlgorithm(bool standaloneStep = false);

  protected:
    bool RunStep(GDALPipelineStepRunContext &ctxt) override;

  private:
    std::vector<std::string> m_fields{};
    std::string m_indexFieldName{"idx"};
    bool m_addIndexField = false;
};

/************************************************************************/
/*                 GDALVectorUnnestAlgorithmStandalone                  */
/************************************************************************/

class GDALVectorUnnestAlgorithmStandalone final
    : public GDALVectorUnnestAlgorithm
{
  public:
    GDALVectorUnnestAlgorithmStandalone()
        : GDALVectorUnnestAlgorithm(/* standaloneStep = */ true)
    {
    }

    ~GDALVectorUnnestAlgorithmStandalone() override;
};

//! @endcond

#endif /* GDALALG_VECTOR_UNNEST_INCLUDED */
