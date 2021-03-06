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

/** \file     CABACWriter.cpp
 *  \brief    Writer for low level syntax
 */

#include "CommonLib/Contexts.h"
#include "CABACWriter.h"

#include "EncLib.h"

#include "CommonLib/UnitTools.h"
#include "CommonLib/dtrace_buffer.h"
#include "CommonLib/BinaryDecisionTree.h"

#include <map>
#include <algorithm>
#include <limits>


//! \ingroup EncoderLib
//! \{

#if JEM_TOOLS
void CABACWriter::initCtxModels( const Slice& slice, const CABACDataStore* cabacDataStore )
#else
void CABACWriter::initCtxModels( const Slice& slice )
#endif
{
  int       qp                = slice.getSliceQp();
  SliceType sliceType         = slice.getSliceType();
  SliceType encCABACTableIdx  = slice.getEncCABACTableIdx();
  if( !slice.isIntra() && (encCABACTableIdx==B_SLICE || encCABACTableIdx==P_SLICE) && slice.getPPS()->getCabacInitPresentFlag() )
  {
    sliceType = encCABACTableIdx;
  }
  m_BinEncoder.reset( qp, (int)sliceType );
#if JEM_TOOLS
  if( cabacDataStore )
  {
#if JEM_TOOLS
    m_BinEncoder.setWinSizes( cabacDataStore->getWinSizes(&slice) );
#endif
  }
#endif
}



template <class BinProbModel>
SliceType xGetCtxInitId( const Slice& slice, const BinEncIf& binEncoder, Ctx& ctxTest )
{
  const CtxStore<BinProbModel>& ctxStoreTest = static_cast<const CtxStore<BinProbModel>&>( ctxTest );
  const CtxStore<BinProbModel>& ctxStoreRef  = static_cast<const CtxStore<BinProbModel>&>( binEncoder.getCtx() );
  int qp = slice.getSliceQp();
  if( !slice.isIntra() )
  {
    SliceType aSliceTypeChoices[] = { B_SLICE, P_SLICE };
    uint64_t  bestCost            = std::numeric_limits<uint64_t>::max();
    SliceType bestSliceType       = aSliceTypeChoices[0];
    for (uint32_t idx=0; idx<2; idx++)
    {
      uint64_t  curCost           = 0;
      SliceType curSliceType      = aSliceTypeChoices[idx];
      ctxTest.init( qp, (int)curSliceType );
      for( int k = 0; k < Ctx::NumberOfContexts; k++ )
      {
        if( binEncoder.getNumBins(k) > 0 )
        {
          curCost += uint64_t( binEncoder.getNumBins(k) ) * ctxStoreRef[k].estFracExcessBits( ctxStoreTest[k] );
        }
      }
      if (curCost < bestCost)
      {
        bestSliceType = curSliceType;
        bestCost      = curCost;
      }
    }
    return bestSliceType;
  }
  else
  {
    return I_SLICE;
  }
}


SliceType CABACWriter::getCtxInitId( const Slice& slice )
{
  switch( m_TestCtx.getBPMType() )
  {
  case BPM_Std:   return  xGetCtxInitId<BinProbModel_Std>   ( slice, m_BinEncoder, m_TestCtx );
#if JEM_TOOLS
  case BPM_JMP:   return  xGetCtxInitId<BinProbModel_JMP>   ( slice, m_BinEncoder, m_TestCtx );
  case BPM_JAW:   return  xGetCtxInitId<BinProbModel_JAW>   ( slice, m_BinEncoder, m_TestCtx );
  case BPM_JMPAW: return  xGetCtxInitId<BinProbModel_JMPAW> ( slice, m_BinEncoder, m_TestCtx );
#endif
  default:        return  NUMBER_OF_SLICE_TYPES;
  }
}



unsigned estBits( BinEncIf& binEnc, const std::vector<bool>& bins, const Ctx& ctx, const int ctxId, const uint8_t winSize )
{
  binEnc.initCtxAndWinSize( ctxId, ctx, winSize );
  binEnc.start();
  const std::size_t numBins   = bins.size();
  unsigned          startBits = binEnc.getNumWrittenBits();
  for( std::size_t binId = 0; binId < numBins; binId++ )
  {
    unsigned  bin = ( bins[binId] ? 1 : 0 );
    binEnc.encodeBin( bin, ctxId );
  }
  unsigned endBits    = binEnc.getNumWrittenBits();
  unsigned codedBits  = endBits - startBits;
  return   codedBits;
}

#if JEM_TOOLS
uint8_t estWinSize( BinEncIf& binEnc, const BinStore& binStore, const Ctx& ctx, const int ctxId )
{
  const uint8_t             numWinSizesToTest = 4;
  const uint8_t             minWinSize        = 4;
  const uint8_t             maxWinSize        = minWinSize + numWinSizesToTest - uint8_t(1);
  const uint8_t             defWinSize        = binEnc.getDefaultWindowSize();
  const std::vector<bool>&  bins              = binStore.getBinVector( ctxId );
  if( !bins.size() )
  {
    return defWinSize;
  }
#if JEM_ADAPTIVE_WINDOW_MECHANISM
  uint8_t         bestWinSize       = 0;
  unsigned        minBits           = std::numeric_limits<unsigned>::max();
  for( uint8_t testWinSize = minWinSize; testWinSize <= maxWinSize; testWinSize++ )
  {
    unsigned testBits = estBits( binEnc, bins, ctx, ctxId, testWinSize );
    if( testBits < minBits )
    {
      minBits     = testBits;
      bestWinSize = testWinSize;
    }
  }
#else
  uint8_t         bestWinSize       = defWinSize;
  unsigned        minBits           = estBits( binEnc, bins, ctx, ctxId, bestWinSize );
  for( uint8_t testWinSize = minWinSize; testWinSize <= maxWinSize; testWinSize++ )
  {
    if( testWinSize != defWinSize )
    {
      unsigned testBits = estBits( binEnc, bins, ctx, ctxId, testWinSize );
      if( testBits < minBits )
      {
        minBits     = testBits;
        bestWinSize = testWinSize;
      }
    }
  }
#endif
  return bestWinSize;
}


void CABACWriter::estWinSizes( const Slice& slice, CABACDataStore& cabacDataStore ) const
{
  const BinStore* binStore          = m_BinEncoder.getBinStore();
  if( !binStore || !binStore->inUse() )
  {
    return;
  }

  OutputBitstream       testBitStream;
  BinEncIf*             testBinEncoder  = m_BinEncoder.getTestBinEncoder();
  CHECKD( !testBinEncoder, "intern error" );
  testBinEncoder->init( &testBitStream );
  std::vector<uint8_t>& winBuffer       = cabacDataStore.getWinSizeBuffer( &slice );
  const std::size_t     numCodeIds      = cabacDataStore.getNumWSizeCodeIds();
  Ctx                   initCtx         = getCtx();

  if( slice.getSPS()->getSpsNext().getCIPFMode() == 1 ) // backward encoder compatibility to JEM [not required]
  {
    cabacDataStore.loadCtxStates( &slice, initCtx, 0 );
  }

  winBuffer.resize( 0 );
  winBuffer.resize( Ctx::NumberOfContexts, 0 );
  for( std::size_t codeId = 0; codeId < numCodeIds; codeId++ )
  {
    const int ctxId = cabacDataStore.getCtxIdFromWSizeCodeId( codeId );
    if( ctxId >= 0 )
    {
      winBuffer[ctxId] = estWinSize( *testBinEncoder, *binStore, initCtx, ctxId );
    }
  }
  cabacDataStore.setWSizeSetValid(&slice);
  delete testBinEncoder;
}

void CABACWriter::enableBinStore( const Slice& slice, CABACDataStore& cabacDataStore )
{
  if( slice.getSPS()->getSpsNext().getCABACEngineMode() == 2 || slice.getSPS()->getSpsNext().getCABACEngineMode() == 3 )
  {
    m_BinEncoder.setBinStorage(!cabacDataStore.validWinSizes(&slice));
  }
}
#endif




//================================================================================
//  clause 7.3.8.1
//--------------------------------------------------------------------------------
//    void  end_of_slice()
//================================================================================

void CABACWriter::end_of_slice()
{
  m_BinEncoder.encodeBinTrm ( 1 );
  m_BinEncoder.finish       ();
}




//================================================================================
//  clause 7.3.8.2
//--------------------------------------------------------------------------------
//    bool  coding_tree_unit( cs, area, qp, ctuRsAddr, skipSao )
//================================================================================

void CABACWriter::coding_tree_unit( CodingStructure& cs, const UnitArea& area, int (&qps)[2], unsigned ctuRsAddr, bool skipSao /* = false */ )
{
  CUCtx cuCtx( qps[CH_L] );
  Partitioner *partitioner = PartitionerFactory::get( *cs.slice );

  partitioner->initCtu( area, CH_L, *cs.slice );

  if( !skipSao )
  {
    sao( *cs.slice, ctuRsAddr );
  }

#if JVET_K0371_ALF
  for( int compIdx = 0; compIdx < MAX_NUM_COMPONENT; compIdx++ )
  {
    codeAlfCtuEnableFlag( cs, ctuRsAddr, compIdx );
  }
#endif

#if JVET_K0230_DUAL_CODING_TREE_UNDER_64x64_BLOCK
  if ( CS::isDualITree(cs) && cs.pcv->chrFormat != CHROMA_400 && cs.pcv->maxCUWidth > 64 )
  {
    CUCtx chromaCuCtx(qps[CH_C]);
    Partitioner *chromaPartitioner = PartitionerFactory::get(*cs.slice);
    chromaPartitioner->initCtu(area, CH_C, *cs.slice);
    coding_tree(cs, *partitioner, cuCtx, chromaPartitioner, &chromaCuCtx);
    qps[CH_L] = cuCtx.qp;
    qps[CH_C] = chromaCuCtx.qp;

    delete chromaPartitioner;
  }
  else
  {
#endif
  coding_tree( cs, *partitioner, cuCtx );
  qps[CH_L] = cuCtx.qp;
  if( CS::isDualITree( cs ) && cs.pcv->chrFormat != CHROMA_400 )
  {
    CUCtx cuCtxChroma( qps[CH_C] );
    partitioner->initCtu( area, CH_C, *cs.slice );
    coding_tree( cs, *partitioner, cuCtxChroma );
    qps[CH_C] = cuCtxChroma.qp;
  }
#if JVET_K0230_DUAL_CODING_TREE_UNDER_64x64_BLOCK
  }
#endif

  delete partitioner;
}





//================================================================================
//  clause 7.3.8.3
//--------------------------------------------------------------------------------
//    void  sao             ( slice, ctuRsAddr )
//    void  sao_block_pars  ( saoPars, bitDepths, sliceEnabled, leftMergeAvail, aboveMergeAvail, onlyEstMergeInfo )
//    void  sao_offset_pars ( ctbPars, compID, sliceEnabled, bitDepth )
//================================================================================

void CABACWriter::sao( const Slice& slice, unsigned ctuRsAddr )
{
  const SPS& sps = *slice.getSPS();
  if( !sps.getUseSAO() )
  {
    return;
  }

  CodingStructure&     cs                     = *slice.getPic()->cs;
  const PreCalcValues& pcv                    = *cs.pcv;
  const SAOBlkParam&  sao_ctu_pars            = cs.picture->getSAO()[ctuRsAddr];
  bool                slice_sao_luma_flag     = ( slice.getSaoEnabledFlag( CHANNEL_TYPE_LUMA ) );
  bool                slice_sao_chroma_flag   = ( slice.getSaoEnabledFlag( CHANNEL_TYPE_CHROMA ) && sps.getChromaFormatIdc() != CHROMA_400 );
  if( !slice_sao_luma_flag && !slice_sao_chroma_flag )
  {
    return;
  }

  bool                sliceEnabled[3]         = { slice_sao_luma_flag, slice_sao_chroma_flag, slice_sao_chroma_flag };
  int                 frame_width_in_ctus     = pcv.widthInCtus;
  int                 ry                      = ctuRsAddr      / frame_width_in_ctus;
  int                 rx                      = ctuRsAddr - ry * frame_width_in_ctus;
  const Position      pos                     ( rx * cs.pcv->maxCUWidth, ry * cs.pcv->maxCUHeight );
  const unsigned      curSliceIdx             = slice.getIndependentSliceIdx();
#if HEVC_TILES_WPP
  const unsigned      curTileIdx              = cs.picture->tileMap->getTileIdxMap( pos );
  bool                leftMergeAvail          = cs.getCURestricted( pos.offset( -(int)pcv.maxCUWidth, 0  ), curSliceIdx, curTileIdx, CH_L ) ? true : false;
  bool                aboveMergeAvail         = cs.getCURestricted( pos.offset( 0, -(int)pcv.maxCUHeight ), curSliceIdx, curTileIdx, CH_L ) ? true : false;
#else
  bool                leftMergeAvail          = cs.getCURestricted( pos.offset( -(int)pcv.maxCUWidth, 0  ), curSliceIdx, CH_L ) ? true : false;
  bool                aboveMergeAvail         = cs.getCURestricted( pos.offset( 0, -(int)pcv.maxCUHeight ), curSliceIdx, CH_L ) ? true : false;
#endif
  sao_block_pars( sao_ctu_pars, sps.getBitDepths(), sliceEnabled, leftMergeAvail, aboveMergeAvail, false );
}


void CABACWriter::sao_block_pars( const SAOBlkParam& saoPars, const BitDepths& bitDepths, bool* sliceEnabled, bool leftMergeAvail, bool aboveMergeAvail, bool onlyEstMergeInfo )
{
  bool isLeftMerge  = false;
  bool isAboveMerge = false;
  if( leftMergeAvail )
  {
    // sao_merge_left_flag
    isLeftMerge   = ( saoPars[COMPONENT_Y].modeIdc == SAO_MODE_MERGE && saoPars[COMPONENT_Y].typeIdc == SAO_MERGE_LEFT );
    m_BinEncoder.encodeBin( (isLeftMerge), Ctx::SaoMergeFlag() );
  }
  if( aboveMergeAvail && !isLeftMerge )
  {
    // sao_merge_above_flag
    isAboveMerge  = ( saoPars[COMPONENT_Y].modeIdc == SAO_MODE_MERGE && saoPars[COMPONENT_Y].typeIdc == SAO_MERGE_ABOVE );
    m_BinEncoder.encodeBin( (isAboveMerge), Ctx::SaoMergeFlag() );
  }
  if( onlyEstMergeInfo )
  {
    return; //only for RDO
  }
  if( !isLeftMerge && !isAboveMerge )
  {
    // explicit parameters
    for( int compIdx=0; compIdx < MAX_NUM_COMPONENT; compIdx++ )
    {
      sao_offset_pars( saoPars[compIdx], ComponentID(compIdx), sliceEnabled[compIdx], bitDepths.recon[ toChannelType(ComponentID(compIdx)) ] );
    }
  }
}


void CABACWriter::sao_offset_pars( const SAOOffset& ctbPars, ComponentID compID, bool sliceEnabled, int bitDepth )
{
  if( !sliceEnabled )
  {
    CHECK( ctbPars.modeIdc != SAO_MODE_OFF, "Sao must be off, if it is disabled on slice level" );
    return;
  }
  const bool isFirstCompOfChType = ( getFirstComponentOfChannel( toChannelType(compID) ) == compID );

  if( isFirstCompOfChType )
  {
    // sao_type_idx_luma / sao_type_idx_chroma
    if( ctbPars.modeIdc == SAO_MODE_OFF )
    {
      m_BinEncoder.encodeBin  ( 0, Ctx::SaoTypeIdx() );
    }
    else if( ctbPars.typeIdc == SAO_TYPE_BO )
    {
      m_BinEncoder.encodeBin  ( 1, Ctx::SaoTypeIdx() );
      m_BinEncoder.encodeBinEP( 0 );
    }
    else
    {
      CHECK(!( ctbPars.typeIdc < SAO_TYPE_START_BO ), "Unspecified error");
      m_BinEncoder.encodeBin  ( 1, Ctx::SaoTypeIdx() );
      m_BinEncoder.encodeBinEP( 1 );
    }
  }

  if( ctbPars.modeIdc == SAO_MODE_NEW )
  {
    const int maxOffsetQVal = SampleAdaptiveOffset::getMaxOffsetQVal( bitDepth );
    int       numClasses    = ( ctbPars.typeIdc == SAO_TYPE_BO ? 4 : NUM_SAO_EO_CLASSES );
    int       k             = 0;
    int       offset[4];
    for( int i = 0; i < numClasses; i++ )
    {
      if( ctbPars.typeIdc != SAO_TYPE_BO && i == SAO_CLASS_EO_PLAIN )
      {
        continue;
      }
      int classIdx = ( ctbPars.typeIdc == SAO_TYPE_BO ? ( ctbPars.typeAuxInfo + i ) % NUM_SAO_BO_CLASSES : i );
      offset[k++]  = ctbPars.offset[classIdx];
    }

    // sao_offset_abs
    for( int i = 0; i < 4; i++ )
    {
      unsigned absOffset = ( offset[i] < 0 ? -offset[i] : offset[i] );
      unary_max_eqprob( absOffset, maxOffsetQVal );
    }

    // band offset mode
    if( ctbPars.typeIdc == SAO_TYPE_BO )
    {
      // sao_offset_sign
      for( int i = 0; i < 4; i++ )
      {
        if( offset[i] )
        {
          m_BinEncoder.encodeBinEP( (offset[i] < 0) );
        }
      }
      // sao_band_position
      m_BinEncoder.encodeBinsEP( ctbPars.typeAuxInfo, NUM_SAO_BO_CLASSES_LOG2 );
    }
    // edge offset mode
    else
    {
      if( isFirstCompOfChType )
      {
        // sao_eo_class_luma / sao_eo_class_chroma
        CHECK( ctbPars.typeIdc - SAO_TYPE_START_EO < 0, "sao edge offset class is outside valid range" );
        m_BinEncoder.encodeBinsEP( ctbPars.typeIdc - SAO_TYPE_START_EO, NUM_SAO_EO_TYPES_LOG2 );
      }
    }
  }
}

#if JEM_TOOLS
#if !JVET_K0371_ALF
int CABACWriter::alf_lengthGolomb(int coeffVal, int k)
{
  int m = 2 << (k - 1);
  int q = coeffVal / m;
  if(coeffVal != 0)
    return(q + 2 + k);
  else
    return(q + 1 + k);
}

void CABACWriter::codeAlfUvlc( uint32_t uiCode )
{
  int i;
  if ( uiCode == 0 )
  {
    m_BinEncoder.encodeBinEP(0);
  }
  else
  {
    m_BinEncoder.encodeBinEP(1);
    for ( i=0; i<uiCode-1; i++ )
    {
      m_BinEncoder.encodeBinEP(1);
    }
    m_BinEncoder.encodeBinEP(0);
  }
}

void CABACWriter::codeAlfSvlc( int iCode )
{
  int i;
  if ( iCode == 0 )
    {
    m_BinEncoder.encodeBinEP(0);
    }
    else
    {
      m_BinEncoder.encodeBinEP(1);

      // write sign
      if ( iCode > 0 )
      {
        m_BinEncoder.encodeBinEP(0);
      }
      else
      {
        m_BinEncoder.encodeBinEP(1);
        iCode = -iCode;
      }

      // write magnitude
      for ( i=0; i<iCode-1; i++ )
      {
        m_BinEncoder.encodeBinEP(1);
      }
      m_BinEncoder.encodeBinEP(0);
    }
}
#endif

