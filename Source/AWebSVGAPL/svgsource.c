/**********************************************************************
 *
 * This file is part of the AWeb distribution
 *
 * Copyright (C) 2026 amigazen project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the AWeb Public License as included in this
 * distribution.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * AWeb Public License for more details.
 *
 **********************************************************************/

/* svgsource.c - AWeb SVG plugin sourcedriver.
 *
 * Architecture
 * ------------
 *   - One subtask per SVG source object.  The main task feeds it data
 *     blocks (AOM_SRCUPDATE) and sets an EOF flag on the source.  The
 *     subtask waits for EOF, then concatenates all blocks into a
 *     single buffer and parses the whole thing into a DOM tree using
 *     xmlparse.c.
 *   - All DOM nodes plus all transient renderer state allocate from a
 *     single exec-pool, freed in one shot when the subtask exits.
 *   - The renderer walks the DOM recursively, carrying a style frame
 *     (fill, stroke, stroke-width, current 2x3 transform matrix in
 *     16.16 fixed point).  Children inherit and may override.
 *   - Drawing is implemented in-house against graphics.library
 *     primitives (RectFill, Move/Draw, WritePixel).  Filled polygons
 *     use a scanline algorithm with the even-odd rule; ellipses use
 *     a midpoint rasteriser.  This avoids the historical surprises
 *     of vrastport.lib's signed 64-bit coordinate scaling and gives
 *     us deterministic, easily-debugged geometry.
 *   - Curves and arcs in <path d="..."> are flattened to line
 *     segments in code before being handed to the polygon helpers.
 *   - All allocated screen pens are tracked in the Svgsource and
 *     released in Disposesource so we are good citizens on shared
 *     colormaps. */

#include "pluginlib.h"
#include "awebsvg.h"
#include "xmlparse.h"
#include "ezlists.h"
#include "svgtext.h"

#include <libraries/awebplugin.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <intuition/intuitionbase.h>
#include <proto/awebplugin.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <libraries/Picasso96.h>
#include <proto/Picasso96.h>
#include <devices/timer.h>
#include <proto/lowlevel.h>

#include <string.h>

extern struct Library *P96Base;
extern struct Library *LowLevelBase;

/* Forward decls (provided by awebplugin.library) */
extern struct Taskmsg *Gettaskmsg(void);
extern void Replytaskmsg(struct Taskmsg *msg);
extern BOOL Checktaskbreak(void);
extern ULONG Waittask(ULONG signals);
extern long Updatetaskattrs(ULONG tag,...);

/* Workaround for missing AOSDV_Displayed in older awebplugin.library. */
#define AOSRC_Displayed    (AOSRC_Dummy+2)

/* Limits */
#define MAX_BITMAP_DIM     2048L
#define DEFAULT_DIM        100L
/* Hard floor on the bitmap dimensions we will ever shrink down to.
 * Keeps thumbnail-class renders legible even when chip RAM is low. */
#define MIN_BITMAP_DIM         64L
/* Public-RAM aware sizing.  CHUNKY_BUDGET_DIVISOR is the fraction of
 * AvailMem(MEMF_PUBLIC|MEMF_LARGEST) we are allowed to use for the
 * intermediate chunky framebuffer; CHUNKY_HARD_CEILING is the
 * absolute cap regardless of how much memory the user has. */
#define CHUNKY_BUDGET_DIVISOR  4UL
#define CHUNKY_HARD_CEILING    (4UL * 1024UL * 1024UL)
/* Chip-RAM aware sizing.  The plugin's palette-mode AllocBitMap
 * lands in chip on AGA/ECS targets, and the picture super in the
 * datatype variant uses chip as well, so a downstream chip
 * exhaustion silently produces a blank picture.  Pre-clamp here to
 * the budget computed below. */
#define CHIP_BUDGET_DIVISOR    2UL
#define CHIP_HEADROOM_BYTES    (256UL * 1024UL)
#define CHIP_MIN_BUDGET_BYTES  (32UL * 1024UL)
/* Heuristic to detect "this is probably an RTG system, relax the
 * chip cap": treat large fast/chip ratios with a healthy fast-RAM
 * floor as an indicator that the bitmap will live in graphics card
 * memory rather than chip. */
#define CHIP_RTG_FAST_RATIO    4UL
#define CHIP_RTG_FAST_FLOOR    (8UL * 1024UL * 1024UL)
/* Maximum number of vertices we will accumulate for a single subpath
 * before silently truncating.  Inkscape-generated maps in the wild can
 * easily reach a few thousand sampled bezier vertices on a single big
 * boundary path - if we cap too low, the subpath wraps back to its
 * start vertex prematurely and the user sees a shortcut line cutting
 * across the shape.  Keep this <=16384 because the in-struct capacity
 * counter is a WORD and capacity*2 must not overflow. */
#define POLY_MAX_VERTS     8192

/* Maximum number of subpaths we will accumulate into a single
 * compound fill (outer outline plus N inner holes).  The SVG
 * "O" / "B" / yin-yang idiom needs at least two; complex stylised
 * logos can use a dozen.  Going past this cap is handled
 * gracefully - extra subpaths are still stroked but excluded from
 * the compound fill, degrading to the previous overpaint behaviour. */
#define MAX_SUBPATHS_PER_PATH  128

#define DECOF_P96MAP       0x0004
#define DECOF_P96DEEP      0x0008

/* Quality flags - which expensive per-pixel rendering paths the
 * current decoder is allowed to take.  Defaults are picked
 * per-decoder by Decidequality() from the measured CPU speed (see
 * Benchmarkcpu) and the document's own complexity.
 *
 * - QF_GRAD_LINEAR : per-pixel linear gradient evaluation
 *                    (cheap-ish; one add + a stop lookup per pixel)
 * - QF_GRAD_RADIAL : per-pixel radial gradient evaluation
 *                    (very expensive; fdiv + Newton sqrt per pixel)
 * - QF_ALPHA_BLEND : real source-over alpha compositing on P96 deep
 *                    (3 mul + 3 div per pixel for partially-trans
 *                    spans, free for fully-opaque ones)
 *
 * When a flag is off, the corresponding feature degrades to the
 * cheap fallback that v1 of the renderer used: averaged colour for
 * gradients, alpha-as-skip-gate for opacity. */
#define QF_GRAD_LINEAR     0x0001
#define QF_GRAD_RADIAL     0x0002
#define QF_ALPHA_BLEND     0x0004
#define QF_ALL             (QF_GRAD_LINEAR|QF_GRAD_RADIAL|QF_ALPHA_BLEND)

/*--------------------------------------------------------------------*/
/* Plugin data structures                                             */
/*--------------------------------------------------------------------*/

struct Datablock
{  NODE(Datablock);
   UBYTE *data;
   long length;
};

struct Svgsource
{  struct Sourcedriver sourcedriver;
   struct Aobject *source;
   struct Aobject *task;
   LIST(Datablock) data;
   long width,height;             /* Bitmap dimensions (raster pixels)   */
   struct BitMap *bitmap;
   UBYTE *mask;
   long memory;
   struct SignalSemaphore sema;
   USHORT flags;
   /* Pen tracking - released on dispose */
   struct ColorMap *colormap;
   short allocated[256];
   struct BitMap *friendbitmap;
};

#define SVGSF_EOF          0x0001
#define SVGSF_DISPLAYED    0x0002
#define SVGSF_MEMORY       0x0004
#define SVGSF_IMAGEREADY   0x0010

/* Matrix is 2x3 affine in 16.16 fixed point:
 *    | a c e |
 *    | b d f |
 *    | 0 0 1 |
 * (x,y) maps to (a*x + c*y + e, b*x + d*y + f).
 */
struct Matrix
{  LONG a,b,c,d,e,f;
};

/* Local raster point type.  vrastport.h used to provide Point32, but
 * the renderer no longer links vrastport; keep this small geometry
 * type private to svgsource.c. */
struct Point32
{  LONG x;
   LONG y;
};

/* Render state inherited along the element tree.
 *
 * Opacity model: each of `element_opacity`, `fill_opacity` and
 * `stroke_opacity` is stored as a 0..255 alpha.  Effective per-channel
 * alpha is the product of the three (combined via (a*b)/255).  We
 * inherit element_opacity multiplicatively when descending into a
 * group so nested transparent groups dim correctly.  When the final
 * effective alpha falls below ALPHA_SKIP we skip the drawing call
 * entirely - we don't have read-modify-write blending in the inner
 * loops for performance reasons, so opacity acts as a soft visibility
 * gate rather than true compositing. */
struct Renderstate
{  struct Matrix M;            /* Current transform: SVG -> raster pixels */
   UBYTE fillpen;
   UBYTE strokepen;
   UBYTE fillvalid;            /* TRUE means fill enabled */
   UBYTE strokevalid;          /* TRUE means stroke enabled */
   UBYTE visible;              /* FALSE means display:none in effect */
   UBYTE element_opacity;      /* `opacity` cascade, inherited multiplicatively */
   UBYTE fill_opacity;         /* `fill-opacity`, not inherited (per-element) */
   UBYTE stroke_opacity;       /* `stroke-opacity`, not inherited            */
   UBYTE fill_color_alpha;     /* alpha embedded in fill colour (gradient/rgba) */
   UBYTE stroke_color_alpha;   /* alpha embedded in stroke colour            */
   UBYTE pad0[2];
   LONG strokewidth;           /* Stroke width in 16.16 SVG units */
   ULONG fillrgb;              /* 0xRRGGBB for Picasso96 deep fills */
   ULONG strokergb;            /* 0xRRGGBB for Picasso96 deep strokes */
   struct Gradient *fillgrad;  /* parsed gradient for per-pixel fill, or NULL */
};

/* Paint context handed to the span filler.  Solid fills set grad to
 * NULL and the rasteriser takes the fast path; gradient fills also
 * include the per-shape evaluator state (linear scalar projection or
 * radial distance from centre, both already in gradient coordinates).
 *
 * For linear gradients we precompute the t value at the leftmost
 * column of the shape's bounding box and the per-pixel `dt`; for
 * radial gradients we keep the pixel-space inverse mapping and the
 * centre/radius in gradient coords. */
struct Paintctx
{  UBYTE has_grad;
   UBYTE alpha;                /* combined element*fill alpha, 0..255 */
   UBYTE pad[2];
   ULONG rgb;                  /* solid fallback / cached average colour */
   struct Gradient *grad;
   /* Per-shape inverse matrix: raster pixel -> gradient coordinates. */
   struct Matrix inv;
   /* Linear: scalar-projection state. */
   LONG t_x0;                  /* t at pixel (0,0) in raster space    */
   LONG dt_dx;                 /* delta t per +1 raster x             */
   LONG dt_dy;                 /* delta t per +1 raster y             */
   /* Radial: pre-normalised focal-point evaluator state.  All
    * coords are divided by g->r so the boundary circle is at unit
    * radius - this keeps fmul inputs small enough to avoid 16.16
    * overflow when the SVG specifies large user-space coordinates
    * (e.g. cx=300, r=100). */
   LONG inv_r;                 /* 1/r in 16.16 */
   LONG Dx_n, Dy_n;            /* (fx-cx)/r, (fy-cy)/r */
   LONG DD_minus_1;            /* Dx_n^2 + Dy_n^2 - 1 (cached) */
};

/* Below this final effective alpha, a paint operation is suppressed. */
#define ALPHA_SKIP 24           /* ~10% */

/* Maximum number of stops we record per gradient.  Real-world SVG
 * gradients almost never have more than 8; the Wikipedia logo caps
 * out at 4.  Sixteen is generous and keeps the per-Iddef cost small. */
#define GRAD_MAX_STOPS 16

#define GRAD_TYPE_LINEAR  0
#define GRAD_TYPE_RADIAL  1

#define GRAD_UNITS_USER   0     /* userSpaceOnUse (the default for our  */
                                /* purposes; objectBoundingBox is below) */
#define GRAD_UNITS_OBJBB  1

/* SVG spreadMethod values - how the gradient extends past its
 * defined range [0,1].
 *   PAD     : clamp to nearest endpoint (the SVG default)
 *   REFLECT : ping-pong, t -> 1-frac(t) on odd reflections
 *   REPEAT  : tile, t -> frac(t)
 */
#define GRAD_SPREAD_PAD     0
#define GRAD_SPREAD_REFLECT 1
#define GRAD_SPREAD_REPEAT  2

/* One stop on a parsed gradient.  Offset is 16.16 in [0,1]. */
struct Gradstop_p
{  LONG offset;
   UBYTE r;
   UBYTE g;
   UBYTE b;
   UBYTE a;
};

/* Resolved gradient paint server, cached on the Iddef of the original
 * <linearGradient> or <radialGradient> node.  All coordinates are in
 * the gradient's coordinate space (i.e. they have NOT had the SVG
 * gradientTransform applied yet - that is stored in `gt` and composed
 * with the rendering CTM when building a per-shape paint context). */
struct Gradient
{  UBYTE type;                 /* GRAD_TYPE_LINEAR or _RADIAL */
   UBYTE units;                /* GRAD_UNITS_USER or _OBJBB */
   UBYTE nstops;
   UBYTE has_gt;               /* TRUE if gradientTransform was supplied */
   UBYTE spread;               /* GRAD_SPREAD_PAD / _REFLECT / _REPEAT     */
   UBYTE pad0[3];
   struct Gradstop_p stops[GRAD_MAX_STOPS];
   LONG x1, y1, x2, y2;        /* linear endpoints (16.16, gradient coords) */
   LONG cx, cy, r, fx, fy;     /* radial centre/focus/radius (16.16)        */
   struct Matrix gt;           /* gradientTransform: grad coords -> user    */
};

/* Axis-aligned bounding box in 16.16 user-space coordinates.
 * Used to map objectBoundingBox gradients onto the shape they
 * decorate, and to feed the radial-gradient focal-point evaluator
 * a normalised reference frame. */
struct Bbox
{  BOOL valid;
   LONG xmin, ymin, xmax, ymax;
};

/* Hash entry for the id->node map.  Pool-allocated, so it lives as
 * long as the decoder's pool.
 *
 * For gradient nodes (linearGradient / radialGradient) we cache both
 * a fully parsed Gradient (used for real per-pixel gradient fills on
 * Picasso96 deep destinations) AND an alpha-weighted average colour
 * used as the fallback on palette destinations, on stroke (we do not
 * stroke gradients) and when gradient parsing fails.  Both forms are
 * computed lazily on first reference and reused afterwards.
 *
 * grad_state values:
 *   0  not yet computed (or node is not a gradient)
 *   1  computed and grad_rgb / grad_alpha are valid; `grad` is non-NULL
 *      when the per-pixel gradient parsed successfully
 *   2  computed but the gradient had no usable stops (don't try again)
 */
struct Iddef
{  struct Iddef *next;
   UBYTE *id;                  /* pointer into the in-place parse buffer */
   struct XmlNode *node;
   ULONG grad_rgb;             /* 0xRRGGBB average over alpha-weighted stops */
   UBYTE grad_alpha;           /* average stop alpha */
   UBYTE grad_state;
   UBYTE pad[2];
   struct Gradient *grad;      /* parsed gradient for per-pixel fills, or NULL */
};
#define IDTABLE_SIZE 64
#define USE_MAX_DEPTH 16
#define GRAD_MAX_DEPTH 8         /* xlink:href chain depth cap          */

/* Per-decoder colour cache.  Keeps repeated palette lookups fast and,
 * critically, avoids handing out stale pen numbers across decodes
 * because the cache lives on the stack with the decoder. */
struct Pencache
{  ULONG rgb;
   UBYTE pen;
   UBYTE valid;
};
#define PENCACHESIZE 16

/* The decoder context - owned by the subtask. */
struct Decoder
{  struct Svgsource *source;
   struct Screen *screen;
   struct RastPort rp;
   struct BitMap *bitmap;
   long bmw, bmh;
   APTR pool;
   UBYTE *buffer;
   long buflen;
   USHORT flags;
   struct Pencache cache[PENCACHESIZE];
   UBYTE cachenext;
   USHORT decflags;
   struct Iddef *idtable[IDTABLE_SIZE];
   LONG usedepth;              /* recursion guard for <use> elements */
   UBYTE *chunky;              /* R8G8B8 row scratch for p96WritePixelArray */
   long chunkybpr;
   struct RenderInfo ri;
   short currentpen;           /* last SetAPen value, -1 if unset */
   /* Deferred <text> rendering accumulator.  Populated by Rendertext()
    * during the DOM walk and replayed via SvgTextRenderAll() once the
    * vector layer has been committed to the output bitmap.  See
    * svgtext.h for the rationale behind deferred rendering. */
   struct SvgTextList textruns;
   /* Adaptive rendering quality bitfield - QF_* flags.  Decided
    * once at the top of Parsertask based on the CPU benchmark and
    * the document complexity. */
   USHORT quality;
};

#define DECOF_STOP         0x0001
#define DECOF_EOF          0x0002

/*--------------------------------------------------------------------*/
/* Optional debug                                                     */
/*--------------------------------------------------------------------*/

#ifdef DEBUG_PLUGINS
#define SVGLOG(args) Aprintf args
#else
#define SVGLOG(args)
#endif

/*--------------------------------------------------------------------*/
/* Adaptive quality - CPU benchmark and document complexity           */
/*--------------------------------------------------------------------*/

/* Cached benchmark result.  Computed lazily by Benchmarkcpu() on the
 * first SVG render and reused for every subsequent decoder.  Units
 * are "synthetic mixed integer ops per second", roughly comparable
 * across machines:
 *
 *   030 @ 25 MHz   ~ 0.5 - 1.0 MOPS
 *   040 @ 25 MHz   ~ 4 - 6 MOPS
 *   060 @ 50 MHz   ~ 20+ MOPS
 *   PPC under WOS  ~ 50+ MOPS
 *
 * A value of 0 means "unknown" (either we haven't benchmarked yet or
 * we failed to do so - in which case Decidequality falls back to
 * AttnFlags-based tiering). */
static ULONG g_cpu_mops_x256 = 0;

/* Volatile observable sink for the benchmark loop result.  The
 * volatile qualifier prevents SAS/C from concluding that the
 * benchmark's per-iteration arithmetic has no effect and folding the
 * whole loop into a noop. */
static volatile ULONG g_bench_sink = 0;

/* Run a fixed-cost integer mix that resembles the inner loops in
 * Fillspan's gradient and alpha paths (fmul, fdiv, branches, byte
 * compositing).  Times it with ElapsedTime and stores the result in
 * g_cpu_mops_x256 (MOPS * 256, so 256 == 1.0 MOPS).  Idempotent: if
 * already benchmarked, returns immediately. */
static void Benchmarkcpu(void)
{  struct EClockVal ctx;
   ULONG et;
   ULONG i;
   ULONG iters;
   ULONG seconds_x65536;
   /* Synthetic mix - keep these in volatile-like locals so the
    * compiler cannot fold the whole loop away.  The arithmetic on
    * the right is chosen to match the inner loop in Fillspan when
    * blending a gradient pixel: a small multiply, a small divide
    * surrogate (mod), a few adds and a byte clamp. */
   ULONG acc=0;
   ULONG t=0x1234UL;
   ULONG d=0x57UL;

   if(g_cpu_mops_x256) return;
   if(!LowLevelBase)
   {  /* No way to time; leave the flag clear so Decidequality picks
       * a tier from AttnFlags. */
      g_cpu_mops_x256=0;
      return;
   }

   iters=20000;
   ctx.ev_hi=0;
   ctx.ev_lo=0;
   /* First call is documented to return nonsense - consume it so the
    * second call gives us a real delta. */
   (void)ElapsedTime(&ctx);

   for(i=0;i<iters;i++)
   {  t = (t * 0x8088405UL + 1UL);
      acc += (t & 0xffUL) * d;
      d = (d ^ (t>>3)) | 1UL;
      acc += (acc / d);
      acc += ((acc * 7UL + 127UL) >> 8) & 0xffUL;
   }
   et=ElapsedTime(&ctx);
   /* Force the optimizer to keep the loop alive by storing the
    * accumulator into a volatile sink. */
   g_bench_sink = acc ^ d ^ t;

   /* et is 16.16 seconds; convert to seconds*65536.  Guard against
    * impossibly small intervals (PPC under WOS, MMU caching, etc) by
    * lower-bounding at 1 tick (~15us); we want a reasonable answer
    * even on overly fast or noisy timers. */
   seconds_x65536=(ULONG)et;
   if(seconds_x65536<1) seconds_x65536=1;

   /* Compute MOPS*256:
    *    MOPS = (iters * 5_ops_per_iter) / seconds / 1_000_000
    *    MOPS*256 = (iters * 5 * 256) / seconds / 1_000_000
    * seconds = seconds_x65536 / 65536.
    *    MOPS*256 = (iters * 5 * 256 * 65536) / (seconds_x65536 * 1_000_000)
    *
    * Compute in two stages to dodge 32-bit overflow.  iters is
    * <=20000, so iters * 5 * 256 fits in 25 bits; multiplying that
    * by 65536 immediately overflows.  Instead pre-divide by
    * (1_000_000 / 65536) ~= 15.26 ~= 15 first. */
   {  ULONG numer = iters * 5UL * 256UL;     /* up to 25.6M  */
      /* divide by 15 first (close to 1e6/65536) to keep things in
       * range, then divide by the actual seconds value. */
      ULONG mops_x256 = numer / 15UL;
      mops_x256 = (mops_x256 * 1024UL) / seconds_x65536;
      /* The above gives roughly MOPS*256 with about 5% calibration
       * slop versus a true 1MHz reference; the tiers below are
       * coarse enough to absorb that. */
      g_cpu_mops_x256 = mops_x256;
   }
   SVGLOG(("SVG: benchmark = %lu/256 MOPS (et=0x%08lx)\n",
      g_cpu_mops_x256,(unsigned long)et));
}

/* Conservative quality flags chosen from exec.library/AttnFlags when
 * lowlevel.library is not available.  Picks the floor of each CPU
 * class - the benchmark would normally let a fast 030 enable more
 * than this, but without a measurement we err on the safe side. */
static USHORT Defaultquality_attnflags(void)
{  UWORD af = ((struct ExecBase *)SysBase)->AttnFlags;
   if(af & 0x80U)      /* AFB_68060 */ return QF_ALL;
   if(af & 0x08U)      /* AFB_68040 */ return QF_GRAD_LINEAR | QF_ALPHA_BLEND;
   if(af & 0x04U)      /* AFB_68030 */ return QF_GRAD_LINEAR;
   /* 68020 or weaker: conservative averaged-colour fallback. */
   return 0;
}

/* Estimate the rendering complexity of an SVG document and combine
 * that with the measured CPU speed to choose a per-decoder quality
 * tier.  Heuristic, intentionally simple:
 *
 *   work_units = bm_pixels * (1
 *                            + 0.5 * has_gradient_fills
 *                            + 8.0 * has_radial_gradient_fills
 *                            + 0.3 * has_alpha_fills)
 *
 * Budget is 2 seconds at the measured MOPS.  Anything over budget
 * gets a feature disabled in priority order: radial first (by far
 * the most expensive), then alpha blending, then linear gradients.
 *
 * `n_grad_linear`, `n_grad_radial`, `n_alpha_shapes` are counts of
 * id'd gradients we saw in the document (from the Iddef table) plus
 * a rough count of shapes that carry partial opacity.  Counted in
 * Decidequality directly. */
static void Decidequality(struct Decoder *dec)
{  ULONG mops_x256;
   ULONG bm_pixels;
   ULONG i;
   ULONG n_linear=0, n_radial=0;
   USHORT q;
   Benchmarkcpu();
   mops_x256 = g_cpu_mops_x256;

   /* Start with the CPU's natural tier. */
   if(mops_x256 == 0)
   {  q = Defaultquality_attnflags();
   }
   else if(mops_x256 >= (15UL*256UL))      /* >=15 MOPS -> everything */
   {  q = QF_ALL;
   }
   else if(mops_x256 >= (4UL*256UL))       /* 4-15 MOPS -> no radial */
   {  q = QF_GRAD_LINEAR | QF_ALPHA_BLEND;
   }
   else if(mops_x256 >= (1UL*256UL))       /* 1-4 MOPS -> linear only */
   {  q = QF_GRAD_LINEAR;
   }
   else                                     /* <1 MOPS -> averaged fallback */
   {  q = 0;
   }

   /* Quality is meaningful only on P96 deep destinations - the
    * palette path is averaged-colour only regardless. */
   if(!(dec->decflags & DECOF_P96DEEP)) q = 0;

   /* Count gradients in the document so we can scale the budget by
    * how much the gradient evaluator will actually be exercised. */
   for(i=0;i<IDTABLE_SIZE;i++)
   {  struct Iddef *e;
      for(e=dec->idtable[i]; e; e=e->next)
      {  if(!e->node) continue;
         if(XmlNameIs(e->node,"linearGradient")) n_linear++;
         else if(XmlNameIs(e->node,"radialGradient")) n_radial++;
      }
   }

   /* Pixel-cost ceiling: a 4-megapixel document with full per-pixel
    * radial gradient (~50x solid cost) on a 1-MOPS machine would
    * take ~200 seconds.  Demote radial to averaged if the budget
    * looks blown - even on a 1-MOPS-class machine. */
   bm_pixels = (ULONG)dec->bmw * (ULONG)dec->bmh;
   if(bm_pixels==0) bm_pixels=1;

   /* Per-pixel radial cost is roughly 50x a solid fill; linear is
    * ~3x.  Estimated work is (bm_pixels * n_grad * cost_factor) in
    * synthetic ops.  Compare against (mops_x256 * 1024 * 2_sec_budget)
    * /256 = mops_x256 * 8 (so the right-hand side is mops_x256 * 8 in
    * the same scaled units).  We pre-scale numerator by /256 to dodge
    * 32-bit overflow on big documents. */
   if((q & QF_GRAD_RADIAL) && n_radial>0 && mops_x256>0)
   {  ULONG est = bm_pixels;
      if(est > (ULONG)(2*1024*1024)) est = 2*1024*1024;
      est = (est * n_radial * 50UL) >> 8;
      if(est > mops_x256 * 8UL)
      {  q &= ~QF_GRAD_RADIAL;
         SVGLOG(("SVG: demoting radial gradients (est %lu vs cap %lu)\n",
            (unsigned long)est, (unsigned long)mops_x256*8UL));
      }
   }
   if((q & QF_GRAD_LINEAR) && n_linear>0 && mops_x256>0)
   {  ULONG est = bm_pixels;
      if(est > (ULONG)(2*1024*1024)) est = 2*1024*1024;
      est = (est * n_linear * 3UL) >> 8;
      if(est > mops_x256 * 8UL)
      {  q &= ~QF_GRAD_LINEAR;
         SVGLOG(("SVG: demoting linear gradients (est %lu vs cap %lu)\n",
            (unsigned long)est, (unsigned long)mops_x256*8UL));
      }
   }
   /* Alpha blending cost is bounded by total framebuffer pixels (we
    * only blend over actually-covered spans, but worst case is full
    * coverage).  At ~5 ops/pixel a 1024x1024 doc on a 1 MOPS machine
    * is already 5 seconds.  Demote alpha when the worst-case fill
    * exceeds the budget. */
   if((q & QF_ALPHA_BLEND) && mops_x256>0)
   {  ULONG est = bm_pixels;
      if(est > (ULONG)(2*1024*1024)) est = 2*1024*1024;
      est = (est * 5UL) >> 8;
      if(est > mops_x256 * 8UL)
      {  q &= ~QF_ALPHA_BLEND;
         SVGLOG(("SVG: demoting alpha blending (est %lu vs cap %lu)\n",
            (unsigned long)est, (unsigned long)mops_x256*8UL));
      }
   }

   dec->quality = q;
   SVGLOG(("SVG: quality flags = 0x%04x (mops*256=%lu, pix=%lu, "
           "lin=%lu rad=%lu)\n",
      (unsigned)q,(unsigned long)mops_x256,(unsigned long)bm_pixels,
      (unsigned long)n_linear,(unsigned long)n_radial));
}

