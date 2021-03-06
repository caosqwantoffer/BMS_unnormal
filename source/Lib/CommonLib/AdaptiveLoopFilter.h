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

/** \file     AdaptiveLoopFilter.h
    \brief    adaptive loop filter class (header)
*/

#ifndef __ADAPTIVELOOPFILTER__
#define __ADAPTIVELOOPFILTER__

#include "CommonDef.h"

#if JVET_K0371_ALF
#include "Unit.h"

struct AlfClassifier
{
  AlfClassifier() {}
  AlfClassifier( uint8_t cIdx, uint8_t tIdx )
    : classIdx( cIdx ), transposeIdx( tIdx )
  {
  }

  uint8_t classIdx;
  uint8_t transposeIdx;
};

enum Direction
{
  HOR,
  VER,
  DIAG0,
  DIAG1,
  NUM_DIRECTIONS
};

class AdaptiveLoopFilter
{
public:
  static constexpr int   m_NUM_BITS = 10;
  static constexpr int   m_CLASSIFICATION_BLK_SIZE = 32;  //non-normative, local buffer size

  AdaptiveLoopFilter();
  virtual ~AdaptiveLoopFilter() {}

  void ALFProcess( CodingStructure& cs, AlfSliceParam& alfSliceParam );
  void reconstructCoeff( AlfSliceParam& alfSliceParam, ChannelType channel, const bool bRedo = false );
  void create( const int picWidth, const int picHeight, const ChromaFormat format, const int maxCUWidth, const int maxCUHeight, const int maxCUDepth, const int inputBitDepth[MAX_NUM_CHANNEL_TYPE] );
  void destroy();
  static void deriveClassificationBlk( AlfClassifier** classifier, int** laplacian[NUM_DIRECTIONS], const CPelBuf& srcLuma, const Area& blk, const int shift );
  void deriveClassification( AlfClassifier** classifier, const CPelBuf& srcLuma, const Area& blk );
  template<AlfFilterType filtType>
  static void filterBlk( AlfClassifier** classifier, const PelUnitBuf &recDst, const CPelUnitBuf& recSrc, const Area& blk, const ComponentID compId, short* filterSet, const ClpRng& clpRng );

  inline static int getMaxGolombIdx( AlfFilterType filterType )
  {
    return filterType == ALF_FILTER_5 ? 2 : 3;
  }

  void( *m_deriveClassificationBlk )( AlfClassifier** classifier, int** laplacian[NUM_DIRECTIONS], const CPelBuf& srcLuma, const Area& blk, const int shift );
  void( *m_filter5x5Blk )( AlfClassifier** classifier, const PelUnitBuf &recDst, const CPelUnitBuf& recSrc, const Area& blk, const ComponentID compId, short* filterSet, const ClpRng& clpRng );
  void( *m_filter7x7Blk )( AlfClassifier** classifier, const PelUnitBuf &recDst, const CPelUnitBuf& recSrc, const Area& blk, const ComponentID compId, short* filterSet, const ClpRng& clpRng );

#ifdef TARGET_SIMD_X86
  void initAdaptiveLoopFilterX86();
  template <X86_VEXT vext>
  void _initAdaptiveLoopFilterX86();
#endif

protected:
  std::vector<AlfFilterShape>  m_filterShapes[MAX_NUM_CHANNEL_TYPE];
  AlfClassifier**              m_classifier;
  short                        m_coeffFinal[MAX_NUM_ALF_CLASSES * MAX_NUM_ALF_LUMA_COEFF];
  int**                        m_laplacian[NUM_DIRECTIONS];
  uint8_t*                       m_ctuEnableFlag[MAX_NUM_COMPONENT];
  PelStorage                   m_tempBuf;
  int                          m_inputBitDepth[MAX_NUM_CHANNEL_TYPE];
  int                          m_picWidth;
  int                          m_picHeight;
  int                          m_maxCUWidth;
  int                          m_maxCUHeight;
  int                          m_maxCUDepth;
  int                          m_numCTUsInWidth;
  int                          m_numCTUsInHeight;
  int                          m_numCTUsInPic;
  ChromaFormat                 m_chromaFormat;
  ClpRngs                      m_clpRngs;
};
#elif JEM_TOOLS

#include "Picture.h"


const static int ALF_NUM_OF_CLASSES = 16;

void destroyMatrix_int(int **m2D);
void initMatrix_int(int ***m2D, int d1, int d2);

