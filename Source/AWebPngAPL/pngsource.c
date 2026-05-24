/**********************************************************************
 * 
 * This file is part of the AWeb distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
 * Changes Copyright (C) 2025-2026 amigazen project
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

/* pngsource.c - AWeb png plugin sourcedriver */

#include "pluginlib.h"
#include "awebpng.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "ezlists.h"
#include <libraries/awebplugin.h>
#include <libraries/Picasso96.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <datatypes/pictureclass.h>
#include <proto/awebplugin.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/graphics.h>
#include <clib/graphics_protos.h>
#include <proto/utility.h>
#include <proto/Picasso96.h>

/* AWeb plugin functions are declared in proto/awebplugin.h */

/* Define STDC and OF() macro for SAS/C compatibility with zlib */
#ifndef STDC
#define STDC
#endif
#ifndef OF
#define OF(args) args
#endif

/* Include zconf.h before png.h to ensure OF() macro is defined correctly for SAS/C */
#include <zconf.h>
#include "png.h"

/* Alpha values >= THRESHOLD are opaque, < THRESHOLD are transparent */
/* As suggested by the PNG docs, treat only alpha 0 as transparent */
#define THRESHOLD 0x01

/*--------------------------------------------------------------------*/
/* Helper functions                                                   */
/*--------------------------------------------------------------------*/

/* Stub function for _XCEXIT to avoid stdio dependency */
/* This is called by SAS/C when stdio functions are used, even if we've disabled them */
void __stdargs _XCEXIT(long x)
{
   /* No-op stub - we don't use stdio */
}

/* Case-insensitive string comparison (replacement for strnicmp to avoid stdio dependency) */
static int mystrnicmp(const char *s1, const char *s2, int n)
{  int i;
   char c1, c2;
   if(!s1 || !s2 || n <= 0) return 0;
   for(i = 0; i < n && *s1 && *s2; i++, s1++, s2++)
   {  c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : *s1;
      c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : *s2;
      if(c1 != c2) return (c1 < c2) ? -1 : 1;
   }
   if(i < n && *s1 != *s2)
   {  c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : *s1;
      c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : *s2;
      return (c1 < c2) ? -1 : 1;
   }
   return 0;
}

/*--------------------------------------------------------------------*/
/* General data structures                                            */
/*--------------------------------------------------------------------*/

/* A struct Datablock holds one block of data. Once it has been read
 * from the list, the data can be accessed without semaphore protection
 * because it will never change (but the ln_Succ pointer will!).
 *
 * Newdatablock() places the payload immediately after the struct using
 * one single AllocVec, so one FreeVec releases everything.  This halves
 * allocator pressure compared to the original (two separate AllocVec
 * calls per 16 KB chunk). */
struct Datablock
{  NODE(Datablock);
   UBYTE *data;                     /* Points just past the struct */
   long length;                     /* Length of the data */
};

/* Allocate a Datablock with the payload appended in a single AllocVec. */
static struct Datablock *Newdatablock(long length)
{  struct Datablock *db;
   if(length<=0) return NULL;
   db=(struct Datablock *)AllocVec(sizeof(struct Datablock)+length,
                                   MEMF_PUBLIC|MEMF_CLEAR);
   if(!db) return NULL;
   db->data=(UBYTE *)(db+1);
   db->length=length;
   return db;
}

/* The object instance data for the source driver */
struct Pngsource
{  struct Sourcedriver sourcedriver;/* Or superclass object instance */
   struct Aobject *source;          /* The AOTP_SOURCE object we belong to */
   struct Aobject *task;            /* Our decoder task */
   struct BitMap *bitmap;           /* The bitmap for the image */
   UBYTE *mask;                     /* Transparent mask for the image */
   long width,height;               /* Bitmap dimensions */
   long ready;                      /* Last complete row in bitmap */
   long memory;                     /* Memory in use */
   LIST(Datablock) data;            /* A linked list of data blocks */
   struct SignalSemaphore sema;     /* Protect the common data */
   USHORT flags;                    /* See below */
   short progress;                  /* Progressive display mode */
   float gamma;                     /* Screen gamma correction */
   UBYTE map[256];                  /* Palette entries mapped to these pens */
   UBYTE mapped[256];               /* This palette entry has been mapped */
   UBYTE allocated[256];            /* This screen pen (not palette entry) was allocated */
   struct BitMap *friendbitmap;     /* Friend bitmap */
   struct ColorMap *colormap;       /* The colormap to obtain pens from */
};

/* Pngsource flags: */
#define PNGSF_EOF          0x0001   /* EOF was received */
#define PNGSF_DISPLAYED    0x0002   /* This image is being displayed */
#define PNGSF_MEMORY       0x0004   /* Our owner knows our memory size */
#define PNGSF_READY        0x0010   /* Decoding is ready */
#define PNGSF_LOWPRI       0x0020   /* Run decoder at low priority */
#define PNGSF_DISPOSING    0x0040   /* Source is being disposed - decoder should exit */

/*--------------------------------------------------------------------*/
/* The decoder subtask                                                */
/*--------------------------------------------------------------------*/

/* This structure holds vital data for the decoding subprocess.
 *
 * Stream access design:
 *   bufptr/bufend describe the contiguous range of bytes currently
 *   available without touching the semaphore or the message port.
 *   libpng calls Readblock() for row-sized fetches, which uses memcpy
 *   on the current Datablock and only enters the slow path
 *   (Refillblock) when the block is exhausted.  This eliminates the
 *   per-byte index update, the per-call NULL/bounds checks and the
 *   per-call message-port drain that the original Readblock did. */
struct Decoder
{  struct Datablock *current;       /* The current datablock */
   UBYTE *bufptr;                   /* Next byte to be read inside current */
   UBYTE *bufend;                   /* One past last byte inside current */
   struct Pngsource *source;        /* Points back to our Pngsource */
   /* Output fields: */
   struct BitMap *bitmap;           /* Bitmap to be filled */
   UBYTE *mask;                     /* Transparent mask to be built */
   long width,height,depth;         /* Bitmap dimensions */
   long maskw;                      /* Width of transparent mask */
   USHORT flags;                    /* See below */
   short progress;                  /* Progress counter */
   UBYTE transpcolor;               /* Transparent color if transparent */
   struct RastPort rp;              /* Temporary for pixelline8 functions */
   struct RastPort temprp;          /* Temporary for pixelline8 functions */
   UBYTE *chunky;                   /* Row of chunky pixels */
   struct RenderInfo renderinfo;    /* RenderInfo for p96WritePixelArray */
   APTR pool;                       /* Per-decoder libpng allocation pool */
};

/* Decoder flags: */
#define DECOF_STOP         0x0001   /* The decoder process should stop */
#define DECOF_EOF          0x0002   /* No more data */
#define DECOF_TRANSPARENT  0x0004   /* Transparent png */
#define DECOF_INTERLACED   0x0008   /* Interlaced png */
#define DECOF_P96MAP       0x0010   /* Bitmap is Picasso96 */
#define DECOF_P96DEEP      0x0020   /* Bitmap is Picasso96 >8 bits */

/*--------------------------------------------------------------------*/
/* Read from the input stream                                         */
/*--------------------------------------------------------------------*/

/* Slow-path block transition: called only when the current contiguous
 * range (bufptr..bufend) has been fully consumed.  Drains pending task
 * messages, takes the source semaphore to swap to the next Datablock,
 * and blocks via Waittask() if no further block is queued and EOF has
 * not yet been signalled.  Returns TRUE if a new block was installed
 * (bufptr/bufend updated), FALSE if EOF or STOP was reached. */