/*--------------------------------------------------------------------*/
/* Forward decls                                                      */
/*--------------------------------------------------------------------*/

static void Parsertask(void *userdata);
static void Renderelement(struct Decoder *dec, struct XmlNode *node, struct Renderstate *parent);
static struct XmlNode *Findid(struct Decoder *dec, const UBYTE *id);
static struct Iddef *Findid_iddef(struct Decoder *dec, const UBYTE *id);
static UBYTE *Resolvehref(struct XmlNode *node);
static BOOL Resolvepaint(struct Decoder *dec, UBYTE *value,
   ULONG *rgb_out, UBYTE *alpha_out, BOOL *enabled_out,
   struct Gradient **grad_out);
static ULONG Gradsample(const struct Gradient *g, LONG t, UBYTE *out_a);
static ULONG Gradeval_pixel(const struct Paintctx *pctx, LONG rx, LONG ry,
   UBYTE *out_a);
static UBYTE Effectivefillalpha(struct Renderstate *rs);
static UBYTE Effectivestrokealpha(struct Renderstate *rs);
static void Buildfillctx(struct Paintctx *pctx, struct Decoder *dec,
   struct Renderstate *rs, UBYTE eff_alpha, const struct Bbox *bb);
static void Buildstrokectx(struct Paintctx *pctx, struct Decoder *dec,
   struct Renderstate *rs, UBYTE eff_alpha);
static BOOL Buildpaintctx(struct Paintctx *pctx, struct Renderstate *rs,
   struct Gradient *g, const struct Bbox *bb);
static LONG Strokewidth_px(struct Renderstate *rs);
static LONG fsqrt(LONG x);
static LONG fatan2_deg(LONG y, LONG x);
static LONG Isqrt32(LONG x);
static LONG Gradspread(const struct Gradient *g, LONG t);
static void Drawpolygon_fill_compound(struct Decoder *dec, struct Point32 *pts,
   const WORD *subpath_ends, WORD subpath_count, const struct Paintctx *pctx);
static void Strokepolyline(struct Decoder *dec, struct Point32 *pts, WORD count,
   BOOL closepath, struct Renderstate *rs);
static void Strokeellipse(struct Decoder *dec, LONG cx, LONG cy,
   LONG rx, LONG ry, struct Renderstate *rs);
static void Drawellipse_fill(struct Decoder *dec, LONG cx, LONG cy,
   LONG rx, LONG ry, const struct Paintctx *pctx);

/*--------------------------------------------------------------------*/
/* Number parsing                                                     */
/*--------------------------------------------------------------------*/

/* Parse a single SVG number starting at *pp.  Skips leading
 * whitespace and a single optional comma.  Returns the value in
 * 16.16 fixed point and advances *pp.  Sets *ok to TRUE on success,
 * FALSE if nothing was parsed.
 *
 * IMPORTANT: this routine deliberately does NOT consume any trailing
 * letters.  Path data (`m0 0h77z`), transform lists (`translate(1 2)`),
 * polygon points etc. all interleave numbers with command/grouping
 * letters that we must hand back to the caller.  Callers that accept
 * CSS-style unit suffixes (`100px`, `2em`, `50%`) call Skipunits()
 * after Parsefixed() to swallow them. */
static LONG Parsefixed(UBYTE **pp, UBYTE *end, BOOL *ok)
{  UBYTE *p=*pp;
   LONG sign=1;
   LONG ipart=0;
   LONG fpart=0;
   LONG fscale=1;
   BOOL anydigits=FALSE;
   BOOL hasdot=FALSE;
   LONG result;

   while(p<end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p==',')) p++;
   if(p<end && (*p=='+' || *p=='-'))
   {  if(*p=='-') sign=-1;
      p++;
   }
   while(p<end && *p>='0' && *p<='9')
   {  ipart=ipart*10+(*p-'0');
      anydigits=TRUE;
      p++;
   }
   if(p<end && *p=='.')
   {  hasdot=TRUE;
      p++;
      while(p<end && *p>='0' && *p<='9' && fscale<1000000)
      {  fpart=fpart*10+(*p-'0');
         fscale*=10;
         anydigits=TRUE;
         p++;
      }
      /* Discard remaining digits beyond our precision. */
      while(p<end && *p>='0' && *p<='9') p++;
   }
   /* Optional exponent. */
   if(p<end && (*p=='e' || *p=='E'))
   {  LONG esign=1;
      LONG eval=0;
      UBYTE *save=p;
      p++;
      if(p<end && (*p=='+' || *p=='-'))
      {  if(*p=='-') esign=-1;
         p++;
      }
      if(p<end && *p>='0' && *p<='9')
      {  while(p<end && *p>='0' && *p<='9')
         {  eval=eval*10+(*p-'0');
            p++;
         }
         /* Apply exponent to ipart/fpart.  Very rough but adequate. */
         eval*=esign;
         while(eval>0) { ipart*=10; fpart*=10; eval--; }
         while(eval<0)
         {  fpart+=ipart*fscale;
            ipart=0;
            fscale*=10;
            eval++;
         }
      }
      else
      {  p=save;
      }
   }

   if(!anydigits)
   {  if(ok) *ok=FALSE;
      return 0;
   }
   /* result = sign * (ipart + fpart/fscale) in 16.16 */
   result=ipart<<16;
   if(hasdot && fscale>1)
   {  result+=(fpart<<16)/fscale;
   }
   if(sign<0) result=-result;
   *pp=p;
   if(ok) *ok=TRUE;
   return result;
}

/* Skip optional CSS-style unit suffix (px, pt, em, %, ...) sitting at
 * *pp.  Used by callers that accept attribute-style dimensions, NOT by
 * the path/transform/points parsers which need to keep command and
 * separator letters intact. */
