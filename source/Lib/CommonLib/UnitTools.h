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

/** \file     UnitTool.h
 *  \brief    defines operations for basic units
 */

#ifndef __UNITTOOLS__
#define __UNITTOOLS__

#include "Unit.h"
#include "UnitPartitioner.h"
#include "ContextModelling.h"
#include "InterPrediction.h"

// CS tools
namespace CS
{
  uint64_t getEstBits                   ( const CodingStructure &cs );
#if JEM_TOOLS
  void   initFrucMvp                  (       CodingStructure &cs );
#endif
  UnitArea getArea                    ( const CodingStructure &cs, const UnitArea &area, const ChannelType chType );
  bool   isDualITree                  ( const CodingStructure &cs );
#if DMVR_JVET_LOW_LATENCY_K0217
  void   setRefinedMotionField        ( CodingStructure &cs );
#endif
}

#if JEM_TOOLS
struct CIPFSpec
{
  bool  loadCtx;
  bool  storeCtx;
  int   ctxId;
};

CIPFSpec getCIPFSpec( const Slice* slice, const int ctuXPosInCtus, const int ctuYPosInCtus );
#endif

// CU tools
namespace CU
{
  bool isIntra                        (const CodingUnit &cu);
  bool isInter                        (const CodingUnit &cu);
  bool isRDPCMEnabled                 (const CodingUnit &cu);
  bool isLosslessCoded                (const CodingUnit &cu);
  uint32_t getIntraSizeIdx                (const CodingUnit &cu);

  bool isSameCtu                      (const CodingUnit &cu, const CodingUnit &cu2);
  bool isSameSlice                    (const CodingUnit &cu, const CodingUnit &cu2);
#if HEVC_TILES_WPP
  bool isSameTile                     (const CodingUnit &cu, const CodingUnit &cu2);
  bool isSameSliceAndTile             (const CodingUnit &cu, const CodingUnit &cu2);
#endif
  bool isLastSubCUOfCtu               (const CodingUnit &cu);
  uint32_t getCtuAddr                     (const CodingUnit &cu);

  int  predictQP                      (const CodingUnit& cu, const int prevQP );
  bool isQGStart                      (const CodingUnit& cu); // check if start of a Quantization Group

  uint32_t getNumPUs                      (const CodingUnit& cu);
  void addPUs                         (      CodingUnit& cu);

  PartSplit getSplitAtDepth           (const CodingUnit& cu, const unsigned depth);

  bool hasNonTsCodedBlock             (const CodingUnit& cu);
  uint32_t getNumNonZeroCoeffNonTs        (const CodingUnit& cu);
#if JEM_TOOLS
  bool isLICFlagPresent               (const CodingUnit& cu);
  bool isObmcFlagCoded                (const CodingUnit& cu);
#endif

#if JVET_K0248_GBI
  bool  isGBiIdxCoded                  (const CodingUnit& cu);
  uint8_t getValidGbiIdx               (const CodingUnit& cu);
  void  setGbiIdx                      (      CodingUnit& cu, uint8_t uh);
  uint8_t deriveGbiIdx                 (uint8_t gbiLO, uint8_t gbiL1);
#endif