// ====================================================================================================================
// Class definition
// ====================================================================================================================

/// adaptive loop filter class
class AdaptiveLoopFilter
{
public:
  static const int m_ALF_MAX_NUM_COEF      = 42;                                    ///< maximum number of filter coefficients
  static const int m_ALF_MAX_NUM_COEF_C    = 14;                                    ///< number of filter taps for chroma
protected:
  static const int m_ALF_VAR_SIZE_H        = 4;
  static const int m_ALF_VAR_SIZE_W        = 4;

  static const int m_ALF_WIN_VERSIZE       = 32;
  static const int m_ALF_WIN_HORSIZE       = 32;

  static const int m_ALF_MAX_NUM_TAP       = 9;                                     ///< maximum number of filter taps (9x9)
  static const int m_ALF_MIN_NUM_TAP       = 5;                                     ///< minimum number of filter taps (5x5)
  static const int m_ALF_MAX_NUM_TAP_C     = 5;                                     ///< number of filter taps for chroma (5x5)

  static const int m_ALF_MIN_NUM_COEF      = 14;                                    ///< minimum number of filter coefficients

  static const int m_ALF_NUM_BIT_SHIFT     = 8;                                     ///< bit shift parameter for quantization of ALF param.
  static const int m_ALF_ROUND_OFFSET      = ( 1 << ( m_ALF_NUM_BIT_SHIFT - 1 ) );  ///< rounding offset for ALF quantization

  static const int m_VAR_SIZE              = 1;                                     ///< JCTVC-E323+E046

  static const int m_FILTER_LENGTH         = 9;

  static const int m_ALF_HM3_QC_CLIP_RANGE = 1024;
  static const int m_ALF_HM3_QC_CLIP_OFFSET= 384;

public:

  static const int m_NO_TEST_FILT          =  3;                                    ///< Filter supports (5/7/9)

  #define NO_VALS_LAGR                     5    //galf stuff
  #define NO_VALS_LAGR_SHIFT               3    //galf stuff
  static const int m_MAX_SQT_FILT_SYM_LENGTH = ((m_FILTER_LENGTH*m_FILTER_LENGTH) / 4 + 1);

#if GALF
  static const int m_NUM_BITS            = 10;
  static const int m_NO_VAR_BINS         = 25;
  static const int m_NO_FILTERS          = 25;
  static const int m_MAX_SQR_FILT_LENGTH = ((m_FILTER_LENGTH*m_FILTER_LENGTH) / 2 + 1);
  static const int m_SQR_FILT_LENGTH_9SYM = ((9 * 9) / 4 + 1);
  static const int m_SQR_FILT_LENGTH_7SYM = ((7 * 7) / 4 + 1);
  static const int m_SQR_FILT_LENGTH_5SYM = ((5 * 5) / 4 + 1);
#else
  static const int m_NUM_BITS              = 9;
  static const int m_NO_VAR_BINS           = 16;
  static const int m_NO_FILTERS            = 16;
  static const int m_MAX_SQR_FILT_LENGTH = ((m_FILTER_LENGTH*m_FILTER_LENGTH) / 2 + 2);
  static const int m_SQR_FILT_LENGTH_9SYM = ((9 * 9) / 4 + 2 - 1);
  static const int m_SQR_FILT_LENGTH_7SYM = ((7 * 7) / 4 + 2);
  static const int m_SQR_FILT_LENGTH_5SYM = ((5 * 5) / 4 + 2);
#endif
  static const int m_FilterTapsOfType[ALF_NUM_OF_FILTER_TYPES];

  static const int m_MAX_SCAN_VAL          = 11;
  static const int m_MAX_EXP_GOLOMB        = 16;

  // quantized filter coefficients
  static const int m_aiSymmetricMag9x9[41];                                         ///< quantization scaling factor for 9x9 filter
  static const int m_aiSymmetricMag7x7[25];                                         ///< quantization scaling factor for 7x7 filter
  static const int m_aiSymmetricMag5x5[13];                                         ///< quantization scaling factor for 5x5 filter
  static const int m_aiSymmetricMag9x7[32];                                         ///< quantization scaling factor for 9x7 filter