static BOOL Refillblock(struct Decoder *decoder)
{  struct Taskmsg *tm;
   struct TagItem *tag,*tstate;
   struct Datablock *db;
   BOOL wait;
   if(!decoder || !decoder->source) return FALSE;
   for(;;)
   {  wait=FALSE;
      /* Drain pending task messages once per refill, not per byte. */
      while(!(decoder->flags&DECOF_STOP) && (tm=Gettaskmsg()))
      {  if(tm->amsg && tm->amsg->method==AOM_SET)
         {  tstate=((struct Amset *)tm->amsg)->tags;
            while((tag=NextTagItem(&tstate)))
            {  switch(tag->ti_Tag)
               {  case AOTSK_Stop:
                     if(tag->ti_Data) decoder->flags|=DECOF_STOP;
                     break;
                  case AOPNG_Data:
                     /* Legacy wakeup, port-signal already arrived. */
                     break;
               }
            }
         }
         Replytaskmsg(tm);
      }
      if(decoder->flags&DECOF_STOP) return FALSE;

      ObtainSemaphore(&decoder->source->sema);
      if(decoder->current) db=decoder->current->next;
      else db=decoder->source->data.first;
      if(db && db->next)
      {  decoder->current=db;
         decoder->bufptr=db->data;
         decoder->bufend=db->data+db->length;
      }
      else if(decoder->source->flags&PNGSF_EOF)
      {  decoder->flags|=DECOF_EOF;
      }
      else
      {  wait=TRUE;
      }
      ReleaseSemaphore(&decoder->source->sema);

      if(decoder->flags&DECOF_EOF) return FALSE;
      if(!wait) return TRUE;
      Waittask(0);
   }
}

/* Read the next byte from the stream.  When the SAS/C compiler honours
 * OPTINL the common case compiles to a load, compare and post-increment
 * - no function call, no semaphore, no message poll. */
static UBYTE Readbyte(struct Decoder *d)
{  if(d->bufptr<d->bufend) return *d->bufptr++;
   if(!Refillblock(d)) return 0;
   return *d->bufptr++;
}

/* Bulk read used by libpng's Readpngdata callback.  Each iteration
 * memcpy()s as much as is already available in the current Datablock,
 * falling back to Refillblock() only when the block is exhausted. */
static BOOL Readblock(struct Decoder *decoder,UBYTE *block,long length)
{  long available,copylen;
   if(!decoder || !decoder->source) return FALSE;
   if(decoder->flags&(DECOF_STOP|DECOF_EOF)) return FALSE;
   while(length>0)
   {  available=(long)(decoder->bufend-decoder->bufptr);
      if(available>0)
      {  copylen=(available<length)?available:length;
         if(block)
         {  memcpy(block,decoder->bufptr,copylen);
            block+=copylen;
         }
         decoder->bufptr+=copylen;
         length-=copylen;
         continue;
      }
      if(!Refillblock(decoder)) return FALSE;
   }
   return (BOOL)!(decoder->flags&(DECOF_STOP|DECOF_EOF));
}

static void Readpngdata(png_structp png,png_bytep data,png_size_t length)
{  struct Decoder *decoder=(struct Decoder *)png_get_io_ptr(png);
   if(!Readblock(decoder,data,length))
   {  png_error(png,"Read Error");
   }
}

/*--------------------------------------------------------------------*/
/* Memory allocation functions                                        */
/*--------------------------------------------------------------------*/

/* AmigaOS-native pooled allocator for libpng.
 *
 * libpng makes hundreds of small allocations per decode (png_struct,
 * png_info, row pointers, zlib internal state, scanline buffers,...).
 * The original wrappers called AllocMem(MEMF_PUBLIC) for every one.
 * AllocMem walks the system free list inside a Forbid()/Permit() pair
 * - cheap individually but pathological in aggregate.
 *
 * Architecture, two-tier:
 *
 *   1. Per-decoder pool (the fast path).  Each Decoder owns an APTR
 *      pool created at the start of Parsepngimage().  We pass it to
 *      libpng via png_create_read_struct_2() with custom Pngmalloc /
 *      Pngmfree callbacks; libpng (and zlib called from within libpng)
 *      then routes every allocation through these callbacks.  Because
 *      only one task (the decoder subtask) owns the pool, NO
 *      synchronisation is required - the callbacks are pure
 *      AllocPooled()/FreePooled().  The pool is destroyed in one shot
 *      at the end of Parsepngimage(); individual FreePooled() calls
 *      from libpng become no-ops in practice because the puddles are
 *      kept until DeletePool().
 *
 *   2. Global pool (the safety net).  Any code that still calls plain
 *      malloc()/free()/realloc() - SAS/C runtime helpers, ad hoc
 *      callers - falls through to this shared pool.  It is protected
 *      by a SignalSemaphore because multiple decoder tasks could race,
 *      but in practice it is rarely used now that libpng goes through
 *      its own callbacks.
 *
 * The header byte trick is the same in both tiers: a ULONG stored just
 * before the returned pointer records the total block size so that
 * FreePooled() can be called without the caller passing the size. */

/* ---- Per-decoder pool callbacks for libpng ------------------------ */

static png_voidp Pngmalloc(png_structp png_ptr,png_alloc_size_t size)
{  APTR pool;
   ULONG *p;
   pool=png_get_mem_ptr(png_ptr);
   if(!pool) return NULL;
   p=(ULONG *)AllocPooled(pool,(ULONG)(size+4));
   if(!p) return NULL;
   *p=(ULONG)(size+4);
   return (png_voidp)(p+1);
}

static void Pngmfree(png_structp png_ptr,png_voidp ptr)
{  APTR pool;
   ULONG *p;
   if(!ptr) return;
   pool=png_get_mem_ptr(png_ptr);
   if(!pool) return;
   p=((ULONG *)ptr)-1;
   FreePooled(pool,p,*p);
}

/* ---- Global pool fallback ---------------------------------------- */

static APTR Pngpool=NULL;
static struct SignalSemaphore Pngpoolsema;
static BOOL Pngpoolsemainit=FALSE;

/* Lazily create the global pool on first use.  Forbid() makes the
 * init sequence atomic against other tasks on single-CPU m68k. */
static APTR Pngpool_get(void)
{  if(Pngpool) return Pngpool;
   Forbid();
   if(!Pngpoolsemainit)
   {  InitSemaphore(&Pngpoolsema);
      Pngpoolsemainit=TRUE;
   }
   if(!Pngpool)
   {  /* 32 KB puddles, 4 KB threshold.  Tuned for occasional small
       * allocations rather than libpng's heavy traffic, which now
       * has its own per-decoder pool. */
      Pngpool=CreatePool(MEMF_PUBLIC,32768UL,4096UL);
   }
   Permit();
   return Pngpool;
}

void Pngsource_freepool(void)
{  if(!Pngpoolsemainit) return;
   ObtainSemaphore(&Pngpoolsema);
   if(Pngpool)
   {  DeletePool(Pngpool);
      Pngpool=NULL;
   }
   ReleaseSemaphore(&Pngpoolsema);
}

void *malloc(size_t size)
{  APTR pool;
   ULONG *p;
   pool=Pngpool_get();
   if(!pool) return NULL;
   ObtainSemaphore(&Pngpoolsema);
   p=(ULONG *)AllocPooled(pool,(ULONG)(size+4));
   ReleaseSemaphore(&Pngpoolsema);
   if(!p) return NULL;
   *p=(ULONG)(size+4);
   return (void *)(p+1);
}

void free(void *q)
{  ULONG *p;
   ULONG sz;
   if(!q || !Pngpool) return;
   p=((ULONG *)q)-1;
   sz=*p;
   ObtainSemaphore(&Pngpoolsema);
   FreePooled(Pngpool,p,sz);
   ReleaseSemaphore(&Pngpoolsema);
}

