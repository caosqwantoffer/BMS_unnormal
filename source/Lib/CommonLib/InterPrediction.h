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

/** \file     InterPrediction.h
    \brief    inter prediction class (header)
*/

#ifndef __INTERPREDICTION__
#define __INTERPREDICTION__


// Include files
#include "InterpolationFilter.h"
#include "WeightPrediction.h"

#include "Buffer.h"
#include "Unit.h"
#include "Picture.h"

#include "RdCost.h"
#include "ContextModelling.h"
#if DMVR_JVET_K0217
#include <array>
#endif
// forward declaration
class Mv;

//! \ingroup CommonLib
//! \{


// ====================================================================================================================
// Class definition
// ====================================================================================================================

#if JEM_TOOLS
#if JVET_K0485_BIO
#define BIO_TEMP_BUFFER_SIZE ( MAX_CU_SIZE+2*JVET_K0485_BIO_EXTEND_SIZE ) * ( MAX_CU_SIZE+2*JVET_K0485_BIO_EXTEND_SIZE )
#else
#define BIO_TEMP_BUFFER_SIZE ( MAX_CU_SIZE ) * ( MAX_CU_SIZE )
#endif
#endif
#if DMVR_JVET_K0217
#if DISTORTION_TYPE_BUGFIX
typedef Distortion  MRSADtype;
#else
typedef uint32_t    MRSADtype;
#endif
namespace ns_SAD_POINTS_INDEXES
{
  const MRSADtype NotDefinedSAD = std::numeric_limits<MRSADtype>::max();
  enum SAD_POINT_INDEX
  {
    NOT_AVAILABLE = -1,
    BOTTOM = 0,
    TOP,
    RIGHT,
    LEFT,
    TOP_LEFT,
    TOP_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_RIGHT,
    CENTER,
    COUNT
  };
  inline SAD_POINT_INDEX& operator += (SAD_POINT_INDEX& lastValue, int value)
  {
    lastValue = static_cast<SAD_POINT_INDEX>(static_cast<int>(lastValue) + value);
    return lastValue;
  }
  inline void operator ++ (SAD_POINT_INDEX& lastValue)
  {
    lastValue = static_cast<SAD_POINT_INDEX>(static_cast<int>(lastValue)+1);
  }
}
#endif
class InterPrediction : public WeightPrediction
{
private:
#if JEM_TOOLS
  static const int  m_LICShift      = 5;
  static const int  m_LICRegShift   = 7;
  static const int  m_LICShiftDiff  = 12;
  int               m_LICMultApprox[64];
#endif

#if JEM_TOOLS
#if JVET_K0485_BIO
  Distortion  m_bioDistThres;
  Distortion  m_bioSubBlkDistThres;
  Distortion  m_bioPredSubBlkDist[MAX_NUM_PARTS_IN_CTU];
#endif
  int64_t m_piDotProduct1[BIO_TEMP_BUFFER_SIZE];
  int64_t m_piDotProduct2[BIO_TEMP_BUFFER_SIZE];
  int64_t m_piDotProduct3[BIO_TEMP_BUFFER_SIZE];
  int64_t m_piDotProduct5[BIO_TEMP_BUFFER_SIZE];
  int64_t m_piDotProduct6[BIO_TEMP_BUFFER_SIZE];
#endif

protected:
  InterpolationFilter  m_if;

  Pel*                 m_acYuvPred            [NUM_REF_PIC_LIST_01][MAX_NUM_COMPONENT];
  Pel*                 m_filteredBlock        [LUMA_INTERPOLATION_FILTER_SUB_SAMPLE_POSITIONS][LUMA_INTERPOLATION_FILTER_SUB_SAMPLE_POSITIONS][MAX_NUM_COMPONENT];
  Pel*                 m_filteredBlockTmp     [LUMA_INTERPOLATION_FILTER_SUB_SAMPLE_POSITIONS][MAX_NUM_COMPONENT];


  ChromaFormat         m_currChromaFormat;

  ComponentID          m_maxCompIDToPred;      ///< tells the predictor to only process the components up to (inklusive) this one - useful to skip chroma components during RD-search

  RdCost*              m_pcRdCost;

