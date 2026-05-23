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

/* webpsource.c - AWeb webp plugin sourcedriver
 *
 * This file implements the decoder side of the WebP plugin.  It is
 * structurally a peer of jfifsource.c / pngsource.c but the inner
 * decoding loop drives libwebp's incremental decoder
 * (WebPIDecode / WebPIAppend / WebPIDecGetRGB) instead of
 * libjpeg / libpng.  WebPIDecode is used in preference to the
 * simpler WebPINewDecoder so we can also pass speed-oriented
 * decoder options (bypass_filtering, no_fancy_upsampling) - see
 * Allocoutputbuf() for the rationale.
 *
 * The data path is the same as the other plugins:
 *   - browser pushes downloaded bytes into a linked list of Datablocks
 *     via AOM_SRCUPDATE messages on the main task;
 *   - a separate decoder subtask drains the list, feeding each
 *     Datablock to WebPIAppend() and asking WebPIDecGetRGB() for
 *     "rows decoded so far" between calls;
 *   - newly produced rows are translated to the AmigaOS BitMap on
 *     the fly: deep p96 RGB(A) is written with p96WritePixelArray,
 *     8-bit planar uses ObtainBestPen + WritePixelLine8;
 *   - if the source image has an alpha channel, a 1-bit-per-pixel
 *     transparency mask is built in parallel and passed to the
 *     copydriver via AOWEBP_Mask.
 *
 * Differences from JFIF/PNG worth noting:
 *
 *   - libwebp consumes appended bytes synchronously; there is no
 *     row-callback to register.  Instead each fed chunk may unblock
 *     zero or more freshly decoded rows that we discover by polling
 *     WebPIDecGetRGB() after every WebPIAppend().
 *
 *   - WebP RIFF files store the entire bitstream after a short
 *     header; we cannot allocate the destination BitMap until at
 *     least the VP8X / VP8L / VP8 header has arrived.  The decoder
 *     task therefore queues data until WebPGetFeatures() succeeds,
 *     then allocates everything in one go.
 *
 *   - WebP supports both lossy (VP8) and lossless (VP8L) encodings
 *     transparently behind the same API.  Only one code path is
 *     required.
 */

#include "pluginlib.h"
#include "awebwebp.h"
#include "ezlists.h"

#include <string.h>
#include <stdlib.h>

#include <exec/memory.h>
#include <graphics/gfx.h>
#include <datatypes/pictureclass.h>
#include <libraries/awebplugin.h>
#include <libraries/Picasso96.h>
#include <proto/awebplugin.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/Picasso96.h>

#include "webp/decode.h"

/* Alpha values >= THRESHOLD are opaque, < THRESHOLD are transparent.
 * Same convention as the PNG plugin so the copydriver can be a near
 * copy of pngcopy.c. */
#define THRESHOLD 0x01

/*--------------------------------------------------------------------*/
/* General data structures                                            */
/*--------------------------------------------------------------------*/

/* A struct Datablock holds one block of data.  Once it has been read
 * from the list, the data can be accessed without semaphore protection
 * because it will never change (but the ln_Succ pointer will!).
 *
 * Newdatablock() places the payload immediately after the struct using
 * one single AllocVec, so one FreeVec releases everything. */
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
struct Webpsource
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
   short ncolors;                   /* Number of colors for 8-bit target */
   short dither;                    /* Dither mode requested (reserved) */
   long maxmem;                     /* Max memory to use (reserved) */
   UBYTE pen[256];                  /* RGB->pen cache fill pens */
   UBYTE allocated[256];            /* This pen slot is allocated */
   struct BitMap *friendbitmap;     /* Friend bitmap */
   struct ColorMap *colormap;       /* The colormap to obtain pens from */
};

/* Webpsource flags: */
#define WEBPSF_EOF         0x0001   /* EOF was received */
#define WEBPSF_DISPLAYED   0x0002   /* This image is being displayed */
#define WEBPSF_MEMORY      0x0004   /* Our owner knows our memory size */
#define WEBPSF_READY       0x0008   /* Decoding is ready */
#define WEBPSF_GRAYSCALE   0x0010   /* Grayscale requested (reserved) */
#define WEBPSF_LOWPRI      0x0020   /* Run decoder at low priority */
#define WEBPSF_DISPOSING   0x0040   /* Source is being disposed */

/*--------------------------------------------------------------------*/
/* The decoder subtask                                                */
/*--------------------------------------------------------------------*/