void CABACWriter::xWriteTruncBinCode(uint32_t uiSymbol, uint32_t uiMaxSymbol)
{
  uint32_t uiThresh;
  if (uiMaxSymbol > 256)
  {
    uint32_t uiThreshVal = 1 << 8;
    uiThresh = 8;
    while (uiThreshVal <= uiMaxSymbol)
    {
      uiThresh++;
      uiThreshVal <<= 1;
    }
    uiThresh--;
  }
#if JEM_TOOLS
  else
  {
    uiThresh = g_NonMPM[uiMaxSymbol];
  }
#endif

  uint32_t uiVal = 1 << uiThresh;
  assert(uiVal <= uiMaxSymbol);
  assert((uiVal << 1) > uiMaxSymbol);
  assert(uiSymbol < uiMaxSymbol);
  uint32_t b = uiMaxSymbol - uiVal;
  assert(b < uiVal);
  if (uiSymbol < uiVal - b)
  {
    m_BinEncoder.encodeBinsEP(uiSymbol, uiThresh);
  }
  else
  {
    uiSymbol += uiVal - b;
    assert(uiSymbol < (uiVal << 1));
    assert((uiSymbol >> 1) >= uiVal - b);
    m_BinEncoder.encodeBinsEP(uiSymbol, uiThresh + 1);
  }
}

#if !JVET_K0371_ALF
#if JVET_C0038_NO_PREV_FILTERS
void CABACWriter::xWriteEpExGolomb(uint32_t uiSymbol, uint32_t uiCount)
{
  uint32_t bins = 0;
  int numBins = 0;

  while (uiSymbol >= (uint32_t)(1 << uiCount))
  {
    bins = 2 * bins + 1;
    numBins++;
    uiSymbol -= 1 << uiCount;
    uiCount++;
  }
  bins = 2 * bins + 0;
  numBins++;

  bins = (bins << uiCount) | uiSymbol;
  numBins += uiCount;

  assert(numBins <= 32);
  m_BinEncoder.encodeBinsEP(bins, numBins);
}
#endif

void CABACWriter::alfGolombEncode(int coeff, int k)
{
  int q, i, m;
  int symbol = abs(coeff);

  m = (int)pow(2.0, k);
  q = symbol / m;

  for (i = 0; i < q; i++)
    m_BinEncoder.encodeBinEP(1);
  m_BinEncoder.encodeBinEP(0);
  // write one zero

  for(i = 0; i < k; i++)
  {
    m_BinEncoder.encodeBinEP(symbol & 0x01);
    symbol >>= 1;
  }

  if(coeff != 0)
  {
    int sign = (coeff > 0)? 1: 0;
    m_BinEncoder.encodeBinEP(sign);
  }
}

void CABACWriter::alf( const Slice& slice , const ALFParam& alfParam )
{
  const SPSNext& spsNext = slice.getSPS()->getSpsNext();
  if( !spsNext.getALFEnabled() )
  {
    return;
  }
  alf( alfParam, slice.getSliceType(), spsNext.getGALFEnabled() );
}

void CABACWriter::alf( const ALFParam& alfParam, SliceType sliceType, bool isGALF )
{
  //AdaptiveLoopFilter::printALFParam( &alfParam, false );
  m_BinEncoder.encodeBinEP( alfParam.alf_flag );
  if( !alfParam.alf_flag )
  {
    return;
  }


#if COM16_C806_ALF_TEMPPRED_NUM
  if( sliceType != I_SLICE )
  {
    m_BinEncoder.encodeBinEP( alfParam.temporalPredFlag );
  }
  else
  {
    CHECK( alfParam.temporalPredFlag != 0, "ALF: Temporal prediction for I-Slices not allowed" );
  }
#endif
  if( !alfParam.temporalPredFlag )
  {
    alf_aux    ( alfParam, isGALF );
    alf_filter ( alfParam, isGALF );
  }
  else
  {
    codeAlfUvlc( alfParam.prevIdx );
  }



  if( isGALF )
  {
    codeAlfUvlc(alfParam.chroma_idc);

    if (alfParam.chroma_idc && !alfParam.temporalPredFlag)
    {
      alf_filter(alfParam, isGALF, true);
    }
  }
  else
  {
    alf_chroma ( alfParam );
  }

  alf_cu_ctrl( alfParam );
}

void CABACWriter::alf_aux( const ALFParam& alfParam, bool isGALF )
{
  int iNoVarBins = AdaptiveLoopFilter::m_NO_VAR_BINS;
#if GALF
  xWriteTruncBinCode((uint32_t)(alfParam.filters_per_group - 1), (uint32_t)iNoVarBins);
#endif

  //FilterType
  codeAlfUvlc( alfParam.filterType );

  //FilterMode
  if( isGALF )
  {
    //int iNoVarBins = AdaptiveLoopFilter::m_NO_VAR_BINS;
    //xWriteTruncBinCode((uint32_t)(alfParam.filters_per_group - 1), (uint32_t)iNoVarBins);
    if (alfParam.filters_per_group > 1)
    {
      for (int i = 0; i < iNoVarBins; i++)
      {
        xWriteTruncBinCode((uint32_t)alfParam.filterPattern[i], (uint32_t)alfParam.filters_per_group);
      }
    }

#if JVET_C0038_NO_PREV_FILTERS
    int i;
    int availableFilters = alfParam.iAvailableFilters;
    uint8_t codetab_pred[3] = { 1, 0, 2 };

    if (availableFilters > 0)
    {
      //codeAlfUvlc(codetab_pred[alfParam.iPredPattern]); //correct way to encode ??
      xWriteEpExGolomb(codetab_pred[alfParam.iPredPattern],0); //0: all zero, no pred from pre-defined filters; 1: all are predicted but could be different values; 2: some predicted and some not
      if (alfParam.iPredPattern == 2)
      {
        for (i = 0; i < iNoVarBins; i++)
        {
          m_BinEncoder.encodeBinEP(alfParam.PrevFiltIdx[i]>0 ? 1 : 0);
        }
      }
      if (alfParam.iPredPattern > 0 && availableFilters > 1)
      {
        for (i = 0; i < iNoVarBins; i++)
        {
          if (alfParam.PrevFiltIdx[i] > 0)
          {
            xWriteTruncBinCode((uint8_t)alfParam.PrevFiltIdx[i] - 1, availableFilters);
          }
        }
      }
    }
#endif
  }
  else
  {
    codeAlfUvlc( alfParam.filterMode );
    if( alfParam.filterMode == ALF_TWO_FILTERS )
    {
      codeAlfUvlc( alfParam.startSecondFilter );
    }
    else if ( alfParam.filterMode == ALF_MULTIPLE_FILTERS )
    {
      for( int i=1; i< AdaptiveLoopFilter::m_NO_VAR_BINS; i++)
      {
        m_BinEncoder.encodeBinEP( alfParam.filterPattern[i] );
      }
    }
  }
}

void CABACWriter::alf_filter( const ALFParam& alfParam, bool isGALF, bool bChroma )
{
  int filters_per_group;
  int sqrFiltLength;
  int filtType;
  if( isGALF )
  {
    filters_per_group = bChroma ? 1: alfParam.filters_per_group;
    sqrFiltLength = bChroma ? alfParam.num_coeff_chroma : alfParam.num_coeff;
    sqrFiltLength = sqrFiltLength - 1;
    filtType = bChroma ? (alfParam.tap_chroma == 5 ? 0 : (alfParam.tap_chroma == 7 ? 1 : 2)) : alfParam.filterType;

    if (!bChroma)
    {
  #if FORCE0
      m_BinEncoder.encodeBinEP(alfParam.forceCoeff0);
      if (!alfParam.forceCoeff0)
      {
  #endif
        if (filters_per_group > 1)
        {
          m_BinEncoder.encodeBinEP(alfParam.predMethod);
        }
  #if FORCE0
      }
  #endif
    }
  }
  else
  {
    filters_per_group  = alfParam.filters_per_group;
    sqrFiltLength          = alfParam.num_coeff;
    filtType               = alfParam.filterType;

    if( filters_per_group > 1 )
    {
      m_BinEncoder.encodeBinEP( alfParam.predMethod );
    }
  }

  int i, k, kMin, kStart, minBits, ind, scanPos, maxScanVal, coeffVal;

  int kMinTab[ AdaptiveLoopFilter::m_MAX_SQR_FILT_LENGTH], bitsCoeffScan[ AdaptiveLoopFilter::m_MAX_SCAN_VAL][AdaptiveLoopFilter::m_MAX_EXP_GOLOMB],
      minKStart, minBitsKStart, bitsKStart;

  maxScanVal = 0;
  const int* pDepthInt = AdaptiveLoopFilter::m_pDepthIntTab[filtType];
  for(i = 0; i < sqrFiltLength; i++ )
  {
    maxScanVal = max(maxScanVal, pDepthInt[i]);
  }
  // vlc for all
  memset( bitsCoeffScan, 0, AdaptiveLoopFilter::m_MAX_SCAN_VAL * AdaptiveLoopFilter::m_MAX_EXP_GOLOMB * sizeof(int));
  for(ind=0; ind<filters_per_group; ++ind)
  {
#if FORCE0
    if (bChroma || alfParam.codedVarBins[ind] || !alfParam.forceCoeff0)
    {
#endif
      for (i = 0; i < sqrFiltLength; i++)
      {
        scanPos = pDepthInt[i] - 1;
        coeffVal = abs((isGALF && bChroma) ? alfParam.coeff_chroma[i] : alfParam.coeffmulti[ind][i]);

        for (k = 1; k < 15; k++)
        {
          bitsCoeffScan[scanPos][k] += alf_lengthGolomb(coeffVal, k);
        }
      }
#if FORCE0
    }
#endif
  }
  minBitsKStart =  0;
  minKStart     = -1;
  for(k = 1; k < 8; k++)
  {
    bitsKStart = 0;
    kStart = k;
    for(scanPos = 0; scanPos < maxScanVal; scanPos++)
    {
      kMin = kStart;
      minBits = bitsCoeffScan[scanPos][kMin];

      if(bitsCoeffScan[scanPos][kStart+1] < minBits)
      {
        kMin = kStart + 1;
        minBits = bitsCoeffScan[scanPos][kMin];
      }
      kStart = kMin;
      bitsKStart += minBits;
    }
    if((bitsKStart < minBitsKStart) || (k == 1))
    {
      minBitsKStart = bitsKStart;
      minKStart = k;
    }
  }

  kStart = minKStart;
  for(scanPos = 0; scanPos < maxScanVal; scanPos++)
  {
    kMin = kStart;
    minBits = bitsCoeffScan[scanPos][kMin];

    if(bitsCoeffScan[scanPos][kStart+1] < minBits)
    {
      kMin = kStart + 1;
      minBits = bitsCoeffScan[scanPos][kMin];
    }

    kMinTab[scanPos] = kMin;
    kStart = kMin;
  }

  // Golomb parameters
  codeAlfUvlc( minKStart -1 );
  int golombIndexBit;
  kMin = minKStart;
  for( scanPos = 0; scanPos < maxScanVal; scanPos++)
  {
     golombIndexBit = (kMinTab[scanPos] != kMin)? 1: 0;
     CHECK( !(kMinTab[scanPos] <= kMin + 1), "ALF Golomb parameter not consistent" );
     m_BinEncoder.encodeBinEP(golombIndexBit);
     kMin = kMinTab[scanPos];
  }
#if FORCE0
  if (!bChroma)
  {
    if (alfParam.forceCoeff0)
    {
      for (ind = 0; ind < filters_per_group; ++ind)
      {
        m_BinEncoder.encodeBinEP(alfParam.codedVarBins[ind]);
      }
    }
  }
#endif
  // Filter coefficients
  if( isGALF )
  {
    if (bChroma)
    {
      for (i = 0; i < sqrFiltLength; i++)
      {
        scanPos = pDepthInt[i] - 1;
        alfGolombEncode(alfParam.coeff_chroma[i], kMinTab[scanPos]);
      }
    }
    else
    {
      for (ind = 0; ind < alfParam.filters_per_group; ++ind)
      {
  #if FORCE0
        if ((alfParam.codedVarBins[ind] == 0) && alfParam.forceCoeff0 )
        {
          continue;
        }
  #endif
        for (i = 0; i < sqrFiltLength; i++)
        {
          scanPos = pDepthInt[i] - 1;
          alfGolombEncode(alfParam.coeffmulti[ind][i], kMinTab[scanPos]);
        }
      }
    }
  }
  else
  {
    for (ind = 0; ind < alfParam.filters_per_group; ++ind)
    {
      for (i = 0; i < sqrFiltLength; i++)
      {
        scanPos = pDepthInt[i] - 1;
        alfGolombEncode(alfParam.coeffmulti[ind][i], kMinTab[scanPos]);
      }
    }
  }
}

void CABACWriter::alf_cu_ctrl( const ALFParam& alfParam )
{
  m_BinEncoder.encodeBinEP( alfParam.cu_control_flag );
  if( alfParam.cu_control_flag)
  {
    unary_max_symbol( alfParam.alf_max_depth, Ctx::AlfUvlcSCModel(0),Ctx::AlfUvlcSCModel(1), alfParam.maxCodingDepth-1);
    uint32_t uiLength = 0;

    uint32_t maxValue = ( alfParam.num_ctus_in_frame << (alfParam.alf_max_depth*2) );
    uint32_t minValue = alfParam.num_ctus_in_frame;
    CHECK( maxValue < alfParam.num_alf_cu_flag, "ALF: wrong max value for number of CUs" );
    CHECK( minValue > alfParam.num_alf_cu_flag, "ALF: wrong min value for number of CUs" );
    uint32_t temp = maxValue - minValue;
    for(uint32_t i=0; i<32; i++)
    {
      if(temp&0x1)
      {
        uiLength = i+1;
      }
      temp = (temp >> 1);
    }
    uint32_t uiSymbol = alfParam.num_alf_cu_flag - minValue;
    if(uiLength)
    {
      while( uiLength-- )
      {
        m_BinEncoder.encodeBinEP( (uiSymbol>>uiLength) & 0x1 );
      }
    }

    DTRACE( g_trace_ctx, D_SYNTAX, "alf_cu_ctrl() max_depth=%d max_alf_depth=%d num_cu_flags=%d\n", alfParam.maxCodingDepth, alfParam.alf_max_depth, alfParam.num_alf_cu_flag );

    for(uint32_t i=0; i< alfParam.num_alf_cu_flag; i++)
    {
      m_BinEncoder.encodeBin( alfParam.alf_cu_flag[i], Ctx::AlfCUCtrlFlags( 0 ) );

      DTRACE( g_trace_ctx, D_SYNTAX, "alf_cu_ctrl() blk=%d alf_cu_flag[blk]=%d\n", i, alfParam.alf_cu_flag[i] );
    }
  }
  else
  {
    DTRACE( g_trace_ctx, D_SYNTAX, "alf_cu_ctrl() off\n" );
  }
}

void CABACWriter::alf_chroma( const ALFParam& alfParam )
{
  codeAlfUvlc( alfParam.chroma_idc );
  if( alfParam.chroma_idc && !alfParam.temporalPredFlag )
  {
    codeAlfUvlc((alfParam.tap_chroma-5)/2);
    // filter coefficients for chroma
    for(int pos=0; pos<alfParam.num_coeff_chroma; pos++)
    {
      codeAlfSvlc( alfParam.coeff_chroma[pos] );
    }
  }
}
#endif
#endif


//================================================================================
//  clause 7.3.8.4
//--------------------------------------------------------------------------------
//    void  coding_tree       ( cs, partitioner, cuCtx )
//    void  split_cu_flag     ( split, cs, partitioner )
//    void  split_cu_mode_mt  ( split, cs, partitioner )
//================================================================================

#if JVET_K0230_DUAL_CODING_TREE_UNDER_64x64_BLOCK
void CABACWriter::coding_tree(const CodingStructure& cs, Partitioner& partitioner, CUCtx& cuCtx, Partitioner* pPartitionerChroma, CUCtx* pCuCtxChroma)
#else
void CABACWriter::coding_tree( const CodingStructure& cs, Partitioner& partitioner, CUCtx& cuCtx )
#endif
{
  const PPS      &pps         = *cs.pps;
  const UnitArea &currArea    = partitioner.currArea();
  const CodingUnit &cu        = *cs.getCU( currArea.blocks[partitioner.chType], partitioner.chType );

  // Reset delta QP coding flag and ChromaQPAdjustemt coding flag
  if( pps.getUseDQP() && partitioner.currDepth <= pps.getMaxCuDQPDepth() )
  {
    cuCtx.isDQPCoded          = false;
  }
  if( cs.slice->getUseChromaQpAdj() && partitioner.currDepth <= pps.getPpsRangeExtension().getDiffCuChromaQpOffsetDepth() )
  {
    cuCtx.isChromaQpAdjCoded  = false;
  }
#if JVET_K0230_DUAL_CODING_TREE_UNDER_64x64_BLOCK
  // Reset delta QP coding flag and ChromaQPAdjustemt coding flag
  if (CS::isDualITree(cs) && pPartitionerChroma != nullptr)
  {
    if (pps.getUseDQP() && pPartitionerChroma->currDepth <= pps.getMaxCuDQPDepth())
    {
      pCuCtxChroma->isDQPCoded = false;
    }
    if (cs.slice->getUseChromaQpAdj() && pPartitionerChroma->currDepth <= pps.getPpsRangeExtension().getDiffCuChromaQpOffsetDepth())
    {
      pCuCtxChroma->isChromaQpAdjCoded = false;
    }
  }
#endif

  const PartSplit implicitSplit = partitioner.getImplicitSplit( cs );

  // QT
  bool canQtSplit = partitioner.canSplit( CU_QUAD_SPLIT, cs );

  if( canQtSplit )
  {
    // split_cu_flag
    bool qtSplit = implicitSplit == CU_QUAD_SPLIT;

    if( !qtSplit && implicitSplit != CU_QUAD_SPLIT )
    {
      qtSplit = ( cu.qtDepth > partitioner.currQtDepth );
      split_cu_flag( qtSplit, cs, partitioner );
    }

    // quad-tree split
    if( qtSplit )
    {
#if JVET_K0230_DUAL_CODING_TREE_UNDER_64x64_BLOCK
      if (CS::isDualITree(cs) && pPartitionerChroma != nullptr && (partitioner.currArea().lwidth() >= 64 || partitioner.currArea().lheight() >= 64))
      {
        partitioner.splitCurrArea(CU_QUAD_SPLIT, cs);
        pPartitionerChroma->splitCurrArea(CU_QUAD_SPLIT, cs);
        bool beContinue = true;
        bool lumaContinue = true;
        bool chromaContinue = true;

        while (beContinue)
        {
          if (partitioner.currArea().lwidth() > 64 || partitioner.currArea().lheight() > 64)
          {
            if (cs.picture->blocks[partitioner.chType].contains(partitioner.currArea().blocks[partitioner.chType].pos()))
            {
              coding_tree(cs, partitioner, cuCtx, pPartitionerChroma, pCuCtxChroma);
            }
            lumaContinue = partitioner.nextPart(cs);
            chromaContinue = pPartitionerChroma->nextPart(cs);
            CHECK(lumaContinue != chromaContinue, "luma chroma partition should be matched");
            beContinue = lumaContinue;
          }
          else
          {
            //dual tree coding under 64x64 block
            if (cs.picture->blocks[partitioner.chType].contains(partitioner.currArea().blocks[partitioner.chType].pos()))
            {
              coding_tree(cs, partitioner, cuCtx);
            }
            lumaContinue = partitioner.nextPart(cs);
            if (cs.picture->blocks[pPartitionerChroma->chType].contains(pPartitionerChroma->currArea().blocks[pPartitionerChroma->chType].pos()))
            {
              coding_tree(cs, *pPartitionerChroma, *pCuCtxChroma);
            }
            chromaContinue = pPartitionerChroma->nextPart(cs);
            CHECK(lumaContinue != chromaContinue, "luma chroma partition should be matched");
            beContinue = lumaContinue;
          }
        }
        partitioner.exitCurrSplit();
        pPartitionerChroma->exitCurrSplit();

      }
      else
      {
#endif
      partitioner.splitCurrArea( CU_QUAD_SPLIT, cs );

      do
      {
        if( cs.picture->blocks[partitioner.chType].contains( partitioner.currArea().blocks[partitioner.chType].pos() ) )
        {
          coding_tree( cs, partitioner, cuCtx );
        }
      } while( partitioner.nextPart( cs ) );

      partitioner.exitCurrSplit();
#if JVET_K0230_DUAL_CODING_TREE_UNDER_64x64_BLOCK
      }
#endif
      return;
    }
  }

  {
#if !JVET_K0554
    //// MT
    //bool mtSplit = partitioner.canSplit( CU_MT_SPLIT, cs );

    //if( mtSplit )
    // MT
#endif
    bool mtSplit = partitioner.canSplit( CU_MT_SPLIT, cs );

    if( mtSplit )
    {
      const PartSplit splitMode = CU::getSplitAtDepth( cu, partitioner.currDepth );

#if JVET_K0554
      split_cu_mode_mt( splitMode, cs, partitioner );
#else
      CHECK( implicitSplit != CU_DONT_SPLIT && implicitSplit != splitMode, "Different split found than the implicit split" );

      if( implicitSplit == CU_DONT_SPLIT )
      {
        split_cu_mode_mt( splitMode, cs, partitioner );
      }
#endif

      if( splitMode != CU_DONT_SPLIT )
      {
        partitioner.splitCurrArea( splitMode, cs );
        do
        {
          if( cs.picture->blocks[partitioner.chType].contains( partitioner.currArea().blocks[partitioner.chType].pos() ) )
          {
            coding_tree( cs, partitioner, cuCtx );
          }
        } while( partitioner.nextPart( cs ) );

        partitioner.exitCurrSplit();
        return;
      }
    }
  }

  // Predict QP on start of quantization group
  if( pps.getUseDQP() && !cuCtx.isDQPCoded && CU::isQGStart( cu ) )
  {
    cuCtx.qp = CU::predictQP( cu, cuCtx.qp );
  }

#if JVET_K0346
  if (!cs.slice->isIntra() && m_EncCu)
  {
    PredictionUnit& pu = *cu.firstPU;
#if JEM_TOOLS
    if (pu.mergeFlag && (pu.mergeType == MRG_TYPE_SUBPU_ATMVP || pu.mergeType == MRG_TYPE_SUBPU_ATMVP_EXT))
#else
    if (pu.mergeFlag && (pu.mergeType == MRG_TYPE_SUBPU_ATMVP))
#endif
    {
      unsigned int layerId = cs.slice->getDepth();
      m_EncCu->incrementSubMergeBlkSize(layerId, cu.Y().width*cu.Y().height);
      m_EncCu->incrementSubMergeBlkNum(layerId, 1);
    }
  }
#endif

  // coding unit
  coding_unit( cu, partitioner, cuCtx );

  DTRACE_COND( ( isEncoding() ), g_trace_ctx, D_QP, "x=%d, y=%d, w=%d, h=%d, qp=%d\n", cu.Y().x, cu.Y().y, cu.Y().width, cu.Y().height, cu.qp );
  DTRACE_BLOCK_REC_COND( ( !isEncoding() ), cs.picture->getRecoBuf( cu ), cu, cu.predMode );
}