static void Skipunits(UBYTE **pp, UBYTE *end)
{  UBYTE *p=*pp;
   while(p<end && ((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='%')) p++;
   *pp=p;
}

/* Parselong() previously parsed width/height as plain integers.  It is
 * unused since width/height now go through Numattr/Parsefixed.  Kept
 * removed rather than #if'd because reintroducing it would also need
 * unit-aware parsing for em/rem/etc. */

/*--------------------------------------------------------------------*/
/* Colour parsing                                                     */
/*--------------------------------------------------------------------*/

struct Namedcolor
{  const char *name;
   ULONG rgb;
};

/* The 17 CSS 2.1 named colours plus a few extras commonly used in
 * SVG icons.  Stored as 0xRRGGBB. */
static const struct Namedcolor named_colors[]=
{  {"aqua",    0x00ffffUL},
   {"black",   0x000000UL},
   {"blue",    0x0000ffUL},
   {"brown",   0xa52a2aUL},
   {"cyan",    0x00ffffUL},
   {"fuchsia", 0xff00ffUL},
   {"gray",    0x808080UL},
   {"grey",    0x808080UL},
   {"green",   0x008000UL},
   {"lime",    0x00ff00UL},
   {"magenta", 0xff00ffUL},
   {"maroon",  0x800000UL},
   {"navy",    0x000080UL},
   {"olive",   0x808000UL},
   {"orange",  0xffa500UL},
   {"pink",    0xffc0cbUL},
   {"purple",  0x800080UL},
   {"red",     0xff0000UL},
   {"silver",  0xc0c0c0UL},
   {"teal",    0x008080UL},
   {"white",   0xffffffUL},
   {"yellow",  0xffff00UL},
   {NULL,      0UL}
};

static int hexdigit(UBYTE c)
{  if(c>='0'&&c<='9') return c-'0';
   if(c>='a'&&c<='f') return c-'a'+10;
   if(c>='A'&&c<='F') return c-'A'+10;
   return -1;
}

static int strieq(const UBYTE *a, const char *b)
{  while(*a && *b)
   {  UBYTE ca=*a;
      UBYTE cb=(UBYTE)*b;
      if(ca>='A'&&ca<='Z') ca=ca-'A'+'a';
      if(cb>='A'&&cb<='Z') cb=cb-'A'+'a';
      if(ca!=cb) return 0;
      a++; b++;
   }
   return (*a==0 && *b==0);
}

/* Parse an SVG opacity / fraction value.
 *
 * Accepts either a plain fraction in [0..1] (`0`, `1`, `0.75`) or a
 * percentage (`50%`, `100%`).  Clamps to [0..1] and returns the
 * value as a 0..255 alpha byte.  Returns 255 on any parse failure
 * so opaque is the safe default. */
static UBYTE Parsealphaval(const UBYTE *str)
{  UBYTE *p,*e;
   LONG v;
   BOOL ok;
   LONG a;
   BOOL pct=FALSE;
   if(!str) return 255;
   p=(UBYTE *)str;
   e=p;
   while(*e) e++;
   v=Parsefixed(&p,e,&ok);
   if(!ok) return 255;
   while(*p==' '||*p=='\t') p++;
   if(*p=='%') pct=TRUE;
   /* v is 16.16 fixed.  Treat % as v/100, else v as is. */
   if(pct) v=v/100;
   if(v<=0) return 0;
   if(v>=(1L<<16)) return 255;
   a=(v*255)>>16;
   if(a<0) a=0;
   if(a>255) a=255;
   return (UBYTE)a;
}

/* Multiply two 0..255 alpha values, rounded.  Used to combine the
 * various opacity sources (element, fill/stroke, colour-embedded). */
static UBYTE Combinealpha(UBYTE a, UBYTE b)
{  ULONG r=(ULONG)a*(ULONG)b + 127UL;
   return (UBYTE)(r/255);
}

/* Parse an SVG colour spec.
 *
 * Sets *rgb to 0xRRGGBB and *alpha to the colour-embedded opacity
 * (255 unless the source said rgba() with an explicit alpha; 0 for
 * "none"/"transparent").  Returns TRUE on a recognised value.  The
 * caller is responsible for further combining *alpha with any
 * separate fill-opacity / stroke-opacity / opacity properties.
 *
 * Recognises:
 *   - none, transparent     -> *enabled = FALSE
 *   - #rgb and #rrggbb hex
 *   - rgb(r,g,b)            byte or percent components
 *   - rgba(r,g,b,a)         alpha is 0..1 (or 0..100%)
 *   - 22 CSS named colours  (the 17 standard plus a few extras)
 *
 * Returns FALSE for unrecognised values (including "currentColor",
 * which we don't model) so the caller's previous colour stands. */
static BOOL Parsecolor(const UBYTE *str, ULONG *rgb, BOOL *enabled, UBYTE *alpha)
{  const UBYTE *p=str;
   if(alpha) *alpha=255;
   if(!p) { *enabled=FALSE; return FALSE; }
   while(*p==' '||*p=='\t') p++;
   if(*p==0) { *enabled=FALSE; return FALSE; }
   if(strieq(p,"none") || strieq(p,"transparent"))
   {  *enabled=FALSE;
      *rgb=0;
      if(alpha) *alpha=0;
      return TRUE;
   }
   *enabled=TRUE;
   if(*p=='#')
   {  ULONG v=0;
      int len=0;
      int i;
      p++;
      while(p[len] && hexdigit(p[len])>=0) len++;
      if(len==3)
      {  for(i=0;i<3;i++)
         {  int d=hexdigit(p[i]);
            v=(v<<8)|((d<<4)|d);
         }
         *rgb=v;
         return TRUE;
      }
      if(len>=6)
      {  for(i=0;i<6;i++) v=(v<<4)|(ULONG)hexdigit(p[i]);
         *rgb=v;
         return TRUE;
      }
      *enabled=FALSE;
      return FALSE;
   }
   if((p[0]=='r' || p[0]=='R')
   && (p[1]=='g' || p[1]=='G')
   && (p[2]=='b' || p[2]=='B'))
   {  /* rgb(...) or rgba(...) - peek at fourth letter to decide. */
      BOOL hasalpha=FALSE;
      UBYTE *q;
      LONG c[3];
      LONG i;
      BOOL ispct;
      BOOL ok;
      LONG v;
      UBYTE *qend;
      LONG cv;
      c[0]=c[1]=c[2]=0; /* defensive: SAS/C cannot prove loop fills all */
      if(p[3]=='a' || p[3]=='A')
      {  hasalpha=TRUE;
         q=(UBYTE *)p+4;
      }
      else q=(UBYTE *)p+3;
      while(*q==' '||*q=='\t') q++;
      if(*q!='(') { *enabled=FALSE; return FALSE; }
      q++;
      qend=q;
      while(*qend) qend++;
      for(i=0;i<3;i++)
      {  v=Parsefixed(&q,qend,&ok);
         if(!ok) { *enabled=FALSE; return FALSE; }
         ispct=FALSE;
         while(*q==' '||*q=='\t') q++;
         if(*q=='%') { ispct=TRUE; q++; }
         while(*q==' '||*q=='\t'||*q==',') q++;
         /* v is 16.16.  Avoid double here; SAS/C's float path pulls in
          * runtime exit/check-break support that plugins do not link. */
         if(v<=0) cv=0;
         else if(ispct)
         {  if(v>=(100L<<16)) cv=255;
            else cv=(v*255)/(100L<<16);
         }
         else
         {  if(v>=(255L<<16)) cv=255;
            else cv=v>>16;
         }
         c[i]=cv;
      }
      *rgb=((ULONG)c[0]<<16)|((ULONG)c[1]<<8)|((ULONG)c[2]);
      if(hasalpha)
      {  LONG a;
         BOOL apct;
         v=Parsefixed(&q,qend,&ok);
         if(!ok)
         {  if(alpha) *alpha=255;
            return TRUE;
         }
         apct=FALSE;
         while(*q==' '||*q=='\t') q++;
         if(*q=='%') apct=TRUE;
         /* Alpha argument is a 0..1 fraction (or 0..100% if explicit). */
         if(v<=0) a=0;
         else if(apct)
         {  if(v>=(100L<<16)) a=255;
            else a=(v*255)/(100L<<16);
         }
         else
         {  if(v>=(1L<<16)) a=255;
            else a=(v*255)>>16;
         }
         if(alpha) *alpha=(UBYTE)a;
      }
      return TRUE;
   }
   /* Named colour. */
   {  const struct Namedcolor *nc;
      for(nc=named_colors; nc->name; nc++)
      {  if(strieq(p,nc->name))
         {  *rgb=nc->rgb;
            return TRUE;
         }
      }
   }
   /* Unknown colour - leave caller's value alone. */
   *enabled=FALSE;
   return FALSE;
}

/*--------------------------------------------------------------------*/
/* Pen allocation                                                     */
/*--------------------------------------------------------------------*/

/* Convert 0..255 byte to RGB() macro form used by ObtainBestPen. */
#define RGBEXP(b) (((ULONG)(b)<<24)|((ULONG)(b)<<16)|((ULONG)(b)<<8)|(ULONG)(b))

/* SetAPen wrapper: skips the call entirely on P96 deep (we paint
 * via the chunky buffer there, no pen state needed) and otherwise
 * elides duplicates so a long run of same-coloured shapes only ever
 * pokes the rastport once. */
static void Setpen(struct Decoder *dec, UBYTE pen)
{  if(dec->decflags&DECOF_P96DEEP) return;
   if(dec->currentpen==(short)pen) return;
   SetAPen(&dec->rp,pen);
   dec->currentpen=(short)pen;
}

/* Obtain a screen pen for the given 0xRRGGBB colour.  The pen is
 * tracked in source->allocated so Disposesource releases it later.
 * Returns the pen number; on failure returns 0 (background pen).
 *
 * We also keep a tiny per-decoder cache so repeated colours during a
 * single render don't go through ObtainBestPen each time. */
static UBYTE Getpen(struct Decoder *dec, ULONG rgb)
{  int i;
   LONG map;
   if(!dec->source->colormap) return 1;
   for(i=0;i<PENCACHESIZE;i++)
   {  if(dec->cache[i].valid && dec->cache[i].rgb==rgb) return dec->cache[i].pen;
   }
   map=ObtainBestPen(dec->source->colormap,
      RGBEXP((rgb>>16)&0xff),
      RGBEXP((rgb>>8)&0xff),
      RGBEXP(rgb&0xff),
      OBP_Precision,PRECISION_IMAGE,
      TAG_END);
   if(map<0) return 1;
   dec->source->allocated[map]++;
   dec->cache[dec->cachenext].rgb=rgb;
   dec->cache[dec->cachenext].pen=(UBYTE)map;
   dec->cache[dec->cachenext].valid=1;
   dec->cachenext=(UBYTE)((dec->cachenext+1)&(PENCACHESIZE-1));
   return (UBYTE)map;
}

/*--------------------------------------------------------------------*/
/* Matrix and transform                                               */
/*--------------------------------------------------------------------*/

static void Midentity(struct Matrix *m)
{  m->a=0x10000L; m->b=0; m->c=0; m->d=0x10000L; m->e=0; m->f=0;
}

/* Fixed-point multiply: returns (a*b) >> 16.  Inputs are 16.16,
 * result is 16.16.  Uses only 32-bit signed multiplication so the
 * 68020 MULS.L instruction handles each partial product in one go -
 * the previous implementation pulled in scm881.lib's double math,
 * which is fine for a one-shot parser routine but disastrous when
 * called inside the bezier sampler and matrix transform loops.
 *
 * Decomposition: a = ah*2^16 + al, b = bh*2^16 + bl.  Then
 *   (a*b) >> 16 = ah*bh*2^16 + ah*bl + al*bh + (al*bl) >> 16.
 *
 * ah,bh are signed; al,bl are unsigned 16-bit halves so the mid
 * products keep their sign correctly when one operand is negative. */
static LONG fmul(LONG a, LONG b)
{  LONG  ah,bh;
   ULONG al,bl;
   ah = a >> 16;
   bh = b >> 16;
   al = (ULONG)a & 0xffffUL;
   bl = (ULONG)b & 0xffffUL;
   return ((ah * bh) << 16)
        + ah * (LONG)bl
        + (LONG)al * bh
        + (LONG)((al * bl) >> 16);
}

/* Fixed-point helpers used by transform parsing.
 *
 * Avoiding libm is important for this plugin: SAS/C's floating-point
 * and transcendental paths can drag in sc.lib's Ctrl-C checking unit
 * (_cxbrk.c), which expects normal C program exit support that an
 * AWeb plugin does not provide.  These helpers keep rotate() and
 * skewX/Y() self-contained.
 */
static LONG fdivclamp(LONG n, LONG d)
{  LONG sign=1;
   LONG q,r;
   LONG limit=16L<<16;
   if(d==0) return (n<0) ? -limit : limit;
   if(n<0) { n=-n; sign=-sign; }
   if(d<0) { d=-d; sign=-sign; }
   q=n/d;
   if(q>=16) return (sign<0) ? -limit : limit;
   r=n%d;
   q=(q<<16)+(((r<<8)/d)<<8);
   return (sign<0) ? -q : q;
}

/* Approximate sin(degrees) where the input is 16.16 degrees and the
 * return value is 16.16.  Uses Bhaskara's sine approximation over
 * 0..180 degrees, exact at 0/90/180 and good enough for SVG
 * transform attributes. */
static LONG fsin_deg(LONG angle)
{  LONG deg;
   LONG sign=1;
   LONG x;
   LONG num,den;
   deg=(angle+(1L<<15))>>16;
   deg%=360;
   if(deg<0) deg+=360;
   if(deg>180)
   {  deg-=180;
      sign=-1;
   }
   x=deg;
   num=4*x*(180-x);
   den=40500 - x*(180-x);
   if(den<=0) return 0;
   x=(num<<16)/den;
   if(x>0x10000L) x=0x10000L;
   return (sign<0) ? -x : x;
}

static LONG fcos_deg(LONG angle)
{  return fsin_deg(angle+(90L<<16));
}

/* Divide two 16.16 fixed-point values producing a 16.16 quotient
 * using a 32-bit-only shift-subtract long division.  Returns clamped
 * maximum on divide-by-zero or overflow.  Only called when building
 * a paint context for a gradient (once per shape) so the per-bit
 * loop is not in any inner rendering path. */
static LONG fdiv(LONG num, LONG denom)
{  LONG sign=1;
   ULONG n_hi, n_lo;
   ULONG d;
   ULONG q=0;
   int i;
   if(denom==0) return (num<0) ? -0x7fffffffL : 0x7fffffffL;
   if(num<0)   { num=-num;     sign=-sign; }
   if(denom<0) { denom=-denom; sign=-sign; }
   /* Build a 48-bit dividend (num << 16) split as n_hi:n_lo where
    * n_hi holds the high 16 bits and n_lo the low 32 bits.  The low
    * 16 bits of n_lo are always zero (they are the shift-fill). */
   n_hi=(ULONG)num >> 16;
   n_lo=(ULONG)num << 16;
   d=(ULONG)denom;
   for(i=0; i<32; i++)
   {  n_hi=(n_hi<<1) | (n_lo>>31);
      n_lo=n_lo<<1;
      q=q<<1;
      if(n_hi>=d)
      {  n_hi-=d;
         q|=1;
      }
   }
   /* Round to nearest: if 2 * remainder >= denom, bump the quotient.
    * Halves the accumulated bias when fdiv is called inside a
    * per-pixel inner loop (gradient evaluator). */
   if((n_hi<<1) >= d && q<0x7fffffffUL) q++;
   if(q>0x7fffffffUL) q=0x7fffffffUL;
   return (sign<0) ? -(LONG)q : (LONG)q;
}

/* 16.16 square root.  Newton-Raphson with a leading-bit seed.
 * Returns 0 for non-positive input. */
static LONG fsqrt(LONG x)
{  LONG y;
   LONG t;
   int i;
   if(x<=0) return 0;
   y=0x10000L;
   t=x;
   while(t>=(LONG)0x00040000L && y<(LONG)0x40000000L) { y<<=1; t>>=2; }
   while(t<0x00010000L) { y>>=1; t<<=2; if(y==0) { y=1; break; } }
   for(i=0;i<8;i++)
   {  LONG ny;
      if(y<=0) { y=0x10000L; break; }
      ny=(y + fdiv(x,y))>>1;
      if(ny==y) break;
      y=ny;
   }
   return y;
}

/* atan2(y,x) in degrees as 16.16 fixed point.  Result in (-180, 180].
 * Uses Rajan's first-quadrant approximation; max error ~0.3 degrees
 * which is more than sufficient for SVG arc rendering. */
static LONG fatan2_deg(LONG y, LONG x)
{  LONG ax,ay;
   LONG ratio;
   LONG result;
   BOOL swapped=FALSE;
   BOOL negx,negy;
   LONG one_minus_r;
   LONG poly;

   if(x==0 && y==0) return 0;
   if(x==0) return (y>0) ? (90L<<16) : -(90L<<16);
   if(y==0) return (x>0) ? 0L : (180L<<16);

   negx=(BOOL)(x<0);
   negy=(BOOL)(y<0);
   ax=negx?-x:x;
   ay=negy?-y:y;

   if(ay>ax)
   {  LONG tmp=ax; ax=ay; ay=tmp;
      swapped=TRUE;
   }
   ratio=fdiv(ay,ax);                  /* 16.16, in [0,1] */

   /* atan_deg(r) ~= 45*r + r*(1-r)*(14.02 + 3.80*r) for r in [0,1].
    * Constants are 16.16 fixed point. */
   one_minus_r=(1L<<16)-ratio;
   poly=918944L + fmul(249037L,ratio); /* 14.02 + 3.80*ratio */
   result=fmul(45L<<16,ratio) + fmul(fmul(ratio,one_minus_r),poly);

   if(swapped) result=(90L<<16)-result;
   if(negx)    result=(180L<<16)-result;
   if(negy)    result=-result;
   return result;
}

/* Integer 32-bit isqrt for pixel-space stroke geometry.  Used by
 * Drawthickseg to avoid lifting raster lengths into 16.16 just to
 * call fsqrt.  Returns floor(sqrt(x)) for x >= 0, 0 for x <= 0. */
static LONG Isqrt32(LONG x)
{  LONG r=0;
   LONG bit;
   if(x<=0) return 0;
   /* Top bit of the binary-decomposition method.  For 32-bit unsigned
    * values, the highest possible bit pair is 30,31 -> shift = 30. */
   bit=1L<<30;
   while(bit>x) bit>>=2;
   while(bit!=0)
   {  if(x>=r+bit) { x-=r+bit; r=(r>>1)+bit; }
      else                    r=r>>1;
      bit>>=2;
   }
   return r;
}

/* Invert a 2x3 affine matrix.  Returns FALSE (and leaves *I untouched)
 * if the matrix is singular.  Used once per shape to build a pixel ->
 * gradient-coords transform. */
static BOOL Minverse(const struct Matrix *M, struct Matrix *I)
{  LONG det;
   det = fmul(M->a,M->d) - fmul(M->c,M->b);
   if(det==0) return FALSE;
   I->a =  fdiv(M->d, det);
   I->b = -fdiv(M->b, det);
   I->c = -fdiv(M->c, det);
   I->d =  fdiv(M->a, det);
   I->e = -fmul(I->a, M->e) - fmul(I->c, M->f);
   I->f = -fmul(I->b, M->e) - fmul(I->d, M->f);
   return TRUE;
}

/* Project a vector (dx,dy) (raster pixels) onto another vector (vx,vy)
 * scaled by 1/lensq.  Returns the scalar projection in 16.16.  Used by
 * the linear gradient per-pixel evaluator to derive `t` and `dt`. */
static LONG Gradproj(LONG dx, LONG dy, LONG vx, LONG vy, LONG inv_lensq)
{  return fmul(fmul(dx,vx) + fmul(dy,vy), inv_lensq);
}

/* Compose two matrices: result = M * T (T applied first then M). */
static void Mcompose(struct Matrix *r, struct Matrix *M, struct Matrix *T)
{  struct Matrix out;
   out.a = fmul(M->a,T->a) + fmul(M->c,T->b);
   out.b = fmul(M->b,T->a) + fmul(M->d,T->b);
   out.c = fmul(M->a,T->c) + fmul(M->c,T->d);
   out.d = fmul(M->b,T->c) + fmul(M->d,T->d);
   out.e = fmul(M->a,T->e) + fmul(M->c,T->f) + M->e;
   out.f = fmul(M->b,T->e) + fmul(M->d,T->f) + M->f;
   *r=out;
}

/* Transform an SVG point (16.16) to raster pixel (integer).
 * Returns raster-pixel coordinates.  Fast-path: when the matrix
 * has no rotation/skew components (b==c==0, the overwhelmingly
 * common case for viewBox scaling + group translates) we skip two
 * of the four multiplies. */
static void Mxform(struct Matrix *M, LONG x, LONG y, LONG *rx, LONG *ry)
{  LONG nx,ny;
   if(M->b==0 && M->c==0)
   {  nx = fmul(M->a,x) + M->e;
      ny = fmul(M->d,y) + M->f;
   }
   else
   {  nx = fmul(M->a,x) + fmul(M->c,y) + M->e;
      ny = fmul(M->b,x) + fmul(M->d,y) + M->f;
   }
   *rx=nx>>16;
   *ry=ny>>16;
}

/* Transform an SVG point but return the raster coordinate in 16.16
 * (i.e. without the final >>16 shift).  Used by the bezier sampler
 * so it can subdivide in actual raster space with sub-pixel
 * precision and stop as soon as the chord is flat enough. */
static void Mxform_raw(struct Matrix *M, LONG x, LONG y, LONG *rx, LONG *ry)
{  if(M->b==0 && M->c==0)
   {  *rx = fmul(M->a,x) + M->e;
      *ry = fmul(M->d,y) + M->f;
   }
   else
   {  *rx = fmul(M->a,x) + fmul(M->c,y) + M->e;
      *ry = fmul(M->b,x) + fmul(M->d,y) + M->f;
   }
}

/* Parse an SVG transform attribute like
 *   "translate(50,30) rotate(45) scale(2)"
 * applying to the matrix in left-to-right order. */
static void Parsetransform(const UBYTE *str, struct Matrix *m)
{  UBYTE *p=(UBYTE *)str;
   UBYTE *end;
   if(!p) return;
   end=p;
   while(*end) end++;
   while(p<end)
   {  UBYTE *kw;
      LONG args[6];
      LONG nargs=0;
      BOOL ok;
      LONG cs,sn,tn;
      struct Matrix T;
      /* Pre-zero so reads beyond nargs are predictable and so SAS/C's
       * flow analyser does not flag args[] as possibly-uninitialised
       * when a transform reads further than its actual argument count
       * (e.g. translate(50) reads args[1] under the y default). */
      args[0]=args[1]=args[2]=args[3]=args[4]=args[5]=0;
      while(p<end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p==',')) p++;
      if(p>=end) break;
      kw=p;
      while(p<end && ((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z'))) p++;
      {  LONG kwlen=(LONG)(p-kw);
         while(p<end && (*p==' '||*p=='\t')) p++;
         if(p>=end || *p!='(') break;
         p++;
         while(nargs<6)
         {  args[nargs]=Parsefixed(&p,end,&ok);
            if(!ok) break;
            nargs++;
         }
         while(p<end && *p!=')') p++;
         if(p<end) p++;
         if(kwlen==9 && (kw[0]=='t'||kw[0]=='T'))
         {  /* translate(tx,ty) */
            Midentity(&T);
            T.e=args[0];
            if(nargs>1) T.f=args[1];
            Mcompose(m,m,&T);
         }
         else if(kwlen==5 && (kw[0]=='s'||kw[0]=='S') && (kw[1]=='c'||kw[1]=='C'))
         {  /* scale(sx[,sy]) */
            Midentity(&T);
            T.a=args[0];
            T.d=(nargs>1)?args[1]:args[0];
            Mcompose(m,m,&T);
         }
         else if(kwlen==6 && (kw[0]=='r'||kw[0]=='R'))
         {  /* rotate(angle [, cx, cy]) */
            cs=fcos_deg(args[0]);
            sn=fsin_deg(args[0]);
            Midentity(&T);
            T.a=cs;
            T.b=sn;
            T.c=-sn;
            T.d=cs;
            if(nargs>=3)
            {  struct Matrix Tt;
               Midentity(&Tt);
               Tt.e=args[1]; Tt.f=args[2];
               Mcompose(m,m,&Tt);
               Mcompose(m,m,&T);
               Tt.e=-args[1]; Tt.f=-args[2];
               Mcompose(m,m,&Tt);
            }
            else
            {  Mcompose(m,m,&T);
            }
         }
         else if(kwlen==6 && (kw[0]=='m'||kw[0]=='M'))
         {  /* matrix(a,b,c,d,e,f) */
            if(nargs==6)
            {  T.a=args[0]; T.b=args[1]; T.c=args[2]; T.d=args[3];
               T.e=args[4]; T.f=args[5];
               Mcompose(m,m,&T);
            }
         }
         else if(kwlen==5 && (kw[0]=='s'||kw[0]=='S') && (kw[1]=='k'||kw[1]=='K'))
         {  /* skewX/skewY */
            cs=fcos_deg(args[0]);
            sn=fsin_deg(args[0]);
            tn=fdivclamp(sn,cs);
            Midentity(&T);
            if(kw[4]=='X' || kw[4]=='x') T.c=tn;
            else                         T.b=tn;
            Mcompose(m,m,&T);
         }
      }
   }
}

/*--------------------------------------------------------------------*/
/* Style attribute parser                                             */
/*--------------------------------------------------------------------*/

/* Walk a CSS-style "k:v;k2:v2" string, calling the callback for each
 * pair.  Both key and value are temporarily NUL-terminated inside the
 * input string.  This is destructive but the input lives in the
 * pool-allocated buffer so it doesn't matter. */
static void Parsestyle(UBYTE *str,
   void (*cb)(UBYTE *,UBYTE *,void *), void *userdata)
{  UBYTE *p=str;
   if(!p) return;
   while(*p)
   {  UBYTE *key,*val,*keyend,*valend;
      UBYTE savekey,saveval;
      while(*p==' '||*p=='\t'||*p==';'||*p=='\n'||*p=='\r') p++;
      if(!*p) break;
      key=p;
      while(*p && *p!=':' && *p!=';') p++;
      if(*p!=':') { while(*p && *p!=';') p++; continue; }
      keyend=p;
      while(keyend>key && (keyend[-1]==' '||keyend[-1]=='\t')) keyend--;
      savekey=*keyend;
      *keyend=0;
      p++;
      while(*p==' '||*p=='\t') p++;
      val=p;
      while(*p && *p!=';') p++;
      valend=p;
      while(valend>val && (valend[-1]==' '||valend[-1]=='\t')) valend--;
      saveval=*valend;
      *valend=0;
      cb(key,val,userdata);
      *valend=saveval;
      *keyend=savekey;
      if(*p==';') p++;
   }
}

/*--------------------------------------------------------------------*/
/* Drawing helpers                                                    */
/*--------------------------------------------------------------------*/

/* These helpers talk to graphics.library directly using raster-pixel
 * coordinates.  We deliberately do NOT route through vrastport.lib
 * because its virtual-to-raster scaling uses a 64-bit signed multiply
 * whose register convention (especially in the bundled math64.a) is
 * subtle and has produced surprising results when paired with our
 * pixel-space input.  Going straight to graphics.library is far
 * easier to reason about and gives identical functionality for the
 * shapes we actually need (rects, lines, polygons, ellipses). */

/* Clip helpers: bitmap is 0..bmw-1, 0..bmh-1. */
static LONG Clip(LONG v, LONG lo, LONG hi)
{  if(v<lo) return lo;
   if(v>hi) return hi;
   return v;
}

/* Write one horizontal span using a paint context.  On Picasso96 deep
 * we write straight into the per-decoder R8G8B8 framebuffer (the whole
 * frame is committed to the bitmap once at the end of Parsertask);
 * gradient fills are evaluated per pixel and partial alpha is blended
 * with the existing pixel.  On palette bitmaps we go through
 * graphics.library RectFill which the caller has primed with SetAPen
 * - gradient fills are not supported there and the caller will have
 * fallen back to the averaged colour. */
static void Fillspan(struct Decoder *dec, LONG x0, LONG x1, LONG y,
   const struct Paintctx *pctx)
{  LONG w,i;
   UBYTE *p;
   UBYTE r,g,b;
   UBYTE sa;
   ULONG rgb;
   BOOL can_blend;
   if(x0>x1) return;
   if(y<0 || y>=dec->bmh) return;
   if(x0<0) x0=0;
   if(x1>=dec->bmw) x1=dec->bmw-1;
   w=x1-x0+1;
   if(w<=0) return;

   can_blend=(BOOL)((dec->quality & QF_ALPHA_BLEND) != 0);

   if(dec->decflags&DECOF_P96DEEP)
   {  p=dec->chunky + (ULONG)y*(ULONG)dec->chunkybpr + (ULONG)x0*3UL;

      if(pctx->has_grad && pctx->grad)
      {  const struct Gradient *grad=pctx->grad;
         LONG t,dt;
         if(grad->type==GRAD_TYPE_LINEAR)
         {  t=pctx->t_x0 + fmul(pctx->dt_dx,(LONG)x0<<16)
                        + fmul(pctx->dt_dy,(LONG)y<<16);
            dt=pctx->dt_dx;
         }
         else { t=0; dt=0; }
         for(i=0;i<w;i++)
         {  UBYTE pa;
            ULONG prgb;
            UBYTE eff;
            if(grad->type==GRAD_TYPE_LINEAR)
            {  LONG ct=t;
               if(ct<0) ct=0;
               if(ct>0x10000L) ct=0x10000L;
               prgb=Gradsample(grad,ct,&pa);
               t+=dt;
            }
            else
            {  prgb=Gradeval_pixel(pctx,(LONG)(x0+i),y,&pa);
            }
            eff=Combinealpha(pa,pctx->alpha);
            if(eff<ALPHA_SKIP) { p+=3; continue; }
            if(eff>=240 || !can_blend)
            {  /* Hard write (also used when adaptive quality has
                * forbidden alpha blending - the stop alpha is
                * effectively rounded to opaque). */
               p[0]=(UBYTE)((prgb>>16)&0xff);
               p[1]=(UBYTE)((prgb>>8)&0xff);
               p[2]=(UBYTE)(prgb&0xff);
            }
            else
            {  /* Source-over blend: out = src*a + dst*(255-a) */
               ULONG na=(ULONG)eff;
               ULONG ia=255UL-na;
               p[0]=(UBYTE)(((((prgb>>16)&0xff)*na) + ((ULONG)p[0]*ia) + 127UL)/255UL);
               p[1]=(UBYTE)(((((prgb>>8 )&0xff)*na) + ((ULONG)p[1]*ia) + 127UL)/255UL);
               p[2]=(UBYTE)((((prgb     &0xff)*na) + ((ULONG)p[2]*ia) + 127UL)/255UL);
            }
            p+=3;
         }
         return;
      }

      rgb=pctx->rgb;
      sa=pctx->alpha;
      if(sa>=240 || !can_blend)
      {  if(sa<ALPHA_SKIP) return;
         r=(UBYTE)((rgb>>16)&0xff);
         g=(UBYTE)((rgb>>8)&0xff);
         b=(UBYTE)(rgb&0xff);
         for(i=0;i<w;i++)
         {  p[0]=r; p[1]=g; p[2]=b;
            p+=3;
         }
      }
      else if(sa<ALPHA_SKIP) return;
      else
      {  ULONG na=(ULONG)sa;
         ULONG ia=255UL-na;
         ULONG sr=(rgb>>16)&0xff;
         ULONG sg=(rgb>>8)&0xff;
         ULONG sb=rgb&0xff;
         for(i=0;i<w;i++)
         {  p[0]=(UBYTE)((sr*na + (ULONG)p[0]*ia + 127UL)/255UL);
            p[1]=(UBYTE)((sg*na + (ULONG)p[1]*ia + 127UL)/255UL);
            p[2]=(UBYTE)((sb*na + (ULONG)p[2]*ia + 127UL)/255UL);
            p+=3;
         }
      }
      return;
   }

   /* Palette destination - the caller has already SetAPen'd the
    * solid colour (averaged colour for gradient fills) so we just
    * issue a horizontal RectFill. */
   RectFill(&dec->rp,x0,y,x1,y);
}

static void Plotpixel_rgb(struct Decoder *dec, LONG x, LONG y, ULONG rgb)
{  UBYTE *p;
   if(x<0 || x>=dec->bmw || y<0 || y>=dec->bmh) return;
   if(dec->decflags&DECOF_P96DEEP)
   {  p=dec->chunky + (ULONG)y*(ULONG)dec->chunkybpr + (ULONG)x*3UL;
      p[0]=(UBYTE)((rgb>>16)&0xff);
      p[1]=(UBYTE)((rgb>>8)&0xff);
      p[2]=(UBYTE)(rgb&0xff);
   }
   else
   {  WritePixel(&dec->rp,x,y);
   }
}

static void Drawline_rgb(struct Decoder *dec, LONG x0, LONG y0, LONG x1, LONG y1, ULONG rgb)
{  LONG dx,dy,sx,sy,err,e2;
   if(dec->decflags&DECOF_P96DEEP)
   {  dx=(x1>x0)?(x1-x0):(x0-x1);
      dy=(y1>y0)?(y1-y0):(y0-y1);
      sx=(x0<x1)?1:-1;
      sy=(y0<y1)?1:-1;
      err=dx-dy;
      for(;;)
      {  Plotpixel_rgb(dec,x0,y0,rgb);
         if(x0==x1 && y0==y1) break;
         e2=err<<1;
         if(e2>-dy) { err-=dy; x0+=sx; }
         if(e2<dx)  { err+=dx; y0+=sy; }
      }
   }
   else
   {  Move(&dec->rp,(WORD)x0,(WORD)y0);
      Draw(&dec->rp,(WORD)x1,(WORD)y1);
   }
}

/* Draw a filled ellipse by horizontal-scanline rasterisation.
 * Uses the standard midpoint algorithm.  Y axis points down (raster
 * convention).  cx,cy is the centre and rx,ry are the half-axes, all
 * in raster pixels. */
static void Drawellipse_fill(struct Decoder *dec, LONG cx, LONG cy, LONG rx, LONG ry, const struct Paintctx *pctx)
{  LONG bmw=dec->bmw;
   LONG bmh=dec->bmh;
   LONG x,y;
   LONG xl,xr,yt,yb;
   LONG rx2,ry2,twoRx2,twoRy2;
   LONG px,py;
   LONG p;
   LONG lasty=-1;
   if(rx<=0 || ry<=0) return;
   rx2=rx*rx;
   ry2=ry*ry;
   twoRx2=rx2<<1;
   twoRy2=ry2<<1;
   /* Region 1 */
   x=0;
   y=ry;
   p=(LONG)(ry2 - rx2*ry + (rx2+2)/4);
   px=0;
   py=twoRx2*y;
   while(px<py)
   {  if(y!=lasty)
      {  xl=Clip(cx-x,0,bmw-1);
         xr=Clip(cx+x,0,bmw-1);
         yt=cy-y;
         yb=cy+y;
         if(yt>=0 && yt<bmh && xl<=xr) Fillspan(dec,xl,xr,yt,pctx);
         if(yb>=0 && yb<bmh && yb!=yt && xl<=xr) Fillspan(dec,xl,xr,yb,pctx);
         lasty=y;
      }
      x++;
      px+=twoRy2;
      if(p<0)
      {  p+=ry2 + px;
      }
      else
      {  y--;
         py-=twoRx2;
         p+=ry2 + px - py;
      }
   }
   /* Region 2 - rounding constant is ry2/4 (derived from expanding
    * b2(x+1/2)2 + a2(y-1)2 - a2b2 at the region transition). */
   p=(LONG)(ry2*(x*x + x) + rx2*(y-1)*(y-1) - rx2*ry2 + (ry2+2)/4);
   while(y>=0)
   {  if(y!=lasty)
      {  xl=Clip(cx-x,0,bmw-1);
         xr=Clip(cx+x,0,bmw-1);
         yt=cy-y;
         yb=cy+y;
         if(yt>=0 && yt<bmh && xl<=xr) Fillspan(dec,xl,xr,yt,pctx);
         if(yb>=0 && yb<bmh && yb!=yt && xl<=xr) Fillspan(dec,xl,xr,yb,pctx);
         lasty=y;
      }
      y--;
      py-=twoRx2;
      if(p>0)
      {  p+=rx2 - py;
      }
      else
      {  x++;
         px+=twoRy2;
         p+=rx2 - py + px;
      }
   }
}

/* Stroke an ellipse outline.  Plots 4-symmetric pixels per midpoint
 * step using Bresenham.  Cheap and predictable. */
static void Drawellipse_stroke(struct Decoder *dec, LONG cx, LONG cy, LONG rx, LONG ry, ULONG rgb)
{  LONG bmw=dec->bmw;
   LONG bmh=dec->bmh;
   LONG x,y;
   LONG q[4][2];
   LONG k;
   LONG rx2,ry2,twoRx2,twoRy2;
   LONG px,py;
   LONG p;
   if(rx<=0 || ry<=0) return;
   rx2=rx*rx;
   ry2=ry*ry;
   twoRx2=rx2<<1;
   twoRy2=ry2<<1;
   x=0;
   y=ry;
   p=(LONG)(ry2 - rx2*ry + (rx2+2)/4);
   px=0;
   py=twoRx2*y;
   while(px<py)
   {  q[0][0]=cx+x; q[0][1]=cy+y;
      q[1][0]=cx-x; q[1][1]=cy+y;
      q[2][0]=cx+x; q[2][1]=cy-y;
      q[3][0]=cx-x; q[3][1]=cy-y;
      for(k=0;k<4;k++)
      {  if(q[k][0]>=0 && q[k][0]<bmw && q[k][1]>=0 && q[k][1]<bmh)
            Plotpixel_rgb(dec,q[k][0],q[k][1],rgb);
      }
      x++;
      px+=twoRy2;
      if(p<0)
      {  p+=ry2 + px;
      }
      else
      {  y--;
         py-=twoRx2;
         p+=ry2 + px - py;
      }
   }
   p=(LONG)(ry2*(x*x + x) + rx2*(y-1)*(y-1) - rx2*ry2 + (ry2+2)/4);
   while(y>=0)
   {  q[0][0]=cx+x; q[0][1]=cy+y;
      q[1][0]=cx-x; q[1][1]=cy+y;
      q[2][0]=cx+x; q[2][1]=cy-y;
      q[3][0]=cx-x; q[3][1]=cy-y;
      for(k=0;k<4;k++)
      {  if(q[k][0]>=0 && q[k][0]<bmw && q[k][1]>=0 && q[k][1]<bmh)
            Plotpixel_rgb(dec,q[k][0],q[k][1],rgb);
      }
      y--;
      py-=twoRx2;
      if(p>0)
      {  p+=rx2 - py;
      }
      else
      {  x++;
         px+=twoRy2;
         p+=rx2 - py + px;
      }
   }
}

/* Scanline-based polygon fill using the even-odd rule.
 *
 * The naive approach is O(rows * edges) - it walks every edge for
 * every scanline.  For Inkscape-style boundary polygons of a few
 * thousand vertices over a few hundred rows that is a million-plus
 * comparisons per shape, which dominated SVG render time on the
 * Kannur test map.
 *
 * This implementation uses the standard edge-binning + active edge
 * list trick instead:
 *
 *   1. For each edge, decide which scanline it first becomes
 *      relevant on and chain it into a per-row bucket.
 *   2. Walk scanlines top to bottom maintaining an "active edge
 *      list" of edges currently crossing y.
 *   3. On each row, add new edges from the bucket, then drop edges
 *      whose lower end has been reached, then compute x at y for
 *      each remaining active edge and pair them up.
 *
 * Complexity drops to O(edges) setup + O(rows + active_edges_per_row
 * * rows) work, which is typically ~50x faster than the naive form
 * for complex polygons and is essentially the same speed for tiny
 * rects since the linked-list per-row overhead is trivial. */
struct Polyedge
{  struct Polyedge *next;
   LONG yA;                 /* scanline at which edge becomes active   */
   LONG yB;                 /* scanline (exclusive) at which it ends   */
   LONG xA;                 /* x coordinate at row yA                   */
   LONG dx;                 /* xB - xA, signed                          */
   LONG dy;                 /* yB - yA, always positive                 */
};

/* Compound polygon fill.  Each subpath i is described by the
 * half-open vertex range
 *
 *   [ (i==0 ? 0 : subpath_ends[i-1]) .. subpath_ends[i] )
 *
 * inside `pts`.  Edges are generated within a single subpath and
 * wrap from each subpath's last vertex back to its FIRST vertex,
 * never bridging across subpaths.  The scanline stage then runs
 * the usual even-odd rule across all collected edges, which gives
 * the SVG "outer ring plus inner hole" idiom for free - the inner
 * subpath's edges flip parity across its interior and cut a hole
 * through the outer fill. */
static void Drawpolygon_fill_compound(struct Decoder *dec, struct Point32 *pts,
   const WORD *subpath_ends, WORD subpath_count, const struct Paintctx *pctx)
{  LONG bmw,bmh;
   LONG ymin,ymax,y;
   LONG nrows;
   LONG poolnext;
   LONG xi;
   LONG sx0,sx1;
   WORD i,j,k;
   WORD s;
   WORD start,end;
   WORD total;
   WORD nx;
   LONG xs[256];
   struct Polyedge *pool;
   struct Polyedge **buckets;
   struct Polyedge *active=NULL;
   struct Polyedge *e,*next_e;
   struct Polyedge **pnext;
   if(subpath_count<=0) return;
   total=subpath_ends[subpath_count-1];
   if(total<3) return;
   bmw=dec->bmw;
   bmh=dec->bmh;

   ymin=pts[0].y;
   ymax=pts[0].y;
   for(i=1;i<total;i++)
   {  if(pts[i].y<ymin) ymin=pts[i].y;
      if(pts[i].y>ymax) ymax=pts[i].y;
   }
   if(ymax<0 || ymin>=bmh) return;
   if(ymin<0) ymin=0;
   if(ymax>=bmh) ymax=bmh-1;
   if(ymin>ymax) return;
   nrows=ymax-ymin+1;

   pool=(struct Polyedge *)AllocVec(sizeof(struct Polyedge)*total,MEMF_PUBLIC);
   buckets=(struct Polyedge **)AllocVec(sizeof(struct Polyedge *)*nrows,
      MEMF_PUBLIC|MEMF_CLEAR);
   if(!pool || !buckets)
   {  if(pool) FreeVec(pool);
      if(buckets) FreeVec(buckets);
      return;
   }

   poolnext=0;
   for(s=0;s<subpath_count;s++)
   {  start=(s==0) ? 0 : subpath_ends[s-1];
      end=subpath_ends[s];
      /* Fill closes subpaths implicitly, but a filled contour still
       * needs an area.  Keep two-point subpaths for stroke handling
       * in Pathend; skip them here so a stroked line with inherited
       * fill does not become a one-pixel filled sliver. */
      if(end-start<3) continue;
      for(i=start;i<end;i++)
      {  LONG x0,y0,x1,y1;
         LONG eyA,eyB,exA;
         LONG edx;
         LONG bucket_idx;
         k=(WORD)((i+1<end) ? (i+1) : start);
         x0=pts[i].x; y0=pts[i].y;
         x1=pts[k].x; y1=pts[k].y;
         if(y0==y1) continue;          /* horizontal edge: ignored */
         if(y0<y1) { eyA=y0; eyB=y1; exA=x0; edx=x1-x0; }
         else      { eyA=y1; eyB=y0; exA=x1; edx=x0-x1; }
         if(eyB<=ymin || eyA>ymax) continue;
         e=&pool[poolnext++];
         e->yA=eyA;
         e->yB=eyB;
         e->xA=exA;
         e->dx=edx;
         e->dy=eyB-eyA;
         bucket_idx = (eyA<ymin) ? 0 : (eyA-ymin);
         e->next=buckets[bucket_idx];
         buckets[bucket_idx]=e;
      }
   }

   /* Sweep scanlines. */
   for(y=ymin;y<=ymax;y++)
   {  e=buckets[y-ymin];
      while(e)
      {  next_e=e->next;
         e->next=active;
         active=e;
         e=next_e;
      }
      pnext=&active;
      while(*pnext)
      {  if((*pnext)->yB<=y) *pnext=(*pnext)->next;
         else                pnext=&(*pnext)->next;
      }
      nx=0;
      for(e=active;e;e=e->next)
      {  xi=e->xA + ((y - e->yA) * e->dx) / e->dy;
         j=nx;
         while(j>0 && xs[j-1]>xi) { xs[j]=xs[j-1]; j--; }
         xs[j]=xi;
         if(nx<256) nx++;
      }
      for(i=0;i+1<nx;i+=2)
      {  sx0=xs[i];
         sx1=xs[i+1];
         if(sx0<0) sx0=0;
         if(sx1>=bmw) sx1=bmw-1;
         if(sx0<=sx1) Fillspan(dec,sx0,sx1,y,pctx);
      }
   }

   FreeVec(buckets);
   FreeVec(pool);
}

/* Single-subpath wrapper around the compound fill.  Callers that
 * draw a simple closed polygon (eg. wide-stroke quads, a single SVG
 * path with no compound holes) hand us a point array with one
 * implicit closing edge; this builds a one-entry subpath_ends and
 * dispatches to the compound code path. */
static void Drawpolygon_fill(struct Decoder *dec, struct Point32 *pts,
   WORD count, const struct Paintctx *pctx)
{  WORD ends[1];
   if(count<3) return;
   ends[0]=count;
   Drawpolygon_fill_compound(dec,pts,ends,1,pctx);
}

/* Draw a single thick segment as a four-vertex polygon perpendicular
 * to the segment direction.  halfw_px is the half-width in 16.16
 * raster pixels.  Caller is responsible for adding round caps/joins
 * via Drawellipse_fill at the endpoints if desired.
 *
 * The perpendicular vector is computed in pure integer pixel-space:
 * perp = (-dy, dx) * halfw / len.  dx/dy/halfw/len are all small
 * integers (raster pixels, single-byte stroke widths) so the
 * numerator stays well under 2^31 - lifting into 16.16 before the
 * multiply overflowed the signed LONG for any segment longer than
 * ~30 pixels and caused complex stroked paths to draw nothing. */
static void Drawthickseg(struct Decoder *dec, LONG x0, LONG y0,
   LONG x1, LONG y1, LONG halfw_px, const struct Paintctx *pctx)
{  LONG dx,dy;
   LONG len_sq,len;
   LONG halfw;
   LONG perp_x,perp_y;
   LONG num_x,num_y;
   struct Point32 pts[4];

   halfw=halfw_px>>16;
   if(halfw<1) halfw=1;

   dx=x1-x0;
   dy=y1-y0;
   if(dx==0 && dy==0)
   {  Drawellipse_fill(dec,x0,y0,halfw,halfw,pctx);
      return;
   }
   len_sq = dx*dx + dy*dy;
   if(len_sq<=0) return;
   len=Isqrt32(len_sq);
   if(len<=0) return;

   num_x = -dy * halfw;
   num_y =  dx * halfw;
   /* Rounded integer division - the signs of num_x / num_y can
    * differ from len (which is always positive), so add half-len
    * with matching sign to round to nearest rather than toward
    * zero. */
   if(num_x>=0) perp_x = (num_x + (len>>1)) / len;
   else         perp_x = (num_x - (len>>1)) / len;
   if(num_y>=0) perp_y = (num_y + (len>>1)) / len;
   else         perp_y = (num_y - (len>>1)) / len;
   if(perp_x==0 && perp_y==0)
   {  if(dy!=0) perp_x = (dy<0) ? 1 : -1;
      else      perp_y = (dx<0) ? -1 : 1;
   }

   pts[0].x=x0+perp_x; pts[0].y=y0+perp_y;
   pts[1].x=x1+perp_x; pts[1].y=y1+perp_y;
   pts[2].x=x1-perp_x; pts[2].y=y1-perp_y;
   pts[3].x=x0-perp_x; pts[3].y=y0-perp_y;
   Drawpolygon_fill(dec,pts,4,pctx);
}

/* Stroke a polyline with the current renderstate's stroke width and
 * colour.  Uses filled quads per segment plus filled disks at each
 * vertex for round joins and (for open polylines) round caps.
 *
 * Sub-pixel-thin strokes (final raster half-width below 1 pixel) drop
 * through to the cheap Bresenham fallback which writes pixels without
 * alpha blending - acceptable for hairlines where individual pixel
 * blending would be invisible anyway. */
static void Strokepolyline(struct Decoder *dec, struct Point32 *pts,
   WORD n, BOOL closed, struct Renderstate *rs)
{  LONG halfw_px;
   int halfw_int;
   struct Paintctx sctx;
   UBYTE eff;
   WORD i;

   if(n<2) return;

   halfw_px = Strokewidth_px(rs)>>1;
   halfw_int=(int)(halfw_px>>16);

   if(halfw_int<1)
   {  /* Sub-pixel: 1-pixel Bresenham fallback, ignoring alpha.
       * Drawline_rgb's palette branch issues Move/Draw against the
       * RastPort's current pen, so swap to strokepen first. */
      Setpen(dec,rs->strokepen);
      for(i=1;i<n;i++)
         Drawline_rgb(dec,pts[i-1].x,pts[i-1].y,
            pts[i].x,pts[i].y,rs->strokergb);
      if(closed && n>=3)
         Drawline_rgb(dec,pts[n-1].x,pts[n-1].y,
            pts[0].x,pts[0].y,rs->strokergb);
      return;
   }

   eff=Effectivestrokealpha(rs);
   if(eff<ALPHA_SKIP) return;
   Buildstrokectx(&sctx,dec,rs,eff);
   /* Palette destinations: Fillspan / Drawellipse_fill / RectFill
    * all draw with the active pen.  Switch from any previously
    * stale (fill) pen to the stroke pen now so the wide-stroke
    * quads and round caps come out in the right colour.  No-op on
    * P96 deep paths because those write directly into chunky. */
   Setpen(dec,rs->strokepen);

   for(i=1;i<n;i++)
      Drawthickseg(dec,pts[i-1].x,pts[i-1].y,
         pts[i].x,pts[i].y,halfw_px,&sctx);
   if(closed && n>=3)
      Drawthickseg(dec,pts[n-1].x,pts[n-1].y,
         pts[0].x,pts[0].y,halfw_px,&sctx);

   /* Round joins / caps - filled disks of the stroke half-width at
    * every vertex.  For closed loops every vertex is an interior
    * join; for open polylines the two endpoints become round caps. */
   for(i=0;i<n;i++)
      Drawellipse_fill(dec,pts[i].x,pts[i].y,
         halfw_int,halfw_int,&sctx);
}

/* Wide stroke of an axis-aligned ellipse: sample to a polyline and
 * defer to Strokepolyline.  When the stroke width is sub-pixel we
 * fall back to the cheap 1-pixel Bresenham outline. */
static void Strokeellipse(struct Decoder *dec, LONG cx, LONG cy,
   LONG rx, LONG ry, struct Renderstate *rs)
{  LONG halfw_px;
   int halfw_int;
   int steps;
   int i;
   LONG max_r;
   LONG step_ang;
   struct Point32 pts[128];

   if(rx<=0 || ry<=0) return;

   halfw_px = Strokewidth_px(rs)>>1;
   halfw_int=(int)(halfw_px>>16);
   if(halfw_int<1)
   {  /* Palette: Plotpixel_rgb -> WritePixel needs the strokepen
       * set first.  Harmless on P96 deep paths. */
      Setpen(dec,rs->strokepen);
      Drawellipse_stroke(dec,cx,cy,rx,ry,rs->strokergb);
      return;
   }

   max_r=(rx>ry)?rx:ry;
   steps=(int)(max_r/2);
   if(steps<16) steps=16;
   if(steps>128) steps=128;

   step_ang=(360L<<16)/steps;
   for(i=0;i<steps;i++)
   {  LONG ang=step_ang*i;
      LONG ct=fcos_deg(ang);
      LONG st=fsin_deg(ang);
      LONG xp=fmul((LONG)rx<<16,ct);
      LONG yp=fmul((LONG)ry<<16,st);
      pts[i].x=cx + ((xp + ((xp<0)?-0x8000L:0x8000L))>>16);
      pts[i].y=cy + ((yp + ((yp<0)?-0x8000L:0x8000L))>>16);
   }

   Strokepolyline(dec,pts,(WORD)steps,TRUE,rs);
}

/*--------------------------------------------------------------------*/
/* Points list parsing                                                */
/*--------------------------------------------------------------------*/

/* Parse the "points" attribute of polygon/polyline into a transformed
 * Point32 array allocated from the pool.  Returns vertex count or 0. */
static WORD Parsepoints(struct Decoder *dec, struct Matrix *M, UBYTE *str,
   struct Point32 **outpts, struct Bbox *bb)
{  UBYTE *p=str;
   UBYTE *end;
   WORD count=0;
   WORD capacity=64;
   struct Point32 *pts;
   BOOL ok;
   *outpts=NULL;
   if(bb) bb->valid=FALSE;
   if(!p) return 0;
   end=p;
   while(*end) end++;
   pts=(struct Point32 *)AllocPooled(dec->pool,sizeof(struct Point32)*capacity);
   if(!pts) return 0;
   while(p<end && count<POLY_MAX_VERTS)
   {  LONG fx,fy,rx,ry;
      fx=Parsefixed(&p,end,&ok);
      if(!ok) break;
      fy=Parsefixed(&p,end,&ok);
      if(!ok) break;
      if(count>=capacity)
      {  WORD newcap=capacity*2;
         struct Point32 *npts;
         if(newcap>POLY_MAX_VERTS) newcap=POLY_MAX_VERTS;
         npts=(struct Point32 *)AllocPooled(dec->pool,sizeof(struct Point32)*newcap);
         if(!npts) break;
         memcpy(npts,pts,sizeof(struct Point32)*count);
         /* Old pts leaks until pool delete - acceptable. */
         pts=npts;
         capacity=newcap;
      }
      /* Track the user-space bounding box BEFORE transformation so
       * objectBoundingBox gradients (and any other bbox-driven
       * decisions) see the geometry the SVG author wrote, not the
       * raster-space projection of it.  Without this, a polygon
       * rendered through a rotated CTM gets a rotated bbox and the
       * gradient lands in the wrong place. */
      if(bb)
      {  if(!bb->valid)
         {  bb->xmin=bb->xmax=fx;
            bb->ymin=bb->ymax=fy;
            bb->valid=TRUE;
         }
         else
         {  if(fx<bb->xmin) bb->xmin=fx;
            if(fx>bb->xmax) bb->xmax=fx;
            if(fy<bb->ymin) bb->ymin=fy;
            if(fy>bb->ymax) bb->ymax=fy;
         }
      }
      Mxform(M,fx,fy,&rx,&ry);
      pts[count].x=rx;
      pts[count].y=ry;
      count++;
   }
   *outpts=pts;
   return count;
}

/*--------------------------------------------------------------------*/
/* Path data parsing                                                  */
/*--------------------------------------------------------------------*/

/* Path emitter - accumulates transformed raster-space points into a
 * subpath that is rendered (filled or stroked) on Z/end. */
struct Pathemit
{  struct Decoder *dec;
   struct Renderstate *rs;
   struct Matrix *M;
   struct Point32 *pts;
   WORD count;
   WORD capacity;
   LONG startx,starty;    /* Subpath start in SVG 16.16 (for Z) */
   LONG curx,cury;        /* Current point in SVG 16.16 */
   LONG ctrlx,ctrly;      /* Last cubic ctrl in SVG 16.16 (for S/T continuation) */
   BOOL hasctrl;
   BOOL subpathopen;
   BOOL truncated;           /* TRUE if vertex cap was hit */
   BOOL bbox_init;
   LONG bbox_xmin,bbox_ymin;
   LONG bbox_xmax,bbox_ymax;
   /* User-space bounding box in 16.16 SVG units.  Accumulated by
    * Pebbox_update at every Parsepath endpoint, persists across
    * subpath boundaries so an objectBoundingBox gradient sees the
    * whole path's extent. */
   BOOL ubbox_init;
   LONG ubbox_xmin,ubbox_ymin;
   LONG ubbox_xmax,ubbox_ymax;
   /* Compound-path tracking.  pe->pts keeps growing across M
    * boundaries; subpath_ends[i] is one-past-last vertex of
    * subpath i, subpath_closed[i] is its per-subpath Z flag.
    * Pathflush only RECORDS into this table; Pathend then runs
    * the compound even-odd fill followed by per-subpath stroke,
    * in that order, so SVG paint order (fill, then stroke on top)
    * is preserved on shapes that carry both. */
   WORD subpath_count;
   WORD subpath_ends[MAX_SUBPATHS_PER_PATH];
   UBYTE subpath_closed[MAX_SUBPATHS_PER_PATH];
   /* TRUE if at least one subpath ended with Z.  This is retained for
    * diagnostics and stroke state, not for fill eligibility: the SVG
    * fill algorithm always treats subpaths as closed. */
   BOOL any_closed;
};

static void Pathreset(struct Pathemit *pe)
{  pe->count=0;
   pe->hasctrl=FALSE;
   pe->truncated=FALSE;
   pe->bbox_init=FALSE;
   pe->ubbox_init=FALSE;
   pe->subpath_count=0;
   pe->any_closed=FALSE;
}

/* Extend the path emitter's user-space bbox to include (x,y).
 * Coordinates are 16.16 SVG units, taken from Parsepath at each
 * command's endpoint or control point. */
static void Pebbox_update(struct Pathemit *pe, LONG x, LONG y)
{  if(!pe->ubbox_init)
   {  pe->ubbox_xmin=pe->ubbox_xmax=x;
      pe->ubbox_ymin=pe->ubbox_ymax=y;
      pe->ubbox_init=TRUE;
      return;
   }
   if(x<pe->ubbox_xmin) pe->ubbox_xmin=x;
   if(x>pe->ubbox_xmax) pe->ubbox_xmax=x;
   if(y<pe->ubbox_ymin) pe->ubbox_ymin=y;
   if(y>pe->ubbox_ymax) pe->ubbox_ymax=y;
}

/* Append a raster-pixel point to the emitter.  Skips consecutive
 * duplicates WITHIN the current subpath (a moveto starting vertex
 * that coincides with the previous subpath's last point must NOT
 * be dropped, otherwise the compound fill loses a closing edge),
 * grows the array on demand, and accumulates the path's raster
 * bounding box so Pathflush can skip work for subpaths off-screen. */
static void Pathemitpt_raster(struct Pathemit *pe, LONG rx, LONG ry)
{  WORD subpath_start;
   if(pe->count>=POLY_MAX_VERTS) { pe->truncated=TRUE; return; }
   if(!pe->bbox_init)
   {  pe->bbox_xmin=pe->bbox_xmax=rx;
      pe->bbox_ymin=pe->bbox_ymax=ry;
      pe->bbox_init=TRUE;
   }
   else
   {  if(rx<pe->bbox_xmin) pe->bbox_xmin=rx;
      if(rx>pe->bbox_xmax) pe->bbox_xmax=rx;
      if(ry<pe->bbox_ymin) pe->bbox_ymin=ry;
      if(ry>pe->bbox_ymax) pe->bbox_ymax=ry;
   }
   subpath_start=(pe->subpath_count==0) ? 0
                                        : pe->subpath_ends[pe->subpath_count-1];
   if(pe->count>subpath_start
   && pe->pts[pe->count-1].x==rx
   && pe->pts[pe->count-1].y==ry) return;
   if(pe->count>=pe->capacity)
   {  WORD newcap=pe->capacity*2;
      struct Point32 *npts;
      if(newcap>POLY_MAX_VERTS || newcap<0) newcap=POLY_MAX_VERTS;
      npts=(struct Point32 *)AllocPooled(pe->dec->pool,sizeof(struct Point32)*newcap);
      if(!npts) return;
      memcpy(npts,pe->pts,sizeof(struct Point32)*pe->count);
      pe->pts=npts;
      pe->capacity=newcap;
   }
   pe->pts[pe->count].x=rx;
   pe->pts[pe->count].y=ry;
   pe->count++;
}

/* Transform an SVG point and append it. */
static void Pathemitpt(struct Pathemit *pe, LONG svgx, LONG svgy)
{  LONG rx,ry;
   Mxform(pe->M,svgx,svgy,&rx,&ry);
   Pathemitpt_raster(pe,rx,ry);
}

/* Finalise the currently-open subpath.
 *
 * Pathflush is RECORD-ONLY: it never draws.  It performs the
 * off-screen cull and either appends the just-completed subpath to
 * (subpath_ends, subpath_closed) for later drawing by Pathend, or
 * rolls pe->count back to discard a degenerate / culled / truncated
 * subpath.  Deferring fill+stroke until Pathend is needed for two
 * reasons:
 *
 *   1) Multi-subpath compound fills (outer ring + inner hole, the
 *      SVG "O" / yin-yang idiom) must be drawn as one polygon so
 *      the even-odd rule produces the hole; per-subpath fill would
 *      overpaint it.
 *
 *   2) SVG paint order is fill-then-stroke.  If Pathflush stroked
 *      each subpath immediately and Pathend filled at the end, the
 *      fill would erase the stroke on every shape that uses both.
 *
 * closepath is the standard SVG meaning: TRUE iff the subpath
 * ended with Z.  It is kept per-subpath for stroke and OR'd into
 * pe->any_closed for the fill gate. */
static void Pathflush(struct Pathemit *pe, BOOL closepath)
{  WORD subpath_start;
   WORD subpath_len;
   WORD i;
   LONG sbxmin,sbxmax,sbymin,sbymax;
   LONG margin;
   LONG w;

   subpath_start=(pe->subpath_count==0) ? 0
                                        : pe->subpath_ends[pe->subpath_count-1];
   subpath_len=(WORD)(pe->count-subpath_start);

   if(subpath_len<2)
   {  pe->count=subpath_start;
      pe->subpathopen=FALSE;
      pe->bbox_init=FALSE;
      return;
   }

   /* Off-screen cull (per-subpath bbox computed on the fly - the
    * Pathemit's bbox_* is path-cumulative).  Subpaths whose raster
    * bbox falls entirely outside the canvas plus a stroke half-width
    * slop margin cannot contribute to any visible scanline and can
    * therefore be dropped without affecting even-odd parity at any
    * visible row. */
   sbxmin=sbxmax=pe->pts[subpath_start].x;
   sbymin=sbymax=pe->pts[subpath_start].y;
   for(i=(WORD)(subpath_start+1);i<pe->count;i++)
   {  if(pe->pts[i].x<sbxmin) sbxmin=pe->pts[i].x;
      if(pe->pts[i].x>sbxmax) sbxmax=pe->pts[i].x;
      if(pe->pts[i].y<sbymin) sbymin=pe->pts[i].y;
      if(pe->pts[i].y>sbymax) sbymax=pe->pts[i].y;
   }
   margin=0;
   if(pe->rs->strokevalid)
   {  w=Strokewidth_px(pe->rs);
      margin=(w>>17)+1;
   }
   if(sbxmax<-margin
   || sbxmin>=pe->dec->bmw+margin
   || sbymax<-margin
   || sbymin>=pe->dec->bmh+margin)
   {  pe->count=subpath_start;
      pe->subpathopen=FALSE;
      pe->bbox_init=FALSE;
      return;
   }

   /* Record the subpath for Pathend.  If we ran out of subpath slots
    * or path emission has been truncated, drop the subpath - its
    * vertex list is unreliable for both fill and stroke. */
   if(pe->subpath_count<MAX_SUBPATHS_PER_PATH && !pe->truncated)
   {  pe->subpath_ends[pe->subpath_count]=pe->count;
      pe->subpath_closed[pe->subpath_count]=(UBYTE)(closepath?1:0);
      pe->subpath_count++;
      if(closepath) pe->any_closed=TRUE;
   }
   else
   {  pe->count=subpath_start;
   }

   pe->subpathopen=FALSE;
   pe->bbox_init=FALSE;
}

/* Path finaliser.  Performs the deferred drawing of every subpath
 * collected by Pathflush:
 *
 *   1) compound even-odd fill across all subpaths at once.  Per SVG,
 *      fill always closes each subpath implicitly; `Z` is only needed
 *      to close the stroke contour.  Many Illustrator/Inkscape icons
 *      return close to the start point without issuing `Z`, and those
 *      must still fill.
 *
 *   2) per-subpath stroke, using each subpath's own Z flag for
 *      whether to close the polyline.
 *
 * Drawing order is fill-then-stroke per SVG paint order so a stroke
 * on a filled shape sits on top of the fill rather than being
 * overpainted by it.  Safe to call on an empty / already-finalised
 * emitter. */
static void Pathend(struct Pathemit *pe)
{  struct Paintctx pctx;
   struct Bbox bb;
   WORD s;
   WORD start,end;

   if(pe->subpath_count==0)
   {  pe->count=0;
      pe->subpathopen=FALSE;
      pe->truncated=FALSE;
      pe->bbox_init=FALSE;
      pe->ubbox_init=FALSE;
      pe->any_closed=FALSE;
      return;
   }

   if(pe->rs->fillvalid && !pe->truncated)
   {  bb.valid=pe->ubbox_init;
      bb.xmin=pe->ubbox_xmin;
      bb.ymin=pe->ubbox_ymin;
      bb.xmax=pe->ubbox_xmax;
      bb.ymax=pe->ubbox_ymax;
      Buildfillctx(&pctx,pe->dec,pe->rs,Effectivefillalpha(pe->rs),&bb);
      /* Palette destinations need the active pen set before
       * Fillspan because Fillspan's palette branch issues RectFill
       * against the current RastPort pen.  Buildfillctx already
       * populated pctx->rgb with the (averaged) solid colour. */
      Setpen(pe->dec,pe->rs->fillpen);
      Drawpolygon_fill_compound(pe->dec,pe->pts,
         pe->subpath_ends,pe->subpath_count,&pctx);
   }

   if(pe->rs->strokevalid)
   {  for(s=0;s<pe->subpath_count;s++)
      {  start=(s==0) ? 0 : pe->subpath_ends[s-1];
         end=pe->subpath_ends[s];
         if(end-start<2) continue;
         Strokepolyline(pe->dec,&pe->pts[start],(WORD)(end-start),
            (BOOL)(pe->subpath_closed[s]?TRUE:FALSE),pe->rs);
      }
   }

   pe->count=0;
   pe->subpath_count=0;
   pe->any_closed=FALSE;
   pe->subpathopen=FALSE;
   pe->truncated=FALSE;
   pe->bbox_init=FALSE;
   pe->ubbox_init=FALSE;
}

/* Adaptive de Casteljau subdivision for cubic and quadratic Beziers.
 *
 * We sample in raster space (16.16 fixed point, the *_raw transform
 * preserves all 32 bits) so the flatness test directly measures the
 * deviation in pixels.  Each subdivision step is a small handful of
 * integer averages - no fmul, no double, no per-sample matrix work.
 *
 * Subdivision stops as soon as both control points sit within ~1
 * pixel of the chord, so smooth long curves emit just a few line
 * segments while tight bends are sampled more densely.  Depth is
 * capped at 10 (=> at most 1024 segments per curve) which is far
 * more than any real SVG icon needs.
 *
 * The flatness predicate uses 2*signed-triangle-area = cross
 * product as a proxy for perpendicular distance times chord
 * length.  Comparing |cross| <= chord (where chord = |dx|+|dy| is
 * the L1 norm) is conservative and keeps the math in 32-bit
 * integers for raster bitmaps up to ~32K x 32K. */

#define BEZIER_MAX_DEPTH   10

static BOOL Cubic_flat(LONG x0,LONG y0, LONG x1,LONG y1,
   LONG x2,LONG y2, LONG x3,LONG y3)
{  LONG dx,dy,ux,uy,vx,vy;
   LONG c1,c2,chord;
   /* Reduce to integer pixels so cross products fit comfortably in
    * a signed LONG even for the largest paths we handle. */
   dx=(x3-x0)>>16;
   dy=(y3-y0)>>16;
   ux=(x1-x0)>>16;
   uy=(y1-y0)>>16;
   vx=(x2-x0)>>16;
   vy=(y2-y0)>>16;
   c1=ux*dy - uy*dx; if(c1<0) c1=-c1;
   c2=vx*dy - vy*dx; if(c2<0) c2=-c2;
   chord=(dx<0?-dx:dx)+(dy<0?-dy:dy);
   if(chord<1) chord=1;
   return (BOOL)(c1<=chord && c2<=chord);
}

static void Bezier3_rec(struct Pathemit *pe,
   LONG x0,LONG y0, LONG x1,LONG y1,
   LONG x2,LONG y2, LONG x3,LONG y3, int depth)
{  LONG q0x,q0y,q1x,q1y,q2x,q2y;
   LONG r0x,r0y,r1x,r1y;
   LONG sx,sy;
   if(depth>=BEZIER_MAX_DEPTH || Cubic_flat(x0,y0,x1,y1,x2,y2,x3,y3))
   {  Pathemitpt_raster(pe, x3>>16, y3>>16);
      return;
   }
   q0x=(x0+x1)>>1; q0y=(y0+y1)>>1;
   q1x=(x1+x2)>>1; q1y=(y1+y2)>>1;
   q2x=(x2+x3)>>1; q2y=(y2+y3)>>1;
   r0x=(q0x+q1x)>>1; r0y=(q0y+q1y)>>1;
   r1x=(q1x+q2x)>>1; r1y=(q1y+q2y)>>1;
   sx =(r0x+r1x)>>1; sy =(r0y+r1y)>>1;
   Bezier3_rec(pe,x0,y0,q0x,q0y,r0x,r0y,sx,sy,depth+1);
   Bezier3_rec(pe,sx,sy,r1x,r1y,q2x,q2y,x3,y3,depth+1);
}

/* Public entry point for cubics - transforms the four control
 * points once, then recurses entirely in raster space. */
static void Bezier3(struct Pathemit *pe, LONG x0, LONG y0,
   LONG x1, LONG y1, LONG x2, LONG y2, LONG x3, LONG y3)
{  LONG rx0,ry0,rx1,ry1,rx2,ry2,rx3,ry3;
   Mxform_raw(pe->M,x0,y0,&rx0,&ry0);
   Mxform_raw(pe->M,x1,y1,&rx1,&ry1);
   Mxform_raw(pe->M,x2,y2,&rx2,&ry2);
   Mxform_raw(pe->M,x3,y3,&rx3,&ry3);
   Bezier3_rec(pe,rx0,ry0,rx1,ry1,rx2,ry2,rx3,ry3,0);
}

static BOOL Quad_flat(LONG x0,LONG y0, LONG x1,LONG y1, LONG x2,LONG y2)
{  LONG dx,dy,ux,uy,c1,chord;
   dx=(x2-x0)>>16;
   dy=(y2-y0)>>16;
   ux=(x1-x0)>>16;
   uy=(y1-y0)>>16;
   c1=ux*dy - uy*dx; if(c1<0) c1=-c1;
   chord=(dx<0?-dx:dx)+(dy<0?-dy:dy);
   if(chord<1) chord=1;
   return (BOOL)(c1<=chord);
}

static void Bezier2_rec(struct Pathemit *pe,
   LONG x0,LONG y0, LONG x1,LONG y1, LONG x2,LONG y2, int depth)
{  LONG q0x,q0y,q1x,q1y;
   LONG sx,sy;
   if(depth>=BEZIER_MAX_DEPTH || Quad_flat(x0,y0,x1,y1,x2,y2))
   {  Pathemitpt_raster(pe, x2>>16, y2>>16);
      return;
   }
   q0x=(x0+x1)>>1; q0y=(y0+y1)>>1;
   q1x=(x1+x2)>>1; q1y=(y1+y2)>>1;
   sx=(q0x+q1x)>>1; sy=(q0y+q1y)>>1;
   Bezier2_rec(pe,x0,y0,q0x,q0y,sx,sy,depth+1);
   Bezier2_rec(pe,sx,sy,q1x,q1y,x2,y2,depth+1);
}

static void Bezier2(struct Pathemit *pe, LONG x0, LONG y0,
   LONG x1, LONG y1, LONG x2, LONG y2)
{  LONG rx0,ry0,rx1,ry1,rx2,ry2;
   Mxform_raw(pe->M,x0,y0,&rx0,&ry0);
   Mxform_raw(pe->M,x1,y1,&rx1,&ry1);
   Mxform_raw(pe->M,x2,y2,&rx2,&ry2);
   Bezier2_rec(pe,rx0,ry0,rx1,ry1,rx2,ry2,0);
}

/* Decompose an SVG elliptical arc segment into a polyline using the
 * W3C SVG 1.1 endpoint-to-centre parameterisation (Appendix F.6.5
 * and F.6.6).  Handles radius correction for arcs whose endpoints
 * lie outside the requested ellipse, samples the resulting elliptic
 * arc at an angle resolution proportional to the raster-space arc
 * length, and finishes with an exact snap to the requested endpoint
 * so a subsequent Z closes back to the right vertex.
 *
 * (x0,y0) is the current point, (x1,y1) the target endpoint, both
 * in SVG user-space 16.16.  rx/ry are the requested radii (same
 * units), x_axis_rot_deg the rotation in degrees (16.16), and the
 * large_arc / sweep flags pick which of the four possible arcs to
 * draw.  The centre-form derivation here avoids the algebraic
 * factor sqrt((rx^2 ry^2 - rx^2 y'^2 - ry^2 x'^2) / (rx^2 y'^2 +
 * ry^2 x'^2)) - the rx^2 ry^2 product overflows 32-bit 16.16
 * arithmetic even for modest icon arcs - and uses the equivalent
 * lambda-normalised form sqrt((1-lam)/lam) instead. */
static void Arcsegment(struct Pathemit *pe,
   LONG x0, LONG y0,
   LONG rx, LONG ry, LONG x_axis_rot_deg,
   BOOL large_arc, BOOL sweep,
   LONG x1, LONG y1)
{  LONG cosphi,sinphi;
   LONG dx_2,dy_2;
   LONG x1p,y1p;
   LONG rxsq,rysq;
   LONG x1psq,y1psq;
   LONG lam;
   LONG s;
   LONG factor;
   LONG cxp,cyp;
   LONG center_x,center_y;
   LONG theta1,theta2,dtheta;
   LONG step_size;
   LONG raster_x0,raster_y0;
   LONG raster_x1,raster_y1;
   LONG raster_cx,raster_cy;
   LONG raster_rx,raster_ry;
   LONG abs_dtheta;
   LONG arc_len_est;
   LONG max_rad;
   LONG ux,uy,vx,vy;
   int steps;
   int i;

   if(x0==x1 && y0==y1) return;
   if(rx==0 || ry==0)
   {  Pathemitpt(pe,x1,y1);
      return;
   }
   if(rx<0) rx=-rx;
   if(ry<0) ry=-ry;

   cosphi=fcos_deg(x_axis_rot_deg);
   sinphi=fsin_deg(x_axis_rot_deg);

   /* F.6.5.1: half-diff in original frame rotated into the
    * x-axis-aligned ellipse frame. */
   dx_2=(x0-x1)>>1;
   dy_2=(y0-y1)>>1;
   x1p =  fmul(cosphi,dx_2) + fmul(sinphi,dy_2);
   y1p = -fmul(sinphi,dx_2) + fmul(cosphi,dy_2);

   rxsq=fmul(rx,rx);
   rysq=fmul(ry,ry);
   x1psq=fmul(x1p,x1p);
   y1psq=fmul(y1p,y1p);
   if(rxsq<=0 || rysq<=0)
   {  Pathemitpt(pe,x1,y1);
      return;
   }

   /* F.6.6.2: scale radii up if the endpoints lie outside the
    * requested ellipse. */
   lam=fdiv(x1psq,rxsq) + fdiv(y1psq,rysq);
   if(lam>(1L<<16))
   {  s=fsqrt(lam);
      rx=fmul(rx,s);
      ry=fmul(ry,s);
   }

   /* F.6.5.2: centre coordinates in the rotated frame.  The direct
    * SVG formula multiplies rx^2 by ry^2 which overflows 32-bit
    * 16.16 arithmetic even for modest icon arcs.  The equivalent
    * sqrt((1-lam)/lam) form below stays in range as long as lam
    * itself fits, which it always does (lam was just computed
    * above by 16.16 fdivs). */
   if(lam<=0)
   {  Pathemitpt(pe,x1,y1);
      return;
   }
   if(lam>(1L<<16)) lam=(1L<<16);
   factor=fdiv((1L<<16)-lam,lam);
   if(factor<0) factor=0;
   factor=fsqrt(factor);
   if(large_arc==sweep) factor=-factor;

   cxp =  fmul(factor, fdiv(fmul(rx,y1p),ry));
   cyp = -fmul(factor, fdiv(fmul(ry,x1p),rx));

   /* F.6.5.3: centre in the original (un-rotated) frame. */
   center_x = fmul(cosphi,cxp) - fmul(sinphi,cyp) + ((x0+x1)>>1);
   center_y = fmul(sinphi,cxp) + fmul(cosphi,cyp) + ((y0+y1)>>1);

   /* F.6.5.4-6: start and sweep angles. */
   ux=fdiv(x1p-cxp,rx);
   uy=fdiv(y1p-cyp,ry);
   vx=fdiv(-x1p-cxp,rx);
   vy=fdiv(-y1p-cyp,ry);
   theta1=fatan2_deg(uy,ux);
   theta2=fatan2_deg(vy,vx);
   dtheta=theta2-theta1;
   if(!sweep && dtheta>0) dtheta-=(360L<<16);
   if( sweep && dtheta<0) dtheta+=(360L<<16);

   /* Pick a sample count proportional to the raster-space arc
    * length using the larger transformed radius and the absolute
    * angular extent. */
   Mxform(pe->M,center_x,center_y,&raster_cx,&raster_cy);
   Mxform(pe->M,center_x+rx,center_y,&raster_x0,&raster_y0);
   raster_rx=raster_x0-raster_cx;
   if(raster_rx<0) raster_rx=-raster_rx;
   Mxform(pe->M,center_x,center_y+ry,&raster_x1,&raster_y1);
   raster_ry=raster_y1-raster_cy;
   if(raster_ry<0) raster_ry=-raster_ry;
   max_rad=(raster_rx>raster_ry)?raster_rx:raster_ry;
   if(max_rad<1) max_rad=1;

   abs_dtheta=(dtheta>0)?dtheta:-dtheta;
   /* arc_len ~ max_rad * angle_in_radians = max_rad * dtheta_deg *
    * pi/180.  Approximate pi/180 as 1145/65536 in 16.16; this
    * stays in 32-bit range as long as max_rad < ~30000 (raster
    * pixels) and abs_dtheta < 23.6M (full 360 degrees). */
   arc_len_est=(max_rad * (abs_dtheta>>16) * 1145L)>>16;
   if(arc_len_est<8) arc_len_est=8;
   steps=(int)(arc_len_est/3);
   if(steps<4) steps=4;
   if(steps>128) steps=128;

   step_size=dtheta/steps;
   for(i=1;i<=steps;i++)
   {  LONG t_param=theta1 + step_size*i;
      LONG ct=fcos_deg(t_param);
      LONG st=fsin_deg(t_param);
      LONG x_local=fmul(rx,ct);
      LONG y_local=fmul(ry,st);
      LONG x_arc=fmul(cosphi,x_local) - fmul(sinphi,y_local) + center_x;
      LONG y_arc=fmul(sinphi,x_local) + fmul(cosphi,y_local) + center_y;
      Pathemitpt(pe,x_arc,y_arc);
   }
   /* Snap the last sample exactly to the requested endpoint - the
    * angular interpolation accumulates a few LSBs of error and a
    * subsequent Z should still close back to the right vertex.
    * Compare against the raster-transformed endpoint because pe->pts
    * is in raster space, not user space. */
   Mxform(pe->M,x1,y1,&raster_x0,&raster_y0);
   if(pe->count>0 && (pe->pts[pe->count-1].x!=raster_x0
                   || pe->pts[pe->count-1].y!=raster_y0))
   {  Pathemitpt(pe,x1,y1);
   }
}

/* Parse and emit an SVG path 'd' attribute. */
static void Parsepath(struct Pathemit *pe, UBYTE *d)
{  UBYTE *p=d;
   UBYTE *end;
   UBYTE cmd=0;
   BOOL relative;
   BOOL ok;
   LONG a,b,c,d0,e,f,g;
   if(!p) return;
   end=p;
   while(*end) end++;
   pe->curx=0; pe->cury=0;
   pe->startx=0; pe->starty=0;
   pe->subpathopen=FALSE;
   while(p<end)
   {  while(p<end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p==',')) p++;
      if(p>=end) break;
      if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z'))
      {  cmd=*p++;
      }
      relative=(cmd>='a'&&cmd<='z');
      switch(cmd|0x20)
      {  case 'm':
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            b=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative) { a+=pe->curx; b+=pe->cury; }
            if(pe->subpathopen) Pathflush(pe,FALSE);
            pe->curx=a; pe->cury=b;
            pe->startx=a; pe->starty=b;
            Pathemitpt(pe,a,b);
            Pebbox_update(pe,a,b);
            pe->subpathopen=TRUE;
            pe->hasctrl=FALSE;
            cmd=relative?'l':'L';
            break;
         case 'l':
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            b=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative) { a+=pe->curx; b+=pe->cury; }
            Pathemitpt(pe,a,b);
            Pebbox_update(pe,a,b);
            pe->curx=a; pe->cury=b;
            pe->hasctrl=FALSE;
            break;
         case 'h':
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative) a+=pe->curx;
            Pathemitpt(pe,a,pe->cury);
            Pebbox_update(pe,a,pe->cury);
            pe->curx=a;
            pe->hasctrl=FALSE;
            break;
         case 'v':
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative) a+=pe->cury;
            Pathemitpt(pe,pe->curx,a);
            Pebbox_update(pe,pe->curx,a);
            pe->cury=a;
            pe->hasctrl=FALSE;
            break;
         case 'z':
            if(pe->subpathopen)
            {  Pathflush(pe,TRUE);
               pe->curx=pe->startx; pe->cury=pe->starty;
            }
            pe->hasctrl=FALSE;
            break;
         case 'c':
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            b=Parsefixed(&p,end,&ok); if(!ok) goto done;
            c=Parsefixed(&p,end,&ok); if(!ok) goto done;
            d0=Parsefixed(&p,end,&ok); if(!ok) goto done;
            e=Parsefixed(&p,end,&ok); if(!ok) goto done;
            f=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative)
            {  a+=pe->curx; b+=pe->cury;
               c+=pe->curx; d0+=pe->cury;
               e+=pe->curx; f+=pe->cury;
            }
            Bezier3(pe,pe->curx,pe->cury,a,b,c,d0,e,f);
            Pebbox_update(pe,a,b);
            Pebbox_update(pe,c,d0);
            Pebbox_update(pe,e,f);
            pe->ctrlx=c; pe->ctrly=d0;
            pe->hasctrl=TRUE;
            pe->curx=e; pe->cury=f;
            break;
         case 's':
            c=Parsefixed(&p,end,&ok); if(!ok) goto done;
            d0=Parsefixed(&p,end,&ok); if(!ok) goto done;
            e=Parsefixed(&p,end,&ok); if(!ok) goto done;
            f=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative)
            {  c+=pe->curx; d0+=pe->cury;
               e+=pe->curx; f+=pe->cury;
            }
            if(pe->hasctrl)
            {  a=2*pe->curx - pe->ctrlx;
               b=2*pe->cury - pe->ctrly;
            }
            else
            {  a=pe->curx; b=pe->cury;
            }
            Bezier3(pe,pe->curx,pe->cury,a,b,c,d0,e,f);
            Pebbox_update(pe,c,d0);
            Pebbox_update(pe,e,f);
            pe->ctrlx=c; pe->ctrly=d0;
            pe->hasctrl=TRUE;
            pe->curx=e; pe->cury=f;
            break;
         case 'q':
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            b=Parsefixed(&p,end,&ok); if(!ok) goto done;
            c=Parsefixed(&p,end,&ok); if(!ok) goto done;
            d0=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative)
            {  a+=pe->curx; b+=pe->cury;
               c+=pe->curx; d0+=pe->cury;
            }
            Bezier2(pe,pe->curx,pe->cury,a,b,c,d0);
            Pebbox_update(pe,a,b);
            Pebbox_update(pe,c,d0);
            pe->ctrlx=a; pe->ctrly=b;
            pe->hasctrl=TRUE;
            pe->curx=c; pe->cury=d0;
            break;
         case 't':
            c=Parsefixed(&p,end,&ok); if(!ok) goto done;
            d0=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative) { c+=pe->curx; d0+=pe->cury; }
            if(pe->hasctrl)
            {  a=2*pe->curx - pe->ctrlx;
               b=2*pe->cury - pe->ctrly;
            }
            else
            {  a=pe->curx; b=pe->cury;
            }
            Bezier2(pe,pe->curx,pe->cury,a,b,c,d0);
            Pebbox_update(pe,c,d0);
            pe->ctrlx=a; pe->ctrly=b;
            pe->hasctrl=TRUE;
            pe->curx=c; pe->cury=d0;
            break;
         case 'a':
            /* A rx ry x-axis-rotation large-arc-flag sweep-flag x y
             * Parse all seven path-data values then hand them to the
             * W3C endpoint-to-centre arc decomposer.  Arcsegment
             * itself transforms the sampled vertices through the
             * current CTM via Pathemitpt(). */
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;   /* rx */
            b=Parsefixed(&p,end,&ok); if(!ok) goto done;   /* ry */
            c=Parsefixed(&p,end,&ok); if(!ok) goto done;   /* x-axis rotation */
            d0=Parsefixed(&p,end,&ok); if(!ok) goto done;  /* large-arc-flag */
            e=Parsefixed(&p,end,&ok); if(!ok) goto done;   /* sweep-flag */
            f=Parsefixed(&p,end,&ok); if(!ok) goto done;   /* x */
            g=Parsefixed(&p,end,&ok); if(!ok) goto done;   /* y */
            if(relative) { f+=pe->curx; g+=pe->cury; }
            Arcsegment(pe, pe->curx, pe->cury,
               a, b, c,
               (BOOL)(d0!=0), (BOOL)(e!=0),
               f, g);
            /* Conservative bbox: the arc can bulge outwards by at
             * most (rx,ry) from each endpoint.  Adding the four
             * corner offsets gives a safe overestimate without
             * having to recompute the arc centre. */
            Pebbox_update(pe,pe->curx-a,pe->cury-b);
            Pebbox_update(pe,pe->curx+a,pe->cury+b);
            Pebbox_update(pe,f-a,g-b);
            Pebbox_update(pe,f+a,g+b);
            pe->curx=f; pe->cury=g;
            pe->hasctrl=FALSE;
            break;
         default:
            /* Unknown command - skip one token to avoid infinite loop. */
            p++;
            cmd=0;
            break;
      }
   }