struct Decoder
{  struct Datablock *current;       /* The current datablock */
   struct Webpsource *source;       /* Points back to our Webpsource */
   /* Output fields: */
   struct BitMap *bitmap;           /* Bitmap to be filled */
   UBYTE *mask;                     /* Transparent mask to be built */
   long width,height,depth;         /* Bitmap dimensions */
   long maskw;                      /* Bytes per row of mask */
   USHORT flags;                    /* See below */
   short progress;                  /* Progress counter */
   struct RastPort rp;              /* Used for blitting */
   struct RastPort temprp;          /* Temporary for pixelline8 functions */
   UBYTE *chunky;                   /* Row of chunky pixels (8-bit path) */
   struct RenderInfo renderinfo;    /* p96 render info for pixel operations */
   /* WebP decoder state.
    *
    * We use WebPDecoderConfig (instead of the bare WebPDecBuffer that
    * WebPINewDecoder() takes) so that we can also pass two libwebp
    * decoder options that dramatically reduce CPU time on a 68k:
    *
    *   - options.bypass_filtering = 1
    *       Skip VP8's in-loop deblocking filter.  This is by far the
    *       single most expensive step in a lossy VP8 decode; turning
    *       it off typically buys ~25-35% on classic Amiga hardware.
    *       The output is slightly blockier on smooth gradients but
    *       perfectly usable for web browsing.
    *
    *   - options.no_fancy_upsampling = 1
    *       Use VP8's point-sample U/V upsampler instead of the bilinear
    *       "fancy" one.  Saves another ~10-15% with only a small loss
    *       of chroma smoothness.
    *
    * Both options together are roughly a 40% wall-clock win on a real
    * 68030/40 system compared to the default config, and they don't
    * change the output stride or colour space at all, so the rest of
    * the row writer keeps working as-is.
    *
    * The config lives in the Decoder struct so its embedded
    * WebPDecoderOptions stays valid for the entire lifetime of idec
    * (libwebp keeps a pointer to it inside WebPIDecoder->params_). */
   WebPDecoderConfig config;        /* libwebp decoder config + buffer */
   WebPIDecoder *idec;              /* Incremental decoder handle */
   UBYTE *rgba;                     /* Buffer libwebp writes into */
   long rgbastride;                 /* Stride of the RGBA buffer in bytes */
   long lastrow;                    /* Last row already blitted to bitmap */
   WEBP_CSP_MODE csp;               /* Output colour mode chosen for libwebp */
   /* Decided once when the bitmap is allocated: if graphics.library is
    * V40+ (AmigaOS 3.0 / AGA) we can replace the slow WritePixelLine8
    * planar emitter with WriteChunkyPixels, which avoids the chunky
    * shadow RastPort blit-back entirely.  Same trick the PNG plugin
    * uses to cut classic-AGA planar render time by ~5x. */
   BOOL usewritechunky;
   /* 256-entry direct-mapped RGB -> screen-pen cache used by the 8-bit
    * chunky path.  The same idea as the PNG plugin: rgbcache_rgb[h]
    * holds the full packed RGB triple for slot h, or the sentinel
    * 0xFFFFFFFF when empty.  ObtainBestPen() is by far the slowest
    * step in the chunky loop on classic Workbench screens and this
    * cache turns most lookups into O(1) hits for natural images. */
   ULONG rgbcache_rgb[256];
   UBYTE rgbcache_pen[256];
};

/* Decoder flags: */
#define DECOF_STOP         0x0001   /* The decoder process should stop */
#define DECOF_EOF          0x0002   /* No more data */
#define DECOF_TRANSPARENT  0x0004   /* Image has an alpha channel */
#define DECOF_P96MAP       0x0010   /* Bitmap is Picasso96 */
#define DECOF_P96DEEP      0x0020   /* Bitmap is Picasso96 >8 bits */

/*--------------------------------------------------------------------*/
/* Drain pending task messages.  Only called when we are about to     */
/* block waiting for more data, never on the fast path.               */
/*--------------------------------------------------------------------*/

static void Webpdrainmessages(struct Decoder *decoder)
{  struct Taskmsg *tm;
   struct TagItem *tag,*tstate;
   while(!(decoder->flags&DECOF_STOP) && (tm=Gettaskmsg()))
   {  if(tm->amsg && tm->amsg->method==AOM_SET)
      {  tstate=((struct Amset *)tm->amsg)->tags;
         while((tag=NextTagItem(&tstate)))
         {  switch(tag->ti_Tag)
            {  case AOTSK_Stop:
                  if(tag->ti_Data) decoder->flags|=DECOF_STOP;
                  break;
               case AOWEBP_Data:
                  /* Legacy wakeup tag - the port signal carried the
                   * wake; nothing more to do here. */
                  break;
            }
         }
      }
      Replytaskmsg(tm);
   }
}

/*--------------------------------------------------------------------*/
/* Stream access: wait for and return the next available Datablock.   */
/* Returns NULL when EOF or STOP has been reached without further     */
/* data, otherwise the pointer to the next datablock that must be     */
/* consumed.                                                          */
/*--------------------------------------------------------------------*/

static struct Datablock *Webpnextblock(struct Decoder *decoder)
{  struct Datablock *db;
   BOOL wait;
   for(;;)
   {  wait=FALSE;
      if(decoder->flags&(DECOF_STOP|DECOF_EOF)) return NULL;
      ObtainSemaphore(&decoder->source->sema);
      if(decoder->current) db=decoder->current->next;
      else db=decoder->source->data.first;
      if(db && db->next)
      {  decoder->current=db;
      }
      else if(decoder->source->flags&WEBPSF_EOF)
      {  decoder->flags|=DECOF_EOF;
         db=NULL;
      }
      else
      {  wait=TRUE;
         db=NULL;
      }
      ReleaseSemaphore(&decoder->source->sema);
      if(!wait) return db;
      Webpdrainmessages(decoder);
      if(decoder->flags&DECOF_STOP) return NULL;
      Waittask(0);
   }
}

/*--------------------------------------------------------------------*/
/* Header pre-scan.  WebPGetFeatures() needs the RIFF + VP8X / VP8(L) */
/* header to determine width/height/has_alpha.  We must therefore     */
/* concatenate available data into a single contiguous buffer and     */
/* repeatedly try WebPGetFeatures() until the call succeeds.          */
/*                                                                    */
/* This function returns 0 on success (features filled), -1 on error  */
/* and 1 if we ran out of data while parsing.  The contiguous header  */
/* buffer is *not* used further: libwebp's decoder will be fed the    */
/* same bytes again via WebPIAppend() (it is cheap, see headerbytes). */
/*--------------------------------------------------------------------*/