void CABACWriter::split_cu_flag( bool split, const CodingStructure& cs, Partitioner& partitioner )
{
  unsigned maxQTDepth = ( cs.sps->getSpsNext().getUseQTBT()
    ? g_aucLog2[cs.sps->getSpsNext().getCTUSize()] - g_aucLog2[cs.sps->getSpsNext().getMinQTSize( cs.slice->getSliceType(), partitioner.chType )]
    : cs.sps->getLog2DiffMaxMinCodingBlockSize() );
#if !JVET_K0554
//#else
//  unsigned maxQTDepth = ( cs.sps->getSpsNext().getUseQTBT()
//    ? g_aucLog2[cs.sps->getSpsNext().getCTUSize()] - g_aucLog2[cs.sps->getSpsNext().getMinQTSize( cs.slice->getSliceType(), partitioner.chType )]
//    : cs.sps->getLog2DiffMaxMinCodingBlockSize() );
//#endif
#endif
  if( partitioner.currDepth == maxQTDepth )
  {
    return;
  }
  unsigned  ctxId = DeriveCtx::CtxCUsplit( cs, partitioner );
  m_BinEncoder.encodeBin( (split), Ctx::SplitFlag(ctxId) );

  DTRACE( g_trace_ctx, D_SYNTAX, "split_cu_flag() ctx=%d split=%d\n", ctxId, split ? 1 : 0 );
}

void CABACWriter::split_cu_mode_mt(const PartSplit split, const CodingStructure& cs, Partitioner& partitioner)
{
  unsigned ctxIdBT = DeriveCtx::CtxBTsplit( cs, partitioner );

  unsigned width   = partitioner.currArea().lumaSize().width;
  unsigned height  = partitioner.currArea().lumaSize().height;

  DecisionTree dt( g_mtSplitDTT );

#if HM_QTBT_AS_IN_JEM_SYNTAX
  unsigned minBTSize = cs.slice->isIntra() ? ( partitioner.chType == 0 ? MIN_BT_SIZE : MIN_BT_SIZE_C ) : MIN_BT_SIZE_INTER;

  dt.setAvail( DTT_SPLIT_BT_HORZ,  height > minBTSize && ( partitioner.canSplit( CU_HORZ_SPLIT, cs ) || width  == minBTSize ) );
  dt.setAvail( DTT_SPLIT_BT_VERT,  width  > minBTSize && ( partitioner.canSplit( CU_VERT_SPLIT, cs ) || height == minBTSize ) );
#else                              
  dt.setAvail( DTT_SPLIT_BT_HORZ,  partitioner.canSplit( CU_HORZ_SPLIT, cs ) );
  dt.setAvail( DTT_SPLIT_BT_VERT,  partitioner.canSplit( CU_VERT_SPLIT, cs ) );
#endif

  dt.setAvail( DTT_SPLIT_TT_HORZ,  partitioner.canSplit( CU_TRIH_SPLIT, cs ) );
  dt.setAvail( DTT_SPLIT_TT_VERT,  partitioner.canSplit( CU_TRIV_SPLIT, cs ) );
#if JVET_K0554
  dt.setAvail( DTT_SPLIT_NO_SPLIT, partitioner.canSplit( CU_DONT_SPLIT, cs ) );
#endif

  unsigned btSCtxId = width == height ? 0 : ( width > height ? 1 : 2 );
  dt.setCtxId( DTT_SPLIT_DO_SPLIT_DECISION,   Ctx::BTSplitFlag( ctxIdBT ) );
  dt.setCtxId( DTT_SPLIT_HV_DECISION,         Ctx::BTSplitFlag( 3 + btSCtxId ) );

  dt.setCtxId( DTT_SPLIT_H_IS_BT_12_DECISION, Ctx::BTSplitFlag( 6 + btSCtxId ) );
  dt.setCtxId( DTT_SPLIT_V_IS_BT_12_DECISION, Ctx::BTSplitFlag( 9 + btSCtxId ) );


  encode_sparse_dt( dt, split == CU_DONT_SPLIT ? ( unsigned ) DTT_SPLIT_NO_SPLIT : ( unsigned ) split );

  DTRACE(g_trace_ctx, D_SYNTAX, "split_cu_mode_mt() ctx=%d split=%d\n", ctxIdBT, split);
}


//================================================================================
//  clause 7.3.8.5
//--------------------------------------------------------------------------------
//    void  coding_unit               ( cu, partitioner, cuCtx )
//    void  cu_transquant_bypass_flag ( cu )
//    void  cu_skip_flag              ( cu )
//    void  pred_mode                 ( cu )
//    void  part_mode                 ( cu )
//    void  pcm_flag                  ( cu )
//    void  pcm_samples               ( tu )
//    void  cu_pred_data              ( pus )
//    void  cu_lic_flag               ( cu )
//    void  intra_luma_pred_modes     ( pus )
//    void  intra_chroma_pred_mode    ( pu )
//    void  cu_residual               ( cu, partitioner, cuCtx )
//    void  rqt_root_cbf              ( cu )
//    void  end_of_ctu                ( cu, cuCtx )
//================================================================================

void CABACWriter::coding_unit( const CodingUnit& cu, Partitioner& partitioner, CUCtx& cuCtx )
{
  CodingStructure& cs = *cu.cs;
#if JVET_K0076_CPR
  cs.chType = partitioner.chType;
#endif
  // transquant bypass flag
  if( cs.pps->getTransquantBypassEnabledFlag() )
  {
    cu_transquant_bypass_flag( cu );
  }

  // skip flag
#if JVET_K0076_CPR_DT
  if (!cs.slice->isIntra() && cu.Y().valid())
#else
  if( !cs.slice->isIntra() )
#endif
  {
    cu_skip_flag( cu );
  }


  // skip data
  if( cu.skip )
  {
    CHECK( !cu.firstPU->mergeFlag, "Merge flag has to be on!" );
    PredictionUnit&   pu = *cu.firstPU;
    prediction_unit ( pu );
#if JEM_TOOLS
    cu_lic_flag     ( cu );
#endif
    end_of_ctu      ( cu, cuCtx );
    return;
  }

  // prediction mode and partitioning data
  pred_mode ( cu );
#if JEM_TOOLS
  pdpc_flag ( cu );
#endif

  // pcm samples
  if( CU::isIntra(cu) && cu.partSize == SIZE_2Nx2N )
  {
    pcm_data( cu );
    if( cu.ipcm )
    {
      end_of_ctu( cu, cuCtx );
      return;
    }
  }

  // prediction data ( intra prediction modes / reference indexes + motion vectors )
  cu_pred_data( cu );

  // residual data ( coded block flags + transform coefficient levels )
  cu_residual( cu, partitioner, cuCtx );

  // end of cu
  end_of_ctu( cu, cuCtx );
}


void CABACWriter::cu_transquant_bypass_flag( const CodingUnit& cu )
{
  m_BinEncoder.encodeBin( (cu.transQuantBypass), Ctx::TransquantBypassFlag() );
}


void CABACWriter::cu_skip_flag( const CodingUnit& cu )
{
  unsigned ctxId = DeriveCtx::CtxSkipFlag( cu );
  m_BinEncoder.encodeBin( ( cu.skip ), Ctx::SkipFlag( ctxId ) );

  DTRACE( g_trace_ctx, D_SYNTAX, "cu_skip_flag() ctx=%d skip=%d\n", ctxId, cu.skip ? 1 : 0 );
}


void CABACWriter::pred_mode( const CodingUnit& cu )
{
  if( cu.cs->slice->isIntra() )
  {
    return;
  }
  m_BinEncoder.encodeBin( ( CU::isIntra( cu ) ), Ctx::PredMode() );
}

void CABACWriter::pcm_data( const CodingUnit& cu )
{
  pcm_flag( cu );
  if( cu.ipcm )
  {
    m_BinEncoder.pcmAlignBits();
    pcm_samples( *cu.firstTU );
  }
}

#if JEM_TOOLS

void CABACWriter::pdpc_flag( const CodingUnit& cu )
{
  if (!cu.cs->sps->getSpsNext().isIntraPDPC() || cu.predMode == MODE_INTER)
  {
    return;
  }

  m_BinEncoder.encodeBin( cu.pdpc, Ctx::PdpcFlag() );
}
#endif

void CABACWriter::pcm_flag( const CodingUnit& cu )
{
  const SPS& sps = *cu.cs->sps;
  if( !sps.getUsePCM() || cu.lumaSize().width > (1 << sps.getPCMLog2MaxSize()) || cu.lumaSize().width < (1 << sps.getPCMLog2MinSize()) )
  {
    return;
  }
  m_BinEncoder.encodeBinTrm( cu.ipcm );
}


void CABACWriter::cu_pred_data( const CodingUnit& cu )
{
  if( CU::isIntra( cu ) )
  {
    intra_luma_pred_modes  ( cu );
    intra_chroma_pred_modes( cu );
    return;
  }
#if JVET_K0076_CPR_DT
  if (!cu.Y().valid()) // dual tree chroma CU
  {
    return;
  }
#endif 
  for( auto &pu : CU::traversePUs( cu ) )
  {
    prediction_unit( pu );
  }

#if JVET_K0357_AMVR
  imv_mode   ( cu );
#endif
#if JEM_TOOLS
  obmc_flag  ( cu );
  cu_lic_flag( cu );
#endif

#if JVET_K0248_GBI
  cu_gbi_flag( cu );
#endif
}

#if JEM_TOOLS
void CABACWriter::cu_lic_flag( const CodingUnit& cu )
{
  if( CU::isLICFlagPresent( cu ) )
  {
    m_BinEncoder.encodeBin( cu.LICFlag ? 1 : 0, Ctx::LICFlag() );
    DTRACE( g_trace_ctx, D_SYNTAX, "cu_lic_flag() lic_flag=%d\n", cu.LICFlag?1:0 );
  }
}

void CABACWriter::obmc_flag( const CodingUnit& cu )
{
  if( !cu.cs->sps->getSpsNext().getUseOBMC() )
  {
    return;
  }

  bool bCoded = CU::isObmcFlagCoded ( cu );

  if ( bCoded )
  {
    m_BinEncoder.encodeBin ( cu.obmcFlag ? 1 : 0, Ctx::ObmcFlag () );
  }

  DTRACE ( g_trace_ctx, D_SYNTAX, "obmc_flag() obmc=%d pos=(%d,%d)\n", cu.obmcFlag ? 1 : 0, cu.lumaPos ().x, cu.lumaPos ().y );
}
#endif

#if JVET_K0248_GBI
void CABACWriter::cu_gbi_flag( const CodingUnit& cu )
{
  if (!CU::isGBiIdxCoded(cu))
  {
    return;
  }

  CHECK( !(GBI_NUM > 1 && (GBI_NUM == 2 || (GBI_NUM & 0x01) == 1 ) ), " !( GBI_NUM > 1 && ( GBI_NUM == 2 || ( GBI_NUM & 0x01 ) == 1 ) ) ");
  const uint8_t gbiCodingIdx = (uint8_t)g_GbiCodingOrder[CU::getValidGbiIdx( cu )];

  int ctxId = 0;

  int32_t numGBi = ( cu.slice->getCheckLDC() ) ? 5 : 3;

  m_BinEncoder.encodeBin( (gbiCodingIdx == 0 ? 1 : 0 ), Ctx::GBiIdx( ctxId ) );

  if( numGBi > 2 && gbiCodingIdx != 0 )
  {
    uint32_t prefixNumBits = numGBi - 2;
    uint32_t step = 1;
    uint8_t prefixSymbol = gbiCodingIdx;

    int ctxIdGBi = 4;
    uint8_t idx = 1;
    for( int ui = 0; ui < prefixNumBits; ++ui )
    {
      if(prefixSymbol == idx)
      {
        m_BinEncoder.encodeBin( 1, Ctx::GBiIdx( ctxIdGBi ) );
        break;
      }
      else
      {
        m_BinEncoder.encodeBin( 0, Ctx::GBiIdx( ctxIdGBi ) );
        ctxIdGBi += step;
        idx += step;
      }
    }
  }

  DTRACE( g_trace_ctx, D_SYNTAX, "cu_gbi_flag() gbi_idx=%d\n", cu.GBiIdx ? 1 : 0 );
}
#endif

void CABACWriter::intra_luma_pred_modes( const CodingUnit& cu )
{
  if( !cu.Y().valid() )
  {
    return;
  }

  unsigned numMPMs   = cu.cs->pcv->numMPMs;
  int      numBlocks = CU::getNumPUs( cu );
  unsigned *mpm_preds  [4];
  unsigned mpm_idxs    [4];
  unsigned ipred_modes [4];

  const PredictionUnit* pu = cu.firstPU;
#if !INTRA67_3MPM
#if JEM_TOOLS
  const bool use65Ang = cu.cs->sps->getSpsNext().getUseIntra65Ang();
#endif
#endif

  // prev_intra_luma_pred_flag
  for( int k = 0; k < numBlocks; k++ )
  {
    unsigned*& mpm_pred   = mpm_preds[k];
    unsigned&  mpm_idx    = mpm_idxs[k];
    unsigned&  ipred_mode = ipred_modes[k];

    mpm_pred = ( unsigned* ) alloca( numMPMs * sizeof( unsigned ) );
    PU::getIntraMPMs( *pu, mpm_pred );

    ipred_mode = pu->intraDir[0];
    mpm_idx    = numMPMs;
    for( unsigned idx = 0; idx < numMPMs; idx++ )
    {
      if( ipred_mode == mpm_pred[idx] )
      {
        mpm_idx = idx;
        break;
      }
    }
    m_BinEncoder.encodeBin( mpm_idx < numMPMs, Ctx::IPredMode[0]() );

    pu = pu->next;
  }

  pu = cu.firstPU;

  // mpm_idx / rem_intra_luma_pred_mode
  for( int k = 0; k < numBlocks; k++ )
  {
    const unsigned& mpm_idx = mpm_idxs[k];
    if( mpm_idx < numMPMs )
    {
#if !INTRA67_3MPM
#if JEM_TOOLS
      if( use65Ang )
      {
        DecisionTree dt( g_intraLumaMpmDTT );

        dt.setCtxId( DTT_INTRA_MPM_ISGT_0, Ctx::IPredMode[0]( mpmCtx[mpm_preds[k][0]] ) );
        dt.setCtxId( DTT_INTRA_MPM_ISGT_1, Ctx::IPredMode[0]( mpmCtx[mpm_preds[k][1]] ) );
        dt.setCtxId( DTT_INTRA_MPM_ISGT_2, Ctx::IPredMode[0]( mpmCtx[mpm_preds[k][2]] ) );

        encode_sparse_dt( dt, mpm_idx );
      }
      else
#endif
#endif
      {
        m_BinEncoder.encodeBinEP( mpm_idx > 0 );
        if( mpm_idx )
        {
          m_BinEncoder.encodeBinEP( mpm_idx > 1 );
        }
      }
    }
    else
    {
      unsigned* mpm_pred   = mpm_preds[k];
      unsigned  ipred_mode = ipred_modes[k];

      // sorting of MPMs
      std::sort( mpm_pred, mpm_pred + numMPMs );

#if !INTRA67_3MPM
#if JEM_TOOLS
      if( use65Ang )
      {
        for( int idx = int( numMPMs ) - 1; idx >= 0; idx-- )
        {
          if( ipred_mode > mpm_pred[idx] )
          {
            ipred_mode--;
          }
        }

#if JVET_B0051_NON_MPM_MODE
        m_BinEncoder.encodeBin(((ipred_mode % 4) == 0) ? 1 : 0, Ctx::IPredMode[0](4)); // flag to indicate if it is selected mode or non-selected mode
        if (ipred_mode % 4 == 0)
        {
          m_BinEncoder.encodeBinsEP(ipred_mode >> 2, 4);  // selected mode is 4-bit FLC coded
        }
        else
        {
          ipred_mode -= ipred_mode >> 2;
          ipred_mode--;
          xWriteTruncBinCode(ipred_mode, 45);  // Non-selected mode is truncated binary coded
        }
#else
        int RealNumIntraMode = cu.cs->sps->getSpsNext().getRealNumIntraMode();
        if( ipred_mode < ( RealNumIntraMode - 8 ) ) { m_BinEncoder.encodeBinsEP( ipred_mode,      6 ); }
        else                                        { m_BinEncoder.encodeBinsEP( ipred_mode >> 2, 4 ); }
#endif
      }
      else
#endif
#endif
      {
#if INTRA67_3MPM
        for (int idx = int(numMPMs) - 1; idx >= 0; idx--)
        {
          if (ipred_mode > mpm_pred[idx])
          {
            ipred_mode--;
          }
        }
        CHECK(ipred_mode >= 64, "Incorrect mode");

        m_BinEncoder.encodeBinsEP(ipred_mode, 6);
#else
        CHECK( g_intraMode33to65AngMapping[g_intraMode65to33AngMapping[ipred_mode]] != ipred_mode, "Using an extended intra mode, although not enabled" );

        ipred_mode = g_intraMode65to33AngMapping[ipred_mode];
        for( int idx = int( numMPMs ) - 1; idx >= 0; idx-- )
        {
          if( ipred_mode > g_intraMode65to33AngMapping[mpm_pred[idx]] )
          {
            ipred_mode--;
          }
        }

        CHECK( ipred_mode >= 32, "Incorrect mode" );

        m_BinEncoder.encodeBinsEP( ipred_mode, 5 );
#endif
      }
    }

    DTRACE( g_trace_ctx, D_SYNTAX, "intra_luma_pred_modes() idx=%d pos=(%d,%d) mode=%d\n", k, pu->lumaPos().x, pu->lumaPos().y, pu->intraDir[0] );
    pu = pu->next;
  }
}


