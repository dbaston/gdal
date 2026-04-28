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
#include <numeric>
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
        .SetDefault(m_indexFieldName);
}

GDALVectorUnnestAlgorithmStandalone::~GDALVectorUnnestAlgorithmStandalone() =
    default;

namespace
{

class GDALVectorUnnestLayer final : public GDALVectorPipelineOutputLayer
{
  public:
    GDALVectorUnnestLayer(OGRLayer &srcLayer,
                          const std::vector<std::string> &fieldsToUnnest,
                          const std::string &indexFieldName)
        : GDALVectorPipelineOutputLayer(srcLayer),
          m_fieldsToUnnest(fieldsToUnnest), m_indexFieldName(indexFieldName)
    {
        if (!PrepareFeatureDefn())
        {
            m_setupError = true;
        }
    }

    bool PrepareFeatureDefn()
    {
        m_poFeatureDefn.reset(
            OGRFeatureDefn::CreateFeatureDefn(m_srcLayer.GetName()));

        // Avoid creating geometry field with null SRS
        // We'll copy it in later from the source layer
        m_poFeatureDefn->DeleteGeomFieldDefn(0);

        const bool addIndexField = !m_indexFieldName.empty();

        if (addIndexField)
        {
            auto poIdxField = std::make_unique<OGRFieldDefn>(
                m_indexFieldName.c_str(), OFTInteger);
            m_poFeatureDefn->AddFieldDefn(std::move(poIdxField));
        }

        const OGRFeatureDefn *poSrcDefn = m_srcLayer.GetLayerDefn();

        // By default, all fields copied as-is.
        m_unnestedFieldSrcToDstMap.resize(poSrcDefn->GetFieldCount(), -1);
        m_passThroughFieldSrcToDstMap.resize(poSrcDefn->GetFieldCount());
        std::iota(m_passThroughFieldSrcToDstMap.begin(),
                  m_passThroughFieldSrcToDstMap.end(), addIndexField ? 1 : 0);

        m_geomFieldUnnested.resize(poSrcDefn->GetGeomFieldCount(), false);

        for (const auto &fieldName : m_fieldsToUnnest)
        {
            const int iSrcField = poSrcDefn->GetFieldIndex(fieldName.c_str());
            if (iSrcField < 0)
            {
                // Is it a geometry field?
                int iSrcGeomField =
                    poSrcDefn->GetGeomFieldIndex(fieldName.c_str());
                if (iSrcGeomField < 0)
                {
                    if (poSrcDefn->GetGeomFieldCount() > 0 &&
                        EQUAL(poSrcDefn->GetGeomFieldDefn(0)->GetNameRef(),
                              "") &&
                        EQUAL(fieldName.c_str(), "_OGR_GEOMETRY_"))
                    {
                        iSrcGeomField = 0;
                    }
                    else
                    {
                        CPLError(CE_Failure, CPLE_AppDefined,
                                 "Field '%s' not found in source layer.",
                                 fieldName.c_str());
                        return false;
                    }
                }

                m_geomFieldUnnested[iSrcGeomField] = true;
            }
            else
            {
                const OGRFieldDefn *poSrcFieldDefn =
                    poSrcDefn->GetFieldDefn(iSrcField);
                const auto eSrcType = poSrcFieldDefn->GetType();
                if (OGR_GetFieldTypeIsList(eSrcType))
                {
                    m_passThroughFieldSrcToDstMap[iSrcField] = -1;
                    m_unnestedFieldSrcToDstMap[iSrcField] =
                        iSrcField + addIndexField;
                }
                else
                {
                    CPLError(CE_Warning, CPLE_AppDefined,
                             "Field '%s' is not a list type.",
                             poSrcFieldDefn->GetNameRef());
                }
            }
        }

        // Create attribute fields
        for (int iSrcField = 0; iSrcField < poSrcDefn->GetFieldCount();
             iSrcField++)
        {
            const auto *poSrcFieldDefn = poSrcDefn->GetFieldDefn(iSrcField);
            std::unique_ptr<OGRFieldDefn> poDstFieldDefn;

            if (m_passThroughFieldSrcToDstMap[iSrcField] != -1)
            {
                poDstFieldDefn =
                    std::make_unique<OGRFieldDefn>(*poSrcFieldDefn);
            }
            else
            {
                const auto eScalarType =
                    OGR_GetFieldTypeAsScalar(poSrcFieldDefn->GetType());
                poDstFieldDefn = std::make_unique<OGRFieldDefn>(
                    poSrcFieldDefn->GetNameRef(), eScalarType);
            }

            m_poFeatureDefn->AddFieldDefn(std::move(poDstFieldDefn));
        }

        // Create geometry fields
        for (int iSrcGeomField = 0;
             iSrcGeomField < poSrcDefn->GetGeomFieldCount(); iSrcGeomField++)
        {
            const OGRGeomFieldDefn *poSrcGeomFieldDefn =
                poSrcDefn->GetGeomFieldDefn(iSrcGeomField);
            std::unique_ptr<OGRGeomFieldDefn> poDstGeomFieldDefn;

            if (m_geomFieldUnnested[iSrcGeomField])
            {
                const auto eDstType =
                    OGR_GT_GetSingle(poSrcGeomFieldDefn->GetType());
                poDstGeomFieldDefn = std::make_unique<OGRGeomFieldDefn>(
                    poSrcGeomFieldDefn->GetNameRef(), eDstType);
                poDstGeomFieldDefn->SetSpatialRef(
                    poSrcGeomFieldDefn->GetSpatialRef());
            }
            else
            {
                poDstGeomFieldDefn =
                    std::make_unique<OGRGeomFieldDefn>(*poSrcGeomFieldDefn);
            }

            m_poFeatureDefn->AddGeomFieldDefn(std::move(poDstGeomFieldDefn));
        }

        return true;
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
        if (m_setupError)
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "Failed to prepare output layer.");
            return false;
        }

