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

/* aweb.h */

#ifndef AWEB_H
#define AWEB_H

#include <exec/types.h>
#include <exec/ports.h>

#ifndef AWEBDEF_H
#include "awebdef.h"
#endif

#define FSF_STRIKE      0x8000   /* addition to OS styles */

struct Colorinfo           /* a color used in the document */
{  NODE(Colorinfo);        /* list node */
   ULONG rgb;              /* palette value */
   short pen;              /* pen assigned */
   USHORT flags;           /* see below */
};

#define COLIF_GLOBAL    0x0001   /* global document color, like text or link */

enum MIME_DRIVERS
{  MIMEDRV_NONE=0,         /* No known driver */
   MIMEDRV_DOCUMENT,       /* Internal text driver */
   MIMEDRV_IMAGE,          /* Internal image driver */
   MIMEDRV_SOUND,          /* Internal sound driver */
   MIMEDRV_EXTPROG,        /* External program driver */
   MIMEDRV_EXTPROGPIPE,    /* External program driver with PIPE: */
   MIMEDRV_PLUGIN,         /* External plugin driver */
   MIMEDRV_SAVELOCAL,      /* Save to local disk */
};

struct Coords              /* accumulated coordinates for displaying */
{  long dx,dy;             /* add to element coords to get rastport coords */
   long minx,miny,maxx,maxy;  /* resulting max visible limits as window coords */
   struct Aobject *win;    /* the top level window object */
   struct RastPort *rp;    /* the window's rastport */
   struct DrawInfo *dri;   /* the current drawinfo */
   short bgcolor,textcolor,linkcolor,vlinkcolor,alinkcolor;
                           /* default pen numbers for current frame */
   void *bgimage;          /* background image for current frame */
   struct Aobject *bgalign; /* object to align background image to */
   short nestcount;
};

/* General purpose backfill hook (defined in aweb.c) */
extern void __saveds __asm Awebbackfillhook(register __a0 struct Hook *hook,
   register __a2 struct RastPort *lrp,
   register __a1 struct Layermessage *msg);

struct Abackfilldata       /* Hook data for Awebbackfillhook() */
{  long fgpen;
   long bgpen;
   USHORT flags;           /* What to do, see below */
};
#define ABKFIL_BACKFILL    0x0001   /* Fill area with bgpen */
#define ABKFIL_BORDER      0x0002   /* Draw border in fgpen */


extern struct LocaleInfo localeinfo;
#define AWEBSTR(n)      GetString(&localeinfo,(n))
extern UBYTE *Getmainstr(ULONG n);

/* nr of pixels to indent for each list level */
#define LEVELINDENT  40

/* nr of pixels to indent left and right for each blockquote */
#define BQINDENT     40

/* request return function type */
typedef void requestfunc(short code,void *data);

/* Configurable button types */
enum BUTTON_TYPES
{  BUTF_BACK,BUTF_FORWARD,BUTF_HOME,BUTF_ADDHOTLIST,BUTF_HOTLIST,
   BUTF_CANCEL,BUTF_NETSTATUS,BUTF_SEARCH,BUTF_RELOAD,BUTF_LOADIMAGES,
   BUTF_UNSECURE,BUTF_SECURE,
   
   BUTF_NSCANCEL,BUTF_NSCANCELALL,
   NR_BUTTONS,
};

/* Addtagstr() function constants */
#define ATSF_NONE    0
#define ATSF_STRING  1
#define ATSF_NUMBER  2

/* suggested pool sizes for private pools */
#define PUDDLESIZE         16*1024
#define TRESHSIZE          4*1024

#define PALLOCTYPE(t,n,f,p)   (t*)Pallocmem((n)*sizeof(t),(f)|MEMF_PUBLIC,p)
#define PALLOCSTRUCT(s,n,f,p) PALLOCTYPE(struct s,n,f,p)


/* Pointer to function in aweblib module. Offsets are from zero with 1 increment */
#define AWEBLIBENTRY(base,offset)   (void (*)())((ULONG)(base)-30-6*(offset))


extern UBYTE *aboutversion;   /* full version string contents */

/* Return AmigaOS version label based on Exec version (SysBase->lib_Version). */
extern const UBYTE *Awebosversion(void);
extern UBYTE *awebversion;    /* version.revision string */

/* Generic JS event handler invocations */
extern UBYTE *awebonclick,*awebonchange,*awebonblur,*awebonfocus,*awebonmouseover,
   *awebonmouseout,*awebonreset,*awebonsubmit,*awebonselect,*awebonload,
   *awebonunload,*awebonerror,*awebonabort;

extern struct Prefs prefs;
extern struct Himage *def_image,*def_imagemap,*def_errimage;
extern struct SignalSemaphore prefssema;
extern struct Hook requestbackfillhook;
extern UBYTE programname[];
extern struct Locale *locale;

extern struct Library *AWebJSBase;

/* boingball.image (V47+) class library base, optionally opened by aweb.c /
 * awebview.c at startup. When non-NULL, the toolbar activity indicator
 * (boopsi LED gadget) renders a rotating boing ball instead of the classic
 * progress LEDs. NULL when running on older systems where penmap.image /
 * boingball.image are absent or below V47. */
extern struct ClassLibrary *BoingBallBase;

/* boopsi status gadget tags: */
#define STATGA_Dummy          (TAG_USER+0x0AEB0000)
#define STATGA_HPText         (STATGA_Dummy+0x0001)   /* high-priority text */
#define STATGA_HPLen1         (STATGA_Dummy+0x0002)   /* scheme, host, path length */
#define STATGA_HPLen2         (STATGA_Dummy+0x0003)   /* file name length */
#define STATGA_SpecialPens    (STATGA_Dummy+0x0004)   /* ClassAct special pens */

#define LEDGGA_Dummy          (TAG_USER+0x0AEC0000)
#define LEDGGA_Active         (LEDGGA_Dummy+0x0001)
#define LEDGGA_AnimBitMap     (LEDGGA_Dummy+0x0002)
#define LEDGGA_AnimX          (LEDGGA_Dummy+0x0003)
#define LEDGGA_AnimY          (LEDGGA_Dummy+0x0004)
#define LEDGGA_AnimWidth      (LEDGGA_Dummy+0x0005)
#define LEDGGA_AnimHeight     (LEDGGA_Dummy+0x0006)
#define LEDGGA_AnimDeltaX     (LEDGGA_Dummy+0x0007)
#define LEDGGA_AnimDeltaY     (LEDGGA_Dummy+0x0008)
#define LEDGGA_AnimFrames     (LEDGGA_Dummy+0x0009)
#define LEDGGA_RestX          (LEDGGA_Dummy+0x000A)
#define LEDGGA_RestY          (LEDGGA_Dummy+0x000B)
#define LEDGGA_SpecialPens    (LEDGGA_Dummy+0x000C)   /* ClassAct special pens */
#define LEDGGA_BoingBall      (LEDGGA_Dummy+0x000D)   


#ifndef AWEBPROTOS_H
#include "awebprotos.h"
#endif

#include "aweblog.h"

extern BOOL httpdebug;
extern BOOL specdebug;
extern BOOL haiku;
extern BOOL nostartup;

#include "haiku.h"

#ifdef DEVELOPER
#define PROFILE
#endif
#include "profile.h"

#endif