void CABACWriter::intra_luma_pred_mode( const PredictionUnit& pu )
{

  // prev_intra_luma_pred_flag
  unsigned  numMPMs  = pu.cs->pcv->numMPMs;
  unsigned *mpm_pred = ( unsigned* ) alloca( numMPMs * sizeof( unsigned ) );

#if !INTRA67_3MPM
#if JEM_TOOLS
  const bool use65Ang = pu.cs->sps->getSpsNext().getUseIntra65Ang();

#endif
#endif
  PU::getIntraMPMs( pu, mpm_pred );

  unsigned ipred_mode = pu.intraDir[0];
  unsigned mpm_idx = numMPMs;

  for( unsigned idx = 0; idx < numMPMs; idx++ )
  {
    if( ipred_mode == mpm_pred[idx] )
    {
      mpm_idx = idx;
      break;
    }
  }
  m_BinEncoder.encodeBin( mpm_idx < numMPMs, Ctx::IPredMode[0]() );

  // mpm_idx / rem_intra_luma_pred_mode
  if( mpm_idx < numMPMs )
  {
#if !INTRA67_3MPM
#if JEM_TOOLS
    if( use65Ang )
    {
      DecisionTree dt( g_intraLumaMpmDTT );

      dt.setCtxId( DTT_INTRA_MPM_ISGT_0, Ctx::IPredMode[0]( mpmCtx[mpm_pred[0]] ) );
      dt.setCtxId( DTT_INTRA_MPM_ISGT_1, Ctx::IPredMode[0]( mpmCtx[mpm_pred[1]] ) );
      dt.setCtxId( DTT_INTRA_MPM_ISGT_2, Ctx::IPredMode[0]( mpmCtx[mpm_pred[2]] ) );

      encode_sparse_dt( dt, mpm_idx );
    }
    else
#endif
#endif
    {
      m_BinEncoder.encodeBinEP( mpm_idx > 0 );
      if( mpm_idx )
      {
        m_BinEncoder.encodeBinEP( mpm_idx > 1 );
      }
    }
  }
  else
  {
    std::sort( mpm_pred, mpm_pred + numMPMs );
#if !INTRA67_3MPM
#if JEM_TOOLS
    if( use65Ang )
    {
      for( int idx = int( numMPMs ) - 1; idx >= 0; idx-- )
      {
        if( ipred_mode > mpm_pred[idx] )
        {
          ipred_mode--;
        }
      }
#if JVET_B0051_NON_MPM_MODE
      m_BinEncoder.encodeBin(((ipred_mode % 4) == 0) ? 1 : 0, Ctx::IPredMode[0](4)); // flag to indicate if it is selected mode or non-selected mode
      if (ipred_mode % 4 == 0)
      {
        m_BinEncoder.encodeBinsEP(ipred_mode >> 2, 4);  // selected mode is 4-bit FLC coded
      }
      else
      {
        ipred_mode -= ipred_mode >> 2;
        ipred_mode--;
        xWriteTruncBinCode(ipred_mode, 45);  // Non-selected mode is truncated binary coded
      }
#else
      int RealNumIntraMode = pu.cs->sps->getSpsNext().getRealNumIntraMode();
      if( ipred_mode < ( RealNumIntraMode - 8 ) ) { m_BinEncoder.encodeBinsEP( ipred_mode, 6 ); }
      else                                        { m_BinEncoder.encodeBinsEP( ( ipred_mode >> 2 ), 4 ); }
#endif
    }
    else
#endif
#endif
    {
#if INTRA67_3MPM
      for (int idx = int(numMPMs) - 1; idx >= 0; idx--)
      {
        if (ipred_mode > mpm_pred[idx])
        {
          ipred_mode--;
        }
      }
      m_BinEncoder.encodeBinsEP(ipred_mode, 6);
#else
      CHECK( g_intraMode33to65AngMapping[g_intraMode65to33AngMapping[ipred_mode]] != ipred_mode, "Using an extended intra mode, although not enabled" );

      ipred_mode = g_intraMode65to33AngMapping[ipred_mode];
      for( int idx = int( numMPMs ) - 1; idx >= 0; idx-- )
      {
        if( ipred_mode > g_intraMode65to33AngMapping[mpm_pred[idx]] )
        {
          ipred_mode--;
        }
      }

      m_BinEncoder.encodeBinsEP( ipred_mode, 5 );
#endif
    }
  }
}


void CABACWriter::intra_chroma_pred_modes( const CodingUnit& cu )
{
  if( cu.chromaFormat == CHROMA_400 || ( CS::isDualITree( *cu.cs ) && cu.chType == CHANNEL_TYPE_LUMA ) )
  {
    return;
  }

  const PredictionUnit* pu = cu.firstPU;

  intra_chroma_pred_mode( *pu );
}

#if JEM_TOOLS||JVET_K0190
void CABACWriter::intra_chroma_lmc_mode( const PredictionUnit& pu )
{
  const unsigned intraDir = pu.intraDir[1];
#if JEM_TOOLS&&!JVET_K0190
  if ( pu.cs->sps->getSpsNext().getUseMDMS() )
  {
    m_BinEncoder.encodeBin( PU::isLMCMode( intraDir ) ? 0 : 1, Ctx::IPredMode[1]( 0 ) );
    if( PU::isLMCMode( intraDir ) )
    {
      unsigned ctxId = 6;
      if ( PU::isMMLMEnabled( pu ) )
      {
        m_BinEncoder.encodeBin( intraDir == MMLM_CHROMA_IDX ? 1 : 0, Ctx::IPredMode[1]( ctxId++ ) );
      }
      if ( PU::isMFLMEnabled( pu ) )
      {
        if ( intraDir != MMLM_CHROMA_IDX )
        {
          m_BinEncoder.encodeBin( intraDir == LM_CHROMA_IDX ? 1 : 0, Ctx::IPredMode[1]( ctxId++ ) );
          if ( intraDir != LM_CHROMA_IDX )
          {
            int candId = intraDir - LM_CHROMA_F1_IDX;
            m_BinEncoder.encodeBin( (candId >> 1) & 1, Ctx::IPredMode[1]( ctxId++ ) );
            m_BinEncoder.encodeBin(  candId       & 1, Ctx::IPredMode[1]( ctxId++ ) );
          }
        }
      }
    }
  }
  else
  {
#endif
    int lmModeList[10];
    int maxSymbol = PU::getLMSymbolList( pu, lmModeList );
    int symbol    = -1;
    for ( int k = 0; k < LM_SYMBOL_NUM; k++ )
    {
      if ( lmModeList[k] == intraDir || ( lmModeList[k] == -1 && intraDir < LM_CHROMA_IDX ) )
      {
        symbol = k;
        break;
      }
    }
    CHECK( symbol < 0, "invalid symbol found" );

    unary_max_symbol( symbol, Ctx::IPredMode[1]( 2 ), Ctx::IPredMode[1]( 3 ), maxSymbol - 1 );
#if JEM_TOOLS&&!JVET_K0190
  }
#endif
}
#endif


void CABACWriter::intra_chroma_pred_mode( const PredictionUnit& pu )
{
  const unsigned intraDir = pu.intraDir[1];
#if JEM_TOOLS

  // DM chroma index
  if ( pu.cs->sps->getSpsNext().getUseMDMS() )
  {
  }
  else
#endif
  {
    if( intraDir == DM_CHROMA_IDX )
    {
      m_BinEncoder.encodeBin( 0, Ctx::IPredMode[1]( 1 ) );
      return;
    }
    m_BinEncoder.encodeBin( 1, Ctx::IPredMode[1]( 1 ) );
  }

#if JEM_TOOLS||JVET_K0190
  // LM chroma mode
  if( pu.cs->sps->getSpsNext().getUseLMChroma() )
  {
    intra_chroma_lmc_mode( pu );
    if ( PU::isLMCMode( intraDir ) )
    {
      return;
    }
  }

#endif
  // chroma candidate index
  unsigned chromaCandModes[ NUM_CHROMA_MODE ];
  PU::getIntraChromaCandModes( pu, chromaCandModes );

  int candId = 0;
  for ( ; candId < NUM_CHROMA_MODE; candId++ )
  {
    if( intraDir == chromaCandModes[ candId ] )
    {
      break;
    }
  }

  CHECK( candId >= NUM_CHROMA_MODE, "Chroma prediction mode index out of bounds" );
  CHECK( chromaCandModes[ candId ] == DM_CHROMA_IDX, "The intra dir cannot be DM_CHROMA for this path" );
#if JEM_TOOLS
  CHECK( PU::isLMCMode( chromaCandModes[ candId ] ), "The intra dir cannot be LM_CHROMA for this path" );

  if ( pu.cs->sps->getSpsNext().getUseMDMS() )
  {
    candId -= NUM_LMC_MODE;

#if HM_MDMS_CODELAST_AS_IN_JEM
    const unsigned lastId = NUM_DM_MODES; // cannot be reached -> always write last bin
#else
    const unsigned lastId = NUM_DM_MODES - 1;
#endif
    const bool codeLast = ( lastId > candId );

    unsigned ctxId = 1;
    while ( candId > 0 )
    {
      m_BinEncoder.encodeBin( 1, Ctx::IPredMode[1]( ctxId++ ) );
      candId -= 1;
    }
    if ( codeLast )
    {
      m_BinEncoder.encodeBin( 0, Ctx::IPredMode[1]( ctxId ) );
    }
  }
  else
#endif
  {
    m_BinEncoder.encodeBinsEP( candId, 2 );
  }
}


void CABACWriter::cu_residual( const CodingUnit& cu, Partitioner& partitioner, CUCtx& cuCtx )
{
  if( CU::isInter( cu ) )
  {
    PredictionUnit& pu = *cu.firstPU;
    if( !( ( cu.cs->pcv->noRQT || cu.partSize == SIZE_2Nx2N ) && pu.mergeFlag ) )
    {
      rqt_root_cbf( cu );
    }

    if( !cu.rootCbf )
    {
#if JEM_TOOLS
#endif
      return;
    }
  }


  ChromaCbfs chromaCbfs;
  transform_tree( *cu.cs, partitioner, cuCtx, chromaCbfs );
#if JEM_TOOLS
  residual_nsst_mode( cu, cuCtx );
#endif

#if (JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT) && !HM_EMT_NSST_AS_IN_JEM
  cu_emt_pertu_idx( cu );
#endif
}

#if (JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT) && !HM_EMT_NSST_AS_IN_JEM
void CABACWriter::cu_emt_pertu_idx( const CodingUnit& cu )
{
  bool anyCbf = false, anyNonTs = false;

  for( const auto &tu : CU::traverseTUs( cu ) )
  {
    anyCbf   |= tu.cbf[0] != 0;
    anyNonTs |= !tu.transformSkip[0];
  }

  if( !cu.cs->pcv->noRQT || !isLuma( cu.chType ) || cu.nsstIdx != 0 ||
      !( cu.cs->sps->getSpsNext().getUseIntraEMT() || cu.cs->sps->getSpsNext().getUseInterEMT() ) || !anyCbf || !anyNonTs )
  {
    return;
  }

  emt_cu_flag( cu );

  if( cu.emtFlag )
  {
    for( const auto &tu : CU::traverseTUs( cu ) )
    {
      if( CU::isIntra( cu ) )
      {
        if( TU::getNumNonZeroCoeffsNonTS( tu, true, false ) > g_EmtSigNumThr )
        {
          emt_tu_index( tu );
        }
        else
        {
          CHECK( tu.emtIdx != 0, "If the number of significant coefficients is <= g_EmtSigNumThr, then the tu index must be 0" );
        }
      }
      else
      {
        emt_tu_index( *cu.firstTU );
      }
    }
  }
}

#endif
void CABACWriter::rqt_root_cbf( const CodingUnit& cu )
{
  m_BinEncoder.encodeBin( cu.rootCbf, Ctx::QtRootCbf() );

  DTRACE( g_trace_ctx, D_SYNTAX, "rqt_root_cbf() ctx=0 root_cbf=%d pos=(%d,%d)\n", cu.rootCbf ? 1 : 0, cu.lumaPos().x, cu.lumaPos().y );
}


void CABACWriter::end_of_ctu( const CodingUnit& cu, CUCtx& cuCtx )
{
  const Slice*  slice             = cu.cs->slice;
#if HEVC_TILES_WPP
  const TileMap& tileMap          = *cu.cs->picture->tileMap;
  const int     currentCTUTsAddr  = tileMap.getCtuRsToTsAddrMap( CU::getCtuAddr( cu ) );
#else
  const int     currentCTUTsAddr  = CU::getCtuAddr( cu );
#endif
  const bool    isLastSubCUOfCtu  = CU::isLastSubCUOfCtu( cu );

  if ( isLastSubCUOfCtu
    && ( !CS::isDualITree( *cu.cs ) || cu.chromaFormat == CHROMA_400 || isChroma( cu.chType ) )
      )
  {
    cuCtx.isDQPCoded = ( cu.cs->pps->getUseDQP() && !cuCtx.isDQPCoded );

    // The 1-terminating bit is added to all streams, so don't add it here when it's 1.
    // i.e. when the slice segment CurEnd CTU address is the current CTU address+1.
#if HEVC_DEPENDENT_SLICES
    if( slice->getSliceSegmentCurEndCtuTsAddr() != currentCTUTsAddr + 1 )
#else
    if(slice->getSliceCurEndCtuTsAddr() != currentCTUTsAddr + 1)
#endif
    {
      m_BinEncoder.encodeBinTrm( 0 );
    }
  }
}





//================================================================================
//  clause 7.3.8.6
//--------------------------------------------------------------------------------
//    void  prediction_unit ( pu );
//    void  merge_flag      ( pu );
//    void  merge_idx       ( pu );
//    void  inter_pred_idc  ( pu );
//    void  ref_idx         ( pu, refList );
//    void  mvp_flag        ( pu, refList );
//================================================================================

void CABACWriter::prediction_unit( const PredictionUnit& pu )
{
#if ENABLE_SPLIT_PARALLELISM || ENABLE_WPP_PARALLELISM
  CHECK( pu.cacheUsed, "Processing a PU that should be in cache!" );
  CHECK( pu.cu->cacheUsed, "Processing a CU that should be in cache!" );

#endif
  if( pu.cu->skip )
  {
    CHECK( !pu.mergeFlag, "merge_flag must be true for skipped CUs" );
  }
  else
  {
    merge_flag( pu );
  }
  if( pu.mergeFlag )
  {
#if JEM_TOOLS
    fruc_mrg_mode( pu );
    affine_flag  ( *pu.cu );
#endif
#if !JEM_TOOLS && JVET_K_AFFINE
    affine_flag  ( *pu.cu );
#endif
    merge_idx    ( pu );
  }
  else
  {
    inter_pred_idc( pu );
#if JEM_TOOLS || JVET_K_AFFINE
    affine_flag   ( *pu.cu );
#endif
    if( pu.interDir != 2 /* PRED_L1 */ )
    {
      ref_idx     ( pu, REF_PIC_LIST_0 );
#if JEM_TOOLS
      if( pu.cu->affine )
      {
#if JVET_K0357_AMVR
        mvd_coding(pu.mvdAffi[REF_PIC_LIST_0][0], 0);
        mvd_coding(pu.mvdAffi[REF_PIC_LIST_0][1], 0);
#else
        mvd_coding(pu.mvdAffi[REF_PIC_LIST_0][0]);
        mvd_coding(pu.mvdAffi[REF_PIC_LIST_0][1]);
#endif
#if JVET_K0337_AFFINE_6PARA
        if ( pu.cu->affineType == AFFINEMODEL_6PARAM )
        {
#if JVET_K0357_AMVR
          mvd_coding(pu.mvdAffi[REF_PIC_LIST_0][2], 0);
#else
          mvd_coding(pu.mvdAffi[REF_PIC_LIST_0][2]);
#endif
        }
#endif
      }
      else
#elif JVET_K_AFFINE
      if ( pu.cu->affine )
      {
#if JVET_K0357_AMVR
        mvd_coding(pu.mvdAffi[REF_PIC_LIST_0][0], 0);
        mvd_coding(pu.mvdAffi[REF_PIC_LIST_0][1], 0);
#else
        mvd_coding(pu.mvdAffi[REF_PIC_LIST_0][0]);
        mvd_coding(pu.mvdAffi[REF_PIC_LIST_0][1]);
#endif
#if JVET_K0337_AFFINE_6PARA
        if ( pu.cu->affineType == AFFINEMODEL_6PARAM )
        {
#if JVET_K0357_AMVR
          mvd_coding(pu.mvdAffi[REF_PIC_LIST_0][2], 0);
#else
          mvd_coding(pu.mvdAffi[REF_PIC_LIST_0][2]);
#endif
        }
#endif
      }
      else
#endif
      {
#if JVET_K0357_AMVR
        mvd_coding( pu.mvd[REF_PIC_LIST_0], pu.cu->imv );
#else
        mvd_coding( pu.mvd[REF_PIC_LIST_0] );
#endif
      }
      mvp_flag    ( pu, REF_PIC_LIST_0 );
    }
    if( pu.interDir != 1 /* PRED_L0 */ )
    {
      ref_idx     ( pu, REF_PIC_LIST_1 );
      if( !pu.cs->slice->getMvdL1ZeroFlag() || pu.interDir != 3 /* PRED_BI */ )
      {
#if JEM_TOOLS
        if( pu.cu->affine )
        {
#if JVET_K0357_AMVR
          mvd_coding(pu.mvdAffi[REF_PIC_LIST_1][0], 0);
          mvd_coding(pu.mvdAffi[REF_PIC_LIST_1][1], 0);
#else
          mvd_coding(pu.mvdAffi[REF_PIC_LIST_1][0]);
          mvd_coding(pu.mvdAffi[REF_PIC_LIST_1][1]);
#endif
#if JVET_K0337_AFFINE_6PARA
          if ( pu.cu->affineType == AFFINEMODEL_6PARAM )
          {
#if JVET_K0357_AMVR
            mvd_coding(pu.mvdAffi[REF_PIC_LIST_1][2], 0);
#else
            mvd_coding(pu.mvdAffi[REF_PIC_LIST_1][2]);
#endif
          }
#endif
        }
        else
#elif JVET_K_AFFINE
        if ( pu.cu->affine )
        {
#if JVET_K0357_AMVR
          mvd_coding(pu.mvdAffi[REF_PIC_LIST_1][0], 0);
          mvd_coding(pu.mvdAffi[REF_PIC_LIST_1][1], 0);
#else
          mvd_coding(pu.mvdAffi[REF_PIC_LIST_1][0]);
          mvd_coding(pu.mvdAffi[REF_PIC_LIST_1][1]);
#endif
#if JVET_K0337_AFFINE_6PARA
          if ( pu.cu->affineType == AFFINEMODEL_6PARAM )
          {
#if JVET_K0357_AMVR
            mvd_coding(pu.mvdAffi[REF_PIC_LIST_1][2], 0);
#else
            mvd_coding(pu.mvdAffi[REF_PIC_LIST_1][2]);
#endif
          }
#endif
        }
        else
#endif
        {
#if JVET_K0357_AMVR
          mvd_coding( pu.mvd[REF_PIC_LIST_1], pu.cu->imv );
#else
          mvd_coding( pu.mvd[REF_PIC_LIST_1] );
#endif
        }
      }
      mvp_flag    ( pu, REF_PIC_LIST_1 );
    }
  }
}