void *realloc(void *q,size_t size)
{  void *np;
   ULONG *p;
   ULONG oldsize=0;
   long copysize;
   if(q)
   {  p=((ULONG *)q)-1;
      oldsize=(*p)-4;
   }
   np=malloc(size);
   if(np && q)
   {  copysize=((long)size<(long)oldsize)?(long)size:(long)oldsize;
      if(copysize>0) memmove(np,q,(size_t)copysize);
   }
   if(q) free(q);
   return np;
}

/*--------------------------------------------------------------------*/
/* Error handling                                                     */
/*--------------------------------------------------------------------*/

/* Prevent exit() meing called from fp functions */
void __stdargs _CXFERR(int code)
{  _FPERR=code;
   /* no raise() here */
}

/*--------------------------------------------------------------------*/
/* Parse the PNG file and build bitmap                                */
/*--------------------------------------------------------------------*/


#define RGB(x) (((x)<<24)|((x)<<16)|((x)<<8)|(x))

/* Read image parameters and data */
static BOOL Parsepngimage(struct Decoder *decoder)
{  png_structp png=NULL;
   png_infop pnginfo=NULL;
   png_uint_32 width,height;
   int bitdepth,colortype,interlace,npalette,ntrans=0;
   double gamma;
   png_color *palette=NULL;
   png_bytep trans=NULL;
   png_byte *buffer=NULL;
   png_byte **rows=NULL;
   long rowbytes,depth=8,npass=0,pass,x,fromrow,y;
   short pen=0,map=0;
   UBYTE *trow=NULL;
   BOOL error=FALSE;
   BOOL usewritechunky;
   int pi;
   /* 256-entry hash cache for RGB->screen-pen lookups on 8-bit colour
    * paths.  ObtainBestPen() is by far the most expensive operation in
    * the 8-bit pixel loop (it walks the system colormap on every call).
    * A direct-mapped hash table keyed on a fold of the 24-bit colour
    * lets us serve ~all repeated colours from cache in O(1).
    *
    *   - rgbcache_rgb[h] holds the full packed RGB triple for slot h,
    *     or the sentinel 0xFFFFFFFF when the slot is empty.  Real
    *     PNG pixels can never reach 0xFFFFFFFF (top byte is always 0),
    *     so the sentinel is unambiguous.
    *   - rgbcache_pen[h] holds the screen pen associated with that
    *     colour, valid only when the matching rgb is stored.
    *
    * Compared to the previous single-pixel run-length cache, the hit
    * rate goes from ~adjacent-pixel-equality only (essentially zero
    * for photographs) to ~all-previously-seen-in-this-image, which is
    * a 5x-50x speedup on the inner loop for natural images. */
   ULONG rgbcache_rgb[256];
   UBYTE rgbcache_pen[256];
   
#ifdef DEBUG_PLUGINS
   PngLog("parse","Parsepngimage called, decoder=0x%08lx",(ULONG)decoder);
#endif
   /* Validate decoder and source pointers to prevent NULL pointer dereference */
   if(!decoder || !decoder->source)
   {  #ifdef DEBUG_PLUGINS
      PngLog("parse","Parsepngimage: ERROR - decoder or decoder->source is NULL");
      #endif
      return FALSE;
   }
   /* Create a private allocation pool for this decode.  Every libpng
    * and zlib allocation will be served from it via the Pngmalloc /
    * Pngmfree callbacks below - no semaphore on the hot path. */
   decoder->pool=CreatePool(MEMF_PUBLIC,32768UL,4096UL);
   if(!decoder->pool)
   {  #ifdef DEBUG_PLUGINS
      PngLog("parse","Parsepngimage: ERROR - CreatePool() failed");
      #endif
      return FALSE;
   }
   if((png=png_create_read_struct_2(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL,
         decoder->pool,Pngmalloc,Pngmfree))
   && (pnginfo=png_create_info_struct(png)))
   {  #ifdef DEBUG_PLUGINS
      PngLog("parse","Parsepngimage: PNG structures created");
      #endif
      if(!setjmp(png_jmpbuf(png)))
      {  /* Normal fallthrough */
#ifdef DEBUG_PLUGINS
         PngLog("parse","Parsepngimage: Setting read function and reading info");
#endif
         png_set_read_fn(png,decoder,Readpngdata);
         png_read_info(png,pnginfo);
#ifdef DEBUG_PLUGINS
         PngLog("parse","Parsepngimage: png_read_info completed");
#endif
         png_get_IHDR(png,pnginfo,&width,&height,&bitdepth,&colortype,
            &interlace,NULL,NULL);
         decoder->width=width;
         decoder->height=height;
         
         if(colortype==PNG_COLOR_TYPE_GRAY && bitdepth<8)
         {  png_set_expand(png);
         }

         /* Probe the target friend bitmap to decide whether the eventual
          * output surface will be a deep (>8 bpp) Picasso96 bitmap.  If
          * so, ask libpng to expand palette/gray to RGB (and tRNS to a
          * real alpha channel) up front, which eliminates the hand-rolled
          * per-pixel expansion loops in the deep output path - libpng's
          * own row code is well tuned and runs on freshly decompressed
          * data still hot in cache.
          *
          * The probe is conservative: if it predicts deep but the bitmap
          * allocation later falls back to 8-bit (e.g. p96AllocBitMap()
          * failed), the 8-bit row code happily handles RGB/RGBA input
          * via the hash-cached ObtainBestPen path, so the prediction
          * never produces incorrect output - only a possible speed
          * regression in an edge case that almost never occurs in
          * practice. */
         {  BOOL probable_deep=FALSE;
            if(P96Base && decoder->source && decoder->source->friendbitmap
               && p96GetBitMapAttr(decoder->source->friendbitmap,P96BMA_ISP96)
               && p96GetBitMapAttr(decoder->source->friendbitmap,P96BMA_DEPTH)>8)
            {  probable_deep=TRUE;
            }
            if(probable_deep)
            {  /* Both functions are no-ops on incompatible colortypes. */
               png_set_palette_to_rgb(png);
               png_set_gray_to_rgb(png);
               if(png_get_valid(png,pnginfo,PNG_INFO_tRNS))
               {  png_set_tRNS_to_alpha(png);
                  decoder->flags|=DECOF_TRANSPARENT;
               }
            }
            else
            {  /* 8-bit colormap target: keep palette as palette (so the
                * fast LUT path can be used) and expand only what the
                * downstream code needs. */
               if(png_get_valid(png,pnginfo,PNG_INFO_tRNS))
               {  if(colortype==PNG_COLOR_TYPE_PALETTE)
                  {  png_get_tRNS(png,pnginfo,&trans,&ntrans,NULL);
                  }
                  else
                  {  /* Expand grayscale or color tRNS to alpha channel */
                     png_set_expand(png);
                  }
                  decoder->flags|=DECOF_TRANSPARENT;
               }
            }
         }

         if(bitdepth==16)
         {  png_set_strip_16(png);
         }
         if(bitdepth<8)
         {  png_set_packing(png);
         }
         if(!png_get_gAMA(png,pnginfo,&gamma))
         {  gamma=0.50;
         }
         /* Check source is still valid and not being disposed before accessing */
         if(!decoder->source)
         {  error=TRUE;
            goto cleanup;
         }
         ObtainSemaphore(&decoder->source->sema);
         if(decoder->source->flags&PNGSF_DISPOSING)
         {  ReleaseSemaphore(&decoder->source->sema);
            error=TRUE;
            goto cleanup;
         }
         ReleaseSemaphore(&decoder->source->sema);
         png_set_gamma(png,decoder->source->gamma,gamma);
         npass=png_set_interlace_handling(png);
         
         png_read_update_info(png,pnginfo);
         rowbytes=png_get_rowbytes(png,pnginfo);
         colortype=png_get_color_type(png,pnginfo);
         bitdepth=png_get_bit_depth(png,pnginfo);
         if(colortype&PNG_COLOR_MASK_ALPHA) decoder->flags|=DECOF_TRANSPARENT;
#ifdef DEBUG_PLUGINS
         PngLog("parse","read_update_info done %lux%lu rowbytes=%ld npass=%ld ct=%d bd=%d ilace=%d",
            (unsigned long)width,(unsigned long)height,(long)rowbytes,(long)npass,
            (int)colortype,(int)bitdepth,(int)interlace);
#endif

         /* Hereafter comes the image data. Allocate a bitmap first.
          * Always allocate a bitmap of depth >=8. Even if our source has less
          * planes, the colours may be remapped to pen numbers up to 255. */
         /* Check source is still valid and not being disposed before accessing */
         if(!decoder->source)
         {  error=TRUE;
            goto cleanup;
         }
         ObtainSemaphore(&decoder->source->sema);
         if(decoder->source->flags&PNGSF_DISPOSING)
         {  ReleaseSemaphore(&decoder->source->sema);
            error=TRUE;
            goto cleanup;
         }
         ReleaseSemaphore(&decoder->source->sema);
         /* Only call p96* on friendbitmap when it is a P96 bitmap; screen RastPort
          * BitMaps are often classic planar even if Picasso96.library is open,
          * and p96GetBitMapAttr(P96BMA_DEPTH) on those can deadlock. */
         if(P96Base && decoder->source->friendbitmap
         && p96GetBitMapAttr(decoder->source->friendbitmap,P96BMA_ISP96))
         {  depth=p96GetBitMapAttr(decoder->source->friendbitmap,P96BMA_DEPTH);
            decoder->bitmap=p96AllocBitMap(decoder->width,decoder->height,depth,
               BMF_MINPLANES|BMF_CLEAR|BMF_DISPLAYABLE,decoder->source->friendbitmap,RGBFB_NONE);
            if(decoder->bitmap && p96GetBitMapAttr(decoder->bitmap,P96BMA_ISP96))
            {  decoder->flags|=DECOF_P96MAP;
               if(depth>8)
               {  decoder->flags|=DECOF_P96DEEP;
               }
            }
         }
         else
         {  /* Classic: decode into a non-displayable planar master so graphics.library
             * can place planes in Fast RAM when possible (Chip is reserved for
             * display bitmaps). Rendering code can still copy/blit from this
             * master; on systems where the blitter cannot source from Fast,
             * graphics.library may fall back to CPU copy. */
            decoder->bitmap=AllocBitMap(decoder->width,decoder->height,8,
               BMF_CLEAR|BMF_MINPLANES,NULL);
         }
         if(!decoder->bitmap) return FALSE;
         if(decoder->flags&DECOF_TRANSPARENT)
         {  if(decoder->flags&DECOF_P96MAP)
            {  decoder->maskw=p96GetBitMapAttr(decoder->bitmap,P96BMA_WIDTH)/8;
            }
            else
            {  decoder->maskw=decoder->bitmap->BytesPerRow;
            }
            decoder->mask=(UBYTE *)AllocVec(decoder->maskw*decoder->height,
               MEMF_PUBLIC|MEMF_CLEAR|(decoder->flags&DECOF_P96MAP?0:MEMF_CHIP));
         }
#ifdef DEBUG_PLUGINS
         PngLog("parse","bitmap %lux%lu depth=%ld p96map=%d mask=%p",
            (unsigned long)decoder->width,(unsigned long)decoder->height,(long)depth,
            (decoder->flags&DECOF_P96MAP)?1:0,(APTR)decoder->mask);
#endif

         /* Save our bitmap and dimensions. */
         /* Check source is still valid and not being disposed before accessing */
         if(!decoder->source)
         {  error=TRUE;
            goto cleanup;
         }
         ObtainSemaphore(&decoder->source->sema);
         if(decoder->source->flags&PNGSF_DISPOSING)
         {  ReleaseSemaphore(&decoder->source->sema);
            error=TRUE;
            goto cleanup;
         }
         decoder->source->bitmap=decoder->bitmap;
         decoder->source->mask=decoder->mask;
         decoder->source->width=decoder->width;
         decoder->source->height=decoder->height;
         /* Check disposing flag while holding semaphore to avoid race condition */
         if(decoder->source->flags&PNGSF_DISPOSING)
         {  ReleaseSemaphore(&decoder->source->sema);
            error=TRUE;
            goto cleanup;
         }
         ReleaseSemaphore(&decoder->source->sema);

         Updatetaskattrs(
            AOPNG_Memory,decoder->width*decoder->height*depth/8+
               (decoder->mask?(decoder->maskw*decoder->height/8):0),
            TAG_END);
#ifdef DEBUG_PLUGINS
         PngLog("parse","source wired to bitmap");
#endif

         InitRastPort(&decoder->rp);
         decoder->rp.BitMap=decoder->bitmap;
         if(decoder->flags&DECOF_P96MAP)
         {  error=!(decoder->chunky=AllocVec(decoder->width*4,MEMF_PUBLIC));
            /* Initialize RenderInfo for p96WritePixelArray */
            decoder->renderinfo.Memory=decoder->chunky;
            decoder->renderinfo.BytesPerRow=decoder->width*4;
            decoder->renderinfo.RGBFormat=RGBFB_R8G8B8;
         }
         else
         {  /* The colour mapping process needs a temporary RastPort plus BitMap
             * for the PixelLine8 functions. */
            InitRastPort(&decoder->temprp);
            error=!(decoder->temprp.BitMap=AllocBitMap(
                  8*(((decoder->width+15)>>4)<<1),1,8,BMF_DISPLAYABLE,decoder->bitmap))
               || !(decoder->chunky=AllocVec(((decoder->width+15)>>4)<<4,MEMF_PUBLIC));
            /* Initialize RenderInfo for 8-bit mode */
            decoder->renderinfo.Memory=decoder->chunky;
            decoder->renderinfo.BytesPerRow=decoder->width;
            decoder->renderinfo.RGBFormat=RGBFB_CLUT;
         }
         if(error) longjmp(png_jmpbuf(png),1);
#ifdef DEBUG_PLUGINS
         PngLog("parse","rastport/chunky init ok p96deep=%d",
            (decoder->flags&DECOF_P96DEEP)?1:0);
#endif
         
         /* For normal images, we just need 1 row of data. For interlaced images,
          * we need to remember the entire image because we can't reconstruct
          * the row data from the previous pass just from the bitmap. */
         if(npass==1)
         {  /* Fast path: non-interlaced RGB on a deep Picasso96 surface
             * doesn't need a separate row buffer at all - libpng's RGB
             * output already matches the chunky/R8G8B8 layout exactly,
             * so we point libpng's row pointer at decoder->chunky and
             * skip the per-row memcpy in the deep RGB case below.  This
             * saves width*3 bytes of memory bandwidth per row, which for
             * a typical 1024-wide photograph is around 2 MB of pointless
             * copying eliminated across the whole image. */
            if((decoder->flags&DECOF_P96DEEP)
               && colortype==PNG_COLOR_TYPE_RGB
               && rowbytes<=decoder->width*4)
            {  buffer=(png_byte *)decoder->chunky;
            }
            else
            {  if(!(buffer=AllocVec(rowbytes,0))) longjmp(png_jmpbuf(png),1);
            }
            rows=&buffer;
         }
         else
         {  /* For transparent interlaced palette images, find a palette entry
             * that is transparent. Initialize the rows with that value so
             * early passes won't obscure areas that become transparent in a
             * later pass. If there is no such entry - well, then the image
             * isn't transparent after all and we need not bother. */
            map=0;
            if(colortype==PNG_COLOR_TYPE_PALETTE && trans)
            {  for(pen=0;pen<ntrans;pen++)
               {  if(trans[pen]<THRESHOLD)
                  {  map=pen;
                     break;
                  }
               }
            }
            if(!(rows=AllocVec(decoder->height*sizeof(png_byte *),MEMF_CLEAR)))
            {  longjmp(png_jmpbuf(png),1);
            }
            for(y=0;y<decoder->height;y++)
            {  if(!(rows[y]=AllocVec(rowbytes,map?0:MEMF_CLEAR)))
               {  longjmp(png_jmpbuf(png),1);
               }
               if(map) memset(rows[y],map,rowbytes);
            }
         }
         
         /* For palette images on an 8-bit display, eagerly resolve every
          * palette entry to a screen pen so the inner row loop becomes a
          * pure table lookup (chunky[x] = map[buffer[x]]).  The original
          * code allocated pens lazily inside the per-pixel switch, which
          * still meant 256 ObtainBestPen() calls scattered through the
          * first few rows and a branch per pixel forever after. */
         if(colortype==PNG_COLOR_TYPE_PALETTE)
         {  png_get_PLTE(png,pnginfo,&palette,&npalette);
            if(!(decoder->flags&DECOF_P96DEEP) && palette)
            {  for(pi=0;pi<npalette;pi++)
               {  if(!decoder->source->mapped[pi])
                  {  map=ObtainBestPen(decoder->source->colormap,
                        RGB(palette[pi].red),RGB(palette[pi].green),RGB(palette[pi].blue),
                        TAG_END);
                     if(decoder->source->allocated[map])
                     {  ReleasePen(decoder->source->colormap,map);
                     }
                     else
                     {  decoder->source->allocated[map]=TRUE;
                     }
                     decoder->source->map[pi]=(UBYTE)map;
                     decoder->source->mapped[pi]=1;
                  }
               }
            }
         }
#ifdef DEBUG_PLUGINS
         PngLog("parse","row buffers ready, entering %ld pass(es)",(long)npass);
#endif

         /* Pre-decide which graphics call to use once for the whole image. */
         usewritechunky=(BOOL)(GfxBase && GfxBase->lib_Version>=40);

         /* Mark the RGB->pen hash cache empty.  Real packed RGB values
          * are in the range 0x000000..0xFFFFFF so 0xFFFFFFFF is a safe
          * sentinel.  The cache persists across rows: when a colour
          * recurs in any later row we serve it without ObtainBestPen. */
         memset(rgbcache_rgb,0xFF,sizeof(rgbcache_rgb));

         for(pass=0;pass<npass;pass++)
         {  fromrow=0;
#ifdef DEBUG_PLUGINS
            PngLog("parse","pass %ld/%ld row decode start",(long)(pass+1),(long)npass);
#endif
            for(y=0;y<decoder->height;y++)
            {
               /* For normal images use a fixed buffer. For interlaced images,
                * use a separate buffer for each row. */
               if(npass>1)
               {  buffer=rows[y];
               }

               /* Use 'sparkle' effect for transparent interlaced images */
               if(npass==1 || (decoder->flags&DECOF_TRANSPARENT))
               {  png_read_rows(png,&buffer,NULL,1);
               }
               else
               {  png_read_rows(png,NULL,&buffer,1);
               }
#ifdef DEBUG_PLUGINS
               {  long yy;
                  yy=(long)y;
                  if(yy==0L || (yy&31L)==0L || yy==(long)(decoder->height-1))
                  {  PngLog("parse","row y=%ld/%ld pass=%ld",(long)y,(long)decoder->height,(long)pass);
                  }
               }
#endif

               /* Hoist mask-row pointer out of the per-pixel loops. */
               trow=decoder->mask ? (decoder->mask+y*decoder->maskw) : NULL;

               if(decoder->flags&DECOF_P96DEEP)
               {  /* Direct 24-bit RGB output to a Picasso96 deep bitmap.
                   * Dispatch on colortype once per row, not once per pixel. */
                  switch(colortype)
                  {  case PNG_COLOR_TYPE_GRAY:
                        {  png_byte *s=buffer;
                           UBYTE *d=decoder->chunky;
                           long w=(long)width;
                           for(x=0;x<w;x++)
                           {  d[0]=d[1]=d[2]=s[x];
                              d+=3;
                           }
                        }
                        break;
                     case PNG_COLOR_TYPE_GRAY_ALPHA:
                        {  png_byte *s=buffer;
                           UBYTE *d=decoder->chunky;
                           long w=(long)width;
                           if(trow)
                           {  for(x=0;x<w;x++)
                              {  d[0]=d[1]=d[2]=s[2*x];
                                 if(s[2*x+1]>=THRESHOLD)
                                    trow[x>>3]|=0x80>>(x&0x7);
                                 d+=3;
                              }
                           }
                           else
                           {  for(x=0;x<w;x++)
                              {  d[0]=d[1]=d[2]=s[2*x];
                                 d+=3;
                              }
                           }
                        }
                        break;
                     case PNG_COLOR_TYPE_PALETTE:
                        {  png_byte *s=buffer;
                           UBYTE *d=decoder->chunky;
                           long w=(long)width;
                           if(trow && trans && ntrans>0)
                           {  for(x=0;x<w;x++)
                              {  pen=s[x];
                                 d[0]=palette[pen].red;
                                 d[1]=palette[pen].green;
                                 d[2]=palette[pen].blue;
                                 if(!(pen<ntrans && trans[pen]<THRESHOLD))
                                    trow[x>>3]|=0x80>>(x&0x7);
                                 d+=3;
                              }
                           }
                           else
                           {  for(x=0;x<w;x++)
                              {  pen=s[x];
                                 d[0]=palette[pen].red;
                                 d[1]=palette[pen].green;
                                 d[2]=palette[pen].blue;
                                 d+=3;
                              }
                           }
                        }
                        break;
                     case PNG_COLOR_TYPE_RGB:
                        /* libpng's RGB row already matches our 24-bit chunky
                         * layout exactly.  When the buffer pointer aliases
                         * decoder->chunky (non-interlaced fast path), libpng
                         * has already written the row in-place and we don't
                         * need to do anything here.  Otherwise a single
                         * memcpy() copies the libpng row into chunky. */
                        if((png_byte *)decoder->chunky!=buffer)
                        {  memcpy(decoder->chunky,buffer,(size_t)(width*3));
                        }
                        break;
                     case PNG_COLOR_TYPE_RGB_ALPHA:
                        {  png_byte *s=buffer;
                           UBYTE *d=decoder->chunky;
                           long w=(long)width;
                           if(trow)
                           {  for(x=0;x<w;x++)
                              {  d[0]=s[4*x];
                                 d[1]=s[4*x+1];
                                 d[2]=s[4*x+2];
                                 if(s[4*x+3]>=THRESHOLD)
                                    trow[x>>3]|=0x80>>(x&0x7);
                                 d+=3;
                              }
                           }
                           else
                           {  for(x=0;x<w;x++)
                              {  d[0]=s[4*x];
                                 d[1]=s[4*x+1];
                                 d[2]=s[4*x+2];
                                 d+=3;
                              }
                           }
                        }
                        break;
                  }
                  p96WritePixelArray(&decoder->renderinfo,0,0,
                     &decoder->rp,0,y,decoder->width,1);
               }
               else
               {  /* 8-bit colormap output.  Colortype is invariant for the
                   * whole image, so dispatch the specialised inner loop here
                   * rather than rechecking it for every pixel. */
                  switch(colortype)
                  {  case PNG_COLOR_TYPE_GRAY:
                        {  png_byte *s=buffer;
                           UBYTE *d=decoder->chunky;
                           UBYTE *mp=decoder->source->map;
                           UBYTE *mpd=decoder->source->mapped;
                           struct ColorMap *cm=decoder->source->colormap;
                           UBYTE *alc=decoder->source->allocated;
                           long w=(long)width;
                           for(x=0;x<w;x++)
                           {  pen=s[x];
                              if(mpd[pen])
                              {  d[x]=mp[pen];
                              }
                              else
                              {  map=ObtainBestPen(cm,RGB(pen),RGB(pen),RGB(pen),TAG_END);
                                 if(alc[map])
                                    ReleasePen(cm,map);
                                 else
                                    alc[map]=1;
                                 mp[pen]=(UBYTE)map;
                                 mpd[pen]=1;
                                 d[x]=(UBYTE)map;
                              }
                           }
                        }
                        break;
                     case PNG_COLOR_TYPE_GRAY_ALPHA:
                        {  png_byte *s=buffer;
                           UBYTE *d=decoder->chunky;
                           UBYTE *mp=decoder->source->map;
                           UBYTE *mpd=decoder->source->mapped;
                           struct ColorMap *cm=decoder->source->colormap;
                           UBYTE *alc=decoder->source->allocated;
                           long w=(long)width;
                           for(x=0;x<w;x++)
                           {  pen=s[2*x];
                              if(mpd[pen])
                              {  d[x]=mp[pen];
                              }
                              else
                              {  map=ObtainBestPen(cm,RGB(pen),RGB(pen),RGB(pen),TAG_END);
                                 if(alc[map])
                                    ReleasePen(cm,map);
                                 else
                                    alc[map]=1;
                                 mp[pen]=(UBYTE)map;
                                 mpd[pen]=1;
                                 d[x]=(UBYTE)map;
                              }
                              if(trow && s[2*x+1]>=THRESHOLD)
                                 trow[x>>3]|=0x80>>(x&0x7);
                           }
                        }
                        break;
                     case PNG_COLOR_TYPE_PALETTE:
                        /* Palette pens were resolved before the row loop, so
                         * the inner loop is now just a 256-entry LUT lookup. */
                        {  png_byte *s=buffer;
                           UBYTE *d=decoder->chunky;
                           UBYTE *mp=decoder->source->map;
                           long w=(long)width;
                           for(x=0;x<w;x++) d[x]=mp[s[x]];
                        }
                        if(trow)
                        {  if(trans && ntrans>0)
                           {  png_byte *s=buffer;
                              long w=(long)width;
                              for(x=0;x<w;x++)
                              {  pen=s[x];
                                 if(!(pen<ntrans && trans[pen]<THRESHOLD))
                                    trow[x>>3]|=0x80>>(x&0x7);
                              }
                           }
                           else
                           {  long w=(long)width;
                              for(x=0;x<w;x++) trow[x>>3]|=0x80>>(x&0x7);
                           }
                        }
                        break;
                     case PNG_COLOR_TYPE_RGB:
                        /* 256-entry hash cache for RGB->pen lookups.
                         * The cache persists across the whole image, so
                         * once a colour has been resolved (whether on
                         * row 0 or row 500) every later occurrence is
                         * O(1).  The XOR fold of the three colour
                         * components spreads natural-photograph data
                         * across the table reasonably evenly. */
                        {  png_byte *s=buffer;
                           UBYTE *d=decoder->chunky;
                           struct ColorMap *cm=decoder->source->colormap;
                           UBYTE *alc=decoder->source->allocated;
                           long w=(long)width;
                           ULONG packed;
                           UBYTE h;
                           for(x=0;x<w;x++)
                           {  packed=((ULONG)s[0]<<16)|((ULONG)s[1]<<8)|(ULONG)s[2];
                              h=(UBYTE)((packed>>16)^(packed>>8)^packed);
                              if(rgbcache_rgb[h]==packed)
                              {  d[x]=rgbcache_pen[h];
                              }
                              else
                              {  map=ObtainBestPen(cm,
                                    RGB(s[0]),RGB(s[1]),RGB(s[2]),TAG_END);
                                 if(alc[map])
                                    ReleasePen(cm,map);
                                 else
                                    alc[map]=1;
                                 rgbcache_rgb[h]=packed;
                                 rgbcache_pen[h]=(UBYTE)map;
                                 d[x]=(UBYTE)map;
                              }
                              s+=3;
                           }
                        }
                        if(trow)
                        {  long w=(long)width;
                           for(x=0;x<w;x++) trow[x>>3]|=0x80>>(x&0x7);
                        }
                        break;
                     case PNG_COLOR_TYPE_RGB_ALPHA:
                        /* Same hash cache as RGB; the only difference
                         * is the per-pixel stride (4 bytes) and the
                         * alpha-driven mask write. */
                        {  png_byte *s=buffer;
                           UBYTE *d=decoder->chunky;
                           struct ColorMap *cm=decoder->source->colormap;
                           UBYTE *alc=decoder->source->allocated;
                           long w=(long)width;
                           ULONG packed;
                           UBYTE h;
                           for(x=0;x<w;x++)
                           {  packed=((ULONG)s[0]<<16)|((ULONG)s[1]<<8)|(ULONG)s[2];
                              h=(UBYTE)((packed>>16)^(packed>>8)^packed);
                              if(rgbcache_rgb[h]==packed)
                              {  d[x]=rgbcache_pen[h];
                              }
                              else
                              {  map=ObtainBestPen(cm,
                                    RGB(s[0]),RGB(s[1]),RGB(s[2]),TAG_END);
                                 if(alc[map])
                                    ReleasePen(cm,map);
                                 else
                                    alc[map]=1;
                                 rgbcache_rgb[h]=packed;
                                 rgbcache_pen[h]=(UBYTE)map;
                                 d[x]=(UBYTE)map;
                              }
                              if(trow && s[3]>=THRESHOLD)
                                 trow[x>>3]|=0x80>>(x&0x7);
                              s+=4;
                           }
                        }
                        break;
                  }
                  if(usewritechunky)
                  {  WriteChunkyPixels(&decoder->rp,0,y,decoder->width-1,y,
                        decoder->chunky,decoder->width);
                  }
                  else
                  {  WritePixelLine8(&decoder->rp,0,y,decoder->width,decoder->chunky,
                        &decoder->temprp);
                  }
               }

               if(++decoder->progress==decoder->source->progress || y==decoder->height-1)
               {  Updatetaskattrs(
                     AOPNG_Readyfrom,fromrow,
                     AOPNG_Readyto,y,
                     TAG_END);
                  decoder->progress=0;
                  fromrow=y+1;
               }
               if(pass==npass-1 && y==decoder->height-1)
               {  Updatetaskattrs(
                     AOPNG_Readyfrom,y,
                     AOPNG_Readyto,y,
                     AOPNG_Imgready,TRUE,
                     TAG_END);
               }
            }
#ifdef DEBUG_PLUGINS
            PngLog("parse","pass %ld/%ld rows done",(long)(pass+1),(long)npass);
#endif
         }
#ifdef DEBUG_PLUGINS
         PngLog("parse","all passes complete");
#endif
      }
      else
      {  /* Error jumps to here */
         error=TRUE;
      }
   }
cleanup:
   /* png_destroy_read_struct() must run while the pool is still alive
    * because libpng frees its internal allocations through Pngmfree(),
    * which calls FreePooled() against decoder->pool. */
   png_destroy_read_struct(&png,&pnginfo,NULL);
   if(npass==1)
   {  /* On the non-interlaced deep-RGB fast path, buffer aliases
       * decoder->chunky and must NOT be freed here - chunky is freed
       * separately just below. */
      if(buffer && (!decoder || (png_byte *)decoder->chunky!=buffer))
         FreeVec(buffer);
   }
   else
   {  if(rows)
      {  for(y=0;y<decoder->height;y++)
         {  if(rows[y]) FreeVec(rows[y]);
         }
         FreeVec(rows);
      }
   }
   if(decoder && decoder->chunky) FreeVec(decoder->chunky);
   if(decoder && decoder->temprp.BitMap) FreeBitMap(decoder->temprp.BitMap);
   /* Tear down the per-decoder pool last; all libpng/zlib memory is
    * reclaimed in a single DeletePool() call. */
   if(decoder && decoder->pool)
   {  DeletePool(decoder->pool);
      decoder->pool=NULL;
   }
   return (BOOL)!error;
}