  int                  m_iRefListIdx;
  
#if JEM_TOOLS
  Pel*                 m_pGradX0;
  Pel*                 m_pGradY0;
  Pel*                 m_pGradX1;
  Pel*                 m_pGradY1;
#if JVET_K0485_BIO
  Pel*                 m_pBIOPadRef;
#endif

  PelStorage           m_tmpObmcBuf;

#if !DMVR_JVET_K0217
  Pel*                 m_cYuvPredTempDMVR[MAX_NUM_COMPONENT];
#else
  PelUnitBuf           m_cYuvPredTempL0;
  PelUnitBuf           m_cYuvPredTempL1;
  Pel*                 m_cYuvPredTempDMVRL0;
  Pel*                 m_cYuvPredTempDMVRL1;
  PelUnitBuf           m_HalfPelFilteredBuffL0[2][2];
  Pel*                 m_filteredBlockL1[2][2];
  PelUnitBuf           m_HalfPelFilteredBuffL1[2][2];
  std::array<MRSADtype, ns_SAD_POINTS_INDEXES::SAD_POINT_INDEX::COUNT> m_currentSADsArray;
  std::array<MRSADtype, ns_SAD_POINTS_INDEXES::SAD_POINT_INDEX::COUNT> m_previousSADsArray;
  ns_SAD_POINTS_INDEXES::SAD_POINT_INDEX m_lastDirection;
#if DMVR_JVET_SEARCH_RANGE_K0217 > 2
  std::vector<Mv>      m_checkedMVsList;
#endif
  std::array<Mv, 5>    m_pSearchOffset = { { Mv(0, 1), Mv(0, -1), Mv(1, 0), Mv(-1, 0), Mv(0, 0) } };
  static const uint32_t m_searchRange = DMVR_JVET_SEARCH_RANGE_K0217;
  static const uint32_t m_bufferWidthExtSize = m_searchRange << 1;
#endif
#if !JVET_K0485_BIO
  uint32_t                 m_uiaBIOShift[64];
#endif
#if JVET_J0090_MEMORY_BANDWITH_MEASURE
  CacheModel*          m_cacheModel;
#endif

  // motion compensation functions
#define BIO_FILTER_LENGTH                 6
#define BIO_FILTER_LENGTH_MINUS_1         (BIO_FILTER_LENGTH-1)
#define BIO_FILTER_HALF_LENGTH_MINUS_1    ((BIO_FILTER_LENGTH>>1)-1)

#if JVET_K0485_BIO
  void          (*bioGradFilter)(Pel* pSrc, int srcStride, int width, int height, int gradStride, Pel* pGradX, Pel* pGradY);
  static void   gradFilter      (Pel* pSrc, int srcStride, int width, int height, int gradStride, Pel* pGradX, Pel* pGradY);
#else
  void          xGradFilterX    ( const Pel* piRefY, int iRefStride, Pel*  piDstY, int iDstStride, int iWidth, int iHeight, int iMVyFrac, int iMVxFrac, const int bitDepth );
  void          xGradFilterY    ( const Pel* piRefY, int iRefStride, Pel*  piDstY, int iDstStride, int iWidth, int iHeight, int iMVyFrac, int iMVxFrac, const int bitDepth );
  inline void   gradFilter2DVer ( const Pel* piSrc, int iSrcStride, int iWidth, int iHeight, int iDstStride, Pel*& rpiDst, int iMv, const int iShift );
  inline void   gradFilter2DHor ( const Pel* piSrc, int iSrcStride, int iWidth, int iHeight, int iDstStride, Pel*& rpiDst, int iMV, const int iShift );
  inline void   fracFilter2DHor ( const Pel* piSrc, int iSrcStride, int iWidth, int iHeight, int iDstStride, Pel*& rpiDst, int iMV, const int iShift );
  inline void   fracFilter2DVer ( const Pel* piSrc, int iSrcStride, int iWidth, int iHeight, int iDstStride, Pel*& rpiDst, int iMv, const int iShift );
  inline void   gradFilter1DHor ( const Pel* piSrc, int iSrcStride, int iWidth, int iHeight, int iDstStride, Pel*& rpiDst, int iMV, const int iShift );
  inline void   gradFilter1DVer ( const Pel* piSrc, int iSrcStride, int iWidth, int iHeight, int iDstStride, Pel*& rpiDst, int iMV, const int iShift );
#endif

