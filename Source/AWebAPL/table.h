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

/* table.h - AWeb html table element object */

#ifndef AWEB_TABLE
#define AWEB_TABLE

#include "element.h"

/* table tags */

#define AOTAB_Dummy        AOBJ_DUMMYTAG(AOTP_TABLE)

#define AOTAB_Pixelwidth   (AOTAB_Dummy+1)
#define AOTAB_Percentwidth (AOTAB_Dummy+2)
#define AOTAB_Relwidth     (AOTAB_Dummy+3)
   /* (short) Pixel, percentage or relative width. Also used as detail. */

#define AOTAB_Border       (AOTAB_Dummy+4)
   /* (short) Border thickness */

#define AOTAB_Cellspacing  (AOTAB_Dummy+5)
   /* (short) Room between cells */

#define AOTAB_Cellpadding  (AOTAB_Dummy+6)
   /* (short) Room around cell contents */

#define AOTAB_Caption      (AOTAB_Dummy+7)
   /* (BOOL) Start or end caption */

#define AOTAB_Row          (AOTAB_Dummy+8)
   /* (BOOL) Start or end table row */

#define AOTAB_Cell         (AOTAB_Dummy+9)
#define AOTAB_Hcell        (AOTAB_Dummy+10)
   /* (BOOL) Start or end table cell or heading cell */

#define AOTAB_Endtable     (AOTAB_Dummy+11)
   /* (-) End table definition */

#define AOTAB_Halign       (AOTAB_Dummy+12)
#define AOTAB_Valign       (AOTAB_Dummy+13)
#define AOTAB_Rowspan      (AOTAB_Dummy+14)
#define AOTAB_Colspan      (AOTAB_Dummy+15)
   /* (short) Table detail */

#define AOTAB_Nowrap       (AOTAB_Dummy+16)
   /* (BOOL) Table cell detail */

#define AOTAB_Body         (AOTAB_Dummy+17)
   /* (void *) GET current body. If no current body, one will be created. */

#define AOTAB_Vspacing     (AOTAB_Dummy+18)
   /* (long) When opening or closing a row, column or table, pass in this
    * tag the number of line breaks already present, BEFORE the other tags. */

#define AOTAB_Bodync       (AOTAB_Dummy+19)  /* GET */
   /* (void *) GET current body. If no current body, return NULL. */

#define AOTAB_Bgimage      (AOTAB_Dummy+20)
   /* (void *) Background image */

#define AOTAB_Bgcolor      (AOTAB_Dummy+21)
#define AOTAB_Bordercolor  (AOTAB_Dummy+22)
#define AOTAB_Borderdark   (AOTAB_Dummy+23)
#define AOTAB_Borderlight  (AOTAB_Dummy+24)
   /* (struct Colorinfo *) Background, border colours. When AOBJ_Window is set,
    * forward the pen numbers to the cell bodies.
    */

#define AOTAB_Colgroup     (AOTAB_Dummy+25)  /* SET */
   /* (BOOL) Start or end a column group. Details: Colspan, width tags, align tags. */

#define AOTAB_Column       (AOTAB_Dummy+26)  /* SET */
   /* (BOOL) Add a column declaration. Details: Colspan, width tags, align tags. */

#define AOTAB_Thead        (AOTAB_Dummy+27)  /* SET */
#define AOTAB_Tfoot        (AOTAB_Dummy+28)  /* SET */
#define AOTAB_Tbody        (AOTAB_Dummy+29)  /* SET */
   /* (BOOL) Start or end a header group, footer group or body group.
    * Details: align tags. */

#define AOTAB_Tabframe     (AOTAB_Dummy+30)  /* SET */
   /* (USHORT) Outer frame style, see below. */

#define AOTAB_Rules        (AOTAB_Dummy+31)  /* SET */
   /* (USHORT) Inner rules style, see below. */

#define AOTAB_Pixelheight  (AOTAB_Dummy+32)  /* SET */
#define AOTAB_Percentheight (AOTAB_Dummy+33) /* SET */
   /* (long) Pixel or percentage height. (Table cell detail) */

#define AOTAB_Bgalign      (AOTAB_Dummy+34)  /* SET,GET */
   /* (struct Aobject *) Object to align background to */

#define AOTAB_    (AOTAB_Dummy+)
#define AOTAB_    (AOTAB_Dummy+)

/* Detail attributes are passed together with (but after) AOTAB_Caption,
 * AOTAB_Colgroup, AOTAB_Column,
 * AOTAB_Row, AOTAB_Cell or AOTAB_Hcell and modify it */

/* Frame styles */
#define TABFRM_NONE     0x0000
#define TABFRM_ABOVE    0x0001
#define TABFRM_BELOW    0x0002
#define TABFRM_HSIDES   0x000c
#define TABFRM_LEFT     0x0004
#define TABFRM_RIGHT    0x0008
#define TABFRM_VSIDES   0x0003
#define TABFRM_ALL      0x000f

/* Rule styles */
#define TABRUL_NONE     0x0000
#define TABRUL_GROUPS   0x0004
#define TABRUL_ROWS     0x0001
#define TABRUL_COLS     0x0002
#define TABRUL_ALL      0x0003

/* Apply width/height/align to a specific cell body (not only the open cell). */
void TableApplyCellAttrs(void *table, void *cellBody, ULONG tag, long data);

/* Reapply td/th stylesheet rules to every cell after async CSS load. */
void ReapplyCSSToTable(struct Document *doc, void *table);

#endif

