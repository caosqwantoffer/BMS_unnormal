/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2018, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file     Rom.h
    \brief    global variables & functions (header)
*/

#ifndef __ROM__
#define __ROM__

#include "CommonDef.h"
#include "Common.h"

#include "BinaryDecisionTree.h"

#include <stdio.h>
#include <iostream>


//! \ingroup CommonLib
//! \{

// ====================================================================================================================
// Initialize / destroy functions
// ====================================================================================================================

void         initROM();
void         destroyROM();

void         generateTrafoBlockSizeScaling( SizeIndexInfo& sizeIdxInfo );

// ====================================================================================================================
// Data structure related table & variable
// ====================================================================================================================

// flexible conversion from relative to absolute index
extern       uint32_t*  g_scanOrder     [SCAN_NUMBER_OF_GROUP_TYPES][SCAN_NUMBER_OF_TYPES][MAX_CU_SIZE / 2 + 1][MAX_CU_SIZE / 2 + 1];
extern       uint32_t*  g_scanOrderPosXY[SCAN_NUMBER_OF_GROUP_TYPES][SCAN_NUMBER_OF_TYPES][MAX_CU_SIZE / 2 + 1][MAX_CU_SIZE / 2 + 1][2];
#if JEM_TOOLS && !ENABLE_BMS
extern       uint32_t   g_auiCoefTopLeftDiagScan8x8[ MAX_CU_SIZE / 2 + 1 ][64];

#endif

extern const int g_quantScales   [SCALING_LIST_REM_NUM];          // Q(QP%6)
extern const int g_invQuantScales[SCALING_LIST_REM_NUM];          // IQ(QP%6)

#if JVET_K1000_SIMPLIFIED_EMT
static const int g_numTransformMatrixSizes = 6;
static const int g_transformMatrixShift[TRANSFORM_NUMBER_OF_DIRECTIONS] = {  6, 6 };
#else
static const int g_numTransformMatrixSizes = 7;
#if RExt__HIGH_PRECISION_FORWARD_TRANSFORM
static const int g_transformMatrixShift[TRANSFORM_NUMBER_OF_DIRECTIONS] = { 14, 6 };
#else
static const int g_transformMatrixShift[TRANSFORM_NUMBER_OF_DIRECTIONS] = {  6, 6 };
#endif

extern const TMatrixCoeff g_aiT2  [TRANSFORM_NUMBER_OF_DIRECTIONS][  2][  2];
extern const TMatrixCoeff g_aiT4  [TRANSFORM_NUMBER_OF_DIRECTIONS][  4][  4];
extern const TMatrixCoeff g_aiT8  [TRANSFORM_NUMBER_OF_DIRECTIONS][  8][  8];
extern const TMatrixCoeff g_aiT16 [TRANSFORM_NUMBER_OF_DIRECTIONS][ 16][ 16];
extern const TMatrixCoeff g_aiT32 [TRANSFORM_NUMBER_OF_DIRECTIONS][ 32][ 32];
extern const TMatrixCoeff g_aiT64 [TRANSFORM_NUMBER_OF_DIRECTIONS][ 64][ 64];
extern const TMatrixCoeff g_aiT128[TRANSFORM_NUMBER_OF_DIRECTIONS][128][128];
#endif


// ====================================================================================================================
// Luma QP to Chroma QP mapping
// ====================================================================================================================
#if JVET_K0251_QP_EXT
static const int chromaQPMappingTableSize = (MAX_QP + 7);
#else
static const int chromaQPMappingTableSize = 58;
#endif

extern const uint8_t  g_aucChromaScale[NUM_CHROMA_FORMAT][chromaQPMappingTableSize];


// ====================================================================================================================
// Scanning order & context mapping table
// ====================================================================================================================

extern const uint32_t   ctxIndMap4x4[4*4];

extern const uint32_t   g_uiGroupIdx[ MAX_TU_SIZE ];
extern const uint32_t   g_uiMinInGroup[ LAST_SIGNIFICANT_GROUPS ];
#if JVET_K0072
extern const uint32_t   g_auiGoRicePars [ 32 ];
#endif
extern const uint32_t   g_auiGoRiceRange[ MAX_GR_ORDER_RESIDUAL ];                  //!< maximum value coded with Rice codes

// ====================================================================================================================
// Intra prediction table
// ====================================================================================================================