  inline int64_t  divide64        ( int64_t numer, int64_t denom);
#if JVET_K0485_BIO
  inline void     calcBlkGradient(int sx, int sy, int64_t *arraysGx2, int64_t *arraysGxGy, int64_t *arraysGxdI, int64_t *arraysGy2, int64_t *arraysGydI,
                                  int64_t &sGx2,  int64_t &sGy2,      int64_t &sGxGy,      int64_t &sGxdI,      int64_t &sGydI,     int width, int height, int unitSize);
#else
  inline void   calcBlkGradient ( int sx, int sy, int64_t *arraysGx2, int64_t *arraysGxGy, int64_t *arraysGxdI, int64_t *arraysGy2, int64_t *arraysGydI, int64_t &sGx2, int64_t &sGy2, int64_t &sGxGy, int64_t &sGxdI, int64_t &sGydI, int iWidth, int iHeight);
  Pel  optical_flow_averaging   ( int64_t s1, int64_t s2, int64_t s3, int64_t s5, int64_t s6,
                                  Pel pGradX0, Pel pGradX1, Pel pGradY0, Pel pGradY1, Pel pSrcY0Temp, Pel pSrcY1Temp,
                                  const int shiftNum, const int offset, const int64_t limit, const int64_t denom_min_1, const int64_t denom_min_2, const ClpRng& clpRng );
#endif
  void applyBiOptFlow           ( const PredictionUnit &pu, const CPelUnitBuf &pcYuvSrc0, const CPelUnitBuf &pcYuvSrc1, const int &iRefIdx0, const int &iRefIdx1, PelUnitBuf &pcYuvDst, const BitDepths &clipBitDepths);
#if JVET_K0485_BIO
  bool xCalcBiPredSubBlkDist    (const PredictionUnit &pu, const Pel* pYuvSrc0, const int src0Stride, const Pel* pYuvSrc1, const int src1Stride, const BitDepths &clipBitDepths);
#endif
#endif

#if JEM_TOOLS
  void xPredInterUni            ( const PredictionUnit& pu, const RefPicList& eRefPicList, PelUnitBuf& pcYuvPred, const bool& bi, const bool& bBIOApplied = false, const bool& bDMVRApplied = false 
#if JVET_K0076_CPR_DT
  , const bool luma = true, const bool chroma = true
#endif
  );
  void xPredInterBi             ( PredictionUnit& pu, PelUnitBuf &pcYuvPred, bool obmc = false );
#else
  void xPredInterUni            ( const PredictionUnit& pu, const RefPicList& eRefPicList, PelUnitBuf& pcYuvPred, const bool& bi 
#if JVET_K0076_CPR_DT
    , const bool luma = true, const bool chroma = true
#endif
  );
  void xPredInterBi             ( PredictionUnit& pu, PelUnitBuf &pcYuvPred );
#endif
  void xPredInterBlk            ( const ComponentID& compID, const PredictionUnit& pu, const Picture* refPic, const Mv& _mv, PelUnitBuf& dstPic, const bool& bi, const ClpRng& clpRng
#if JEM_TOOLS
                                  , const bool& bBIOApplied = false, const bool& bDMVRApplied = false, const int& nFRUCMode = FRUC_MERGE_OFF, const bool& doLic = true
#endif
#if DMVR_JVET_K0217
  , bool doPred = true
  , SizeType DMVRwidth = 0
  , SizeType DMVRheight = 0
#endif 
                                 );
  
#if JEM_TOOLS
#if JVET_K0485_BIO
  void xPadRefFromFMC           (const Pel* refBufPtr, int refBufStride, int width, int height, Pel* padRefPelPtr, int &padRefStride, bool isFracMC);
#endif
  void xPredAffineBlk           ( const ComponentID& compID, const PredictionUnit& pu, const Picture* refPic, const Mv* _mv, PelUnitBuf& dstPic, const bool& bi, const ClpRng& clpRng, const bool& bBIOApplied = false );
  void xGetLICParams            ( const CodingUnit& cu, const ComponentID compID, const Picture& refPic, const Mv& mv, int& shift, int& scale, int& offset );
  void xLocalIlluComp           ( const PredictionUnit& pu, const ComponentID compID, const Picture& refPic, const Mv& mv, const bool biPred, PelBuf& dstBuf );
  void xWeightedAverage         ( const PredictionUnit& pu, const CPelUnitBuf& pcYuvSrc0, const CPelUnitBuf& pcYuvSrc1, PelUnitBuf& pcYuvDst, const BitDepths& clipBitDepths, const ClpRngs& clpRngs, const bool& bBIOApplied );
#else
  void xWeightedAverage         ( const PredictionUnit& pu, const CPelUnitBuf& pcYuvSrc0, const CPelUnitBuf& pcYuvSrc1, PelUnitBuf& pcYuvDst, const BitDepths& clipBitDepths, const ClpRngs& clpRngs );
#endif
#if !JEM_TOOLS && JVET_K_AFFINE
  void xPredAffineBlk( const ComponentID& compID, const PredictionUnit& pu, const Picture* refPic, const Mv* _mv, PelUnitBuf& dstPic, const bool& bi, const ClpRng& clpRng );
#endif

