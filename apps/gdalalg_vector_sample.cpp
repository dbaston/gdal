/******************************************************************************
 *
 * Project:  GDAL
 * Purpose:  "sort-geom" step of "vector pipeline"
 * Author:   Dan Baston
 *
 ******************************************************************************
 * Copyright (c) 2025, ISciences LLC
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "gdalalg_vector_sample.h"

#include "gdal_priv.h"
#include "ogrsf_frmts.h"
#include "ogr_p.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <random>
#include <vector>

//! @cond Doxygen_Suppress

#ifndef _
#define _(x) (x)
#endif

/************************************************************************/
/*          GDALVectorSampleAlgorithm::GDALVectorSampleAlgorithm()      */
/************************************************************************/

GDALVectorSampleAlgorithm::GDALVectorSampleAlgorithm(bool standaloneStep)
    : GDALVectorPipelineStepAlgorithm(NAME, DESCRIPTION, HELP_URL,
                                      standaloneStep)
{
    AddActiveLayerArg(&m_activeLayer);
    AddArg("n", 0, _("The number of features to sample per layer"),
           &m_numSampledFeatures)
        .SetPositional()
        .SetRequired();
    AddArg("seed", 0, _("Seed for random number generation"), &m_seed)
        .SetMinValueIncluded(0);
}

namespace
{

/************************************************************************/
/*                        GDALVectorSampledLayer                        */
/************************************************************************/

class GDALVectorSampledLayer : public OGRLayer
{
  public:
    GDALVectorSampledLayer(OGRLayer &layer, int n, std::optional<int> seed)
        : m_srcLayer(layer),
          m_randomRead(m_srcLayer.TestCapability(OLCFastSetNextByIndex)),
          m_it(m_sampledIndices.end())
    {
        constexpr bool bForceCount = true;

        std::vector<GIntBig> indices(layer.GetFeatureCount(bForceCount));
        std::iota(indices.begin(), indices.end(), 1);

        auto gen = std::mt19937{std::random_device{}()};
        if (seed.has_value())
        {
            gen.seed(seed.value());
        }

        m_sampledIndices.reserve(n);
        std::sample(indices.begin(), indices.end(),
                    std::back_inserter(m_sampledIndices), n, gen);
        std::sort(m_sampledIndices.begin(), m_sampledIndices.end());

        // FIXME test n > featureCount
        m_it = m_sampledIndices.begin();
        CPLDebug("GDALVectorSampledLayer", "RandomRead %d %lu", m_randomRead,
                 m_sampledIndices.size());
        //m_srcLayer.GetDataset()->Reference();
    }

    ~GDALVectorSampledLayer() override;

    OGRFeature *GetNextFeature() override
    {
        if (m_it == m_sampledIndices.end())
        {
            return nullptr;
        }

        std::unique_ptr<OGRFeature> ret;

        GIntBig nextIndex = *m_it;

        if (m_featuresRead == 0)
        {
            CPLDebug("nextIndex", "%d", (int)nextIndex);
        }

        if (false && m_randomRead)
        {
            m_srcLayer.SetNextByIndex(nextIndex);
            ret.reset(m_srcLayer.GetNextFeature());
        }
        else
        {
            while (m_featuresRead < nextIndex)
            {
                ret.reset(m_srcLayer.GetNextFeature());
                m_featuresRead++;
                if (ret == nullptr)
                {
                    m_featuresRead =
                        static_cast<GIntBig>(m_sampledIndices.size());
                    return nullptr;
                }
            }
        }
        ++m_it;
        return ret.release();
    }

    void ResetReading() override
    {
        m_it = m_sampledIndices.cbegin();
        m_currentFeatureIndex = 0;
        m_featuresRead = 0;
    }

    const OGRFeatureDefn *GetLayerDefn() const override
    {
        return m_srcLayer.GetLayerDefn();
    }

    int TestCapability(const char *) const override
    {
        return false;
    }

  private:
    OGRLayer &m_srcLayer;
    bool m_randomRead;
    std::vector<GIntBig> m_sampledIndices{};
    GIntBig m_featuresRead{0};
    GIntBig m_currentFeatureIndex{0};
    decltype(m_sampledIndices.cbegin()) m_it;
};

GDALVectorSampledLayer::~GDALVectorSampledLayer() = default;

}  // namespace

/************************************************************************/
/*              GDALVectorSampleGeomAlgorithm::RunStep()                  */
/************************************************************************/

bool GDALVectorSampleAlgorithm::RunStep(GDALPipelineStepRunContext &)
{
    auto poSrcDS = m_inputDataset[0].GetDatasetRef();
    CPLAssert(poSrcDS);

    CPLAssert(m_outputDataset.GetName().empty());
    CPLAssert(!m_outputDataset.GetDatasetRef());

    auto outDS = std::make_unique<GDALVectorOutputDataset>();

    for (auto &&poSrcLayer : poSrcDS->GetLayers())
    {
        std::optional<int> seed;
        if (m_seed >= 0)
        {
            seed = m_seed;
        }

        if (m_activeLayer.empty() ||
            m_activeLayer == poSrcLayer->GetDescription())
        {
            auto poSampledLayer = std::make_unique<GDALVectorSampledLayer>(
                *poSrcLayer, m_numSampledFeatures, seed);
            outDS->AddLayer(std::move(poSampledLayer));
        }
        else
        {
            //outDS->AddLayer(
            //    *poSrcLayer,
            //    std::make_unique<GDALVectorPipelinePassthroughLayer>(
            //        *poSrcLayer));
        }
    }

    m_outputDataset.Set(std::move(outDS));

    return true;
}

GDALVectorSampleAlgorithmStandalone::~GDALVectorSampleAlgorithmStandalone() =
    default;

//! @endcond