/* Main subtask process. */
static __saveds __asm void Decodetask(register __a0 struct Pngsource *is)
{  struct Decoder decoderdata,*decoder=&decoderdata;
   struct Task *task=FindTask(NULL);
   
#ifdef DEBUG_PLUGINS
   PngLog("decode","Decodetask started, is=0x%08lx",(ULONG)is);
#endif
   if(!is)
   {  #ifdef DEBUG_PLUGINS
      PngLog("decode","Decodetask: ERROR - is (Pngsource) is NULL!");
      #endif
      return;
   }
   memset(&decoderdata,0,sizeof(decoderdata));
   decoder->source=is;
#ifdef DEBUG_PLUGINS
   PngLog("decode","Decodetask: decoder->source set to 0x%08lx",(ULONG)decoder->source);
#endif
   /* Check source is still valid before accessing */
   if(!decoder->source)
   {  #ifdef DEBUG_PLUGINS
      PngLog("decode","Decodetask: ERROR - decoder->source is NULL after assignment");
      #endif
      return;
   }
   if(decoder->source->flags&PNGSF_LOWPRI)
   {  SetTaskPri(task,task->ln_Pri-1);
   }
   
#ifdef DEBUG_PLUGINS
   PngLog("decode","Decodetask: Calling Parsepngimage");
#endif
   if(!Parsepngimage(decoder))
   {  #ifdef DEBUG_PLUGINS
      PngLog("decode","Decodetask: Parsepngimage failed");
      #endif
      goto err;
   }
#ifdef DEBUG_PLUGINS
   PngLog("decode","Decodetask: Parsepngimage succeeded, returning");
#endif
   return;

err:
#ifdef DEBUG_PLUGINS
   PngLog("decode","Decodetask: ERROR path, setting Error flag");
#endif
   Updatetaskattrs(AOPNG_Error,TRUE,TAG_END);
#ifdef DEBUG_PLUGINS
   PngLog("decode","Decodetask: ERROR path done, exiting");
#endif
}

