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

/* svgtext.h - Optional ttengine.library based text rendering for the
 *             SVG plugin.
 *
 * Architecture and scope
 * ----------------------
 *   - This module mirrors the font-handling path of the PDF plugin
 *     (Source/AWebPdfAPL/pdffont.c), but adapted to SVG semantics:
 *     SVG addresses fonts by family name, size and weight directly,
 *     so no base-14 substitution table or PostScript-name lookup is
 *     needed.  We pass the author's `font-family` through to
 *     ttengine.library verbatim and rely on its family table fallback
 *     to find a usable host TrueType font.
 *
 *   - Unlike the PDF plugin, ttengine.library is OPTIONAL here.  If
 *     ttengine could not be opened at plugin Init time (TTEngineBase
 *     == NULL / TTEngineAvail == FALSE), every entry point in this
 *     module becomes a no-op: <text>/<tspan> elements are silently
 *     dropped from the rendered output.  This is the deliberate
 *     graceful-degradation policy: an SVG icon without its labels is
 *     still a useful picture, while a PDF page without text is not.
 *
 *   - Text is RECORDED during the DOM walk and RENDERED in a single
 *     pass after the renderer has finished with all vector
 *     primitives.  The reason is the SVG plugin's render context:
 *     in Picasso96 deep modes it draws shapes into an R8G8B8 chunky
 *     framebuffer and flushes that to the output BitMap exactly once
 *     at the end of the render task (svgsource.c, the
 *     p96WritePixelArray call near the bottom of Parsertask).
 *     ttengine.library draws via the RastPort, which writes
 *     straight to the BitMap and would be overwritten by the final
 *     chunky flush.  By deferring all text until *after* the flush
 *     we avoid the band-restricted flush/pull dance the PDF plugin
 *     needs.  The visible consequence is that text always paints on
 *     top of the vector layer regardless of document order; for the
 *     SVG content domain (icons, diagrams, maps, charts, labels)
 *     this is the desired outcome.
 *
 * Public objects
 * --------------
 *   - struct SvgTextRun, struct SvgTextList: container types used by
 *     svgsource.c to hold the deferred text list.  Each run is one
 *     contiguous UTF-8 string at a single baseline with one font and
 *     one fill colour.
 *
 *   - SvgTextListInit / SvgTextIsAvailable: trivial helpers.
 *
 *   - SvgTextAccumulate: parse one <text> element (recursing into its
 *     <tspan> children), build a SvgTextRun and append to the list.
 *     The caller supplies the current 2x3 16.16 matrix used to map
 *     SVG user-space to raster pixels and the cascaded fill colour;
 *     other style state (font-family, font-size, font-weight,
 *     font-style, text-anchor) is read off the <text> attributes
 *     directly because the SVG plugin's Renderstate cascade does not
 *     currently track font properties.
 *
 *   - SvgTextRenderAll: walk the accumulated list and emit a TT_Text
 *     call for each run.  Returns FALSE when ttengine was unavailable
 *     and nothing was drawn.  The pen-obtain callback decouples this
 *     module from the SVG plugin's per-source ColorMap accounting.
 *
 * All allocations come from the caller-supplied exec.library pool.
 * Nothing in this module touches global state beyond reading the
 * TTEngineBase pointer; the input and output streams are entirely
 * the SvgTextList and the supplied RastPort. */

#ifndef SVGTEXT_H
#define SVGTEXT_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <intuition/screens.h>

/* Forward decls to avoid a hard include of xmlparse.h from svgsource.c
 * consumers that might otherwise pick up duplicate definitions. */
struct XmlNode;
struct ColorMap;

/* One accumulated text run.
 *
 * Storage notes: every UBYTE pointer below is owned by the decoder's
 * memory pool (the same pool passed to SvgTextAccumulate).  Pool
 * memory is freed in one shot when the decoder pool is destroyed, so
 * there is no per-run free in this module.
 *
 * `utf8` is already entity-decoded by the XML parser (xmlparse.c
 * substitutes entities in place in the input buffer) and is NUL-
 * terminated for callers that prefer C-string semantics; `utf8len`
 * is the byte length excluding the terminator. `glyphs` is the count
 * of Unicode codepoints in `utf8` - this is what TT_Text and
 * TT_TextExtent want as their `count` argument, NOT the byte
 * length. */
