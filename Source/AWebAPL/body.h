/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
 * Changes Copyright (C) 2025 amigazen project
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

/* body.h - AWeb html document body object */

#ifndef AWEB_BODY_H
#define AWEB_BODY_H

#include "object.h"

/*--- body tags ---*/

#define AOBDY_Dummy        AOBJ_DUMMYTAG(AOTP_BODY)

#define AOBDY_Sethardstyle (AOBDY_Dummy+1)
#define AOBDY_Unsethardstyle (AOBDY_Dummy+2)
   /* (USHORT) Set or unset hard style flags */

#define AOBDY_Align        (AOBDY_Dummy+3)
#define AOBDY_Divalign     (AOBDY_Dummy+4)
   /* (short) Set paragraph or division align. (-1) unsets. */

#define AOBDY_Style        (AOBDY_Dummy+5)   /* SET,GET */
   /* (short) Stack logical style. */

#define AOBDY_Fixedfont    (AOBDY_Dummy+6)
   /* (BOOL) Fixed font, default normal font */

#define AOBDY_Fontsize     (AOBDY_Dummy+7)
   /* (short) Stack absolute font size. */

#define AOBDY_Fontsizerel  (AOBDY_Dummy+8)
   /* (short) Stack relative font size. */

#define AOBDY_Fontcolor    (AOBDY_Dummy+9)
   /* (struct Colorinfo *) Stack font color. */

#define AOBDY_Fontend      (AOBDY_Dummy+10)
   /* (BOOL) Pops font stack (style, size, color). */

#define AOBDY_Basefont     (AOBDY_Dummy+11)
   /* (short) Sets absolute base font size. */

#define AOBDY_Subscript    (AOBDY_Dummy+12)
#define AOBDY_Superscript  (AOBDY_Dummy+13)
   /* (BOOL) Sets sub/superscript mode */

#define AOBDY_Link         (AOBDY_Dummy+14)
   /* (void *) Current hyperlink or NULL */

#define AOBDY_List         (AOBDY_Dummy+15)
   /* (struct Listinfo *) Information about the current list */

#define AOBDY_Dterm        (AOBDY_Dummy+16)
   /* (BOOL) TRUE if a <DT> tag is active in the current <DL> list */

#define AOBDY_Blockquote   (AOBDY_Dummy+17)
   /* (BOOL) Increment or decrement blockquote indent level */

#define AOBDY_Leftmargin   (AOBDY_Dummy+18)
#define AOBDY_Topmargin    (AOBDY_Dummy+19)
   /* (short) Horizontal and vertical body margins */

#define AOBDY_Bgcolor      (AOBDY_Dummy+20)
   /* (short) Pen numbers for background, or (-1) to reset */

#define AOBDY_Bgimage      (AOBDY_Dummy+21)
   /* (void *) Background image to use, or NULL to reset */

#define AOBDY_Nobr         (AOBDY_Dummy+22)
   /* (BOOL) Turns no-break on or off */

#define AOBDY_End          (AOBDY_Dummy+23)  /* SET */
   /* (BOOL) Body definition is complete */

#define AOBDY_Basefontrel  (AOBDY_Dummy+24)  /* SET */
   /* (short) Sets relative basefont size. */

#define AOBDY_Forcebgcolor (AOBDY_Dummy+25)  /* SET */
   /* (short) Use this pen number for background even if bgcolors are off. */

#define AOBDY_Basecolor    (AOBDY_Dummy+26)  /* SET */
   /* (struct Colorinfo *) Sets basefont color. */

#define AOBDY_Fontface     (AOBDY_Dummy+27)  /* SET,GET */
   /* (UBYTE *) Comma-separated list of preferred font face names (GET: active stack top) */

#define AOBDY_Baseface     (AOBDY_Dummy+28)  /* SET */
   /* (UBYTE *) Comma-separated list of preferred basefont face names */

#define AOBDY_Bgupdate     (AOBDY_Dummy+31)   /* GET */
   /* ULONG last bgupdate key value */

#define AOBDY_Bgalign      (AOBDY_Dummy+29)  /* SET,GET */
   /* (struct Aobject *) Object to align background to */

#define AOBDY_Tcell        (AOBDY_Dummy+30)  /* SET,GET */
   /* (void *) Table cell that owns body */

#define AOBDY_TagName      (AOBDY_Dummy+32)  /* SET,GET */
   /* (UBYTE *) HTML tag name (e.g. "DIV", "P") for CSS matching */

