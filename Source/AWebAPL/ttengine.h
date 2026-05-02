/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2025 amigazen project
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

/* ttengine.h - Optional ttengine.library support for font rendering */

#ifndef AWEB_TTENGINE
#define AWEB_TTENGINE

#include "aweb.h"

/* IANA MIBenum 106; same value as ttengine TT_Encoding_UTF8 (see SDK libraries/ttengine.h). */
#define AWEB_TT_ENCODING_UTF8 ((ULONG)106)
#include <graphics/text.h>
#include <graphics/rastport.h>

/* Initialize ttengine.library support (returns TRUE if available) */
BOOL InitTTEngine(void);

/* Cleanup ttengine.library support */
void FreeTTEngine(void);

/* Check if ttengine is available */
BOOL TTEngineAvailable(void);

/* Convert TextFont to ttengine font handle */
APTR TTEngineOpenFont(struct TextFont *font);

/* Close ttengine font handle */
void TTEngineCloseFont(APTR ttfont);

/* Set font on rastport (uses ttengine if available, else standard SetFont) */
/* If fontface is provided (CSS font-family string), it will be used directly with ttengine */
/* Otherwise, font name will be mapped from TextFont name */
/* style is the text element's style flags (FSF_BOLD, FSF_ITALIC) for bold/italic detection */
void TTEngineSetFont(struct RastPort *rp, struct TextFont *font, UBYTE *fontface, USHORT style);

/* IANA-style encoding for ttengine on (rp), e.g. TT_Encoding_UTF8 or TT_Encoding_Default.
 * Only applies when a ttengine font is already active on the RastPort. */
void TTEngineSetRastPortTextEncoding(struct RastPort *rp,ULONG encoding);

/* Text rendering wrapper (uses ttengine if available) */
void TTEngineText(struct RastPort *rp, UBYTE *string, ULONG count);

/* Extra horizontal pixels beyond ttengine advance (bold faux ink + italic overhang). */
ULONG TTEngineBoldInkPad(struct RastPort *rp);

/* Vertical line box for HTML layout: max(tf_YSize, TT_FontMaxTop+MaxBottom) when ttengine active. */
ULONG TTEngineLineBoxHeight(struct RastPort *rp, struct TextFont *tf);

/* Text length wrapper (uses ttengine if available) */
ULONG TTEngineTextLength(struct RastPort *rp, UBYTE *string, ULONG count);

/* Text extent wrapper (uses ttengine if available) */
void TTEngineTextExtent(struct RastPort *rp, UBYTE *string, WORD count, struct TextExtent *te);

/* Text fit wrapper (uses ttengine if available) */
ULONG TTEngineTextFit(struct RastPort *rp, UBYTE *string, UWORD count, struct TextExtent *te,
   struct TextExtent *tec, WORD dir, UWORD cwidth, UWORD cheight);

/* Check if ttengine font is currently active on a rastport */
BOOL IsTTEngineFontActive(struct RastPort *rp);

/* Drop ttengine overlay on (rp) so graphics Text()/TextFit() use the TextFont from SetFont.
 * Call before SetFont when switching an UTF-8 run from TTF metrics back to 7-bit ASCII. */
void TTEngineClearRastPort(struct RastPort *rp);

/* Enable built-in JKFF mixed Shift_JIS rendering (no system patches). */
void SetJkffEnable(BOOL on);

#endif
