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

/** \file     CABACReader.cpp
 *  \brief    Reader for low level syntax
 */

#include "CABACReader.h"

#include "CommonLib/CodingStructure.h"
#include "CommonLib/TrQuant.h"
#include "CommonLib/UnitTools.h"
#include "CommonLib/SampleAdaptiveOffset.h"
#if JEM_TOOLS
#include "CommonLib/AdaptiveLoopFilter.h"
#endif
#include "CommonLib/dtrace_next.h"
#include "CommonLib/Picture.h"

#if RExt__DECODER_DEBUG_BIT_STATISTICS
#include "CommonLib/CodingStatistics.h"
#endif

#if RExt__DECODER_DEBUG_BIT_STATISTICS
#define RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET(x)           const CodingStatisticsClassType CSCT(x);                       m_BinDecoder.set( CSCT )
#define RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET2(x,y)        const CodingStatisticsClassType CSCT(x,y);                     m_BinDecoder.set( CSCT )
#define RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE(x,s)    const CodingStatisticsClassType CSCT(x, s.width, s.height);    m_BinDecoder.set( CSCT )
#define RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE2(x,s,z) const CodingStatisticsClassType CSCT(x, s.width, s.height, z); m_BinDecoder.set( CSCT )
#define RExt__DECODER_DEBUG_BIT_STATISTICS_SET(x)                  m_BinDecoder.set( x );
#else
#define RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET(x)
#define RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET2(x,y)
#define RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE(x,s)
#define RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE2(x,s,z)
#define RExt__DECODER_DEBUG_BIT_STATISTICS_SET(x)
#endif


#if JEM_TOOLS
void CABACReader::initCtxModels( Slice& slice, CABACDataStore* cabacDataStore )
#else
void CABACReader::initCtxModels( Slice& slice )
#endif
{
  SliceType sliceType  = slice.getSliceType();
  int       qp         = slice.getSliceQp();
  if( slice.getPPS()->getCabacInitPresentFlag() && slice.getCabacInitFlag() )
  {
    switch( sliceType )
    {
    case P_SLICE:           // change initialization table to B_SLICE initialization
      sliceType = B_SLICE;
      break;
    case B_SLICE:           // change initialization table to P_SLICE initialization
      sliceType = P_SLICE;
      break;
    default     :           // should not occur
      THROW( "Invalid slice type" );
      break;
    }
  }
  m_BinDecoder.reset( qp, (int)sliceType );
#if JEM_TOOLS
  if( cabacDataStore )
  {
#if JEM_TOOLS
    m_BinDecoder.setWinSizes( cabacDataStore->getWinSizes( &slice ) );
#endif
  }
#endif
}


//================================================================================
//  clause 7.3.8.1
//--------------------------------------------------------------------------------
//    bool  terminating_bit()
//    void  remaining_bytes( noTrailingBytesExpected )
//================================================================================

bool CABACReader::terminating_bit()
{
  if( m_BinDecoder.decodeBinTrm() )
  {
    m_BinDecoder.finish();
#if RExt__DECODER_DEBUG_BIT_STATISTICS
    CodingStatistics::IncrementStatisticEP( STATS__TRAILING_BITS, m_Bitstream->readOutTrailingBits(), 0 );
#else
    m_Bitstream->readOutTrailingBits();
#endif
    return true;
  }
  return false;
}

void CABACReader::remaining_bytes( bool noTrailingBytesExpected )
{
  if( noTrailingBytesExpected )
  {
    CHECK( 0 != m_Bitstream->getNumBitsLeft(), "Bits left when not supposed" );
  }
  else
  {
    while( m_Bitstream->getNumBitsLeft() )
    {
      unsigned trailingNullByte = m_Bitstream->readByte();
      if( trailingNullByte != 0 )
      {
        THROW( "Trailing byte should be '0', but has a value of " << std::hex << trailingNullByte << std::dec << "\n" );
      }
    }
  }
}





//================================================================================
//  clause 7.3.8.2
//--------------------------------------------------------------------------------
//    bool  coding_tree_unit( cs, area, qpL, qpC, ctuRsAddr )
//================================================================================

bool CABACReader::coding_tree_unit( CodingStructure& cs, const UnitArea& area, int (&qps)[2], unsigned ctuRsAddr )
{
  CUCtx cuCtx( qps[CH_L] );
  Partitioner *partitioner = PartitionerFactory::get( *cs.slice );

  partitioner->initCtu( area, CH_L, *cs.slice );


  sao( cs, ctuRsAddr );

#if JVET_K0371_ALF
  AlfSliceParam& alfSliceParam = cs.slice->getAlfSliceParam();

  if( cs.sps->getUseALF() && ( alfSliceParam.enabledFlag[COMPONENT_Y] || alfSliceParam.enabledFlag[COMPONENT_Cb] || alfSliceParam.enabledFlag[COMPONENT_Cr] ) )
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

    for( int compIdx = 0; compIdx < MAX_NUM_COMPONENT; compIdx++ )
    {
      if( alfSliceParam.enabledFlag[compIdx] )
      {
        uint8_t* ctbAlfFlag = cs.slice->getPic()->getAlfCtuEnableFlag( compIdx );
        int ctx = 0;
        ctx += leftCTUAddr > -1 ? ( ctbAlfFlag[leftCTUAddr] ? 1 : 0 ) : 0;
        ctx += aboveCTUAddr > -1 ? ( ctbAlfFlag[aboveCTUAddr] ? 1 : 0 ) : 0;

        if( compIdx && alfSliceParam.chromaCtbPresentFlag )
        {
          ctbAlfFlag[ctuRsAddr] = 1;
        }
        else
        {
          RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET(STATS__CABAC_BITS__ALF);
          ctbAlfFlag[ctuRsAddr] = m_BinDecoder.decodeBin( Ctx::ctbAlfFlag( compIdx * 3 + ctx ) );
        }
      }
    }
  }
#endif

#if JVET_K0230_DUAL_CODING_TREE_UNDER_64x64_BLOCK
  bool isLast = false;

  if ( CS::isDualITree(cs) && cs.pcv->chrFormat != CHROMA_400 && cs.pcv->maxCUWidth > 64 )
  {
    Partitioner *chromaPartitioner = PartitionerFactory::get(*cs.slice);
    chromaPartitioner->initCtu(area, CH_C, *cs.slice);
    CUCtx cuCtxChroma(qps[CH_C]);
    isLast = coding_tree(cs, *partitioner, cuCtx, chromaPartitioner, &cuCtxChroma);
    qps[CH_L] = cuCtx.qp;
    qps[CH_C] = cuCtxChroma.qp;
    delete chromaPartitioner;
  }
  else
  {
    isLast = coding_tree(cs, *partitioner, cuCtx);
#else
  bool isLast = coding_tree( cs, *partitioner, cuCtx );
#endif
  qps[CH_L] = cuCtx.qp;
  if( !isLast && CS::isDualITree( cs ) && cs.pcv->chrFormat != CHROMA_400 )
  {
    CUCtx cuCtxChroma( qps[CH_C] );
    partitioner->initCtu( area, CH_C, *cs.slice );
    isLast = coding_tree( cs, *partitioner, cuCtxChroma );
    qps[CH_C] = cuCtxChroma.qp;
  }
#if JVET_K0230_DUAL_CODING_TREE_UNDER_64x64_BLOCK
  }
#endif

  DTRACE_COND( ctuRsAddr == 0, g_trace_ctx, D_QP_PER_CTU, "\n%4d %2d", cs.picture->poc, cs.slice->getSliceQpBase() );
  DTRACE     (                 g_trace_ctx, D_QP_PER_CTU, " %3d",           qps[CH_L] - cs.slice->getSliceQpBase() );

  delete partitioner;
  return isLast;
}





//================================================================================
//  clause 7.3.8.3
//--------------------------------------------------------------------------------
//    void  sao( slice, ctuRsAddr )
//================================================================================

void CABACReader::sao( CodingStructure& cs, unsigned ctuRsAddr )
{
  const SPS&   sps   = *cs.sps;

  if( !sps.getUseSAO() )
  {
    return;
  }

  const Slice& slice                        = *cs.slice;
  SAOBlkParam&      sao_ctu_pars            = cs.picture->getSAO()[ctuRsAddr];
  bool              slice_sao_luma_flag     = ( slice.getSaoEnabledFlag( CHANNEL_TYPE_LUMA ) );
  bool              slice_sao_chroma_flag   = ( slice.getSaoEnabledFlag( CHANNEL_TYPE_CHROMA ) && sps.getChromaFormatIdc() != CHROMA_400 );
  sao_ctu_pars[ COMPONENT_Y  ].modeIdc      = SAO_MODE_OFF;
  sao_ctu_pars[ COMPONENT_Cb ].modeIdc      = SAO_MODE_OFF;
  sao_ctu_pars[ COMPONENT_Cr ].modeIdc      = SAO_MODE_OFF;
  if( !slice_sao_luma_flag && !slice_sao_chroma_flag )
  {
    return;
  }

  // merge
  int             frame_width_in_ctus     = cs.pcv->widthInCtus;
  int             ry                      = ctuRsAddr      / frame_width_in_ctus;
  int             rx                      = ctuRsAddr - ry * frame_width_in_ctus;
  int             sao_merge_type          = -1;
  const Position  pos( rx * cs.pcv->maxCUWidth, ry * cs.pcv->maxCUHeight );
  const unsigned  curSliceIdx = cs.slice->getIndependentSliceIdx();
#if HEVC_TILES_WPP
  const unsigned  curTileIdx  = cs.picture->tileMap->getTileIdxMap( pos );
#endif

  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__SAO );

#if HEVC_TILES_WPP
  if( cs.getCURestricted( pos.offset(-(int)cs.pcv->maxCUWidth, 0), curSliceIdx, curTileIdx, CH_L ) )
#else
  if( cs.getCURestricted( pos.offset(-(int)cs.pcv->maxCUWidth, 0), curSliceIdx, CH_L ) )
#endif
  {
    // sao_merge_left_flag
    sao_merge_type  += int( m_BinDecoder.decodeBin( Ctx::SaoMergeFlag() ) );
  }

#if HEVC_TILES_WPP
  if( sao_merge_type < 0 && cs.getCURestricted( pos.offset(0, -(int)cs.pcv->maxCUHeight), curSliceIdx, curTileIdx, CH_L ) )
#else
  if( sao_merge_type < 0 && cs.getCURestricted( pos.offset(0, -(int)cs.pcv->maxCUHeight), curSliceIdx, CH_L ) )
#endif
  {
    // sao_merge_above_flag
    sao_merge_type  += int( m_BinDecoder.decodeBin( Ctx::SaoMergeFlag() ) ) << 1;
  }
  if( sao_merge_type >= 0 )
  {
    if( slice_sao_luma_flag || slice_sao_chroma_flag )
    {
      sao_ctu_pars[ COMPONENT_Y  ].modeIdc  = SAO_MODE_MERGE;
      sao_ctu_pars[ COMPONENT_Y  ].typeIdc  = sao_merge_type;
    }
    if( slice_sao_chroma_flag )
    {
      sao_ctu_pars[ COMPONENT_Cb ].modeIdc  = SAO_MODE_MERGE;
      sao_ctu_pars[ COMPONENT_Cr ].modeIdc  = SAO_MODE_MERGE;
      sao_ctu_pars[ COMPONENT_Cb ].typeIdc  = sao_merge_type;
      sao_ctu_pars[ COMPONENT_Cr ].typeIdc  = sao_merge_type;
    }
    return;
  }

  // explicit parameters
  ComponentID firstComp = ( slice_sao_luma_flag   ? COMPONENT_Y  : COMPONENT_Cb );
  ComponentID lastComp  = ( slice_sao_chroma_flag ? COMPONENT_Cr : COMPONENT_Y  );
  for( ComponentID compID = firstComp; compID <= lastComp; compID = ComponentID( compID + 1 ) )
  {
    SAOOffset& sao_pars = sao_ctu_pars[ compID ];

    // sao_type_idx_luma / sao_type_idx_chroma
    if( compID != COMPONENT_Cr )
    {
      if( m_BinDecoder.decodeBin( Ctx::SaoTypeIdx() ) )
      {
        if( m_BinDecoder.decodeBinEP( ) )
        {
          // edge offset
          sao_pars.modeIdc = SAO_MODE_NEW;
          sao_pars.typeIdc = SAO_TYPE_START_EO;
        }
        else
        {
          // band offset
          sao_pars.modeIdc = SAO_MODE_NEW;
          sao_pars.typeIdc = SAO_TYPE_START_BO;
        }
      }
    }
    else //Cr, follow Cb SAO type
    {
      sao_pars.modeIdc = sao_ctu_pars[ COMPONENT_Cb ].modeIdc;
      sao_pars.typeIdc = sao_ctu_pars[ COMPONENT_Cb ].typeIdc;
    }
    if( sao_pars.modeIdc == SAO_MODE_OFF )
    {
      continue;
    }

    // sao_offset_abs
    int       offset[4];
    const int maxOffsetQVal = SampleAdaptiveOffset::getMaxOffsetQVal( sps.getBitDepth( toChannelType(compID) ) );
    offset    [0]           = (int)unary_max_eqprob( maxOffsetQVal );
    offset    [1]           = (int)unary_max_eqprob( maxOffsetQVal );
    offset    [2]           = (int)unary_max_eqprob( maxOffsetQVal );
    offset    [3]           = (int)unary_max_eqprob( maxOffsetQVal );

    // band offset mode
    if( sao_pars.typeIdc == SAO_TYPE_START_BO )
    {
      // sao_offset_sign
      for( int k = 0; k < 4; k++ )
      {
        if( offset[k] && m_BinDecoder.decodeBinEP( ) )
        {
          offset[k] = -offset[k];
        }
      }
      // sao_band_position
      sao_pars.typeAuxInfo = m_BinDecoder.decodeBinsEP( NUM_SAO_BO_CLASSES_LOG2 );
      for( int k = 0; k < 4; k++ )
      {
        sao_pars.offset[ ( sao_pars.typeAuxInfo + k ) % MAX_NUM_SAO_CLASSES ] = offset[k];
      }
      continue;
    }

    // edge offset mode
    sao_pars.typeAuxInfo = 0;
    if( compID != COMPONENT_Cr )
    {
      // sao_eo_class_luma / sao_eo_class_chroma
      sao_pars.typeIdc += m_BinDecoder.decodeBinsEP( NUM_SAO_EO_TYPES_LOG2 );
    }
    else
    {
      sao_pars.typeIdc  = sao_ctu_pars[ COMPONENT_Cb ].typeIdc;
    }
    sao_pars.offset[ SAO_CLASS_EO_FULL_VALLEY ] =  offset[0];
    sao_pars.offset[ SAO_CLASS_EO_HALF_VALLEY ] =  offset[1];
    sao_pars.offset[ SAO_CLASS_EO_PLAIN       ] =  0;
    sao_pars.offset[ SAO_CLASS_EO_HALF_PEAK   ] = -offset[2];
    sao_pars.offset[ SAO_CLASS_EO_FULL_PEAK   ] = -offset[3];
  }
}


#if JEM_TOOLS
#if !JVET_K0371_ALF
uint32_t CABACReader::parseAlfUvlc ()
{
  uint32_t uiCode;
  int  i;

  uiCode = m_BinDecoder.decodeBinEP();
  if ( uiCode == 0 )
  {
    return 0;
  }

  i=1;
  while (1)
  {
    uiCode = m_BinDecoder.decodeBinEP();
    if ( uiCode == 0 ) break;
    i++;
  }
  return i;
}

int CABACReader::parseAlfSvlc()
{
  uint32_t uiCode;
  int  iSign;
  int  i;

  uiCode = m_BinDecoder.decodeBinEP();
  if ( uiCode == 0 )
  {
    return 0;
  }

  // read sign
  uiCode = m_BinDecoder.decodeBinEP();

  if ( uiCode == 0 ) iSign =  1;
  else               iSign = -1;

  // read magnitude
  i=1;
  while (1)
  {
    uiCode = m_BinDecoder.decodeBinEP();
    if ( uiCode == 0 ) break;
    i++;
  }
  return i*iSign;
}
#endif