#if JEM_TOOLS || JVET_K_AFFINE
void CABACWriter::affine_flag( const CodingUnit& cu )
{
#if JEM_TOOLS
  if( cu.cs->slice->isIntra() || !cu.cs->sps->getSpsNext().getUseAffine() || cu.partSize != SIZE_2Nx2N || cu.firstPU->frucMrgMode )
#else
  if( cu.cs->slice->isIntra() || !cu.cs->sps->getSpsNext().getUseAffine() || cu.partSize != SIZE_2Nx2N )
#endif
  {
    return;
  }

  if( !cu.firstPU->mergeFlag && !( cu.lumaSize().width > 8 && cu.lumaSize().height > 8 ) )
  {
    return;
  }

  if( cu.firstPU->mergeFlag && !PU::isAffineMrgFlagCoded( *cu.firstPU ) )
  {
    return;
  }

  CHECK( !cu.cs->pcv->rectCUs && cu.lumaSize().width != cu.lumaSize().height, "CU width and height are not equal for QTBT off." );

  unsigned ctxId = DeriveCtx::CtxAffineFlag( cu );
  m_BinEncoder.encodeBin( cu.affine, Ctx::AffineFlag( ctxId ) );
  DTRACE( g_trace_ctx, D_COMMON, " (%d) affine_flag() affine=%d\n", DTRACE_GET_COUNTER(g_trace_ctx, D_COMMON), cu.affine ? 1 : 0 );

  DTRACE( g_trace_ctx, D_SYNTAX, "affine_flag() affine=%d ctx=%d pos=(%d,%d)\n", cu.affine ? 1 : 0, ctxId, cu.Y().x, cu.Y().y );

#if JVET_K0337_AFFINE_6PARA
  if ( cu.affine && !cu.firstPU->mergeFlag && cu.cs->sps->getSpsNext().getUseAffineType() )
  {
    unsigned ctxId = 0;
    m_BinEncoder.encodeBin( cu.affineType, Ctx::AffineType( ctxId ) );

    DTRACE( g_trace_ctx, D_COMMON, " (%d) affine_type() affine_type=%d\n", DTRACE_GET_COUNTER( g_trace_ctx, D_COMMON ), cu.affineType ? 1 : 0 );
    DTRACE( g_trace_ctx, D_SYNTAX, "affine_type() affine_type=%d ctx=%d pos=(%d,%d)\n", cu.affineType ? 1 : 0, ctxId, cu.Y().x, cu.Y().y );
  }
#endif
}
#endif

void CABACWriter::merge_flag( const PredictionUnit& pu )
{
  m_BinEncoder.encodeBin( pu.mergeFlag, Ctx::MergeFlag() );

  DTRACE( g_trace_ctx, D_SYNTAX, "merge_flag() merge=%d pos=(%d,%d) size=%dx%d\n", pu.mergeFlag ? 1 : 0, pu.lumaPos().x, pu.lumaPos().y, pu.lumaSize().width, pu.lumaSize().height );
}

#if JVET_K0357_AMVR
void CABACWriter::imv_mode( const CodingUnit& cu )
{
  const SPSNext& spsNext = cu.cs->sps->getSpsNext();

  if( !spsNext.getUseIMV() )
  {
    return;
  }

  bool bNonZeroMvd = CU::hasSubCUNonZeroMVd( cu );
  if( !bNonZeroMvd )
  {
    return;
  }

  unsigned ctxId = DeriveCtx::CtxIMVFlag( cu );
#if JVET_K0076_CPR
  if (!(cu.firstPU->interDir == 1 && cu.cs->slice->getRefPic(REF_PIC_LIST_0, cu.firstPU->refIdx[REF_PIC_LIST_0])->getPOC() == cu.cs->slice->getPOC())) // the first bin of IMV flag does need to be signaled in CPR block
#endif
    m_BinEncoder.encodeBin( ( cu.imv > 0 ), Ctx::ImvFlag( ctxId ) );
  DTRACE( g_trace_ctx, D_SYNTAX, "imv_mode() value=%d ctx=%d\n", (cu.imv > 0), ctxId );

  if( spsNext.getImvMode() == IMV_4PEL && cu.imv > 0 )
  {
    m_BinEncoder.encodeBin( ( cu.imv > 1 ), Ctx::ImvFlag( 3 ) );
    DTRACE( g_trace_ctx, D_SYNTAX, "imv_mode() value=%d ctx=%d\n", ( cu.imv > 1 ), 3 );
  }

  DTRACE( g_trace_ctx, D_SYNTAX, "imv_mode() IMVFlag=%d\n", cu.imv );
}
#endif

void CABACWriter::merge_idx( const PredictionUnit& pu )
{
#if JEM_TOOLS
  if( pu.frucMrgMode || pu.cu->affine )
  {
    return;
  }
#endif
#if !JEM_TOOLS && JVET_K_AFFINE
  if ( pu.cu->affine )
  {
    return;
  }
#endif

  int numCandminus1 = int( pu.cs->slice->getMaxNumMergeCand() ) - 1;
  if( numCandminus1 > 0 )
  {
    if( pu.mergeIdx == 0 )
    {
      m_BinEncoder.encodeBin( 0, Ctx::MergeIdx() );
      DTRACE( g_trace_ctx, D_SYNTAX, "merge_idx() merge_idx=%d\n", pu.mergeIdx );
      return;
    }
    else
    {
#if JEM_TOOLS || JVET_K0346
      bool useExtCtx = pu.cs->sps->getSpsNext().getUseSubPuMvp();
#endif
      m_BinEncoder.encodeBin( 1, Ctx::MergeIdx() );
      for( unsigned idx = 1; idx < numCandminus1; idx++ )
      {
#if JEM_TOOLS || JVET_K0346
        if( useExtCtx )
        {
          m_BinEncoder.encodeBin( pu.mergeIdx == idx ? 0 : 1, Ctx::MergeIdx( std::min<int>( idx, NUM_MERGE_IDX_EXT_CTX - 1 ) ) );
        }
        else
#endif
        {
          m_BinEncoder.encodeBinEP( pu.mergeIdx == idx ? 0 : 1 );
        }
        if( pu.mergeIdx == idx )
        {
          break;
        }
      }
    }
  }
  DTRACE( g_trace_ctx, D_SYNTAX, "merge_idx() merge_idx=%d\n", pu.mergeIdx );
}

void CABACWriter::inter_pred_idc( const PredictionUnit& pu )
{
  if( !pu.cs->slice->isInterB() )
  {
    return;
  }
#if JEM_TOOLS || JVET_K0346
  if( pu.cu->partSize == SIZE_2Nx2N || pu.cs->sps->getSpsNext().getUseSubPuMvp() || pu.cu->lumaSize().width != 8 )
#else
  if( pu.cu->partSize == SIZE_2Nx2N || pu.cu->lumaSize().width != 8 )
#endif
  {
    unsigned ctxId = DeriveCtx::CtxInterDir(pu);
    if( pu.interDir == 3 )
    {
      m_BinEncoder.encodeBin( 1, Ctx::InterDir(ctxId) );
      DTRACE( g_trace_ctx, D_SYNTAX, "inter_pred_idc() ctx=%d value=%d pos=(%d,%d)\n", ctxId, pu.interDir, pu.lumaPos().x, pu.lumaPos().y );
      return;
    }
    else
    {
      m_BinEncoder.encodeBin( 0, Ctx::InterDir(ctxId) );
    }
  }
  m_BinEncoder.encodeBin( ( pu.interDir == 2 ), Ctx::InterDir( 4 ) );
  DTRACE( g_trace_ctx, D_SYNTAX, "inter_pred_idc() ctx=4 value=%d pos=(%d,%d)\n", pu.interDir, pu.lumaPos().x, pu.lumaPos().y );
}


void CABACWriter::ref_idx( const PredictionUnit& pu, RefPicList eRefList )
{
  int numRef  = pu.cs->slice->getNumRefIdx(eRefList);
  if( numRef <= 1 )
  {
    return;
  }
  int refIdx  = pu.refIdx[eRefList];
  m_BinEncoder.encodeBin( (refIdx > 0), Ctx::RefPic() );
  if( numRef <= 2 || refIdx == 0 )
  {
    DTRACE( g_trace_ctx, D_SYNTAX, "ref_idx() value=%d pos=(%d,%d)\n", refIdx, pu.lumaPos().x, pu.lumaPos().y );
    return;
  }
  m_BinEncoder.encodeBin( (refIdx > 1), Ctx::RefPic(1) );
  if( numRef <= 3 || refIdx == 1 )
  {
    DTRACE( g_trace_ctx, D_SYNTAX, "ref_idx() value=%d pos=(%d,%d)\n", refIdx, pu.lumaPos().x, pu.lumaPos().y );
    return;
  }
  for( int idx = 3; idx < numRef; idx++ )
  {
    if( refIdx > idx - 1 )
    {
      m_BinEncoder.encodeBinEP( 1 );
      DTRACE( g_trace_ctx, D_SYNTAX, "ref_idx() value=%d ctxId=%d pos=(%d,%d)\n", 1, 0, pu.lumaPos().x, pu.lumaPos().y );
    }
    else
    {
      m_BinEncoder.encodeBinEP( 0 );
      DTRACE( g_trace_ctx, D_SYNTAX, "ref_idx() value=%d ctxId=%d pos=(%d,%d)\n", 0, 0, pu.lumaPos().x, pu.lumaPos().y );
      break;
    }
  }
  DTRACE( g_trace_ctx, D_SYNTAX, "ref_idx() value=%d pos=(%d,%d)\n", refIdx, pu.lumaPos().x, pu.lumaPos().y );
}

void CABACWriter::mvp_flag( const PredictionUnit& pu, RefPicList eRefList )
{
  m_BinEncoder.encodeBin( pu.mvpIdx[eRefList], Ctx::MVPIdx() );
  DTRACE( g_trace_ctx, D_SYNTAX, "mvp_flag() value=%d pos=(%d,%d)\n", pu.mvpIdx[eRefList], pu.lumaPos().x, pu.lumaPos().y );
  DTRACE( g_trace_ctx, D_SYNTAX, "mvpIdx(refList:%d)=%d\n", eRefList, pu.mvpIdx[eRefList] );
}

#if JEM_TOOLS
void CABACWriter::fruc_mrg_mode( const PredictionUnit& pu )
{
  if( !pu.cs->slice->getSPS()->getSpsNext().getUseFRUCMrgMode() )
    return;

  unsigned uiFirstBin = pu.frucMrgMode != FRUC_MERGE_OFF;
  unsigned flag_idx   = DeriveCtx::CtxFrucFlag( pu );

  m_BinEncoder.encodeBin( uiFirstBin, Ctx::FrucFlag(flag_idx) );

  if( uiFirstBin )
  {
    if( pu.cs->slice->isInterP() )
    {
      CHECK( pu.frucMrgMode != FRUC_MERGE_TEMPLATE, "wrong fruc mode" );
    }
    else
    {
      unsigned uiSecondBin = pu.frucMrgMode == FRUC_MERGE_BILATERALMV;
      unsigned mode_idx    = DeriveCtx::CtxFrucMode( pu );

      m_BinEncoder.encodeBin( uiSecondBin, Ctx::FrucMode(mode_idx) );
    }
  }

  DTRACE( g_trace_ctx, D_SYNTAX, "fruc_mrg_mode() fruc_mode=%d pos=(%d,%d) size: %dx%d\n", pu.frucMrgMode, pu.Y().x, pu.Y().y, pu.lumaSize().width, pu.lumaSize().height );
}
#endif


//================================================================================
//  clause 7.3.8.7
//--------------------------------------------------------------------------------
//    void  pcm_samples( tu )
//================================================================================

void CABACWriter::pcm_samples( const TransformUnit& tu )
{
  CHECK( !tu.cu->ipcm, "pcm mode expected" );

  const SPS&        sps       = *tu.cu->cs->sps;
  const ComponentID maxCompId = ( tu.chromaFormat == CHROMA_400 ? COMPONENT_Y : COMPONENT_Cr );
  for( ComponentID compID = COMPONENT_Y; compID <= maxCompId; compID = ComponentID(compID+1) )
  {
    const CPelBuf   samples     = tu.getPcmbuf( compID );
    const unsigned  sampleBits  = sps.getPCMBitDepth( toChannelType(compID) );
    for( unsigned y = 0; y < samples.height; y++ )
    {
      for( unsigned x = 0; x < samples.width; x++ )
      {
        m_BinEncoder.encodeBinsPCM( samples.at(x, y), sampleBits );
      }
    }
  }
  m_BinEncoder.restart();
}



//================================================================================
//  clause 7.3.8.8
//--------------------------------------------------------------------------------
//    void  transform_tree      ( cs, area, cuCtx, chromaCbfs )
//    bool  split_transform_flag( split, depth )
//    bool  cbf_comp            ( cbf, area, depth )
//================================================================================

void CABACWriter::transform_tree( const CodingStructure& cs, Partitioner& partitioner, CUCtx& cuCtx, ChromaCbfs& chromaCbfs )
{
  const UnitArea&       area          = partitioner.currArea();

#if HM_QTBT_AS_IN_JEM_SYNTAX
  if( cs.pcv->noRQT )
  {
    const TransformUnit &tu = *cs.getTU( area.blocks[partitioner.chType].pos(), partitioner.chType );

    transform_unit_qtbt( tu, cuCtx, chromaCbfs );

    return;
  }
#endif

  const TransformUnit&  tu            = *cs.getTU( area.blocks[partitioner.chType].pos(), partitioner.chType );
  const CodingUnit&     cu            = *tu.cu;
#if ENABLE_BMS
  const unsigned        trDepth       = partitioner.currTrDepth;
  const bool            split         = ( tu.depth > trDepth );

  // split_transform_flag
  if( cs.pcv->noRQT )
  {
#if ENABLE_BMS
    if( partitioner.canSplit( TU_MAX_TR_SPLIT, cs ) )
    {
      CHECK( !split, "transform split implied" );
    }
    else
#endif
    CHECK( split, "transform split not allowed with QTBT" );
  }
#endif

  // cbf_cb & cbf_cr
  if( area.chromaFormat != CHROMA_400 && area.blocks[COMPONENT_Cb].valid() && ( !CS::isDualITree( cs ) || partitioner.chType == CHANNEL_TYPE_CHROMA ) )
  {
    {
#if ENABLE_BMS
      if( trDepth == 0 || chromaCbfs.Cb )
#endif
      {
#if ENABLE_BMS
        chromaCbfs.Cb = TU::getCbfAtDepth( tu, COMPONENT_Cb, trDepth );
        cbf_comp( cs, chromaCbfs.Cb, area.blocks[COMPONENT_Cb], trDepth );
#else
        chromaCbfs.Cb = TU::getCbf( tu, COMPONENT_Cb );
        cbf_comp( cs, chromaCbfs.Cb, area.blocks[COMPONENT_Cb] );
#endif
      }
#if ENABLE_BMS
      else
      {
        CHECK( TU::getCbfAtDepth( tu, COMPONENT_Cb, trDepth ) != chromaCbfs.Cb, "incorrect Cb cbf" );
      }

      if( trDepth == 0 || chromaCbfs.Cr )
#endif
      {
#if ENABLE_BMS
        chromaCbfs.Cr = TU::getCbfAtDepth( tu, COMPONENT_Cr,   trDepth );
#if JVET_K0072
        cbf_comp( cs, chromaCbfs.Cr, area.blocks[COMPONENT_Cr], trDepth, chromaCbfs.Cb );
#else
        cbf_comp( cs, chromaCbfs.Cr, area.blocks[COMPONENT_Cr], trDepth );
#endif
#else
        chromaCbfs.Cr = TU::getCbf( tu, COMPONENT_Cr );
#if JVET_K0072
        cbf_comp( cs, chromaCbfs.Cr, area.blocks[COMPONENT_Cr], chromaCbfs.Cb );
#else
        cbf_comp( cs, chromaCbfs.Cr, area.blocks[COMPONENT_Cr] );
#endif
#endif
      }
#if ENABLE_BMS
      else
      {
        CHECK( TU::getCbfAtDepth( tu, COMPONENT_Cr, trDepth ) != chromaCbfs.Cr, "incorrect Cr cbf" );
      }
#endif
    }
  }
  else if( CS::isDualITree( cs ) )
  {
    chromaCbfs = ChromaCbfs( false );
  }

#if ENABLE_BMS
  if( split )
  {
    if( area.chromaFormat != CHROMA_400 )
    {
      chromaCbfs.Cb        = TU::getCbfAtDepth( tu, COMPONENT_Cb,  trDepth );
      chromaCbfs.Cr        = TU::getCbfAtDepth( tu, COMPONENT_Cr,  trDepth );
    }
#if (JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT) && HM_EMT_NSST_AS_IN_JEM
    if( trDepth == 0 ) emt_cu_flag( cu );
#endif

#if ENABLE_BMS
    if( partitioner.canSplit( TU_MAX_TR_SPLIT, cs ) )
    {
#if ENABLE_TRACING
      const CompArea &tuArea = partitioner.currArea().blocks[partitioner.chType];
      DTRACE( g_trace_ctx, D_SYNTAX, "transform_tree() maxTrSplit chType=%d pos=(%d,%d) size=%dx%d\n", partitioner.chType, tuArea.x, tuArea.y, tuArea.width, tuArea.height );

#endif
      partitioner.splitCurrArea( TU_MAX_TR_SPLIT, cs );
    }
    else
#endif
      THROW( "Implicit TU split not available" );

    do
    {
      ChromaCbfs subChromaCbfs = chromaCbfs;
      transform_tree( cs, partitioner, cuCtx, subChromaCbfs );
    } while( partitioner.nextPart( cs ) );

    partitioner.exitCurrSplit();
  }
  else
#endif
  {
#if ENABLE_BMS
    DTRACE( g_trace_ctx, D_SYNTAX, "transform_unit() pos=(%d,%d) size=%dx%d depth=%d trDepth=%d\n", tu.blocks[tu.chType].x, tu.blocks[tu.chType].y, tu.blocks[tu.chType].width, tu.blocks[tu.chType].height, cu.depth, partitioner.currTrDepth );
#else
    DTRACE( g_trace_ctx, D_SYNTAX, "transform_unit() pos=(%d,%d) size=%dx%d depth=%d\n", tu.blocks[tu.chType].x, tu.blocks[tu.chType].y, tu.blocks[tu.chType].width, tu.blocks[tu.chType].height, cu.depth );
#endif

    if( !isChroma( partitioner.chType ) )
    {
#if ENABLE_BMS
      if( !CU::isIntra( cu ) && trDepth == 0 && !chromaCbfs.sigChroma( area.chromaFormat ) )
      {
        CHECK( !TU::getCbfAtDepth( tu, COMPONENT_Y, trDepth ), "Luma cbf must be true for inter units with no chroma coeffs" );
      }
#else
      if( !CU::isIntra( cu ) && !chromaCbfs.sigChroma( area.chromaFormat ) )
      {
        CHECK( !TU::getCbf( tu, COMPONENT_Y ), "Luma cbf must be true for inter units with no chroma coeffs" );
      }
#endif
      else
      {
#if ENABLE_BMS
        cbf_comp( cs, TU::getCbfAtDepth( tu, COMPONENT_Y, trDepth ), tu.Y(), trDepth );
#else
        cbf_comp( cs, TU::getCbf( tu, COMPONENT_Y ), tu.Y() );
#endif
      }
    }

#if (JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT) && HM_EMT_NSST_AS_IN_JEM
#if ENABLE_BMS
    if( trDepth == 0 && TU::getCbfAtDepth( tu, COMPONENT_Y, 0 ) ) emt_cu_flag( cu );
#else
    if( TU::getCbf( tu, COMPONENT_Y ) ) emt_cu_flag( cu );
#endif
#endif

    transform_unit( tu, cuCtx, chromaCbfs );
  }
}

