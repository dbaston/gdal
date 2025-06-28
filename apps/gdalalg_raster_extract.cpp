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

#include <optional>

//! @cond Doxygen_Suppress

#ifndef _
#define _(x) (x)
#endif

GDALRasterExtractAlgorithm::GDALRasterExtractAlgorithm(bool standaloneStep)
    : GDALPipelineStepAlgorithm(
          NAME, DESCRIPTION, HELP_URL,
          ConstructorOptions()
              .SetStandaloneStep(standaloneStep)
              .SetOutputFormatCreateCapability(GDAL_DCAP_CREATE)),
      m_skipNoData(false)
{
    if (standaloneStep)
    {
        AddOutputFormatArg(&m_format).AddMetadataItem(
            GAAMDI_REQUIRED_CAPABILITIES, {GDAL_DCAP_VECTOR, GDAL_DCAP_CREATE});

        AddInputDatasetArg(&m_inputDataset, GDAL_OF_RASTER);
        AddOutputDatasetArg(&m_outputDataset, GDAL_OF_VECTOR)
            .SetDatasetInputFlags(GADV_NAME | GADV_OBJECT);
        AddOverwriteArg(&m_overwrite);
    }

    AddArg("geometry-type", 0, _("Geometry type"), &m_geomTypeName)
        .SetChoices("none", "point", "polygon")
        .SetDefault("none");
    AddArg("skip-nodata", 0, _("Omit NoData pixels from the result"),
           &m_skipNoData);
    AddArg("include-xy", 0, _("Include fields for cell center coordinates"),
           &m_includeXY);
    AddArg("include-row-col", 0, _("Include columns for row and column"),
           &m_includeRowCol);
}

GDALRasterExtractAlgorithm::~GDALRasterExtractAlgorithm() = default;

bool GDALRasterExtractAlgorithm::RunImpl(GDALProgressFunc pfnProgress,
                                         void *pProgressData)
{
    GDALPipelineStepRunContext stepCtxt;
    stepCtxt.m_pfnProgress = pfnProgress;
    stepCtxt.m_pProgressData = pProgressData;
    return RunPreStepPipelineValidations() && RunStep(stepCtxt);
}

GDALRasterExtractAlgorithmStandalone::~GDALRasterExtractAlgorithmStandalone() =
    default;

//struct Window
//{
//    int nXOff;
//    int nYOff;
//    int nX;
//    int nY;
//};
//
//class GDALRasterIterator
//{
//
//};

struct RasterExtractOptions
{
    OGRwkbGeometryType geomType;
    bool includeXY;
    bool includeRowCol;
    bool skipNoData;
};

class GDALRasterExtractLayer : public OGRLayer
{
  public:
    static constexpr const char *ROW_FIELD = "ROW";
    static constexpr const char *COL_FIELD = "COL";
    static constexpr const char *X_FIELD = "CENTER_X";
    static constexpr const char *Y_FIELD = "CENTER_Y";

    GDALRasterExtractLayer(GDALDataset &ds, RasterExtractOptions options)
        : m_ds(ds), m_includeRowCol(options.includeRowCol),
          m_includeXY(options.includeXY),
          m_excludeNoDataPixels(options.skipNoData)
    {
        m_ds.GetRasterBand(1)->GetBlockSize(&m_chunkSizeX, &m_chunkSizeY);

        //auto eSrcDT = m_ds.GetRasterBand(1)->GetRasterDataType();
        //m_bufType = m_ds.GetRasterBand(1)->GetRasterDataType();
        m_bufType = GDT_Float64;
        m_ds.GetGeoTransform(m_gt);

        {
            int hasNoData;
            double noData = m_ds.GetRasterBand(1)->GetNoDataValue(&hasNoData);
            if (hasNoData)
            {
                m_noData = noData;
            }
        }

        m_defn = new OGRFeatureDefn("layer_name_fixme");
        m_defn->GetGeomFieldDefn(0)->SetType(options.geomType);
        m_defn->Reference();
        if (m_includeXY)
        {
            auto xField = std::make_unique<OGRFieldDefn>(X_FIELD, OFTReal);
            auto yField = std::make_unique<OGRFieldDefn>(Y_FIELD, OFTReal);
            m_defn->AddFieldDefn(std::move(xField));
            m_defn->AddFieldDefn(std::move(yField));
        }
        if (m_includeRowCol)
        {
            auto rowField =
                std::make_unique<OGRFieldDefn>(ROW_FIELD, OFTInteger);
            auto colField =
                std::make_unique<OGRFieldDefn>(COL_FIELD, OFTInteger);
            m_defn->AddFieldDefn(std::move(rowField));
            m_defn->AddFieldDefn(std::move(colField));
        }
        {
            auto bandField = std::make_unique<OGRFieldDefn>("BAND_1", OFTReal);
            m_defn->AddFieldDefn(std::move(bandField));
        }

        NextWindow();
    }

    ~GDALRasterExtractLayer()
    {
        m_defn->Dereference();
    }

    void ResetReading() override
    {
    }

    int TestCapability(const char *) override
    {
        // FIXME implement
        return 0;
    }

    OGRFeatureDefn *GetLayerDefn() override
    {
        return m_defn;
    }

