/**********************************************************************
 * 
 * This file is part of the AWeb-II distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
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

/* awebpng.h - AWeb png plugin general definitions */

#include <libraries/awebplugin.h>

/* Define DEBUG_PLUGINS to enable PngLog() (ElapsedTime delta + NAME per line). */
/* #define DEBUG_PLUGINS 1 */

#ifdef DEBUG_PLUGINS
/* lowlevel.library ElapsedTime delta + PLUGIN_LIBNAME per line. */
void PngLog(const char *facility,const char *fmt,...);
#endif

/* Pointer to our own library base */
extern struct AwebPngBase *PluginBase;

/* Library bases */
extern struct Library *AwebPluginBase;
extern struct GfxBase *GfxBase;

/* Declarations of the OO dispatcher functions */
extern __saveds __asm ULONG Dispatchsource(
   register __a0 struct Aobject *,
   register __a1 struct Amessage *);

extern __saveds __asm ULONG Dispatchcopy(
   register __a0 struct Aobject *,
   register __a1 struct Amessage *);

/* Release the shared libpng allocation pool.  Called from
 * Expungepluginlib() so the pool is freed when the plugin is unloaded.
 * Implemented in pngsource.c. */
extern void Pngsource_freepool(void);

/* Definition of attribute IDs that are used internally. */

#define AOPNG_Dummy     AOBJ_DUMMYTAG(AOTP_PLUGIN)

#define AOPNG_Data      (AOPNG_Dummy+1)
   /* (BOOL) Let decoder task know that there is new data, or EOF was
    * reached. */

#define AOPNG_Readyfrom (AOPNG_Dummy+2)
#define AOPNG_Readyto   (AOPNG_Dummy+3)
   /* (long) The rows in the bitmap that has gotten new valid data. */

#define AOPNG_Error     (AOPNG_Dummy+4)
   /* (BOOL) The decoding process discovered an error */

#define AOPNG_Bitmap    (AOPNG_Dummy+5)
   /* (struct BitMap *) The bitmap to use, or NULL to remove
    * the bitmap. */

#define AOPNG_Width     (AOPNG_Dummy+6)
#define AOPNG_Height    (AOPNG_Dummy+7)
   /* (long) Dimensions of the bitmap. */

#define AOPNG_Imgready  (AOPNG_Dummy+8)
   /* (BOOL) The image is ready */

#define AOPNG_Mask      (AOPNG_Dummy+9)
   /* (UBYTE *) Transparent mask for the image */

#define AOPNG_Memory    (AOPNG_Dummy+10)
   /* (long) Add this amount of memory to current usage */