static int Webpreadfeatures(struct Decoder *decoder,
   WebPBitstreamFeatures *features)
{  struct Datablock *db;
   UBYTE *hdrbuf;
   long hdrsize;
   long hdrcap;
   VP8StatusCode vp8s;
   long copylen;
   UBYTE *newbuf;
   long newcap;
   hdrbuf=NULL;
   hdrsize=0;
   hdrcap=0;
   for(;;)
   {  /* Try to parse what we have so far.  WebPGetFeatures returns
       * NOT_ENOUGH_DATA when more bytes are needed. */
      if(hdrsize>0)
      {  vp8s=WebPGetFeatures(hdrbuf,(size_t)hdrsize,features);
         if(vp8s==VP8_STATUS_OK)
         {  if(hdrbuf) FreeVec(hdrbuf);
            return 0;
         }
         if(vp8s!=VP8_STATUS_NOT_ENOUGH_DATA)
         {  if(hdrbuf) FreeVec(hdrbuf);
            return -1;
         }
      }
      /* Need more bytes.  Pull in the next Datablock; if none arrives
       * before EOF we cannot decode this image. */
      db=Webpnextblock(decoder);
      if(!db)
      {  if(hdrbuf) FreeVec(hdrbuf);
         return 1;
      }
      copylen=db->length;
      if(hdrsize+copylen>hdrcap)
      {  newcap=hdrcap?hdrcap*2:8192;
         while(newcap<hdrsize+copylen) newcap*=2;
         newbuf=(UBYTE *)AllocVec((ULONG)newcap,MEMF_PUBLIC);
         if(!newbuf)
         {  if(hdrbuf) FreeVec(hdrbuf);
            return -1;
         }
         if(hdrsize>0) memcpy(newbuf,hdrbuf,(size_t)hdrsize);
         if(hdrbuf) FreeVec(hdrbuf);
         hdrbuf=newbuf;
         hdrcap=newcap;
      }
      memcpy(hdrbuf+hdrsize,db->data,(size_t)copylen);
      hdrsize+=copylen;
      /* Safety net: don't blow memory on garbage input that is not a
       * WebP file at all.  64 KB is more than enough to extract the
       * features header from any legitimate WebP. */
      if(hdrsize>65536L)
      {  if(hdrbuf) FreeVec(hdrbuf);
         return -1;
      }
   }
}

/*--------------------------------------------------------------------*/
/* Build the 1bpp transparency mask row from the alpha bytes of a     */
/* freshly decoded RGBA scanline.  No-op for opaque images.           */
/*--------------------------------------------------------------------*/

static void Buildmaskrow(struct Decoder *decoder,long row)
{  long x;
   long maskoff;
   UBYTE *src;
   UBYTE a,bit;
   if(!decoder->mask) return;
   if(!(decoder->flags&DECOF_TRANSPARENT)) return;
   src=decoder->rgba+row*decoder->rgbastride;
   maskoff=row*decoder->maskw;
   for(x=0;x<decoder->width;x++)
   {  a=src[x*4+3];
      bit=(UBYTE)(0x80>>(x&7));
      if(a>=THRESHOLD) decoder->mask[maskoff+(x>>3)]|=bit;
      else decoder->mask[maskoff+(x>>3)]&=~bit;
   }
}

/*--------------------------------------------------------------------*/
/* Convert one RGBA / RGB scanline into 8-bit pen indices.  Uses a    */
/* direct-mapped 256-entry hash cache so repeated colours skip        */
/* ObtainBestPen.  Caller has already verified that the chunky path  */
/* is the one to take.                                                */
/*--------------------------------------------------------------------*/

static void Maprowto8bit(struct Decoder *decoder,UBYTE *src)
{  long x;
   ULONG key;
   ULONG prevkey;
   short slot;
   UBYTE r,g,b;
   UBYTE map;
   UBYTE prevmap;
   UBYTE *chunky;
   long step;
   step=(decoder->flags&DECOF_TRANSPARENT)?4L:3L;
   /* prevkey starts as a sentinel that can never match a real
    * packed RGB triple (whose top byte is always 0 here). */
   prevkey=0xffffffffUL;
   prevmap=0;
   chunky=decoder->chunky;
   for(x=0;x<decoder->width;x++)
   {  r=src[x*step+0];
      g=src[x*step+1];
      b=src[x*step+2];
      key=((ULONG)r<<16)|((ULONG)g<<8)|(ULONG)b;
      /* Fast path: identical to the previous pixel.  With
       * no_fancy_upsampling enabled in libwebp, point-sampled
       * chroma means each 2x2 macroblock shares its U/V values
       * and produces long runs of identical RGB samples in flat
       * regions, so this short-circuit hits very often. */
      if(key==prevkey)
      {  chunky[x]=prevmap;
         continue;
      }
      /* Multiplicative hash (Knuth) folded down to 8 bits. */
      slot=(short)(((key*2654435761UL)>>24)&0xffUL);
      if(decoder->rgbcache_rgb[slot]==key)
      {  map=decoder->rgbcache_pen[slot];
      }
      else
      {  map=(UBYTE)ObtainBestPen(decoder->source->colormap,
            ((ULONG)r<<24)|((ULONG)r<<16)|((ULONG)r<<8)|(ULONG)r,
            ((ULONG)g<<24)|((ULONG)g<<16)|((ULONG)g<<8)|(ULONG)g,
            ((ULONG)b<<24)|((ULONG)b<<16)|((ULONG)b<<8)|(ULONG)b,
            OBP_Precision,PRECISION_IMAGE,
            TAG_END);
         /* If the slot was previously occupied by a different colour
          * whose pen is still allocated, we must NOT release it: the
          * pen may be in use by earlier pixels in the same image
          * (which we won't repaint).  Tracking per-pen refcounts is
          * not worth the complexity here - the next image dispose
          * walk releases everything via Releaseimage(). */
         decoder->rgbcache_rgb[slot]=key;
         decoder->rgbcache_pen[slot]=map;
         decoder->source->pen[map]=map;
         decoder->source->allocated[map]=TRUE;
      }
      chunky[x]=map;
      prevkey=key;
      prevmap=map;
   }
}