struct SvgTextRun
{  struct SvgTextRun *next;
   UBYTE *family;             /* primary family name (NUL-term)         */
   UBYTE *family2;            /* generic fallback family or NULL        */
   UBYTE *utf8;               /* entity-decoded UTF-8 (NUL-term)        */
   LONG   utf8len;            /* byte length of utf8                    */
   LONG   glyphs;             /* codepoint count - count arg of TT_Text */
   LONG   raster_x;           /* baseline left in raster pixels         */
   LONG   raster_y;           /* baseline in raster pixels              */
   LONG   pix_size;           /* TT_FontSize value (raster pixels)      */
   ULONG  rgb;                /* fill colour 0xRRGGBB                   */
   UBYTE  bold;
   UBYTE  italic;
   UBYTE  anchor;             /* SVGT_ANCHOR_*                          */
   UBYTE  pad;
};

struct SvgTextList
{  struct SvgTextRun *head;
   struct SvgTextRun *tail;
   long count;
};

/* SVG `text-anchor` enum values.  Default per spec is start. */
#define SVGT_ANCHOR_START   0
#define SVGT_ANCHOR_MIDDLE  1
#define SVGT_ANCHOR_END     2

/* Set list head/tail/count to zero.  Cheap; no allocation. */
extern void SvgTextListInit(struct SvgTextList *list);

/* TRUE iff ttengine.library opened at plugin init.  Callers can short
 * circuit before doing any text-related parsing work. */
extern BOOL SvgTextIsAvailable(void);

/* Parse one SVG <text> element and append a SvgTextRun to `list`.
 *
 * Parameters:
 *   pool       - exec.library memory pool for all allocations
 *   list       - destination accumulator (head/tail updated in place)
 *   node       - XML node for the <text> element
 *   M          - six 16.16 LONGs forming the current 2x3 affine
 *                (a,b,c,d,e,f as used elsewhere in svgsource.c -
 *                see struct Matrix definition in that file).  Used
 *                to map the SVG-space text origin to raster pixels.
 *   fillrgb    - cascaded fill colour 0xRRGGBB
 *   fillvalid  - if FALSE the element is treated as invisible (still
 *                recurses for diagnostic uses but nothing is queued)
 *
 * SVG presentation attributes / style honoured:
 *   x, y                 (numeric, first value of any list is taken)
 *   font-family          (comma-separated CSS list, generic mapped)
 *   font-size            (CSS unit suffix tolerated; px assumed)
 *   font-weight          (named "bold" or numeric >=700)
 *   font-style           ("italic"/"oblique" -> italic, else regular)
 *   text-anchor          (start | middle | end, default start)
 *   style="..."          (in-line declarations override the above)
 *
 * Text content from the element and its <tspan> descendants is
 * concatenated; tspan-level positioning and per-span style overrides
 * are NOT honoured in v1 (the whole text becomes one TT_Text call at
 * the parent <text>'s x/y).  This is enough to render labels in the
 * vast majority of real-world SVG icons/maps/diagrams, but text
 * editors that emit per-character <tspan>s will collapse to a single
 * run with the parent style.
 *
 * Silently does nothing when SvgTextIsAvailable() is FALSE or the
 * element produces no text content. */
extern void SvgTextAccumulate(APTR pool, struct SvgTextList *list,
   struct XmlNode *node, const LONG *M,
   ULONG fillrgb, BOOL fillvalid);

/* Render every accumulated run by calling TT_Text against rp.
 *
 * Parameters:
 *   rp         - rastport bound to the output BitMap
 *   screen     - public screen handle (used for TT_Screen)
 *   colormap   - screen colormap (used by pen_obtain on palette modes)
 *   list       - run list previously built by SvgTextAccumulate
 *   pen_obtain - callback that returns a pen number for an 0xRRGGBB
 *                colour.  Mirrors svgsource.c's Getpen() so the
 *                allocated pen is tracked by the source for later
 *                release.  Receives the user `ctx` pointer unchanged.
 *   ctx        - opaque context for pen_obtain
 *
 * No-op (returns FALSE) when SvgTextIsAvailable() is FALSE, the list
 * is empty, rp is NULL, or TT_SetFont fails for every run.  Returns
 * TRUE when at least one run was rendered. */
extern BOOL SvgTextRenderAll(struct RastPort *rp, struct Screen *screen,
   struct ColorMap *colormap, struct SvgTextList *list,
   UBYTE (*pen_obtain)(void *ctx, ULONG rgb), void *ctx);

#endif
