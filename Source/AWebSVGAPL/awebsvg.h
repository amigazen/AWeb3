/**********************************************************************
 * 
 * This file is part of the AWeb distribution
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

/* awebsvg.h - AWeb SVG plugin general definitions */

#include <libraries/awebplugin.h>

/* Base pointers of libraries needed */
extern struct Library *SysBase;         /* Defined in startup.c */
extern struct Library *DOSBase;
extern struct Library *AwebPluginBase;
extern struct Library *GfxBase;
extern struct Library *IntuitionBase;
extern struct Library *UtilityBase;
extern struct Library *P96Base;

/* Pointer to our own library base */
extern struct AwebSvgBase *PluginBase;

/* Declarations of the OO dispatcher functions */
extern __asm ULONG Dispatchsource(
   register __a0 struct Aobject *,
   register __a1 struct Amessage *);

extern __asm ULONG Dispatchcopy(
   register __a0 struct Aobject *,
   register __a1 struct Amessage *);

/* Definition of attribute IDs that are used internally. */

#define AOSVG_Dummy     AOBJ_DUMMYTAG(AOTP_PLUGIN)

#define AOSVG_Data      (AOSVG_Dummy+1)
   /* (BOOL) Let parser task know that there is new data, or EOF was
    * reached. */

#define AOSVG_Readyfrom (AOSVG_Dummy+2)
#define AOSVG_Readyto   (AOSVG_Dummy+3)
   /* (long) The rows in the bitmap that has gotten new valid data. */

#define AOSVG_Error     (AOSVG_Dummy+4)
   /* (BOOL) The parsing process discovered an error */

#define AOSVG_Bitmap    (AOSVG_Dummy+5)
   /* (struct BitMap *) The bitmap to use, or NULL to remove
    * the bitmap. */

#define AOSVG_Width     (AOSVG_Dummy+6)
#define AOSVG_Height    (AOSVG_Dummy+7)
   /* (long) Dimensions of the bitmap. */

#define AOSVG_Imgready  (AOSVG_Dummy+8)
   /* (BOOL) The image is ready */

#define AOSVG_Mask      (AOSVG_Dummy+9)
   /* (UBYTE *) Transparent mask for the image */

#define AOSVG_Parseready (AOSVG_Dummy+10)
   /* (BOOL) Parsing is complete */

#define AOSVG_Jsready   (AOSVG_Dummy+11)
   /* (BOOL) Parsing is complete */

#define AOSVG_Memory    (AOSVG_Dummy+12)
   /* (long) Add this amount of memory to current usage */