  static bool xCheckIdenticalMotion( const PredictionUnit& pu );

#if JEM_TOOLS
  void xSubPuMC                 ( PredictionUnit& pu, PelUnitBuf& predBuf, const RefPicList &eRefPicList = REF_PIC_LIST_X );
  void xSubblockOBMC            ( const ComponentID eComp, PredictionUnit &pu, PelUnitBuf &pcYuvPredDst, PelUnitBuf &pcYuvPredSrc, int iDir, bool bOBMCSimp );
  void xSubtractOBMC            ( PredictionUnit &pu, PelUnitBuf &pcYuvPredDst, PelUnitBuf &pcYuvPredSrc, int iDir, bool bOBMCSimp );
#endif
#if !JEM_TOOLS && JVET_K0346
  void xSubPuMC(PredictionUnit& pu, PelUnitBuf& predBuf, const RefPicList &eRefPicList = REF_PIC_LIST_X);
#endif
#if JEM_TOOLS
  void xSubBlockMotionCompensation( PredictionUnit &pu, PelUnitBuf &pcYuvPred );
#endif
#if JVET_K0076_CPR_DT
  void xChromaMC                ( PredictionUnit &pu, PelUnitBuf& pcYuvPred );
#endif
  void destroy();

#if JEM_TOOLS
  MotionInfo      m_SubPuMiBuf   [( MAX_CU_SIZE * MAX_CU_SIZE ) >> ( MIN_CU_LOG2 << 1 )];
  MotionInfo      m_SubPuExtMiBuf[( MAX_CU_SIZE * MAX_CU_SIZE ) >> ( MIN_CU_LOG2 << 1 )];

  std::list<MvField> m_listMVFieldCand[2];
  RefPicList m_bilatBestRefPicList;
  Pel*   m_acYuvPredFrucTemplate[2][MAX_NUM_COMPONENT];   //0: top, 1: left
  bool   m_bFrucTemplateAvailabe[2];

  bool xFrucFindBlkMv           (PredictionUnit& pu, const MergeCtx& mergeCtx );
  bool xFrucRefineSubBlkMv      (PredictionUnit& pu, const MergeCtx& mergeCtx, bool bTM);