#if ENABLE_BMS
#if JVET_K0072
void CABACWriter::cbf_comp( const CodingStructure& cs, bool cbf, const CompArea& area, unsigned depth, const bool prevCbCbf )
#else
void CABACWriter::cbf_comp( const CodingStructure& cs, bool cbf, const CompArea& area, unsigned depth )
#endif
#else
#if JVET_K0072
void CABACWriter::cbf_comp( const CodingStructure& cs, bool cbf, const CompArea& area, const bool prevCbCbf )
#else
void CABACWriter::cbf_comp( const CodingStructure& cs, bool cbf, const CompArea& area )
#endif
#endif
{
#if JVET_K0072
#if ENABLE_BMS
  const unsigned  ctxId   = DeriveCtx::CtxQtCbf( area.compID, depth, prevCbCbf );
#else
  const unsigned  ctxId   = DeriveCtx::CtxQtCbf( area.compID, prevCbCbf );
#endif
  const CtxSet&   ctxSet  = Ctx::QtCbf[ area.compID ];
#else
#if ENABLE_BMS
  const unsigned  ctxId   = DeriveCtx::CtxQtCbf( area.compID, depth );
#else
  const unsigned  ctxId   = DeriveCtx::CtxQtCbf( area.compID );
#endif
  const CtxSet&   ctxSet  = Ctx::QtCbf[ toChannelType(area.compID) ];
#endif

  m_BinEncoder.encodeBin( cbf, ctxSet( ctxId ) );
  DTRACE( g_trace_ctx, D_SYNTAX, "cbf_comp() etype=%d pos=(%d,%d) ctx=%d cbf=%d\n", area.compID, area.x, area.y, ctxId, cbf );
}





//================================================================================
//  clause 7.3.8.9
//--------------------------------------------------------------------------------
//    void  mvd_coding( pu, refList )
//================================================================================

#if JVET_K0357_AMVR
void CABACWriter::mvd_coding( const Mv &rMvd, uint8_t imv )
#else
void CABACWriter::mvd_coding( const Mv &rMvd )
#endif
{
  int       horMvd = rMvd.getHor();
  int       verMvd = rMvd.getVer();
#if JVET_K0357_AMVR
  if( imv )
  {
    CHECK( (horMvd % 4) != 0 && (verMvd % 4) != 0, "IMV: MVD is not a multiple of 4" );
    horMvd >>= 2;
    verMvd >>= 2;
    if( imv == 2 )//IMV_4PEL
    {
      CHECK( (horMvd % 4) != 0 && (verMvd % 4) != 0, "IMV: MVD is not a multiple of 8" );
      horMvd >>= 2;
      verMvd >>= 2;
    }
  }
#endif
  unsigned  horAbs  = unsigned( horMvd < 0 ? -horMvd : horMvd );
  unsigned  verAbs  = unsigned( verMvd < 0 ? -verMvd : verMvd );

#if (JEM_TOOLS || JVET_K0346 || JVET_K_AFFINE) && !REMOVE_MV_ADAPT_PREC
  if (rMvd.highPrec)
  {
    CHECK(horAbs & ((1 << VCEG_AZ07_MV_ADD_PRECISION_BIT_FOR_STORE) - 1), "mvd-x has high precision fractional part.");
    CHECK(verAbs & ((1 << VCEG_AZ07_MV_ADD_PRECISION_BIT_FOR_STORE) - 1), "mvd-y has high precision fractional part.");
    horAbs >>= VCEG_AZ07_MV_ADD_PRECISION_BIT_FOR_STORE;
    verAbs >>= VCEG_AZ07_MV_ADD_PRECISION_BIT_FOR_STORE;
  }
#endif

  // abs_mvd_greater0_flag[ 0 | 1 ]
  m_BinEncoder.encodeBin( (horAbs > 0), Ctx::Mvd() );
  m_BinEncoder.encodeBin( (verAbs > 0), Ctx::Mvd() );

  // abs_mvd_greater1_flag[ 0 | 1 ]
  if( horAbs > 0 )
  {
    m_BinEncoder.encodeBin( (horAbs > 1), Ctx::Mvd(1) );
  }
  if( verAbs > 0 )
  {
    m_BinEncoder.encodeBin( (verAbs > 1), Ctx::Mvd(1) );
  }

  // abs_mvd_minus2[ 0 | 1 ] and mvd_sign_flag[ 0 | 1 ]
  if( horAbs > 0 )
  {
    if( horAbs > 1 )
    {
      exp_golomb_eqprob( horAbs - 2, 1 );
    }
    m_BinEncoder.encodeBinEP( (horMvd < 0) );
  }
  if( verAbs > 0 )
  {
    if( verAbs > 1 )
    {
      exp_golomb_eqprob( verAbs - 2, 1 );
    }
    m_BinEncoder.encodeBinEP( (verMvd < 0) );
  }
}




//================================================================================
//  clause 7.3.8.10
//--------------------------------------------------------------------------------
//    void  transform_unit      ( tu, cuCtx, chromaCbfs )
//    void  cu_qp_delta         ( cu )
//    void  cu_chroma_qp_offset ( cu )
//================================================================================

void CABACWriter::transform_unit( const TransformUnit& tu, CUCtx& cuCtx, ChromaCbfs& chromaCbfs )
{
  CodingUnit& cu        = *tu.cu;
  bool        lumaOnly  = ( cu.chromaFormat == CHROMA_400 || !tu.blocks[COMPONENT_Cb].valid() );
  bool        cbf[3]    = { TU::getCbf( tu, COMPONENT_Y ), chromaCbfs.Cb, chromaCbfs.Cr };
  bool        cbfLuma   = ( cbf[ COMPONENT_Y ] != 0 );
  bool        cbfChroma = false;


  if( cu.chromaFormat != CHROMA_400 )
  {
    if( tu.blocks[COMPONENT_Cb].valid() )
    {
      cbf   [ COMPONENT_Cb  ] = TU::getCbf( tu, COMPONENT_Cb );
      cbf   [ COMPONENT_Cr  ] = TU::getCbf( tu, COMPONENT_Cr );
    }
    cbfChroma = ( cbf[ COMPONENT_Cb ] || cbf[ COMPONENT_Cr ] );
  }
  if( cbfLuma || cbfChroma )
  {
    if( cu.cs->pps->getUseDQP() && !cuCtx.isDQPCoded )
    {
      cu_qp_delta( cu, cuCtx.qp, cu.qp );
      cuCtx.qp         = cu.qp;
      cuCtx.isDQPCoded = true;
    }
    if( cu.cs->slice->getUseChromaQpAdj() && cbfChroma && !cu.transQuantBypass && !cuCtx.isChromaQpAdjCoded )
    {
      cu_chroma_qp_offset( cu );
      cuCtx.isChromaQpAdjCoded = true;
    }
    if( cbfLuma )
    {
      residual_coding( tu, COMPONENT_Y );
    }
    if( !lumaOnly )
    {
      for( ComponentID compID = COMPONENT_Cb; compID <= COMPONENT_Cr; compID = ComponentID( compID + 1 ) )
      {
        if( TU::hasCrossCompPredInfo( tu, compID ) )
        {
          cross_comp_pred( tu, compID );
        }
        if( cbf[ compID ] )
        {
          residual_coding( tu, compID );
        }
      }
    }
  }
}

#if HM_QTBT_AS_IN_JEM_SYNTAX
void CABACWriter::transform_unit_qtbt( const TransformUnit& tu, CUCtx& cuCtx, ChromaCbfs& chromaCbfs )
{
  CodingUnit& cu  = *tu.cu;
  bool cbfLuma    = false;
  bool cbfChroma  = false;

  bool lumaOnly   = ( cu.chromaFormat == CHROMA_400 || !tu.blocks[COMPONENT_Cb].valid() );
  bool chromaOnly =                                    !tu.blocks[COMPONENT_Y ].valid();

  if( !lumaOnly )
  {
#if JVET_K0072
    bool prevCbf = false;
#endif
    for( ComponentID compID = COMPONENT_Cb; compID <= COMPONENT_Cr; compID = ComponentID( compID + 1 ) )
    {
#if ENABLE_BMS
#if JVET_K0072
      cbf_comp( *tu.cs, tu.cbf[compID] != 0, tu.blocks[compID], tu.depth, prevCbf );
      prevCbf = (tu.cbf[compID] != 0);
#else
      cbf_comp( *tu.cs, tu.cbf[compID] != 0, tu.blocks[compID], tu.depth );
#endif
#else
#if JVET_K0072
      cbf_comp( *tu.cs, tu.cbf[compID] != 0, tu.blocks[compID], prevCbf );
      prevCbf = (tu.cbf[compID] != 0);
#else
      cbf_comp( *tu.cs, tu.cbf[compID] != 0, tu.blocks[compID] );
#endif
#endif
      chromaCbfs.cbf( compID ) = tu.cbf[compID] != 0;

      if( TU::hasCrossCompPredInfo( tu, compID ) )
      {
        cross_comp_pred( tu, compID );
      }
      if( tu.cbf[compID] )
      {
        residual_coding( tu, compID );
        cbfChroma = true;
      }
    }
  }

  if( !chromaOnly )
  {
    if( !CU::isIntra( cu ) && !chromaCbfs.sigChroma( tu.chromaFormat ) )
    {
#if ENABLE_BMS
      CHECK( !TU::getCbfAtDepth( tu, COMPONENT_Y, 0 ), "The luma CBF is implicitely '1', but '0' found" );
#else
      CHECK( !TU::getCbf( tu, COMPONENT_Y ), "The luma CBF is implicitely '1', but '0' found" );
#endif
    }
    else
    {
#if ENABLE_BMS
      cbf_comp( *tu.cs, TU::getCbf( tu, COMPONENT_Y ), tu.Y(), tu.depth );
#else
      cbf_comp( *tu.cs, TU::getCbf( tu, COMPONENT_Y ), tu.Y() );
#endif
    }

    if( tu.cbf[0] )
    {
#if (JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT) && HM_EMT_NSST_AS_IN_JEM
      emt_cu_flag( cu );
#endif
      residual_coding( tu, COMPONENT_Y );
      cbfLuma = true;
    }
  }

  if( cbfLuma || cbfChroma )
  {
    if( cu.cs->pps->getUseDQP() && !cuCtx.isDQPCoded )
    {
      cu_qp_delta( cu, cuCtx.qp, cu.qp );
      cuCtx.qp         = cu.qp;
      cuCtx.isDQPCoded = true;
    }
    if( cu.cs->slice->getUseChromaQpAdj() && cbfChroma && !cu.transQuantBypass && !cuCtx.isChromaQpAdjCoded )
    {
      cu_chroma_qp_offset( cu );
      cuCtx.isChromaQpAdjCoded = true;
    }
  }
}
#endif

void CABACWriter::cu_qp_delta( const CodingUnit& cu, int predQP, const int8_t qp )
{
  CHECK(!( predQP != std::numeric_limits<int>::max()), "Unspecified error");
  int       DQp         = qp - predQP;
  int       qpBdOffsetY = cu.cs->sps->getQpBDOffset( CHANNEL_TYPE_LUMA );
#if JVET_K0251_QP_EXT
  DQp                   = ( DQp + (MAX_QP + 1) + (MAX_QP + 1) / 2 + qpBdOffsetY + (qpBdOffsetY / 2)) % ((MAX_QP + 1) + qpBdOffsetY) - (MAX_QP + 1) / 2 - (qpBdOffsetY / 2);
#else
  DQp                   = ( DQp + 78 + qpBdOffsetY + ( qpBdOffsetY / 2 ) ) % ( 52 + qpBdOffsetY ) - 26 - ( qpBdOffsetY / 2 );
#endif
  unsigned  absDQP      = unsigned( DQp < 0 ? -DQp : DQp );
  unsigned  unaryDQP    = std::min<unsigned>( absDQP, CU_DQP_TU_CMAX );

  unary_max_symbol( unaryDQP, Ctx::DeltaQP(), Ctx::DeltaQP(1), CU_DQP_TU_CMAX );
  if( absDQP >= CU_DQP_TU_CMAX )
  {
    exp_golomb_eqprob( absDQP - CU_DQP_TU_CMAX, CU_DQP_EG_k );
  }
  if( absDQP > 0 )
  {
    m_BinEncoder.encodeBinEP( DQp < 0 );
  }

  DTRACE_COND( ( isEncoding() ), g_trace_ctx, D_DQP, "x=%d, y=%d, d=%d, pred_qp=%d, DQp=%d, qp=%d\n", cu.blocks[cu.chType].lumaPos().x, cu.blocks[cu.chType].lumaPos().y, cu.qtDepth, predQP, DQp, qp );
}


void CABACWriter::cu_chroma_qp_offset( const CodingUnit& cu )
{
  // cu_chroma_qp_offset_flag
  unsigned qpAdj = cu.chromaQpAdj;
  if( qpAdj == 0 )
  {
    m_BinEncoder.encodeBin( 0, Ctx::ChromaQpAdjFlag() );
  }
  else
  {
    m_BinEncoder.encodeBin( 1, Ctx::ChromaQpAdjFlag() );
    int length = cu.cs->pps->getPpsRangeExtension().getChromaQpOffsetListLen();
    if( length > 1 )
    {
      unary_max_symbol( qpAdj-1, Ctx::ChromaQpAdjIdc(), Ctx::ChromaQpAdjIdc(), length-1 );
    }
  }
}





//================================================================================
//  clause 7.3.8.11
//--------------------------------------------------------------------------------
//    void        residual_coding         ( tu, compID )
//    void        transform_skip_flag     ( tu, compID )
//    void        explicit_rdpcm_mode     ( tu, compID )
//    void        last_sig_coeff          ( coeffCtx )
//    void        residual_coding_subblock( coeffCtx )
//================================================================================

void CABACWriter::residual_coding( const TransformUnit& tu, ComponentID compID )
{
#if ENABLE_TRACING || HEVC_USE_SIGN_HIDING || JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT
  const CodingUnit& cu = *tu.cu;
#endif
  DTRACE( g_trace_ctx, D_SYNTAX, "residual_coding() etype=%d pos=(%d,%d) size=%dx%d predMode=%d\n", tu.blocks[compID].compID, tu.blocks[compID].x, tu.blocks[compID].y, tu.blocks[compID].width, tu.blocks[compID].height, cu.predMode );

  // code transform skip and explicit rdpcm mode
  transform_skip_flag( tu, compID );
  explicit_rdpcm_mode( tu, compID );

#if HEVC_USE_SIGN_HIDING
  // determine sign hiding
#if JVET_K0072
  bool signHiding  = ( cu.cs->slice->getSignDataHidingEnabledFlag() && !cu.transQuantBypass && tu.rdpcm[compID] == RDPCM_OFF );
#else
  bool signHiding  = ( cu.cs->pps->getSignDataHidingEnabledFlag() && !cu.transQuantBypass && tu.rdpcm[compID] == RDPCM_OFF );
#endif
  if(  signHiding && CU::isIntra(cu) && CU::isRDPCMEnabled(cu) && tu.transformSkip[compID] )
  {
    const ChannelType chType    = toChannelType( compID );
    const unsigned    intraMode = PU::getFinalIntraMode( *cu.cs->getPU( tu.blocks[compID].pos(), chType ), chType );
    if( intraMode == HOR_IDX || intraMode == VER_IDX )
    {
      signHiding = false;
    }
  }
#endif

  // init coeff coding context
#if HEVC_USE_SIGN_HIDING
  CoeffCodingContext  cctx    ( tu, compID, signHiding );
#else
  CoeffCodingContext  cctx    ( tu, compID );
#endif
  const TCoeff*       coeff   = tu.getCoeffs( compID ).buf;
#if JVET_K0072
#else
  unsigned&           GRStats = m_BinEncoder.getCtx().getGRAdaptStats( TU::getGolombRiceStatisticsIndex( tu, compID ) );
#endif
#if JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT
  unsigned            numSig  = 0;
#endif

  // determine and set last coeff position and sig group flags
  int                      scanPosLast = -1;
  std::bitset<MLS_GRP_NUM> sigGroupFlags;
  for( int scanPos = 0; scanPos < cctx.maxNumCoeff(); scanPos++)
  {
    unsigned blkPos = cctx.blockPos( scanPos );
    if( coeff[blkPos] )
    {
      scanPosLast = scanPos;
      sigGroupFlags.set( scanPos >> cctx.log2CGSize() );
    }
  }
  CHECK( scanPosLast < 0, "Coefficient coding called for empty TU" );
  cctx.setScanPosLast(scanPosLast);

  // code last coeff position
  last_sig_coeff( cctx );

  // code subblocks
#if JVET_K0072
  const int stateTab  = ( tu.cs->slice->getDepQuantEnabledFlag() ? 32040 : 0 );
  int       state     = 0;
#else
  cctx.setGoRiceStats( GRStats );
#endif
#if JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT
  bool useEmt = ( cu.cs->sps->getSpsNext().getUseIntraEMT() && cu.predMode == MODE_INTRA ) || ( cu.cs->sps->getSpsNext().getUseInterEMT() && cu.predMode != MODE_INTRA );
  useEmt = useEmt && isLuma(compID);
#endif

  for( int subSetId = ( cctx.scanPosLast() >> cctx.log2CGSize() ); subSetId >= 0; subSetId--)
  {
    cctx.initSubblock       ( subSetId, sigGroupFlags[subSetId] );
#if JVET_K0072
    residual_coding_subblock( cctx, coeff, stateTab, state );
#else
    residual_coding_subblock( cctx, coeff );
#endif

#if JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT
    if (useEmt)
    {
      numSig += cctx.emtNumSigCoeff();
      cctx.setEmtNumSigCoeff( 0 );
    }
#endif
  }

#if JVET_K0072
#else
  GRStats = cctx.currGoRiceStats();
#endif

#if (JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT) && HM_EMT_NSST_AS_IN_JEM
  if( useEmt && !tu.transformSkip[compID] && compID == COMPONENT_Y && tu.cu->emtFlag )
  {
    if( CU::isIntra( *tu.cu ) )
    {
      if( numSig > g_EmtSigNumThr )
      {
        emt_tu_index( tu );
      }
      else
      {
        CHECK( tu.emtIdx != 0, "If the number of significant coefficients is <= g_EmtSigNumThr, then the tu index must be 0" );
      }
    }
    else
    {
      emt_tu_index( tu );
    }
  }
#endif
}


