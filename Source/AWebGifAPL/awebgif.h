/**********************************************************************
 * 
 * This file is part of the AWeb distribution
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

/* awebgif.h - AWeb gif plugin general definitions */

#include <libraries/awebplugin.h>

/* Define DEBUG_PLUGINS to enable debug output via Aprintf() */
/* #define DEBUG_PLUGINS */

/* Base pointers of libraries needed */
extern struct Library *SysBase;         /* Defined in startup.c */
extern struct Library *DOSBase;
extern struct Library *AwebPluginBase;
extern struct Library *P96Base;

/* Pointer to our own library base */
extern struct AwebGifBase *PluginBase;

/* Declarations of the OO dispatcher functions */
extern __saveds __asm ULONG Dispatchsource(
   register __a0 struct Gifsource *,
   register __a1 struct Amessage *);

extern __saveds __asm ULONG Dispatchcopy(
   register __a0 struct Gifcopy *,
   register __a1 struct Amessage *);

/* Declaration of common flags */
extern BOOL animate;

/* Macro for max function */
#define max(a,b) ((a) > (b) ? (a) : (b))

/* Definition of attribute IDs that are used internally. */

#define AOGIF_Dummy     AOBJ_DUMMYTAG(AOTP_PLUGIN)

#define AOGIF_Data      (AOGIF_Dummy+1)
   /* (BOOL) Let decoder task know that there is new data, or EOF was
    * reached. */

#define AOGIF_Readyfrom (AOGIF_Dummy+2)
#define AOGIF_Readyto   (AOGIF_Dummy+3)
   /* (long) The rows in the bitmap that has gotten new valid data. */

#define AOGIF_Error     (AOGIF_Dummy+4)
   /* (BOOL) The decoding process discovered an error */

#define AOGIF_Bitmap    (AOGIF_Dummy+5)
   /* (struct BitMap *) The bitmap to use, or NULL to remove
    * the bitmap. */

#define AOGIF_Width     (AOGIF_Dummy+6)
#define AOGIF_Height    (AOGIF_Dummy+7)
   /* (long) Dimensions of the bitmap. */

#define AOGIF_Imgready  (AOGIF_Dummy+8)
   /* (BOOL) The image is ready */

#define AOGIF_Mask      (AOGIF_Dummy+9)
   /* (UBYTE *) Transparent mask for the image */

#define AOGIF_Decodeready (AOGIF_Dummy+10)
   /* (BOOL) Image or animation decoding is complete */

#define AOGIF_Animframe (AOGIF_Dummy+11)
   /* (BOOL) This is a new animation frame. If it is transparent, clear
    * the background. */

#define AOGIF_Jsready   (AOGIF_Dummy+12)
   /* (BOOL) Decoding is complete */

#define AOGIF_Jsanim    (AOGIF_Dummy+13)
   /* (BOOL) The last animation frame is complete */

#define AOGIF_Memory    (AOGIF_Dummy+14)
   /* (long) Absolute source-owned memory accounting in bytes.
    * The carried value is the subtask's snapshot of
    * GifSourceMemoryBytes() (encoded Datablocks + every animation
    * frame's BitMap/mask), but Updatesource() discards it and
    * recomputes from the current Gifsource state.  Senders MUST
    * set (not add) this attribute. */

#define AOGIF_Maxloops  (AOGIF_Dummy+15)
   /* (long) Maximum number of loops to show, or -1 for infinite. */

#define AOGIF_Newloop   (AOGIF_Dummy+16)
   /* (BOOL) This animation frame starts a new loop */

#define AOGIF_Loops     (AOGIF_Dummy+17)
   /* (long) Number of loops to show for this image file, or 0 for infinite. */