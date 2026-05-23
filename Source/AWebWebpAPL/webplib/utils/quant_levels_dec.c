// Copyright 2011 Google Inc. All Rights Reserved.
//
// This code is licensed under the same terms as WebM:
//  Software License Agreement:  http://www.webmproject.org/license/software/
//  Additional IP Rights Grant:  http://www.webmproject.org/license/additional/
// -----------------------------------------------------------------------------
//
// Decoder-only quant_levels helpers, extracted from upstream
// quant_levels.c.  The full upstream file also contains the encoder's
// QuantizeLevels(); we omit it here because the AWeb WebP plugin only
// decodes images and we do not want to link in encoder-side code.
//
// DequantizeLevels() is a stub in upstream 0.2.0 (returns 1, no actual
// gradient smoothing implemented) but alpha.c still calls it.  Keeping
// the implementation byte-for-byte identical preserves behaviour.
//
// Author: Skal (pascal.massimino@gmail.com)
// Amiga changes: amigazen project, 2026.

#include "quant_levels.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

int DequantizeLevels(uint8_t* const data, int width, int height) {
  if (data == NULL || width <= 0 || height <= 0) return 0;
  /* TODO(skal): implement gradient smoothing. */
  (void)data;
  (void)width;
  (void)height;
  return 1;
}

#if defined(__cplusplus) || defined(c_plusplus)
}    // extern "C"
#endif