  PUTraverser traversePUs             (      CodingUnit& cu);
  TUTraverser traverseTUs             (      CodingUnit& cu);
  cPUTraverser traversePUs            (const CodingUnit& cu);
  cTUTraverser traverseTUs            (const CodingUnit& cu);

#if JVET_K0357_AMVR
  bool  hasSubCUNonZeroMVd            (const CodingUnit& cu);
  int   getMaxNeighboriMVCandNum      (const CodingStructure& cs, const Position& pos);
  void  resetMVDandMV2Int             (      CodingUnit& cu, InterPrediction *interPred );
#endif


}
// PU tools
namespace PU
{
#if JVET_K0190
  int  getLMSymbolList(const PredictionUnit &pu, int *pModeList);
#if JEM_TOOLS
  int  getIntraMPMs(const PredictionUnit &pu, unsigned *mpm, const ChannelType &channelType = CHANNEL_TYPE_LUMA, const bool isChromaMDMS = false, const unsigned startIdx = 0);  
  int  getDMModes(const PredictionUnit &pu, unsigned *modeList);
#else
  int  getIntraMPMs(const PredictionUnit &pu, unsigned *mpm, const ChannelType &channelType = CHANNEL_TYPE_LUMA);
#endif
#else
#if JEM_TOOLS
  int  getIntraMPMs                   (const PredictionUnit &pu, unsigned *mpm, const ChannelType &channelType = CHANNEL_TYPE_LUMA, const bool isChromaMDMS = false, const unsigned startIdx = 0 );
  int  getLMSymbolList                (const PredictionUnit &pu, int *pModeList);
  int  getDMModes                     (const PredictionUnit &pu, unsigned *modeList);
#else
  int  getIntraMPMs                   (const PredictionUnit &pu, unsigned *mpm, const ChannelType &channelType = CHANNEL_TYPE_LUMA);
#endif
#endif
  void getIntraChromaCandModes        (const PredictionUnit &pu, unsigned modeList[NUM_CHROMA_MODE]);
  uint32_t getFinalIntraMode              (const PredictionUnit &pu, const ChannelType &chType);

  void getInterMergeCandidates        (const PredictionUnit &pu, MergeCtx& mrgCtx, const int& mrgCandIdx = -1 );
  bool isDiffMER                      (const PredictionUnit &pu, const PredictionUnit &pu2);
#if JEM_TOOLS
  bool getColocatedMVP                (const PredictionUnit &pu, const RefPicList &eRefPicList, const Position &pos, Mv& rcMv, const int &refIdx, bool* pLICFlag = 0 );
  void fillMvpCand                    (      PredictionUnit &pu, const RefPicList &eRefPicList, const int &refIdx, AMVPInfo &amvpInfo, InterPrediction *interPred = NULL);
  void fillAffineMvpCand              (      PredictionUnit &pu, const RefPicList &eRefPicList, const int &refIdx, AffineAMVPInfo &affiAMVPInfo);
  bool addMVPCandUnscaled             (const PredictionUnit &pu, const RefPicList &eRefPicList, const int &iRefIdx, const Position &pos, const MvpDir &eDir, AMVPInfo &amvpInfo, bool affine = false);
  bool addMVPCandWithScaling          (const PredictionUnit &pu, const RefPicList &eRefPicList, const int &iRefIdx, const Position &pos, const MvpDir &eDir, AMVPInfo &amvpInfo, bool affine = false);
  void xInheritedAffineMv             ( const PredictionUnit &pu, const PredictionUnit* puNeighbour, RefPicList eRefPicList, Mv rcMv[3] );
#elif JVET_K_AFFINE
  bool getColocatedMVP                (const PredictionUnit &pu, const RefPicList &eRefPicList, const Position &pos, Mv& rcMv, const int &refIdx);
  void fillMvpCand                    (      PredictionUnit &pu, const RefPicList &eRefPicList, const int &refIdx, AMVPInfo &amvpInfo );
  void fillAffineMvpCand              (      PredictionUnit &pu, const RefPicList &eRefPicList, const int &refIdx, AffineAMVPInfo &affiAMVPInfo);
  bool addMVPCandUnscaled             (const PredictionUnit &pu, const RefPicList &eRefPicList, const int &iRefIdx, const Position &pos, const MvpDir &eDir, AMVPInfo &amvpInfo, bool affine = false);
  bool addMVPCandWithScaling          (const PredictionUnit &pu, const RefPicList &eRefPicList, const int &iRefIdx, const Position &pos, const MvpDir &eDir, AMVPInfo &amvpInfo, bool affine = false);
  void xInheritedAffineMv             ( const PredictionUnit &pu, const PredictionUnit* puNeighbour, RefPicList eRefPicList, Mv rcMv[3] );
#else
  void fillMvpCand                    (      PredictionUnit &pu, const RefPicList &eRefPicList, const int &refIdx, AMVPInfo &amvpInfo);
  bool addMVPCandUnscaled             (const PredictionUnit &pu, const RefPicList &eRefPicList, const int &iRefIdx, const Position &pos, const MvpDir &eDir, AMVPInfo &amvpInfo);
  bool addMVPCandWithScaling          (const PredictionUnit &pu, const RefPicList &eRefPicList, const int &iRefIdx, const Position &pos, const MvpDir &eDir, AMVPInfo &amvpInfo);
  bool getColocatedMVP                (const PredictionUnit &pu, const RefPicList &eRefPicList, const Position &pos, Mv& rcMv, const int &refIdx );
#endif
  bool isBipredRestriction            (const PredictionUnit &pu);
  void spanMotionInfo                 (      PredictionUnit &pu, const MergeCtx &mrgCtx = MergeCtx() );
#if JEM_TOOLS
  void spanLICFlags                   (      PredictionUnit &pu, const bool LICFlag );

