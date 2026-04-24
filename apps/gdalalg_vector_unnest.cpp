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

#include "gdalalg_vector_unnest.h"

#include "cpl_conv.h"
#include "cpl_string.h"
#include "gdal_priv.h"
#include "ogrsf_frmts.h"

#include <cinttypes>
#include <memory>
#include <vector>

//! @cond Doxygen_Suppress

#ifndef _
#define _(x) (x)
#endif

/************************************************************************/
/*        GDALVectorUnnestAlgorithm::GDALVectorUnnestAlgorithm()        */
/************************************************************************/

GDALVectorUnnestAlgorithm::GDALVectorUnnestAlgorithm(bool standaloneStep)
    : GDALVectorPipelineStepAlgorithm(NAME, DESCRIPTION, HELP_URL,
                                      standaloneStep)
{
    AddArg("field", 0, _("Field(s) to include in the output"), &m_fields)
        .SetRequired()
        .SetMinCount(1)
        .SetMetaVar("FIELD");

    AddArg("index-field", 0, _("Name of the output index field"),
           &m_indexFieldName)
        .SetDefault(m_indexFieldName)
        .AddAction([this] { m_addIndexField = true; });
}

GDALVectorUnnestAlgorithmStandalone::~GDALVectorUnnestAlgorithmStandalone() =
    default;

namespace
{

class GDALVectorUnnestLayer final : public GDALVectorPipelineOutputLayer
{
  public:
    GDALVectorUnnestLayer(OGRLayer &srcLayer,
                          const std::vector<std::string> &fields,
                          const std::string &indexFieldName, bool addIndexField)
        : GDALVectorPipelineOutputLayer(srcLayer),
          m_addIndexField(addIndexField),
          m_poFeatureDefn(OGRFeatureDefn::CreateFeatureDefn(srcLayer.GetName()))
    {
        // Avoid creating geometry field with null SRS
        // We'll copy it in later from the source layer
        m_poFeatureDefn->DeleteGeomFieldDefn(0);

        if (addIndexField)
        {
            auto poIdxField = std::make_unique<OGRFieldDefn>(
                indexFieldName.c_str(), OFTInteger);
            m_poFeatureDefn->AddFieldDefn(std::move(poIdxField));
        }

        const OGRFeatureDefn *poSrcDefn = srcLayer.GetLayerDefn();

        m_listFieldSrcToDstMap.resize(poSrcDefn->GetFieldCount(), -1);
        m_scalarFieldSrcToDstMap.resize(poSrcDefn->GetFieldCount(), -1);

        for (int iField = 0; iField < poSrcDefn->GetFieldCount(); iField++)
        {
            const auto *poSrcFieldDefn = poSrcDefn->GetFieldDefn(iField);
            const std::string osFieldName = poSrcFieldDefn->GetNameRef();

            const bool bIsSelected = std::find(fields.begin(), fields.end(),
                                               osFieldName) != fields.end();

            if (bIsSelected)
            {
                const int iDstField = m_poFeatureDefn->GetFieldCount();
                std::unique_ptr<OGRFieldDefn> poDstFieldDefn;

                if (OGR_GetFieldTypeIsList(poSrcFieldDefn->GetType()))
                {
                    const auto eScalarType =
                        OGR_GetFieldTypeAsScalar(poSrcFieldDefn->GetType());

                    poDstFieldDefn = std::make_unique<OGRFieldDefn>(
                        poSrcFieldDefn->GetNameRef(), eScalarType);

                    m_listFieldSrcToDstMap[iField] = iDstField;
                }
                else
                {
                    poDstFieldDefn =
                        std::make_unique<OGRFieldDefn>(*poSrcFieldDefn);
                    m_scalarFieldSrcToDstMap[iField] = iDstField;
                }

                m_poFeatureDefn->AddFieldDefn(std::move(poDstFieldDefn));
            }
        }

        // Copy geometry field definitions unchanged.
        for (int iGeom = 0; iGeom < poSrcDefn->GetGeomFieldCount(); ++iGeom)
        {
            auto poGeomFieldDefn = std::make_unique<OGRGeomFieldDefn>(
                *poSrcDefn->GetGeomFieldDefn(iGeom));
            m_poFeatureDefn->AddGeomFieldDefn(std::move(poGeomFieldDefn));
        }
    }

    const char *GetDescription() const override
    {
        return m_poFeatureDefn->GetName();
    }

    const OGRFeatureDefn *GetLayerDefn() const override
    {
        return m_poFeatureDefn.get();
    }

    void ResetReading() override
    {
        m_nextFID = 1;
        GDALVectorPipelineOutputLayer::ResetReading();
    }

    int TestCapability(const char *pszCap) const override
    {
        if (EQUAL(pszCap, OLCFastGetExtent) ||
            EQUAL(pszCap, OLCFastGetExtent3D) ||
            EQUAL(pszCap, OLCStringsAsUTF8) ||
            EQUAL(pszCap, OLCCurveGeometries) ||
            EQUAL(pszCap, OLCMeasuredGeometries) ||
            EQUAL(pszCap, OLCZGeometries))
        {
            return m_srcLayer.TestCapability(pszCap);
        }

        return false;
    }

