/******************************************************************************
 *
 * Project:  GDAL
 * Purpose:  "limit" step of "vector pipeline"
 * Author:   Dan Baston
 *
 ******************************************************************************
 * Copyright (c) 2025, ISciences LLC
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "gdalalg_vector_limit.h"

#include "gdal_priv.h"
#include "ogrsf_frmts.h"
#include "ogr_p.h"

#include <set>

//! @cond Doxygen_Suppress

#ifndef _
#define _(x) (x)
#endif

/************************************************************************/
/*         GDALVectorLimitAlgorithm::GDALVectorLimitAlgorithm()       */
/************************************************************************/

GDALVectorLimitAlgorithm::GDALVectorLimitAlgorithm(bool standaloneStep)
    : GDALVectorPipelineStepAlgorithm(NAME, DESCRIPTION, HELP_URL,
                                      standaloneStep)
{
    AddArg("limit", 0, _("Limit the number of features to read per layer"),
           &m_featureLimit)
        .SetPositional()
        .SetRequired();
    AddActiveLayerArg(&m_activeLayer);
}

namespace
{

/************************************************************************/
/*                      GDALVectorReadLimitedLayer                      */
/************************************************************************/

class GDALVectorReadLimitedLayer : public GDALVectorPipelineOutputLayer
{
  public:
    GDALVectorReadLimitedLayer(OGRLayer &layer, int featureLimit)
        : GDALVectorPipelineOutputLayer(layer), m_featureLimit(featureLimit),
          m_featuresRead(0)
    {
    }

    ~GDALVectorReadLimitedLayer() override;

    bool TranslateFeature(
        std::unique_ptr<OGRFeature> poSrcFeature,
        std::vector<std::unique_ptr<OGRFeature>> &apoOutFeatures) override
    {
        m_featuresRead++;
        apoOutFeatures.push_back(std::move(poSrcFeature));
        return m_featuresRead <= m_featureLimit;
    }

    const OGRFeatureDefn *GetLayerDefn() const override
    {
        return m_srcLayer.GetLayerDefn();
    }

    int TestCapability(const char *pszCap) const override
    {
        // FIXME featurecount
        return m_srcLayer.TestCapability(pszCap);
    }

  private:
    int m_featureLimit;
    int m_featuresRead;
};

GDALVectorReadLimitedLayer::~GDALVectorReadLimitedLayer() = default;

}  // namespace

/************************************************************************/
/*               GDALVectorLimitAlgorithm::RunStep()                   */
/************************************************************************/

bool GDALVectorLimitAlgorithm::RunStep(GDALPipelineStepRunContext &)
{
    auto poSrcDS = m_inputDataset[0].GetDatasetRef();
    CPLAssert(poSrcDS);

    CPLAssert(m_outputDataset.GetName().empty());
    CPLAssert(!m_outputDataset.GetDatasetRef());

    auto outDS = std::make_unique<GDALVectorPipelineOutputDataset>(*poSrcDS);

    for (auto &&poSrcLayer : poSrcDS->GetLayers())
    {
        if (m_activeLayer.empty() ||
            m_activeLayer == poSrcLayer->GetDescription())
        {
            outDS->AddLayer(*poSrcLayer,
                            std::make_unique<GDALVectorReadLimitedLayer>(
                                *poSrcLayer, m_featureLimit));
        }
        else
        {
            outDS->AddLayer(
                *poSrcLayer,
                std::make_unique<GDALVectorPipelinePassthroughLayer>(
                    *poSrcLayer));
        }
    }

    m_outputDataset.Set(std::move(outDS));

    return true;
}

//! @endcond
