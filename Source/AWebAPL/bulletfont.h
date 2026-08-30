/**********************************************************************
 *
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2026 amigazen project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the AWeb Public License as included in this
 * distribution.
 *
 **********************************************************************/

/* bulletfont.h - AWEB4 HTML outline fonts via bullet.library (not diskfont).
 * Keep this header free of aweb.h so AWebCfg sources (awebcfg.h) can include it.
 *
 * Two layers:
 *  1) BulletFontOpen — TextFont shell (metric stub and/or packed CharData)
 *  2) Live GlyphEngine path — WidthList metrics + cropped OT_GlyphMap
 *     rasters via BltTemplate; kerning only above small HTML sizes; shear/
 *     embolden / optional rotate (BulletExamples). Used when TTEngine is
 *     not active on the rastport. */

#ifndef AWEB_BULLETFONT_H
#define AWEB_BULLETFONT_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef GRAPHICS_TEXT_H
#include <graphics/text.h>
#endif
#ifndef GRAPHICS_RASTPORT_H
#include <graphics/rastport.h>
#endif

/* Open bullet.library; returns TRUE if the engine is usable. */
BOOL InitBulletFont(void);

/* Release bullet.library and any cached outline TextFonts / live engines. */
void FreeBulletFont(void);

/* TRUE when bullet.library opened and OpenEngine works. */
BOOL BulletFontAvailable(void);

/* Open an outline typeface by Amiga font name (e.g. "CGTimes.font") at
 * pixel height ysize. Builds a TextFont shell (and live GlyphEngine when
 * possible). Pair with AwebCloseBrowserFont() / BulletFontClose(). */
struct TextFont *BulletFontOpen(UBYTE *fontname,short ysize);

/* TRUE if tf was created by BulletFontOpen. */
BOOL BulletFontIsOurs(struct TextFont *tf);

/* Duplicate a bullet TextFont (refcount). Returns tf or NULL. */
struct TextFont *BulletFontDup(struct TextFont *tf);

/* Drop one reference; frees when the count hits zero. Returns TRUE if ours. */
BOOL BulletFontClose(struct TextFont *tf);

/* Close a browser/HTML TextFont: bullet fonts via BulletFontClose, else CloseFont. */
void AwebCloseBrowserFont(struct TextFont *tf);

/*----- AWEB4 live GlyphEngine render (BulletExamples-style) --------------*/

/* Bind rp to live Bullet rendering for tf (outline). Applies FSF_BOLD /
 * FSF_ITALIC via OT_Embolden* / OT_Shear*. No-op if tf is not ours or
 * the live engine cannot be opened. */
void BulletFontSetFont(struct RastPort *rp,struct TextFont *tf,USHORT style);

/* Document buffer encoding for LiveRun: FALSE = ISO-8859/Amiga 8-bit
 * (one byte per glyph), TRUE = UTF-8 multi-byte fold. */
void BulletFontSetSourceUtf8(BOOL utf8);

/* TRUE if Bullet can live-draw/measure for the font on rp (armed or ours). */
BOOL BulletFontIsLiveActive(struct RastPort *rp);

/* Clear live binding (e.g. before switching to TTEngine on same rp). */
void BulletFontClearLive(struct RastPort *rp);

/* Draw / measure like graphics Text / TextLength, with text kerning.
 * Bytes are Latin-1 / Unicode code points in 0..255 (Amiga HTML path). */
void BulletFontText(struct RastPort *rp,UBYTE *string,ULONG count);
ULONG BulletFontTextLength(struct RastPort *rp,UBYTE *string,ULONG count);
void BulletFontTextExtent(struct RastPort *rp,UBYTE *string,WORD count,
   struct TextExtent *te);
/* How many leading bytes fit in cwidth pixels (advance-based, like TextFit). */
ULONG BulletFontTextFit(struct RastPort *rp,UBYTE *string,UWORD count,UWORD cwidth);

/* Form fields / gadgets: diskfont (or topaz) at prefer's YSize. Bullet HTML
 * stubs have empty CharData so graphics Text() would draw blank. Cached;
 * do not CloseFont the result (owned until FreeBulletFont). If prefer is
 * already a non-Bullet font it is returned unchanged. */
struct TextFont *AwebFormDiskFont(struct TextFont *prefer);

/* Optional transforms (FIXED 16.16). Both sin and cos must be set; sin
 * first then cos per NDK. Identity = 0 / 0x10000. Used by PDF/SVG later;
 * HTML leaves rotation at identity. */
BOOL BulletFontSetRotate(LONG sinFixed,LONG cosFixed);
BOOL BulletFontSetShear(LONG sinFixed,LONG cosFixed);
BOOL BulletFontSetSetFactor(LONG factorFixed);
BOOL BulletFontSetEmbolden(LONG xFixed,LONG yFixed);

#endif