        int nDstFeatures = 1;

        for (int iDstFeature = 0; iDstFeature < nDstFeatures; iDstFeature++)
        {
            auto poDstFeature =
                std::make_unique<OGRFeature>(m_poFeatureDefn.get());
            if (!m_indexFieldName.empty())
            {
                poDstFeature->SetField(0, iDstFeature);
            }

            if (poDstFeature->SetFieldsFrom(
                    poSrcFeature.get(), m_passThroughFieldSrcToDstMap.data(),
                    true) != OGRERR_NONE)
            {
                CPLError(CE_Failure, CPLE_AppDefined,
                         "Failed to set fields of output feature");
                return false;
            }

            for (int iSrcArrayField = 0;
                 iSrcArrayField <
                 static_cast<int>(m_unnestedFieldSrcToDstMap.size());
                 iSrcArrayField++)
            {
                int iDstField = m_unnestedFieldSrcToDstMap[iSrcArrayField];
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

            for (int iGeomField = 0;
                 iGeomField < poSrcFeature->GetGeomFieldCount(); iGeomField++)
            {
                // FIXME remove clone
                std::unique_ptr<OGRGeometry> poSrcGeom(
                    poSrcFeature->GetGeomFieldRef(iGeomField)->clone());

                if (m_geomFieldUnnested[iGeomField])
                {
                    OGRGeometryCollection *poColl =
                        poSrcGeom->toGeometryCollection();

                    auto nGeoms = poColl->getNumGeometries();

                    if (nGeoms <= iDstFeature)
                    {
                        CPLError(
                            CE_Failure, CPLE_AppDefined,
                            "Geometry field '%s' of source feature %" PRId64
                            " has %d elements (expected %d)",
                            poSrcFeature->GetDefnRef()
                                ->GetGeomFieldDefn(iGeomField)
                                ->GetNameRef(),
                            static_cast<int64_t>(poSrcFeature->GetFID()),
                            nGeoms, nDstFeatures);
                        return false;
                    }
                    nDstFeatures = std::max(nDstFeatures, nGeoms);

                    std::unique_ptr<OGRGeometry> poDstGeom =
                        poColl->stealGeometry(iDstFeature);
                    poDstFeature->SetGeomField(iGeomField,
                                               std::move(poDstGeom));
                }
                else
                {
                    poDstFeature->SetGeomField(iGeomField,
                                               std::move(poSrcGeom));
                }
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
    std::vector<int> m_passThroughFieldSrcToDstMap{};
    std::vector<int> m_unnestedFieldSrcToDstMap{};
    std::vector<bool> m_geomFieldUnnested{};
    std::vector<std::string> m_fieldsToUnnest{};
    std::string m_indexFieldName{};
    bool m_setupError{false};
    OGRFeatureDefnRefCountedPtr m_poFeatureDefn{nullptr};
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
            *poLayer, m_fields, m_indexFieldName);
        poOutDS->AddLayer(*poLayer, std::move(poOutLayer));
    }

    m_outputDataset.Set(std::move(poOutDS));
    return true;
}

//! @endcond