void CABACWriter::transform_skip_flag( const TransformUnit& tu, ComponentID compID )
{
#if HM_EMT_NSST_AS_IN_JEM && (JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT)
  if( !tu.cu->cs->pps->getUseTransformSkip() || tu.cu->transQuantBypass || !TU::hasTransformSkipFlag( *tu.cs, tu.blocks[compID] ) || ( isLuma( compID ) && tu.cu->emtFlag ) )
#else
  if( !tu.cu->cs->pps->getUseTransformSkip() || tu.cu->transQuantBypass || !TU::hasTransformSkipFlag( *tu.cs, tu.blocks[compID] ) )
#endif
  {
    return;
  }
  m_BinEncoder.encodeBin( tu.transformSkip[compID], Ctx::TransformSkipFlag(toChannelType(compID)) );

  DTRACE( g_trace_ctx, D_SYNTAX, "transform_skip_flag() etype=%d pos=(%d,%d) trSkip=%d\n", compID, tu.blocks[compID].x, tu.blocks[compID].y, (int)tu.transformSkip[compID] );
}

#if JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT
void CABACWriter::emt_tu_index( const TransformUnit& tu )
{
  int maxSizeEmtIntra, maxSizeEmtInter;
  if( tu.cs->pcv->noRQT )
  {
    maxSizeEmtIntra = EMT_INTRA_MAX_CU_WITH_QTBT;
    maxSizeEmtInter = EMT_INTER_MAX_CU_WITH_QTBT;
  }
  else
  {
    maxSizeEmtIntra = EMT_INTRA_MAX_CU;
    maxSizeEmtInter = EMT_INTER_MAX_CU;
  }
  if( CU::isIntra( *tu.cu ) && ( tu.cu->Y().width <= maxSizeEmtIntra ) && ( tu.cu->Y().height <= maxSizeEmtIntra ) )
  {
    uint8_t trIdx = tu.emtIdx;
    m_BinEncoder.encodeBin( ( trIdx & 1 ) ? 1 : 0, Ctx::EMTTuIndex( 0 ) );
    m_BinEncoder.encodeBin( ( trIdx / 2 ) ? 1 : 0, Ctx::EMTTuIndex( 1 ) );
    DTRACE( g_trace_ctx, D_SYNTAX, "emt_tu_index() etype=%d pos=(%d,%d) emtTrIdx=%d\n", COMPONENT_Y, tu.blocks[COMPONENT_Y].x, tu.blocks[COMPONENT_Y].y, ( int ) tu.emtIdx );
  }
  if( !CU::isIntra( *tu.cu ) && ( tu.cu->Y().width <= maxSizeEmtInter ) && ( tu.cu->Y().height <= maxSizeEmtInter ) )
  {
    uint8_t trIdx = tu.emtIdx;
    m_BinEncoder.encodeBin( ( trIdx & 1 ) ? 1 : 0, Ctx::EMTTuIndex( 2 ) );
    m_BinEncoder.encodeBin( ( trIdx / 2 ) ? 1 : 0, Ctx::EMTTuIndex( 3 ) );
    DTRACE( g_trace_ctx, D_SYNTAX, "emt_tu_index() etype=%d pos=(%d,%d) emtTrIdx=%d\n", COMPONENT_Y, tu.blocks[COMPONENT_Y].x, tu.blocks[COMPONENT_Y].y, ( int ) tu.emtIdx );
  }
}

//void CABACWriter::emt_cu_flag(const CodingUnit& cu, uint32_t depth, bool codeCuFlag, const int tuWidth,const int tuHeight)
void CABACWriter::emt_cu_flag( const CodingUnit& cu )
{
  const CodingStructure& cs = *cu.cs;

  if( !( ( cs.sps->getSpsNext().getUseIntraEMT() && CU::isIntra( cu ) ) || ( cs.sps->getSpsNext().getUseInterEMT() && CU::isInter( cu ) ) ) || isChroma( cu.chType ) )
  {
    return;
  }

  unsigned depth          = cu.qtDepth;
  const unsigned cuWidth  = cu.lwidth();
  const unsigned cuHeight = cu.lheight();

  int maxSizeEmtIntra, maxSizeEmtInter;

  if( cu.cs->pcv->noRQT )
  {
    if( depth >= NUM_EMT_CU_FLAG_CTX )
    {
      depth = NUM_EMT_CU_FLAG_CTX - 1;
    }
    maxSizeEmtIntra = EMT_INTRA_MAX_CU_WITH_QTBT;
    maxSizeEmtInter = EMT_INTER_MAX_CU_WITH_QTBT;
  }
  else
  {
    maxSizeEmtIntra = EMT_INTRA_MAX_CU;
    maxSizeEmtInter = EMT_INTER_MAX_CU;
    CHECK( depth >= NUM_EMT_CU_FLAG_CTX, "Depth exceeds limit." );
  }

  const unsigned maxSizeEmt = CU::isIntra( cu ) ? maxSizeEmtIntra : maxSizeEmtInter;

  if( cuWidth <= maxSizeEmt && cuHeight <= maxSizeEmt )
  {
    m_BinEncoder.encodeBin( cu.emtFlag, Ctx::EMTCuFlag( depth ) );
    DTRACE( g_trace_ctx, D_SYNTAX, "emt_cu_flag() etype=%d pos=(%d,%d) emtCuFlag=%d\n", COMPONENT_Y, cu.lx(), cu.ly(), ( int ) cu.emtFlag );
  }
}
#endif



void CABACWriter::explicit_rdpcm_mode( const TransformUnit& tu, ComponentID compID )
{
  const CodingUnit& cu = *tu.cu;
  if( !CU::isIntra(cu) && CU::isRDPCMEnabled(cu) && ( tu.transformSkip[compID] || cu.transQuantBypass ) )
  {
    ChannelType chType = toChannelType( compID );
    switch( tu.rdpcm[compID] )
    {
    case RDPCM_VER:
      m_BinEncoder.encodeBin( 1, Ctx::RdpcmFlag(chType) );
      m_BinEncoder.encodeBin( 1, Ctx::RdpcmDir (chType) );
      break;
    case RDPCM_HOR:
      m_BinEncoder.encodeBin( 1, Ctx::RdpcmFlag(chType) );
      m_BinEncoder.encodeBin( 0, Ctx::RdpcmDir (chType) );
      break;
    default: // RDPCM_OFF
      m_BinEncoder.encodeBin( 0, Ctx::RdpcmFlag(chType) );
    }
  }
}

#if JEM_TOOLS
void CABACWriter::residual_nsst_mode( const CodingUnit& cu, CUCtx& cuCtx )
{
  if( CS::isDualITree( *cu.cs ) && cu.chType == CHANNEL_TYPE_CHROMA && std::min( cu.blocks[1].width, cu.blocks[1].height ) < 4 )
  {
    return;
  }

#if !HM_EMT_NSST_AS_IN_JEM
  CHECK( cu.nsstIdx != 0 && cu.emtFlag, "EMT cannot be applied if NSST is on!" );
#endif

  if( cu.cs->sps->getSpsNext().getUseNSST() && CU::isIntra( cu ) && !CU::isLosslessCoded( cu ) && !cu.pdpc )
  {
    bool nonZeroCoeffNonTs;
    if( cu.cs->pcv->noRQT )
    {
      const int nonZeroCoeffThr = CS::isDualITree( *cu.cs ) ? ( isLuma( cu.chType ) ? NSST_SIG_NZ_LUMA : NSST_SIG_NZ_CHROMA ) : NSST_SIG_NZ_LUMA + NSST_SIG_NZ_CHROMA;
      cuCtx.numNonZeroCoeffNonTs = CU::getNumNonZeroCoeffNonTs( cu );
      nonZeroCoeffNonTs = cuCtx.numNonZeroCoeffNonTs > nonZeroCoeffThr;
    }
    else
    {
      nonZeroCoeffNonTs = CU::hasNonTsCodedBlock( cu );
    }
    if( !nonZeroCoeffNonTs )
    {
      return;
    }
  }
  else
  {
    return;
  }

  bool bUseThreeNSSTPasses = false;

  if( cu.partSize == SIZE_2Nx2N )
  {
    int intraMode = cu.firstPU->intraDir[cu.chType];
    if( intraMode == DM_CHROMA_IDX )
    {
      intraMode = CS::isDualITree( *cu.cs ) ? cu.cs->picture->cs->getPU( cu.blocks[cu.chType].lumaPos(), CHANNEL_TYPE_LUMA )->intraDir[0] : cu.firstPU->intraDir[0];
    }
    else if( PU::isLMCMode( intraMode ) )
    {
      intraMode = PLANAR_IDX;
    }

    bUseThreeNSSTPasses = ( intraMode <= DC_IDX );
  }

  if( bUseThreeNSSTPasses )
  {
    const uint32_t idxROT = cu.nsstIdx;
    assert( idxROT < 3 );
    m_BinEncoder.encodeBin( idxROT ? 1 : 0, Ctx::NSSTIdx( 1 ) );
    if( idxROT )
    {
      m_BinEncoder.encodeBin( ( idxROT - 1 ) ? 1 : 0, Ctx::NSSTIdx( 3 ) );
    }
  }
  else
  {
    const uint32_t idxROT = cu.nsstIdx;
    assert( idxROT < 4 );
    m_BinEncoder.encodeBin( idxROT ? 1 : 0, Ctx::NSSTIdx( 0 ) );
    if( idxROT )
    {
      m_BinEncoder.encodeBin( ( idxROT - 1 ) ? 1 : 0, Ctx::NSSTIdx( 2 ) );
      if( idxROT > 1 )
      {
        m_BinEncoder.encodeBin( ( idxROT - 2 ) ? 1 : 0, Ctx::NSSTIdx( 4 ) );
      }
    }
  }

  DTRACE( g_trace_ctx, D_SYNTAX, "residual_nsst_mode() etype=%d pos=(%d,%d) mode=%d\n", COMPONENT_Y, cu.lx(), cu.ly(), ( int ) cu.nsstIdx );
}
#endif

void CABACWriter::last_sig_coeff( CoeffCodingContext& cctx )
{
  unsigned blkPos = cctx.blockPos( cctx.scanPosLast() );
  unsigned posX, posY;
#if HEVC_USE_MDCS
  if( cctx.scanType() == SCAN_VER )
  {
    posX  = blkPos / cctx.width();
    posY  = blkPos - ( posX * cctx.width() );
  }
  else
#endif
  {
    posY  = blkPos / cctx.width();
    posX  = blkPos - ( posY * cctx.width() );
  }

  unsigned CtxLast;
  unsigned GroupIdxX = g_uiGroupIdx[ posX ];
  unsigned GroupIdxY = g_uiGroupIdx[ posY ];

  for( CtxLast = 0; CtxLast < GroupIdxX; CtxLast++ )
  {
    m_BinEncoder.encodeBin( 1, cctx.lastXCtxId( CtxLast ) );
  }
  if( GroupIdxX < cctx.maxLastPosX() )
  {
    m_BinEncoder.encodeBin( 0, cctx.lastXCtxId( CtxLast ) );
  }
  for( CtxLast = 0; CtxLast < GroupIdxY; CtxLast++ )
  {
    m_BinEncoder.encodeBin( 1, cctx.lastYCtxId( CtxLast ) );
  }
  if( GroupIdxY < cctx.maxLastPosY() )
  {
    m_BinEncoder.encodeBin( 0, cctx.lastYCtxId( CtxLast ) );
  }
  if( GroupIdxX > 3 )
  {
    posX -= g_uiMinInGroup[ GroupIdxX ];
    for (int i = ( ( GroupIdxX - 2 ) >> 1 ) - 1 ; i >= 0; i-- )
    {
      m_BinEncoder.encodeBinEP( ( posX >> i ) & 1 );
    }
  }
  if( GroupIdxY > 3 )
  {
    posY -= g_uiMinInGroup[ GroupIdxY ];
    for ( int i = ( ( GroupIdxY - 2 ) >> 1 ) - 1 ; i >= 0; i-- )
    {
      m_BinEncoder.encodeBinEP( ( posY >> i ) & 1 );
    }
  }
}



#if JVET_K0072
void CABACWriter::residual_coding_subblock( CoeffCodingContext& cctx, const TCoeff* coeff, const int stateTransTable, int& state )
{
  //===== init =====
  const int   minSubPos   = cctx.minSubPos();
  const bool  isLast      = cctx.isLast();
  int         firstSigPos = ( isLast ? cctx.scanPosLast() : cctx.maxSubPos() );
  int         nextSigPos  = firstSigPos;

  //===== encode significant_coeffgroup_flag =====
  if( !isLast && cctx.isNotFirst() )
  {
    if( cctx.isSigGroup() )
    {
      m_BinEncoder.encodeBin( 1, cctx.sigGroupCtxId() );
    }
    else
    {
      m_BinEncoder.encodeBin( 0, cctx.sigGroupCtxId() );
      return;
    }
  }

  uint8_t   ctxOffset[16];
  unsigned  nextPass = 0;

  //===== encode absolute values =====
  const int inferSigPos   = nextSigPos != cctx.scanPosLast() ? ( cctx.isNotFirst() ? minSubPos : -1 ) : nextSigPos;
#if HEVC_USE_SIGN_HIDING
  int       firstNZPos    = nextSigPos;
  int       lastNZPos     = -1;
#endif
  int       remAbsLevel   = -1;
  int       numNonZero    =  0;
  unsigned  signPattern   =  0;

  for( ; nextSigPos >= minSubPos; nextSigPos-- )
  {
    TCoeff    Coeff      = coeff[ cctx.blockPos( nextSigPos ) ];
    unsigned  sigFlag    = ( Coeff != 0 );
    if( numNonZero || nextSigPos != inferSigPos )
    {
      const unsigned sigCtxId = cctx.sigCtxIdAbs( nextSigPos, coeff, state );
      m_BinEncoder.encodeBin( sigFlag, sigCtxId );
      DTRACE( g_trace_ctx, D_SYNTAX_RESI, "sig_bin() bin=%d ctx=%d\n", sigFlag, sigCtxId );
    }

    if( sigFlag )
    {
      uint8_t&  ctxOff  = ctxOffset[ nextSigPos - minSubPos ];
      ctxOff            = cctx.ctxOffsetAbs();
      numNonZero++;
#if HEVC_USE_SIGN_HIDING
      firstNZPos  = nextSigPos;
      lastNZPos   = std::max<int>( lastNZPos, nextSigPos );
#endif
      remAbsLevel = abs( Coeff ) - 1;

      if( nextSigPos != cctx.scanPosLast() ) signPattern <<= 1;
      if( Coeff < 0 )                        signPattern++;

      m_BinEncoder.encodeBin( remAbsLevel&1, cctx.parityCtxIdAbs(ctxOff) );
      DTRACE( g_trace_ctx, D_SYNTAX_RESI, "par_flag() bin=%d ctx=%d\n", remAbsLevel&1, cctx.parityCtxIdAbs(ctxOff) );
      remAbsLevel >>= 1;

      unsigned gt1 = !!remAbsLevel;
      m_BinEncoder.encodeBin( gt1, cctx.greater1CtxIdAbs(ctxOff) );
      DTRACE( g_trace_ctx, D_SYNTAX_RESI, "gt1_flag() bin=%d ctx=%d\n", gt1, cctx.greater1CtxIdAbs(ctxOff) );
      nextPass |= gt1;
    }

    state = ( stateTransTable >> ((state<<2)+((Coeff&1)<<1)) ) & 3;
  }


  //===== 2nd PASS: gt2 =====
  if( nextPass )
  {
    nextPass = 0;
    for( int scanPos = firstSigPos; scanPos >= minSubPos; scanPos-- )
    {
      unsigned absLevel = abs( coeff[ cctx.blockPos( scanPos ) ] );
      if( absLevel > 2 )
      {
        uint8_t& ctxOff = ctxOffset[ scanPos - minSubPos ];
        unsigned gt2    = ( absLevel > 4 );
        m_BinEncoder.encodeBin( gt2, cctx.greater2CtxIdAbs(ctxOff) );
        DTRACE( g_trace_ctx, D_SYNTAX_RESI, "gt2_flag() bin=%d ctx=%d\n", gt2, cctx.greater2CtxIdAbs(ctxOff) );
        nextPass |= gt2;
      }
    }
  }

  //===== 3rd PASS: Go-rice codes =====
  if( nextPass )
  {
    for( int scanPos = firstSigPos; scanPos >= minSubPos; scanPos-- )
    {
      unsigned absLevel = abs( coeff[ cctx.blockPos( scanPos ) ] );
      if( absLevel > 4 )
      {
        unsigned rem     = ( absLevel - 5 ) >> 1;
        unsigned ricePar = cctx.GoRiceParAbs( scanPos, coeff );
        m_BinEncoder.encodeRemAbsEP( rem, ricePar, cctx.extPrec(), cctx.maxLog2TrDRange() );
        DTRACE( g_trace_ctx, D_SYNTAX_RESI, "rem_val() bin=%d ctx=%d\n", rem, ricePar );
      }
    }
  }

  //===== encode sign's =====
#if HEVC_USE_SIGN_HIDING
  unsigned numSigns = numNonZero;
  if( cctx.hideSign( firstNZPos, lastNZPos ) )
  {
    numSigns    --;
    signPattern >>= 1;
  }
  m_BinEncoder.encodeBinsEP( signPattern, numSigns );
#else
  m_BinEncoder.encodeBinsEP( signPattern, numNonZero );
#endif
#if JVET_K1000_SIMPLIFIED_EMT
  cctx.setEmtNumSigCoeff(numNonZero);
#endif
}

#else