    OGRFeature *GetNextFeature() override
    {
        std::unique_ptr<OGRFeature> feature;

        while (m_row < m_chunkSizeY)
        {
            const double *pSrcVal = static_cast<double *>(
                m_buf + (m_row * m_chunkSizeX + m_col) *
                            GDALGetDataTypeSizeBytes(m_bufType));

            const bool emitFeature =
                !m_excludeNoDataPixels || !IsNoData(*pSrcVal);

            if (emitFeature)
            {
                feature.reset(OGRFeature::CreateFeature(m_defn));

                feature->SetField("BAND_1", *pSrcVal);

                const size_t line = m_yChunk * m_chunkSizeY + m_row;
                const size_t pixel = m_xChunk * m_chunkSizeX + m_col;

                if (m_includeRowCol)
                {
                    feature->SetField(ROW_FIELD, static_cast<GIntBig>(line));
                    feature->SetField(COL_FIELD, static_cast<GIntBig>(pixel));
                }
                if (m_includeXY)
                {
                    double x, y;
                    m_gt.Apply(static_cast<double>(pixel) + 0.5,
                               static_cast<double>(line) + 0.5, &x, &y);
                    feature->SetField(X_FIELD, x);
                    feature->SetField(Y_FIELD, y);
                }

                std::unique_ptr<OGRGeometry> geom;
                const auto geomType = m_defn->GetGeomFieldDefn(0)->GetType();
                if (geomType == wkbPoint)
                {
                    double x, y;
                    m_gt.Apply(static_cast<double>(pixel) + 0.5,
                               static_cast<double>(line) + 0.5, &x, &y);

                    geom = std::make_unique<OGRPoint>(x, y);
                }
                else if (geomType == wkbPolygon)
                {
                    double x, y;

                    auto lr = std::make_unique<OGRLinearRing>();

                    m_gt.Apply(pixel, line, &x, &y);
                    lr->addPoint(x, y);
                    m_gt.Apply(pixel, line + 1, &x, &y);
                    lr->addPoint(x, y);
                    m_gt.Apply(pixel + 1, line + 1, &x, &y);
                    lr->addPoint(x, y);
                    m_gt.Apply(pixel + 1, line, &x, &y);
                    lr->addPoint(x, y);
                    m_gt.Apply(pixel, line, &x, &y);
                    lr->addPoint(x, y);

                    auto poly = std::make_unique<OGRPolygon>();
                    poly->addRing(std::move(lr));
                    geom = std::move(poly);
                }

                feature->SetGeometry(std::move(geom));
            }
            else
            {
                CPLDebug("Skipped", "");
            }

            m_col += 1;
            if (m_col >= m_chunkSizeX)
            {
                m_col = 0;
                m_row++;
            }

            if (feature)
            {
                return feature.release();
            }
        }

        return nullptr;
    }

  private:
    bool IsNoData(double x) const
    {
        if (!m_noData.has_value())
        {
            return false;
        }

        return m_noData.value() == x ||
               (std::isnan(m_noData.value()) && std::isnan(x));
    }

    void NextWindow()
    {
        int nXOff = 0;
        int nYOff = 0;
        int nBandCount = 1;

        m_buf = VSI_MALLOC3_VERBOSE(m_chunkSizeX, m_chunkSizeY,
                                    GDALGetDataTypeSizeBytes(m_bufType));

        auto eErr =
            m_ds.RasterIO(GF_Read, nXOff, nYOff, m_chunkSizeX, m_chunkSizeY,
                          m_buf, m_chunkSizeX, m_chunkSizeY, m_bufType,
                          nBandCount, nullptr, 0, 0, 0, nullptr);
        if (eErr != CE_None)
        {
            // FIXME handle;
        }

        m_row = 0;
        m_col = 0;
    }

    void *m_buf;
    GDALDataType m_bufType;
    GDALDataset &m_ds;
    GDALGeoTransform m_gt;
    std::optional<double> m_noData{std::nullopt};
    int m_chunkSizeX;
    int m_chunkSizeY;

    size_t m_xChunk{0};
    size_t m_yChunk{0};

    size_t m_row{0};
    size_t m_col{0};

    OGRFeatureDefn *m_defn;
    bool m_excludeNoDataPixels;
    bool m_includeXY;
    bool m_includeRowCol;
};

bool GDALRasterExtractAlgorithm::RunStep(GDALPipelineStepRunContext &ctxt)
{
    auto poSrcDS = m_inputDataset[0].GetDatasetRef();
    CPLAssert(poSrcDS);

    GDALDataset *poDstDS = m_outputDataset.GetDatasetRef();
    std::string outputFilename = m_outputDataset.GetName();

    std::unique_ptr<GDALDataset> poRetDS;
    if (!poDstDS)
    {
        if (m_standaloneStep && m_format.empty())
        {
            const auto aosFormats =
                CPLStringList(GDALGetOutputDriversForDatasetName(
                    m_outputDataset.GetName().c_str(), GDAL_OF_VECTOR,
                    /* bSingleMatch = */ true,
                    /* bWarn = */ true));
            if (aosFormats.size() != 1)
            {
                ReportError(CE_Failure, CPLE_AppDefined,
                            "Cannot guess driver for %s",
                            m_outputDataset.GetName().c_str());
                return false;
            }
            m_format = aosFormats[0];
        }
        else
        {
            m_format = "MEM";
        }

        auto poDriver =
            GetGDALDriverManager()->GetDriverByName(m_format.c_str());

        poRetDS.reset(
            poDriver->Create(outputFilename.c_str(), 0, 0, 0, GDT_Unknown,
                             CPLStringList(m_creationOptions).List()));
        if (!poRetDS)
            return false;

        poDstDS = poRetDS.get();
    }

    RasterExtractOptions options;
    options.geomType = m_geomTypeName == "point"     ? wkbPoint
                       : m_geomTypeName == "polygon" ? wkbPolygon
                                                     : wkbNone;
    options.includeRowCol = m_includeRowCol;
    options.includeXY = m_includeXY;
    options.skipNoData = m_skipNoData;

    GDALRasterExtractLayer layer(*poSrcDS, options);

    poDstDS->CopyLayer(&layer, "extract");

    if (poRetDS)
    {
        m_outputDataset.Set(std::move(poRetDS));
    }

    return true;
}

//! @endcond