extern const uint8_t  g_aucIntraModeNumFast_UseMPM_2D[7 - MIN_CU_LOG2 + 1][7 - MIN_CU_LOG2 + 1];
extern const uint8_t  g_aucIntraModeNumFast_UseMPM   [MAX_CU_DEPTH];
extern const uint8_t  g_aucIntraModeNumFast_NotUseMPM[MAX_CU_DEPTH];

extern const uint8_t  g_chroma422IntraAngleMappingTable[NUM_INTRA_MODE];
#if !INTRA67_3MPM
extern const uint8_t  g_intraMode65to33AngMapping[NUM_INTRA_MODE];

extern const uint8_t  g_intraMode33to65AngMapping[36];

static const unsigned mpmCtx[NUM_INTRA_MODE] =
{ 1, 1,                                                                                              // PLANAR, DC
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, // HOR domain
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3     // VER domain
};
#endif

#if JEM_TOOLS
extern const int g_intraCubicFilter[32][4];
extern const int g_intraGaussFilter[32][4];

extern const int g_pdpc_pred_param[5][35][6];
extern const int g_pdpcParam[5][6];

#endif

// ====================================================================================================================
// Mode-Dependent DST Matrices
// ====================================================================================================================

#if HEVC_USE_4x4_DSTVII
extern const TMatrixCoeff g_as_DST_MAT_4 [TRANSFORM_NUMBER_OF_DIRECTIONS][4][4];
#endif

#if JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT
extern const int g_aiTrSubsetIntra[3][2];
extern const int g_aiTrSubsetInter[4];

extern const uint8_t g_aucTrSetVert[NUM_INTRA_MODE - 1];
extern const uint8_t g_aucTrSetHorz[NUM_INTRA_MODE - 1];

extern const uint8_t g_aucTrSetVert35[35];
extern const uint8_t g_aucTrSetHorz35[35];

extern const uint32_t g_EmtSigNumThr;
#endif

extern TMatrixCoeff g_aiTr2   [NUM_TRANS_TYPE][  2][  2];
extern TMatrixCoeff g_aiTr4   [NUM_TRANS_TYPE][  4][  4];
extern TMatrixCoeff g_aiTr8   [NUM_TRANS_TYPE][  8][  8];
extern TMatrixCoeff g_aiTr16  [NUM_TRANS_TYPE][ 16][ 16];
extern TMatrixCoeff g_aiTr32  [NUM_TRANS_TYPE][ 32][ 32];
extern TMatrixCoeff g_aiTr64  [NUM_TRANS_TYPE][ 64][ 64];
#if !JVET_K1000_SIMPLIFIED_EMT
extern TMatrixCoeff g_aiTr128 [NUM_TRANS_TYPE][128][128];
#endif

#if JEM_TOOLS
extern const uint8_t  g_NsstLut           [NUM_INTRA_MODE-1];
struct tabSinCos { int c, s; };
extern tabSinCos    g_tabSinCos         [NSST_HYGT_PTS];
extern const uint8_t  g_nsstHyGTPermut4x4 [35][3][16];
extern const int    g_nsstHyGTPar4x4    [35][3][64];
#if !ENABLE_BMS
extern const uint8_t  g_nsstHyGTPermut8x8 [35][3][64];
extern const int    g_nsstHyGTPar8x8    [35][3][768];
#endif
#endif

// ====================================================================================================================
// Decision tree templates
// ====================================================================================================================

enum SplitDecisionTree
{
  DTT_SPLIT_DO_SPLIT_DECISION = 0, // decision node
  DTT_SPLIT_NO_SPLIT          = 1, // end-node
  DTT_SPLIT_BT_HORZ           = 2, // end-node - id same as CU_HORZ_SPLIT
  DTT_SPLIT_BT_VERT           = 3, // end-node - id same as CU_VERT_SPLIT
  DTT_SPLIT_TT_HORZ           = 4, // end-node - id same as CU_TRIH_SPLIT
  DTT_SPLIT_TT_VERT           = 5, // end-node - id same as CU_TRIV_SPLIT
  DTT_SPLIT_HV_DECISION,           // decision node
  DTT_SPLIT_H_IS_BT_12_DECISION,   // decision node
  DTT_SPLIT_V_IS_BT_12_DECISION,   // decision node
};

// decision tree for multi-type tree split decision
extern const DecisionTreeTemplate g_mtSplitDTT;

// decision tree for QTBT split
extern const DecisionTreeTemplate g_qtbtSplitDTT;

