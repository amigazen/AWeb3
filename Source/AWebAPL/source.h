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

/* source.h - AWeb source interpreter object */

#ifndef AWEB_SOURCE_H
#define AWEB_SOURCE_H

#include "object.h"

/*--- source tags ---*/

#define AOSRC_Dummy        AOBJ_DUMMYTAG(AOTP_SOURCE)

#define AOSRC_Url          (AOSRC_Dummy+1)
   /* (void *) Url this source belongs to */

#define AOSRC_Displayed    (AOSRC_Dummy+2)
   /* (BOOL) Register as displayed or undisplayed */

#define AOSRC_Driver       (AOSRC_Dummy+3)
   /* (void *) GET current source driver object */

#define AOSRC_Usedriver    (AOSRC_Dummy+4)
   /* (BOOL) When SET, create copydrivers for all copies that don't have
    * a driver yet. */

#define AOSRC_Movetourl    (AOSRC_Dummy+5)
   /* (void *) When SET, detach all COPYs and attach them to the SOURCE
    * of this URL. */

#define AOSRC_Saveas       (AOSRC_Dummy+6)
   /* (BOOL) Ignore content type, create a SAVEAS driver. */

#define AOSRC_Savesource   (AOSRC_Dummy+7)
   /* (BOOL) Save the source */

#define AOSRC_Viewsource   (AOSRC_Dummy+8)
   /* (BOOL) View the source */

#define AOSRC_Flush        (AOSRC_Dummy+9)   /* SET */
   /* (BOOL) Flush memory occupied by drivers */

#define AOSRC_Memory       (AOSRC_Dummy+10)  /* SET */
   /* (long) Total amount of memory occupied by driver. */

#define AOSRC_Defaulttype  (AOSRC_Dummy+11)  /* SET,GET */
   /* (UBYTE *) If no driver known for this type, try this content type. */

#define AOSRC_Savename     (AOSRC_Dummy+12)  /* SET */
   /* (UBYTE *) In combination with AOSRC_Savesource, use this name to save. */

#define AOSRC_Saveappend   (AOSRC_Dummy+13)  /* SET */
   /* (BOOL) In combination with AOSRC_Savesource and AOSRC_Savename, append
    * to this file. */

#define AOSRC_Getsource    (AOSRC_Dummy+14)  /* GET */
   /* (UBYTE *) Get the source from the driver (if the driver supports it). */

#define AOSRC_Editsource   (AOSRC_Dummy+15)  /* SET */
   /* (BOOL) Edit the source */

#define AOSRC_Lastmodified (AOSRC_Dummy+16)  /* GET */
   /* (ULONG) Last modified date as source remembers it. */

#define AOSRC_Noicon       (AOSRC_Dummy+17)  /* NEW,SET */
   /* (BOOL) Don't save icon (use in combination with AOSRC_Saveas) */

#define AOSRC_Noflush      (AOSRC_Dummy+18)  /* NEW,SET */
   /* (BOOL) Never flush this source */

#define AOSRC_Docext       (AOSRC_Dummy+19)  /* NEW */
   /* (BOOL) Ignore content type, create DOCEXT driver. */

#define AOSRC_Foreign      (AOSRC_Dummy+20)  /* GET */
   /* (BOOL) Data uses foreign character set */

#define AOSRC_Filename     (AOSRC_Dummy+21)  /* GET */
   /* (UBYTE *) Filename from Content-Disposition (set on SOURCE via fetch srcupdate). */

#define AOSRC_    (AOSRC_Dummy+)
#define AOSRC_    (AOSRC_Dummy+)


/*--- source relationships ---*/

#define AOREL_SRC_DUMMY    AOREL_DUMMYTAG(AOTP_SOURCE)

#define AOREL_SRC_COPY     (AOREL_SRC_DUMMY+1)
   /* Relation between a SOURCE object and its COPY objects */


/*--- source functions ---*/

extern void Flushsources(USHORT type);
   /* Remove drivers from memory */

/* flush types */

#define SRCFT_NDIMAGES     1  /* Nondisplayed nondocuments */
#define SRCFT_ALLIMAGES    2  /* All nondocuments */
#define SRCFT_NDDOCUMENTS  3  /* Nondisplayed documents */
#define SRCFT_EXCESS       4  /* All objects beyond memory limit */

extern void Srcsetfiltertype(void *handle,UBYTE *type);
extern void Srcwritefilter(void *handle,UBYTE *data,long length);

#endif
