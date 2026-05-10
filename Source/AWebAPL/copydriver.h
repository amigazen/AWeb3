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

/* copydriver.h - AWeb copy driver interface */

#ifndef AWEB_COPYDRIVER_H
#define AWEB_COPYDRIVER_H

#include "object.h"

/*--- copydriver tags ---*/

#define AOCDV_Dummy        AOBJ_DUMMYTAG(AOTP_COPYDRIVER)

#define AOCDV_Copy         (AOCDV_Dummy+1)      /* NEW,SET */
   /* (void *) The copy object owning this driver. */

#define AOCDV_Title        (AOCDV_Dummy+2)      /* GET */
   /* (UBYTE *) GET current object title */

#define AOCDV_Fragmentname (AOCDV_Dummy+3)      /* GET */
   /* (UBYTE *) Fragment name, supply when GETting AOCDV_Fragmentpos */

#define AOCDV_Fragmentpos  (AOCDV_Dummy+4)      /* GET */
   /* (long) GET Y position for this fragment, or -1 if not found.
    * Supply a AOCDV_Fragmentname first */

#define AOCDV_Sourcedriver (AOCDV_Dummy+5)      /* NEW,SET */
   /* (void *) The source driver controlling this copy. */

#define AOCDV_Ready        (AOCDV_Dummy+6)      /* GET */
   /* (BOOL) Driver is ready to receive AOM_MEASURE, AOM_LAYOUT and
    * AOM_RENDER messages. */

#define AOCDV_Shapes       (AOCDV_Dummy+7)      /* NEW,SET,GET */
   /* (BOOL) Driver handles AOM_HITTEST and activation messages. */

#define AOCDV_Width        (AOCDV_Dummy+8)      /* NEW,SET */
#define AOCDV_Height       (AOCDV_Dummy+9)      /* NEW,SET */
   /* (long) Suggested dimensions for object, scale if possible. */

/* The next four attributes can be GET if object used as a background
 * image. Return NULL/zero if no bitmap or not yet complete. */
#define AOCDV_Imagebitmap  (AOCDV_Dummy+10)     /* GET */
   /* (struct BitMap *) Image bitmap */

#define AOCDV_Imagemask    (AOCDV_Dummy+11)     /* GET */
   /* (UBYTE *) Image transparent mask or NULL. */

#define AOCDV_Imagewidth   (AOCDV_Dummy+12)     /* GET */
#define AOCDV_Imageheight  (AOCDV_Dummy+13)     /* GET */
   /* (long) Image bitmap dimensions. */

#define AOCDV_Hlayout      (AOCDV_Dummy+14)     /* GET */
#define AOCDV_Vlayout      (AOCDV_Dummy+15)     /* GET */
   /* (BOOL) If the object is sensitive to dimensional changes in this
    * direction (and wants to relayout itself) */

#define AOCDV_Extendhit    (AOCDV_Dummy+16)     /* GET */
   /* (BOOL) If the mouse hit boundary is extended to fill the whole frame.
    * Default FALSE, meaning only the actual width and height of the object. */

#define AOCDV_Undisplayed  (AOCDV_Dummy+17)     /* GET */
   /* (BOOL) If the object doesn't want to display itself. Example is
    * sound that can either be used as background object, or as a main
    * object but thet shouldn't take over the display.
    * Default FALSE, meaning it is displayable. */

#define AOCDV_Soundloop    (AOCDV_Dummy+18)     /* NEW,SET */
   /* (long) Number of times to play sound. Negative means infinite. */

#define AOCDV_Playsound    (AOCDV_Dummy+19)     /* SET */
   /* (BOOL) Start or stop playing of background sound */

#define AOCDV_Displayed    (AOCDV_Dummy+20)     /* SET */
   /* (BOOL) If object is allowed to render itself apart from
    * received AOM_RENDER messages. Regardless of AOBJ_Frame. */

#define AOCDV_Bgchanged    (AOCDV_Dummy+21)     /* SET */
   /* (BOOL) Background pattern has changed. */

#define AOCDV_Reloadverify (AOCDV_Dummy+22)     /* SET */
   /* (BOOL) This is subject to a reload, forced verify all children. */

#define AOCDV_Mapdocument  (AOCDV_Dummy+23)     /* NEW,SET */
   /* (BOOL) This is a HTML document for <MAP> definitions only. */

#define AOCDV_Objectparams (AOCDV_Dummy+24)     /* NEW,SET */
   /* (struct Objectparam *) Object parameter, first in linked list. */

#define AOCDV_Marginwidth  (AOCDV_Dummy+25)     /* NEW,SET */
#define AOCDV_Marginheight (AOCDV_Dummy+26)     /* NEW,SET */
   /* (long) Suggested margin dimensions */

#define AOCDV_Viewportwidth (AOCDV_Dummy+28)   /* NEW,SET */
   /* (long) Viewport width from meta viewport tag. If > 0, overrides default
    * viewport width calculation. Value of 0 means use default (window inner width). */

#define AOCDV_Alpha        (AOCDV_Dummy+27)     /* GET */
   /* (ULONG) Does this copy driver support alpha channel? Returns non-zero if alpha channel is available. */

#define AOCDV_Eagerdecode  (AOCDV_Dummy+29)     /* SET */
   /* (BOOL) IMGCOPY only: tell imgsource to decode at EOF when LAZY_IMAGE_DECODE is on.
    * Used for CSS/HTML background images (not on-screen <IMG>), which never receive
    * the normal "displayed copy" lazy-decode path. */


/*--- copydriver data ---*/

struct Copydriver
{  struct Aobject object;     /* Object header */
   void *extension;           /* Private extension data */
   long aox,aoy;              /* Left,top position */
   long aow,aoh;              /* Width, height */
   struct Aobject *cframe;    /* Current cframe */
};

struct Objectparam            /* General purpose object parameter */
{  NODE(Objectparam);
   UBYTE *name;               /* Parameter name */
   UBYTE *value;              /* Parameter value */
   UBYTE *valuetype;          /* Type of value: "DATA", "REF", "URL" or "OBJECT" */
   UBYTE *type;               /* MIME type of parameter, only for REF or URL. */
};

#endif