#define AOBDY_Class        (AOBDY_Dummy+33)  /* SET,GET */
   /* (UBYTE *) CSS class name(s) for CSS matching */

#define AOBDY_Id           (AOBDY_Dummy+34)  /* SET,GET */
   /* (UBYTE *) Element ID for CSS matching */

#define AOBDY_Position     (AOBDY_Dummy+35)  /* SET,GET */
   /* (UBYTE *) CSS position: "static", "relative", "absolute", "fixed" */

#define AOBDY_ZIndex       (AOBDY_Dummy+36)  /* SET,GET */
   /* (long) CSS z-index value (higher = on top) */

#define AOBDY_Display      (AOBDY_Dummy+37)  /* SET,GET */
   /* (UBYTE *) CSS display: "none", "block", "inline", "inline-block", etc. */

#define AOBDY_Overflow     (AOBDY_Dummy+38)  /* SET,GET */
   /* (UBYTE *) CSS overflow: "visible", "hidden", "auto", "scroll" */

#define AOBDY_Clear        (AOBDY_Dummy+39)  /* SET,GET */
   /* (UBYTE *) CSS clear: "none", "left", "right", "both" */

#define AOBDY_VerticalAlign (AOBDY_Dummy+40) /* SET,GET */
   /* (short) CSS vertical-align: VALIGN_TOP, VALIGN_MIDDLE, VALIGN_BOTTOM, VALIGN_BASELINE */

#define AOBDY_ListStyle    (AOBDY_Dummy+41)  /* SET,GET */
   /* (UBYTE *) CSS list-style: "disc", "circle", "square", "decimal", etc. */

#define AOBDY_MinWidth     (AOBDY_Dummy+42)  /* SET,GET */
   /* (long) CSS min-width value */

#define AOBDY_MaxWidth     (AOBDY_Dummy+43)  /* SET,GET */
   /* (long) CSS max-width value */

#define AOBDY_MinHeight    (AOBDY_Dummy+44)  /* SET,GET */
   /* (long) CSS min-height value */

#define AOBDY_MaxHeight    (AOBDY_Dummy+45)  /* SET,GET */
   /* (long) CSS max-height value */

#define AOBDY_PaddingTop   (AOBDY_Dummy+46)  /* SET,GET */
   /* (long) CSS padding-top value */

#define AOBDY_PaddingRight (AOBDY_Dummy+47)  /* SET,GET */
   /* (long) CSS padding-right value */

#define AOBDY_PaddingBottom (AOBDY_Dummy+48)  /* SET,GET */
   /* (long) CSS padding-bottom value */

#define AOBDY_PaddingLeft  (AOBDY_Dummy+49)  /* SET,GET */
   /* (long) CSS padding-left value */

#define AOBDY_BorderWidth  (AOBDY_Dummy+50)  /* SET,GET */
   /* (long) CSS border width (all sides) */

#define AOBDY_BorderColor  (AOBDY_Dummy+51)  /* SET,GET */
   /* (struct Colorinfo *) CSS border color */

#define AOBDY_BorderStyle  (AOBDY_Dummy+52)  /* SET,GET */
   /* (UBYTE *) CSS border style: "solid", "dashed", "dotted", etc. */

#define AOBDY_Right        (AOBDY_Dummy+53)  /* SET,GET */
   /* (long) CSS right position value */

#define AOBDY_Bottom       (AOBDY_Dummy+54)  /* SET,GET */
   /* (long) CSS bottom position value */

#define AOBDY_Cursor       (AOBDY_Dummy+55)  /* SET,GET */
   /* (UBYTE *) CSS cursor: "pointer", "help", "default", etc. */

#define AOBDY_TextTransform (AOBDY_Dummy+56)  /* SET,GET */
   /* (UBYTE *) CSS text-transform: "uppercase", "lowercase", "capitalize", "none" */

#define AOBDY_WhiteSpace   (AOBDY_Dummy+57)  /* SET,GET */
   /* (UBYTE *) CSS white-space: "nowrap", "normal", "pre", etc. */

#define AOBDY_LinkTextColor (AOBDY_Dummy+58)  /* SET,GET */
   /* (struct Colorinfo *) Text color for current link (from CSS class-based selectors or inline styles) */

#define AOBDY_BackgroundRepeat (AOBDY_Dummy+59)  /* SET,GET */
   /* (UBYTE *) CSS background-repeat: "repeat", "no-repeat", "repeat-x", "repeat-y" */