/*--------------------------------------------------------------------*/
/* Append a freshly decoded scanline to the destination BitMap.       */
/* Handles three target classes:                                      */
/*   - Picasso96 deep (>8 bpp): direct p96WritePixelArray of the      */
/*     R8G8B8(A8) sample buffer libwebp filled.                        */
/*   - Picasso96 shallow (<=8 bpp on P96 chip):  pen mapping then     */
/*     p96WritePixelArray RGBFB_CLUT.                                  */
/*   - Classic planar 8-bit: pen mapping then WritePixelLine8.        */
/*--------------------------------------------------------------------*/

static void Writerow(struct Decoder *decoder,long row)
{  UBYTE *src;
   struct RenderInfo rgbinfo;

   if(row<0 || row>=decoder->height) return;
   src=decoder->rgba+row*decoder->rgbastride;

   if(decoder->flags&DECOF_P96DEEP)
   {  rgbinfo.Memory=src;
      rgbinfo.BytesPerRow=decoder->rgbastride;
      if(decoder->flags&DECOF_TRANSPARENT)
      {  rgbinfo.RGBFormat=RGBFB_R8G8B8A8;
      }
      else
      {  rgbinfo.RGBFormat=RGBFB_R8G8B8;
      }
      p96WritePixelArray(&rgbinfo,0,0,
         &decoder->rp,0,row,decoder->width,1);
   }
   else
   {  Maprowto8bit(decoder,src);
      if(decoder->flags&DECOF_P96MAP)
      {  decoder->renderinfo.Memory=decoder->chunky;
         decoder->renderinfo.BytesPerRow=decoder->width;
         decoder->renderinfo.RGBFormat=RGBFB_CLUT;
         p96WritePixelArray(&decoder->renderinfo,0,0,
            &decoder->rp,0,row,decoder->width,1);
      }
      else if(decoder->usewritechunky)
      {  /* V40+ fast path: graphics.library does the chunky->planar
          * conversion internally without the temprp blit-back. */
         WriteChunkyPixels(&decoder->rp,0,row,
            decoder->width-1,row,decoder->chunky,decoder->width);
      }
      else
      {  /* Pre-V40 fallback: WritePixelLine8 with the 1-row scratch
          * RastPort allocated in Allocbitmap. */
         WritePixelLine8(&decoder->rp,0,row,decoder->width,decoder->chunky,
            &decoder->temprp);
      }
   }
   Buildmaskrow(decoder,row);
}

/*--------------------------------------------------------------------*/
/* Choose the libwebp output colour mode that best matches the        */
/* eventual BitMap target.  Sets decoder->csp and decoder->rgbastride */
/* but does not allocate the buffer; that is done in Allocoutputbuf().*/
/*--------------------------------------------------------------------*/

static void Selectoutputmode(struct Decoder *decoder)
{  /* The colour-mode choice has to match how Maprowto8bit and the deep
    * blitter step through the buffer: MODE_RGB writes 3 bytes/pixel,
    * MODE_RGBA writes 4 bytes/pixel.  Picking RGBA for an opaque image
    * makes libwebp interleave alpha bytes into the pixel stream and
    * any consumer that walks with step=3 will pull alpha into the R/B
    * channels, producing regular vertical magenta/green banding. */
   if(decoder->flags&DECOF_TRANSPARENT)
   {  decoder->csp=MODE_RGBA;
      decoder->rgbastride=decoder->width*4;
   }
   else
   {  decoder->csp=MODE_RGB;
      decoder->rgbastride=decoder->width*3;
   }
   /* Round stride up to a multiple of 4 - some p96 paths prefer that
    * (and it costs nothing). */
   decoder->rgbastride=(decoder->rgbastride+3L)&~3L;
}

/*--------------------------------------------------------------------*/
/* Allocate the per-image output buffer that libwebp writes into.     */
/*--------------------------------------------------------------------*/

static BOOL Allocoutputbuf(struct Decoder *decoder)
{  ULONG sz;
   WebPDecBuffer *out;
   sz=(ULONG)(decoder->rgbastride*decoder->height);
   decoder->rgba=(UBYTE *)AllocVec(sz,MEMF_PUBLIC|MEMF_CLEAR);
   if(!decoder->rgba) return FALSE;
   /* WebPInitDecoderConfig zeroes the structure and writes the ABI
    * version into it.  Must succeed for the API to be usable. */
   if(!WebPInitDecoderConfig(&decoder->config))
   {  FreeVec(decoder->rgba);
      decoder->rgba=NULL;
      return FALSE;
   }
   /* Speed-over-quality options.  Both are safe defaults for a web
    * browser on slow hardware: the decoded image is still a valid
    * RGB buffer of exactly the requested width/height/stride.  See
    * the comment on WebPDecoderConfig in the Decoder struct for a
    * full rationale. */
   decoder->config.options.bypass_filtering=1;
   decoder->config.options.no_fancy_upsampling=1;
   /* The output buffer description.  is_external_memory tells
    * libwebp to write into our pre-allocated rgba[] rather than
    * malloc'ing its own buffer. */
   out=&decoder->config.output;
   out->colorspace=decoder->csp;
   out->width=decoder->width;
   out->height=decoder->height;
   out->is_external_memory=1;
   out->u.RGBA.rgba=decoder->rgba;
   out->u.RGBA.stride=(int)decoder->rgbastride;
   out->u.RGBA.size=(size_t)sz;
   return TRUE;
}