void CABACReader::xReadTruncBinCode(uint32_t& ruiSymbol, uint32_t uiMaxSymbol)
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
  else
  {
    uiThresh = g_NonMPM[uiMaxSymbol];
  }

  uint32_t uiVal = 1 << uiThresh;
  uint32_t b = uiMaxSymbol - uiVal;
  ruiSymbol = m_BinDecoder.decodeBinsEP(uiThresh);
  if (ruiSymbol >= uiVal - b)
  {
    uint32_t uiSymbol;
    uiSymbol = m_BinDecoder.decodeBinEP();
    ruiSymbol <<= 1;
    ruiSymbol += uiSymbol;
    ruiSymbol -= (uiVal - b);
  }
}

#if !JVET_K0371_ALF
uint32_t CABACReader::xReadEpExGolomb(uint32_t uiCount)
{
  uint32_t uiSymbol = 0;
  uint32_t uiBit = 1;

  while (uiBit)
  {
    uiBit = m_BinDecoder.decodeBinEP();
    uiSymbol += uiBit << uiCount++;
  }

  if (--uiCount)
  {
    uint32_t bins;
    bins = m_BinDecoder.decodeBinsEP(uiCount);
    uiSymbol += bins;
  }

  return uiSymbol;
}

int CABACReader::alfGolombDecode(int k)
{
  uint32_t uiSymbol;
  int q = -1;
  int nr = 0;
  int m = (int)pow(2.0, k);
  int a;

  uiSymbol = 1;
  while (uiSymbol)
  {
    uiSymbol = m_BinDecoder.decodeBinEP();
    q++;
  }
  for(a = 0; a < k; ++a)          // read out the sequential log2(M) bits
  {
    uiSymbol = m_BinDecoder.decodeBinEP();
    if(uiSymbol)
      nr += 1 << a;
  }
  nr += q * m;                    // add the bits and the multiple of M
  if(nr != 0)
  {
    uiSymbol = m_BinDecoder.decodeBinEP();
    nr = (uiSymbol)? nr: -nr;
  }
  return nr;
}



void CABACReader::alf( CodingStructure& cs )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__ALF );

  const SPSNext& spsNext = cs.sps->getSpsNext();
  if( !spsNext.getALFEnabled() )
  {
    return;
  }
  ALFParam& alfParam = cs.picture->getALFParam();
  {
    CHECK( alfParam.num_ctus_in_frame != cs.pcv->sizeInCtus, "inconsistent" );
    alfParam.reset();
  }

  uint32_t alfFlag = m_BinDecoder.decodeBinEP();
  if( !alfFlag )
  {
    return;
  }

  alfParam.alf_flag = 1;


#if COM16_C806_ALF_TEMPPRED_NUM
  if (cs.slice->getSliceType() == I_SLICE)
#else
  if (cs.slice->getSliceType() == I_SLICE )
#endif
  {
    alfParam.temporalPredFlag = 0;
  }
  else
  {
    alfParam.temporalPredFlag = m_BinDecoder.decodeBinEP();
    if( alfParam.temporalPredFlag )
    {
      alfParam.prevIdx = parseAlfUvlc();
    }
  }
  if( alfParam.temporalPredFlag == 0 )
  {
    alf_aux   ( alfParam, spsNext.getGALFEnabled() );
    alf_filter( alfParam, spsNext.getGALFEnabled() );
  }


  if( spsNext.getGALFEnabled() )
  {
    alfParam.chroma_idc = parseAlfUvlc();
  #if COM16_C806_ALF_TEMPPRED_NUM
    if (!alfParam.temporalPredFlag && alfParam.chroma_idc)
  #else
    if (alfParam.chroma_idc)
  #endif
    {
      alfParam.num_coeff_chroma = ((alfParam.tap_chroma*alfParam.tap_chroma) >> 2) + 1;
      alf_filter(alfParam, spsNext.getGALFEnabled(), true);
    }
  }
  else
  {
    alf_chroma( alfParam );
  }
  alf_cu_ctrl( alfParam );
}

void CABACReader::alf_aux( ALFParam& alfParam, bool isGALF )
{
  int FiltTab[3] = {5, 7, 9};
  int sqrFiltLengthTab[3] = {AdaptiveLoopFilter::m_SQR_FILT_LENGTH_5SYM, AdaptiveLoopFilter::m_SQR_FILT_LENGTH_7SYM, AdaptiveLoopFilter::m_SQR_FILT_LENGTH_9SYM };

  uint32_t uiSymbol;
  const int iNoVarBins = AdaptiveLoopFilter::m_NO_VAR_BINS;
  int i;
  if( isGALF )
  {
    memset(alfParam.filterPattern, 0, sizeof(int)*AdaptiveLoopFilter::m_NO_VAR_BINS);
    //number of total filters
    xReadTruncBinCode(uiSymbol, iNoVarBins);
    alfParam.filters_per_group = uiSymbol + 1;
  }

  AlfFilterType filtType = (AlfFilterType) parseAlfUvlc();
  alfParam.filterType =                   filtType;
  alfParam.tapH       =          FiltTab[ filtType ];
  alfParam.num_coeff  = sqrFiltLengthTab[ filtType ];

  if( isGALF )
  {
    //filter set index for each class
    if (alfParam.filters_per_group > 1)
    {
      for (i = 0; i< iNoVarBins; i++)
      {
        xReadTruncBinCode(uiSymbol, (uint32_t)alfParam.filters_per_group);
        alfParam.filterPattern[i] = (int)uiSymbol;
      }
    }
    else
    {
      memset(alfParam.filterPattern, 0, iNoVarBins* sizeof(int));
    }

#if JVET_C0038_NO_PREV_FILTERS
    int decodetab_pred[3] = { 1, 0, 2 };
    memset(alfParam.PrevFiltIdx, 0, sizeof(int)*AdaptiveLoopFilter::m_NO_VAR_BINS);

    if (alfParam.iAvailableFilters > 0)
    {
      // prediction pattern
      //0: all zero, no pred from pre-defined filters; 1: all are predicted but could be different values; 2: some predicted and some not

      alfParam.iPredPattern = decodetab_pred[xReadEpExGolomb(0)];

      if (alfParam.iPredPattern == 0)
      {
        memset(alfParam.PrevFiltIdx, 0, sizeof(int)*AdaptiveLoopFilter::m_NO_VAR_BINS);
      }
      else
      {
        if (alfParam.iPredPattern == 2)
        {
          //on/off flags
          for (i = 0; i < iNoVarBins; i++)
          {
            alfParam.PrevFiltIdx[i] = m_BinDecoder.decodeBinEP();
          }
        }
        else
        {
          CHECK(alfParam.iPredPattern != 1, "iPredPattern has to be equal 1");
          for (i = 0; i < iNoVarBins; i++)
          {
            alfParam.PrevFiltIdx[i] = 1;
          }
        }
        if (alfParam.iAvailableFilters > 1)
        {
          CHECK(alfParam.iPredPattern <= 0, "iPredPattern has to be greater 0");
          for (i = 0; i < iNoVarBins; i++)
          {
            if (alfParam.PrevFiltIdx[i] > 0)
            {
              xReadTruncBinCode(uiSymbol, (uint32_t)alfParam.iAvailableFilters);
              alfParam.PrevFiltIdx[i] = (uiSymbol + 1);
            }
          }
        }
      }
    }
#endif
  }
  else
  {
    alfParam.filters_per_group = 0;
    memset ( alfParam.filterPattern, 0 , sizeof(int)*AdaptiveLoopFilter::m_NO_VAR_BINS);

    AlfFilterMode fMode = (AlfFilterMode) parseAlfUvlc();

    alfParam.filterMode = fMode;

    if( fMode == ALF_ONE_FILTER )
    {
      alfParam.filters_per_group = 1;
    }
    else if( fMode == ALF_TWO_FILTERS )
    {
      alfParam.filters_per_group = 2;
      uint32_t symbol = parseAlfUvlc();
      alfParam.startSecondFilter = symbol;
      alfParam.filterPattern[ symbol ] = 1;
    }
    else if( fMode == ALF_MULTIPLE_FILTERS )
    {
      alfParam.filters_per_group = 1;
      for( int i=1; i< AdaptiveLoopFilter::m_NO_VAR_BINS; i++)
      {
        uint32_t symbol = m_BinDecoder.decodeBinEP();
        alfParam.filterPattern[i]   = symbol;
        alfParam.filters_per_group += symbol;
      }
    }
    else
    {
      THROW( "ALF: unknown filter mode" );
    }
  }

 memset( alfParam.mapClassToFilter, 0, AdaptiveLoopFilter::m_NO_VAR_BINS * sizeof(int));
 for(int i = 1; i < AdaptiveLoopFilter::m_NO_VAR_BINS; ++i)
 {
   if( alfParam.filterPattern[i])
   {
     alfParam.mapClassToFilter[i] = alfParam.mapClassToFilter[i-1] + 1;
   }
   else
   {
     alfParam.mapClassToFilter[i] = alfParam.mapClassToFilter[i-1];
   }
 }

}

void CABACReader::alf_filter( ALFParam& alfParam, bool isGALF, bool bChroma )
{
  uint32_t uiSymbol;
  int ind, scanPos, i;
  int golombIndexBit;
  int kMin;
  int maxScanVal = 0;
  const int* pDepthInt = nullptr;

#if FORCE0
  if (!bChroma)
  {
    alfParam.forceCoeff0 = m_BinDecoder.decodeBinEP();
    if (!alfParam.forceCoeff0)
    {
#endif
      for (int i = 0; i < AdaptiveLoopFilter::m_NO_VAR_BINS; i++)
      {
        alfParam.codedVarBins[i] = 1;
      }
      if (alfParam.filters_per_group > 1)
      {
#if !FORCE0
        alfParam.forceCoeff0 = 0;
#endif
        alfParam.predMethod = m_BinDecoder.decodeBinEP();
      }
      else
      {
        alfParam.forceCoeff0 = 0;
        alfParam.predMethod = 0;
      }
#if FORCE0
    }
    else
    {
      alfParam.predMethod = 0;
    }
  }
#endif

  //Determine maxScanVal
  if( bChroma)
  {
    pDepthInt = AdaptiveLoopFilter::m_pDepthIntTab[alfParam.tap_chroma == 5 ? 0 : (alfParam.tap_chroma == 7 ? 1 : 2)];
  }
  else
  {
    pDepthInt = AdaptiveLoopFilter::m_pDepthIntTab[alfParam.filterType];
  }
  int maxIdx = (bChroma ? alfParam.num_coeff_chroma : alfParam.num_coeff) - !!isGALF;
  for( int i = 0; i < maxIdx; i++ )
  {
    maxScanVal = std::max(maxScanVal, pDepthInt[i]);
  }

  uiSymbol = parseAlfUvlc();
  alfParam.minKStart = 1 + uiSymbol;

  kMin = alfParam.minKStart;
  for(scanPos = 0; scanPos < maxScanVal; scanPos++)
  {
     golombIndexBit = m_BinDecoder.decodeBinEP();
     if( golombIndexBit)
     {
       alfParam.kMinTab[scanPos] = kMin + 1;
     }
     else
     {
       alfParam.kMinTab[scanPos] = kMin;
     }
     kMin = alfParam.kMinTab[scanPos];
  }

#if FORCE0
  if (!bChroma)
  {
    if (alfParam.forceCoeff0)
    {
      for (ind = 0; ind < alfParam.filters_per_group; ++ind)
      {
        alfParam.codedVarBins[ind] = m_BinDecoder.decodeBinEP();
      }
    }
  }
#endif
  if( bChroma && isGALF)
  {
    int iNumCoeffMinus1 = alfParam.num_coeff_chroma - 1;
    for (i = 0; i < iNumCoeffMinus1; i++)
    {
      scanPos = pDepthInt[i] - 1;
      alfParam.coeff_chroma[i] = alfGolombDecode(alfParam.kMinTab[scanPos]);
    }
  }
  else
  {
    // Filter coefficients
    for (ind = 0; ind < alfParam.filters_per_group; ++ind)
    {
      if( isGALF )
      {
#if FORCE0
        if (!alfParam.codedVarBins[ind] && alfParam.forceCoeff0)
        {
          continue;
        }
#endif
      }
      int maxIdx = alfParam.num_coeff  - !!isGALF;
      for( int i = 0; i < maxIdx; i++ )
      {
        scanPos = pDepthInt[i] - 1;
        alfParam.coeffmulti[ind][i] = alfGolombDecode(alfParam.kMinTab[scanPos]);
      }
    }
  }
}

void CABACReader::alf_cu_ctrl( ALFParam& alfParam )
{
  alfParam.cu_control_flag = m_BinDecoder.decodeBinEP();
  if( alfParam.cu_control_flag )
  {
    alfParam.alf_max_depth = unary_max_symbol( Ctx::AlfUvlcSCModel( 0 ), Ctx::AlfUvlcSCModel( 1 ), alfParam.maxCodingDepth - 1 );

    uint32_t uiLength = 0;
    uint32_t minValue = alfParam.num_ctus_in_frame;
    uint32_t maxValue = ( minValue << ( alfParam.alf_max_depth * 2) );
    uint32_t temp = maxValue - minValue;
    for( uint32_t i = 0; i < 32; i++ )
    {
      if( temp & 0x1 )
      {
        uiLength = i + 1;
      }
      temp = ( temp >> 1 );
    }
    uint32_t numOfFlags = 0;
    uint32_t uiBit;
    if( uiLength )
    {
      while( uiLength-- )
      {
        uiBit = m_BinDecoder.decodeBinEP();
        numOfFlags += uiBit << uiLength;
      }
    }
    numOfFlags += minValue;
    alfParam.num_alf_cu_flag = numOfFlags;

    DTRACE( g_trace_ctx, D_SYNTAX, "alf_cu_ctrl() max_depth=%d max_alf_depth=%d num_cu_flags=%d\n", alfParam.maxCodingDepth, alfParam.alf_max_depth, alfParam.num_alf_cu_flag );

    for( uint32_t i = 0; i < alfParam.num_alf_cu_flag; i++ )
    {
      alfParam.alf_cu_flag[i] = m_BinDecoder.decodeBin( Ctx::AlfCUCtrlFlags( 0 ) );

      DTRACE( g_trace_ctx, D_SYNTAX, "alf_cu_ctrl() blk=%d alf_cu_flag[blk]=%d\n", i, alfParam.alf_cu_flag[i] );
    }
  }
  else
  {
    DTRACE( g_trace_ctx, D_SYNTAX, "alf_cu_ctrl() off\n" );
  }
}


void CABACReader::alf_chroma( ALFParam& alfParam )
{

  //alfParam.chroma_idc = unary_max_eqprob( 3 );
  alfParam.chroma_idc = parseAlfUvlc();
  if(alfParam.chroma_idc && !alfParam.temporalPredFlag )
  {
    //alfParam.tap_chroma = (unary_max_eqprob( 3 ) <<1 ) + 5;
    alfParam.tap_chroma = (parseAlfUvlc() <<1 ) + 5;
    alfParam.num_coeff_chroma = ((alfParam.tap_chroma*alfParam.tap_chroma+1) >> 1) + 1;
    // filter coefficients for chroma
    for(int pos=0; pos<alfParam.num_coeff_chroma; pos++)
    {
      alfParam.coeff_chroma[pos] = parseAlfSvlc();
    }
  }
}

#endif
#endif


//================================================================================
//  clause 7.3.8.4
//--------------------------------------------------------------------------------
//    bool  coding_tree       ( cs, partitioner, cuCtx )
//    bool  split_cu_flag     ( cs, partitioner )
//    split split_cu_mode_mt  ( cs, partitioner )
//================================================================================