#define AOBDY_BackgroundPosition (AOBDY_Dummy+60)  /* SET,GET */
   /* (UBYTE *) CSS background-position: "center", "top", "bottom", "left", "right", percentages, lengths */

#define AOBDY_BackgroundAttachment (AOBDY_Dummy+61)  /* SET,GET */
   /* (UBYTE *) CSS background-attachment: "scroll", "fixed" */

#define AOBDY_Transform (AOBDY_Dummy+62)  /* SET,GET */
   /* (UBYTE *) CSS transform: "translate(x, y)", "translateX(x)", "translateY(y)", etc. */

#define AOBDY_TopPercent (AOBDY_Dummy+63)  /* SET,GET */
   /* (long) CSS top position as percentage (0-10000 for 0-100%) */

#define AOBDY_LeftPercent (AOBDY_Dummy+64)  /* SET,GET */
   /* (long) CSS left position as percentage (0-10000 for 0-100%) */

#define AOBDY_MarginRight (AOBDY_Dummy+65)  /* SET,GET */
   /* (long) CSS margin-right value (can be negative) */

#define AOBDY_MarginBottom (AOBDY_Dummy+66)  /* SET,GET */
   /* (long) CSS margin-bottom value (can be negative) */

#define AOBDY_ListStyleImage (AOBDY_Dummy+67)  /* SET,GET */
   /* (UBYTE *) CSS list-style-image: url(...) value */

#define AOBDY_MarqueeDirection (AOBDY_Dummy+68)  /* SET,GET */
   /* (UBYTE *) Marquee direction: "left", "right", "up", "down" */

#define AOBDY_MarqueeBehavior (AOBDY_Dummy+69)  /* SET,GET */
   /* (UBYTE *) Marquee behavior: "scroll", "slide", "alternate" */

#define AOBDY_MarqueeScrollAmount (AOBDY_Dummy+70)  /* SET,GET */
   /* (long) Marquee scroll amount in pixels (default 6) */

#define AOBDY_MarqueeScrollDelay (AOBDY_Dummy+71)  /* SET,GET */
   /* (long) Marquee scroll delay in milliseconds (default 85) */

#define AOBDY_MarqueeLoop (AOBDY_Dummy+72)  /* SET,GET */
   /* (long) Marquee loop count (-1 for infinite) */

#define AOBDY_MarqueeScrollX (AOBDY_Dummy+73)  /* SET,GET */
   /* (long) Current horizontal scroll position */

#define AOBDY_MarqueeScrollY (AOBDY_Dummy+74)  /* SET,GET */
   /* (long) Current vertical scroll position */

/*--- body support structures ---*/

/* Forward declaration of Body structure (defined in body.c) */
struct Body;

/* Accessor functions for Body structure fields (for CSS support) */
extern void SetBodyMarginLeftAuto(struct Body *bd, BOOL isAuto);
extern void SetBodyMarginRightAuto(struct Body *bd, BOOL isAuto);
extern void SetBodyLineHeight(struct Body *bd, float lineheight);

/* global references */
extern ULONG bgupdate;

/* Listinfo contains details about the current list.
 * Set AOBDY_List to add a new list level. Set to NULL to
 * remove the last level. Get to obtain info about the list.
 * When set, all information is copied. When get, the real
 * thing is returned and bulletnr may be modified. */
struct Listinfo
{  NODE(Listinfo);
   USHORT type;            /* Type of list */
   USHORT bullettype;      /* Type of bullet */
   UBYTE *bulletsrc;       /* Url of image, dingbat bullet */
   long bulletnr;          /* Last used OL bullet number */
   short level;            /* Current nesting level */
   short indent;           /* Current indent level (same as level except when <DT>) */
   BOOL horizontal;        /* TRUE if list items should be horizontal (inline) */
};

/* List types */
#define BDLT_UL         1
#define BDLT_OL         2
#define BDLT_DL         3

/* Bullet types */
#define BDBT_DEFAULT    0  /* When SET, will be changed to real type */
#define BDBT_DISC       1  /* Unordered lists. The same values as bullet's types. */
#define BDBT_CIRCLE     2
#define BDBT_SQUARE     3
#define BDBT_DIAMOND    4
#define BDBT_SOLIDDIA   5
#define BDBT_RECTANGLE  6
#define BDBT_IMAGE      7
#define BDBT_PLAIN      8
#define BDBT_NUMBER     9  /* Ordered lists */
#define BDBT_ALPHA      10
#define BDBT_ALPHALOW   11
#define BDBT_ROMAN      12
#define BDBT_ROMANLOW   13

#endif