done:
   if(pe->subpathopen) Pathflush(pe,FALSE);
   Pathend(pe);
}

/*--------------------------------------------------------------------*/
/* Paint server resolution (gradients reduced to a solid colour)      */
/*--------------------------------------------------------------------*/

/* Context passed to the style-attribute callback while walking a
 * single <stop> element.  We accumulate the colour and opacity from
 * either presentation attributes or the in-line style="..." until
 * we've seen them all. */
struct GradstopCtx
{  ULONG rgb;
   UBYTE alpha;
   BOOL enabled;
};

static void Gradstoppair(UBYTE *key, UBYTE *value, void *u)
{  struct GradstopCtx *ctx=(struct GradstopCtx *)u;
   if(strieq(key,"stop-color"))
   {  ULONG rgb;
      BOOL enabled;
      UBYTE a=255;
      if(Parsecolor(value,&rgb,&enabled,&a) && enabled)
      {  ctx->rgb=rgb;
         ctx->enabled=TRUE;
         /* rgba()-embedded alpha is rare on stops but honour it. */
         if(a<ctx->alpha) ctx->alpha=a;
      }
   }
   else if(strieq(key,"stop-opacity"))
   {  ctx->alpha=Parsealphaval(value);
   }
}

/* Compute the alpha-weighted average colour of a gradient's <stop>
 * children, recursing through xlink:href to inherit stops from
 * another gradient if this one has none of its own (the Inkscape
 * idiom is to put the actual stops on a base gradient and then have
 * a series of derived gradients that only override the geometry).
 *
 * Real gradient interpolation would require per-pixel maths in the
 * rasteriser, which is far too expensive for our target hardware.
 * The averaged colour is a much better fallback than the black the
 * caller would otherwise pick, and for most paintings the picked
 * colour is visually recognisable. */