#if JVET_K0230_DUAL_CODING_TREE_UNDER_64x64_BLOCK
bool CABACReader::coding_tree( CodingStructure& cs, Partitioner& partitioner, CUCtx& cuCtx, Partitioner* pPartitionerChroma, CUCtx* pCuCtxChroma)
#else
bool CABACReader::coding_tree( CodingStructure& cs, Partitioner& partitioner, CUCtx& cuCtx )
#endif
{
  const PPS      &pps         = *cs.pps;
  const UnitArea &currArea    = partitioner.currArea();
  bool           lastSegment  = false;

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
    // force QT split enabling on the edges and if the current area exceeds maximum transformation size
    bool qtSplit = implicitSplit == CU_QUAD_SPLIT;

    // split_cu_flag
    if( !qtSplit && implicitSplit != CU_QUAD_SPLIT )
    {
      qtSplit = split_cu_flag( cs, partitioner );
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
        bool lastSegmentC = false;

        while (beContinue)
        {
          if (partitioner.currArea().lwidth() > 64 || partitioner.currArea().lheight() > 64)
          {
            if (!lastSegmentC && cs.area.blocks[partitioner.chType].contains(partitioner.currArea().blocks[partitioner.chType].pos()))
            {
              lastSegmentC = coding_tree(cs, partitioner, cuCtx, pPartitionerChroma, pCuCtxChroma);
            }
            lumaContinue = partitioner.nextPart(cs);
            chromaContinue = pPartitionerChroma->nextPart(cs);
            CHECK(lumaContinue != chromaContinue, "luma chroma partition should be matched");
            beContinue = lumaContinue;
          }
          else
          {
            //dual tree coding under 64x64 block
            if (!lastSegment && cs.area.blocks[partitioner.chType].contains(partitioner.currArea().blocks[partitioner.chType].pos()))
            {
              lastSegment = coding_tree(cs, partitioner, cuCtx);
            }
            lumaContinue = partitioner.nextPart(cs);
            if (!lastSegmentC && cs.area.blocks[pPartitionerChroma->chType].contains(pPartitionerChroma->currArea().blocks[pPartitionerChroma->chType].pos()))
            {
              lastSegmentC = coding_tree(cs, *pPartitionerChroma, *pCuCtxChroma);
            }
            chromaContinue = pPartitionerChroma->nextPart(cs);
            CHECK(lumaContinue != chromaContinue, "luma chroma partition should be matched");
            CHECK(lastSegment == true, "luma should not be the last segment");
            beContinue = lumaContinue;
          }
        }
        partitioner.exitCurrSplit();
        pPartitionerChroma->exitCurrSplit();

        //cat the chroma CUs together
        CodingUnit* currentCu = cs.getCU(partitioner.currArea().lumaPos(), CHANNEL_TYPE_LUMA);
        CodingUnit* nextCu = nullptr;
        CodingUnit* tempLastLumaCu = nullptr;
        CodingUnit* tempLastChromaCu = nullptr;
        ChannelType currentChType = currentCu->chType;
        while (currentCu->next != nullptr)
        {
          nextCu = currentCu->next;
          if (currentChType != nextCu->chType && currentChType == CHANNEL_TYPE_LUMA)
          {
            tempLastLumaCu = currentCu;
            if (tempLastChromaCu != nullptr) //swap
            {
              tempLastChromaCu->next = nextCu;
            }
          }
          else if (currentChType != nextCu->chType && currentChType == CHANNEL_TYPE_CHROMA)
          {
            tempLastChromaCu = currentCu;
            if (tempLastLumaCu != nullptr) //swap
            {
              tempLastLumaCu->next = nextCu;
            }
          }
          currentCu = nextCu;
          currentChType = currentCu->chType;
        }

        CodingUnit* chromaFirstCu = cs.getCU(pPartitionerChroma->currArea().chromaPos(), CHANNEL_TYPE_CHROMA);
        tempLastLumaCu->next = chromaFirstCu;

        lastSegment = lastSegmentC;
      }
      else
      {
#endif
      partitioner.splitCurrArea( CU_QUAD_SPLIT, cs );
      do
      {
        if( !lastSegment && cs.area.blocks[partitioner.chType].contains( partitioner.currArea().blocks[partitioner.chType].pos() ) )
        {
          lastSegment = coding_tree( cs, partitioner, cuCtx );
        }
      } while( partitioner.nextPart( cs ) );

      partitioner.exitCurrSplit();
#if JVET_K0230_DUAL_CODING_TREE_UNDER_64x64_BLOCK
      }
#endif
      return lastSegment;
    }
  }

  {
    // MT
    bool mtSplit = partitioner.canSplit( CU_MT_SPLIT, cs );

    if( mtSplit )
    {
#if JVET_K0554
      const PartSplit splitMode = split_cu_mode_mt( cs, partitioner );
#else
      const PartSplit splitMode = implicitSplit != CU_DONT_SPLIT ? implicitSplit : split_cu_mode_mt( cs, partitioner );
#endif

      if( splitMode != CU_DONT_SPLIT )
      {
        partitioner.splitCurrArea( splitMode, cs );

        do
        {
          if( !lastSegment && cs.area.blocks[partitioner.chType].contains( partitioner.currArea().blocks[partitioner.chType].pos() ) )
          {
            lastSegment = coding_tree(cs, partitioner, cuCtx);
          }
        } while( partitioner.nextPart( cs ) );

        partitioner.exitCurrSplit();
        return lastSegment;
      }
    }
  }


  CodingUnit& cu = cs.addCU( CS::getArea( cs, currArea, partitioner.chType ), partitioner.chType );

  partitioner.setCUData( cu );
  cu.slice   = cs.slice;
#if HEVC_TILES_WPP
  cu.tileIdx = cs.picture->tileMap->getTileIdxMap( currArea.lumaPos() );
#endif

  // Predict QP on start of quantization group
  if( pps.getUseDQP() && !cuCtx.isDQPCoded && CU::isQGStart( cu ) )
  {
    cuCtx.qp = CU::predictQP( cu, cuCtx.qp );
  }

  cu.qp          = cuCtx.qp;        //NOTE: CU QP can be changed by deltaQP signaling at TU level
  cu.chromaQpAdj = cs.chromaQpAdj;  //NOTE: CU chroma QP adjustment can be changed by adjustment signaling at TU level

  // coding unit

  bool isLastCtu = coding_unit( cu, partitioner, cuCtx );

  DTRACE( g_trace_ctx, D_QP, "x=%d, y=%d, w=%d, h=%d, qp=%d\n", cu.Y().x, cu.Y().y, cu.Y().width, cu.Y().height, cu.qp );
  return isLastCtu;
}

PartSplit CABACReader::split_cu_mode_mt( CodingStructure& cs, Partitioner &partitioner )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__SPLIT_FLAG );

  PartSplit mode      = CU_DONT_SPLIT;

  unsigned ctxIdBT    = DeriveCtx::CtxBTsplit( cs, partitioner );

  unsigned width      = partitioner.currArea().lumaSize().width;
  unsigned height     = partitioner.currArea().lumaSize().height;

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
  dt.setCtxId( DTT_SPLIT_DO_SPLIT_DECISION,   Ctx::BTSplitFlag( ctxIdBT ) );      // 0- 2
  dt.setCtxId( DTT_SPLIT_HV_DECISION,         Ctx::BTSplitFlag( 3 + btSCtxId ) ); // 3- 5

  dt.setCtxId( DTT_SPLIT_H_IS_BT_12_DECISION, Ctx::BTSplitFlag( 6 + btSCtxId ) ); // 6- 8
  dt.setCtxId( DTT_SPLIT_V_IS_BT_12_DECISION, Ctx::BTSplitFlag( 9 + btSCtxId ) ); // 9-11

  unsigned id = decode_sparse_dt( dt );

  mode = id == DTT_SPLIT_NO_SPLIT ? CU_DONT_SPLIT : PartSplit( id );

  DTRACE( g_trace_ctx, D_SYNTAX, "split_cu_mode_mt() ctx=%d split=%d\n", ctxIdBT, mode );

  return mode;

}

bool CABACReader::split_cu_flag( CodingStructure& cs, Partitioner &partitioner )
{
  // TODO: make maxQTDepth a slice parameter
  unsigned maxQTDepth = ( cs.sps->getSpsNext().getUseQTBT()
    ? g_aucLog2[cs.sps->getSpsNext().getCTUSize()] - g_aucLog2[cs.sps->getSpsNext().getMinQTSize( cs.slice->getSliceType(), partitioner.chType )]
    : cs.sps->getLog2DiffMaxMinCodingBlockSize() );
  if( partitioner.currDepth == maxQTDepth )
  {
    return false;
  }

  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE( STATS__CABAC_BITS__SPLIT_FLAG, partitioner.currArea().lumaSize() );

  unsigned  ctxId = DeriveCtx::CtxCUsplit( cs, partitioner );
  bool      split = ( m_BinDecoder.decodeBin( Ctx::SplitFlag( ctxId ) ) );
  DTRACE( g_trace_ctx, D_SYNTAX, "split_cu_flag() ctx=%d split=%d\n", ctxId, split ? 1 : 0 );
  return split;
}


//================================================================================
//  clause 7.3.8.5
//--------------------------------------------------------------------------------
//    bool  coding_unit               ( cu, partitioner, cuCtx )
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
//    bool  end_of_ctu                ( cu, cuCtx )
//================================================================================

bool CABACReader::coding_unit( CodingUnit &cu, Partitioner &partitioner, CUCtx& cuCtx )
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
    cs.addTU         ( cu, partitioner.chType );
    PredictionUnit&    pu = cs.addPU( cu, partitioner.chType );
    MergeCtx           mrgCtx;
    prediction_unit  ( pu, mrgCtx );
#if JEM_TOOLS
    cu.obmcFlag      = cu.cs->sps->getSpsNext().getUseOBMC();
    cu_lic_flag      ( cu );
#endif
    return end_of_ctu( cu, cuCtx );
  }

  // prediction mode and partitioning data
  pred_mode ( cu );
#if JEM_TOOLS
  pdpc_flag ( cu );
#endif
  cu.partSize = SIZE_2Nx2N;

  // --> create PUs
  CU::addPUs( cu );

  // pcm samples
  if( CU::isIntra(cu) && cu.partSize == SIZE_2Nx2N )
  {
    pcm_flag( cu );
    if( cu.ipcm )
    {
      TransformUnit& tu = cs.addTU( cu, partitioner.chType );
      pcm_samples( tu );
      return end_of_ctu( cu, cuCtx );
    }
  }

  // prediction data ( intra prediction modes / reference indexes + motion vectors )
  cu_pred_data( cu );

  // residual data ( coded block flags + transform coefficient levels )
  cu_residual( cu, partitioner, cuCtx );

  // check end of cu
  return end_of_ctu( cu, cuCtx );
}


void CABACReader::cu_transquant_bypass_flag( CodingUnit& cu )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__TQ_BYPASS_FLAG );

  cu.transQuantBypass = ( m_BinDecoder.decodeBin( Ctx::TransquantBypassFlag() ) );
}


void CABACReader::cu_skip_flag( CodingUnit& cu )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__SKIP_FLAG );

  unsigned ctxId  = DeriveCtx::CtxSkipFlag(cu);
  unsigned skip   = m_BinDecoder.decodeBin( Ctx::SkipFlag(ctxId) );

  DTRACE( g_trace_ctx, D_SYNTAX, "cu_skip_flag() ctx=%d skip=%d\n", ctxId, skip ? 1 : 0 );

  if( skip )
  {
    cu.skip     = true;
    cu.rootCbf  = false;
    cu.predMode = MODE_INTER;
    cu.partSize = SIZE_2Nx2N;
  }
}

#if JVET_K0357_AMVR
void CABACReader::imv_mode( CodingUnit& cu, MergeCtx& mrgCtx )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__OTHER );

  if( !cu.cs->sps->getSpsNext().getUseIMV() )
  {
    return;
  }

  bool bNonZeroMvd = CU::hasSubCUNonZeroMVd( cu );
  if( !bNonZeroMvd )
  {
    return;
  }

  const SPSNext& spsNext = cu.cs->sps->getSpsNext();

  unsigned value = 0;
  unsigned ctxId = DeriveCtx::CtxIMVFlag( cu );
#if JVET_K0076_CPR
  if (cu.firstPU->interDir == 1 && cu.cs->slice->getRefPic(REF_PIC_LIST_0, cu.firstPU->refIdx[REF_PIC_LIST_0])->getPOC() == cu.cs->slice->getPOC()) // the first bin of IMV flag does need to be signaled in CPR block
    value = 1;
  else
#endif
    value = m_BinDecoder.decodeBin( Ctx::ImvFlag( ctxId ) );
  DTRACE( g_trace_ctx, D_SYNTAX, "imv_mode() value=%d ctx=%d\n", value, ctxId );

  if( spsNext.getImvMode() == IMV_4PEL && value )
  {
    value = m_BinDecoder.decodeBin( Ctx::ImvFlag( 3 ) );
    DTRACE( g_trace_ctx, D_SYNTAX, "imv_mode() value=%d ctx=%d\n", value, 3 );
    value++;
  }

  cu.imv = value;
  DTRACE( g_trace_ctx, D_SYNTAX, "imv_mode() IMVFlag=%d\n", cu.imv );
}
#endif

void CABACReader::pred_mode( CodingUnit& cu )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__PRED_MODE );

  if( cu.cs->slice->isIntra() || m_BinDecoder.decodeBin( Ctx::PredMode() ) )
  {
    cu.predMode = MODE_INTRA;
  }
  else
  {
    cu.predMode = MODE_INTER;
  }
}

#if JEM_TOOLS

void CABACReader::pdpc_flag( CodingUnit& cu )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__INTRA_PDPC_FLAG );

  if (!cu.cs->sps->getSpsNext().isIntraPDPC() || cu.predMode == MODE_INTER)
  {
    cu.pdpc = false;
    return;
  }

  cu.pdpc = ( m_BinDecoder.decodeBin( Ctx::PdpcFlag() ) );
}
#endif

void CABACReader::pcm_flag( CodingUnit& cu )
{
  const SPS& sps = *cu.cs->sps;
  if( !sps.getUsePCM() || cu.lumaSize().width > (1 << sps.getPCMLog2MaxSize()) || cu.lumaSize().width < (1 << sps.getPCMLog2MinSize()) )
  {
    cu.ipcm = false;
    return;
  }
  cu.ipcm = ( m_BinDecoder.decodeBinTrm() );
}


void CABACReader::cu_pred_data( CodingUnit &cu )
{
  if( CU::isIntra( cu ) )
  {
    intra_luma_pred_modes( cu );
    intra_chroma_pred_modes( cu );
    return;
  }
#if JVET_K0076_CPR_DT
  if (!cu.Y().valid()) // dual tree chroma CU
  {
    cu.predMode = MODE_INTER;
    cu.ibc = true;
    return;
  }
#endif 
  MergeCtx mrgCtx;

  for( auto &pu : CU::traversePUs( cu ) )
  {
    prediction_unit( pu, mrgCtx );
  }

#if JVET_K0357_AMVR
  imv_mode   ( cu, mrgCtx );
#endif
#if JEM_TOOLS
  obmc_flag  ( cu );
  cu_lic_flag( cu ); // local illumination compensation

  for( auto &pu : CU::traversePUs( cu ) )
  {
    PU::spanLICFlags( pu, cu.LICFlag );
  }
#endif

#if JVET_K0248_GBI
  cu_gbi_flag( cu );
#endif
}


#if JEM_TOOLS
void CABACReader::cu_lic_flag( CodingUnit& cu )
{
  if( CU::isLICFlagPresent( cu ) )
  {
    RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__LIC_FLAG );

    cu.LICFlag = ( m_BinDecoder.decodeBin( Ctx::LICFlag() ) );
    DTRACE( g_trace_ctx, D_SYNTAX, "cu_lic_flag() lic_flag=%d\n", cu.LICFlag?1:0 );
  }
}

void CABACReader::obmc_flag( CodingUnit& cu )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__OTHER );

  cu.obmcFlag = cu.cs->sps->getSpsNext().getUseOBMC();
  if( !cu.obmcFlag )
  {
    return;
  }

  bool bCoded = CU::isObmcFlagCoded( cu );

  if( bCoded )
  {
    unsigned obmc = m_BinDecoder.decodeBin( Ctx::ObmcFlag() );
    cu.obmcFlag = ( bool ) obmc;
  }

  DTRACE( g_trace_ctx, D_SYNTAX, "obmc_flag() obmc=%d pos=(%d,%d)\n", cu.obmcFlag ? 1 : 0, cu.lumaPos().x, cu.lumaPos().y );
}
#endif

