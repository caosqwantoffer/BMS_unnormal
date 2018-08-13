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

/** \file     EncGOP.h
    \brief    GOP encoder class (header)
*/

#ifndef __ENCGOP__
#define __ENCGOP__

#include <list>

#include <stdlib.h>

#include "CommonLib/Picture.h"
#include "CommonLib/LoopFilter.h"
#include "CommonLib/NAL.h"
#include "EncSampleAdaptiveOffset.h"
#if JEM_TOOLS || JVET_K0371_ALF
#include "EncAdaptiveLoopFilter.h"
#endif
#include "EncSlice.h"
#include "VLCWriter.h"
#include "CABACWriter.h"
#include "SEIwrite.h"
#include "SEIEncoder.h"
#if EXTENSION_360_VIDEO
#include "AppEncHelper360/TExt360EncGop.h"
#endif

#include "Analyze.h"
#include "RateCtrl.h"
#include <vector>

//! \ingroup EncoderLib
//! \{

class EncLib;

// ====================================================================================================================
// Class definition
// ====================================================================================================================
class AUWriterIf
{
public:
  virtual void outputAU( const AccessUnit& ) = 0;
};


class EncGOP
{
  class DUData
  {
  public:
    DUData()
    :accumBitsDU(0)
    ,accumNalsDU(0) {};

    int accumBitsDU;
    int accumNalsDU;
  };

private:

  Analyze                 m_gcAnalyzeAll;
  Analyze                 m_gcAnalyzeI;
  Analyze                 m_gcAnalyzeP;
  Analyze                 m_gcAnalyzeB;
#if WCG_WPSNR
  Analyze                 m_gcAnalyzeWPSNR;
#endif
  Analyze                 m_gcAnalyzeAll_in;
#if EXTENSION_360_VIDEO
  TExt360EncGop           m_ext360;
public:
  TExt360EncGop &getExt360Data() { return m_ext360; }
private:
#endif

  //  Data
  bool                    m_bLongtermTestPictureHasBeenCoded;
  bool                    m_bLongtermTestPictureHasBeenCoded2;
  UInt                    m_numLongTermRefPicSPS;
  UInt                    m_ltRefPicPocLsbSps[MAX_NUM_LONG_TERM_REF_PICS];
  bool                    m_ltRefPicUsedByCurrPicFlag[MAX_NUM_LONG_TERM_REF_PICS];
  int                     m_iLastIDR;
  int                     m_iGopSize;
  int                     m_iNumPicCoded;
  bool                    m_bFirst;
  int                     m_iLastRecoveryPicPOC;
  int                     m_lastRasPoc;

  //  Access channel
  EncLib*                 m_pcEncLib;
  EncCfg*                 m_pcCfg;
  EncSlice*               m_pcSliceEncoder;
  PicList*                m_pcListPic;

  HLSWriter*              m_HLSWriter;
  LoopFilter*             m_pcLoopFilter;

  SEIWriter               m_seiWriter;

  //--Adaptive Loop filter
  EncSampleAdaptiveOffset*  m_pcSAO;
#if JEM_TOOLS || JVET_K0371_ALF
  EncAdaptiveLoopFilter*    m_pcALF;
#endif
  RateCtrl*                 m_pcRateCtrl;
  // indicate sequence first
  bool                    m_bSeqFirst;

  // clean decoding refresh
  bool                    m_bRefreshPending;
  int                     m_pocCRA;
  NalUnitType             m_associatedIRAPType;
  int                     m_associatedIRAPPOC;

  std::vector<int>        m_vRVM_RP;
  UInt                    m_lastBPSEI;
  UInt                    m_totalCoded;
  bool                    m_bufferingPeriodSEIPresentInAU;
  SEIEncoder              m_seiEncoder;
#if W0038_DB_OPT
  PelStorage*             m_pcDeblockingTempPicYuv;
  int                     m_DBParam[MAX_ENCODER_DEBLOCKING_QUALITY_LAYERS][4];   //[layer_id][0: available; 1: bDBDisabled; 2: Beta Offset Div2; 3: Tc Offset Div2;]
#endif

  // members needed for adaptive max BT size
  UInt                    m_uiBlkSize[10];
  UInt                    m_uiNumBlk[10];
  UInt                    m_uiPrevISlicePOC;
  bool                    m_bInitAMaxBT;

  AUWriterIf*             m_AUWriterIf;

public:
  EncGOP();
  virtual ~EncGOP();

  void  create      ();
  void  destroy     ();

  void  init        ( EncLib* pcEncLib );
  void  compressGOP ( int iPOCLast, int iNumPicRcvd, PicList& rcListPic, std::list<PelUnitBuf*>& rcListPicYuvRec,
                      bool isField, bool isTff, const InputColourSpaceConversion snr_conversion, const bool printFrameMSE );
  void  xAttachSliceDataToNalUnit (OutputNALUnit& rNalu, OutputBitstream* pcBitstreamRedirect);


  int   getGOPSize()          { return  m_iGopSize;  }

  PicList*   getListPic()      { return m_pcListPic; }