static BOOL Gradaverage(struct Decoder *dec, struct XmlNode *grad,
   ULONG *rgb_out, UBYTE *alpha_out, LONG depth)
{  struct XmlNode *child;
   ULONG sumR=0,sumG=0,sumB=0;
   ULONG sumA=0;
   LONG cnt=0;
   if(!grad || depth>=GRAD_MAX_DEPTH) return FALSE;
   for(child=grad->firstchild; child; child=child->nextsibling)
   {  struct GradstopCtx sctx;
      UBYTE *v;
      if(child->type!=XMLN_ELEMENT) continue;
      if(!XmlNameIs(child,"stop")) continue;
      sctx.rgb=0;
      sctx.alpha=255;
      sctx.enabled=FALSE;
      v=XmlAttrValue(child,"stop-color");
      if(v)
      {  ULONG rgb;
         BOOL en;
         UBYTE a=255;
         if(Parsecolor(v,&rgb,&en,&a) && en)
         {  sctx.rgb=rgb;
            sctx.enabled=TRUE;
            if(a<sctx.alpha) sctx.alpha=a;
         }
      }
      v=XmlAttrValue(child,"stop-opacity");
      if(v) sctx.alpha=Parsealphaval(v);
      v=XmlAttrValue(child,"style");
      if(v) Parsestyle(v,Gradstoppair,&sctx);
      if(sctx.enabled)
      {  ULONG a=sctx.alpha;
         sumR += ((sctx.rgb>>16)&0xff) * a;
         sumG += ((sctx.rgb>>8)&0xff) * a;
         sumB += (sctx.rgb&0xff) * a;
         sumA += a;
         cnt++;
      }
   }
   if(cnt==0)
   {  /* Inherit stops via xlink:href, if any. */
      UBYTE *idref=Resolvehref(grad);
      if(idref)
      {  struct XmlNode *target=Findid(dec,idref);
         if(target && (XmlNameIs(target,"linearGradient")
                    || XmlNameIs(target,"radialGradient")))
         {  return Gradaverage(dec,target,rgb_out,alpha_out,depth+1);
         }
      }
      return FALSE;
   }
   {  LONG R,G,B,A;
      if(sumA==0)
      {  R=G=B=0;
      }
      else
      {  R=sumR/sumA;
         G=sumG/sumA;
         B=sumB/sumA;
      }
      A=sumA/cnt;                 /* mean stop opacity */
      if(R>255) R=255;
      if(G>255) G=255;
      if(B>255) B=255;
      if(A>255) A=255;
      *rgb_out=((ULONG)R<<16)|((ULONG)G<<8)|(ULONG)B;
      *alpha_out=(UBYTE)A;
   }
   return TRUE;
}

/*--------------------------------------------------------------------*/
/* Per-pixel gradient parser and evaluator                            */
/*--------------------------------------------------------------------*/

/* Parse a single number out of the attribute string `v` and store it
 * in *out (16.16).  Returns TRUE on success, leaving *out untouched on
 * failure.  Tolerates CSS unit suffixes. */
static BOOL Gradnumattr(struct XmlNode *node, const char *name, LONG *out)
{  UBYTE *v=XmlAttrValue(node,name);
   UBYTE *p,*e;
   BOOL ok;
   LONG r;
   if(!v) return FALSE;
   p=v;
   e=v;
   while(*e) e++;
   r=Parsefixed(&p,e,&ok);
   if(!ok) return FALSE;
   Skipunits(&p,e);
   *out=r;
   return TRUE;
}

/* Collect one stop into a Gradient's stop array.  `style_only` causes
 * us to read just the in-line style attribute - used when the caller
 * has already seen and stored the presentation attributes. */
static void Gradparse_stop(struct XmlNode *child, struct Gradient *g)
{  struct GradstopCtx sctx;
   UBYTE *v;
   LONG offset=0;
   BOOL ok;
   UBYTE *p,*e;
   if(g->nstops>=GRAD_MAX_STOPS) return;
   sctx.rgb=0;
   sctx.alpha=255;
   sctx.enabled=FALSE;
   v=XmlAttrValue(child,"stop-color");
   if(v)
   {  ULONG rgb;
      BOOL en;
      UBYTE a=255;
      if(Parsecolor(v,&rgb,&en,&a) && en)
      {  sctx.rgb=rgb;
         sctx.enabled=TRUE;
         if(a<sctx.alpha) sctx.alpha=a;
      }
   }
   v=XmlAttrValue(child,"stop-opacity");
   if(v) sctx.alpha=Parsealphaval(v);
   v=XmlAttrValue(child,"style");
   if(v) Parsestyle(v,Gradstoppair,&sctx);

   v=XmlAttrValue(child,"offset");
   if(v)
   {  p=v;
      e=v;
      while(*e) e++;
      offset=Parsefixed(&p,e,&ok);
      if(ok)
      {  while(*p==' '||*p=='\t') p++;
         if(*p=='%') offset=offset/100;
      }
      else offset=0;
   }
   if(offset<0) offset=0;
   if(offset>0x10000L) offset=0x10000L;

   if(!sctx.enabled)
   {  /* Spec: a stop with no colour inherits its colour from the
       * previous stop and contributes only an offset and opacity.
       * Approximate by skipping it if we have no previous stop, or
       * cloning the previous stop's colour otherwise. */
      if(g->nstops==0) return;
      g->stops[g->nstops].r=g->stops[g->nstops-1].r;
      g->stops[g->nstops].g=g->stops[g->nstops-1].g;
      g->stops[g->nstops].b=g->stops[g->nstops-1].b;
   }
   else
   {  g->stops[g->nstops].r=(UBYTE)((sctx.rgb>>16)&0xff);
      g->stops[g->nstops].g=(UBYTE)((sctx.rgb>>8)&0xff);
      g->stops[g->nstops].b=(UBYTE)(sctx.rgb&0xff);
   }
   g->stops[g->nstops].a=sctx.alpha;
   g->stops[g->nstops].offset=offset;
   g->nstops++;
}

/* Walk a gradient node and (a) populate the stops array, (b) fill in
 * any geometry attributes present on the node, recursing through
 * xlink:href to inherit anything missing.  The base node's stops and
 * geometry are used for whatever this node does not override - the
 * standard Inkscape idiom for a derived gradient with a moved axis but
 * shared stops.
 *
 * The `seen_*` flags track which geometry attributes have already been
 * set during the walk so later inheritance does not stomp on them.  */
static void Gradparse_walk(struct Decoder *dec, struct XmlNode *node,
   struct Gradient *g, BOOL *seen_x1, BOOL *seen_y1,
   BOOL *seen_x2, BOOL *seen_y2,
   BOOL *seen_cx, BOOL *seen_cy, BOOL *seen_r,
   BOOL *seen_fx, BOOL *seen_fy, LONG depth)
{  struct XmlNode *child;
   UBYTE *v;
   LONG val;
   BOOL had_stops;
   if(!node || depth>=GRAD_MAX_DEPTH) return;

   /* Geometry: only adopt attributes we have not yet seen. */
   if(!*seen_x1 && Gradnumattr(node,"x1",&val)) { g->x1=val; *seen_x1=TRUE; }
   if(!*seen_y1 && Gradnumattr(node,"y1",&val)) { g->y1=val; *seen_y1=TRUE; }
   if(!*seen_x2 && Gradnumattr(node,"x2",&val)) { g->x2=val; *seen_x2=TRUE; }
   if(!*seen_y2 && Gradnumattr(node,"y2",&val)) { g->y2=val; *seen_y2=TRUE; }
   if(!*seen_cx && Gradnumattr(node,"cx",&val)) { g->cx=val; *seen_cx=TRUE; }
   if(!*seen_cy && Gradnumattr(node,"cy",&val)) { g->cy=val; *seen_cy=TRUE; }
   if(!*seen_r  && Gradnumattr(node,"r" ,&val)) { g->r =val; *seen_r =TRUE; }
   if(!*seen_fx && Gradnumattr(node,"fx",&val)) { g->fx=val; *seen_fx=TRUE; }
   if(!*seen_fy && Gradnumattr(node,"fy",&val)) { g->fy=val; *seen_fy=TRUE; }

   v=XmlAttrValue(node,"gradientUnits");
   if(v)
   {  if(strieq(v,"objectBoundingBox")) g->units=GRAD_UNITS_OBJBB;
      else g->units=GRAD_UNITS_USER;
   }
   v=XmlAttrValue(node,"spreadMethod");
   if(v)
   {  if(strieq(v,"reflect"))    g->spread=GRAD_SPREAD_REFLECT;
      else if(strieq(v,"repeat")) g->spread=GRAD_SPREAD_REPEAT;
      else                        g->spread=GRAD_SPREAD_PAD;
   }
   v=XmlAttrValue(node,"gradientTransform");
   if(v && !g->has_gt)
   {  Midentity(&g->gt);
      Parsetransform(v,&g->gt);
      g->has_gt=1;
   }

   /* Stops: only adopt this node's stops if we have none yet (so
    * inherited stops do not overwrite our own).  Then recurse into
    * the xlink:href base if we still have no stops at all. */
   had_stops=(BOOL)(g->nstops>0);
   if(!had_stops)
   {  for(child=node->firstchild; child; child=child->nextsibling)
      {  if(child->type!=XMLN_ELEMENT) continue;
         if(!XmlNameIs(child,"stop")) continue;
         Gradparse_stop(child,g);
      }
   }

   /* Recurse into the referenced base gradient for anything still
    * missing.  Most Inkscape SVGs use this for stops. */
   {  UBYTE *idref=Resolvehref(node);
      if(idref)
      {  struct XmlNode *target=Findid(dec,idref);
         if(target && (XmlNameIs(target,"linearGradient")
                    || XmlNameIs(target,"radialGradient")))
         {  Gradparse_walk(dec,target,g,
               seen_x1,seen_y1,seen_x2,seen_y2,
               seen_cx,seen_cy,seen_r,seen_fx,seen_fy,depth+1);
         }
      }
   }
}

/* Insertion-sort stops by offset (already-sorted is the common case so
 * a tiny insertion sort is both shortest and fastest). */
static void Gradparse_sortstops(struct Gradient *g)
{  int i,j;
   struct Gradstop_p tmp;
   for(i=1;i<g->nstops;i++)
   {  tmp=g->stops[i];
      j=i-1;
      while(j>=0 && g->stops[j].offset>tmp.offset)
      {  g->stops[j+1]=g->stops[j];
         j--;
      }
      g->stops[j+1]=tmp;
   }
}

/* Build a fully resolved Gradient struct for the given gradient node.
 * Returns NULL if the node has no usable stops.  Allocated from the
 * decoder pool. */
static struct Gradient *Gradparse(struct Decoder *dec, struct XmlNode *node)
{  struct Gradient *g;
   BOOL seen_x1=FALSE, seen_y1=FALSE, seen_x2=FALSE, seen_y2=FALSE;
   BOOL seen_cx=FALSE, seen_cy=FALSE, seen_r=FALSE;
   BOOL seen_fx=FALSE, seen_fy=FALSE;
   if(!node) return NULL;
   g=(struct Gradient *)AllocPooled(dec->pool,sizeof(*g));
   if(!g) return NULL;
   memset(g,0,sizeof(*g));
   g->type   = XmlNameIs(node,"radialGradient") ? GRAD_TYPE_RADIAL : GRAD_TYPE_LINEAR;
   /* SVG 1.1 13.2.1: default gradientUnits is "objectBoundingBox", NOT
    * userSpaceOnUse.  The 0x10000 / 0x8000 defaults below describe the
    * gradient in bbox-relative 0..1 coords, exactly how the spec
    * defines them.  Defaulting to userSpaceOnUse here used to make
    * Inkscape exports (which omit gradientUnits, expecting the spec
    * default) paint with the entire gradient compressed into the
    * top-left corner of every shape - because the shape would be at
    * eg. cx=200 cy=300 r=120 while the gradient stayed pinned at the
    * user-space unit square.  Compound paths and radial fills are
    * particularly sensitive to this. */
   g->units  = GRAD_UNITS_OBJBB;
   g->spread = GRAD_SPREAD_PAD;
   g->nstops = 0;
   g->has_gt = 0;
   /* Spec defaults (in objectBoundingBox 0..1 coords): linear
    * x1=y1=0, x2=1, y2=0; radial cx=cy=0.5, r=0.5. */
   g->x1=0;          g->y1=0;
   g->x2=0x10000L;   g->y2=0;
   g->cx=0x8000L;    g->cy=0x8000L;
   g->r =0x8000L;
   g->fx=g->cx;      g->fy=g->cy;
   Midentity(&g->gt);

   Gradparse_walk(dec,node,g,
      &seen_x1,&seen_y1,&seen_x2,&seen_y2,
      &seen_cx,&seen_cy,&seen_r,&seen_fx,&seen_fy,0);

   /* Radial focus defaults to the centre when not supplied. */
   if(!seen_fx) g->fx=g->cx;
   if(!seen_fy) g->fy=g->cy;

   /* SVG 1.1 13.2.3: if the focal point lies outside the boundary
    * circle the user agent must move it to the intersection of the
    * line from (cx,cy) to (fx,fy) and that circle.  Clamp slightly
    * inside (97% of r) so the per-pixel ray-quadratic discriminant
    * stays well above zero - we've seen radial gradients with fx/fy
    * literally on the circle round-off to a negative discriminant
    * and turn the whole shape into the first colour stop. */
   if(g->type==GRAD_TYPE_RADIAL && g->r>0)
   {  LONG dfx=g->fx - g->cx;
      LONG dfy=g->fy - g->cy;
      LONG dnx=fdiv(dfx,g->r);
      LONG dny=fdiv(dfy,g->r);
      LONG nsq=fmul(dnx,dnx) + fmul(dny,dny);
      LONG safe_lim=0xF852L;
      LONG safe_sq=fmul(safe_lim,safe_lim);
      if(nsq>safe_sq)
      {  LONG mag=fsqrt(nsq);
         LONG scale=fdiv(safe_lim,mag);
         g->fx=g->cx + fmul(dfx,scale);
         g->fy=g->cy + fmul(dfy,scale);
      }
   }