void CABACWriter::residual_coding_subblock( CoeffCodingContext& cctx, const TCoeff* coeff )
{
  //===== init =====
  const int   maxSbbSize  = 1 << cctx.log2CGSize();
  const int   minSubPos   = cctx.minSubPos();
  const bool  isLast      = cctx.isLast();
  int         nextSigPos  = ( isLast ? cctx.scanPosLast() : cctx.maxSubPos() );

  //===== encode significant_coeffgroup_flag =====
  if( !isLast && cctx.isNotFirst() )
  {
    if( cctx.isSigGroup() )
    {
      m_BinEncoder.encodeBin( 1, cctx.sigGroupCtxId() );
    }
    else
    {
      m_BinEncoder.encodeBin( 0, cctx.sigGroupCtxId() );
      return;
    }
  }

#if JEM_TOOLS
  if( cctx.altResiCompId() == 1 )
  {
    //===== encode significant_coeff_flag's =====
    const int inferSigPos = ( cctx.isNotFirst() ? minSubPos : -1 );
    unsigned  numNonZero = 0;
#if HEVC_USE_SIGN_HIDING
    int       firstNZPos = maxSbbSize;
    int       lastNZPos = -1;
#endif
    int       pos     [ 1 << MLS_CG_SIZE ];
    int       absCoeff[ 1 << MLS_CG_SIZE ];
    unsigned  signPattern = 0;

    if( isLast )
    {
#if HEVC_USE_SIGN_HIDING
      firstNZPos = nextSigPos;
      lastNZPos = std::max<int>( lastNZPos, nextSigPos );
#endif
      pos   [ numNonZero ] = nextSigPos;
      TCoeff    Coeff = coeff[ cctx.blockPos( nextSigPos-- ) ];
      absCoeff[ numNonZero++ ] = ( Coeff > 0 ? Coeff : ( signPattern++, -Coeff ) );
    }
    for( ; nextSigPos >= minSubPos; nextSigPos-- )
    {
      TCoeff    Coeff = coeff[ cctx.blockPos( nextSigPos ) ];
      unsigned  sigFlag = ( Coeff != 0 );
      if( numNonZero || nextSigPos != inferSigPos )
      {
        m_BinEncoder.encodeBin( sigFlag, cctx.sigCtxId( nextSigPos, coeff ) );
      }
      if( sigFlag )
      {
#if HEVC_USE_SIGN_HIDING
        firstNZPos = nextSigPos;
        lastNZPos = std::max<int>( lastNZPos, nextSigPos );
#endif
        signPattern <<= 1;
        pos   [ numNonZero ] = nextSigPos;
        absCoeff[ numNonZero++ ] = ( Coeff > 0 ? Coeff : ( signPattern++, -Coeff ) );
      }
    }


    //===== decode abs_greater1_flag's =====
    const unsigned  numGt1Flags = std::min<unsigned>( numNonZero, C1FLAG_NUMBER );
    int             gt2FlagIdx = maxSbbSize;
    bool            escapeData = false;
    unsigned        ctxG1      = cctx.greater1CtxId( 0 );
    for( unsigned k = 0; k < numGt1Flags; k++ )
    {
      if( k || !cctx.isLast() )
      {
        ctxG1 = cctx.greater1CtxId( pos[ k ], coeff );
      }

      if( absCoeff[ k ] > 1 )
      {
        m_BinEncoder.encodeBin( 1, ctxG1 );
        if( gt2FlagIdx < maxSbbSize )
        {
          escapeData = true;
        }
        else
        {
          gt2FlagIdx = k;
        }
      }
      else
      {
        m_BinEncoder.encodeBin( 0, ctxG1 );
      }
    }
    escapeData = escapeData || ( numGt1Flags < numNonZero );


    //===== decode abs_greater2_flag =====
    if( gt2FlagIdx < maxSbbSize )
    {
      unsigned ctxG2 = cctx.greater1CtxId( 0 );
      if( gt2FlagIdx || !cctx.isLast() )
      {
        ctxG2 = cctx.greater2CtxId( pos[ gt2FlagIdx ], coeff );
      }

      if( absCoeff[ gt2FlagIdx ] > 2 )
      {
        m_BinEncoder.encodeBin( 1, ctxG2 );
        escapeData = true;
      }
      else
      {
        m_BinEncoder.encodeBin( 0, ctxG2 );
      }
    }


    //===== align data =====
    if( escapeData && cctx.alignFlag() )
    {
      m_BinEncoder.align();
    }


    //===== decode remaining absolute values =====
    if( escapeData )
    {
      bool      updateGoRiceStats = cctx.updGoRiceStats();
      unsigned  GoRicePar = cctx.currGoRiceStats() >> 2;
      unsigned  MaxGoRicePar = ( updateGoRiceStats ? std::numeric_limits<unsigned>::max() : 4 );
      int       baseLevel = 3;
      for( int k = 0; k < numNonZero; k++ )
      {
        if( absCoeff[ k ] >= baseLevel )
        {
          int remAbs = absCoeff[ k ] - baseLevel;
          if( !updateGoRiceStats || !cctx.isLast() )
          {
            GoRicePar = cctx.GoRicePar( pos[ k ], coeff );
          }
          m_BinEncoder.encodeRemAbsEP( remAbs, GoRicePar, cctx.extPrec(), cctx.maxLog2TrDRange(), true );

          // update rice parameter
          if( absCoeff[ k ] >( 3 << GoRicePar ) )
          {
            GoRicePar = std::min<unsigned>( MaxGoRicePar, GoRicePar + 1 );
          }
          if( updateGoRiceStats )
          {
            unsigned initGoRicePar = cctx.currGoRiceStats() >> 2;
            if( remAbs >= ( 3 << initGoRicePar ) )
            {
              cctx.incGoRiceStats();
            }
            else if( cctx.currGoRiceStats() > 0 && ( remAbs << 1 ) < ( 1 << initGoRicePar ) )
            {
              cctx.decGoRiceStats();
            }
            updateGoRiceStats = false;
          }
        }
        if( k > C1FLAG_NUMBER - 2 )
        {
          baseLevel = 1;
        }
        else if( baseLevel == 3 && absCoeff[ k ] > 1 )
        {
          baseLevel = 2;
        }
      }
    }

    //===== decode sign's =====
#if HEVC_USE_SIGN_HIDING
    unsigned numSigns = numNonZero;
    if( cctx.hideSign( firstNZPos, lastNZPos ) )
    {
      numSigns--;
      signPattern >>= 1;
    }
    m_BinEncoder.encodeBinsEP( signPattern, numSigns );
#else
    m_BinEncoder.encodeBinsEP( signPattern, numNonZero );
#endif
    cctx.setEmtNumSigCoeff(numNonZero);
  }
  else
#endif
  {
    //===== encode significant_coeff_flag's =====
    const int inferSigPos = ( cctx.isNotFirst() ? minSubPos : -1 );
    unsigned  numNonZero  = 0;
#if HEVC_USE_SIGN_HIDING
    int       firstNZPos  = maxSbbSize;
    int       lastNZPos   = -1;
#endif
    int       absCoeff    [ 1 << MLS_CG_SIZE ];
    unsigned  signPattern = 0;
    if( isLast )
    {
#if HEVC_USE_SIGN_HIDING
      firstNZPos                    = nextSigPos;
      lastNZPos                     = std::max<int>( lastNZPos, nextSigPos );
#endif
      TCoeff    Coeff               = coeff[ cctx.blockPos( nextSigPos-- ) ];
      absCoeff[ numNonZero++ ]      = ( Coeff > 0 ? Coeff : ( signPattern++, -Coeff ) );
    }
    for( ; nextSigPos >= minSubPos; nextSigPos-- )
    {
      TCoeff    Coeff               = coeff[ cctx.blockPos( nextSigPos ) ];
      unsigned  sigFlag             = ( Coeff != 0 );
      if( numNonZero || nextSigPos != inferSigPos )
      {
        m_BinEncoder.encodeBin( sigFlag, cctx.sigCtxId( nextSigPos ) );
      }
      if( sigFlag )
      {
#if HEVC_USE_SIGN_HIDING
        firstNZPos                = nextSigPos;
        lastNZPos                 = std::max<int>( lastNZPos, nextSigPos );
#endif
        signPattern             <<= 1;
        absCoeff[ numNonZero++ ]  = ( Coeff > 0 ? Coeff : ( signPattern++, -Coeff ) );
      }
    }


    //===== decode abs_greater1_flag's =====
    const unsigned  numGt1Flags = std::min<unsigned>( numNonZero, C1FLAG_NUMBER );
    int             gt2FlagIdx  = maxSbbSize;
    bool            escapeData  = false;
    uint16_t        ctxGt1Id    = 1;
    for( unsigned k = 0; k < numGt1Flags; k++ )
    {
      if( absCoeff[ k ] > 1 )
      {
        m_BinEncoder.encodeBin( 1, cctx.greater1CtxId( ctxGt1Id ) );
        ctxGt1Id      = 0;
        if( gt2FlagIdx < maxSbbSize )
        {
          escapeData  = true;
        }
        else
        {
          gt2FlagIdx  = k;
        }
      }
      else
      {
        m_BinEncoder.encodeBin( 0, cctx.greater1CtxId( ctxGt1Id ) );
        if( ctxGt1Id && ctxGt1Id < 3 )
        {
          ctxGt1Id++;
        }
      }
    }
    escapeData = escapeData || ( numGt1Flags < numNonZero );
    cctx.setGt2Flag( ctxGt1Id == 0 );


    //===== decode abs_greater2_flag =====
    if( gt2FlagIdx < maxSbbSize )
    {
      if( absCoeff[ gt2FlagIdx ] > 2 )
      {
        m_BinEncoder.encodeBin( 1, cctx.greater2CtxId() );
        escapeData  = true;
      }
      else
      {
        m_BinEncoder.encodeBin( 0, cctx.greater2CtxId() );
      }
    }


    //===== align data =====
    if( escapeData && cctx.alignFlag() )
    {
      m_BinEncoder.align();
    }


    //===== decode sign's =====
#if HEVC_USE_SIGN_HIDING
    unsigned numSigns = numNonZero;
    if( cctx.hideSign( firstNZPos, lastNZPos ) )
    {
      numSigns    --;
      signPattern >>= 1;
    }
    m_BinEncoder.encodeBinsEP( signPattern, numSigns );
#else
    m_BinEncoder.encodeBinsEP( signPattern, numNonZero );
#endif

    //===== decode remaining absolute values =====
    if( escapeData )
    {
      bool      updateGoRiceStats = cctx.updGoRiceStats();
      unsigned  GoRicePar         = cctx.currGoRiceStats() >> 2;
      unsigned  MaxGoRicePar      = ( updateGoRiceStats ? std::numeric_limits<unsigned>::max() : 4 );
      int       baseLevel         = 3;
      for( int k = 0; k < numNonZero; k++ )
      {
        if( absCoeff[ k ] >= baseLevel )
        {
          int remAbs    = absCoeff[ k ] - baseLevel;
          m_BinEncoder.encodeRemAbsEP( remAbs, GoRicePar, cctx.extPrec(), cctx.maxLog2TrDRange() );

          // update rice parameter
          if( absCoeff[ k ] > ( 3 << GoRicePar ) )
          {
            GoRicePar = std::min<unsigned>( MaxGoRicePar, GoRicePar + 1 );
          }
          if( updateGoRiceStats )
          {
            unsigned initGoRicePar = cctx.currGoRiceStats() >> 2;
            if( remAbs >= ( 3 << initGoRicePar) )
            {
              cctx.incGoRiceStats();
            }
            else if( cctx.currGoRiceStats() > 0 && ( remAbs << 1 ) < ( 1 << initGoRicePar ) )
            {
              cctx.decGoRiceStats();
            }
            updateGoRiceStats = false;
          }
        }
        if( k > C1FLAG_NUMBER - 2 )
        {
          baseLevel = 1;
        }
        else if( baseLevel == 3 && absCoeff[ k ] > 1 )
        {
          baseLevel = 2;
        }
      }
    }
#if JEM_TOOLS

    cctx.setEmtNumSigCoeff(numNonZero);
#endif
  }
}
#endif





//================================================================================
//  clause 7.3.8.12
//--------------------------------------------------------------------------------
//    void  cross_comp_pred( tu, compID )
//================================================================================

void CABACWriter::cross_comp_pred( const TransformUnit& tu, ComponentID compID )
{
  CHECK(!( !isLuma( compID ) ), "Unspecified error");
  signed char alpha   = tu.compAlpha[compID];
  unsigned    ctxBase = ( compID == COMPONENT_Cr ? 5 : 0 );
  if( alpha == 0 )
  {
    m_BinEncoder.encodeBin( 0, Ctx::CrossCompPred( ctxBase ) );
    DTRACE( g_trace_ctx, D_SYNTAX, "cross_comp_pred() etype=%d pos=(%d,%d) alpha=%d\n", compID, tu.blocks[compID].x, tu.blocks[compID].y, tu.compAlpha[compID] );
    return;
  }

  static const unsigned log2AbsAlphaMinus1Table[8] = { 0, 1, 1, 2, 2, 2, 3, 3 };
  unsigned sign = ( alpha < 0 );
  if( sign )
  {
    alpha = -alpha;
  }
  CHECK(!( alpha <= 8 ), "Unspecified error");
  m_BinEncoder.encodeBin( 1, Ctx::CrossCompPred(ctxBase) );
  if( alpha > 1)
  {
     m_BinEncoder.encodeBin( 1, Ctx::CrossCompPred(ctxBase+1) );
     unary_max_symbol( log2AbsAlphaMinus1Table[alpha-1]-1, Ctx::CrossCompPred(ctxBase+2), Ctx::CrossCompPred(ctxBase+3), 2 );
  }
  else
  {
     m_BinEncoder.encodeBin( 0, Ctx::CrossCompPred(ctxBase+1) );
  }
  m_BinEncoder.encodeBin( sign, Ctx::CrossCompPred(ctxBase+4) );

  DTRACE( g_trace_ctx, D_SYNTAX, "cross_comp_pred() etype=%d pos=(%d,%d) alpha=%d\n", compID, tu.blocks[compID].x, tu.blocks[compID].y, tu.compAlpha[compID] );
}




//================================================================================
//  helper functions
//--------------------------------------------------------------------------------
//    void  unary_max_symbol  ( symbol, ctxId0, ctxIdN, maxSymbol )
//    void  unary_max_eqprob  ( symbol,                 maxSymbol )
//    void  exp_golomb_eqprob ( symbol, count )
//================================================================================

void CABACWriter::unary_max_symbol( unsigned symbol, unsigned ctxId0, unsigned ctxIdN, unsigned maxSymbol )
{
  CHECK( symbol > maxSymbol, "symbol > maxSymbol" );
  const unsigned totalBinsToWrite = std::min( symbol + 1, maxSymbol );
  for( unsigned binsWritten = 0; binsWritten < totalBinsToWrite; ++binsWritten )
  {
    const unsigned nextBin = symbol > binsWritten;
    m_BinEncoder.encodeBin( nextBin, binsWritten == 0 ? ctxId0 : ctxIdN );
  }
}


void CABACWriter::unary_max_eqprob( unsigned symbol, unsigned maxSymbol )
{
  if( maxSymbol == 0 )
  {
    return;
  }
  bool     codeLast = ( maxSymbol > symbol );
  unsigned bins     = 0;
  unsigned numBins  = 0;
  while( symbol-- )
  {
    bins   <<= 1;
    bins   ++;
    numBins++;
  }
  if( codeLast )
  {
    bins  <<= 1;
    numBins++;
  }
  CHECK(!( numBins <= 32 ), "Unspecified error");
  m_BinEncoder.encodeBinsEP( bins, numBins );
}


void CABACWriter::exp_golomb_eqprob( unsigned symbol, unsigned count )
{
  unsigned bins    = 0;
  unsigned numBins = 0;
  while( symbol >= (unsigned)(1<<count) )
  {
    bins <<= 1;
    bins++;
    numBins++;
    symbol -= 1 << count;
    count++;
  }
  bins <<= 1;
  numBins++;
  bins = (bins << count) | symbol;
  numBins += count;
  CHECK(!( numBins <= 32 ), "Unspecified error");
  m_BinEncoder.encodeBinsEP( bins, numBins );
}

void CABACWriter::encode_sparse_dt( DecisionTree& dt, unsigned toCodeId )
{
  // propagate the sparsity information from end-nodes to intermediate nodes
  dt.reduce();

  unsigned depth  = dt.dtt.depth;
  unsigned offset = 0;

  const unsigned encElPos = dt.dtt.mapping[toCodeId];

  while( dt.dtt.hasSub[offset] )
  {
    CHECKD( depth == 0, "Depth is '0' for a decision node in a decision tree" );

    const unsigned posRight = offset + 1;
    const unsigned posLeft  = offset + ( 1u << depth );

    const bool isLeft = encElPos >= posLeft;

    if( dt.isAvail[posRight] && dt.isAvail[posLeft] )
    {
      // encode the decision as both sub-paths are available
      const unsigned ctxId = dt.ctxId[offset];

      if( ctxId > 0 )
      {
        DTRACE( g_trace_ctx, D_DECISIONTREE, "Decision coding using context %d\n", ctxId - 1 );
        m_BinEncoder.encodeBin( isLeft ? 0 : 1, ctxId - 1 );
      }
      else
      {
        DTRACE( g_trace_ctx, D_DECISIONTREE, "Decision coding as an EP bin\n" );
        m_BinEncoder.encodeBinEP( isLeft ? 0 : 1 );
      }
    }

    DTRACE( g_trace_ctx, D_DECISIONTREE, "Following the tree to the %s sub-node\n", isLeft ? "left" : "right" );

    offset = isLeft ? posLeft : posRight;
    depth--;
  }

  CHECKD( offset != encElPos,             "Encoded a different element than assigned" );
  CHECKD( dt.dtt.ids[offset] != toCodeId, "Encoded a different element than assigned" );
  CHECKD( dt.isAvail[offset] == false,    "The encoded element is not available" );
  DTRACE( g_trace_ctx, D_DECISIONTREE,    "Found an end-node of the tree\n" );
  return;
}

#if JVET_K0371_ALF
void CABACWriter::codeAlfCtuEnableFlags( CodingStructure& cs, ChannelType channel, AlfSliceParam* alfParam)
{
  if( isLuma( channel ) )
  {
    if (alfParam->enabledFlag[COMPONENT_Y])
      codeAlfCtuEnableFlags( cs, COMPONENT_Y, alfParam );
  }
  else
  {
    if (alfParam->enabledFlag[COMPONENT_Cb])
      codeAlfCtuEnableFlags( cs, COMPONENT_Cb, alfParam );
    if (alfParam->enabledFlag[COMPONENT_Cr])
      codeAlfCtuEnableFlags( cs, COMPONENT_Cr, alfParam );
  }
}
void CABACWriter::codeAlfCtuEnableFlags( CodingStructure& cs, ComponentID compID, AlfSliceParam* alfParam)
{
  uint32_t numCTUs = cs.pcv->sizeInCtus;

  for( int ctuIdx = 0; ctuIdx < numCTUs; ctuIdx++ )
  {
    codeAlfCtuEnableFlag( cs, ctuIdx, compID, alfParam );
  }
}

void CABACWriter::codeAlfCtuEnableFlag( CodingStructure& cs, uint32_t ctuRsAddr, const int compIdx, AlfSliceParam* alfParam)
{
  AlfSliceParam& alfSliceParam = alfParam ? (*alfParam) : cs.slice->getAlfSliceParam();

  if( cs.sps->getUseALF() && alfSliceParam.enabledFlag[compIdx] )
  {
    const PreCalcValues& pcv = *cs.pcv;
    int                 frame_width_in_ctus = pcv.widthInCtus;
    int                 ry = ctuRsAddr / frame_width_in_ctus;
    int                 rx = ctuRsAddr - ry * frame_width_in_ctus;
    const Position      pos( rx * cs.pcv->maxCUWidth, ry * cs.pcv->maxCUHeight );
    const uint32_t          curSliceIdx = cs.slice->getIndependentSliceIdx();
#if HEVC_TILES_WPP
    const uint32_t          curTileIdx = cs.picture->tileMap->getTileIdxMap( pos );
    bool                leftMergeAvail = cs.getCURestricted( pos.offset( -(int)pcv.maxCUWidth, 0 ), curSliceIdx, curTileIdx, CH_L ) ? true : false;
    bool                aboveMergeAvail = cs.getCURestricted( pos.offset( 0, -(int)pcv.maxCUHeight ), curSliceIdx, curTileIdx, CH_L ) ? true : false;
#else
    bool                leftAvail = cs.getCURestricted( pos.offset( -(int)pcv.maxCUWidth, 0 ), curSliceIdx, CH_L ) ? true : false;
    bool                aboveAvail = cs.getCURestricted( pos.offset( 0, -(int)pcv.maxCUHeight ), curSliceIdx, CH_L ) ? true : false;
#endif

    int leftCTUAddr = leftAvail ? ctuRsAddr - 1 : -1;
    int aboveCTUAddr = aboveAvail ? ctuRsAddr - frame_width_in_ctus : -1;

    if( alfSliceParam.enabledFlag[compIdx] )
    {
      uint8_t* ctbAlfFlag = cs.slice->getPic()->getAlfCtuEnableFlag( compIdx );
      if( alfSliceParam.chromaCtbPresentFlag && compIdx )
      {
        CHECK( !ctbAlfFlag[ctuRsAddr], "ALF chroma CTB enable flag must be 1 with chromaCtbPresentFlag = 1" );
      }
      else
      {
        int ctx = 0;
        ctx += leftCTUAddr > -1 ? ( ctbAlfFlag[leftCTUAddr] ? 1 : 0 ) : 0;
        ctx += aboveCTUAddr > -1 ? ( ctbAlfFlag[aboveCTUAddr] ? 1 : 0 ) : 0;
        m_BinEncoder.encodeBin( ctbAlfFlag[ctuRsAddr], Ctx::ctbAlfFlag( compIdx * 3 + ctx ) );
      }
    }
  }
}
#endif

//! \}