  void  printOutSummary      ( UInt uiNumAllPicCoded, bool isField, const bool printMSEBasedSNR, const bool printSequenceMSE, const BitDepths &bitDepths );
#if W0038_DB_OPT
  uint64_t  preLoopFilterPicAndCalcDist( Picture* pcPic );
#endif
  EncSlice*  getSliceEncoder()   { return m_pcSliceEncoder; }
  NalUnitType getNalUnitType( int pocCurr, int lastIdr, bool isField );
  void arrangeLongtermPicturesInRPS(Slice *, PicList& );

#if EXTENSION_360_VIDEO
  Analyze& getAnalyzeAllData() { return m_gcAnalyzeAll; }
  Analyze& getAnalyzeIData() { return m_gcAnalyzeI; }
  Analyze& getAnalyzePData() { return m_gcAnalyzeP; }
  Analyze& getAnalyzeBData() { return m_gcAnalyzeB; }
#endif

protected:
  RateCtrl* getRateCtrl()       { return m_pcRateCtrl;  }

protected:

  void  xInitGOP          ( int iPOCLast, int iNumPicRcvd, bool isField );
  void  xGetBuffer        ( PicList& rcListPic, std::list<PelUnitBuf*>& rcListPicYuvRecOut,
                            int iNumPicRcvd, int iTimeOffset, Picture*& rpcPic, int pocCurr, bool isField );

  void  xCalculateAddPSNRs         ( const bool isField, const bool isFieldTopFieldFirst, const int iGOPid, Picture* pcPic, const AccessUnit&accessUnit, PicList &rcListPic, int64_t dEncTime, const InputColourSpaceConversion snr_conversion, const bool printFrameMSE, double* PSNR_Y );
  void  xCalculateAddPSNR          ( Picture* pcPic, PelUnitBuf cPicD, const AccessUnit&, double dEncTime, const InputColourSpaceConversion snr_conversion, const bool printFrameMSE, double* PSNR_Y );
  void  xCalculateInterlacedAddPSNR( Picture* pcPicOrgFirstField, Picture* pcPicOrgSecondField,
                                     PelUnitBuf cPicRecFirstField, PelUnitBuf cPicRecSecondField,
                                     const InputColourSpaceConversion snr_conversion, const bool printFrameMSE, double* PSNR_Y );

  uint64_t xFindDistortionPlane(const CPelBuf& pic0, const CPelBuf& pic1, const UInt rshift
#if ENABLE_QPA
                            , const UInt chromaShift = 0
#endif
                             );
#if WCG_WPSNR
  double xFindDistortionPlaneWPSNR(const CPelBuf& pic0, const CPelBuf& pic1, const UInt rshift, const CPelBuf& picLuma0, ComponentID compID, const ChromaFormat chfmt );
#endif
  double xCalculateRVM();

  void xUpdateRasInit(Slice* slice);

  void xWriteAccessUnitDelimiter (AccessUnit &accessUnit, Slice *slice);

  void xCreateIRAPLeadingSEIMessages (SEIMessages& seiMessages, const SPS *sps, const PPS *pps);
  void xCreatePerPictureSEIMessages (int picInGOP, SEIMessages& seiMessages, SEIMessages& nestedSeiMessages, Slice *slice);
  void xCreatePictureTimingSEI  (int IRAPGOPid, SEIMessages& seiMessages, SEIMessages& nestedSeiMessages, SEIMessages& duInfoSeiMessages, Slice *slice, bool isField, std::deque<DUData> &duData);
  void xUpdateDuData(AccessUnit &testAU, std::deque<DUData> &duData);
  void xUpdateTimingSEI(SEIPictureTiming *pictureTimingSEI, std::deque<DUData> &duData, const SPS *sps);
  void xUpdateDuInfoSEI(SEIMessages &duInfoSeiMessages, SEIPictureTiming *pictureTimingSEI);

  void xCreateScalableNestingSEI (SEIMessages& seiMessages, SEIMessages& nestedSeiMessages);
  void xWriteSEI (NalUnitType naluType, SEIMessages& seiMessages, AccessUnit &accessUnit, AccessUnit::iterator &auPos, int temporalId, const SPS *sps);
  void xWriteSEISeparately (NalUnitType naluType, SEIMessages& seiMessages, AccessUnit &accessUnit, AccessUnit::iterator &auPos, int temporalId, const SPS *sps);
  void xClearSEIs(SEIMessages& seiMessages, bool deleteMessages);
  void xWriteLeadingSEIOrdered (SEIMessages& seiMessages, SEIMessages& duInfoSeiMessages, AccessUnit &accessUnit, int temporalId, const SPS *sps, bool testWrite);
  void xWriteLeadingSEIMessages  (SEIMessages& seiMessages, SEIMessages& duInfoSeiMessages, AccessUnit &accessUnit, int temporalId, const SPS *sps, std::deque<DUData> &duData);
  void xWriteTrailingSEIMessages (SEIMessages& seiMessages, AccessUnit &accessUnit, int temporalId, const SPS *sps);
  void xWriteDuSEIMessages       (SEIMessages& duInfoSeiMessages, AccessUnit &accessUnit, int temporalId, const SPS *sps, std::deque<DUData> &duData);

#if HEVC_VPS
  int xWriteVPS (AccessUnit &accessUnit, const VPS *vps);
#endif
  int xWriteSPS (AccessUnit &accessUnit, const SPS *sps);
  int xWritePPS (AccessUnit &accessUnit, const PPS *pps);
  int xWriteParameterSets (AccessUnit &accessUnit, Slice *slice, const bool bSeqFirst);

  void applyDeblockingFilterMetric( Picture* pcPic, UInt uiNumSlices );
#if W0038_DB_OPT
  void applyDeblockingFilterParameterSelection( Picture* pcPic, const UInt numSlices, const int gopID );
#endif
};// END CLASS DEFINITION EncGOP

//! \}

#endif // __ENCGOP__