   if(g->nstops==0) return NULL;
   if(g->nstops==1)
   {  /* Single-stop gradient: clone the only stop at offset 1.0 so
       * the evaluator can interpolate trivially. */
      g->stops[1]=g->stops[0];
      g->stops[1].offset=0x10000L;
      g->nstops=2;
   }
   Gradparse_sortstops(g);
   return g;
}

/* Compose per-shape evaluator state into *pctx given a gradient and
 * the CTM in use when the shape is rendered.
 *
 * For both linear and radial gradients we precompute:
 *   inv = inverse of (CTM * gradientTransform)
 * i.e. the affine that maps raster pixels back into the gradient's
 * own coordinate system.  Linear gradients then maintain a per-pixel
 * `t` value by stepping `dt_dx`/`dt_dy` deltas in raster space;
 * radial gradients evaluate distance from centre per pixel using the
 * `inv` matrix directly. */
static BOOL Buildpaintctx(struct Paintctx *pctx, struct Renderstate *rs,
   struct Gradient *g, const struct Bbox *bb)
{  struct Matrix tot, inv;
   /* Note: pctx->alpha and pctx->rgb are owned by the caller
    * (Buildfillctx).  We only set the gradient-specific evaluator
    * fields here and clear them on failure. */
   pctx->has_grad=0;
   pctx->grad=NULL;
   if(!g) return FALSE;

   tot=rs->M;
   /* objectBoundingBox: insert a bbox-to-userspace mapping between
    * the gradient's own coords and the CTM, so the gradient sits
    * at the same place on the shape regardless of where the shape
    * lives in user space. */
   if(g->units==GRAD_UNITS_OBJBB && bb && bb->valid)
   {  LONG bw=bb->xmax - bb->xmin;
      LONG bh=bb->ymax - bb->ymin;
      if(bw>0 && bh>0)
      {  struct Matrix bm;
         Midentity(&bm);
         bm.a=bw;
         bm.d=bh;
         bm.e=bb->xmin;
         bm.f=bb->ymin;
         Mcompose(&tot,&tot,&bm);
      }
   }
   if(g->has_gt) Mcompose(&tot,&tot,&g->gt);
   if(!Minverse(&tot,&inv)) return FALSE;
   pctx->inv=inv;
   pctx->grad=g;
   pctx->has_grad=1;

   if(g->type==GRAD_TYPE_LINEAR)
   {  LONG vx, vy;
      LONG lensq;
      LONG inv_lensq;
      LONG g00x, g00y;          /* gradient coords at raster (0,0) */
      LONG g10x, g10y;          /* gradient coords at raster (1,0) */
      LONG g01x, g01y;          /* gradient coords at raster (0,1) */
      vx=g->x2 - g->x1;
      vy=g->y2 - g->y1;
      lensq=fmul(vx,vx) + fmul(vy,vy);
      if(lensq<=0)
      {  pctx->has_grad=0;
         pctx->grad=NULL;
         return FALSE;
      }
      inv_lensq=fdiv(0x10000L, lensq);

      g00x=fmul(inv.a,0)        + fmul(inv.c,0)        + inv.e;
      g00y=fmul(inv.b,0)        + fmul(inv.d,0)        + inv.f;
      g10x=fmul(inv.a,1L<<16)   + fmul(inv.c,0)        + inv.e;
      g10y=fmul(inv.b,1L<<16)   + fmul(inv.d,0)        + inv.f;
      g01x=fmul(inv.a,0)        + fmul(inv.c,1L<<16)   + inv.e;
      g01y=fmul(inv.b,0)        + fmul(inv.d,1L<<16)   + inv.f;

      pctx->t_x0 = Gradproj(g00x - g->x1, g00y - g->y1, vx, vy, inv_lensq);
      pctx->dt_dx= Gradproj(g10x - g00x , g10y - g00y , vx, vy, inv_lensq);
      pctx->dt_dy= Gradproj(g01x - g00x , g01y - g00y , vx, vy, inv_lensq);
   }
   else
   {  if(g->r<=0)
      {  pctx->has_grad=0;
         pctx->grad=NULL;
         return FALSE;
      }
      pctx->inv_r=fdiv(0x10000L,g->r);
      if(g->fx==g->cx && g->fy==g->cy)
      {  /* Centered focal point: short-circuit the per-pixel
          * quadratic and use t = |P-C|/r directly. */
         pctx->Dx_n=0;
         pctx->Dy_n=0;
         pctx->DD_minus_1=-0x10000L;     /* 0 - 1 in 16.16 */
      }
      else
      {  /* Displaced focal: precompute (F-C)/r and |D|^2 - 1 so
          * the inner loop's quadratic stays in 16.16 range even
          * for documents whose user-space coords run into the
          * thousands. */
         pctx->Dx_n=fmul(g->fx - g->cx, pctx->inv_r);
         pctx->Dy_n=fmul(g->fy - g->cy, pctx->inv_r);
         pctx->DD_minus_1=
            fmul(pctx->Dx_n,pctx->Dx_n)
            + fmul(pctx->Dy_n,pctx->Dy_n)
            - 0x10000L;
      }
   }
   return TRUE;
}

/* Apply spreadMethod to a gradient parameter in 16.16, returning a
 * value clamped/wrapped into [0,1] (i.e. 0..0x10000).
 *
 * - pad      : clamp at the boundary (default)
 * - reflect  : triangle wave - bounce off each integer boundary
 * - repeat   : sawtooth - take the fractional part
 *
 * Internally we work in 16.16 with bit twiddling: shifting right 16
 * gives the integer count of full traversals, mask 0xFFFF gives the
 * 0..1 fractional part. */
static LONG Gradspread(const struct Gradient *g, LONG t)
{  LONG period;
   LONG frac;
   if(g->spread==GRAD_SPREAD_PAD)
   {  if(t<0)         return 0;
      if(t>0x10000L)  return 0x10000L;
      return t;
   }
   period=(g->spread==GRAD_SPREAD_REFLECT) ? 0x20000L : 0x10000L;
   frac=t;
   if(frac<0)
   {  LONG q=(-frac)/period + 1;
      frac+=q*period;
   }
   if(frac>=period) frac %= period;
   if(g->spread==GRAD_SPREAD_REFLECT)
   {  if(frac>0x10000L) frac=0x20000L - frac;
   }
   if(frac<0)         frac=0;
   if(frac>0x10000L)  frac=0x10000L;
   return frac;
}

/* Sample a Gradient at parameter t (16.16).  Out-of-range t values
 * are routed through Gradspread() which clamps, reflects or repeats
 * according to spreadMethod.  Linear interpolation between adjacent
 * stops in 8-bit precision (256 sub-steps), with the standard SVG
 * stop ordering. */
static ULONG Gradsample(const struct Gradient *g, LONG t, UBYTE *out_a)
{  int i;
   LONG t0,t1,span,frac;
   LONG r0,g0,b0,a0;
   LONG r1,g1,b1,a1;
   LONG R,G,B,A;
   t=Gradspread(g,t);
   if(t<=g->stops[0].offset)
   {  *out_a=g->stops[0].a;
      return ((ULONG)g->stops[0].r<<16)|((ULONG)g->stops[0].g<<8)|(ULONG)g->stops[0].b;
   }
   if(t>=g->stops[g->nstops-1].offset)
   {  *out_a=g->stops[g->nstops-1].a;
      return ((ULONG)g->stops[g->nstops-1].r<<16)
            |((ULONG)g->stops[g->nstops-1].g<<8)
            |(ULONG)g->stops[g->nstops-1].b;
   }
   for(i=1;i<g->nstops;i++)
   {  if(t<g->stops[i].offset)
      {  t0=g->stops[i-1].offset;
         t1=g->stops[i].offset;
         span=t1-t0;
         if(span<=0)
         {  *out_a=g->stops[i].a;
            return ((ULONG)g->stops[i].r<<16)
                  |((ULONG)g->stops[i].g<<8)
                  |(ULONG)g->stops[i].b;
         }
         frac=((t-t0)<<8)/span;
         if(frac<0) frac=0;
         if(frac>256) frac=256;
         r0=g->stops[i-1].r;  r1=g->stops[i].r;
         g0=g->stops[i-1].g;  g1=g->stops[i].g;
         b0=g->stops[i-1].b;  b1=g->stops[i].b;
         a0=g->stops[i-1].a;  a1=g->stops[i].a;
         R=r0 + ((r1-r0)*frac>>8);
         G=g0 + ((g1-g0)*frac>>8);
         B=b0 + ((b1-b0)*frac>>8);
         A=a0 + ((a1-a0)*frac>>8);
         if(R<0) R=0; if(R>255) R=255;
         if(G<0) G=0; if(G>255) G=255;
         if(B<0) B=0; if(B>255) B=255;
         if(A<0) A=0; if(A>255) A=255;
         *out_a=(UBYTE)A;
         return ((ULONG)R<<16)|((ULONG)G<<8)|(ULONG)B;
      }
   }
   *out_a=g->stops[g->nstops-1].a;
   return ((ULONG)g->stops[g->nstops-1].r<<16)
         |((ULONG)g->stops[g->nstops-1].g<<8)
         |(ULONG)g->stops[g->nstops-1].b;
}

/* Sample the gradient at raster pixel (rx,ry) using per-shape state.
 *
 * Linear gradients:
 *   t is the orthogonal projection onto (g->x1,y1)->(g->x2,y2), with
 *   t=0 at x1,y1 and t=1 at x2,y2.
 *
 * Radial gradients with focal point at the centre:
 *   t = |P - C| / r, the centered-radial fast path.
 *
 * Radial gradients with displaced focal point:
 *   t is computed from SVG 1.1's geometric construction.  Solve the
 *   quadratic for the ray F + s*v that crosses the boundary circle,
 *   then t = 1/s gives the fractional distance along the ray.  This
 *   is what makes the gradient highlight actually shift toward
 *   (fx,fy) rather than sitting at the centre.
 *
 * Out-of-range t values are routed through Gradspread() inside
 * Gradsample so spreadMethod is honoured uniformly. */
static ULONG Gradeval_pixel(const struct Paintctx *pctx, LONG rx, LONG ry,
   UBYTE *out_a)
{  const struct Gradient *g=pctx->grad;
   LONG t;
   if(g->type==GRAD_TYPE_LINEAR)
   {  t=pctx->t_x0 + pctx->dt_dx*rx + pctx->dt_dy*ry;
   }
   else
   {  LONG gx,gy;
      LONG rx16=rx<<16, ry16=ry<<16;
      LONG vx,vy;
      LONG vv;
      LONG Dv,disc,s;
      gx=fmul(pctx->inv.a,rx16) + fmul(pctx->inv.c,ry16) + pctx->inv.e;
      gy=fmul(pctx->inv.b,rx16) + fmul(pctx->inv.d,ry16) + pctx->inv.f;
      /* Move into normalised r=1 coords so squaring can't overflow
       * 16.16.  vx/vy is (P - F) / r. */
      vx=fmul(gx - g->fx, pctx->inv_r);
      vy=fmul(gy - g->fy, pctx->inv_r);
      vv=fmul(vx,vx) + fmul(vy,vy);
      if(vv<=0)
      {  t=0;
      }
      else if(pctx->Dx_n==0 && pctx->Dy_n==0)
      {  /* Centered focal: t = |v_n|. */
         t=fsqrt(vv);
      }
      else
      {  /* Displaced focal: solve normalised quadratic
          *   s^2 (v.v) + 2s (D.v) + (D.D - 1) = 0
          * for the ray F + s*v through the boundary circle,
          * then return t = 1/s. */
         Dv=fmul(pctx->Dx_n,vx) + fmul(pctx->Dy_n,vy);
         disc=fmul(Dv,Dv) - fmul(vv, pctx->DD_minus_1);
         if(disc<0) disc=0;
         disc=fsqrt(disc);
         s=fdiv(disc - Dv, vv);
         if(s<=0) t=0x10000L;
         else     t=fdiv(0x10000L, s);
      }
   }
   return Gradsample(g,t,out_a);
}

/* If `value` begins with "url(#id)", lookup the referenced node and,
 * provided it is a linearGradient or radialGradient, fill in *rgb_out
 * and *alpha_out from its cached average colour (computing the cache
 * on first reference).  Also writes the parsed Gradient pointer into
 * *grad_out (if non-NULL) for callers that want per-pixel evaluation.
 * Returns the address of the first character past the closing ')' so
 * the caller can parse a fallback colour from the remainder of the
 * value if we couldn't resolve the URL.  Returns NULL if `value` is
 * not a url(...) reference at all. */
static UBYTE *Lookupgradient(struct Decoder *dec, UBYTE *value,
   ULONG *rgb_out, UBYTE *alpha_out, BOOL *resolved_out,
   struct Gradient **grad_out)
{  UBYTE *p,*idstart,*idend;
   UBYTE save;
   struct Iddef *iddef;
   *resolved_out=FALSE;
   if(!value) return NULL;
   p=value;
   while(*p==' '||*p=='\t') p++;
   if(p[0]!='u' && p[0]!='U') return NULL;
   if(p[1]!='r' && p[1]!='R') return NULL;
   if(p[2]!='l' && p[2]!='L') return NULL;
   p+=3;
   while(*p==' '||*p=='\t') p++;
   if(*p!='(') return NULL;
   p++;
   while(*p==' '||*p=='\t') p++;
   if(*p=='\'' || *p=='"') p++;
   if(*p!='#') return NULL;
   p++;
   idstart=p;
   while(*p && *p!=')' && *p!='\'' && *p!='"' && *p!=' ' && *p!='\t') p++;
   idend=p;
   /* Skip past closing quote / paren so the caller can parse a
    * fallback colour from whatever follows. */
   while(*p && *p!=')') p++;
   if(*p==')') p++;
   /* Temporarily NUL-terminate the id so the existing string
    * comparator in Findid_iddef works.  The buffer we are slicing
    * into is owned by the decoder so a transient modification is
    * safe even on re-entry. */
   if(idstart>=idend) return p;
   save=*idend;
   *idend=0;
   iddef=Findid_iddef(dec,idstart);
   *idend=save;
   if(!iddef) return p;
   if(iddef->grad_state==0)
   {  /* Compute on first reference and cache. */
      if(iddef->node
      && (XmlNameIs(iddef->node,"linearGradient")
       || XmlNameIs(iddef->node,"radialGradient")))
      {  ULONG rgb;
         UBYTE a;
         if(Gradaverage(dec,iddef->node,&rgb,&a,0))
         {  iddef->grad_rgb=rgb;
            iddef->grad_alpha=a;
            iddef->grad_state=1;
            /* Real per-pixel gradient (only on P96 deep destinations,
             * but parse it unconditionally so the cache is consistent
             * across multiple shapes that share the same id). */
            iddef->grad=Gradparse(dec,iddef->node);
         }
         else iddef->grad_state=2;
      }
      else iddef->grad_state=2;
   }
   if(iddef->grad_state==1)
   {  *rgb_out=iddef->grad_rgb;
      *alpha_out=iddef->grad_alpha;
      *resolved_out=TRUE;
      if(grad_out) *grad_out=iddef->grad;
   }
   return p;
}

/* Resolve an SVG paint value (the right-hand side of a fill or stroke
 * declaration) into a single 0xRRGGBB colour with a 0..255 alpha.
 *
 * Handles:
 *   - "none" / "transparent"   sets *enabled=FALSE
 *   - "url(#id) fallback"      tries the gradient, falls back to the
 *                              trailing colour spec if the lookup fails
 *   - everything Parsecolor accepts
 *
 * Returns TRUE if the value was a recognised paint expression
 * (regardless of whether it ended up enabled).  Returns FALSE only
 * for unrecognised values, in which case the caller should leave its
 * previous paint untouched. */
static BOOL Resolvepaint(struct Decoder *dec, UBYTE *value,
   ULONG *rgb_out, UBYTE *alpha_out, BOOL *enabled_out,
   struct Gradient **grad_out)
{  BOOL resolved;
   UBYTE *tail;
   *alpha_out=255;
   if(grad_out) *grad_out=NULL;
   if(!value) { *enabled_out=FALSE; return FALSE; }
   tail=Lookupgradient(dec,value,rgb_out,alpha_out,&resolved,grad_out);
   if(resolved)
   {  *enabled_out=(*alpha_out>0);
      return TRUE;
   }
   if(tail)
   {  /* We saw a url(...) but couldn't resolve it.  Try whatever
       * follows it as a paint fallback. */
      while(*tail==' '||*tail=='\t') tail++;
      if(*tail)
         return Parsecolor(tail,rgb_out,enabled_out,alpha_out);
      /* No fallback supplied - treat as none so we don't paint a
       * black blob where the author expected a gradient. */
      *enabled_out=FALSE;
      *alpha_out=0;
      return TRUE;
   }
   return Parsecolor(value,rgb_out,enabled_out,alpha_out);
}

/*--------------------------------------------------------------------*/
/* Style application                                                  */
/*--------------------------------------------------------------------*/

struct StyleCtx
{  struct Decoder *dec;
   struct Renderstate *rs;
};

static void Applyfill(struct Decoder *dec, struct Renderstate *rs, UBYTE *value)
{  ULONG rgb;
   UBYTE alpha;
   BOOL enabled;
   struct Gradient *grad=NULL;
   if(!Resolvepaint(dec,value,&rgb,&alpha,&enabled,&grad)) return;
   rs->fillvalid=enabled;
   rs->fill_color_alpha=alpha;
   rs->fillgrad=grad;
   if(enabled)
   {  rs->fillrgb=rgb;
      rs->fillpen=Getpen(dec,rgb);
   }
}

static void Applystroke(struct Decoder *dec, struct Renderstate *rs, UBYTE *value)
{  ULONG rgb;
   UBYTE alpha;
   BOOL enabled;
   /* Strokes through a gradient are unusual and we render strokes as
    * 1-pixel-wide lines through graphics.library, where a real
    * gradient stroke is impractical.  Reduce to the averaged colour. */
   if(!Resolvepaint(dec,value,&rgb,&alpha,&enabled,NULL)) return;
   rs->strokevalid=enabled;
   rs->stroke_color_alpha=alpha;
   if(enabled)
   {  rs->strokergb=rgb;
      rs->strokepen=Getpen(dec,rgb);
   }
}

static void Stylepair(UBYTE *key, UBYTE *value, void *u)
{  struct StyleCtx *ctx=(struct StyleCtx *)u;
   if(strieq(key,"fill"))
   {  Applyfill(ctx->dec,ctx->rs,value);
   }
   else if(strieq(key,"stroke"))
   {  Applystroke(ctx->dec,ctx->rs,value);
   }
   else if(strieq(key,"opacity"))
   {  /* `opacity` cascades down by multiplication: a transparent group
       * makes every descendant paint that much more see-through. */
      ctx->rs->element_opacity=Combinealpha(ctx->rs->element_opacity,
         Parsealphaval(value));
   }
   else if(strieq(key,"fill-opacity"))
   {  ctx->rs->fill_opacity=Parsealphaval(value);
   }
   else if(strieq(key,"stroke-opacity"))
   {  ctx->rs->stroke_opacity=Parsealphaval(value);
   }
   else if(strieq(key,"stroke-width"))
   {  UBYTE *p=value;
      UBYTE *e=value;
      BOOL ok;
      LONG w;
      while(*e) e++;
      w=Parsefixed(&p,e,&ok);
      Skipunits(&p,e);          /* tolerate `2px`, `1em` etc. */
      if(ok) ctx->rs->strokewidth=w;
   }
   else if(strieq(key,"display"))
   {  ctx->rs->visible=(UBYTE)(strieq((UBYTE *)value,"none")?FALSE:TRUE);
   }
   else if(strieq(key,"visibility"))
   {  /* SVG: "hidden" or "collapse" hides this element but still
       * lets it participate in layout.  For our raster output the
       * effect is identical to display:none. */
      ctx->rs->visible=(UBYTE)((strieq((UBYTE *)value,"hidden")
         || strieq((UBYTE *)value,"collapse"))?FALSE:TRUE);
   }
}

/* Read fill/stroke/style from a node and update the render state.
 *
 * Order matters: presentation attributes (`fill="..."`) are applied
 * first, then the in-line style="..." overrides them.  This matches
 * SVG's CSS-precedence rules well enough for the documents we
 * actually encounter. */
static void Applystyle(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  UBYTE *v;
   v=XmlAttrValue(node,"fill");
   if(v) Applyfill(dec,rs,v);
   v=XmlAttrValue(node,"stroke");
   if(v) Applystroke(dec,rs,v);
   v=XmlAttrValue(node,"opacity");
   if(v) rs->element_opacity=Combinealpha(rs->element_opacity,Parsealphaval(v));
   v=XmlAttrValue(node,"fill-opacity");
   if(v) rs->fill_opacity=Parsealphaval(v);
   v=XmlAttrValue(node,"stroke-opacity");
   if(v) rs->stroke_opacity=Parsealphaval(v);
   v=XmlAttrValue(node,"stroke-width");
   if(v)
   {  UBYTE *p=v;
      UBYTE *e=v;
      BOOL ok;
      LONG w;
      while(*e) e++;
      w=Parsefixed(&p,e,&ok);
      Skipunits(&p,e);
      if(ok) rs->strokewidth=w;
   }
   v=XmlAttrValue(node,"display");
   if(v) rs->visible=(UBYTE)(strieq(v,"none")?FALSE:TRUE);
   v=XmlAttrValue(node,"visibility");
   if(v) rs->visible=(UBYTE)((strieq(v,"hidden")||strieq(v,"collapse"))?FALSE:TRUE);
   v=XmlAttrValue(node,"style");
   if(v)
   {  struct StyleCtx ctx;
      ctx.dec=dec;
      ctx.rs=rs;
      Parsestyle(v,Stylepair,&ctx);
   }
}

/* Apply a transform attribute if present.  Modifies *rs->M in place. */
static void Applytransform(struct XmlNode *node, struct Renderstate *rs)
{  UBYTE *v=XmlAttrValue(node,"transform");
   if(v) Parsetransform(v,&rs->M);
}

/*--------------------------------------------------------------------*/
/* Fill paint-context assembly                                        */
/*--------------------------------------------------------------------*/

/* Combine the three fill alpha sources (colour-embedded, fill-opacity,
 * inherited element opacity) into a single effective 0..255. */
static UBYTE Effectivefillalpha(struct Renderstate *rs)
{  UBYTE a;
   a=Combinealpha(rs->fill_color_alpha, rs->fill_opacity);
   a=Combinealpha(a, rs->element_opacity);
   return a;
}

/* Combined effective stroke alpha: stroke-colour alpha * stroke-opacity
 * * element-opacity, with values in 0..255 and rounded multiplication. */
static UBYTE Effectivestrokealpha(struct Renderstate *rs)
{  UBYTE a;
   a=Combinealpha(rs->stroke_color_alpha, rs->stroke_opacity);
   a=Combinealpha(a, rs->element_opacity);
   return a;
}

/* Stroke width in 16.16 raster pixels.  Combines the SVG-space stroke
 * width with an approximate scale from the active CTM so a 1.5-unit
 * stroke under a 4x zoom comes out as 6 raster pixels.  Pure rotation
 * matrices have determinant 1 and pass through unchanged; the
 * sqrt(|det|) factor is exact for uniform scale and a good geometric
 * mean for non-uniform scale.  Pure axis-aligned scales avoid the
 * sqrt cost entirely; the result matches |a| or |d| within rounding. */
static LONG Strokewidth_px(struct Renderstate *rs)
{  LONG sw=rs->strokewidth;
   LONG ascale,dscale;
   LONG det;
   if(sw<=0) return 0;
   if(rs->M.b==0 && rs->M.c==0)
   {  ascale=rs->M.a; if(ascale<0) ascale=-ascale;
      dscale=rs->M.d; if(dscale<0) dscale=-dscale;
      if(ascale==0) return fmul(sw,dscale);
      if(dscale==0) return fmul(sw,ascale);
      return fmul(sw, fsqrt(fmul(ascale,dscale)));
   }
   det=fmul(rs->M.a,rs->M.d) - fmul(rs->M.c,rs->M.b);
   if(det<0) det=-det;
   return fmul(sw, fsqrt(det));
}

/* Compose a Paintctx for the current shape given the active Renderstate
 * and the combined effective fill alpha (the renderer has already
 * folded element_opacity, fill_opacity and the colour-embedded alpha
 * into one value).  When the fill is a gradient and we are rendering
 * to a P96 deep destination, this attempts to build per-pixel
 * evaluator state; if that fails (singular CTM, no resolved gradient,
 * etc) we transparently fall back to the averaged solid colour.
 *
 * `bb` is the user-space bounding box of the shape being painted.
 * Required for objectBoundingBox gradients (the SVG default) which
 * map [0,1]^2 in gradient coords onto the shape's bbox.  Pass a
 * !bb->valid Bbox when the caller doesn't know one (palette path,
 * stroke etc.) and the gradient evaluator will fall back to user
 * space - matching the pre-bbox-aware behaviour. */
static void Buildfillctx(struct Paintctx *pctx, struct Decoder *dec,
   struct Renderstate *rs, UBYTE eff_alpha, const struct Bbox *bb)
{  pctx->has_grad=0;
   pctx->grad=NULL;
   pctx->alpha=eff_alpha;
   pctx->rgb=rs->fillrgb;
   /* Quality gate: per-pixel gradient evaluation only happens when
    * the destination is P96 deep AND the adaptive quality tier
    * approved this gradient type.  Otherwise the renderer falls
    * back to the averaged colour already in rs->fillrgb. */
   if(rs->fillgrad && (dec->decflags&DECOF_P96DEEP))
   {  struct Gradient *g=rs->fillgrad;
      USHORT need = (g->type==GRAD_TYPE_RADIAL) ? QF_GRAD_RADIAL : QF_GRAD_LINEAR;
      if((dec->quality & need) && Buildpaintctx(pctx,rs,g,bb))
      {  /* Buildpaintctx left pctx->has_grad and pctx->grad set. */
      }
      else
      {  pctx->has_grad=0;
         pctx->grad=NULL;
      }
   }
   /* When alpha blending is disabled, snap fully-opaque or fully
    * transparent so Fillspan can take the hard-write/skip fast path
    * without ever entering the per-pixel blend loop. */
   if(!(dec->quality & QF_ALPHA_BLEND))
   {  if(pctx->alpha < ALPHA_SKIP) pctx->alpha=0;
      else pctx->alpha=255;
   }
}

/* Paint context for stroke painting.  Strokes are always solid (no
 * per-pixel gradient evaluation), so this just packs the solid colour
 * and effective alpha into the Paintctx that Drawpolygon_fill /
 * Drawellipse_fill consume to render the thick-segment quads and
 * round caps/joins. */
static void Buildstrokectx(struct Paintctx *pctx, struct Decoder *dec,
   struct Renderstate *rs, UBYTE eff_alpha)
{  pctx->has_grad=0;
   pctx->grad=NULL;
   pctx->alpha=eff_alpha;
   pctx->rgb=rs->strokergb;
   if(!(dec->quality & QF_ALPHA_BLEND))
   {  if(pctx->alpha < ALPHA_SKIP) pctx->alpha=0;
      else pctx->alpha=255;
   }
}