#if JVET_K0248_GBI
void CABACReader::cu_gbi_flag( CodingUnit& cu )
{
  if( !CU::isGBiIdxCoded(cu) )
  {
    return;
  }

  uint8_t gbiIdx = GBI_DEFAULT;
  
  CHECK( !( GBI_NUM > 1 && (GBI_NUM == 2 || (GBI_NUM & 0x01 ) == 1 ) ), " !( GBI_NUM > 1 && ( GBI_NUM == 2 || ( GBI_NUM & 0x01 ) == 1 ) ) " );

  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__GBI_IDX );
  
  int ctxId = 0;

  uint32_t idx = 0;
  uint32_t symbol;
  
  symbol = ( m_BinDecoder.decodeBin( Ctx::GBiIdx( ctxId ) ) );

  int32_t numGBi = ( cu.slice->getCheckLDC() ) ? 5 : 3;

  if( symbol == 0 )
  {
    uint32_t prefixNumBits = numGBi - 2;
    uint32_t step = 1;

    unsigned ctxIdGBi = 4;
    idx = 1;

    for( int ui = 0; ui < prefixNumBits; ++ui )
    {
      symbol = ( m_BinDecoder.decodeBin( Ctx::GBiIdx( ctxIdGBi ) ) );

      if( symbol == 1 )
      {
        break;
      }
      ctxIdGBi += step;
      idx += step;
    }
  }
  
  gbiIdx = ( uint8_t )g_GbiParsingOrder[idx];
  CU::setGbiIdx( cu, gbiIdx);

  DTRACE( g_trace_ctx, D_SYNTAX, "cu_gbi_flag() gbi_idx=%d\n", cu.GBiIdx ? 1 : 0 );
}
#endif

void CABACReader::intra_luma_pred_modes( CodingUnit &cu )
{
  if( !cu.Y().valid() )
  {
    return;
  }

  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE2( STATS__CABAC_BITS__INTRA_DIR_ANG, cu.lumaSize(), CHANNEL_TYPE_LUMA );

  const uint32_t numMPMs = cu.cs->pcv->numMPMs;

  // prev_intra_luma_pred_flag
  int numBlocks = CU::getNumPUs( cu );
  int mpmFlag[4];
  for( int k = 0; k < numBlocks; k++ )
  {
    mpmFlag[k] = m_BinDecoder.decodeBin( Ctx::IPredMode[0]() );
  }

  PredictionUnit *pu = cu.firstPU;
#if !INTRA67_3MPM
#if JEM_TOOLS
  const bool use65Ang = cu.cs->sps->getSpsNext().getUseIntra65Ang();
#endif
#endif

  // mpm_idx / rem_intra_luma_pred_mode
  for( int k = 0; k < numBlocks; k++ )
  {
    unsigned *mpm_pred = ( unsigned* ) alloca( numMPMs * sizeof( unsigned ) );
    PU::getIntraMPMs( *pu, mpm_pred );

    if( mpmFlag[k] )
    {
      unsigned ipred_idx = 0;
#if !INTRA67_3MPM
#if JEM_TOOLS
      if( use65Ang )
      {
        DecisionTree dt( g_intraLumaMpmDTT );

        dt.setCtxId( DTT_INTRA_MPM_ISGT_0, Ctx::IPredMode[0]( mpmCtx[mpm_pred[0]] ) );
        dt.setCtxId( DTT_INTRA_MPM_ISGT_1, Ctx::IPredMode[0]( mpmCtx[mpm_pred[1]] ) );
        dt.setCtxId( DTT_INTRA_MPM_ISGT_2, Ctx::IPredMode[0]( mpmCtx[mpm_pred[2]] ) );

        ipred_idx = decode_sparse_dt( dt );
      }
      else
#endif
#endif
      {
        ipred_idx = m_BinDecoder.decodeBinEP();
        if( ipred_idx )
        {
          ipred_idx += m_BinDecoder.decodeBinEP();
        }
      }
      pu->intraDir[0] = mpm_pred[ipred_idx];
    }
    else
    {
      unsigned ipred_mode = 0;

#if !INTRA67_3MPM
#if JEM_TOOLS
      if( use65Ang )
      {
#if JVET_B0051_NON_MPM_MODE
        unsigned selected_flag = m_BinDecoder.decodeBin(Ctx::IPredMode[0](4));
        if (selected_flag)
        {
          ipred_mode = m_BinDecoder.decodeBinsEP(4);
          ipred_mode <<= 2;
        }
        else
        {
          xReadTruncBinCode(ipred_mode, 45);
          ipred_mode += g_ipred_mode_table[ipred_mode];
        }
#else
        ipred_mode    = m_BinDecoder.decodeBinsEP( 4 );
        ipred_mode  <<= 2;
        int RealNumIntraMode = cu.cs->sps->getSpsNext().getRealNumIntraMode();
        if( ipred_mode < ( RealNumIntraMode - 8 ) )
        {
          ipred_mode += m_BinDecoder.decodeBinsEP( 2 );
        }
#endif
      }
      else
#endif
#endif
      {
#if INTRA67_3MPM
        ipred_mode = m_BinDecoder.decodeBinsEP(6);
#else
        ipred_mode = m_BinDecoder.decodeBinsEP( 5 );
#endif
      }
      //postponed sorting of MPMs (only in remaining branch)
      std::sort( mpm_pred, mpm_pred + cu.cs->pcv->numMPMs );

      for( unsigned i = 0; i < cu.cs->pcv->numMPMs; i++ )
      {
#if !INTRA67_3MPM
#if JEM_TOOLS
        ipred_mode += use65Ang ? ( ipred_mode >= mpm_pred[i] ) : ( ipred_mode >= g_intraMode65to33AngMapping[mpm_pred[i]] );
#else
        ipred_mode += ipred_mode >= g_intraMode65to33AngMapping[mpm_pred[i]];
#endif
#else
        ipred_mode += (ipred_mode >= mpm_pred[i]);
#endif
      }

#if !INTRA67_3MPM
#if JEM_TOOLS
      pu->intraDir[0] = use65Ang ? ipred_mode : g_intraMode33to65AngMapping[ipred_mode];
#else
      pu->intraDir[0] = g_intraMode33to65AngMapping[ipred_mode];
#endif
#else
      pu->intraDir[0] = ipred_mode;
#endif
    }

    DTRACE( g_trace_ctx, D_SYNTAX, "intra_luma_pred_modes() idx=%d pos=(%d,%d) mode=%d\n", k, pu->lumaPos().x, pu->lumaPos().y, pu->intraDir[0] );
    pu = pu->next;
  }
}

void CABACReader::intra_chroma_pred_modes( CodingUnit& cu )
{
  if( cu.chromaFormat == CHROMA_400 || ( CS::isDualITree( *cu.cs ) && cu.chType == CHANNEL_TYPE_LUMA ) )
  {
    return;
  }

  PredictionUnit *pu = cu.firstPU;

  {
    CHECK( pu->cu != &cu, "Inkonsistent PU-CU mapping" );
    intra_chroma_pred_mode( *pu );
  }
}
#if JEM_TOOLS||JVET_K0190
bool CABACReader::intra_chroma_lmc_mode( PredictionUnit& pu )
{
#if JEM_TOOLS&&!JVET_K0190
  if ( pu.cs->sps->getSpsNext().getUseMDMS() )
  {
    if ( m_BinDecoder.decodeBin( Ctx::IPredMode[1]( 0 ) ) == 0 )
    {
      unsigned ctxId = 6;
      if ( PU::isMMLMEnabled( pu ) )
      {
        if ( m_BinDecoder.decodeBin( Ctx::IPredMode[1]( ctxId++ ) ) == 1 )
        {
          pu.intraDir[1] = MMLM_CHROMA_IDX;
          return true;
        }
      }
      if ( PU::isMFLMEnabled( pu ) )
      {
        if ( m_BinDecoder.decodeBin( Ctx::IPredMode[1]( ctxId++ ) ) == 1 )
        {
          pu.intraDir[1] = LM_CHROMA_IDX;
          return true;
        }
        int candId = m_BinDecoder.decodeBin( Ctx::IPredMode[1]( ctxId++ ) ) << 1;
        candId    += m_BinDecoder.decodeBin( Ctx::IPredMode[1]( ctxId++ ) );
        pu.intraDir[1] = LM_CHROMA_F1_IDX + candId;
        return true;
      }
      pu.intraDir[1] = LM_CHROMA_IDX;
      return true;
    }
  }
  else
  {
#endif
    int lmModeList[10];
    int maxSymbol = PU::getLMSymbolList(pu, lmModeList);
    int symbol    = unary_max_symbol( Ctx::IPredMode[1]( 2 ), Ctx::IPredMode[1]( 3 ), maxSymbol - 1 );
    if ( lmModeList[ symbol ] != -1 )
    {
      pu.intraDir[1] = lmModeList[ symbol ];
      return true;
    }
#if JEM_TOOLS&&!JVET_K0190
  }
#endif
  return false;
}
#endif

void CABACReader::intra_chroma_pred_mode( PredictionUnit& pu )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE2( STATS__CABAC_BITS__INTRA_DIR_ANG, pu.cu->blocks[pu.chType].lumaSize(), CHANNEL_TYPE_CHROMA );

#if JEM_TOOLS
  // DM chroma index
  if( pu.cs->sps->getSpsNext().getUseMDMS() )
  {
  }
  else
#endif
  {
    if( m_BinDecoder.decodeBin( Ctx::IPredMode[1]( 1 ) ) == 0 )
    {
      pu.intraDir[1] = DM_CHROMA_IDX;
      return;
    }
  }

#if JEM_TOOLS||JVET_K0190
  // LM chroma mode
  if( pu.cs->sps->getSpsNext().getUseLMChroma() )
  {
    if( intra_chroma_lmc_mode( pu ) )
    {
      return;
    }
  }
#endif
#if JEM_TOOLS
  // chroma candidate index
  unsigned candId = 0;
  if( pu.cs->sps->getSpsNext().getUseMDMS() )
  {
#if HM_MDMS_CODELAST_AS_IN_JEM
    const unsigned lastId = NUM_DM_MODES; // cannot be reached -> always read last bin
#else
    const unsigned lastId = NUM_DM_MODES - 1;
#endif

    unsigned ctxId = 1;
    while( candId < lastId && m_BinDecoder.decodeBin( Ctx::IPredMode[1]( ctxId++ ) ) )
    {
      candId += 1;
    }
    candId += NUM_LMC_MODE;
  }
  else
  {
    candId = m_BinDecoder.decodeBinsEP( 2 );
  }
#else
  unsigned candId = m_BinDecoder.decodeBinsEP( 2 );
#endif

  unsigned chromaCandModes[ NUM_CHROMA_MODE ];
  PU::getIntraChromaCandModes( pu, chromaCandModes );

  CHECK( candId >= NUM_CHROMA_MODE, "Chroma prediction mode index out of bounds" );
#if JEM_TOOLS||JVET_K0190
  CHECK( PU::isLMCMode( chromaCandModes[ candId ] ), "The intra dir cannot be LM_CHROMA for this path" );
#endif
  CHECK( chromaCandModes[ candId ] == DM_CHROMA_IDX, "The intra dir cannot be DM_CHROMA for this path" );

  pu.intraDir[1] = chromaCandModes[ candId ];
}

void CABACReader::cu_residual( CodingUnit& cu, Partitioner &partitioner, CUCtx& cuCtx )
{
  if( CU::isInter( cu ) )
  {
    PredictionUnit& pu = *cu.firstPU;
    if( !( ( cu.cs->pcv->noRQT || cu.partSize == SIZE_2Nx2N ) && pu.mergeFlag ) )
    {
      rqt_root_cbf( cu );
    }
    else
    {
      cu.rootCbf = true;
    }
    if( !cu.rootCbf )
    {
      TransformUnit& tu = cu.cs->addTU(cu, partitioner.chType);
#if ENABLE_BMS
      tu.depth = 0;
#endif
      for( unsigned c = 0; c < tu.blocks.size(); c++ )
      {
        tu.cbf[c]             = 0;
        ComponentID   compID  = ComponentID(c);
        tu.getCoeffs( compID ).fill( 0 );
        tu.getPcmbuf( compID ).fill( 0 );
      }
      return;
    }
  }

  ChromaCbfs chromaCbfs;
  transform_tree( *cu.cs, partitioner, cuCtx, chromaCbfs );

#if JEM_TOOLS
  residual_nsst_mode( cu );
#endif

#if (JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT) && !HM_EMT_NSST_AS_IN_JEM
  cu_emt_pertu_idx( cu );
#endif
}

#if (JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT) && !HM_EMT_NSST_AS_IN_JEM
void CABACReader::cu_emt_pertu_idx( CodingUnit& cu )
{
  bool anyCbf = false, anyNonTs = false;

  for( const auto &tu : CU::traverseTUs( cu ) )
  {
    anyCbf   |= tu.cbf[0] != 0;
    anyNonTs |= !tu.transformSkip[0];
  }

  if( !isLuma( cu.chType ) || cu.nsstIdx != 0 || !( cu.cs->sps->getSpsNext().getUseIntraEMT() || cu.cs->sps->getSpsNext().getUseInterEMT() ) || !anyCbf || !anyNonTs )
  {
    return;
  }

  emt_cu_flag( cu );

  if( cu.emtFlag )
  {
    for( auto &tu : CU::traverseTUs( cu ) )
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
        emt_tu_index( tu );
      }
    }
  }
}

#endif
void CABACReader::rqt_root_cbf( CodingUnit& cu )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__QT_ROOT_CBF );

  cu.rootCbf = ( m_BinDecoder.decodeBin( Ctx::QtRootCbf() ) );

  DTRACE( g_trace_ctx, D_SYNTAX, "rqt_root_cbf() ctx=0 root_cbf=%d pos=(%d,%d)\n", cu.rootCbf ? 1 : 0, cu.lumaPos().x, cu.lumaPos().y );
}


bool CABACReader::end_of_ctu( CodingUnit& cu, CUCtx& cuCtx )
{
  const SPS     &sps   = *cu.cs->sps;
  const Position rbPos = recalcPosition( cu.chromaFormat, cu.chType, CHANNEL_TYPE_LUMA, cu.blocks[cu.chType].bottomRight().offset( 1, 1 ) );

  if ( ( ( rbPos.x & cu.cs->pcv->maxCUWidthMask  ) == 0 || rbPos.x == sps.getPicWidthInLumaSamples () )
    && ( ( rbPos.y & cu.cs->pcv->maxCUHeightMask ) == 0 || rbPos.y == sps.getPicHeightInLumaSamples() )
    && ( !CS::isDualITree( *cu.cs ) || cu.chromaFormat == CHROMA_400 || isChroma( cu.chType ) )
      )
  {
    cuCtx.isDQPCoded = ( cu.cs->pps->getUseDQP() && !cuCtx.isDQPCoded );

    return terminating_bit();
  }

  return false;
}



//================================================================================
//  clause 7.3.8.6
//--------------------------------------------------------------------------------
//    void  prediction_unit ( pu, mrgCtx );
//    void  merge_flag      ( pu );
//    void  merge_data      ( pu, mrgCtx );
//    void  merge_idx       ( pu );
//    void  inter_pred_idc  ( pu );
//    void  ref_idx         ( pu, refList );
//    void  mvp_flag        ( pu, refList );
//================================================================================