/*--------------------------------------------------------------------*/
/* Main task functions                                                */
/*--------------------------------------------------------------------*/

/* Save all our source data blocks in this AOTP_FILE object */
static void Savesource(struct Pngsource *ps,struct Aobject *file)
{  struct Datablock *db;
   for(db=ps->data.first;db->next;db=db->next)
   {  Asetattrs(file,
         AOFIL_Data,db->data,
         AOFIL_Datalength,db->length,
         TAG_END);
   }
}

/* Start the decoder task. Only start it if the screen is
 * currenty valid. */
static void Startdecoder(struct Pngsource *ps)
{  struct Screen *screen=NULL;
   if(Agetattr(Aweb(),AOAPP_Screenvalid))
   {  Agetattrs(Aweb(),
         AOAPP_Screen,&screen,
         AOAPP_Colormap,&ps->colormap,
         TAG_END);
      if(screen) ps->friendbitmap=screen->RastPort.BitMap;
      if(ps->task=Anewobject(AOTP_TASK,
         AOTSK_Entry,Decodetask,
         AOTSK_Name,"AWebPng decoder",
         AOTSK_Userdata,ps,
         AOTSK_Stacksize,128000,
         AOBJ_Target,ps,
         TAG_END))
      {  Asetattrs(ps->task,AOTSK_Start,TRUE,TAG_END);
      }
   }
}