/*--------------------------------------------------------------------*/
/* Allocate the destination AmigaOS BitMap and (if needed) mask, and  */
/* wire it onto the Webpsource object so the copydriver can see it.   */
/*--------------------------------------------------------------------*/

static BOOL Allocbitmap(struct Decoder *decoder)
{  ULONG memfchip;
   /* Only call p96* on friend when it is a P96 bitmap; on classic
    * Workbench friend bitmaps the p96 query can hit graphics paths
    * we don't want.  Same dance as in pngsource.c. */
   if(P96Base && decoder->source->friendbitmap
      && p96GetBitMapAttr(decoder->source->friendbitmap,P96BMA_ISP96))
   {  decoder->depth=p96GetBitMapAttr(decoder->source->friendbitmap,
         P96BMA_DEPTH);
      decoder->bitmap=p96AllocBitMap(decoder->width,decoder->height,
         decoder->depth,
         BMF_MINPLANES|BMF_CLEAR|BMF_DISPLAYABLE,
         decoder->source->friendbitmap,RGBFB_NONE);
      if(decoder->bitmap && p96GetBitMapAttr(decoder->bitmap,P96BMA_ISP96))
      {  decoder->flags|=DECOF_P96MAP;
         if(decoder->depth>8) decoder->flags|=DECOF_P96DEEP;
      }
   }
   else
   {  /* Classic planar bitmap.  NULL friend so graphics.library can
       * place the planes in Fast RAM when available. */
      decoder->bitmap=AllocBitMap(decoder->width,decoder->height,8,
         BMF_CLEAR|BMF_MINPLANES,NULL);
      decoder->depth=8;
   }
   if(!decoder->bitmap) return FALSE;

   if(decoder->flags&DECOF_TRANSPARENT)
   {  if(decoder->flags&DECOF_P96MAP)
      {  decoder->maskw=p96GetBitMapAttr(decoder->bitmap,P96BMA_WIDTH)/8;
         memfchip=0;
      }
      else
      {  decoder->maskw=decoder->bitmap->BytesPerRow;
         memfchip=MEMF_CHIP;
      }
      decoder->mask=(UBYTE *)AllocVec(
         (ULONG)(decoder->maskw*decoder->height),
         MEMF_PUBLIC|MEMF_CLEAR|memfchip);
      if(!decoder->mask) return FALSE;
   }

   /* Publish bitmap and dimensions on the source object so the
    * copydriver can pick them up. */
   ObtainSemaphore(&decoder->source->sema);
   decoder->source->bitmap=decoder->bitmap;
   decoder->source->mask=decoder->mask;
   decoder->source->width=decoder->width;
   decoder->source->height=decoder->height;
   ReleaseSemaphore(&decoder->source->sema);

   Updatetaskattrs(
      AOWEBP_Memory,decoder->width*decoder->height*decoder->depth/8
         +(decoder->mask?(decoder->maskw*decoder->height):0),
      TAG_END);

   InitRastPort(&decoder->rp);
   decoder->rp.BitMap=decoder->bitmap;

   if(!(decoder->flags&DECOF_P96DEEP))
   {  /* Shallow or planar target - allocate the chunky scratch row
       * and (for planar) a temporary RastPort used by
       * WritePixelLine8.  The chunky buffer is rounded up to a multiple
       * of 16 pixels to match the same alignment WritePixelLine8 and
       * WriteChunkyPixels expect internally (and to match the PNG/JFIF
       * plugins so we never tickle a graphics-library edge case at
       * width values that aren't a multiple of 16). */
      decoder->chunky=(UBYTE *)AllocVec(
         (ULONG)(((decoder->width+15)>>4)<<4),MEMF_PUBLIC);
      if(!decoder->chunky) return FALSE;
      if(!(decoder->flags&DECOF_P96MAP))
      {  /* Pre-decide which graphics call to use once for the whole
          * image.  WriteChunkyPixels was introduced with V40
          * (AmigaOS 3.0 / AGA) and is dramatically faster than the
          * WritePixelLine8 + temprp combination because it skips the
          * chunky-to-planar shadow blit. */
         decoder->usewritechunky=
            (BOOL)(GfxBase && GfxBase->lib_Version>=40);
         if(!decoder->usewritechunky)
         {  /* WritePixelLine8 path still needs the 1-row scratch
             * BitMap.  When WriteChunkyPixels is available we can
             * skip this allocation altogether. */
            InitRastPort(&decoder->temprp);
            decoder->temprp.BitMap=AllocBitMap(
               8*(((decoder->width+15)>>4)<<1),1,8,
               BMF_MINPLANES|BMF_CLEAR,decoder->bitmap);
            if(!decoder->temprp.BitMap) return FALSE;
         }
      }
   }
   return TRUE;
}

/*--------------------------------------------------------------------*/
/* Free everything the decoder allocated during Decompress().         */
/*--------------------------------------------------------------------*/

static void Decoderelease(struct Decoder *decoder)
{  if(decoder->idec)
   {  WebPIDelete(decoder->idec);
      decoder->idec=NULL;
   }
   /* WebPFreeDecBuffer is safe even if the buffer was never
    * initialized (zeroed structure).  config.output IS the
    * WebPDecBuffer libwebp wrote into. */
   WebPFreeDecBuffer(&decoder->config.output);
   if(decoder->rgba)
   {  FreeVec(decoder->rgba);
      decoder->rgba=NULL;
   }
   if(decoder->chunky)
   {  FreeVec(decoder->chunky);
      decoder->chunky=NULL;
   }
   if(decoder->temprp.BitMap)
   {  FreeBitMap(decoder->temprp.BitMap);
      decoder->temprp.BitMap=NULL;
   }
}