void CABACReader::prediction_unit( PredictionUnit& pu, MergeCtx& mrgCtx )
{
  if( pu.cu->skip )
  {
    pu.mergeFlag = true;
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
    merge_data   ( pu );
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
#if JEM_TOOLS || JVET_K_AFFINE
      if( pu.cu->affine )
      {
        mvd_coding( pu.mvdAffi[REF_PIC_LIST_0][0] );
        mvd_coding( pu.mvdAffi[REF_PIC_LIST_0][1] );
#if JVET_K0337_AFFINE_6PARA
        if ( pu.cu->affineType == AFFINEMODEL_6PARAM )
        {
          mvd_coding( pu.mvdAffi[REF_PIC_LIST_0][2] );
        }
#endif
      }
      else
#endif
      {
        mvd_coding( pu.mvd[REF_PIC_LIST_0] );
      }
      mvp_flag    ( pu, REF_PIC_LIST_0 );
    }

    if( pu.interDir != 1 /* PRED_L0 */ )
    {
      ref_idx     ( pu, REF_PIC_LIST_1 );
      if( pu.cu->cs->slice->getMvdL1ZeroFlag() && pu.interDir == 3 /* PRED_BI */ )
      {
        pu.mvd[ REF_PIC_LIST_1 ] = Mv();
#if JEM_TOOLS || JVET_K_AFFINE
        pu.mvdAffi[REF_PIC_LIST_1][0] = Mv();
        pu.mvdAffi[REF_PIC_LIST_1][1] = Mv();
        pu.mvdAffi[REF_PIC_LIST_1][2] = Mv();
#endif
      }
#if JEM_TOOLS || JVET_K_AFFINE
      else if( pu.cu->affine )
      {
        mvd_coding( pu.mvdAffi[REF_PIC_LIST_1][0] );
        mvd_coding( pu.mvdAffi[REF_PIC_LIST_1][1] );
#if JVET_K0337_AFFINE_6PARA
        if ( pu.cu->affineType == AFFINEMODEL_6PARAM )
        {
          mvd_coding( pu.mvdAffi[REF_PIC_LIST_1][2] );
        }
#endif
      }
#endif
      else
      {
        mvd_coding( pu.mvd[REF_PIC_LIST_1] );
      }
      mvp_flag    ( pu, REF_PIC_LIST_1 );
    }
  }
  if( pu.interDir == 3 /* PRED_BI */ && PU::isBipredRestriction(pu) )
  {
    pu.mv    [REF_PIC_LIST_1] = Mv(0, 0);
    pu.refIdx[REF_PIC_LIST_1] = -1;
    pu.interDir               =  1;
#if JVET_K0248_GBI
    pu.cu->GBiIdx = GBI_DEFAULT;
#endif
  }

  PU::spanMotionInfo( pu, mrgCtx );
}

#if JEM_TOOLS || JVET_K_AFFINE
void CABACReader::affine_flag( CodingUnit& cu )
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

  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__AFFINE_FLAG );

  unsigned ctxId = DeriveCtx::CtxAffineFlag( cu );
  cu.affine = m_BinDecoder.decodeBin( Ctx::AffineFlag( ctxId ) );

  DTRACE( g_trace_ctx, D_SYNTAX, "affine_flag() affine=%d ctx=%d pos=(%d,%d)\n", cu.affine ? 1 : 0, ctxId, cu.Y().x, cu.Y().y );

#if JVET_K0337_AFFINE_6PARA
  if ( cu.affine && !cu.firstPU->mergeFlag && cu.cs->sps->getSpsNext().getUseAffineType() )
  {
    ctxId = 0;
    cu.affineType = m_BinDecoder.decodeBin( Ctx::AffineType( ctxId ) );
    DTRACE( g_trace_ctx, D_SYNTAX, "affine_type() affine_type=%d ctx=%d pos=(%d,%d)\n", cu.affineType ? 1 : 0, ctxId, cu.Y().x, cu.Y().y );
  }
  else
  {
    cu.affineType = AFFINEMODEL_4PARAM;
  }
#endif
}
#endif

void CABACReader::merge_flag( PredictionUnit& pu )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__MERGE_FLAG );

  pu.mergeFlag = ( m_BinDecoder.decodeBin( Ctx::MergeFlag() ) );

  DTRACE( g_trace_ctx, D_SYNTAX, "merge_flag() merge=%d pos=(%d,%d) size=%dx%d\n", pu.mergeFlag ? 1 : 0, pu.lumaPos().x, pu.lumaPos().y, pu.lumaSize().width, pu.lumaSize().height );
}


void CABACReader::merge_data( PredictionUnit& pu )
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

  merge_idx( pu );
}


void CABACReader::merge_idx( PredictionUnit& pu )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__MERGE_INDEX );

  int numCandminus1 = int( pu.cs->slice->getMaxNumMergeCand() ) - 1;
  pu.mergeIdx       = 0;
  if( numCandminus1 > 0 )
  {
    if( m_BinDecoder.decodeBin( Ctx::MergeIdx() ) )
    {
#if JEM_TOOLS || JVET_K0346
      bool useExtCtx = pu.cs->sps->getSpsNext().getUseSubPuMvp();
#endif
      pu.mergeIdx++;
      for( ; pu.mergeIdx < numCandminus1; pu.mergeIdx++ )
      {
#if JEM_TOOLS || JVET_K0346
        if( useExtCtx )
        {
          if( !m_BinDecoder.decodeBin( Ctx::MergeIdx( std::min<int>( pu.mergeIdx, NUM_MERGE_IDX_EXT_CTX - 1 ) ) ) )
          {
            break;
          }
        }
        else
#endif
        {
          if( !m_BinDecoder.decodeBinEP() )
          {
            break;
          }
        }
      }
    }
  }
  DTRACE( g_trace_ctx, D_SYNTAX, "merge_idx() merge_idx=%d\n", pu.mergeIdx );
}


void CABACReader::inter_pred_idc( PredictionUnit& pu )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__INTER_DIR );

  if( pu.cs->slice->isInterP() )
  {
    pu.interDir = 1;
    return;
  }
#if JEM_TOOLS || JVET_K0346
  if( pu.cu->partSize == SIZE_2Nx2N || pu.cs->sps->getSpsNext().getUseSubPuMvp() || pu.cu->lumaSize().width != 8 )
#else
  if( pu.cu->partSize == SIZE_2Nx2N || pu.cu->lumaSize().width != 8 )
#endif
  {
    unsigned ctxId = DeriveCtx::CtxInterDir(pu);
    if( m_BinDecoder.decodeBin( Ctx::InterDir(ctxId) ) )
    {
      DTRACE( g_trace_ctx, D_SYNTAX, "inter_pred_idc() ctx=%d value=%d pos=(%d,%d)\n", ctxId, 3, pu.lumaPos().x, pu.lumaPos().y );
      pu.interDir = 3;
      return;
    }
  }
  if( m_BinDecoder.decodeBin( Ctx::InterDir(4) ) )
  {
    DTRACE( g_trace_ctx, D_SYNTAX, "inter_pred_idc() ctx=4 value=%d pos=(%d,%d)\n", 2, pu.lumaPos().x, pu.lumaPos().y );
    pu.interDir = 2;
    return;
  }
  DTRACE( g_trace_ctx, D_SYNTAX, "inter_pred_idc() ctx=4 value=%d pos=(%d,%d)\n", 1, pu.lumaPos().x, pu.lumaPos().y );
  pu.interDir = 1;
  return;
}


void CABACReader::ref_idx( PredictionUnit &pu, RefPicList eRefList )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__REF_FRM_IDX );

  int numRef  = pu.cs->slice->getNumRefIdx(eRefList);
  if( numRef <= 1 || !m_BinDecoder.decodeBin( Ctx::RefPic() ) )
  {
    if( numRef > 1 )
    {
      DTRACE( g_trace_ctx, D_SYNTAX, "ref_idx() value=%d pos=(%d,%d)\n", 0, pu.lumaPos().x, pu.lumaPos().y );
    }
    pu.refIdx[eRefList] = 0;
    return;
  }
  if( numRef <= 2 || !m_BinDecoder.decodeBin( Ctx::RefPic(1) ) )
  {
    DTRACE( g_trace_ctx, D_SYNTAX, "ref_idx() value=%d pos=(%d,%d)\n", 1, pu.lumaPos().x, pu.lumaPos().y );
    pu.refIdx[eRefList] = 1;
    return;
  }
  for( int idx = 3; ; idx++ )
  {
    if( numRef <= idx || !m_BinDecoder.decodeBinEP() )
    {
      pu.refIdx[eRefList] = (signed char)( idx - 1 );
      DTRACE( g_trace_ctx, D_SYNTAX, "ref_idx() value=%d pos=(%d,%d)\n", idx-1, pu.lumaPos().x, pu.lumaPos().y );
      return;
    }
  }
}



void CABACReader::mvp_flag( PredictionUnit& pu, RefPicList eRefList )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__MVP_IDX );

  unsigned mvp_idx = m_BinDecoder.decodeBin( Ctx::MVPIdx() );
  DTRACE( g_trace_ctx, D_SYNTAX, "mvp_flag() value=%d pos=(%d,%d)\n", mvp_idx, pu.lumaPos().x, pu.lumaPos().y );
  pu.mvpIdx [eRefList] = mvp_idx;
  DTRACE( g_trace_ctx, D_SYNTAX, "mvpIdx(refList:%d)=%d\n", eRefList, mvp_idx );
}

#if JEM_TOOLS
void CABACReader::fruc_mrg_mode( PredictionUnit& pu )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__OTHER );

  if( !pu.cs->slice->getSPS()->getSpsNext().getUseFRUCMrgMode() )
    return;

  unsigned fruc_mode  = FRUC_MERGE_OFF;
  unsigned flag_idx   = DeriveCtx::CtxFrucFlag( pu );

  if( m_BinDecoder.decodeBin( Ctx::FrucFlag(flag_idx) ) )
  {
    if( pu.cs->slice->isInterP() )
    {
      fruc_mode = FRUC_MERGE_TEMPLATE;
    }
    else
    {
      unsigned mode_idx   = DeriveCtx::CtxFrucMode( pu );
      unsigned second_bin = m_BinDecoder.decodeBin( Ctx::FrucMode(mode_idx) );
      fruc_mode = second_bin ? FRUC_MERGE_BILATERALMV : FRUC_MERGE_TEMPLATE;
    }
  }

  pu.frucMrgMode = fruc_mode;

  DTRACE( g_trace_ctx, D_SYNTAX, "fruc_mrg_mode() fruc_mode=%d pos=(%d,%d) size: %dx%d\n", fruc_mode, pu.Y().x, pu.Y().y, pu.lumaSize().width, pu.lumaSize().height );
}
#endif

//================================================================================
//  clause 7.3.8.7
//--------------------------------------------------------------------------------
//    void  pcm_samples( tu )
//================================================================================

void CABACReader::pcm_samples( TransformUnit& tu )
{
  CHECK( !tu.cu->ipcm, "pcm mode expected" );

  const SPS&        sps       = *tu.cu->cs->sps;
  const ComponentID maxCompId = ( tu.chromaFormat == CHROMA_400 ? COMPONENT_Y : COMPONENT_Cr );
#if ENABLE_BMS
  tu.depth                    = 0;
#endif
  for( ComponentID compID = COMPONENT_Y; compID <= maxCompId; compID = ComponentID(compID+1) )
  {
    PelBuf          samples     = tu.getPcmbuf( compID );
    const unsigned  sampleBits  = sps.getPCMBitDepth( toChannelType(compID) );
    for( unsigned y = 0; y < samples.height; y++ )
    {
      for( unsigned x = 0; x < samples.width; x++ )
      {
        samples.at(x, y) = m_BinDecoder.decodeBinsPCM( sampleBits );
      }
    }
  }
  m_BinDecoder.start();
}





//================================================================================
//  clause 7.3.8.8
//--------------------------------------------------------------------------------
//    void  transform_tree      ( cs, area, cuCtx, chromaCbfs )
//    bool  split_transform_flag( depth )
//    bool  cbf_comp            ( area, depth )
//================================================================================

void CABACReader::transform_tree( CodingStructure &cs, Partitioner &partitioner, CUCtx& cuCtx, ChromaCbfs& chromaCbfs )
{
  const UnitArea& area          = partitioner.currArea();

#if HM_QTBT_AS_IN_JEM_SYNTAX
  if( cs.pcv->noRQT )
  {
    TransformUnit &tu = cs.addTU( CS::getArea( cs, area, partitioner.chType ), partitioner.chType );
#if ENABLE_BMS
    tu.depth = 0;
#endif

    unsigned numBlocks = ::getNumberValidTBlocks( *cs.pcv );
    for( unsigned compID = COMPONENT_Y; compID < numBlocks; compID++ )
    {
      if( tu.blocks[compID].valid() )
      {
        tu.getCoeffs( ComponentID( compID ) ).fill( 0 );
        tu.getPcmbuf( ComponentID( compID ) ).fill( 0 );
      }
    }

    transform_unit_qtbt( tu, cuCtx, chromaCbfs );
    return;
  }

#endif
  CodingUnit&     cu            = *cs.getCU( area.blocks[partitioner.chType], partitioner.chType );
#if ENABLE_BMS
  const unsigned  trDepth       = partitioner.currTrDepth;

  // split_transform_flag
  bool split = false;
  if( cu.cs->pcv->noRQT )
  {
    split = partitioner.canSplit( TU_MAX_TR_SPLIT, cs );
  }
#endif

  // cbf_cb & cbf_cr
  if( area.chromaFormat != CHROMA_400 && area.blocks[COMPONENT_Cb].valid() && ( !CS::isDualITree( cs ) || partitioner.chType == CHANNEL_TYPE_CHROMA ) )
  {
    {
      {
        if( chromaCbfs.Cb )
        {
#if ENABLE_BMS
          chromaCbfs.Cb &= cbf_comp( cs, area.blocks[COMPONENT_Cb], trDepth );
#else
          chromaCbfs.Cb &= cbf_comp( cs, area.blocks[COMPONENT_Cb] );
#endif
        }
        if( chromaCbfs.Cr )
        {
#if ENABLE_BMS
#if JVET_K0072
          chromaCbfs.Cr &= cbf_comp( cs, area.blocks[COMPONENT_Cr], trDepth, chromaCbfs.Cb );
#else
          chromaCbfs.Cr &= cbf_comp( cs, area.blocks[COMPONENT_Cr], trDepth );
#endif
#else
#if JVET_K0072
          chromaCbfs.Cr &= cbf_comp( cs, area.blocks[COMPONENT_Cr], chromaCbfs.Cb );
#else
          chromaCbfs.Cr &= cbf_comp( cs, area.blocks[COMPONENT_Cr] );
#endif
#endif
        }
      }
    }
  }
  else if( CS::isDualITree( cs ) )
  {
    chromaCbfs = ChromaCbfs( false );
  }

#if ENABLE_BMS
  if( split )
  {
    {
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
        THROW( "Implicit TU split not available!" );
    }

    do
    {
      ChromaCbfs subCbfs = chromaCbfs;
      transform_tree( cs, partitioner, cuCtx, subCbfs );
    } while( partitioner.nextPart( cs ) );

    partitioner.exitCurrSplit();

    const UnitArea &currArea  = partitioner.currArea();
    const unsigned  currDepth = partitioner.currTrDepth;
    const unsigned numTBlocks = getNumberValidTBlocks( *cs.pcv );

    unsigned        compCbf[3] = { 0, 0, 0 };

    for( auto &currTU : cs.traverseTUs( currArea, partitioner.chType ) )
    {
      for( unsigned ch = 0; ch < numTBlocks; ch++ )
      {
        compCbf[ch] |= ( TU::getCbfAtDepth( currTU, ComponentID( ch ), currDepth + 1 ) ? 1 : 0 );
      }
    }

    {

      for( auto &currTU : cs.traverseTUs( currArea, partitioner.chType ) )
      {
        TU::setCbfAtDepth( currTU, COMPONENT_Y, currDepth, compCbf[COMPONENT_Y] );
        if( currArea.chromaFormat != CHROMA_400 )
        {
          TU::setCbfAtDepth( currTU, COMPONENT_Cb, currDepth, compCbf[COMPONENT_Cb] );
          TU::setCbfAtDepth( currTU, COMPONENT_Cr, currDepth, compCbf[COMPONENT_Cr] );
        }
      }
    }
  }
  else
#endif
  {
    TransformUnit &tu = cs.addTU( CS::getArea( cs, area, partitioner.chType ), partitioner.chType );
    unsigned numBlocks = ::getNumberValidTBlocks( *cs.pcv );

    for( unsigned compID = COMPONENT_Y; compID < numBlocks; compID++ )
    {
      if( tu.blocks[compID].valid() )
      {
        tu.getCoeffs( ComponentID( compID ) ).fill( 0 );
        tu.getPcmbuf( ComponentID( compID ) ).fill( 0 );
      }
    }
#if ENABLE_BMS
    tu.depth = trDepth;
#endif
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
        TU::setCbfAtDepth( tu, COMPONENT_Y, trDepth, 1 );
      }
#else
      if( !CU::isIntra( cu ) && !chromaCbfs.sigChroma( area.chromaFormat ) )
      {
        TU::setCbf( tu, COMPONENT_Y, true );
      }
#endif
      else
      {
#if ENABLE_BMS
        bool cbfY = cbf_comp( cs, tu.Y(), trDepth );
#else
        bool cbfY = cbf_comp( cs, tu.Y() );
#endif
#if ENABLE_BMS
        TU::setCbfAtDepth( tu, COMPONENT_Y, trDepth, ( cbfY ? 1 : 0 ) );
#else
        TU::setCbf( tu, COMPONENT_Y, cbfY );
#endif
      }
    }
    if( area.chromaFormat != CHROMA_400 )
    {
#if ENABLE_BMS
      TU::setCbfAtDepth( tu, COMPONENT_Cb, trDepth, ( chromaCbfs.Cb ? 1 : 0 ) );
      TU::setCbfAtDepth( tu, COMPONENT_Cr, trDepth, ( chromaCbfs.Cr ? 1 : 0 ) );
#else
      TU::setCbf( tu, COMPONENT_Cb, chromaCbfs.Cb );
      TU::setCbf( tu, COMPONENT_Cr, chromaCbfs.Cr );
#endif
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
bool CABACReader::cbf_comp( CodingStructure& cs, const CompArea& area, unsigned depth, const bool prevCbCbf )
#else
bool CABACReader::cbf_comp( CodingStructure& cs, const CompArea& area, unsigned depth )
#endif
#else
#if JVET_K0072
bool CABACReader::cbf_comp( CodingStructure& cs, const CompArea& area, const bool prevCbCbf )
#else
bool CABACReader::cbf_comp( CodingStructure& cs, const CompArea& area )
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

  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE2(STATS__CABAC_BITS__QT_CBF, area.size(), area.compID);

  const unsigned  cbf = m_BinDecoder.decodeBin( ctxSet( ctxId ) );

  DTRACE( g_trace_ctx, D_SYNTAX, "cbf_comp() etype=%d pos=(%d,%d) ctx=%d cbf=%d\n", area.compID, area.x, area.y, ctxId, cbf );
  return cbf;
}