/* Notify our related Pngcopy objects that the bitmap has gone.
 * Delete the bitmap, and release all obtained pens. */
static void Releaseimage(struct Pngsource *ps)
{  short i;
   Anotifyset(ps->source,AOPNG_Bitmap,NULL,TAG_END);
   if(ps->bitmap)
   {  FreeBitMap(ps->bitmap);
      ps->bitmap=NULL;
   }
   if(ps->mask)
   {  FreeVec(ps->mask);
      ps->mask=NULL;
   }
   for(i=0;i<256;i++)
   {  if(ps->allocated[i])
      {  ReleasePen(ps->colormap,i);
         ps->allocated[i]=FALSE;
      }
   }
   ps->colormap=NULL;
   ps->flags&=~PNGSF_READY;
   ps->memory=0;
   Asetattrs(ps->source,AOSRC_Memory,0,TAG_END);
}

/* Delete all source data.  The node and its payload share a single
 * AllocVec (see Newdatablock), so one FreeVec releases everything. */
static void Releasedata(struct Pngsource *ps)
{  struct Datablock *db;
   while(db=REMHEAD(&ps->data))
   {  FreeVec(db);
   }
}

/*--------------------------------------------------------------------*/
/* Plugin sourcedriver object dispatcher functions                    */
/*--------------------------------------------------------------------*/