  void xFrucCollectBlkStartMv   (PredictionUnit& pu, const MergeCtx& mergeCtx, RefPicList eTargetRefList = REF_PIC_LIST_0, int nTargetRefIdx = -1, AMVPInfo* pInfo = NULL);
  void xFrucCollectSubBlkStartMv(PredictionUnit& pu, const MergeCtx& mergeCtx, RefPicList eRefPicList , const MvField& rMvStart , int nSubBlkWidth , int nSubBlkHeight, Position basePuPos);
#if DISTORTION_TYPE_BUGFIX
  Distortion xFrucFindBestMvFromList(MvField *pBestMvField, RefPicList &rBestRefPicList, PredictionUnit &pu,
                                     const MvField &rMvStart, int nBlkWidth, int nBlkHeight, bool bTM, bool bMvCost);
  Distortion xFrucRefineMv(MvField *pBestMvField, RefPicList eCurRefPicList, Distortion uiMinCost, int nSearchMethod,
                           PredictionUnit &pu, const MvField &rMvStart, int nBlkWidth, int nBlkHeight, bool bTM,
                           bool bMvCostZero = false);
#else
  uint32_t xFrucFindBestMvFromList  (MvField* pBestMvField, RefPicList& rBestRefPicList, PredictionUnit& pu, const MvField& rMvStart, int nBlkWidth, int nBlkHeight, bool bTM, bool bMvCost);
  uint32_t xFrucRefineMv(MvField *pBestMvField, RefPicList eCurRefPicList, uint32_t uiMinCost, int nSearchMethod,
                     PredictionUnit &pu, const MvField &rMvStart, int nBlkWidth, int nBlkHeight, bool bTM,
                     bool bMvCostZero = false);
#endif
#if DISTORTION_TYPE_BUGFIX
  template<int SearchPattern>
  Distortion xFrucRefineMvSearch(MvField *pBestMvField, RefPicList eCurRefPicList, PredictionUnit &pu,
                                 const MvField &rMvStart, int nBlkWidth, int nBlkHeight, Distortion uiMinDist, bool bTM,
                                 int nSearchStepShift, uint32_t uiMaxSearchRounds = MAX_UINT, bool bMvCostZero = false);
#else
  template<int SearchPattern>
  uint32_t xFrucRefineMvSearch      (MvField* pBestMvField, RefPicList eCurRefPicList, PredictionUnit& pu, const MvField& rMvStart, int nBlkWidth, int nBlkHeight, uint32_t uiMinDist, bool bTM, int nSearchStepShift, uint32_t uiMaxSearchRounds = MAX_UINT, bool bMvCostZero = false);
#endif

#if DISTORTION_TYPE_BUGFIX
  Distortion xFrucGetMvCost(const Mv &rMvStart, const Mv &rMvCur, int nSearchRange, int nWeighting, uint32_t precShift);
  Distortion xFrucGetBilaMatchCost(PredictionUnit &pu, int nWidth, int nHeight, RefPicList eCurRefPicList,
                                   const MvField &rCurMvField, MvField &rPairMVField, Distortion uiMVCost);
  Distortion xFrucGetTempMatchCost(PredictionUnit &pu, int nWidth, int nHeight, RefPicList eCurRefPicList,
                                   const MvField &rCurMvField, Distortion uiMVCost);
#else
  uint32_t xFrucGetMvCost(const Mv &rMvStart, const Mv &rMvCur, int nSearchRange, int nWeighting, uint32_t precShift);
  uint32_t xFrucGetBilaMatchCost    (PredictionUnit& pu, int nWidth, int nHeight, RefPicList eCurRefPicList, const MvField& rCurMvField, MvField& rPairMVField, uint32_t uiMVCost );
  uint32_t xFrucGetTempMatchCost    (PredictionUnit& pu, int nWidth, int nHeight, RefPicList eCurRefPicList, const MvField& rCurMvField, uint32_t uiMVCost );
#endif
  void xFrucUpdateTemplate      (PredictionUnit& pu, int nWidth, int nHeight, RefPicList eCurRefPicList, const MvField& rCurMvField );

#if REMOVE_MV_ADAPT_PREC
  void xFrucInsertMv2StartList(const MvField & rMvField, std::list<MvField> & rList);
#else
  void xFrucInsertMv2StartList(const MvField & rMvField, std::list<MvField> & rList, bool setHighPrec);
#endif
  bool xFrucIsInList            (const MvField & rMvField, std::list<MvField> & rList);

