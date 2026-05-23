// Copyright 2010 Google Inc. All Rights Reserved.
//
// This code is licensed under the same terms as WebM:
//  Software License Agreement:  http://www.webmproject.org/license/software/
//  Additional IP Rights Grant:  http://www.webmproject.org/license/additional/
// -----------------------------------------------------------------------------
//
//  Common types
//
// SAS/C / AmigaOS m68k variant.  Upstream's types.h pulls in
// <inttypes.h>, which SAS/C does not ship.  This file substitutes
// hand-rolled integer typedefs that match the m68k ABI.  SAS/C 6.58
// has no dependable native 64-bit integer operators, so the Amiga
// decoder build patches libwebp's 64-bit-dependent hot spots and maps
// int64_t/uint64_t to native 32-bit long types.
//
// Author: Skal (pascal.massimino@gmail.com)
// Amiga changes: amigazen project, 2026.

#ifndef WEBP_WEBP_TYPES_H_
#define WEBP_WEBP_TYPES_H_

#include <stddef.h>  /* for size_t */

#if defined(__SASC) || defined(AMIGA) || defined(_AMIGA)
/* SAS/C 6.58 on AmigaOS m68k has no <inttypes.h> and does not provide
 * dependable C99 `long long' integer operators.  The decoder-only
 * Amiga build patches the few libwebp sites that really wanted 64-bit
 * shifts/multiplies, so these typedefs are kept at native 32-bit size. */
typedef signed   char       int8_t;
typedef unsigned char       uint8_t;
typedef signed   short      int16_t;
typedef unsigned short      uint16_t;
typedef signed   int        int32_t;
typedef unsigned int        uint32_t;
typedef signed   long       int64_t;
typedef unsigned long       uint64_t;
typedef unsigned long       uintptr_t;
/* SAS/C accepts __inline as a keyword, but the upstream `static
 * WEBP_INLINE ...' functions in decode.h are short - it is fine to
 * compile them as plain `static' functions and let the optimiser
 * inline them where useful. */
#define WEBP_INLINE
#elif !defined(_MSC_VER)
#include <inttypes.h>
#ifdef __STRICT_ANSI__
#define WEBP_INLINE
#else
#define WEBP_INLINE inline
#endif
#else
typedef signed   char int8_t;
typedef unsigned char uint8_t;
typedef signed   short int16_t;
typedef unsigned short uint16_t;
typedef signed   int int32_t;
typedef unsigned int uint32_t;
typedef unsigned long long int uint64_t;
typedef long long int int64_t;
#define WEBP_INLINE __forceinline
#endif

#ifndef WEBP_EXTERN
/* This explicitly marks library functions and allows for changing
 * the signature for e.g., Windows DLL builds. */
#define WEBP_EXTERN(type) extern type
#endif  /* WEBP_EXTERN */

/* Macro to check ABI compatibility (same major revision number) */
#define WEBP_ABI_IS_INCOMPATIBLE(a, b) (((a) >> 8) != ((b) >> 8))

#endif  /* WEBP_WEBP_TYPES_H_ */