//================================================================================
//  clause 7.3.8.9
//--------------------------------------------------------------------------------
//    void  mvd_coding( pu, refList )
//================================================================================

void CABACReader::mvd_coding( Mv &rMvd )
{
#if RExt__DECODER_DEBUG_BIT_STATISTICS
  CodingStatisticsClassType ctype_mvd    ( STATS__CABAC_BITS__MVD );
  CodingStatisticsClassType ctype_mvd_ep ( STATS__CABAC_BITS__MVD_EP );
#endif

  RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_mvd );

  // abs_mvd_greater0_flag[ 0 | 1 ]
  int horAbs = (int)m_BinDecoder.decodeBin(Ctx::Mvd());
  int verAbs = (int)m_BinDecoder.decodeBin(Ctx::Mvd());

  // abs_mvd_greater1_flag[ 0 | 1 ]
  if (horAbs)
  {
    horAbs += (int)m_BinDecoder.decodeBin(Ctx::Mvd(1));
  }
  if (verAbs)
  {
    verAbs += (int)m_BinDecoder.decodeBin(Ctx::Mvd(1));
  }

  RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_mvd_ep );

  // abs_mvd_minus2[ 0 | 1 ] and mvd_sign_flag[ 0 | 1 ]
  if (horAbs)
  {
    if (horAbs > 1)
    {
      horAbs += exp_golomb_eqprob(1 );
    }
    if (m_BinDecoder.decodeBinEP())
    {
      horAbs = -horAbs;
    }
  }
  if (verAbs)
  {
    if (verAbs > 1)
    {
      verAbs += exp_golomb_eqprob(1 );
    }
    if (m_BinDecoder.decodeBinEP())
    {
      verAbs = -verAbs;
    }
  }
  rMvd = Mv(horAbs, verAbs);
}


//================================================================================
//  clause 7.3.8.10
//--------------------------------------------------------------------------------
//    void  transform_unit      ( tu, cuCtx, chromaCbfs )
//    void  cu_qp_delta         ( cu )
//    void  cu_chroma_qp_offset ( cu )
//================================================================================

void CABACReader::transform_unit( TransformUnit& tu, CUCtx& cuCtx, ChromaCbfs& chromaCbfs )
{
  CodingUnit& cu         = *tu.cu;
  bool        lumaOnly   = ( cu.chromaFormat == CHROMA_400 || !tu.blocks[COMPONENT_Cb].valid() );
  bool        cbfLuma    = ( tu.cbf[ COMPONENT_Y ] != 0 );
  bool        cbfChroma  = ( cu.chromaFormat == CHROMA_400 ? false : ( chromaCbfs.Cb || chromaCbfs.Cr ) );



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
        if( tu.cbf[ compID ] )
        {
          residual_coding( tu, compID );
        }
      }
    }
  }
}

#if HM_QTBT_AS_IN_JEM_SYNTAX
void CABACReader::transform_unit_qtbt( TransformUnit& tu, CUCtx& cuCtx, ChromaCbfs& chromaCbfs )
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
      bool cbf = false;
#if ENABLE_BMS
#if JVET_K0072
      cbf = cbf_comp( *tu.cs, tu.blocks[compID], tu.depth, prevCbf );
      prevCbf = cbf;
#else
      cbf = cbf_comp( *tu.cs, tu.blocks[compID], tu.depth );
#endif
      chromaCbfs.cbf( compID ) = cbf;
      TU::setCbfAtDepth( tu, compID, tu.depth, cbf ? 1 : 0 );
#else
#if JVET_K0072
      cbf = cbf_comp( *tu.cs, tu.blocks[compID], prevCbf );
      prevCbf = cbf;
#else
      cbf = cbf_comp( *tu.cs, tu.blocks[compID] );
#endif
      chromaCbfs.cbf( compID ) = cbf;
      TU::setCbf( tu, compID, cbf );
#endif

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
      TU::setCbfAtDepth( tu, COMPONENT_Y, tu.depth, 1 );
#else
      TU::setCbf( tu, COMPONENT_Y, true );
#endif
    }
    else
    {
#if ENABLE_BMS
      bool cbf = cbf_comp( *tu.cs, tu.Y(), tu.depth );
      TU::setCbfAtDepth( tu, COMPONENT_Y, tu.depth, cbf ? 1 : 0 );
#else
      bool cbf = cbf_comp( *tu.cs, tu.Y() );
      TU::setCbf( tu, COMPONENT_Y, cbf );
#endif
    }
  }

  if( tu.cbf[0] )
  {
#if (JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT) && HM_EMT_NSST_AS_IN_JEM
    emt_cu_flag    ( cu );
#endif
    residual_coding( tu, COMPONENT_Y );
    cbfLuma = true;
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

void CABACReader::cu_qp_delta( CodingUnit& cu, int predQP, int8_t& qp )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__DELTA_QP_EP );

  CHECK( predQP == std::numeric_limits<int>::max(), "Invalid predicted QP" );
  int qpY = predQP;
  int DQp = unary_max_symbol( Ctx::DeltaQP(), Ctx::DeltaQP(1), CU_DQP_TU_CMAX );
  if( DQp >= CU_DQP_TU_CMAX )
  {
    DQp += exp_golomb_eqprob( CU_DQP_EG_k  );
  }
  if( DQp > 0 )
  {
    if( m_BinDecoder.decodeBinEP( ) )
    {
      DQp = -DQp;
    }
    int     qpBdOffsetY = cu.cs->sps->getQpBDOffset( CHANNEL_TYPE_LUMA );
#if JVET_K0251_QP_EXT
    qpY = ( (predQP + DQp + (MAX_QP + 1) + 2 * qpBdOffsetY) % ((MAX_QP + 1) + qpBdOffsetY)) - qpBdOffsetY;
#else
    qpY = ( ( predQP + DQp + 52 + 2*qpBdOffsetY ) % (52 + qpBdOffsetY) ) - qpBdOffsetY;
#endif
  }
  qp = (int8_t)qpY;

  DTRACE( g_trace_ctx, D_DQP, "x=%d, y=%d, d=%d, pred_qp=%d, DQp=%d, qp=%d\n", cu.blocks[cu.chType].lumaPos().x, cu.blocks[cu.chType].lumaPos().y, cu.qtDepth, predQP, DQp, qp );
}


void CABACReader::cu_chroma_qp_offset( CodingUnit& cu )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE2( STATS__CABAC_BITS__CHROMA_QP_ADJUSTMENT, cu.blocks[cu.chType].lumaSize(), CHANNEL_TYPE_CHROMA );

  // cu_chroma_qp_offset_flag
  int       length  = cu.cs->pps->getPpsRangeExtension().getChromaQpOffsetListLen();
  unsigned  qpAdj   = m_BinDecoder.decodeBin( Ctx::ChromaQpAdjFlag() );
  if( qpAdj && length > 1 )
  {
    // cu_chroma_qp_offset_idx
    qpAdj += unary_max_symbol( Ctx::ChromaQpAdjIdc(), Ctx::ChromaQpAdjIdc(), length-1 );
  }
  /* NB, symbol = 0 if outer flag is not set,
   *              1 if outer flag is set and there is no inner flag
   *              1+ otherwise */
  cu.chromaQpAdj = cu.cs->chromaQpAdj = qpAdj;
}





//================================================================================
//  clause 7.3.8.11
//--------------------------------------------------------------------------------
//    void        residual_coding         ( tu, compID )
//    bool        transform_skip_flag     ( tu, compID )
//    RDPCMMode   explicit_rdpcm_mode     ( tu, compID )
//    int         last_sig_coeff          ( coeffCtx )
//    void        residual_coding_subblock( coeffCtx )
//================================================================================

void CABACReader::residual_coding( TransformUnit& tu, ComponentID compID )
{
#if ENABLE_TRACING || HEVC_USE_SIGN_HIDING || JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT
  const CodingUnit& cu = *tu.cu;
#endif
  DTRACE( g_trace_ctx, D_SYNTAX, "residual_coding() etype=%d pos=(%d,%d) size=%dx%d predMode=%d\n", tu.blocks[compID].compID, tu.blocks[compID].x, tu.blocks[compID].y, tu.blocks[compID].width, tu.blocks[compID].height, cu.predMode );

  // parse transform skip and explicit rdpcm mode
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
  TCoeff*             coeff   = tu.getCoeffs( compID ).buf;
#if JVET_K0072
#else
  unsigned&           GRStats = m_BinDecoder.getCtx().getGRAdaptStats( TU::getGolombRiceStatisticsIndex( tu, compID ) );
#endif
#if JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT
  unsigned            numSig  = 0;
#endif

  // parse last coeff position
  cctx.setScanPosLast( last_sig_coeff( cctx ) );

  // parse subblocks
#if JVET_K0072
  const int stateTransTab = ( tu.cs->slice->getDepQuantEnabledFlag() ? 32040 : 0 );
  int       state         = 0;
#else
  cctx.setGoRiceStats( GRStats );
#endif

#if JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT
  bool useEmt = ( cu.cs->sps->getSpsNext().getUseIntraEMT() && cu.predMode == MODE_INTRA ) || ( cu.cs->sps->getSpsNext().getUseInterEMT() && cu.predMode != MODE_INTRA );
  useEmt = useEmt && isLuma(compID);
#endif

    for( int subSetId = ( cctx.scanPosLast() >> cctx.log2CGSize() ); subSetId >= 0; subSetId--)
    {
      cctx.initSubblock       ( subSetId );
#if JVET_K0072
      residual_coding_subblock( cctx, coeff, stateTransTab, state );
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
        tu.emtIdx = 0; //default transform
      }
    }
    else
    {
      emt_tu_index( tu );
    }
  }

#endif
}


void CABACReader::transform_skip_flag( TransformUnit& tu, ComponentID compID )
{

#if HM_EMT_NSST_AS_IN_JEM && (JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT)
  if( !tu.cu->cs->pps->getUseTransformSkip() || tu.cu->transQuantBypass || !TU::hasTransformSkipFlag( *tu.cs, tu.blocks[compID] ) || ( isLuma( compID ) && tu.cu->emtFlag ) )
#else
  if( !tu.cu->cs->pps->getUseTransformSkip() || tu.cu->transQuantBypass || !TU::hasTransformSkipFlag( *tu.cs, tu.blocks[compID] ) )
#endif
  {
    tu.transformSkip[compID] = false;
    return;
  }
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET2( STATS__CABAC_BITS__TRANSFORM_SKIP_FLAGS, compID );

  bool tskip = m_BinDecoder.decodeBin( Ctx::TransformSkipFlag( toChannelType( compID ) ) );
  DTRACE( g_trace_ctx, D_SYNTAX, "transform_skip_flag() etype=%d pos=(%d,%d) trSkip=%d\n", compID, tu.blocks[compID].x, tu.blocks[compID].y, (int)tskip );
  tu.transformSkip[compID] = tskip;
}

#if JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT
void CABACReader::emt_tu_index( TransformUnit& tu )
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

  uint8_t trIdx = 0;
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__EMT_TU_INDEX );

  if( CU::isIntra( *tu.cu ) && ( tu.cu->Y().width <= maxSizeEmtIntra ) && ( tu.cu->Y().height <= maxSizeEmtIntra ) )
  {
    bool uiSymbol1 = m_BinDecoder.decodeBin( Ctx::EMTTuIndex( 0 ) );
    bool uiSymbol2 = m_BinDecoder.decodeBin( Ctx::EMTTuIndex( 1 ) );

    trIdx = ( uiSymbol2 << 1 ) | ( int ) uiSymbol1;

    DTRACE( g_trace_ctx, D_SYNTAX, "emt_tu_index() etype=%d pos=(%d,%d) emtTrIdx=%d\n", COMPONENT_Y, tu.lx(), tu.ly(), ( int ) trIdx );
  }
  if( !CU::isIntra( *tu.cu ) && ( tu.cu->Y().width <= maxSizeEmtInter ) && ( tu.cu->Y().height <= maxSizeEmtInter ) )
  {
    bool uiSymbol1 = m_BinDecoder.decodeBin( Ctx::EMTTuIndex( 2 ) );
    bool uiSymbol2 = m_BinDecoder.decodeBin( Ctx::EMTTuIndex( 3 ) );

    trIdx = ( uiSymbol2 << 1 ) | ( int ) uiSymbol1;

    DTRACE( g_trace_ctx, D_SYNTAX, "emt_tu_index() etype=%d pos=(%d,%d) emtTrIdx=%d\n", COMPONENT_Y, tu.lx(), tu.ly(), ( int ) trIdx );
  }

  tu.emtIdx = trIdx;
}

void CABACReader::emt_cu_flag( CodingUnit& cu )
{
  const CodingStructure &cs = *cu.cs;

  if( !( ( cs.sps->getSpsNext().getUseIntraEMT() && CU::isIntra( cu ) ) || ( cs.sps->getSpsNext().getUseInterEMT() && CU::isInter( cu ) ) ) || isChroma( cu.chType ) )
  {
    return;
  }

  unsigned       depth      = cu.qtDepth;
  const unsigned cuWidth    = cu.lwidth();
  const unsigned cuHeight   = cu.lheight();

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

  cu.emtFlag = 0;

  const unsigned maxSizeEmt = CU::isIntra( cu ) ? maxSizeEmtIntra : maxSizeEmtInter;

  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__EMT_CU_FLAG );
  if( cuWidth <= maxSizeEmt && cuHeight <= maxSizeEmt )
  {
    bool uiCuFlag = m_BinDecoder.decodeBin( Ctx::EMTCuFlag( depth ) );
    cu.emtFlag = uiCuFlag;
    DTRACE( g_trace_ctx, D_SYNTAX, "emt_cu_flag() etype=%d pos=(%d,%d) emtCuFlag=%d\n", COMPONENT_Y, cu.lx(), cu.ly(), ( int ) cu.emtFlag );
  }
}
#endif


