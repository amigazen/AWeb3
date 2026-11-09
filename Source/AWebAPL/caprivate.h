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

/* caprivate.h - cache, cabrowse, cabrtask private data */

struct Cache
{  struct Aobject object;
   void *url;                    /* Url cached. */
   UBYTE *name;                  /* File name. */
   void *fh;                     /* Async file handle while writing. */
   long nr;                      /* Sequence number. */
   ULONG date;                   /* Object date in seconds from 1978, server GMT time. */
   ULONG expires;                /* In GMT as we locally believe it. */
   ULONG cachedate;              /* Time object went into cache, in seconds from 1978. */
   UBYTE mimetype[32];
   UBYTE etag[64];               /* ETag value from HTTP response for cache validation */
   long disksize;                /* Size written to disk. */
   USHORT flags;
   struct Node *brnode;          /* Cache browser list node. */
#ifdef AWEB4
   /* Staging path (.part) while writing; published name is only set after rename. */
   UBYTE *staging;
   /* Relative path under the cache root (aa/bb/<id> or legacy AWCDxx/...). */
   UBYTE *relpath;
   /* Coalesce buffer so FS sees large sequential writes, not TCP fragment sizes. */
   UBYTE *wbuf;
   long wbufused;
   ULONG checksum;               /* Running payload checksum for index validation */
#endif
};

#define CACF_NODELETE      0x0001   /* Don't delete cache file. */
#ifdef AWEB4
#define CACF_PUBLISHED     0x0002   /* Final name exists and is indexed as valid */
#define CACF_WRITING       0x0004   /* Staging file open for write */
#define CACF_PENDINGDEL    0x0008   /* Queued for batched delete after index update */
#endif

struct Cabrwindow
{  struct Aobject object;
   void *task;
   struct SignalSemaphore *cachesema;
   LIST(Cache) *cache;
   UBYTE *(*cfnameshort)(UBYTE *);
   struct Library *libbase;
   UBYTE *screenname;
   struct Screen *screen;
   void *winobj;
   struct Window *window;
   ULONG winsigmask;
   struct List gadlist,sortlist;
   struct Gadget *toplayout,*listgad,*findgad,*patgad,*pagelayout,*pagegad,*fstrgad,*pstrgad,
      *fuelgad,*statusgad,*sortgad,*opengad,*savegad,*deletegad;
   long disksize;                /* max disksize as per prefs */
   long currentsize;             /* current occupied disk size */
   long nrfiles;
   short x,y,w,h;
   USHORT sort;
   short lastclick;
   short lastselect;
   USHORT flags;
};

#define CBRF_BREAKING      0x0001   /* Breaking the subtask */
#define CBRF_WASOPEN       0x0002   /* Window was open when screen became invalid */
#define CBRF_WASFIND       0x0004   /* (lastselect) was a find result */

#define AOCBR_Dummy        AOBJ_DUMMYTAG(AOTP_CABROWSE)

/* From subtask to main task: */
#define AOCBR_Close        (AOCBR_Dummy+1)
   /* (BOOL) Window was closed. */

#define AOCBR_Dimx         (AOCBR_Dummy+2)
#define AOCBR_Dimy         (AOCBR_Dummy+3)
#define AOCBR_Dimw         (AOCBR_Dummy+4)
#define AOCBR_Dimh         (AOCBR_Dummy+5)
   /* (long) Changed window dimensions */

#define AOCBR_Open         (AOCBR_Dummy+6)
#define AOCBR_Save         (AOCBR_Dummy+7)
#define AOCBR_Delete       (AOCBR_Dummy+8)
   /* (void *) URL to open, save or delete */

/* From main task to subtask: */
#define AOCBR_Tofront      (AOCBR_Dummy+101)
   /* (BOOL) Push window to front */

#define AOCBR_Addobject    (AOCBR_Dummy+102)
   /* (struct Cache *) Add entry for this object */

#define AOCBR_Remobject    (AOCBR_Dummy+103)
   /* (struct Node *) Remove this entry */

#define AOCBR_Disksize     (AOCBR_Dummy+104)
   /* (long) New total disk size */

/* from cache.c */

extern LIST(Cache) cache;
extern struct SignalSemaphore cachesema;
extern long cadisksize;
extern UBYTE *Cfnameshort(UBYTE *name);

/* from cabrowse.c */

extern void Addcabrobject(struct Cache *cac);
extern void Remcabrobject(struct Cache *cac);