static ULONG Setsource(struct Pngsource *ps,struct Amset *amset)
{  struct TagItem *tag,*tstate;
   UBYTE *arg,*p;
   Amethodas(AOTP_SOURCEDRIVER,ps,AOM_SET,amset->tags);
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            ps->source=(struct Aobject *)tag->ti_Data;
            break;
         case AOSDV_Savesource:
            Savesource(ps,(struct Aobject *)tag->ti_Data);
            break;
         case AOSDV_Displayed:
            if(tag->ti_Data)
            {  /* We are becoming displayed. Start our decoder task if
                * we have data but no bitmap yet, and task is not yet
                * running. */
               ps->flags|=PNGSF_DISPLAYED;
               if(ps->data.first->next && !ps->bitmap && !ps->task)
               {  Startdecoder(ps);
               }
            }
            else
            {  /* We are not displayed anywhere now. Leave the bitmap
                * intact so we don't need to decode it again when we
                * are becoming displayed again. */
               ps->flags&=~PNGSF_DISPLAYED;
            }
            break;
         case AOAPP_Screenvalid:
            if(tag->ti_Data)
            {  if(ps->data.first->next && (ps->flags&PNGSF_DISPLAYED)
               && !ps->task)
               {  Startdecoder(ps);
               }
            }
            else
            {  if(ps->task)
               {  Adisposeobject(ps->task);
                  ps->task=NULL;
               }
               Releaseimage(ps);
            }
            break;
         case AOSDV_Arguments:
            arg=(UBYTE *)tag->ti_Data;
            if(arg)
            {  for(p=arg;*p;p++)
               {  if(!mystrnicmp(p,"PROGRESS=",9))
                  {  ps->progress=atoi(p+9);
                     p+=9;
                  }
                  else if(!mystrnicmp(p,"GAMMA=",6))
                  {  ps->gamma=atof(p+6);
                     p+=6;
                  }
                  else if(!mystrnicmp(p,"LOWPRI",6))
                  {  ps->flags|=PNGSF_LOWPRI;
                     p+=6;
                  }
               }
            }
            break;
      }
   }
   return 0;
}