void CABACReader::explicit_rdpcm_mode( TransformUnit& tu, ComponentID compID )
{
  const CodingUnit& cu = *tu.cu;

  tu.rdpcm[compID] = RDPCM_OFF;

  if( !CU::isIntra(cu) && CU::isRDPCMEnabled(cu) && ( tu.transformSkip[compID] || cu.transQuantBypass ) )
  {
    RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE( STATS__EXPLICIT_RDPCM_BITS, tu.blocks[tu.chType].lumaSize() );

    ChannelType chType = toChannelType( compID );
    if( m_BinDecoder.decodeBin( Ctx::RdpcmFlag( chType ) ) )
    {
      if( m_BinDecoder.decodeBin( Ctx::RdpcmDir( chType ) ) )
      {
        tu.rdpcm[compID] = RDPCM_VER;
      }
      else
      {
        tu.rdpcm[compID] = RDPCM_HOR;
      }
    }
  }
}

#if JEM_TOOLS
void CABACReader::residual_nsst_mode( CodingUnit& cu )
{
  if( CS::isDualITree( *cu.cs ) && cu.chType == CHANNEL_TYPE_CHROMA && std::min( cu.blocks[1].width, cu.blocks[1].height ) < 4 )
  {
    return;
  }

  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET( STATS__CABAC_BITS__NSST );
#if JEM_TOOLS
  if( cu.cs->sps->getSpsNext().getUseNSST() && CU::isIntra( cu ) && !CU::isLosslessCoded( cu ) && !cu.pdpc )
#else
  if( cu.cs->sps->getSpsNext().getUseNSST() && CU::isIntra( cu ) && !CU::isLosslessCoded( cu ) )
#endif
  {
    bool nonZeroCoeffNonTs;
    if( cu.cs->pcv->noRQT )
    {
      const int nonZeroCoeffThr = CS::isDualITree( *cu.cs ) ? ( isLuma( cu.chType ) ? NSST_SIG_NZ_LUMA : NSST_SIG_NZ_CHROMA ) : NSST_SIG_NZ_LUMA + NSST_SIG_NZ_CHROMA;
      nonZeroCoeffNonTs = CU::getNumNonZeroCoeffNonTs( cu ) > nonZeroCoeffThr;
    }
    else
    {
      nonZeroCoeffNonTs = CU::hasNonTsCodedBlock( cu );
    }
    if( !nonZeroCoeffNonTs )
    {
      cu.nsstIdx = 0;
      return;
    }
  }
  else
  {
    cu.nsstIdx = 0;
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
    uint32_t idxROT = m_BinDecoder.decodeBin( Ctx::NSSTIdx( 1 ) );
    if( idxROT )
    {
      idxROT += m_BinDecoder.decodeBin( Ctx::NSSTIdx( 3 ) );
    }
    cu.nsstIdx = idxROT;
  }
  else
  {
    uint32_t idxROT = m_BinDecoder.decodeBin( Ctx::NSSTIdx( 0 ) );
    if( idxROT )
    {
      uint32_t uiSymbol = m_BinDecoder.decodeBin( Ctx::NSSTIdx( 2 ) );
      if( uiSymbol )
      {
        idxROT += 1 + m_BinDecoder.decodeBin( Ctx::NSSTIdx( 4 ) );
      }
    }
    cu.nsstIdx = idxROT;
  }

  DTRACE( g_trace_ctx, D_SYNTAX, "residual_nsst_mode() etype=%d pos=(%d,%d) mode=%d\n", COMPONENT_Y, cu.lx(), cu.ly(), ( int ) cu.nsstIdx );
}
#endif

int CABACReader::last_sig_coeff( CoeffCodingContext& cctx )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE2( STATS__CABAC_BITS__LAST_SIG_X_Y, Size( cctx.width(), cctx.height() ), cctx.compID() );

  unsigned PosLastX = 0, PosLastY = 0;
  for( ; PosLastX < cctx.maxLastPosX(); PosLastX++ )
  {
    if( ! m_BinDecoder.decodeBin( cctx.lastXCtxId( PosLastX ) ) )
    {
      break;
    }
  }
  for( ; PosLastY < cctx.maxLastPosY(); PosLastY++ )
  {
    if( ! m_BinDecoder.decodeBin( cctx.lastYCtxId( PosLastY ) ) )
    {
      break;
    }
  }
  if( PosLastX > 3 )
  {
    uint32_t uiTemp  = 0;
    uint32_t uiCount = ( PosLastX - 2 ) >> 1;
    for ( int i = uiCount - 1; i >= 0; i-- )
    {
      uiTemp += m_BinDecoder.decodeBinEP( ) << i;
    }
    PosLastX = g_uiMinInGroup[ PosLastX ] + uiTemp;
  }
  if( PosLastY > 3 )
  {
    uint32_t uiTemp  = 0;
    uint32_t uiCount = ( PosLastY - 2 ) >> 1;
    for ( int i = uiCount - 1; i >= 0; i-- )
    {
      uiTemp += m_BinDecoder.decodeBinEP( ) << i;
    }
    PosLastY = g_uiMinInGroup[ PosLastY ] + uiTemp;
  }

  int blkPos;
#if HEVC_USE_MDCS
  if( cctx.scanType() == SCAN_VER )
  {
    blkPos = PosLastY + ( PosLastX * cctx.width() );
  }
  else
#endif
  {
    blkPos = PosLastX + ( PosLastY * cctx.width() );
  }

  int scanPos = 0;
  for( ; scanPos < cctx.maxNumCoeff() - 1; scanPos++ )
  {
    if( blkPos == cctx.blockPos( scanPos ) )
    {
      break;
    }
  }
  return scanPos;
}



#if JVET_K0072
void CABACReader::residual_coding_subblock( CoeffCodingContext& cctx, TCoeff* coeff, const int stateTransTable, int& state )
{
  // NOTE: All coefficients of the subblock must be set to zero before calling this function
#if RExt__DECODER_DEBUG_BIT_STATISTICS
  CodingStatisticsClassType ctype_group ( STATS__CABAC_BITS__SIG_COEFF_GROUP_FLAG,  cctx.width(), cctx.height(), cctx.compID() );
  CodingStatisticsClassType ctype_map   ( STATS__CABAC_BITS__SIG_COEFF_MAP_FLAG,    cctx.width(), cctx.height(), cctx.compID() );
  CodingStatisticsClassType ctype_par   ( STATS__CABAC_BITS__PAR_FLAG,              cctx.width(), cctx.height(), cctx.compID() );
  CodingStatisticsClassType ctype_gt1   ( STATS__CABAC_BITS__GT1_FLAG,              cctx.width(), cctx.height(), cctx.compID() );
  CodingStatisticsClassType ctype_gt2   ( STATS__CABAC_BITS__GT2_FLAG,              cctx.width(), cctx.height(), cctx.compID() );
  CodingStatisticsClassType ctype_escs  ( STATS__CABAC_BITS__ESCAPE_BITS,           cctx.width(), cctx.height(), cctx.compID() );
#endif

  //===== init =====
  const int   minSubPos   = cctx.minSubPos();
  const bool  isLast      = cctx.isLast();
  int         firstSigPos = ( isLast ? cctx.scanPosLast() : cctx.maxSubPos() );
  int         nextSigPos  = firstSigPos;

  //===== decode significant_coeffgroup_flag =====
  RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_group );
  bool sigGroup = ( isLast || !minSubPos );
  if( !sigGroup )
  {
    sigGroup = m_BinDecoder.decodeBin( cctx.sigGroupCtxId() );
  }
  if( sigGroup )
  {
    cctx.setSigGroup();
  }
  else
  {
    return;
  }

  uint8_t   ctxOffset[16];
  unsigned  nextPass = 0;

  //===== decode absolute values =====
  const int inferSigPos   = nextSigPos != cctx.scanPosLast() ? ( cctx.isNotFirst() ? minSubPos : -1 ) : nextSigPos;
#if HEVC_USE_SIGN_HIDING
  int       firstNZPos    = nextSigPos;
  int       lastNZPos     = -1;
#endif
  int       numNonZero    =  0;
  int       sigBlkPos[ 1 << MLS_CG_SIZE ];

  for( ; nextSigPos >= minSubPos; nextSigPos-- )
  {
    int      blkPos     = cctx.blockPos( nextSigPos );
    unsigned sigFlag    = ( !numNonZero && nextSigPos == inferSigPos );
    if( !sigFlag )
    {
      RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_map );
      const unsigned sigCtxId = cctx.sigCtxIdAbs( nextSigPos, coeff, state );
      sigFlag = m_BinDecoder.decodeBin( sigCtxId );
      DTRACE( g_trace_ctx, D_SYNTAX_RESI, "sig_bin() bin=%d ctx=%d\n", sigFlag, sigCtxId );
    }

    if( sigFlag )
    {
      uint8_t&  ctxOff = ctxOffset[ nextSigPos - minSubPos ];
      ctxOff           = cctx.ctxOffsetAbs();
      sigBlkPos[ numNonZero++ ] = blkPos;
#if HEVC_USE_SIGN_HIDING
      firstNZPos = nextSigPos;
      lastNZPos  = std::max<int>( lastNZPos, nextSigPos );
#endif

      RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_par );
      unsigned parFlag = m_BinDecoder.decodeBin( cctx.parityCtxIdAbs(ctxOff) );
      DTRACE( g_trace_ctx, D_SYNTAX_RESI, "par_flag() bin=%d ctx=%d\n", parFlag, cctx.parityCtxIdAbs(ctxOff) );

      RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_gt1 );
      unsigned gt1Flag = m_BinDecoder.decodeBin( cctx.greater1CtxIdAbs(ctxOff) );
      DTRACE( g_trace_ctx, D_SYNTAX_RESI, "gt1_flag() bin=%d ctx=%d\n", gt1Flag, cctx.greater1CtxIdAbs(ctxOff) );
      coeff[blkPos] += 1+parFlag+(gt1Flag<<1);
      nextPass      |= gt1Flag;
    }

    state = ( stateTransTable >> ((state<<2)+((coeff[blkPos]&1)<<1)) ) & 3;
  }

  //===== 2nd PASS: gt2 =====
  if( nextPass )
  {
    nextPass = 0;
    for( int scanPos = firstSigPos; scanPos >= minSubPos; scanPos-- )
    {
      TCoeff& tcoeff = coeff[ cctx.blockPos( scanPos ) ];
      if( tcoeff > 2 )
      {
        RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_gt2 );
        uint8_t& ctxOff  = ctxOffset[ scanPos - minSubPos ];
        unsigned gt2Flag = m_BinDecoder.decodeBin( cctx.greater2CtxIdAbs(ctxOff) );
        DTRACE( g_trace_ctx, D_SYNTAX_RESI, "gt2_flag() bin=%d ctx=%d\n", gt2Flag, cctx.greater2CtxIdAbs(ctxOff) );
        tcoeff    += (gt2Flag<<1);
        nextPass  |=  gt2Flag;
      }
    }
  }

  //===== 3rd PASS: Go-rice codes =====
  if( nextPass )
  {
    for( int scanPos = firstSigPos; scanPos >= minSubPos; scanPos-- )
    {
      TCoeff& tcoeff = coeff[ cctx.blockPos( scanPos ) ];
      if( tcoeff > 4 )
      {
        RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_escs );
        unsigned ricePar = cctx.GoRiceParAbs( scanPos, coeff );
        int  remAbsLevel = m_BinDecoder.decodeRemAbsEP( ricePar, cctx.extPrec(), cctx.maxLog2TrDRange() );
        DTRACE( g_trace_ctx, D_SYNTAX_RESI, "rem_val() bin=%d ctx=%d\n", remAbsLevel, ricePar );
        tcoeff += (remAbsLevel<<1);
      }
    }
  }

  //===== decode sign's =====
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE2( STATS__CABAC_BITS__SIGN_BIT, Size( cctx.width(), cctx.height() ), cctx.compID() );
#if HEVC_USE_SIGN_HIDING
  const unsigned  numSigns    = ( cctx.hideSign( firstNZPos, lastNZPos ) ? numNonZero - 1 : numNonZero );
  unsigned        signPattern = m_BinDecoder.decodeBinsEP( numSigns ) << ( 32 - numSigns );
#else
  unsigned        signPattern = m_BinDecoder.decodeBinsEP( numNonZero ) << ( 32 - numNonZero );
#endif

  //===== set final coefficents =====
  int sumAbs = 0;
#if HEVC_USE_SIGN_HIDING
  for( unsigned k = 0; k < numSigns; k++ )
#else
  for( unsigned k = 0; k < numNonZero; k++ )
#endif
  {
    int AbsCoeff          = coeff[ sigBlkPos[ k ] ];
    sumAbs               += AbsCoeff;
    coeff[ sigBlkPos[k] ] = ( signPattern & ( 1u << 31 ) ? -AbsCoeff : AbsCoeff );
    signPattern         <<= 1;
  }
#if HEVC_USE_SIGN_HIDING
  if( numNonZero > numSigns )
  {
    int k                 = numSigns;
    int AbsCoeff          = coeff[ sigBlkPos[ k ] ];
    sumAbs               += AbsCoeff;
    coeff[ sigBlkPos[k] ] = ( sumAbs & 1 ? -AbsCoeff : AbsCoeff );
  }
#endif
#if JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT
  cctx.setEmtNumSigCoeff( numNonZero );
#endif
}

#else

void CABACReader::residual_coding_subblock( CoeffCodingContext& cctx, TCoeff* coeff )
{
  // NOTE: All coefficients of the subblock must be set to zero before calling this function
#if RExt__DECODER_DEBUG_BIT_STATISTICS
  CodingStatisticsClassType ctype_group ( STATS__CABAC_BITS__SIG_COEFF_GROUP_FLAG,  cctx.width(), cctx.height(), cctx.compID() );
  CodingStatisticsClassType ctype_map   ( STATS__CABAC_BITS__SIG_COEFF_MAP_FLAG,    cctx.width(), cctx.height(), cctx.compID() );
  CodingStatisticsClassType ctype_gt1   ( STATS__CABAC_BITS__GT1_FLAG,              cctx.width(), cctx.height(), cctx.compID() );
  CodingStatisticsClassType ctype_gt2   ( STATS__CABAC_BITS__GT2_FLAG,              cctx.width(), cctx.height(), cctx.compID() );
#endif
  RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_group );

  //===== init =====
  const int   maxSbbSize  = 1 << cctx.log2CGSize();
  const int   minSubPos   = cctx.minSubPos();
  const bool  isLast      = cctx.isLast();
  int         nextSigPos  = ( isLast ? cctx.scanPosLast() : cctx.maxSubPos() );

  //===== decode significant_coeffgroup_flag =====
  bool sigGroup = ( isLast || !minSubPos );
  if( !sigGroup )
  {
    sigGroup = m_BinDecoder.decodeBin( cctx.sigGroupCtxId() );
  }
  if( sigGroup )
  {
    cctx.setSigGroup();
  }
  else
  {
    return;
  }