/*--------------------------------------------------------------------*/
/* Shape attribute helpers                                            */
/*--------------------------------------------------------------------*/

static LONG Numattr(struct XmlNode *node, const char *name, LONG def)
{  UBYTE *v=XmlAttrValue(node,name);
   UBYTE *p,*e;
   BOOL ok;
   LONG r;
   if(!v) return def;
   p=v;
   e=v;
   while(*e) e++;
   r=Parsefixed(&p,e,&ok);
   /* Attribute-level values may carry a CSS-style unit suffix
    * (`100px`, `2em`, `50%`) - swallow it so we don't trip up on it
    * here.  We treat all units as user units for now; px is the only
    * one most SVG files actually use. */
   Skipunits(&p,e);
   return ok?r:def;
}

/*--------------------------------------------------------------------*/
/* Element renderers                                                  */
/*--------------------------------------------------------------------*/

static void Renderrect(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  LONG x=Numattr(node,"x",0);
   LONG y=Numattr(node,"y",0);
   LONG w=Numattr(node,"width",0);
   LONG h=Numattr(node,"height",0);
   LONG rx=Numattr(node,"rx",-1);
   LONG ry=Numattr(node,"ry",-1);
   LONG hw,hh;
   LONG x0,y0,x1,y1;
   struct Point32 pts[4];
   struct Paintctx pctx;
   struct Bbox bb;

   if(w<=0 || h<=0) return;

   /* SVG 1.1 9.2: a missing rx mirrors ry and vice versa; both then
    * clamp to half the width / height. */
   if(rx<0 && ry<0) { rx=0; ry=0; }
   else
   {  if(rx<0) rx=ry;
      if(ry<0) ry=rx;
      if(rx<0) rx=0;
      if(ry<0) ry=0;
      hw=w>>1; hh=h>>1;
      if(rx>hw) rx=hw;
      if(ry>hh) ry=hh;
   }

   if(rx==0 && ry==0)
   {  /* Sharp-cornered rectangle: 4 transformed corners. */
      Mxform(&rs->M,x,y,&x0,&y0);
      Mxform(&rs->M,x+w,y,&x1,&y1);
      pts[0].x=x0; pts[0].y=y0;
      pts[1].x=x1; pts[1].y=y1;
      Mxform(&rs->M,x+w,y+h,&x0,&y0);
      Mxform(&rs->M,x,y+h,&x1,&y1);
      pts[2].x=x0; pts[2].y=y0;
      pts[3].x=x1; pts[3].y=y1;
      bb.valid=TRUE;
      bb.xmin=x;     bb.ymin=y;
      bb.xmax=x+w;   bb.ymax=y+h;
      if(rs->fillvalid)
      {  Buildfillctx(&pctx,dec,rs,Effectivefillalpha(rs),&bb);
         Setpen(dec,rs->fillpen);
         Drawpolygon_fill(dec,pts,4,&pctx);
      }
      if(rs->strokevalid)
         Strokepolyline(dec,pts,4,TRUE,rs);
      return;
   }

   /* Rounded rectangle: build as a path so each corner curve goes
    * through the adaptive Bezier subdivider and renders at the same
    * fidelity as any hand-authored path. */
   {  struct Pathemit pe;
      LONG k_rx, k_ry;
      WORD cap;
      cap=64;
      pe.dec=dec;
      pe.rs=rs;
      pe.M=&rs->M;
      pe.capacity=cap;
      pe.pts=(struct Point32 *)AllocPooled(dec->pool,
         sizeof(struct Point32)*cap);
      if(!pe.pts) return;
      Pathreset(&pe);
      pe.subpathopen=TRUE;
      pe.curx=x+rx; pe.cury=y;
      pe.startx=x+rx; pe.starty=y;
      /* Seed the user-space bbox with the rect corners so the
       * gradient evaluator gets a proper objectBoundingBox.
       * Pebbox_update during Parsepath wouldn't fire here because
       * we drive Bezier3 directly. */
      pe.ubbox_xmin=x;
      pe.ubbox_ymin=y;
      pe.ubbox_xmax=x+w;
      pe.ubbox_ymax=y+h;
      pe.ubbox_init=TRUE;
      Pathemitpt(&pe,x+rx,y);
      /* Cubic-bezier handle distance for a quarter-circle:
       * k = 4*(sqrt(2)-1)/3 ~ 0.5522847 (0x8D0D in 16.16). */
      k_rx=fmul(rx,0x8D0DL);
      k_ry=fmul(ry,0x8D0DL);
      Pathemitpt(&pe,x+w-rx,y);
      Bezier3(&pe,
         x+w-rx,         y,
         x+w-rx+k_rx,    y,
         x+w,            y+ry-k_ry,
         x+w,            y+ry);
      Pathemitpt(&pe,x+w,y+h-ry);
      Bezier3(&pe,
         x+w,            y+h-ry,
         x+w,            y+h-ry+k_ry,
         x+w-rx+k_rx,    y+h,
         x+w-rx,         y+h);
      Pathemitpt(&pe,x+rx,y+h);
      Bezier3(&pe,
         x+rx,           y+h,
         x+rx-k_rx,      y+h,
         x,              y+h-ry+k_ry,
         x,              y+h-ry);
      Pathemitpt(&pe,x,y+ry);
      Bezier3(&pe,
         x,              y+ry,
         x,              y+ry-k_ry,
         x+rx-k_rx,      y,
         x+rx,           y);
      Pathflush(&pe,TRUE);
      Pathend(&pe);
   }
}

static void Rendercircle(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  LONG cx=Numattr(node,"cx",0);
   LONG cy=Numattr(node,"cy",0);
   LONG r=Numattr(node,"r",0);
   LONG rcx,rcy,redge,dummy;
   LONG rr;
   struct Paintctx pctx;
   struct Bbox bb;
   if(r<=0) return;
   Mxform(&rs->M,cx,cy,&rcx,&rcy);
   Mxform(&rs->M,cx+r,cy,&redge,&dummy);
   rr=redge-rcx;
   if(rr<0) rr=-rr;
   if(rr<=0) return;
   bb.valid=TRUE;
   bb.xmin=cx-r; bb.ymin=cy-r;
   bb.xmax=cx+r; bb.ymax=cy+r;
   if(rs->fillvalid)
   {  Buildfillctx(&pctx,dec,rs,Effectivefillalpha(rs),&bb);
      Setpen(dec,rs->fillpen);
      Drawellipse_fill(dec,rcx,rcy,rr,rr,&pctx);
   }
   if(rs->strokevalid)
      Strokeellipse(dec,rcx,rcy,rr,rr,rs);
}

static void Renderellipse(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  LONG cx=Numattr(node,"cx",0);
   LONG cy=Numattr(node,"cy",0);
   LONG rx=Numattr(node,"rx",0);
   LONG ry=Numattr(node,"ry",0);
   LONG rcx,rcy,redgex,redgey,dummyx,dummyy;
   LONG rrx,rry;
   struct Paintctx pctx;
   struct Bbox bb;
   if(rx<=0 || ry<=0) return;
   Mxform(&rs->M,cx,cy,&rcx,&rcy);
   Mxform(&rs->M,cx+rx,cy,&redgex,&dummyy);
   Mxform(&rs->M,cx,cy+ry,&dummyx,&redgey);
   rrx=redgex-rcx; if(rrx<0) rrx=-rrx;
   rry=redgey-rcy; if(rry<0) rry=-rry;
   if(rrx<=0 || rry<=0) return;
   bb.valid=TRUE;
   bb.xmin=cx-rx; bb.ymin=cy-ry;
   bb.xmax=cx+rx; bb.ymax=cy+ry;
   if(rs->fillvalid)
   {  Buildfillctx(&pctx,dec,rs,Effectivefillalpha(rs),&bb);
      Setpen(dec,rs->fillpen);
      Drawellipse_fill(dec,rcx,rcy,rrx,rry,&pctx);
   }
   if(rs->strokevalid)
      Strokeellipse(dec,rcx,rcy,rrx,rry,rs);
}

static void Renderline(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  LONG x1=Numattr(node,"x1",0);
   LONG y1=Numattr(node,"y1",0);
   LONG x2=Numattr(node,"x2",0);
   LONG y2=Numattr(node,"y2",0);
   LONG rx1,ry1,rx2,ry2;
   struct Point32 pts[2];
   if(!rs->strokevalid) return;
   Mxform(&rs->M,x1,y1,&rx1,&ry1);
   Mxform(&rs->M,x2,y2,&rx2,&ry2);
   pts[0].x=rx1; pts[0].y=ry1;
   pts[1].x=rx2; pts[1].y=ry2;
   Strokepolyline(dec,pts,2,FALSE,rs);
}

static void Renderpoly(struct Decoder *dec, struct XmlNode *node,
   struct Renderstate *rs, BOOL closeit)
{  UBYTE *pts_str=XmlAttrValue(node,"points");
   struct Point32 *pts;
   WORD n;
   struct Paintctx pctx;
   struct Bbox bb;
   if(!pts_str) return;
   /* Parsepoints now accumulates the user-space bbox while it
    * tokenises the points= string, so we get a correct
    * objectBoundingBox even when the polygon is rendered through a
    * rotated / sheared CTM. */
   n=Parsepoints(dec,&rs->M,pts_str,&pts,&bb);
   if(n<2) return;
   if(rs->fillvalid && closeit && n>=3)
   {  Buildfillctx(&pctx,dec,rs,Effectivefillalpha(rs),&bb);
      Setpen(dec,rs->fillpen);
      Drawpolygon_fill(dec,pts,n,&pctx);
   }
   if(rs->strokevalid)
      Strokepolyline(dec,pts,n,closeit,rs);
}

/* Rough upper bound on how many raster vertices a path may emit.
 * Used to pre-size the emitter's point array so a 5000-vertex
 * boundary path doesn't have to claw its way through six rounds of
 * realloc-and-memcpy.
 *
 * Each path command consumes one or more parameter sets in the
 * d="..." string.  We just count the command letters and assume the
 * worst case for each: M/L/H/V/A emit one point, Z emits none, and
 * the bezier commands C/S/Q/T may emit several after adaptive
 * subdivision.  Pick generous multipliers - allocating slightly
 * too much is far cheaper than re-allocing. */
static WORD Pathestimate(const UBYTE *d)
{  WORD n=8;                  /* startup slack for sub-path opens */
   if(!d) return 64;
   while(*d)
   {  UBYTE c=*d;
      switch(c)
      {  case 'M': case 'm':
         case 'L': case 'l':
         case 'H': case 'h':
         case 'V': case 'v':
            n++;
            break;
         case 'A': case 'a':
            /* Each elliptical arc is decomposed into up to 128 line
             * samples by Arcsegment.  Budget ~32 vertices so paths
             * dominated by arcs (a typical icon outline) don't
             * realloc their point pool on every single arc. */
            n+=32;
            break;
         case 'C': case 'c':
         case 'S': case 's':
            n+=16;
            break;
         case 'Q': case 'q':
         case 'T': case 't':
            n+=8;
            break;
         default:
            break;
      }
      d++;
   }
   if(n<64) n=64;
   if(n>POLY_MAX_VERTS) n=POLY_MAX_VERTS;
   return n;
}

static void Renderpath(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  UBYTE *d=XmlAttrValue(node,"d");
   struct Pathemit pe;
   if(!d) return;
   pe.dec=dec;
   pe.rs=rs;
   pe.M=&rs->M;
   pe.capacity=Pathestimate(d);
   pe.pts=(struct Point32 *)AllocPooled(dec->pool,sizeof(struct Point32)*pe.capacity);
   if(!pe.pts) return;
   Pathreset(&pe);
   pe.subpathopen=FALSE;
   Parsepath(&pe,d);
}

/*--------------------------------------------------------------------*/
/* id -> node map and <use> support                                   */
/*--------------------------------------------------------------------*/

/* Case-sensitive byte equality - XML ids are case sensitive. */
static int Streq(const UBYTE *a, const UBYTE *b)
{  while(*a && *b)
   {  if(*a!=*b) return 0;
      a++; b++;
   }
   return *a==0 && *b==0;
}

/* Tiny djb2-style hash modulo IDTABLE_SIZE. */
static ULONG Hashid(const UBYTE *s)
{  ULONG h=5381;
   while(*s) { h=((h<<5)+h)+(ULONG)(*s); s++; }
   return h&(IDTABLE_SIZE-1);
}

/* Insert one node into the id table.  Called recursively on every
 * element after the DOM has been parsed.  Each entry is pool-allocated
 * so no explicit cleanup is needed. */
static void Indexids(struct Decoder *dec, struct XmlNode *node)
{  struct XmlNode *child;
   if(!node) return;
   if(node->type==XMLN_ELEMENT)
   {  UBYTE *id=XmlAttrValue(node,"id");
      if(id && *id)
      {  ULONG h=Hashid(id);
         struct Iddef *e=(struct Iddef *)AllocPooled(dec->pool,sizeof(*e));
         if(e)
         {  e->id=id;
            e->node=node;
            e->grad_rgb=0;
            e->grad_alpha=0;
            e->grad_state=0;
            e->grad=NULL;
            e->next=dec->idtable[h];
            dec->idtable[h]=e;
         }
      }
   }
   for(child=node->firstchild;child;child=child->nextsibling)
      Indexids(dec,child);
}

/* Look up an Iddef by id.  Returns NULL if not found.  Used by both
 * Findid (which returns just the node) and the gradient resolver
 * (which also reads/writes the cached average colour). */
static struct Iddef *Findid_iddef(struct Decoder *dec, const UBYTE *id)
{  struct Iddef *e;
   ULONG h;
   if(!id || !*id) return NULL;
   h=Hashid(id);
   for(e=dec->idtable[h];e;e=e->next)
      if(Streq(e->id,id)) return e;
   return NULL;
}

/* Look up a node by id.  Returns NULL if not found. */
static struct XmlNode *Findid(struct Decoder *dec, const UBYTE *id)
{  struct Iddef *e=Findid_iddef(dec,id);
   return e ? e->node : NULL;
}

/* Resolve a "#id" reference.  Returns the bare id (after stripping the
 * leading '#') or NULL if the reference is malformed or external. */
static UBYTE *Resolvehref(struct XmlNode *node)
{  UBYTE *href=XmlAttrValue(node,"xlink:href");
   if(!href) href=XmlAttrValue(node,"href");
   if(!href) return NULL;
   while(*href==' '||*href=='\t') href++;
   if(*href!='#') return NULL;
   return href+1;
}

/* Render a <use> element.  Resolves the referenced node, applies the
 * use element's own x/y/transform/style on top of the inherited state,
 * then renders the referenced subtree as if it were a child.  Cycles
 * are blocked by a depth counter. */
static void Renderuse(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  UBYTE *idref;
   struct XmlNode *target;
   LONG ux,uy;
   struct Matrix T;
   if(dec->usedepth>=USE_MAX_DEPTH) return;
   idref=Resolvehref(node);
   if(!idref) return;
   target=Findid(dec,idref);
   if(!target) return;
   /* Use's x and y attributes act as an additional translate that is
    * applied before the referenced subtree's own transform. */
   ux=Numattr(node,"x",0);
   uy=Numattr(node,"y",0);
   if(ux || uy)
   {  Midentity(&T);
      T.e=ux;
      T.f=uy;
      Mcompose(&rs->M,&rs->M,&T);
   }
   /* Render the target.  <symbol> is normally invisible during a
    * regular walk, so handle it explicitly: treat it as a <g> here so
    * its children inherit the use's transform/style cascade. */
   dec->usedepth++;
   if(XmlNameIs(target,"symbol"))
   {  struct XmlNode *child;
      struct Renderstate child_rs=*rs;
      Applytransform(target,&child_rs);
      Applystyle(dec,target,&child_rs);
      for(child=target->firstchild;child;child=child->nextsibling)
         Renderelement(dec,child,&child_rs);
   }
   else
   {  Renderelement(dec,target,rs);
   }
   dec->usedepth--;
}

/*--------------------------------------------------------------------*/
/* Recursive renderer                                                 */
/*--------------------------------------------------------------------*/

/* Append a deferred <text> run to dec->textruns.  Real rendering does
 * not happen until Parsertask has flushed the vector layer to the
 * output BitMap; see svgtext.h for the rationale behind deferred
 * text.  When ttengine.library is not available
 * (SvgTextIsAvailable()==FALSE) the call is a documented no-op,
 * which is precisely what we want here: an SVG icon without its
 * labels is still useful. */
static void Rendertext(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  LONG Mvec[6];
   if(!SvgTextIsAvailable()) return;
   if(!rs->fillvalid) return;
   /* Pack the 16.16 affine matrix into the layout svgtext.c expects
    * (a,b,c,d,e,f).  Going via an explicit copy avoids relying on
    * the in-memory ordering of struct Matrix members, which is
    * compiler-stable but not part of the public contract. */
   Mvec[0]=rs->M.a; Mvec[1]=rs->M.b; Mvec[2]=rs->M.c;
   Mvec[3]=rs->M.d; Mvec[4]=rs->M.e; Mvec[5]=rs->M.f;
   SvgTextAccumulate(dec->pool,&dec->textruns,node,
      Mvec,rs->fillrgb,(BOOL)rs->fillvalid);
}

/* Trampoline used by SvgTextRenderAll's pen_obtain callback.  Routes
 * back into the static Getpen() so allocated pens are tracked on
 * source->allocated[] for proper Disposesource release. */
static UBYTE Textrender_pen_cb(void *ctx, ULONG rgb)
{  return Getpen((struct Decoder *)ctx,rgb);
}

static void Renderelement(struct Decoder *dec, struct XmlNode *node, struct Renderstate *parent)
{  struct Renderstate rs;
   UBYTE feff,seff;
   if(!node || node->type!=XMLN_ELEMENT) return;
   rs=*parent;
   Applytransform(node,&rs);
   Applystyle(dec,node,&rs);
   if(!rs.visible) return;     /* display:none */

   /* Combine the three opacity sources into a single effective alpha
    * per channel and gate the paint flags.  We do not blend; below
    * threshold we simply skip the drawing call.  This is a cheap
    * proxy for real compositing that turns near-transparent shapes
    * into a no-op rather than painting them as if they were opaque. */
   feff=Combinealpha(rs.fill_color_alpha,rs.fill_opacity);
   feff=Combinealpha(feff,rs.element_opacity);
   if(feff<ALPHA_SKIP) rs.fillvalid=FALSE;
   seff=Combinealpha(rs.stroke_color_alpha,rs.stroke_opacity);
   seff=Combinealpha(seff,rs.element_opacity);
   if(seff<ALPHA_SKIP) rs.strokevalid=FALSE;

   if(XmlNameIs(node,"g") || XmlNameIs(node,"svg") || XmlNameIs(node,"a"))
   {  struct XmlNode *child;
      for(child=node->firstchild;child;child=child->nextsibling)
      {  Renderelement(dec,child,&rs);
      }
      return;
   }
   if(XmlNameIs(node,"use"))          { Renderuse(dec,node,&rs); return; }
   if(XmlNameIs(node,"rect"))         { Renderrect(dec,node,&rs); return; }
   if(XmlNameIs(node,"circle"))       { Rendercircle(dec,node,&rs); return; }
   if(XmlNameIs(node,"ellipse"))      { Renderellipse(dec,node,&rs); return; }
   if(XmlNameIs(node,"line"))         { Renderline(dec,node,&rs); return; }
   if(XmlNameIs(node,"polygon"))      { Renderpoly(dec,node,&rs,TRUE); return; }
   if(XmlNameIs(node,"polyline"))     { Renderpoly(dec,node,&rs,FALSE); return; }
   if(XmlNameIs(node,"path"))         { Renderpath(dec,node,&rs); return; }
   if(XmlNameIs(node,"text"))         { Rendertext(dec,node,&rs); return; }
   /* defs, symbol, title, desc, metadata, style, script - never
    * rendered during a regular walk.  Symbol contents are reached
    * exclusively via <use>. */
}

/*--------------------------------------------------------------------*/
/* viewBox handling                                                   */
/*--------------------------------------------------------------------*/

/* Read width/height/viewBox from the root <svg> and decide our
 * bitmap dimensions and root transform. */
static void Setupviewport(struct Decoder *dec, struct XmlNode *root,
   long *bmw, long *bmh, struct Matrix *initial)
{  LONG svgw, svgh;
   LONG vbx=0, vby=0, vbw=0, vbh=0;
   BOOL havevb=FALSE;
   UBYTE *vbstr;
   LONG w,h;
   LONG sx,sy;
   LONG vbwu,vbhu;
   (void)dec; /* reserved for future bbox-driven canvas sizing */

   svgw=Numattr(root,"width",0);
   svgh=Numattr(root,"height",0);

   vbstr=XmlAttrValue(root,"viewBox");
   if(vbstr)
   {  UBYTE *p=vbstr;
      UBYTE *e=vbstr;
      BOOL ok;
      while(*e) e++;
      vbx=Parsefixed(&p,e,&ok);
      if(ok) { vby=Parsefixed(&p,e,&ok); }
      if(ok) { vbw=Parsefixed(&p,e,&ok); }
      if(ok) { vbh=Parsefixed(&p,e,&ok); }
      if(ok && vbw>0 && vbh>0) havevb=TRUE;
   }

   /* Fill in missing root dimensions.
    *
    * Order of preference, matching SVG 1.1 spec section 7.10 as
    * closely as is feasible without a real CSS layout engine:
    *  1) An explicit value on the <svg> element wins.
    *  2) If a viewBox is present, its width/height supplies the
    *     missing dimension (so the rendered raster has the viewBox
    *     aspect ratio).
    *  3) If only one of width/height is given and there is no
    *     viewBox, mirror the present one.  This is what fixes the
    *     famous Inkscape tiger.svg (height="800", no width, no
    *     viewBox) - the previous DEFAULT_DIM fallback produced a
    *     100x800 column that clipped most of the picture.
    *  4) If both are missing and there is no viewBox, default to a
    *     3*DEFAULT_DIM square - 300x300 is what most browsers use
    *     for an SVG with no intrinsic size.  We can't compute a
    *     content bbox here without doing a full pre-walk of the DOM
    *     with all transforms applied, which is too expensive to do
    *     up front. */
   if(svgw<=0 && havevb) svgw=vbw;
   if(svgh<=0 && havevb) svgh=vbh;
   if(svgw<=0 && svgh>0) svgw=svgh;
   if(svgh<=0 && svgw>0) svgh=svgw;
   if(svgw<=0) svgw=(DEFAULT_DIM*3)<<16;
   if(svgh<=0) svgh=(DEFAULT_DIM*3)<<16;

   w=svgw>>16;
   h=svgh>>16;
   if(w<=0) w=DEFAULT_DIM;
   if(h<=0) h=DEFAULT_DIM;
   if(w>MAX_BITMAP_DIM)
   {  /* Preserve aspect ratio when clamping. */
      LONG nh=(h*MAX_BITMAP_DIM + (w/2))/w;
      if(nh<1) nh=1;
      w=MAX_BITMAP_DIM;
      h=nh;
   }
   if(h>MAX_BITMAP_DIM)
   {  LONG nw=(w*MAX_BITMAP_DIM + (h/2))/h;
      if(nw<1) nw=1;
      h=MAX_BITMAP_DIM;
      w=nw;
   }

   /* Memory-adaptive secondary cap.  Up to this point sizing has
    * been driven entirely by what the SVG declares and the
    * MAX_BITMAP_DIM ceiling.  Now consult the actual system: if the
    * chunky intermediate we are about to allocate would consume
    * more than CHUNKY_BUDGET_DIVISOR-th of the largest free public
    * memory block, or breach the hard ceiling, scale BOTH
    * dimensions down proportionally until it fits.
    *
    * This is what lets an A1200 with 4MB free fast still preview a
    * 4000x3000 Wikipedia SVG at a reduced resolution rather than
    * silently failing the AllocVec further down. */
   {  ULONG needed;
      ULONG largest;
      ULONG budget;
      needed  = (ULONG)w * (ULONG)h * 3UL;
      largest = AvailMem(MEMF_PUBLIC|MEMF_LARGEST);
      budget  = largest / CHUNKY_BUDGET_DIVISOR;
      if(budget > CHUNKY_HARD_CEILING) budget = CHUNKY_HARD_CEILING;
      while(needed > budget && w > MIN_BITMAP_DIM && h > MIN_BITMAP_DIM)
      {  LONG nw = (w * 9L) / 10L;
         LONG nh = (h * 9L) / 10L;
         if(nw < MIN_BITMAP_DIM) nw = MIN_BITMAP_DIM;
         if(nh < MIN_BITMAP_DIM) nh = MIN_BITMAP_DIM;
         if(nw == w && nh == h) break;
         w = nw;
         h = nh;
         needed = (ULONG)w * (ULONG)h * 3UL;
      }
   }

   /* Secondary cap for CHIP RAM.  The downstream AllocBitMap for
    * the palette destination pulls from chip on AGA / ECS targets;
    * a chip exhaustion there silently produces an empty bitmap.
    * Worst-case estimate is one byte per pixel (8 bitplanes,
    * MEMF_CHIP).  On systems whose fast/chip ratio looks like an
    * RTG box we relax the cap because the destination probably
    * lives in graphics card memory. */
   {  ULONG chip_largest;
      ULONG fast_largest;
      ULONG chip_needed;
      ULONG chip_budget;
      BOOL  rtg_likely;
      chip_largest = AvailMem(MEMF_CHIP|MEMF_LARGEST);
      fast_largest = AvailMem(MEMF_FAST|MEMF_LARGEST);
      rtg_likely = (BOOL)(fast_largest >= CHIP_RTG_FAST_FLOOR
                  && chip_largest > 0
                  && fast_largest / chip_largest >= CHIP_RTG_FAST_RATIO);
      if(rtg_likely)
      {  chip_budget = chip_largest;
      }
      else
      {  if(chip_largest > CHIP_HEADROOM_BYTES)
            chip_budget = (chip_largest - CHIP_HEADROOM_BYTES)
                          / CHIP_BUDGET_DIVISOR;
         else
            chip_budget = 0;
         if(chip_budget < CHIP_MIN_BUDGET_BYTES)
            chip_budget = CHIP_MIN_BUDGET_BYTES;
      }
      chip_needed = (ULONG)w * (ULONG)h;
      while(chip_needed > chip_budget
         && w > MIN_BITMAP_DIM
         && h > MIN_BITMAP_DIM)
      {  LONG nw = (w * 9L) / 10L;
         LONG nh = (h * 9L) / 10L;
         if(nw < MIN_BITMAP_DIM) nw = MIN_BITMAP_DIM;
         if(nh < MIN_BITMAP_DIM) nh = MIN_BITMAP_DIM;
         if(nw == w && nh == h) break;
         w = nw;
         h = nh;
         chip_needed = (ULONG)w * (ULONG)h;
      }
   }

   *bmw=w;
   *bmh=h;

   Midentity(initial);
   if(havevb)
   {  /* Map (vbx,vby)..(vbx+vbw,vby+vbh) to (0,0)..(w,h).
       * Keep this integer-only to avoid pulling in the SAS/C floating
       * point runtime.  Fractional viewBox dimensions are rounded to
       * the nearest user unit; that is acceptable for real-world SVGs
       * and avoids fragile plugin link dependencies. */
      vbwu=(vbw+(1L<<15))>>16;
      vbhu=(vbh+(1L<<15))>>16;
      if(vbwu<1) vbwu=1;
      if(vbhu<1) vbhu=1;
      sx=(w<<16)/vbwu;
      sy=(h<<16)/vbhu;
      initial->a=sx;
      initial->d=sy;
      initial->e=-fmul(sx,vbx);
      initial->f=-fmul(sy,vby);
   }
   /* No viewBox - SVG coords map 1:1 to raster pixels (identity). */
}