static struct Pngsource *Newsource(struct Amset *amset)
{  struct Pngsource *ps;
   if(ps=Allocobject(PluginBase->sourcedriver,sizeof(struct Pngsource),amset))
   {  InitSemaphore(&ps->sema);
      NEWLIST(&ps->data);
      Aaddchild(Aweb(),ps,AOREL_APP_USE_SCREEN);
      ps->progress=4;
      ps->gamma=2.0;
      Setsource(ps,amset);
   }
   return ps;
}

static ULONG Getsource(struct Pngsource *ps,struct Amset *amset)
{  struct TagItem *tag,*tstate;
   AmethodasA(AOTP_SOURCEDRIVER,ps,amset);
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            PUTATTR(tag,ps->source);
            break;
         case AOSDV_Saveable:
            PUTATTR(tag,(ps->flags&PNGSF_EOF)?TRUE:FALSE);
            break;
      }
   }
   return 0;
}

static ULONG Srcupdatesource(struct Pngsource *ps,struct Amsrcupdate *amsrcupdate)
{  struct TagItem *tag,*tstate;
   UBYTE *data=NULL;
   long datalength=0;
   BOOL eof=FALSE;
#ifdef DEBUG_PLUGINS
   PngLog("src","Srcupdatesource called, ps=0x%08lx, amsrcupdate=0x%08lx",(ULONG)ps,(ULONG)amsrcupdate);
#endif
   AmethodasA(AOTP_SOURCEDRIVER,ps,amsrcupdate);
   tstate=amsrcupdate->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOURL_Data:
            data=(UBYTE *)tag->ti_Data;
#ifdef DEBUG_PLUGINS
            PngLog("src","Srcupdatesource: Got AOURL_Data, data=0x%08lx",(ULONG)data);
#endif
            break;
         case AOURL_Datalength:
            datalength=tag->ti_Data;
#ifdef DEBUG_PLUGINS
            PngLog("src","Srcupdatesource: Got AOURL_Datalength, datalength=%ld",datalength);
#endif
            break;
         case AOURL_Eof:
            if(tag->ti_Data)
            {  eof=TRUE;
               ps->flags|=PNGSF_EOF;
#ifdef DEBUG_PLUGINS
               PngLog("src","Srcupdatesource: Got AOURL_Eof, setting EOF flag");
#endif
            }
            break;
      }
   }
   if(data && datalength)
   {  struct Datablock *db;
#ifdef DEBUG_PLUGINS
      PngLog("src","Srcupdatesource: Allocating datablock, datalength=%ld",datalength);
#endif
      db=Newdatablock(datalength);
      if(db)
      {  memcpy(db->data,data,datalength);
#ifdef DEBUG_PLUGINS
         PngLog("src","Srcupdatesource: Adding datablock to list, db=0x%08lx",(ULONG)db);
#endif
         ObtainSemaphore(&ps->sema);
         ADDTAIL(&ps->data,db);
         ReleaseSemaphore(&ps->sema);
#ifdef DEBUG_PLUGINS
         PngLog("src","Srcupdatesource: Datablock added");
#endif
      }
      else
      {  #ifdef DEBUG_PLUGINS
         PngLog("src","Srcupdatesource: ERROR - Failed to allocate Datablock");
         #endif
      }
      if(!ps->task)
      {  #ifdef DEBUG_PLUGINS
         PngLog("src","Srcupdatesource: Starting decoder task");
         #endif
         Startdecoder(ps);
      }
   }
   if((data && datalength) || eof)
   {  if(ps->task)
      {  #ifdef DEBUG_PLUGINS
         PngLog("src","Srcupdatesource: Signaling task with AOPNG_Data");
         #endif
         Asetattrsasync(ps->task,AOPNG_Data,TRUE,TAG_END);
      }
      else
      {  #ifdef DEBUG_PLUGINS
         PngLog("src","Srcupdatesource: WARNING - ps->task is NULL, cannot signal");
         #endif
      }
   }
#ifdef DEBUG_PLUGINS
   PngLog("src","Srcupdatesource: Returning");
#endif
   return 0;
}

static ULONG Updatesource(struct Pngsource *ps,struct Amset *amset)
{  struct TagItem *tag,*tstate;
   BOOL notify=FALSE;
   long readyfrom=0,readyto=-1;
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOPNG_Readyfrom:
            readyfrom=tag->ti_Data;
            notify=TRUE;
            break;
         case AOPNG_Readyto:
            readyto=tag->ti_Data;
            if(readyto>ps->ready) ps->ready=readyto;
            notify=TRUE;
            break;
         case AOPNG_Imgready:
            if(tag->ti_Data) ps->flags|=PNGSF_READY;
            else ps->flags&=~PNGSF_READY;
            break;
         case AOPNG_Error:
            break;
         case AOPNG_Memory:
            ps->memory+=tag->ti_Data;
            Asetattrs(ps->source,
               AOSRC_Memory,ps->width*ps->height,
               TAG_END);
      }
   }
   if(notify && ps->bitmap)
   {  Anotifyset(ps->source,
         AOPNG_Bitmap,ps->bitmap,
         AOPNG_Mask,ps->mask,
         AOPNG_Width,ps->width,
         AOPNG_Height,ps->height,
         AOPNG_Readyfrom,readyfrom,
         AOPNG_Readyto,readyto,
         AOPNG_Imgready,ps->flags&PNGSF_READY,
         TAG_END);
   }
   return 0;
}

static ULONG Addchildsource(struct Pngsource *ps,struct Amadd *amadd)
{  if(amadd->relation==AOREL_SRC_COPY)
   {  if(ps->bitmap)
      {  Asetattrs(amadd->child,
            AOPNG_Bitmap,ps->bitmap,
            AOPNG_Mask,ps->mask,
            AOPNG_Width,ps->width,
            AOPNG_Height,ps->height,
            AOPNG_Readyfrom,0,
            AOPNG_Readyto,ps->ready,
            AOPNG_Imgready,ps->flags&PNGSF_READY,
            TAG_END);
      }
   }
   return 0;
}

static void Disposesource(struct Pngsource *ps)
{  /* Set disposing flag to signal decoder task to exit */
   if(ps)
   {  ObtainSemaphore(&ps->sema);
      ps->flags|=PNGSF_DISPOSING;
      ReleaseSemaphore(&ps->sema);
   }
   if(ps->task)
   {  Adisposeobject(ps->task);
      ps->task=NULL;
   }
   Releaseimage(ps);
   Releasedata(ps);
   Aremchild(Aweb(),ps,AOREL_APP_USE_SCREEN);
   Amethodas(AOTP_SOURCEDRIVER,ps,AOM_DISPOSE,NULL);
}

__saveds __asm ULONG Dispatchsource(
   register __a0 struct Pngsource *ps,
   register __a1 struct Amessage *amsg)
{  ULONG result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(ULONG)Newsource((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setsource(ps,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getsource(ps,(struct Amset *)amsg);
         break;
      case AOM_SRCUPDATE:
         result=Srcupdatesource(ps,(struct Amsrcupdate *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatesource(ps,(struct Amset *)amsg);
         break;
      case AOM_ADDCHILD:
         result=Addchildsource(ps,(struct Amadd *)amsg);
         break;
      case AOM_DISPOSE:
         Disposesource(ps);
         break;
      default:
         AmethodasA(AOTP_SOURCEDRIVER,ps,amsg);
         break;
   }
   return result;
}

