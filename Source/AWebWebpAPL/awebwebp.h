/**********************************************************************
 *
 * This file is part of the AWeb-II distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
 * Changes Copyright (C) 2026 amigazen project
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

/* awebwebp.h - AWeb webp plugin general definitions */

#include <libraries/awebplugin.h>

/* Pull in the C prototypes for the awebplugin.library calls.  The
 * <proto/awebplugin.h> umbrella header only includes the SAS/C
 * pragmas (calling-convention directives); without the prototypes
 * here the compiler treats Gettaskmsg, Allocobject, Clipcoords, ...
 * as returning int and emits Warning 225 (pointer type mismatch)
 * at every call site. */
#include <clib/awebplugin_protos.h>

/* Pointer to our own library base */
extern struct AwebWebpBase *PluginBase;

/* Library bases shared between translation units */
extern struct Library *AwebPluginBase;
extern struct Library *P96Base;
extern struct GfxBase *GfxBase;

/* Macro for max function.  SAS/C does not provide max() as a builtin
 * or in any standard header; we use the same trivial definition that
 * the GIF plugin (awebgif.h) ships. */
#define max(a,b) ((a) > (b) ? (a) : (b))

/* Declarations of the OO dispatcher functions */
extern __saveds __asm ULONG Dispatchsource(
   register __a0 struct Aobject *,
   register __a1 struct Amessage *);

extern __saveds __asm ULONG Dispatchcopy(
   register __a0 struct Aobject *,
   register __a1 struct Amessage *);

/* Definition of attribute IDs that are used internally. */

#define AOWEBP_Dummy     AOBJ_DUMMYTAG(AOTP_PLUGIN)

#define AOWEBP_Data      (AOWEBP_Dummy+1)
   /* (BOOL) Let decoder task know that there is new data, or EOF was
    * reached. */

#define AOWEBP_Readyfrom (AOWEBP_Dummy+2)
#define AOWEBP_Readyto   (AOWEBP_Dummy+3)
   /* (long) The rows in the bitmap that have gotten new valid data. */

#define AOWEBP_Error     (AOWEBP_Dummy+4)
   /* (BOOL) The decoding process discovered an error */

#define AOWEBP_Bitmap    (AOWEBP_Dummy+5)
   /* (struct BitMap *) The bitmap to use, or NULL to remove
    * the bitmap. */

#define AOWEBP_Width     (AOWEBP_Dummy+6)
#define AOWEBP_Height    (AOWEBP_Dummy+7)
   /* (long) Dimensions of the bitmap. */

#define AOWEBP_Imgready  (AOWEBP_Dummy+8)
   /* (BOOL) The image is ready */

#define AOWEBP_Mask      (AOWEBP_Dummy+9)
   /* (UBYTE *) Transparent mask for the image */

#define AOWEBP_Memory    (AOWEBP_Dummy+10)
   /* (long) Add this amount of memory to current usage */
