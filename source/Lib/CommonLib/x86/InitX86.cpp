/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2017, ITU/ISO/IEC
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

/*
 * \ingroup CommonLib
 * \file    InitX86.cpp
 * \brief   Initialize encoder SIMD functions.
 */


#include "CommonLib/CommonDef.h"
#include "CommonLib/InterpolationFilter.h"
#include "CommonLib/TrQuant.h"
#include "CommonLib/RdCost.h"
#include "CommonLib/Buffer.h"

#if JVET_K0367_AFFINE_FIX_POINT
#include "CommonLib/AffineGradientSearch.h"
#endif

#ifdef TARGET_SIMD_X86


#if ENABLE_SIMD_OPT_MCIF
Void InterpolationFilter::initInterpolationFilterX86( /*Int iBitDepthY, Int iBitDepthC*/ )
{
  auto vext = read_x86_extension_flags();
  switch (vext){
  case AVX512:
  case AVX2:
    _initInterpolationFilterX86<AVX2>(/*iBitDepthY, iBitDepthC*/);
    break;
  case AVX:
  case SSE42:
  case SSE41:
    _initInterpolationFilterX86<SSE41>(/*iBitDepthY, iBitDepthC*/);
    break;
  default:
    break;
  }
}
#endif

#if ENABLE_SIMD_OPT_BUFFER
Void PelBufferOps::initPelBufOpsX86()
{
  auto vext = read_x86_extension_flags();
  switch (vext){
    case AVX512:
    case AVX2:
      _initPelBufOpsX86<AVX2>();
      break;
    case AVX:
    case SSE42:
    case SSE41:
      _initPelBufOpsX86<SSE41>();
      break;
    default:
      break;
  }
}
#endif




#if ENABLE_SIMD_OPT_DIST
Void RdCost::initRdCostX86()
{
  auto vext = read_x86_extension_flags();
  switch (vext){
    case AVX512:
    case AVX2:
      _initRdCostX86<AVX2>();
      break;
    case AVX:
    case SSE42:
    case SSE41:
      _initRdCostX86<SSE41>();
      break;
    default:
      break;
  }
}
#endif

#if ENABLE_SIMD_OPT_AFFINE_ME
void AffineGradientSearch::initAffineGradientSearchX86()
{
  auto vext = read_x86_extension_flags();
  switch ( vext ) {
  case AVX512:
  case AVX2:
  case AVX:
  case SSE42:
  case SSE41:
    _initAffineGradientSearchX86<SSE41>();
    break;
  default:
    break;
  }
}
#endif

#endif