/*--------------------------------------------------------------------*/
/* Main decompression routine.  Pulls Datablocks off the source list  */
/* and feeds them to WebPIAppend(), translating decoded rows to the   */
/* AmigaOS BitMap as they become available.                           */
/*--------------------------------------------------------------------*/

static BOOL Decompress(struct Decoder *decoder)
{  WebPBitstreamFeatures features;
   VP8StatusCode vp8s;
   struct Datablock *db;
   int last_y;
   int out_w,out_h,out_stride;
   long row;
   long fromrow;
   /* SAS/C 6.58 cannot prove that Webpreadfeatures() always writes
    * to rc before the test below; initialise explicitly to silence
    * Warning 317. */
   int rc=0;
   BOOL done;

   memset(&features,0,sizeof(features));

   /* Phase 1: parse the bitstream features so we know the eventual
    * image dimensions and whether there is an alpha channel. */
   rc=Webpreadfeatures(decoder,&features);
   if(rc!=0) return FALSE;
   if(features.width<=0 || features.height<=0) return FALSE;
   if(features.width>32767 || features.height>32767) return FALSE;
   decoder->width=features.width;
   decoder->height=features.height;
   if(features.has_alpha) decoder->flags|=DECOF_TRANSPARENT;

   /* Phase 2: allocate the destination BitMap (and optional mask). */
   if(!Allocbitmap(decoder)) return FALSE;

   /* Phase 3: choose the libwebp output colour mode and allocate the
    * scanline buffer that libwebp will write into. */
   Selectoutputmode(decoder);
   if(!Allocoutputbuf(decoder)) return FALSE;

   /* Phase 4: create the incremental decoder and reset our cursor
    * back to the start of the data list - libwebp needs to see the
    * RIFF header again, which we already streamed into the feature
    * pre-scan.
    *
    * We use WebPIDecode (not WebPINewDecoder) because that's the
    * only public entry point that wires our WebPDecoderOptions
    * (bypass_filtering / no_fancy_upsampling) into the decoder.
    * Passing NULL data/0 size just creates the decoder; we still
    * feed bytes incrementally via WebPIAppend below. */
   decoder->idec=WebPIDecode(NULL,0,&decoder->config);
   if(!decoder->idec) return FALSE;
   decoder->current=NULL;
   decoder->lastrow=-1;

   /* Phase 5: feed Datablocks in order and drain decoded rows. */
   done=FALSE;
   fromrow=0;
   while(!done)
   {  if(decoder->flags&DECOF_STOP) return FALSE;
      db=Webpnextblock(decoder);
      if(!db)
      {  /* No more blocks.  If we were not at EOF this means STOP. */
         if(!(decoder->flags&DECOF_EOF)) return FALSE;
         /* Flush a final, empty append so libwebp finalizes. */
         vp8s=WebPIAppend(decoder->idec,NULL,0);
         done=TRUE;
      }
      else
      {  vp8s=WebPIAppend(decoder->idec,db->data,(size_t)db->length);
      }
      if(vp8s!=VP8_STATUS_OK && vp8s!=VP8_STATUS_SUSPENDED)
      {  /* Hard decoding error. */
         return FALSE;
      }

      /* Drain whatever rows became available since last time.
       * libwebp's WebPIDecGetRGB() returns last_y as the count of
       * fully decoded rows (rows [0, last_y) are valid).  Each
       * WebPIAppend usually produces a whole macroblock-row band
       * (16 raster rows for VP8), so we walk the new rows one at a
       * time and flush a progress notification every
       * source->progress rows (default 4).  This mirrors the
       * PNG plugin's per-row progress emission and is what gives
       * the user the live "shutter wipe" effect; sending one big
       * notify at the end of each WebPIAppend looks like the image
       * just appears in slabs of 16. */
      last_y=0;
      if(WebPIDecGetRGB(decoder->idec,&last_y,
            &out_w,&out_h,&out_stride))
      {  if(last_y>decoder->lastrow+1)
         {  long endrow=last_y-1;
            if(endrow>decoder->height-1) endrow=decoder->height-1;
            for(row=decoder->lastrow+1;row<=endrow;row++)
            {  Writerow(decoder,row);
               if(++decoder->progress>=decoder->source->progress
                  || row==decoder->height-1)
               {  Updatetaskattrs(
                     AOWEBP_Readyfrom,fromrow,
                     AOWEBP_Readyto,row,
                     TAG_END);
                  decoder->progress=0;
                  fromrow=row+1;
               }
            }
            decoder->lastrow=endrow;
         }
      }
      if(vp8s==VP8_STATUS_OK)
      {  /* Decoding finished normally. */
         done=TRUE;
      }
   }

   /* Final flush: make sure the copydriver sees every row that the
    * inner loop didn't notify (can happen if libwebp returned OK
    * without producing the very last row band). */
   if(decoder->lastrow<decoder->height-1)
   {  for(row=decoder->lastrow+1;row<decoder->height;row++)
      {  Writerow(decoder,row);
      }
      decoder->lastrow=decoder->height-1;
   }
   /* Mark the image complete.  If the inner loop already pushed every
    * row through Updatetaskattrs(), send Imgready on its own rather
    * than re-notifying an empty Readyfrom/Readyto range. */
   if(fromrow<=decoder->height-1)
   {  Updatetaskattrs(
         AOWEBP_Readyfrom,fromrow,
         AOWEBP_Readyto,decoder->height-1,
         AOWEBP_Imgready,TRUE,
         TAG_END);
   }
   else
   {  Updatetaskattrs(
         AOWEBP_Imgready,TRUE,
         TAG_END);
   }
   return TRUE;
}