  // temporary picture buffer
  PelStorage   m_tmpRecExtBuf;                                                     ///< temporary picture buffer for extended reconstructed frame

public:
  static const int* m_pDepthIntTab[m_NO_TEST_FILT];

protected:
#if JVET_C0038_NO_PREV_FILTERS
  static const int m_ALFfilterCoeffFixed[m_NO_FILTERS*JVET_C0038_NO_PREV_FILTERS][21]; /// fixed filters used in ALF.
#endif
  static const int depthInt9x9Cut[21];
  static const int depthInt7x7Cut[14];
  static const int depthInt5x5Cut[8];
  static const int m_depthInt9x9Sym[21];
  static const int m_depthInt7x7Sym[14];
  static const int m_depthInt5x5Sym[8];
  // ------------------------------------------------------------------------------------------------------------------
  // For luma component
  // ------------------------------------------------------------------------------------------------------------------
#if GALF
  static const int m_pattern9x9Sym[41];
  static const int m_weights9x9Sym[22];
#else
  static const int m_pattern9x9Sym[39];
  static const int m_weights9x9Sym[21];
#endif
  static const int m_pattern9x9Sym_Quart[42];
  static const int m_pattern7x7Sym[25];
  static const int m_weights7x7Sym[14];
  static const int m_pattern7x7Sym_Quart[42];
  static const int m_pattern5x5Sym[13];
  static const int m_weights5x5Sym[8];
  static const int m_pattern5x5Sym_Quart[45];
  static const int m_pattern9x9Sym_9[39];
  static const int m_pattern9x9Sym_7[25];
  static const int m_pattern9x9Sym_5[13];

  static const int m_flTab[m_NO_TEST_FILT];
  static const int *m_patternTab[m_NO_TEST_FILT];
  static const int *m_patternMapTab[m_NO_TEST_FILT];
  static const int *m_weightsTab[m_NO_TEST_FILT];
  static const int m_sqrFiltLengthTab[m_NO_TEST_FILT];
  static const int m_mapTypeToNumOfTaps[m_NO_TEST_FILT];

  int       m_img_height,m_img_width;
  int       m_nInputBitDepth;
  int       m_nInternalBitDepth;
  int       m_nBitIncrement;
  int       m_nIBDIMax;
  ClpRngs   m_clpRngs;
  uint32_t      m_uiMaxTotalCUDepth;
  uint32_t      m_uiMaxCUWidth;
  uint32_t      m_uiNumCUsInFrame; //TODO rename

  Pel**     m_imgY_var;
  int**     m_imgY_temp;

  int**     m_imgY_ver;
  int**     m_imgY_hor;
  int**     m_imgY_dig0;
  int**     m_imgY_dig1;
  int **    m_filterCoeffFinal;
  Pel**     m_varImgMethods;

  int**     m_filterCoeffSym;
  int**     m_filterCoeffPrevSelected;
  int16_t**   m_filterCoeffShort;
  int**     m_filterCoeffTmp;
  int**     m_filterCoeffSymTmp;

  bool      m_isGALF;
  bool      m_wasCreated;
  bool      m_isDec;

public:


  bool      m_galf;
  unsigned  m_storedAlfParaNum[E0104_ALF_MAX_TEMPLAYERID];
  ALFParam  m_acStoredAlfPara[E0104_ALF_MAX_TEMPLAYERID][C806_ALF_TEMPPRED_NUM];
protected:
  /// ALF for luma component
  void xALFLuma( CodingStructure& cs, ALFParam* pcAlfParam, PelUnitBuf& recSrcExt, PelUnitBuf& recDst );


  void reconstructFilterCoeffs(ALFParam* pcAlfParam,int **pfilterCoeffSym );
  void getCurrentFilter(int **filterCoeffSym,ALFParam* pcAlfParam);
  void xFilterFrame  (PelUnitBuf& recSrcExt, PelUnitBuf& recDst, AlfFilterType filtType
    );
  void xFilterBlkGalf(PelUnitBuf &recDst, const CPelUnitBuf& recSrcExt, const Area& blk, AlfFilterType filtType, const ComponentID compId);
  void xFilterBlkAlf (PelBuf &recDst, const CPelBuf& recSrc, const Area& blk, AlfFilterType filtType);

  void xClassify                 (Pel** classes, const CPelBuf& recSrcBuf, int pad_size, int fl);
  void xClassifyByGeoLaplacian   (Pel** classes, const CPelBuf& srcLumaBuf, int pad_size, int fl, const Area& blk);
  void xClassifyByGeoLaplacianBlk(Pel** classes, const CPelBuf& srcLumaBuf, int pad_size, int fl, const Area& blk);
  int  selectTransposeVarInd     (int varInd, int *transpose);

