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

/* fetch.h - AWeb input fetch object */

#ifndef AWEB_FETCH_H
#define AWEB_FETCH_H

#include "object.h"

/*--- fetch tags ---*/

#define AOFCH_Dummy        AOBJ_DUMMYTAG(AOTP_FETCH)

#define AOFCH_Cache        (AOFCH_Dummy+1)
   /* (BOOL) Load from cache. Name is full file name. */

#define AOFCH_Name         (AOFCH_Dummy+2)
   /* (UBYTE *) File name, full URL, etc. */

#define AOFCH_Url          (AOFCH_Dummy+3)
   /* (void *) The Url object to notify with srcupdate messages */

#define AOFCH_Ifmodifiedsince (AOFCH_Dummy+4)
   /* (ULONG) Verify if object modified since, remote time, in system seconds. */

#define AOFCH_Nocache      (AOFCH_Dummy+5)
   /* (BOOL) Don't use caches */

#define AOFCH_Authorize    (AOFCH_Dummy+6)
   /* (struct Authorize *) Set new authorize details after user authorization. */

#define AOFCH_Cancel       (AOFCH_Dummy+7)
   /* (BOOL) Cancel this fetch. */

#define AOFCH_Postmsg      (AOFCH_Dummy+8)   /* NEW,SET,GET */
   /* (UBYTE *) Data to POST */

#define AOFCH_Referer      (AOFCH_Dummy+9)   /* NEW,SET,GET */
   /* (UBYTE *) Referer string */

#define AOFCH_Windowkey    (AOFCH_Dummy+10)  /* NEW,SET,GET */
   /* (ULONG) Key of window whose ARexxport to use for x-aweb:command, and
    * to identify wait requests by. */

#define AOFCH_Noproxy      (AOFCH_Dummy+11)
   /* (BOOL) Don't use proxy */

#define AOFCH_Loadflags    (AOFCH_Dummy+12)  /* NEW,SET,GET */
   /* (ULONG) Opaque set of flags for this fetch. */

#define AOFCH_Imagefetch   (AOFCH_Dummy+13)  /* NEW,SET */
   /* (BOOL) This fetch counts as image fetch, otherwise document fetch. */

#define AOFCH_Commands     (AOFCH_Dummy+14)  /* NEW */
   /* (BOOL) Shell/ARexx commands are allowed. */

#define AOFCH_Cancellocal  (AOFCH_Dummy+15)  /* SET */
   /* (BOOL) Cancel this fetch only if it is local (cache load or local file) */

#define AOFCH_Jframe       (AOFCH_Dummy+16)  /* NEW */
   /* (void *) Frame object to run javascript: URLs in. */

#define AOFCH_Formwarn     (AOFCH_Dummy+17)  /* NEW */
   /* (BOOL) Warn if form is sent over unsecure link */

#define AOFCH_Channel      (AOFCH_Dummy+18)  /* NEW */
   /* (BOOL) Start a channel fetch. */

#define AOFCH_Channelid    (AOFCH_Dummy+19)  /* GET */
   /* (long) ID for this channel fetch or 0. */

#define AOFCH_Multipartdata (AOFCH_Dummy+20) /* NEW */
   /* (struct Multipartdata *) Multipart form data to post */

#define AOFCH_Etag      (AOFCH_Dummy+21)   /* NEW,SET,GET */
   /* (UBYTE *) ETag value for If-None-Match header */

#define AOFCH_    (AOFCH_Dummy+)
#define AOFCH_    (AOFCH_Dummy+)

/*--- fetch functions ---*/

extern BOOL Transferring(void);
   /* Returns TRUE if any fetches are going on. */

extern BOOL Windowtransferring(ULONG windowkey);
   /* Returns TRUE if there are active network transfers for the specified window. */

extern void Windowfetchstats(ULONG windowkey,long *netrunning,long *netqueued,
   long *localqueued,long *imgrunning,long *imgqueued);
   /* Return simple per-window fetch counts. All output pointers are optional. */

extern void Fetchslotfills(long *netactive,long *netqueued,long *localactive,long *localqueued);
   /* Global scheduler: nrnet/nrlocal active slots and net/local queue depths (waiting fetches). */

extern void Addwaitrequest(struct Arexxcmd *ac,ULONG windowkey,BOOL doc,BOOL img,void *url);
   /* Reply this ARexx command when selected transfers are ready */

extern BOOL Isurlloading(void *url);
   /* Returns TRUE if the given URL has any active fetches */

extern void Cancelfetchesbyreferer(UBYTE *refererurl);
   /* Cancel all fetches that have the specified referer URL string */

extern void Cancelwindowfetches(ULONG windowkey);
   /* Cancel all fetches for the specified window - like CancelAll but for one window */

/*--- channel fetch ---*/

extern BOOL Channelfetch(struct Arexxcmd *ac,long id,...);
   /* Sends this data to the channel task. When it returns TRUE, the ARexx command
    * will be replied when the data is processed.
    * When it retruns FALSE, the ARexx command is not replied. */

#define AOFCC_Dummy        (AOFCH_Dummy+128)

#define AOFCC_Header       (AOFCC_Dummy+1)
   /* (UBYTE *) HTTP header. Use dynamic string, will be freed after processing. */

#define AOFCC_Data         (AOFCC_Dummy+2)
   /* (UBYTE *) Data. Use dynamic string, will be freed after processing. */

#define AOFCC_Newline      (AOFCC_Dummy+3)
   /* (BOOL) Send newline */

#define AOFCC_Close        (AOFCC_Dummy+4)
   /* (BOOL) Close channel */

#define AOFCC_    (AOFCC_Dummy+)

#endif