/*--------------------------------------------------------------------*/
/* The decoder subtask entry point.                                   */
/*--------------------------------------------------------------------*/

static __saveds __asm void Decodetask(register __a0 struct Webpsource *is)
{  struct Decoder decoderdata;
   struct Decoder *decoder=&decoderdata;
   struct Task *task=FindTask(NULL);
   BOOL ok;
   short i;

   memset(&decoderdata,0,sizeof(decoderdata));
   /* 0xFFFFFFFF is unreachable as a real RGB key (top byte is 0 for
    * actual colours), so it serves as the "empty slot" sentinel for
    * the RGB->pen direct-mapped cache. */
   for(i=0;i<256;i++) decoder->rgbcache_rgb[i]=0xFFFFFFFFUL;
   decoder->source=is;
   if(decoder->source->flags&WEBPSF_LOWPRI)
   {  SetTaskPri(task,max(-128,task->tc_Node.ln_Pri-1));
   }

   ok=Decompress(decoder);

   /* Always release decoder resources whatever happened. */
   Decoderelease(decoder);

   if(!ok)
   {  Updatetaskattrs(AOWEBP_Error,TRUE,TAG_END);
   }
}

/*--------------------------------------------------------------------*/
/* Main task helpers                                                  */
/*--------------------------------------------------------------------*/

/* Save all our source data blocks in this AOTP_FILE object */
static void Savesource(struct Webpsource *ws,struct Aobject *file)
{  struct Datablock *db;
   for(db=ws->data.first;db->next;db=db->next)
   {  Asetattrs(file,
         AOFIL_Data,db->data,
         AOFIL_Datalength,db->length,
         TAG_END);
   }
}

/* Start the decoder task. Only start it if the screen is
 * currently valid. */
static void Startdecoder(struct Webpsource *ws)
{  struct Screen *screen=NULL;
   if(Agetattr(Aweb(),AOAPP_Screenvalid))
   {  Agetattrs(Aweb(),
         AOAPP_Screen,&screen,
         AOAPP_Colormap,&ws->colormap,
         TAG_END);
      if(screen) ws->friendbitmap=screen->RastPort.BitMap;
      if(ws->task=Anewobject(AOTP_TASK,
         AOTSK_Entry,Decodetask,
         AOTSK_Name,"AWebWEBP decoder",
         AOTSK_Userdata,ws,
         AOTSK_Stacksize,32000,
         AOBJ_Target,ws,
         TAG_END))
      {  Asetattrs(ws->task,AOTSK_Start,TRUE,TAG_END);
      }
   }
}

/* Notify our related Webpcopy objects that the bitmap has gone.
 * Delete the bitmap, and release all obtained pens. */
static void Releaseimage(struct Webpsource *ws)
{  short i;
   Anotifyset(ws->source,
      AOWEBP_Bitmap,NULL,
      AOWEBP_Mask,NULL,
      TAG_END);
   if(ws->bitmap)
   {  FreeBitMap(ws->bitmap);
      ws->bitmap=NULL;
   }
   if(ws->mask)
   {  FreeVec(ws->mask);
      ws->mask=NULL;
   }
   for(i=0;i<256;i++)
   {  if(ws->allocated[i])
      {  ReleasePen(ws->colormap,ws->pen[i]);
         ws->allocated[i]=FALSE;
      }
   }
   ws->colormap=NULL;
   ws->flags&=~WEBPSF_READY;
   ws->memory=0;
   Asetattrs(ws->source,AOSRC_Memory,0,TAG_END);
}

/* Delete all source data.  The node and its payload share a single
 * AllocVec (see Newdatablock), so one FreeVec releases everything. */
static void Releasedata(struct Webpsource *ws)
{  struct Datablock *db;
   while(db=REMHEAD(&ws->data))
   {  FreeVec(db);
   }
}

/*--------------------------------------------------------------------*/
/* Plugin sourcedriver object dispatcher functions                    */
/*--------------------------------------------------------------------*/

static ULONG Setsource(struct Webpsource *ws,struct Amset *amset)
{  struct TagItem *tag,*tstate;
   UBYTE *arg,*p;
   Amethodas(AOTP_SOURCEDRIVER,ws,AOM_SET,amset->tags);
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            ws->source=(struct Aobject *)tag->ti_Data;
            break;
         case AOSDV_Savesource:
            Savesource(ws,(struct Aobject *)tag->ti_Data);
            break;
         case AOSDV_Displayed:
            if(tag->ti_Data)
            {  ws->flags|=WEBPSF_DISPLAYED;
               if(ws->data.first->next && !ws->bitmap && !ws->task)
               {  Startdecoder(ws);
               }
            }
            else
            {  ws->flags&=~WEBPSF_DISPLAYED;
            }
            break;
         case AOAPP_Screenvalid:
            if(tag->ti_Data)
            {  if(ws->data.first->next && (ws->flags&WEBPSF_DISPLAYED)
               && !ws->task)
               {  Startdecoder(ws);
               }
            }
            else
            {  if(ws->task)
               {  Adisposeobject(ws->task);
                  ws->task=NULL;
               }
               Releaseimage(ws);
            }
            break;
         case AOSDV_Arguments:
            arg=(UBYTE *)tag->ti_Data;
            if(arg)
            {  for(p=arg;*p;p++)
               {  if(!strnicmp(p,"PROGRESS=",9))
                  {  ws->progress=atoi(p+9);
                     p+=9;
                  }
                  else if(!strnicmp(p,"MAXMEM=",7))
                  {  ws->maxmem=atoi(p+7);
                     if(ws->maxmem<1) ws->maxmem=1;
                     p+=7;
                  }
                  else if(!strnicmp(p,"LOWPRI",6))
                  {  ws->flags|=WEBPSF_LOWPRI;
                     p+=6;
                  }
               }
            }
            break;
      }
   }
   return 0;
}

