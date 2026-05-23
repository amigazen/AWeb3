libwebp 0.2.0 (decoder only) for the AWeb WebP plugin
======================================================

This directory builds a static SAS/C link library "libwebp.lib" that
contains the decoder portion of Google's libwebp v0.2.0.  The AWeb
WebP plugin (awebwebp.awebplugin) links against this library to
decode WebP images in the browser.

This is a vendored copy of the upstream 0.2.0 source tree with seven
small modifications, listed below.  No further patching is needed -
just run "smake" in this directory and libwebp.lib will be built.

Files in this directory
-----------------------

   webp/                  Public headers
      decode.h            Decoder API.  Verbatim from upstream 0.2.0.
      types.h             SAS/C-friendly replacement for the upstream
                          header (see PATCHES below).
      format_constants.h  Verbatim from upstream 0.2.0.

   dec/                   Decoder sources from upstream
                          src/dec/ (alpha.c, buffer.c, frame.c, idec.c,
                          io.c, layer.c, quant.c, tree.c, vp8.c,
                          vp8l.c, webp.c plus the private .h files).
                          frame.c, vp8.c and webp.c have the small
                          SAS/C patches noted below.

   dsp/                   Scalar DSP sources, verbatim from upstream
                          src/dsp/ minus dec_neon.c, dec_sse2.c,
                          upsampling_sse2.c (SIMD intrinsics not
                          available under SAS/C on m68k) and minus
                          enc.c, enc_sse2.c (encoder).
                          lossless.c has the one-line patch noted below.

   utils/                 Utility sources, verbatim from upstream
                          src/utils/ minus bit_writer.c/.h,
                          huffman_encode.c/.h (encoder) and minus the
                          original quant_levels.c (replaced).
                          A slimmed quant_levels_dec.c is provided in
                          its place.

   smakefile              SAS/C makefile that builds libwebp.lib.

   COPYING                Upstream BSD licence.
   PATENTS                Upstream patent grant.  Required for
                          redistribution of WebP code in compiled form.
   AUTHORS                Upstream authors list.

PATCHES
-------
Compared to upstream libwebp 0.2.0, seven changes are present:

1.  webp/types.h replaced by an SAS/C-friendly variant.
    The original pulls in <inttypes.h> for int8_t/uint8_t/...; SAS/C
    does not ship that header.  SAS/C 6.58 also lacks dependable
    native C99-style 64-bit integer operators, so this Amiga build
    maps int64_t/uint64_t to native 32-bit long types and patches
    the decoder code paths that used 64-bit shifts/multiplies.
    WEBP_INLINE is neutralised so `static WEBP_INLINE ...' compiles
    as `static ...' under strict C89.

2.  dsp/lossless.c has a stale `#include "../enc/histogram.h"' near
    the top that references no symbols actually used in the
    translation unit.  That include line has been replaced by a
    comment explaining the change, so the file compiles without
    the encoder headers being present.

3.  utils/quant_levels.c (which mixed the encoder QuantizeLevels and
    a stub DequantizeLevels) has been replaced by
    utils/quant_levels_dec.c that contains only DequantizeLevels.
    QuantizeLevels would be linked into the plugin image otherwise,
    even though it is never called.

4.  utils/bit_reader.[ch] use the 16-bit VP8 boolean reader on SAS/C
    and a simple 32-bit VP8L bit reader.  This avoids 64-bit shifts
    while preserving lossy and lossless WebP decoding.  The VP8 byte
    loader also treats SAS/C/Amiga m68k as big-endian explicitly
    because SAS/C does not define the upstream __BIG_ENDIAN__ probe.

5.  dsp/dec.c avoids the upstream 64-bit chroma DC fill helper and
    uses eight byte-wide memset() row fills instead.

6.  dsp/dec.c VE4() (4x4 vertical intra prediction) replaces a C99
    brace-initialised array of AVG3() expressions with explicit
    element assignments.  SAS/C 6.58 rejects non-constant aggregate
    initialiser expressions (Error 20: invalid constant expression)
    even for automatic arrays, so the values are materialised first
    and then memcpy'd.

7.  dec/vp8.c treats SAS/C/Amiga m68k as big-endian for the
    PACK_CST residual non-zero packing constant.  Without this, lossy
    VP8 streams on m68k use the little-endian packing constant and can
    fail to render even though the RIFF/WebP headers parse correctly.

Suppressed SAS/C diagnostics
----------------------------
The SCOPTIONS file (and the smakefile CFLAGS) suppress a number of
SAS/C warnings that are noisy on the upstream libwebp 0.2.0 sources
but do not indicate real defects on the Amiga build:

    IGNORE=63   "item already declared"      - forward struct
                                                declarations of
                                                VP8LTransform after
                                                a full definition.
    IGNORE=79   "duplicate of enumeration    - libwebp's prediction
                value"                         mode enums alias one
                                                value under multiple
                                                identifiers.
    IGNORE=85   "return value mismatch"      - implicit signed/
                                                unsigned and float/
                                                double conversions
                                                in helper functions
                                                that produce
                                                in-range values.
    IGNORE=100  "missing prototype"          - libwebp's
                                                file-internal
                                                statics.
    IGNORE=104  "non-portable code"          - portability hints on
                                                stdint typedefs.
    IGNORE=193  "unreachable code"           - libwebp branch
                                                trimming.
    IGNORE=304  "declaration after           - C99-style mixed
                statement"                     declarations.
    IGNORE=306  "inline keyword"             - libwebp uses
                                                WEBP_INLINE, which
                                                is neutralised here
                                                but the diagnostic
                                                fires anyway in some
                                                places.
    IGNORE=308  "inline function does        - predictor helper
                not use formal parameter"      variants share a
                                                common signature but
                                                not every variant
                                                uses both neighbour
                                                parameters.
    IGNORE=315  "static variable is          - kHashMul and similar
                unreachable"                   table constants from
                                                header-defined
                                                helpers that the
                                                particular TU
                                                doesn't reach.
    IGNORE=316  "static function is          - WEBP_INLINE helper
                unreachable"                   functions defined in
                                                headers that the
                                                particular TU
                                                doesn't reach.

Source provenance
-----------------
Upstream tarball: libwebp-0.2.0.tar.gz from
    https://chromium.googlesource.com/webm/libwebp
    https://github.com/webmproject/libwebp
Released 2012.  This version was chosen because it is the
earliest libwebp release with the incremental decoding API and is
small enough to fit comfortably on classic AmigaOS targets while
covering both VP8 (lossy) and VP8L (lossless) bitstreams plus the
ALPH (alpha) chunk.

Building
--------
On the Amiga with SAS/C tools (sc, slink, smake, oml) on the path,
run:

   smake

inside this directory.  The result is libwebp.lib in this directory.
The parent AWebWebpAPL smakefile depends on libwebp.lib and will
relink the plugin once it is available.

Licence
-------
libwebp is BSD-licensed; see COPYING.  Note that BSD licence
compliance requires distributing the COPYING and PATENTS files
alongside any redistribution of the compiled object code.