  void xClassifyByLaplacian      (Pel** classes, const CPelBuf& srcLumaBuf, int pad_size, int fl, const Area& blk);
  void xClassifyByLaplacianBlk   (Pel** classes, const CPelBuf& srcLumaBuf, int pad_size, int fl, const Area& blk);
  void xDecodeFilter( ALFParam* pcAlfParam );

  // memory allocation
  void destroyMatrix_short(short **m2D);
  void initMatrix_short(short ***m2D, int d1, int d2);
  void destroyMatrix_Pel(Pel **m2D);
  void destroyMatrix_int(int **m2D);
  void initMatrix_int(int ***m2D, int d1, int d2);
  void initMatrix_Pel(Pel ***m2D, int d1, int d2);
  void destroyMatrix4D_double(double ****m4D, int d1, int d2);
  void destroyMatrix3D_double(double ***m3D, int d1);
  void destroyMatrix_double(double **m2D);
  void initMatrix4D_double(double *****m4D, int d1, int d2, int d3, int d4);
  void initMatrix3D_double(double ****m3D, int d1, int d2, int d3);
  void initMatrix_double(double ***m2D, int d1, int d2);
  void free_mem2Dpel(Pel **array2D);
  void get_mem2Dpel(Pel ***array2D, int rows, int columns);
  void no_mem_exit(const char *where);
  void xError(const char *text, int code);
  void calcVar(Pel **imgY_var, Pel *imgY_pad, int pad_size, int fl, int img_height, int img_width, int img_stride, int start_width = 0 , int start_height = 0 );
  void xCalcVar(Pel **imgY_var, Pel *imgY_pad, int pad_size, int fl, int img_height, int img_width, int img_stride, int start_width , int start_height );

  void xCUAdaptive( CodingStructure& cs, const PelUnitBuf &recExtBuf, PelUnitBuf &recBuf, ALFParam* pcAlfParam
    );

  /// ALF for chroma component
  void xALFChroma   ( ALFParam* pcAlfParam,const PelUnitBuf& recExtBuf, PelUnitBuf& recUnitBuf );
  void xFrameChromaGalf(ALFParam* pcAlfParam, const PelUnitBuf& recExtBuf, PelUnitBuf& recUnitBuf, ComponentID compID);
  void xFrameChromaAlf (ALFParam* pcAlfParam, const PelUnitBuf& recExtBuf, PelUnitBuf& recUnitBuf, ComponentID compID );

public:
  AdaptiveLoopFilter();
  virtual ~AdaptiveLoopFilter() {}

  // initialize & destroy temporary buffer
  void create( const int iPicWidth, int iPicHeight, const ChromaFormat chromaFormatIDC, const int uiMaxCUWidth, const uint32_t uiMaxCUHeight, const uint32_t uiMaxCUDepth, const int nInputBitDepth, const int nInternalBitDepth, const int numberOfCTUs );
  void destroy ();

  void ALFProcess     ( CodingStructure& cs, ALFParam* pcAlfParam
                      ); ///< interface function for ALF process

  // alloc & free & set functions //TODO move to ALFParam class
  void allocALFParam  ( ALFParam* pAlfParam );
  void freeALFParam   ( ALFParam* pAlfParam );
  void copyALFParam   ( ALFParam* pDesAlfParam, ALFParam* pSrcAlfParam, bool max_depth_copy = true );

  void storeALFParam  ( ALFParam* pAlfParam, bool isISlice, unsigned tLayer, unsigned tLayerMax );
  void loadALFParam   ( ALFParam* pAlfParam, unsigned idx, unsigned tLayer );

  void resetALFParam  ( ALFParam* pDesAlfParam);
  void resetALFPredParam(ALFParam *pAlfParam, bool bIntra);
  void setNumCUsInFrame(uint32_t uiNumCUsInFrame);

  // predict filter coefficients
  void predictALFCoeffChroma  ( ALFParam* pAlfParam );                  ///< prediction of chroma ALF coefficients
  void initVarForChroma(ALFParam* pcAlfParam, bool bUpdatedDCCoef);

  void refreshAlfTempPred();

  static int ALFTapHToTapV     ( int tapH );
  static int ALFTapHToNumCoeff ( int tapH );
  static int ALFFlHToFlV       ( int flH  );
};
#endif

#endif
