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

/* awebdef.h - AWeb definitions both valid for main and AWebLib modules */

#ifndef AWEBDEF_H
#define AWEBDEF_H

#include <exec/types.h>
#include "ezlists.h"

#include <clib/exec_protos.h>
#include <clib/alib_protos.h>
#include <clib/dos_protos.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/diskfont.h>
#include <proto/layers.h>
#include <proto/colorwheel.h>
#include <proto/gadtools.h>
#include <proto/datatypes.h>
#include <proto/asl.h>
#include <proto/keymap.h>
#include <proto/icon.h>
#include <proto/locale.h>
#include <proto/wb.h>
#include <proto/iffparse.h>

#define CATCOMP_NUMBERS
#include "locale.h"

#include "keyfile.h"

#define FILEBLOCKSIZE   8192  /* blocksize on local file reads */
#define INPUTBLOCKSIZE  16384 /* blocksize in protocol interface (Input) - increased to handle long headers more common in 2025 */
#define TEXTBLOCKSIZE   4096  /* blocksize on document text buffer */

/* zlib constants if not defined by zlib.h TODO: double check if this is needed */
#ifndef Z_NULL
#define Z_NULL 0
#endif
#ifndef Z_OK
#define Z_OK 0
#endif
#ifndef Z_STREAM_END
#define Z_STREAM_END 1
#endif
#ifndef Z_NEED_DICT
#define Z_NEED_DICT 2
#endif
#ifndef Z_STREAM_ERROR
#define Z_STREAM_ERROR (-2)
#endif
#ifndef Z_DATA_ERROR
#define Z_DATA_ERROR (-3)
#endif
#ifndef Z_MEM_ERROR
#define Z_MEM_ERROR (-4)
#endif
#ifndef Z_BUF_ERROR
#define Z_BUF_ERROR (-5)
#endif
#ifndef Z_SYNC_FLUSH
#define Z_SYNC_FLUSH 2
#endif
#define STATUSBUFSIZE   192   /* size of status text buffer */
#define STRINGBUFSIZE   256   /* size of general string buffer */

struct Buffer              /* an expandable text buffer */
{  long size;              /* size of buffer */
   long length;            /* length of information */
   UBYTE *buffer;          /* contents of buffer */
};

struct Arect               /* a rectangle */
{  long minx,miny;
   long maxx,maxy;
};

struct Authorize           /* HTTP Basic authorization scheme data */
{  UBYTE *server;          /* Server name (dynamic) */
   UBYTE *realm;           /* Realm name (dynamic) */
   UBYTE *cookie;          /* Encoded uid/pwd for server/realm (dynamic) */
};

/* Network status window codes */
enum NWS_STATUSES
{  NWS_QUEUED=1,NWS_STARTED,NWS_LOOKUP,NWS_CONNECT,NWS_LOGIN,NWS_NEWSGROUP,
   NWS_UPLOAD,NWS_WAIT,NWS_READ,NWS_PROCESS,NWS_KEEPALIVE,NWS_END,
};

/* OS dependency */
extern BOOL has35;

#ifdef NEED35
#define OSNEED(a,b) (b)
#define OSDEP(a,b) (b)
#else
#define OSNEED(a,b) (a)
#define OSDEP(a,b) (has35?(b):(a))
#endif


#ifndef AWEBPREFS_H
#include "awebprefs.h"
#endif

#define ALLOCPLUS(t,n,p,f)    (t*)Allocmem((n)*sizeof(t)+(p),(f)|MEMF_PUBLIC)
#define ALLOCTYPE(t,n,f)      (t*)Allocmem((n)*sizeof(t),(f)|MEMF_PUBLIC)
#define ALLOCSTRUCT(s,n,f)    ALLOCTYPE(struct s,n,f)
#define FREE(p)            Freemem(p)

#define STRNIEQUAL(a,b,n)  !strnicmp(a,b,n)
#define STRNEQUAL(a,b,n)   !strncmp(a,b,n)
#define STRIEQUAL(a,b)     !stricmp(a,b)
#define STREQUAL(a,b)      !strcmp(a,b)

#define BOOLVAL(x)         (BOOL)((x)!=0)

#define SETFLAG(f,v,c)     if(c) (f)|=(v);else (f)&=~(v)

#define MAX(a,b)           (((a)>(b))?(a):(b))
#define MIN(a,b)           (((a)<(b))?(a):(b))

/* get pointer to first vararg after p */
#define VARARG(p)          (void *)((ULONG)&p+sizeof(p))

#define NULLSTRING         ((UBYTE *)"")

#endif