  bool getInterMergeSubPuMvpCand      (const PredictionUnit &pu, MergeCtx &mrgCtx, bool& LICFlag, const int count 
#if JVET_K0076_CPR
    , const int countIBC
#endif
  );
  bool getInterMergeSubPuRecurCand    (const PredictionUnit &pu, MergeCtx &mrgCtx, const int count );
#endif
#if JVET_K0357_AMVR
  void applyImv                       (      PredictionUnit &pu, MergeCtx &mrgCtx, InterPrediction *interPred = NULL );
#endif
#if JVET_K0248_GBI
  void getAffineMergeCand             (const PredictionUnit &pu, MvField (*mvFieldNeighbours)[3], unsigned char &interDirNeighbours, unsigned char &gbiIdx, int &numValidMergeCand);
#else
  void getAffineMergeCand             (const PredictionUnit &pu, MvField (*mvFieldNeighbours)[3], unsigned char &interDirNeighbours, int &numValidMergeCand );
#endif
#if JEM_TOOLS
  bool isAffineMrgFlagCoded           (const PredictionUnit &pu );
  void setAllAffineMvField            (      PredictionUnit &pu, MvField *mvField, RefPicList eRefList );
  void setAllAffineMv                 (      PredictionUnit &pu, Mv affLT, Mv affRT, Mv affLB, RefPicList eRefList
#if REMOVE_MV_ADAPT_PREC
    , bool setHighPrec = false
#endif 
  );
  bool isBIOLDB                       (const PredictionUnit &pu);
#endif
#if !JEM_TOOLS && JVET_K_AFFINE
  bool isAffineMrgFlagCoded           (const PredictionUnit &pu );
  void getAffineMergeCand             (const PredictionUnit &pu, MvField (*mvFieldNeighbours)[3], unsigned char &interDirNeighbours, int &numValidMergeCand );
  void setAllAffineMvField            (      PredictionUnit &pu, MvField *mvField, RefPicList eRefList );
  void setAllAffineMv                 (      PredictionUnit &pu, Mv affLT, Mv affRT, Mv affLB, RefPicList eRefList 
#if REMOVE_MV_ADAPT_PREC
    , bool setHighPrec = false
#endif 
  );
#endif
#if !JEM_TOOLS && JVET_K0346
  bool getInterMergeSubPuMvpCand(const PredictionUnit &pu, MergeCtx &mrgCtx, bool& LICFlag, const int count
#if JVET_K0076_CPR
    , const int countIBC
#endif
  );
  bool getInterMergeSubPuRecurCand(const PredictionUnit &pu, MergeCtx &mrgCtx, const int count);
#endif
  bool isBiPredFromDifferentDir       (const PredictionUnit &pu);
  void restrictBiPredMergeCands       (const PredictionUnit &pu, MergeCtx& mrgCtx);
#if JEM_TOOLS
  bool getNeighborMotion              (      PredictionUnit &pu, MotionInfo& mi, Position off, int iDir, bool bSubPu );
  bool getMvPair                      (const PredictionUnit &pu, RefPicList eCurRefPicList, const MvField & rCurMvField, MvField &rMvPair);
  bool isSameMVField                  (const PredictionUnit &pu, RefPicList eListA, MvField &rMVFieldA, RefPicList eListB, MvField &rMVFieldB);
  Mv   scaleMv                        (const Mv &rColMV, int iCurrPOC, int iCurrRefPOC, int iColPOC, int iColRefPOC, Slice *slice);
#endif

#if JEM_TOOLS||JVET_K0190
  bool isLMCMode                      (                          unsigned mode);
#endif
#if JEM_TOOLS
  bool isMMLMEnabled                  (const PredictionUnit &pu);
  bool isMFLMEnabled                  (const PredictionUnit &pu);
#endif
#if JEM_TOOLS||JVET_K0190
  bool isLMCModeEnabled               (const PredictionUnit &pu, unsigned mode);
#endif
  bool isChromaIntraModeCrossCheckMode(const PredictionUnit &pu);
#if JVET_K0076_CPR
  void getIntraBCMVPsEncOnly          (PredictionUnit &pu, Mv* MvPred, int& nbPred);
  bool getDerivedBV                   (PredictionUnit &pu, const Mv& currentMv, Mv& derivedMv);
  bool isBlockVectorValid             (PredictionUnit& pu, int xPos, int yPos, int width, int height, int picWidth, int picHeight, int xStartInCU, int yStartInCU, int xBv, int yBv, int ctuSize);
#endif
}