/*--------------------------------------------------------------------*/
/* The parser subtask                                                 */
/*--------------------------------------------------------------------*/

static void Parsertask(void *userdata)
{  struct Decoder dec;
   struct Svgsource *ss;
   struct Datablock *db;
   struct XmlNode *root;
   struct Renderstate rs;
   struct Matrix initial;
   long bmw=0, bmh=0;
   long offset;
   /* Reported bitmap byte usage.  Sent to AWeb via AOSVG_Memory once
    * the bitmap is allocated so the layout-cache flusher (source.c,
    * case AOSRC_Memory) can pick this source for eviction under chip
    * RAM pressure.  Without this report AWeb thinks the SVG source
    * costs zero bytes and silently keeps every previously-rendered
    * image alive long after the user has navigated away - which on
    * AGA / ECS machines exhausts chip RAM after a handful of pages. */
   long bitmap_bytes=0;
   UBYTE bgpen=0;

   memset(&dec,0,sizeof(dec));
   dec.currentpen=-1;
   SvgTextListInit(&dec.textruns);
   ss=(struct Svgsource *)userdata;
   if(!ss)
   {  struct Task *t=FindTask(NULL);
      struct Aobject *to;
      if(t && t->tc_UserData)
      {  to=(struct Aobject *)t->tc_UserData;
         ss=(struct Svgsource *)Agetattr(to,AOTSK_Userdata);
      }
   }
   if(!ss) return;
   dec.source=ss;

   SVGLOG(("SVG: Parsertask start, ss=%p\n",ss));

   /* Wait for EOF.  Drain task messages along the way. */
   for(;;)
   {  struct Taskmsg *tm;
      ULONG cur;
      ObtainSemaphore(&ss->sema);
      cur=ss->flags;
      ReleaseSemaphore(&ss->sema);
      if(cur&SVGSF_EOF) break;
      if(dec.flags&DECOF_STOP) goto cleanup;
      while((tm=Gettaskmsg()))
      {  struct TagItem *tag,*tstate;
         if(tm->amsg && tm->amsg->method==AOM_SET)
         {  tstate=((struct Amset *)tm->amsg)->tags;
            while((tag=NextTagItem(&tstate)))
            {  if(tag->ti_Tag==AOTSK_Stop && tag->ti_Data)
                  dec.flags|=DECOF_STOP;
            }
         }
         Replytaskmsg(tm);
      }
      if(dec.flags&DECOF_STOP) goto cleanup;
      Waittask(0);
   }

   /* Concatenate all received data into one buffer. */
   dec.buflen=0;
   ObtainSemaphore(&ss->sema);
   for(db=ss->data.first;db && db->next;db=db->next) dec.buflen+=db->length;
   ReleaseSemaphore(&ss->sema);
   if(dec.buflen<=0) goto cleanup;
   if(dec.buflen>(2*1024*1024)) dec.buflen=2*1024*1024;
   dec.buffer=(UBYTE *)AllocVec(dec.buflen+1,MEMF_PUBLIC);
   if(!dec.buffer) goto cleanup;
   offset=0;
   ObtainSemaphore(&ss->sema);
   for(db=ss->data.first;db && db->next && offset<dec.buflen;db=db->next)
   {  long copy=db->length;
      if(offset+copy>dec.buflen) copy=dec.buflen-offset;
      memcpy(dec.buffer+offset,db->data,copy);
      offset+=copy;
   }
   ReleaseSemaphore(&ss->sema);
   dec.buffer[dec.buflen]=0;

   /* Create the parse pool.  Sized so most SVG icons fit one or two
    * 32KB puddles without a second allocator round-trip. */
   dec.pool=CreatePool(MEMF_PUBLIC,32768UL,4096UL);
   if(!dec.pool) goto cleanup;

   /* Parse the XML. */
   root=XmlParse(dec.pool,dec.buffer,dec.buflen);
   if(!root) { SVGLOG(("SVG: XML parse returned NULL root\n")); goto cleanup; }

   /* Pre-walk the DOM once to build the id-map used by <use> /
    * xlink:href resolution.  Doing this up front means lookups during
    * rendering are O(1) hash hits instead of O(n) tree searches. */
   Indexids(&dec,root);

   /* Compute viewport. */
   Setupviewport(&dec,root,&bmw,&bmh,&initial);
   dec.bmw=bmw;
   dec.bmh=bmh;
   SVGLOG(("SVG: bitmap %ldx%ld\n",bmw,bmh));

   /* Get screen + colormap. */
   if(Agetattr(Aweb(),AOAPP_Screenvalid))
   {  Agetattrs(Aweb(),
         AOAPP_Screen,&dec.screen,
         AOAPP_Colormap,&ss->colormap,
         TAG_END);
   }
   if(!dec.screen) goto cleanup;
   if(dec.screen->RastPort.BitMap)
      ss->friendbitmap=dec.screen->RastPort.BitMap;

   /* Allocate the output bitmap.  Prefer a Picasso96 true-colour surface
    * when the browser is running on one (same policy as PNG/GIF/JFIF);
    * otherwise fall back to an 8-bit palette bitmap. */
   {  ULONG depth=8;
      ULONG bytes_per_pixel=1;
      if(P96Base && ss->friendbitmap
      && p96GetBitMapAttr(ss->friendbitmap,P96BMA_ISP96))
      {  depth=p96GetBitMapAttr(ss->friendbitmap,P96BMA_DEPTH);
         dec.bitmap=p96AllocBitMap(bmw,bmh,depth,
            BMF_MINPLANES|BMF_CLEAR|BMF_DISPLAYABLE,ss->friendbitmap,RGBFB_NONE);
         if(dec.bitmap && p96GetBitMapAttr(dec.bitmap,P96BMA_ISP96))
         {  dec.decflags|=DECOF_P96MAP;
            if(depth>8) dec.decflags|=DECOF_P96DEEP;
         }
         else
         {  if(dec.bitmap) { p96FreeBitMap(dec.bitmap); dec.bitmap=NULL; }
         }
      }
      if(!dec.bitmap)
      {  if(dec.screen->RastPort.BitMap)
            depth=GetBitMapAttr(dec.screen->RastPort.BitMap,BMA_DEPTH);
         if(depth<1) depth=8;
         if(depth>8) depth=8;
         dec.bitmap=AllocBitMap(bmw,bmh,depth,BMF_CLEAR,dec.screen->RastPort.BitMap);
      }
      /* Compute a nominal byte count for AWeb's source memory tally.
       * For palette bitmaps we under-report slightly (planar layout
       * pads each row to a 16-bit boundary) and for P96 deep
       * destinations we under-report by the per-row stride padding,
       * but both are close enough for cache pressure decisions and
       * avoid relying on optional p96 attribute queries that might
       * not exist on every Picasso96 build. */
      bytes_per_pixel=(depth+7UL)>>3;
      if(bytes_per_pixel<1UL) bytes_per_pixel=1UL;
      bitmap_bytes=(long)((ULONG)bmw*(ULONG)bmh*bytes_per_pixel);
   }
   if(!dec.bitmap) goto cleanup;
   InitRastPort(&dec.rp);
   dec.rp.BitMap=dec.bitmap;
   /* NOTE: bitmap_bytes is reported to AWeb later, AFTER the render
    * walk has completed and dec.bitmap has been transferred to
    * ss->bitmap.  Reporting it here (i.e. before the render) caused a
    * visible regression on multi-path SVGs such as the Wikipedia
    * logo: AOSRC_Memory in source.c sets flushsources=TRUE via
    * Deferflushmem, and the main task processes the resulting
    * Flushexcess pass while the parser subtask is still drawing into
    * a bitmap that is not yet owned by the source object.  PNG and
    * JFIF announce their memory only after their source bitmap is
    * wired up, and the SVG plugin must follow the same ordering. */
   if(dec.decflags&DECOF_P96DEEP)
   {  /* Allocate one R8G8B8 framebuffer for the entire SVG and write
       * into it directly from every drawing primitive.  At the end
       * of Parsertask we hand the whole thing to p96WritePixelArray
       * in a single call - which is the only call into the RTG
       * driver for the entire frame.  This is dramatically faster
       * than the previous per-span p96 round-trip, especially on
       * complex map-class SVGs that issue tens of thousands of
       * spans.  Memory cost is width*height*3 bytes - well below
       * the source data budget. */
      dec.chunkybpr=bmw*3;
      dec.chunky=(UBYTE *)AllocVec((ULONG)dec.chunkybpr*(ULONG)bmh,MEMF_PUBLIC);
      if(!dec.chunky) goto cleanup;
      /* Initialise to white - SVG has no background by default and
       * sits in a layout cell that is itself painted before us, but
       * white is what every Inkscape / Wikimedia file expects to
       * see through the unpainted parts of an icon. */
      memset(dec.chunky,0xff,(size_t)dec.chunkybpr*(size_t)bmh);
      dec.ri.Memory=dec.chunky;
      dec.ri.BytesPerRow=dec.chunkybpr;
      dec.ri.RGBFormat=RGBFB_R8G8B8;
   }
   else
   {  dec.currentpen=-1;
      /* Keep palette destinations visually consistent with the RTG chunky
       * path above.  Pen 0 is often the Workbench/AWeb grey, which makes
       * light grey icons such as mac.svg appear blank against themselves. */
      bgpen=Getpen(&dec,0xffffffUL);
      Setpen(&dec,bgpen);
      RectFill(&dec.rp,0,0,bmw-1,bmh-1);
   }

   /* Pick the adaptive rendering quality tier now that we know both
    * the destination capabilities (DECOF_P96DEEP) and the document
    * geometry (bmw/bmh, gradient population in the id table).  Does
    * the first-call benchmark on demand. */
   Decidequality(&dec);

   /* Render. */
   rs.M=initial;
   rs.fillpen=1;
   rs.strokepen=1;
   rs.fillvalid=TRUE;       /* SVG default fill is black */
   rs.strokevalid=FALSE;
   rs.visible=TRUE;
   rs.strokewidth=0x10000L;
   rs.fillrgb=0x000000UL;
   rs.strokergb=0x000000UL;
   /* Opacity model: everything fully opaque by default so the alpha
    * gate in Renderelement is a no-op for SVGs that don't bother with
    * opacity at all. */
   rs.element_opacity=255;
   rs.fill_opacity=255;
   rs.stroke_opacity=255;
   rs.fill_color_alpha=255;
   rs.stroke_color_alpha=255;
   rs.fillgrad=NULL;
   /* Resolve default black pen up front so primitives without
    * explicit fill don't trigger a per-call ObtainBestPen. */
   rs.fillpen=Getpen(&dec,0x000000UL);

   if(XmlNameIs(root,"svg"))
   {  struct XmlNode *child;
      Applytransform(root,&rs);
      Applystyle(&dec,root,&rs);
      for(child=root->firstchild;child;child=child->nextsibling)
      {  Renderelement(&dec,child,&rs);
      }
   }
   else
   {  Renderelement(&dec,root,&rs);
   }

   /* P96 deep: flush the entire R8G8B8 framebuffer to the output
    * bitmap in a single RTG call.  Every drawing primitive above
    * wrote straight into dec.chunky, so this is the only time the
    * Picasso96 driver is touched for the whole frame. */
   if(dec.decflags&DECOF_P96DEEP)
   {  p96WritePixelArray(&dec.ri,0,0,&dec.rp,0,0,bmw,bmh);
   }

   /* Render all accumulated <text> runs via ttengine.library.  This
    * runs AFTER the chunky -> bitmap commit above because TT_Text
    * writes directly into the BitMap (via the RastPort) and would
    * otherwise be overwritten by the p96WritePixelArray flush.  The
    * trade-off is that text always lands on top of the vector layer
    * regardless of document order - acceptable for the SVG content
    * domain (icons, diagrams, maps, charts, labels).  When
    * ttengine.library is unavailable this call is a documented
    * no-op and any queued runs are silently dropped. */
   if(dec.textruns.count>0)
   {  SvgTextRenderAll(&dec.rp,dec.screen,ss->colormap,&dec.textruns,
         Textrender_pen_cb,&dec);
   }

   /* Hand the bitmap to the source object. */
   ObtainSemaphore(&ss->sema);
   ss->width=bmw;
   ss->height=bmh;
   ss->bitmap=dec.bitmap;
   ss->flags|=SVGSF_IMAGEREADY;
   ReleaseSemaphore(&ss->sema);
   dec.bitmap=NULL; /* now owned by ss */

   /* Announce final state in one round trip: width, height, ready
    * flags and the AOSVG_Memory tally that feeds AWeb's chip-RAM
    * cache flusher.  The memory line MUST be in this message rather
    * than emitted earlier: ss->bitmap is now wired up, so if a
    * subsequent Flushexcess pass disposes us we will tear down
    * cleanly via Disposesource -> Releaseimage instead of orphaning
    * the still-subtask-owned bitmap. */
   if(bitmap_bytes>0)
   {  Updatetaskattrs(
         AOSVG_Width,bmw,
         AOSVG_Height,bmh,
         AOSVG_Imgready,TRUE,
         AOSVG_Parseready,TRUE,
         AOSVG_Memory,bitmap_bytes,
         TAG_END);
   }
   else
   {  Updatetaskattrs(
         AOSVG_Width,bmw,
         AOSVG_Height,bmh,
         AOSVG_Imgready,TRUE,
         AOSVG_Parseready,TRUE,
         TAG_END);
   }

cleanup:
   SVGLOG(("SVG: Parsertask cleanup\n"));
   if(dec.chunky) FreeVec(dec.chunky);
   if(dec.bitmap)
   {  if(P96Base && p96GetBitMapAttr(dec.bitmap,P96BMA_ISP96))
         p96FreeBitMap(dec.bitmap);
      else
         FreeBitMap(dec.bitmap);
   }
   if(dec.pool) DeletePool(dec.pool);
   if(dec.buffer) FreeVec(dec.buffer);
   if(ss && !ss->bitmap)
   {  Updatetaskattrs(AOSVG_Error,TRUE,TAG_END);
   }
}

/*--------------------------------------------------------------------*/
/* AOM_ dispatch                                                      */
/*--------------------------------------------------------------------*/

static void Startparser(struct Svgsource *ss)
{  struct Screen *screen=NULL;
   if(!ss || ss->task) return;
   if(!Agetattr(Aweb(),AOAPP_Screenvalid)) return;
   Agetattrs(Aweb(),AOAPP_Screen,&screen,TAG_END);
   if(!screen) return;
   ss->task=Anewobject(AOTP_TASK,
      AOTSK_Entry,Parsertask,
      AOTSK_Name,"AWebSvg parser",
      AOTSK_Userdata,ss,
      AOBJ_Target,ss,
      TAG_END);
   if(ss->task)
   {  void *check=(void *)Agetattr(ss->task,AOTSK_Userdata);
      if(check!=ss) Asetattrs(ss->task,AOTSK_Userdata,ss,TAG_END);
      Asetattrs(ss->task,AOTSK_Start,TRUE,TAG_END);
   }
}

static ULONG Getsource(struct Svgsource *ss, struct Amset *amset)
{  struct TagItem *tag,*tstate;
   AmethodasA(AOTP_SOURCEDRIVER,(struct Aobject *)ss,(struct Amessage *)amset);
   tstate=amset->tags;
   while((tag=NextTagItem(&tstate)))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            PUTATTR(tag,ss->source);
            break;
         case AOSDV_Saveable:
            PUTATTR(tag,(ss->flags&SVGSF_EOF)?TRUE:FALSE);
            break;
      }
   }
   return 0;
}

/* Forward declaration: defined further down alongside the other
 * source teardown helpers.  Needed here so the AOAPP_Screenvalid
 * FALSE branch in Setsource can share the same teardown sequence as
 * Disposesource - including the Anotifyset(AOSVG_Bitmap, NULL, ...)
 * that tells our Svgcopy children to drop their references before
 * we FreeBitMap, and the AOSRC_Memory=0 update that releases our
 * contribution to the cache flusher's tally. */
static void Releaseimage(struct Svgsource *ss);

static ULONG Setsource(struct Svgsource *ss, struct Amset *amset)
{  struct TagItem *tag,*tstate;
   Amethodas(AOTP_SOURCEDRIVER,(struct Aobject *)ss,AOM_SET,amset->tags);
   tstate=amset->tags;
   while((tag=NextTagItem(&tstate)))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            ss->source=(struct Aobject *)tag->ti_Data;
            break;
         case AOSDV_Displayed:
            if(tag->ti_Data)
            {  struct Datablock *first;
               ss->flags|=SVGSF_DISPLAYED;
               ObtainSemaphore(&ss->sema);
               first=ss->data.first->next;
               ReleaseSemaphore(&ss->sema);
               if(first && !ss->bitmap && !ss->task) Startparser(ss);
            }
            else
            {  ss->flags&=~SVGSF_DISPLAYED;
            }
            break;
         case AOAPP_Screenvalid:
            if(tag->ti_Data)
            {  struct Datablock *first;
               ObtainSemaphore(&ss->sema);
               first=ss->data.first->next;
               ReleaseSemaphore(&ss->sema);
               if(first && (ss->flags&SVGSF_DISPLAYED) && !ss->task)
                  Startparser(ss);
            }
            else
            {  /* Screen is going away: stop the parser so it cannot
                * be mid-write into the bitmap, then tear down the
                * image with Releaseimage so chip RAM is returned to
                * the system and AWeb's cache tally is zeroed.  The
                * source itself stays alive - if the screen comes
                * back later AOSDV_Displayed=TRUE in Setsource will
                * restart the parser and produce a fresh bitmap. */
               if(ss->task) { Adisposeobject(ss->task); ss->task=NULL; }
               Releaseimage(ss);
            }
            break;
      }
   }
   return 0;
}

static ULONG Updatesource(struct Svgsource *ss, struct Amset *amset)
{  struct TagItem *tag,*tstate;
   BOOL notify=FALSE;
   BOOL parseready=FALSE;
   tstate=amset->tags;
   while((tag=NextTagItem(&tstate)))
   {  switch(tag->ti_Tag)
      {  case AOSVG_Width:    ss->width=tag->ti_Data; notify=TRUE; break;
         case AOSVG_Height:   ss->height=tag->ti_Data; notify=TRUE; break;
         case AOSVG_Bitmap:
            ObtainSemaphore(&ss->sema);
            if(tag->ti_Data) ss->bitmap=(struct BitMap *)tag->ti_Data;
            ReleaseSemaphore(&ss->sema);
            notify=TRUE;
            break;
         case AOSVG_Mask:     ss->mask=(UBYTE *)tag->ti_Data; notify=TRUE; break;
         case AOSVG_Imgready:
            if(tag->ti_Data) { ss->flags|=SVGSF_IMAGEREADY; notify=TRUE; }
            else ss->flags&=~SVGSF_IMAGEREADY;
            break;
         case AOSVG_Parseready:
            if(tag->ti_Data) { ss->flags|=SVGSF_IMAGEREADY; parseready=TRUE; notify=TRUE; }
            break;
         case AOSVG_Memory:
            ss->memory+=tag->ti_Data;
            Asetattrs(ss->source,AOSRC_Memory,ss->memory,TAG_END);
            break;
      }
   }
   if(notify && ss->bitmap)
   {  Anotifyset(ss->source,
         AOSVG_Bitmap,ss->bitmap,
         AOSVG_Mask,ss->mask,
         AOSVG_Width,ss->width,
         AOSVG_Height,ss->height,
         AOSVG_Imgready,(ss->flags&SVGSF_IMAGEREADY)?TRUE:FALSE,
         AOSVG_Jsready,parseready?TRUE:FALSE,
         TAG_END);
   }
   return 0;
}

static struct Svgsource *Newsource(struct Amset *amset)
{  struct Svgsource *ss;
   ss=(struct Svgsource *)Allocobject(PluginBase->sourcedriver,sizeof(struct Svgsource),amset);
   if(!ss) return NULL;
   InitSemaphore(&ss->sema);
   NEWLIST(&ss->data);
   Aaddchild(Aweb(),(struct Aobject *)ss,AOREL_APP_USE_SCREEN);
   ss->width=0;
   ss->height=0;
   ss->bitmap=NULL;
   ss->mask=NULL;
   ss->memory=0;
   ss->flags=0;
   ss->source=NULL;
   ss->task=NULL;
   ss->colormap=NULL;
   ss->friendbitmap=NULL;
   memset(ss->allocated,0,sizeof(ss->allocated));
   Setsource(ss,amset);
   if(!(ss->flags&SVGSF_DISPLAYED))
   {  if(Agetattr(ss->source,AOSRC_Displayed))
         Asetattrs((struct Aobject *)ss,AOSDV_Displayed,TRUE,TAG_END);
   }
   return ss;
}

static void Releasedata(struct Svgsource *ss)
{  struct Datablock *db;
   /* Single AllocVec for struct + payload (see Srcupdatesource);
    * one FreeVec releases everything. */
   while((db=REMHEAD(&ss->data)))
   {  FreeVec(db);
   }
}

/* Notify any attached copy drivers that our bitmap is going away,
 * release the bitmap / mask / pens, and reset the per-image state.
 *
 * Must be called AFTER the parser subtask has been disposed.  The
 * subtask owns dec.bitmap during render and only transfers
 * ownership to ss->bitmap right before exiting; freeing ss->bitmap
 * while the subtask is still drawing would risk a use-after-free
 * in graphics.library or Picasso96.
 *
 * The Anotifyset(ss->source, AOSVG_Bitmap, NULL, ...) call mirrors
 * the convention used by the PNG and JFIF plugins: it forwards a
 * NULL bitmap down to every Svgcopy that referenced this source so
 * the copy driver clears its own bitmap pointer before we
 * deallocate the actual storage.  Without it, Disposecopy could
 * later dereference a dangling pointer if anything had set
 * SVGCF_OURBITMAP in the future.
 *
 * Finally, Asetattrs(ss->source, AOSRC_Memory, 0, ...) zeroes our
 * contribution to the global cache-pressure tally maintained in
 * source.c.  Because Parsertask reports bitmap_bytes via
 * AOSVG_Memory after allocation, this is what actually lets AWeb's
 * cache flusher reclaim our chip RAM when memory runs low. */
static void Releaseimage(struct Svgsource *ss)
{  short i;
   Anotifyset(ss->source,AOSVG_Bitmap,NULL,TAG_END);
   if(ss->bitmap)
   {  if(P96Base && p96GetBitMapAttr(ss->bitmap,P96BMA_ISP96))
         p96FreeBitMap(ss->bitmap);
      else
         FreeBitMap(ss->bitmap);
      ss->bitmap=NULL;
   }
   if(ss->mask)
   {  FreeVec(ss->mask);
      ss->mask=NULL;
   }
   if(ss->colormap)
   {  for(i=0;i<256;i++)
      {  while(ss->allocated[i])
         {  ReleasePen(ss->colormap,i);
            ss->allocated[i]--;
         }
      }
      ss->colormap=NULL;
   }
   ss->friendbitmap=NULL;
   ss->width=0;
   ss->height=0;
   ss->flags&=~SVGSF_IMAGEREADY;
   ss->memory=0;
   Asetattrs(ss->source,AOSRC_Memory,0,TAG_END);
}

static void Disposesource(struct Svgsource *ss)
{  if(ss->task) { Adisposeobject(ss->task); ss->task=NULL; }
   Releaseimage(ss);
   Releasedata(ss);
   Aremchild(Aweb(),(struct Aobject *)ss,AOREL_APP_USE_SCREEN);
   Amethodas(AOTP_SOURCEDRIVER,(struct Aobject *)ss,AOM_DISPOSE);
}

static ULONG Addchildsource(struct Svgsource *ss, struct Amadd *amadd)
{  if(amadd->relation==AOREL_SRC_COPY && ss->bitmap)
   {  Asetattrs(amadd->child,
         AOSVG_Bitmap,ss->bitmap,
         AOSVG_Mask,ss->mask,
         AOSVG_Width,ss->width,
         AOSVG_Height,ss->height,
         AOSVG_Imgready,ss->flags&SVGSF_IMAGEREADY,
         AOSVG_Jsready,ss->flags&SVGSF_IMAGEREADY,
         TAG_END);
   }
   return 0;
}

static ULONG Srcupdatesource(struct Svgsource *ss, struct Amsrcupdate *amsrcupdate)
{  struct TagItem *tag,*tstate;
   UBYTE *data=NULL;
   long datalength=0;
   struct Datablock *db;
   BOOL eof=FALSE;
   AmethodasA(AOTP_SOURCEDRIVER,(struct Aobject *)ss,(struct Amessage *)amsrcupdate);
   tstate=amsrcupdate->tags;
   while((tag=NextTagItem(&tstate)))
   {  switch(tag->ti_Tag)
      {  case AOURL_Data:       data=(UBYTE *)tag->ti_Data; break;
         case AOURL_Datalength: datalength=tag->ti_Data; break;
         case AOURL_Eof:
            if(tag->ti_Data)
            {  eof=TRUE;
               ObtainSemaphore(&ss->sema);
               ss->flags|=SVGSF_EOF;
               ReleaseSemaphore(&ss->sema);
            }
            break;
      }
   }
   if(data && datalength>0)
   {  /* Combined Datablock + payload allocation, matching the
       * refactored GIF/PNG plugins. */
      if(datalength>1024*1024) datalength=1024*1024;
      db=(struct Datablock *)AllocVec(sizeof(struct Datablock)+datalength,
                                       MEMF_PUBLIC|MEMF_CLEAR);
      if(db)
      {  db->data=(UBYTE *)(db+1);
         memcpy(db->data,data,datalength);
         db->length=datalength;
         ObtainSemaphore(&ss->sema);
         ADDTAIL(&ss->data,db);
         ReleaseSemaphore(&ss->sema);
      }
      if(!ss->task) Startparser(ss);
   }
   if((data && datalength>0) || eof)
   {  if(ss->task) Asetattrsasync(ss->task,AOSVG_Data,TRUE,TAG_END);
   }
   return 0;
}

__asm __saveds ULONG Dispatchsource(register __a0 struct Aobject *obj,
                                    register __a1 struct Amessage *amsg)
{  struct Svgsource *ss=(struct Svgsource *)obj;
   ULONG result=0;
   switch(amsg->method)
   {  case AOM_NEW:       result=(ULONG)Newsource((struct Amset *)amsg); break;
      case AOM_SET:       result=Setsource(ss,(struct Amset *)amsg); break;
      case AOM_GET:       result=Getsource(ss,(struct Amset *)amsg); break;
      case AOM_DISPOSE:   Disposesource(ss); break;
      case AOM_SRCUPDATE: result=Srcupdatesource(ss,(struct Amsrcupdate *)amsg); break;
      case AOM_UPDATE:    result=Updatesource(ss,(struct Amset *)amsg); break;
      case AOM_ADDCHILD:  result=Addchildsource(ss,(struct Amadd *)amsg); break;
      default:            result=AmethodasA(AOTP_SOURCEDRIVER,(struct Aobject *)ss,amsg); break;
   }
   return result;
}
