/******************************************************************************
 *
 * Project:  GDAL Gridding API.
 * Purpose:  Implementation of GDAL scattered data gridder.
 * Author:   Even Rouault, <even dot rouault at spatialys.com>
 *
 ******************************************************************************
 * Copyright (c) 2013, Even Rouault <even dot rouault at spatialys.com>
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "gdalgrid.h"
#include "gdalgrid_priv.h"

#ifdef HAVE_SSE_AT_COMPILE_TIME

#ifdef USE_NEON_OPTIMIZATIONS
#include "include_sse2neon.h"
#else
#include <xmmintrin.h>
#endif

/************************************************************************/
/*         GDALGridInverseDistanceToAPower2NoSmoothingNoSearchSSE()     */
/************************************************************************/

CPLErr GDALGridInverseDistanceToAPower2NoSmoothingNoSearchSSE(
    const void *poOptions, GUInt32 nPoints,
    CPL_UNUSED const double *unused_padfX,
    CPL_UNUSED const double *unused_padfY,
    CPL_UNUSED const double *unused_padfZ, double dfXPoint, double dfYPoint,
    double *pdfValue, void *hExtraParamsIn)
{
    size_t i = 0;
    GDALGridExtraParameters *psExtraParams =
        static_cast<GDALGridExtraParameters *>(hExtraParamsIn);
    const float *pafX = psExtraParams->pafX;
    const float *pafY = psExtraParams->pafY;
    const float *pafZ = psExtraParams->pafZ;

    const float fEpsilon = 0.0000000000001f;
    const float fXPoint = static_cast<float>(dfXPoint);
    const float fYPoint = static_cast<float>(dfYPoint);
    const __m128 xmm_small = _mm_load1_ps(const_cast<float *>(&fEpsilon));
    const __m128 xmm_x = _mm_load1_ps(const_cast<float *>(&fXPoint));
    const __m128 xmm_y = _mm_load1_ps(const_cast<float *>(&fYPoint));
    __m128 xmm_nominator = _mm_setzero_ps();
    __m128 xmm_denominator = _mm_setzero_ps();
    int mask = 0;

#if defined(__x86_64) || defined(_M_X64) || defined(USE_NEON_OPTIMIZATIONS)
    // This would also work in 32bit mode, but there are only 8 XMM registers
    // whereas we have 16 for 64bit.
    const size_t LOOP_SIZE = 8;
    size_t nPointsRound = (nPoints / LOOP_SIZE) * LOOP_SIZE;
    for (i = 0; i < nPointsRound; i += LOOP_SIZE)
    {
        // rx = pafX[i] - fXPoint
        __m128 xmm_rx = _mm_sub_ps(_mm_load_ps(pafX + i), xmm_x);
        __m128 xmm_rx_4 = _mm_sub_ps(_mm_load_ps(pafX + i + 4), xmm_x);
        // ry = pafY[i] - fYPoint
        __m128 xmm_ry = _mm_sub_ps(_mm_load_ps(pafY + i), xmm_y);
        __m128 xmm_ry_4 = _mm_sub_ps(_mm_load_ps(pafY + i + 4), xmm_y);
        // r2 = rx * rx + ry * ry
        __m128 xmm_r2 =
            _mm_add_ps(_mm_mul_ps(xmm_rx, xmm_rx), _mm_mul_ps(xmm_ry, xmm_ry));
        __m128 xmm_r2_4 = _mm_add_ps(_mm_mul_ps(xmm_rx_4, xmm_rx_4),
                                     _mm_mul_ps(xmm_ry_4, xmm_ry_4));
        // invr2 = 1.0f / r2
        __m128 xmm_invr2 = _mm_rcp_ps(xmm_r2);
        __m128 xmm_invr2_4 = _mm_rcp_ps(xmm_r2_4);
        // nominator += invr2 * pafZ[i]
        xmm_nominator = _mm_add_ps(
            xmm_nominator, _mm_mul_ps(xmm_invr2, _mm_load_ps(pafZ + i)));
        xmm_nominator = _mm_add_ps(
            xmm_nominator, _mm_mul_ps(xmm_invr2_4, _mm_load_ps(pafZ + i + 4)));
        // denominator += invr2
        xmm_denominator = _mm_add_ps(xmm_denominator, xmm_invr2);
        xmm_denominator = _mm_add_ps(xmm_denominator, xmm_invr2_4);
        // if( r2 < fEpsilon)
        mask = _mm_movemask_ps(_mm_cmplt_ps(xmm_r2, xmm_small)) |
               (_mm_movemask_ps(_mm_cmplt_ps(xmm_r2_4, xmm_small)) << 4);
        if (mask)
            break;
    }
#else
#define LOOP_SIZE 4
    size_t nPointsRound = (nPoints / LOOP_SIZE) * LOOP_SIZE;
    for (i = 0; i < nPointsRound; i += LOOP_SIZE)
    {
        __m128 xmm_rx = _mm_sub_ps(_mm_load_ps(pafX + i),
                                   xmm_x); /* rx = pafX[i] - fXPoint */
        __m128 xmm_ry = _mm_sub_ps(_mm_load_ps(pafY + i),
                                   xmm_y); /* ry = pafY[i] - fYPoint */
        __m128 xmm_r2 =
            _mm_add_ps(_mm_mul_ps(xmm_rx, xmm_rx), /* r2 = rx * rx + ry * ry */
                       _mm_mul_ps(xmm_ry, xmm_ry));
        __m128 xmm_invr2 = _mm_rcp_ps(xmm_r2); /* invr2 = 1.0f / r2 */
        xmm_nominator =
            _mm_add_ps(xmm_nominator, /* nominator += invr2 * pafZ[i] */
                       _mm_mul_ps(xmm_invr2, _mm_load_ps(pafZ + i)));
        xmm_denominator =
            _mm_add_ps(xmm_denominator, xmm_invr2); /* denominator += invr2 */
        mask = _mm_movemask_ps(
            _mm_cmplt_ps(xmm_r2, xmm_small)); /* if( r2 < fEpsilon) */
        if (mask)
            break;
    }
#endif

    // Find which i triggered r2 < fEpsilon.
    if (mask)
    {
        for (size_t j = 0; j < LOOP_SIZE; j++)
        {
            if (mask & (1 << j))
            {
                (*pdfValue) = (pafZ)[i + j];
                return CE_None;
            }
        }
    }

    // Get back nominator and denominator values for XMM registers.
    float afNominator[4];
    float afDenominator[4];
    _mm_storeu_ps(afNominator, xmm_nominator);
    _mm_storeu_ps(afDenominator, xmm_denominator);

    float fNominator =
        afNominator[0] + afNominator[1] + afNominator[2] + afNominator[3];
    float fDenominator = afDenominator[0] + afDenominator[1] +
                         afDenominator[2] + afDenominator[3];

    /* Do the few remaining loop iterations */
    for (; i < nPoints; i++)
    {
        const float fRX = pafX[i] - fXPoint;
        const float fRY = pafY[i] - fYPoint;
        const float fR2 = fRX * fRX + fRY * fRY;

        // If the test point is close to the grid node, use the point
        // value directly as a node value to avoid singularity.
        if (fR2 < 0.0000000000001)
        {
            break;
        }
        else
        {
            const float fInvR2 = 1.0f / fR2;
            fNominator += fInvR2 * pafZ[i];
            fDenominator += fInvR2;
        }
    }

    if (i != nPoints)
    {
        (*pdfValue) = pafZ[i];
    }
    else if (fDenominator == 0.0)
    {
        (*pdfValue) =
            static_cast<const GDALGridInverseDistanceToAPowerOptions *>(
                poOptions)
                ->dfNoDataValue;
    }
    else
    {
        (*pdfValue) = fNominator / fDenominator;
    }

    return CE_None;
}

#endif /* HAVE_SSE_AT_COMPILE_TIME */