    bool TranslateFeature(
        std::unique_ptr<OGRFeature> poSrcFeature,
        std::vector<std::unique_ptr<OGRFeature>> &apoOutFeatures) override
    {
        int nDstFeatures = 1;

        for (int iDstFeature = 0; iDstFeature < nDstFeatures; iDstFeature++)
        {
            auto poDstFeature =
                std::make_unique<OGRFeature>(m_poFeatureDefn.get());
            if (m_addIndexField)
            {
                poDstFeature->SetField(0, iDstFeature);
            }

            if (poDstFeature->SetFrom(poSrcFeature.get(),
                                      m_scalarFieldSrcToDstMap.data(),
                                      true) != OGRERR_NONE)
            {
                CPLError(CE_Failure, CPLE_AppDefined,
                         "Failed to set fields of output feature");
                return false;
            }

            for (int iSrcArrayField = 0;
                 iSrcArrayField <
                 static_cast<int>(m_listFieldSrcToDstMap.size());
                 iSrcArrayField++)
            {
                int iDstField = m_listFieldSrcToDstMap[iSrcArrayField];
                if (iDstField < 0)
                {
                    continue;
                }

                const auto poSrcFieldDefn =
                    poSrcFeature->GetFieldDefnRef(iSrcArrayField);
                const auto eSrcType = poSrcFieldDefn->GetType();
                int nArrayLength = -1;
                if (eSrcType == OFTIntegerList)
                {
                    const int *pnArray = poSrcFeature->GetFieldAsIntegerList(
                        iSrcArrayField, &nArrayLength);
                    if (iDstFeature >= nArrayLength)
                    {
                        CPLError(CE_Failure, CPLE_AppDefined,
                                 "Field '%s' of source feature %" PRId64
                                 " does not have enough elements.",
                                 poSrcFieldDefn->GetNameRef(),
                                 static_cast<int64_t>(poSrcFeature->GetFID()));
                        return false;
                    }
                    poDstFeature->SetField(iDstField, pnArray[iDstFeature]);
                }
                else if (eSrcType == OFTInteger64List)
                {
                    const GIntBig *pnArray =
                        poSrcFeature->GetFieldAsInteger64List(iSrcArrayField,
                                                              &nArrayLength);
                    if (iDstFeature >= nArrayLength)
                    {
                        CPLError(CE_Failure, CPLE_AppDefined,
                                 "Field '%s' of source feature %" PRId64
                                 " does not have enough elements.",
                                 poSrcFieldDefn->GetNameRef(),
                                 static_cast<int64_t>(poSrcFeature->GetFID()));
                        return false;
                    }
                    poDstFeature->SetField(iDstField, pnArray[iDstFeature]);
                }
                else if (eSrcType == OFTRealList)
                {
                    const double *padfArray =
                        poSrcFeature->GetFieldAsDoubleList(iSrcArrayField,
                                                           &nArrayLength);
                    if (iDstFeature >= nArrayLength)
                    {
                        CPLError(CE_Failure, CPLE_AppDefined,
                                 "Field '%s' of source feature %" PRId64
                                 " does not have enough elements.",
                                 poSrcFieldDefn->GetNameRef(),
                                 static_cast<int64_t>(poSrcFeature->GetFID()));
                        return false;
                    }
                    poDstFeature->SetField(iDstField, padfArray[iDstFeature]);
                }
                else if (eSrcType == OFTStringList)
                {
                    char **papszArray =
                        poSrcFeature->GetFieldAsStringList(iSrcArrayField);
                    nArrayLength = CSLCount(papszArray);
                    if (iDstFeature >= nArrayLength)
                    {
                        CPLError(CE_Failure, CPLE_AppDefined,
                                 "Field '%s' of source feature %" PRId64
                                 " does not have enough elements.",
                                 poSrcFieldDefn->GetNameRef(),
                                 static_cast<int64_t>(poSrcFeature->GetFID()));
                        return false;
                    }
                    poDstFeature->SetField(iDstField, papszArray[iDstFeature]);
                }
                nDstFeatures = std::max(nDstFeatures, nArrayLength);
            }

            poDstFeature->SetFID(m_nextFID++);
            apoOutFeatures.push_back(std::move(poDstFeature));
        }

        return true;
    }

  protected:
    OGRErr IGetExtent(int iGeomField, OGREnvelope *psExtent,
                      bool bForce) override
    {
        return m_srcLayer.GetExtent(iGeomField, psExtent, bForce);
    }

    OGRErr IGetExtent3D(int iGeomField, OGREnvelope3D *psExtent3D,
                        bool bForce) override
    {
        return m_srcLayer.GetExtent3D(iGeomField, psExtent3D, bForce);
    }

  private:
    std::vector<int> m_scalarFieldSrcToDstMap{};
    std::vector<int> m_listFieldSrcToDstMap{};
    bool m_addIndexField = false;
    OGRFeatureDefnRefCountedPtr m_poFeatureDefn;
    GIntBig m_nextFID{1};

    CPL_DISALLOW_COPY_ASSIGN(GDALVectorUnnestLayer)
};

}  // namespace

/************************************************************************/
/*                 GDALVectorUnnestAlgorithm::RunStep()                 */
/************************************************************************/

bool GDALVectorUnnestAlgorithm::RunStep(GDALPipelineStepRunContext &)
{
    auto poSrcDS = m_inputDataset[0].GetDatasetRef();
    CPLAssert(poSrcDS);

    auto poOutDS = std::make_unique<GDALVectorPipelineOutputDataset>(*poSrcDS);

    const int nLayerCount = poSrcDS->GetLayerCount();
    for (int i = 0; i < nLayerCount; ++i)
    {
        auto poLayer = poSrcDS->GetLayer(i);
        if (!poLayer)
            continue;

        auto poOutLayer = std::make_unique<GDALVectorUnnestLayer>(
            *poLayer, m_fields, m_indexFieldName, m_addIndexField);
        poOutDS->AddLayer(*poLayer, std::move(poOutLayer));
    }

    m_outputDataset.Set(std::move(poOutDS));
    return true;
}

//! @endcond