// TU tools
namespace TU
{
  uint32_t getNumNonZeroCoeffsNonTS       (const TransformUnit &tu, const bool bLuma = true, const bool bChroma = true);
#if HEVC_USE_4x4_DSTVII
  bool useDST                         (const TransformUnit &tu, const ComponentID &compID);
#endif
  bool isNonTransformedResidualRotated(const TransformUnit &tu, const ComponentID &compID);
  bool getCbf                         (const TransformUnit &tu, const ComponentID &compID);
#if ENABLE_BMS
  bool getCbfAtDepth                  (const TransformUnit &tu, const ComponentID &compID, const unsigned &depth);
  void setCbfAtDepth                  (      TransformUnit &tu, const ComponentID &compID, const unsigned &depth, const bool &cbf);
#else
  void setCbf                         (      TransformUnit &tu, const ComponentID &compID, const bool &cbf);
#endif
  bool hasTransformSkipFlag           (const CodingStructure& cs, const CompArea& area);
  uint32_t getGolombRiceStatisticsIndex   (const TransformUnit &tu, const ComponentID &compID);
#if HEVC_USE_MDCS
  uint32_t getCoefScanIdx                 (const TransformUnit &tu, const ComponentID &compID);
#endif
  bool hasCrossCompPredInfo           (const TransformUnit &tu, const ComponentID &compID);

  bool needsSqrt2Scale                ( const Size& size );
#if HM_QTBT_AS_IN_JEM_QUANT
  bool needsBlockSizeTrafoScale       ( const Size& size );
#else
  bool needsQP3Offset                 (const TransformUnit &tu, const ComponentID &compID);
#endif
}

uint32_t getCtuAddr        (const Position& pos, const PreCalcValues &pcv);

template<typename T, size_t N>
uint32_t updateCandList( T uiMode, double uiCost, static_vector<T, N>& candModeList, static_vector<double, N>& candCostList, size_t uiFastCandNum = N )
{
  CHECK( std::min( uiFastCandNum, candModeList.size() ) != std::min( uiFastCandNum, candCostList.size() ), "Sizes do not match!" );
  CHECK( uiFastCandNum > candModeList.capacity(), "The vector is to small to hold all the candidates!" );

  size_t i;
  size_t shift = 0;
  size_t currSize = std::min( uiFastCandNum, candCostList.size() );

  while( shift < uiFastCandNum && shift < currSize && uiCost < candCostList[currSize - 1 - shift] )
  {
    shift++;
  }

  if( candModeList.size() >= uiFastCandNum && shift != 0 )
  {
    for( i = 1; i < shift; i++ )
    {
      candModeList[currSize - i] = candModeList[currSize - 1 - i];
      candCostList[currSize - i] = candCostList[currSize - 1 - i];
    }
    candModeList[currSize - shift] = uiMode;
    candCostList[currSize - shift] = uiCost;
    return 1;
  }
  else if( currSize < uiFastCandNum )
  {
    candModeList.insert( candModeList.end() - shift, uiMode );
    candCostList.insert( candCostList.end() - shift, uiCost );
    return 1;
  }

  return 0;
}


#endif

