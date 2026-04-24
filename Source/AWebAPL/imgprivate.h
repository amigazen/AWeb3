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

/* imgprivate.h - AWeb image drivers private */

#include "object.h"

/*--- Image source driver ---*/

struct Imgsource
{  struct Aobject object;
   void *source;              /* Our source object */
   void *file;                /* Temporary file to hold image source */
   UBYTE *filename;           /* Name of file holding image source */
   void *dto;                 /* Datatype object */
   struct BitMap *bitmap;     /* Bitmap of image */
   UBYTE *mask;               /* Transparent mask */
   long width,height,depth;   /* Bitmap dimensions */
   USHORT flags;
   void *task;                /* The processing task */
   UBYTE *bmpdata;            /* BMP data for ICO decoder (DTST_MEMORY) */
   long bmpdatasize;          /* Size of BMP data */
};

#define IMSF_EOF        0x0001   /* EOF was reached on input */
#define IMSF_OURMASK    0x0002   /* Transparent mask is our own */
#define IMSF_CACHEFILE  0x0004   /* Use cache file as input */
#define IMSF_ERROR      0x0008   /* Error, don't process */
#define IMSF_DECODEWAIT  0x0010   /* Decode requested, waiting for EOF */

/*--- Private tags: ---*/

#define AOIMP_Dummy        AOBJ_DUMMYTAG(AOTP_IMGCOPY)

#define AOIMP_Srcupdate    (AOIMP_Dummy+101)
   /* (BOOL) Bitmap is available or disappeared */

/*--- Imgsource private control tags (set on AOTP_IMGSOURCE objects) ---*/

#ifndef AOIMS_Dummy
#define AOIMS_Dummy           AOBJ_DUMMYTAG(AOTP_IMGSOURCE)
#endif

#define AOIMS_Requestdecode   (AOIMS_Dummy+100)
   /* (BOOL) Request starting decode now (experimental lazy decode). */