  bool xFrucGetCurBlkTemplate   (PredictionUnit& pu, int nCurBlkWidth , int nCurBlkHeight);
  bool xFrucIsTopTempAvailable  (PredictionUnit& pu);
  bool xFrucIsLeftTempAvailable (PredictionUnit& pu);
  int  xFrucGetSubBlkSize       (PredictionUnit& pu, int nBlkWidth, int nBlkHeight);
#if !DMVR_JVET_K0217
#if DISTORTION_TYPE_BUGFIX
  void xBIPMVRefine(PredictionUnit &pu, RefPicList eRefPicList, int iWidth, int iHeight, const CPelUnitBuf &pcYuvOrg,
                    uint32_t uiMaxSearchRounds, uint32_t nSearchStepShift, Distortion &uiMinCost, bool fullPel = true);
  Distortion xDirectMCCost(int iBitDepth, Pel *pRef, uint32_t uiRefStride, const Pel *pOrg, uint32_t uiOrgStride, int iWidth,
                           int iHeight);
#else
  void xBIPMVRefine(PredictionUnit &pu, RefPicList eRefPicList, int iWidth, int iHeight, const CPelUnitBuf &pcYuvOrg,
                    uint32_t uiMaxSearchRounds, uint32_t nSearchStepShift, uint32_t &uiMinCost, bool fullPel = true);
  uint32_t xDirectMCCost            (int iBitDepth, Pel* pRef, uint32_t uiRefStride, const Pel* pOrg, uint32_t uiOrgStride, int iWidth, int iHeight);
#endif
  void xPredInterLines          (const PredictionUnit& pu, const Picture* refPic, Mv &mv, PelUnitBuf &dstPic, const bool &bi, const ClpRng& clpRng );
  void xFillPredBlckAndBorder   (const PredictionUnit& pu, RefPicList eRefPicList, int iWidth, int iHeight, PelBuf &cTmpY );
#else
  void xBIPMVRefine(PredictionUnit& pu, uint32_t nSearchStepShift, MRSADtype& minCost, DistParam &cDistParam, Mv *refineMv = nullptr);
  MRSADtype xDirectMCCostDMVR(const Pel* pSrcL0, const Pel* pSrcL1, uint32_t stride, SizeType width, SizeType height, const DistParam &cDistParam);
  void sumUpSamples(const Pel *pRef, uint32_t  refStride, SizeType cuWidth, SizeType cuHeight, int32_t& Avg);
  void xGenerateFracPixel(PredictionUnit& pu, uint32_t nSearchStepShift, const ClpRngs &clpRngs);
#endif  
  void xProcessDMVR             (      PredictionUnit& pu, PelUnitBuf &pcYuvDst, const ClpRngs &clpRngs, const bool bBIOApplied);
#endif

#if !JEM_TOOLS && JVET_K0346
  MotionInfo      m_SubPuMiBuf[(MAX_CU_SIZE * MAX_CU_SIZE) >> (MIN_CU_LOG2 << 1)];
#endif

public:
  InterPrediction();
  virtual ~InterPrediction();

  void    init                (RdCost* pcRdCost, ChromaFormat chromaFormatIDC);

  // inter
  void    motionCompensation  (PredictionUnit &pu, PelUnitBuf& predBuf, const RefPicList &eRefPicList = REF_PIC_LIST_X
#if JVET_K0076_CPR_DT
 , const bool luma = true, const bool chroma = true
#endif
  );
  void    motionCompensation  (PredictionUnit &pu, const RefPicList &eRefPicList = REF_PIC_LIST_X
#if JVET_K0076_CPR_DT
    , const bool luma = true, const bool chroma = true
#endif
  );
  void    motionCompensation  (CodingUnit &cu,     const RefPicList &eRefPicList = REF_PIC_LIST_X
#if JVET_K0076_CPR_DT
    , const bool luma = true, const bool chroma = true
#endif
  );

#if JEM_TOOLS
  void    subBlockOBMC        (CodingUnit      &cu);
  void    subBlockOBMC        (PredictionUnit  &pu, PelUnitBuf *pDst = nullptr, bool bOBMC4ME = false);

  bool    deriveFRUCMV        (PredictionUnit &pu);
  bool    frucFindBlkMv4Pred  (PredictionUnit& pu, RefPicList eTargetRefPicList, const int nTargetRefIdx, AMVPInfo* pInfo = NULL);
#endif
#if JVET_J0090_MEMORY_BANDWITH_MEASURE
  void    cacheAssign( CacheModel *cache );
#endif

};

//! \}

#endif // __INTERPREDICTION__