#if JEM_TOOLS
enum IntraLumaMpmDecisionTree
{
  DTT_INTRA_MPM_0 = 0,
  DTT_INTRA_MPM_1,
  DTT_INTRA_MPM_2,
  DTT_INTRA_MPM_3,
  DTT_INTRA_MPM_4,
  DTT_INTRA_MPM_5,
  DTT_INTRA_MPM_ISGT_0,
  DTT_INTRA_MPM_ISGT_1,
  DTT_INTRA_MPM_ISGT_2,
  DTT_INTRA_MPM_ISGT_3,
  DTT_INTRA_MPM_ISGT_4,
};

extern const DecisionTreeTemplate g_intraLumaMpmDTT;

#endif

// ====================================================================================================================
// Misc.
// ====================================================================================================================
extern SizeIndexInfo* gp_sizeIdxInfo;
extern int            g_BlockSizeTrafoScale           [MAX_CU_SIZE + 1][MAX_CU_SIZE + 1][2];
extern int8_t          g_aucLog2                       [MAX_CU_SIZE + 1];
extern int8_t          g_aucNextLog2        [MAX_CU_SIZE + 1];
extern int8_t          g_aucPrevLog2        [MAX_CU_SIZE + 1];
extern const int8_t    i2Log2Tab[257];

inline bool is34( const SizeType& size )
{
  return ( size & ( ( int64_t ) 1 << ( g_aucLog2[size] - 1 ) ) );
}

inline bool is58( const SizeType& size )
{
  return ( size & ( ( int64_t ) 1 << ( g_aucLog2[size] - 2 ) ) );
}

inline bool isNonLog2BlockSize( const Size& size )
{
  return ( ( 1 << g_aucLog2[size.width] ) != size.width ) || ( ( 1 << g_aucLog2[size.height] ) != size.height );
}

inline bool isNonLog2Size( const SizeType& size )
{
  return ( ( 1 << g_aucLog2[size] ) != size );
}

extern UnitScale     g_miScaling; // scaling object for motion scaling

/*! Sophisticated Trace-logging */
#if ENABLE_TRACING
#include "dtrace.h"
extern CDTrace* g_trace_ctx;
#endif

const char* nalUnitTypeToString(NalUnitType type);

#if HEVC_USE_SCALING_LISTS
extern const char *MatrixType   [SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM];
extern const char *MatrixType_DC[SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM];

extern const int g_quantTSDefault4x4   [4*4];
extern const int g_quantIntraDefault8x8[8*8];
extern const int g_quantInterDefault8x8[8*8];

extern const uint32_t g_scalingListSize [SCALING_LIST_SIZE_NUM];
extern const uint32_t g_scalingListSizeX[SCALING_LIST_SIZE_NUM];
#endif

extern MsgLevel g_verbosity;


#if JEM_TOOLS

extern int g_aiLMDivTableLow[];
extern int g_aiLMDivTableHigh[];

extern const int g_aiMFLM_MinSize[];
extern const int g_aiMMLM_MinSize[];
#endif
#if JEM_TOOLS||JVET_K0190
extern const int g_aiNonLMPosThrs[];
#endif
#if JEM_TOOLS
extern const uint8_t g_NonMPM[257];
#if !INTRA67_3MPM
#if JVET_B0051_NON_MPM_MODE
extern const int g_ipred_mode_table[];
#endif
#endif
#endif
#if JVET_K0248_GBI
extern const int8_t g_GbiLog2WeightBase;
extern const int8_t g_GbiWeightBase;
extern const int8_t g_GbiWeights[GBI_NUM];
extern const int8_t g_GbiSearchOrder[GBI_NUM];
extern       int8_t g_GbiCodingOrder[GBI_NUM];
extern       int8_t g_GbiParsingOrder[GBI_NUM];
int8_t getGbiWeight(uint8_t gbiIdx, uint8_t uhRefFrmList);

class CodingStructure;
void resetGbiCodingOrder(bool bRunDecoding, const CodingStructure &cs);
uint32_t deriveWeightIdxBits(uint8_t gbiIdx);

template<int32_t n> int32_t integerScalor(Pel p);
extern int32_t(*g_apIntMultiplier[])(Pel);

#define GET_INT_MULTIPLIER(w)  (*( g_apIntMultiplier - (-2) + (w) ))   // Make sure -2 is the minimal weight values defined in g_apIntMultiplier
#endif 

#if JVET_K0371_ALF
constexpr uint8_t g_tbMax[257] = { 0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8 };
#endif

//! \}

#endif  //__TCOMROM__