#if JEM_TOOLS
  if( cctx.altResiCompId() == 1 )
  {
    //===== decode significant_coeff_flag's =====
    const int inferSigPos = ( cctx.isNotFirst() ? minSubPos : -1 );
    unsigned  numNonZero  = 0;
#if HEVC_USE_SIGN_HIDING
    int       firstNZPos  = maxSbbSize;
    int       lastNZPos   = -1;
#endif
    int       sigBlkPos   [ 1 << MLS_CG_SIZE ];
    int       scanPos     [ 1 << MLS_CG_SIZE ];
    if( isLast )
    {
#if HEVC_USE_SIGN_HIDING
      firstNZPos                = nextSigPos;
      lastNZPos                 = nextSigPos;
#endif
      sigBlkPos[ numNonZero   ] = cctx.blockPos( nextSigPos );
      scanPos  [ numNonZero++ ] = nextSigPos--;
      coeff[ sigBlkPos[ 0 ] ]   = 1;
    }
    for( ; nextSigPos >= minSubPos; nextSigPos-- )
    {
      unsigned sigFlag = ( !numNonZero && nextSigPos == inferSigPos );
      if( !sigFlag )
      {
        sigFlag = m_BinDecoder.decodeBin( cctx.sigCtxId( nextSigPos, coeff ) );
      }
      if( sigFlag )
      {
        sigBlkPos [ numNonZero ]    = cctx.blockPos( nextSigPos );
        scanPos   [ numNonZero++ ]  = nextSigPos;
        coeff[ cctx.blockPos( nextSigPos ) ] = 1;
#if HEVC_USE_SIGN_HIDING
        firstNZPos                  = nextSigPos;
        lastNZPos                   = std::max<int>( lastNZPos, nextSigPos );
#endif
      }
    }

    RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_gt1 );

    //===== decode abs_greater1_flag's =====
    const unsigned  numGt1Flags = std::min<unsigned>( numNonZero, C1FLAG_NUMBER );
    int             gt2FlagIdx  = maxSbbSize;
    bool            escapeData  = false;
    uint16_t        ctxGt1Id    = cctx.greater1CtxId( 0 );
    for( unsigned k = 0; k < numGt1Flags; k++ )
    {
      if( k || !cctx.isLast() )
      {
        ctxGt1Id = cctx.greater1CtxId( scanPos[ k ], coeff );
      }

      if( m_BinDecoder.decodeBin( ctxGt1Id ) )
      {
        coeff[ sigBlkPos[ k ] ] = 2;
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
        coeff[ sigBlkPos[ k ] ] = 1;
      }
    }
    for( unsigned k = numGt1Flags; k < numNonZero; k++ )
    {
      coeff[ sigBlkPos[ k ] ] = 1;
      escapeData              = true;
    }
    cctx.setGt2Flag( ctxGt1Id == 0 );

    RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_gt2 );

    //===== decode abs_greater2_flag =====
    if( gt2FlagIdx < maxSbbSize )
    {
      uint16_t ctxGt2Id = cctx.greater1CtxIdOfs();
      if( gt2FlagIdx || !cctx.isLast() )
      {
        ctxGt2Id = cctx.greater2CtxId( scanPos[ gt2FlagIdx ], coeff );
      }

      if( m_BinDecoder.decodeBin( ctxGt2Id ) )
      {
        coeff[ sigBlkPos[ gt2FlagIdx ] ]++;
        escapeData = true;
      }
    }

    //===== align data =====
    if( escapeData && cctx.alignFlag() )
    {
      m_BinDecoder.align();
    }

#if RExt__DECODER_DEBUG_BIT_STATISTICS
    const bool alignGroup = escapeData && cctx.alignFlag();
    CodingStatisticsClassType ctype_signs( ( alignGroup ? STATS__CABAC_BITS__ALIGNED_SIGN_BIT    : STATS__CABAC_BITS__SIGN_BIT    ), cctx.width(), cctx.height(), cctx.compID() );
    CodingStatisticsClassType ctype_escs ( ( alignGroup ? STATS__CABAC_BITS__ALIGNED_ESCAPE_BITS : STATS__CABAC_BITS__ESCAPE_BITS ), cctx.width(), cctx.height(), cctx.compID() );
#endif

    RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_signs );

    //===== decode remaining absolute values =====
    if( escapeData )
    {
      RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_escs );

      bool      updateGoRiceStats = cctx.updGoRiceStats();
      unsigned  GoRicePar         = cctx.currGoRiceStats() >> 2;
      unsigned  MaxGoRicePar      = ( updateGoRiceStats ? std::numeric_limits<unsigned>::max() : 4 );
      int       baseLevel         = 3;
      for( int k = 0; k < numNonZero; k++ )
      {
        if( coeff[ sigBlkPos[ k ] ] == baseLevel )
        {
          if( !updateGoRiceStats || !cctx.isLast() )
          {
            GoRicePar = cctx.GoRicePar( scanPos[ k ], coeff );
          }

          int remAbs    = m_BinDecoder.decodeRemAbsEP( GoRicePar, cctx.extPrec(), cctx.maxLog2TrDRange(), true );
          coeff[ sigBlkPos[ k ] ] = baseLevel + remAbs;

          // update rice parameter
          if( coeff[ sigBlkPos[ k ] ] > ( 3 << GoRicePar ) && GoRicePar < MaxGoRicePar )
          {
            GoRicePar++;
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
        else if( baseLevel == 3 && coeff[ sigBlkPos[ k ] ] > 1 )
        {
          baseLevel = 2;
        }
      }
    }

    //===== decode sign's =====
#if HEVC_USE_SIGN_HIDING
    const unsigned  numSigns    = ( cctx.hideSign( firstNZPos, lastNZPos ) ? numNonZero - 1 : numNonZero );
    unsigned        signPattern = m_BinDecoder.decodeBinsEP( numSigns ) << ( 32 - numSigns );
#else
    unsigned        signPattern = m_BinDecoder.decodeBinsEP( numNonZero ) << ( 32 - numNonZero );
#endif

    //===== set final coefficents =====
    int sumAbs = 0;
#if HEVC_USE_SIGN_HIDING
    for( unsigned k = 0; k < numSigns; k++ )
#else
    for( unsigned k = 0; k < numNonZero; k++ )
#endif
    {
      int AbsCoeff          = coeff[ sigBlkPos[ k ] ];
      sumAbs               += AbsCoeff;
      coeff[ sigBlkPos[k] ] = ( signPattern & ( 1u << 31 ) ? -AbsCoeff : AbsCoeff );
      signPattern         <<= 1;
    }
#if HEVC_USE_SIGN_HIDING
    if( numNonZero > numSigns )
    {
      int k                 = numSigns;
      int AbsCoeff          = coeff[ sigBlkPos[ k ] ];
      sumAbs               += AbsCoeff;
      coeff[ sigBlkPos[k] ] = ( sumAbs & 1 ? -AbsCoeff : AbsCoeff );
    }
#endif
    cctx.setEmtNumSigCoeff( numNonZero );
  }
  else
#endif
  {
    //===== decode significant_coeff_flag's =====
    RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_map );
    const int inferSigPos = ( cctx.isNotFirst() ? minSubPos : -1 );
    unsigned  numNonZero  = 0;
#if HEVC_USE_SIGN_HIDING
    int       firstNZPos  = maxSbbSize;
    int       lastNZPos   = -1;
#endif
    int       sigBlkPos   [ 1 << MLS_CG_SIZE ];
    if( isLast )
    {
#if HEVC_USE_SIGN_HIDING
      firstNZPos                = nextSigPos;
      lastNZPos                 = nextSigPos;
#endif
      sigBlkPos[ numNonZero++ ] = cctx.blockPos( nextSigPos-- );
    }
    for( ; nextSigPos >= minSubPos; nextSigPos-- )
    {
      unsigned sigFlag = ( !numNonZero && nextSigPos == inferSigPos );
      if( !sigFlag )
      {
        sigFlag = m_BinDecoder.decodeBin( cctx.sigCtxId( nextSigPos ) );
      }
      if( sigFlag )
      {
        sigBlkPos [ numNonZero++ ]  = cctx.blockPos( nextSigPos );
#if HEVC_USE_SIGN_HIDING
        firstNZPos                  = nextSigPos;
        lastNZPos                   = std::max<int>( lastNZPos, nextSigPos );
#endif
      }
    }

    RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_gt1 );

    //===== decode abs_greater1_flag's =====
    int             absCoeff    [ 1 << MLS_CG_SIZE ];
    const unsigned  numGt1Flags = std::min<unsigned>( numNonZero, C1FLAG_NUMBER );
    int             gt2FlagIdx  = maxSbbSize;
    bool            escapeData  = false;
    uint16_t        ctxGt1Id    = 1;
    for( unsigned k = 0; k < numGt1Flags; k++ )
    {
      if( m_BinDecoder.decodeBin( cctx.greater1CtxId( ctxGt1Id ) ) )
      {
        absCoeff[ k ] = 2;
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
        absCoeff[ k ] = 1;
        if( ctxGt1Id && ctxGt1Id < 3 )
        {
          ctxGt1Id++;
        }
      }
    }
    for( unsigned k = numGt1Flags; k < numNonZero; k++ )
    {
      absCoeff[ k ] = 1;
      escapeData    = true;
    }
    cctx.setGt2Flag( ctxGt1Id == 0 );

    RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_gt2 );

    //===== decode abs_greater2_flag =====
    if( gt2FlagIdx < maxSbbSize )
    {
      if( m_BinDecoder.decodeBin( cctx.greater2CtxId() ) )
      {
        absCoeff[ gt2FlagIdx ]++;
        escapeData = true;
      }
    }

    //===== align data =====
    if( escapeData && cctx.alignFlag() )
    {
      m_BinDecoder.align();
    }

  #if RExt__DECODER_DEBUG_BIT_STATISTICS
    const bool alignGroup = escapeData && cctx.alignFlag();
    CodingStatisticsClassType ctype_signs( ( alignGroup ? STATS__CABAC_BITS__ALIGNED_SIGN_BIT    : STATS__CABAC_BITS__SIGN_BIT    ), cctx.width(), cctx.height(), cctx.compID() );
    CodingStatisticsClassType ctype_escs ( ( alignGroup ? STATS__CABAC_BITS__ALIGNED_ESCAPE_BITS : STATS__CABAC_BITS__ESCAPE_BITS ), cctx.width(), cctx.height(), cctx.compID() );
  #endif

    RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_signs );

    //===== decode sign's =====
#if HEVC_USE_SIGN_HIDING
    const unsigned  numSigns    = ( cctx.hideSign( firstNZPos, lastNZPos ) ? numNonZero - 1 : numNonZero );
    unsigned        signPattern = m_BinDecoder.decodeBinsEP( numSigns ) << ( 32 - numSigns );
#else
    unsigned        signPattern = m_BinDecoder.decodeBinsEP( numNonZero ) << ( 32 - numNonZero );
#endif

    //===== decode remaining absolute values =====
    if( escapeData )
    {
      RExt__DECODER_DEBUG_BIT_STATISTICS_SET( ctype_escs );

      bool      updateGoRiceStats = cctx.updGoRiceStats();
      unsigned  GoRicePar         = cctx.currGoRiceStats() >> 2;
      unsigned  MaxGoRicePar      = ( updateGoRiceStats ? std::numeric_limits<unsigned>::max() : 4 );
      int       baseLevel         = 3;
      for( int k = 0; k < numNonZero; k++ )
      {
        if( absCoeff[ k ] == baseLevel )
        {
          int remAbs    = m_BinDecoder.decodeRemAbsEP( GoRicePar, cctx.extPrec(), cctx.maxLog2TrDRange() );
          absCoeff[ k ] = baseLevel + remAbs;

          // update rice parameter
          if( absCoeff[ k ] > ( 3 << GoRicePar ) && GoRicePar < MaxGoRicePar )
          {
            GoRicePar++;
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

    //===== set final coefficents =====
    int sumAbs = 0;
#if HEVC_USE_SIGN_HIDING
    for( unsigned k = 0; k < numSigns; k++ )
#else
    for( unsigned k = 0; k < numNonZero; k++ )
#endif
    {
      int AbsCoeff          = absCoeff[k];
      sumAbs               += AbsCoeff;
      coeff[ sigBlkPos[k] ] = ( signPattern & ( 1u << 31 ) ? -AbsCoeff : AbsCoeff );
      signPattern         <<= 1;
    }
#if HEVC_USE_SIGN_HIDING
    if( numNonZero > numSigns )
    {
      int k                 = numSigns;
      int AbsCoeff          = absCoeff[k];
      sumAbs               += AbsCoeff;
      coeff[ sigBlkPos[k] ] = ( sumAbs & 1 ? -AbsCoeff : AbsCoeff );
    }
#endif
#if JEM_TOOLS || JVET_K1000_SIMPLIFIED_EMT
    cctx.setEmtNumSigCoeff( numNonZero );
#endif
  }
}
#endif





//================================================================================
//  clause 7.3.8.12
//--------------------------------------------------------------------------------
//    void  cross_comp_pred( tu, compID )
//================================================================================

void CABACReader::cross_comp_pred( TransformUnit& tu, ComponentID compID )
{
  RExt__DECODER_DEBUG_BIT_STATISTICS_CREATE_SET_SIZE2(STATS__CABAC_BITS__CROSS_COMPONENT_PREDICTION, tu.blocks[compID], compID);

  signed char alpha   = 0;
  unsigned    ctxBase = ( compID == COMPONENT_Cr ? 5 : 0 );
  unsigned    symbol  = m_BinDecoder.decodeBin( Ctx::CrossCompPred(ctxBase) );
  if( symbol )
  {
    // Cross-component prediction alpha is non-zero.
    symbol = m_BinDecoder.decodeBin( Ctx::CrossCompPred(ctxBase+1) );
    if( symbol )
    {
      // alpha is 2 (symbol=1), 4(symbol=2) or 8(symbol=3).
      // Read up to two more bits
      symbol += unary_max_symbol( Ctx::CrossCompPred(ctxBase+2), Ctx::CrossCompPred(ctxBase+3), 2 );
    }
    alpha = ( 1 << symbol );
    if( m_BinDecoder.decodeBin( Ctx::CrossCompPred(ctxBase+4) ) )
    {
      alpha = -alpha;
    }
  }
  DTRACE( g_trace_ctx, D_SYNTAX, "cross_comp_pred() etype=%d pos=(%d,%d) alpha=%d\n", compID, tu.blocks[compID].x, tu.blocks[compID].y, tu.compAlpha[compID] );
  tu.compAlpha[compID] = alpha;
}



//================================================================================
//  helper functions
//--------------------------------------------------------------------------------
//    unsigned  unary_max_symbol ( ctxId0, ctxId1, maxSymbol )
//    unsigned  unary_max_eqprob (                 maxSymbol )
//    unsigned  exp_golomb_eqprob( count )
//================================================================================

unsigned CABACReader::unary_max_symbol( unsigned ctxId0, unsigned ctxIdN, unsigned maxSymbol  )
{
  unsigned onesRead = 0;
  while( onesRead < maxSymbol && m_BinDecoder.decodeBin( onesRead == 0 ? ctxId0 : ctxIdN ) == 1 )
  {
    ++onesRead;
  }
  return onesRead;
}


unsigned CABACReader::unary_max_eqprob( unsigned maxSymbol )
{
  for( unsigned k = 0; k < maxSymbol; k++ )
  {
    if( !m_BinDecoder.decodeBinEP() )
    {
      return k;
    }
  }
  return maxSymbol;
}


unsigned CABACReader::exp_golomb_eqprob( unsigned count )
{
  unsigned symbol = 0;
  unsigned bit    = 1;
  while( bit )
  {
    bit     = m_BinDecoder.decodeBinEP( );
    symbol += bit << count++;
  }
  if( --count )
  {
    symbol += m_BinDecoder.decodeBinsEP( count );
  }
  return symbol;
}


unsigned CABACReader::decode_sparse_dt( DecisionTree& dt )
{
  dt.reduce();

  unsigned depth  = dt.dtt.depth;
  unsigned offset = 0;

  while( dt.dtt.hasSub[offset] )
  {
    CHECKD( depth == 0, "Depth is '0' for a decision node in a decision tree" );

    const unsigned posRight = offset + 1;
    const unsigned posLeft  = offset + ( 1u << depth );

    bool isLeft = true;

    if( dt.isAvail[posRight] && dt.isAvail[posLeft] )
    {
      // encode the decision as both sub-paths are available
      const unsigned ctxId = dt.ctxId[offset];

      if( ctxId > 0 )
      {
        DTRACE( g_trace_ctx, D_DECISIONTREE, "Decision coding using context %d\n", ctxId - 1 );
        isLeft = m_BinDecoder.decodeBin( ctxId - 1 ) == 0;
      }
      else
      {
        DTRACE( g_trace_ctx, D_DECISIONTREE, "Decision coding as an EP bin\n" );
        isLeft = m_BinDecoder.decodeBinEP() == 0;
      }
    }
    else if( dt.isAvail[posRight] )
    {
      isLeft = false;
    }

    DTRACE( g_trace_ctx, D_DECISIONTREE, "Following the tree to the %s sub-node\n", isLeft ? "left" : "right" );

    offset = isLeft ? posLeft : posRight;
    depth--;
  }

  CHECKD( dt.isAvail[offset] == false, "The decoded element is not available" );
  DTRACE( g_trace_ctx, D_DECISIONTREE, "Found an end-node of the tree\n" );
  return dt.dtt.ids[offset];
}