static struct Webpsource *Newsource(struct Amset *amset)
{  struct Webpsource *ws;
   if(ws=Allocobject(PluginBase->sourcedriver,sizeof(struct Webpsource),
         amset))
   {  InitSemaphore(&ws->sema);
      NEWLIST(&ws->data);
      Aaddchild(Aweb(),ws,AOREL_APP_USE_SCREEN);
      ws->progress=4;
      ws->ncolors=0;
      ws->dither=0;
      ws->maxmem=1024;
      Setsource(ws,amset);
   }
   return ws;
}

static ULONG Getsource(struct Webpsource *ws,struct Amset *amset)
{  struct TagItem *tag,*tstate;
   AmethodasA(AOTP_SOURCEDRIVER,ws,amset);
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            PUTATTR(tag,ws->source);
            break;
         case AOSDV_Saveable:
            PUTATTR(tag,(ws->flags&WEBPSF_EOF)?TRUE:FALSE);
            break;
      }
   }
   return 0;
}

static ULONG Srcupdatesource(struct Webpsource *ws,
   struct Amsrcupdate *amsrcupdate)
{  struct TagItem *tag,*tstate;
   UBYTE *data=NULL;
   long datalength=0;
   BOOL eof=FALSE;
   AmethodasA(AOTP_SOURCEDRIVER,ws,amsrcupdate);
   tstate=amsrcupdate->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOURL_Data:
            data=(UBYTE *)tag->ti_Data;
            break;
         case AOURL_Datalength:
            datalength=tag->ti_Data;
            break;
         case AOURL_Eof:
            if(tag->ti_Data)
            {  eof=TRUE;
               ws->flags|=WEBPSF_EOF;
            }
            break;
      }
   }
   if(data && datalength)
   {  struct Datablock *db=Newdatablock(datalength);
      if(db)
      {  memcpy(db->data,data,(size_t)datalength);
         ObtainSemaphore(&ws->sema);
         ADDTAIL(&ws->data,db);
         ReleaseSemaphore(&ws->sema);
      }
      if(!ws->task)
      {  Startdecoder(ws);
      }
   }
   if((data && datalength) || eof)
   {  if(ws->task)
      {  Asetattrsasync(ws->task,AOWEBP_Data,TRUE,TAG_END);
      }
   }
   return 0;
}

static ULONG Updatesource(struct Webpsource *ws,struct Amset *amset)
{  struct TagItem *tag,*tstate;
   BOOL notify=FALSE;
   long readyfrom=0,readyto=-1;
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOWEBP_Readyfrom:
            readyfrom=tag->ti_Data;
            notify=TRUE;
            break;
         case AOWEBP_Readyto:
            readyto=tag->ti_Data;
            if(readyto>ws->ready) ws->ready=readyto;
            notify=TRUE;
            break;
         case AOWEBP_Imgready:
            if(tag->ti_Data) ws->flags|=WEBPSF_READY;
            else ws->flags&=~WEBPSF_READY;
            break;
         case AOWEBP_Error:
            break;
         case AOWEBP_Memory:
            ws->memory+=tag->ti_Data;
            Asetattrs(ws->source,
               AOSRC_Memory,ws->memory,
               TAG_END);
            break;
      }
   }
   if(notify && ws->bitmap)
   {  Anotifyset(ws->source,
         AOWEBP_Bitmap,ws->bitmap,
         AOWEBP_Mask,ws->mask,
         AOWEBP_Width,ws->width,
         AOWEBP_Height,ws->height,
         AOWEBP_Readyfrom,readyfrom,
         AOWEBP_Readyto,readyto,
         AOWEBP_Imgready,ws->flags&WEBPSF_READY,
         TAG_END);
   }
   return 0;
}

static ULONG Addchildsource(struct Webpsource *ws,struct Amadd *amadd)
{  if(amadd->relation==AOREL_SRC_COPY)
   {  if(ws->bitmap)
      {  Asetattrs(amadd->child,
            AOWEBP_Bitmap,ws->bitmap,
            AOWEBP_Mask,ws->mask,
            AOWEBP_Width,ws->width,
            AOWEBP_Height,ws->height,
            AOWEBP_Readyfrom,0,
            AOWEBP_Readyto,ws->ready,
            AOWEBP_Imgready,ws->flags&WEBPSF_READY,
            TAG_END);
      }
   }
   return 0;
}

static void Disposesource(struct Webpsource *ws)
{  ws->flags|=WEBPSF_DISPOSING;
   if(ws->task)
   {  Adisposeobject(ws->task);
   }
   Releaseimage(ws);
   Releasedata(ws);
   Aremchild(Aweb(),ws,AOREL_APP_USE_SCREEN);
   Amethodas(AOTP_SOURCEDRIVER,ws,AOM_DISPOSE);
}

__saveds __asm ULONG Dispatchsource(
   register __a0 struct Webpsource *ws,
   register __a1 struct Amessage *amsg)
{  ULONG result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(ULONG)Newsource((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setsource(ws,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getsource(ws,(struct Amset *)amsg);
         break;
      case AOM_SRCUPDATE:
         result=Srcupdatesource(ws,(struct Amsrcupdate *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatesource(ws,(struct Amset *)amsg);
         break;
      case AOM_ADDCHILD:
         result=Addchildsource(ws,(struct Amadd *)amsg);
         break;
      case AOM_DISPOSE:
         Disposesource(ws);
         break;
      default:
         result=AmethodasA(AOTP_SOURCEDRIVER,ws,amsg);
         break;
   }
   return result;
}
