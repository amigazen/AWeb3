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

/* gifsource.c - AWeb gif plugin sourcedriver */

#include "pluginlib.h"
#include "awebgif.h"
/* max() macro is defined in awebgif.h */
#include <string.h>
#include <stdlib.h>
#include "ezlists.h"
#include <libraries/awebplugin.h>
#include <libraries/Picasso96.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <intuition/intuitionbase.h>
#include <datatypes/pictureclass.h>
#include <proto/awebplugin.h>
#include <proto/Picasso96.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <proto/dos.h>

/* External library base */
extern struct Library *P96Base;
extern struct Library *AwebPluginBase;

/* Workaround for missing AOSDV_Displayed, not part of plugin API */
#define AOSRC_Displayed    (AOSRC_Dummy+2)

/*--------------------------------------------------------------------*/
/* General data structures                                            */
/*--------------------------------------------------------------------*/

/* A struct Datablock holds one block of data. Once it has been read
 * from the list, the data can be accessed without semaphore protection
 * because it will never change (but the ln_Succ pointer will!). */
struct Datablock
{  NODE(Datablock);
   UBYTE *data;                     /* The data */
   long length;                     /* Length of the data */
};

/* The image, or one frame of an animation */
struct Gifimage
{  NODE(Gifimage);
   struct BitMap *bitmap;           /* Bitmap for this image */
   UBYTE *mask;                     /* Transparent mask for this image */
   short delay;                     /* Delay in 0.01 seconds */
   BOOL ready;                      /* This image is ready */
};

/* The object instance data for the source driver */
struct Gifsource
{  struct Sourcedriver sourcedriver;/* Or superclass object instance */
   struct Aobject *source;          /* The AOTP_SOURCE object we belong to */
   struct Aobject *task;            /* Our decoder task */
   LIST(Gifimage) images;           /* All our images */
   struct Gifimage *current;        /* Our current playing image */
   long width,height;               /* Bitmap dimensions */
   long ready;                      /* Last complete row in bitmap */
   long memory;                     /* Memory in use for bitmap,mask */
   LIST(Datablock) data;            /* A linked list of data blocks */
   struct SignalSemaphore sema;     /* Protect the common data */
   USHORT flags;                    /* See below */
   short progress;                  /* Progressive display mode */
   ULONG globalcolorrgb[256];       /* Global color table, left aligned RRGGBB00 values. */
   short allocated[256];            /* Number of times this pen is allocated. */
   struct BitMap *friendbitmap;     /* Friend bitmap */
   struct ColorMap *colormap;       /* The colormap to obtain pens from */
   struct Aobject *timer;           /* Animation timer */
   long maxloops;                   /* Max number of loops to show */
};

/* Gifsource flags: */
#define GIFSF_EOF          0x0001   /* EOF was received */
#define GIFSF_DISPLAYED    0x0002   /* This image is being displayed */
#define GIFSF_MEMORY       0x0004   /* Our owner knows our memory size */
#define GIFSF_IMAGEREADY   0x0010   /* Single image gif is ready */
#define GIFSF_ANIMREADY    0x0020   /* Animated gif is ready */
#define GIFSF_NOANIMATE    0x0040   /* Animation prohibited */
#define GIFSF_TIMERWAIT    0x0080   /* Timer is running */
#define GIFSF_LOWPRI       0x0100   /* Run decoder at low pri */

/*--------------------------------------------------------------------*/
/* The decoder subtask                                                */
/*--------------------------------------------------------------------*/

/* This structure holds vital data for the decoding subprocess */
struct Decoder
{  struct Datablock *current;       /* The current datablock */
   long currentbyte;                /* The current byte in the current datablock */
   struct Gifsource *source;        /* Points back to our Gifsource */
   /* LZW decompress fields: */
   long remaining;                  /* nr of bytes remaining in current data block */
   short rembits;                   /* nr of bits remaining in last char */
   UBYTE lastchar;                  /* last character partially read */
   short clearcode;                 /* code to clear strings */
   short eofcode;                   /* code for eof */
   short startcodesize;             /* initial nr of bits in code */
   short codesize;                  /* current nr of bits in code */
   short nextcode;                  /* next free code index */
   UBYTE outstack[4096];            /* stacked output bytes */
   UBYTE suffix[4096];              /* last byte per code */
   short prefix[4096];              /* prefix codes per code */
   short outsp;                     /* stack pointer in outstack, if >0 then data is available */
   short lastcode;                  /* last code read */
   UBYTE lastgifbyte;               /* last decoded byte */
   /* Output fields: */
   struct BitMap *bitmap;           /* Bitmap to be filled */
   UBYTE *mask;                     /* Transparent mask to be built */
   long width,height,depth;         /* Bitmap dimensions */
   long loops;                      /* Number of loops to play */
   long ileft,itop;                 /* Image position within bitmap */
   long iwidth,iheight;             /* Image dimensions */
   short disposal;                  /* GIF disposal method */
   long maskw;                      /* Width of transparent mask */
   long chunkyw;                    /* Width of chunky pixel array in bytes */
   USHORT flags;                    /* See below */
   short progress;                  /* Progress counter */
   UBYTE background;                /* Background color */
   UBYTE transpcolor;               /* Transparent color if transparent */
   ULONG colorrgb[256];             /* Left aligned RRGGBB00 values. */
   UBYTE pen[256];                  /* Mapped to these pens */
   UBYTE validpen[256];             /* This pen is valid */
   struct RastPort rp;              /* Temporary for pixelline8 functions */
   struct RastPort temprp;          /* Temporary for pixelline8 functions */
   UBYTE *chunky;                   /* Row of chunky pixels */
   struct RastPort saverp;          /* Accumulating save of previous frames */
   UBYTE *savemask;                 /* Accumulating save of transparent mask */
   struct RenderInfo renderinfo;    /* P96 render info for RGB mode */
};

/* Decoder flags: */
#define DECOF_STOP         0x0001   /* The decoder process should stop */
#define DECOF_EOF          0x0002   /* No more data */
#define DECOF_TRANSPARENT  0x0004   /* Transparent gif */
#define DECOF_INTERLACED   0x0008   /* Interlaced gif */
#define DECOF_CYBERMAP     0x0010   /* Bitmap is P96 */
#define DECOF_CYBERDEEP    0x0020   /* Bitmap is P96 >8 bits */
#define DECOF_ANIMFRAME    0x0040   /* This is a new animation frame */
#define DECOF_SAVEPREVIOUS 0x0080   /* Save the previous image's bitmap */

/*--------------------------------------------------------------------*/
/* Read from the input stream                                         */
/*--------------------------------------------------------------------*/

/* Read the next byte. First check any waiting messages, if the task
 * should stop then set the DECOF_STOP flag.
 * If end of current block reached, skip to next block. If no more
 * blocks available, wait until a new block is added or eof is
 * reached. */
static UBYTE Readbyte(struct Decoder *decoder)
{  struct Taskmsg *tm;
   struct TagItem *tag,*tstate;
   struct Datablock *db;
   BOOL wait;
   UBYTE retval=0;
   static int readbyte_count=0;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase && (readbyte_count++ % 100 == 0))
   {  Aprintf("GIF: Readbyte called, decoder=0x%08lx, count=%d\n", (ULONG)decoder, readbyte_count);
   }
#endif
   if(!decoder)
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Readbyte: ERROR - decoder is NULL!\n");
      }
      #endif
      return 0;
   }
   if(!decoder->source)
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Readbyte: ERROR - decoder->source is NULL!\n");
      }
      #endif
      return 0;
   }
   for(;;)
   {  wait=FALSE;
      while(!(decoder->flags&DECOF_STOP) && (tm=Gettaskmsg()))
      {  if(tm->amsg && tm->amsg->method==AOM_SET)
         {  tstate=((struct Amset *)tm->amsg)->tags;
            while((tag=NextTagItem(&tstate)))
            {  switch(tag->ti_Tag)
               {  case AOTSK_Stop:
                     if(tag->ti_Data) decoder->flags|=DECOF_STOP;
                     break;
                  case AOGIF_Data:
                     /* Ignore these now */
                     break;
               }
            }
         }
         Replytaskmsg(tm);
      }
      /* Only continue if we shouldn't stop */
      if(decoder->flags&DECOF_STOP) break;
      if(decoder->current)
      {  if(++decoder->currentbyte>=decoder->current->length)
         {  /* End of block reached */
            ObtainSemaphore(&decoder->source->sema);
            db=decoder->current->next;
            if(db && db->next)
            {  decoder->current=db;
               decoder->currentbyte=0;
            }
            else if(decoder->source->flags&GIFSF_EOF)
            {  decoder->flags|=DECOF_EOF;
            }
            else
            {  /* No more blocks; wait for next block */
               wait=TRUE;
            }
            ReleaseSemaphore(&decoder->source->sema);
         }
      }
      else
      {  /* No current block yet */
         if(!decoder->source)
         {  #ifdef DEBUG_PLUGINS
            if(AwebPluginBase)
            {  Aprintf("GIF: Readbyte: ERROR - decoder->source is NULL in else branch!\n");
            }
            #endif
            decoder->flags|=DECOF_EOF;
            return 0;
         }
         ObtainSemaphore(&decoder->source->sema);
         db=decoder->source->data.first;
         if(db && db->next)
         {  decoder->current=db;
            decoder->currentbyte=0;
#ifdef DEBUG_PLUGINS
            if(AwebPluginBase && readbyte_count <= 10)
            {  Aprintf("GIF: Readbyte: Got first block, current=0x%08lx, length=%ld\n", (ULONG)decoder->current, decoder->current ? decoder->current->length : 0);
            }
#endif
         }
         else if(decoder->source->flags&GIFSF_EOF)
         {  #ifdef DEBUG_PLUGINS
            if(AwebPluginBase && readbyte_count <= 10)
            {  Aprintf("GIF: Readbyte: EOF flag set\n");
            }
            #endif
            decoder->flags|=DECOF_EOF;
         }
         else
         {  /* No block yet; wait for next block */
            #ifdef DEBUG_PLUGINS
            if(AwebPluginBase && readbyte_count <= 10)
            {  Aprintf("GIF: Readbyte: No block yet, waiting...\n");
            }
            #endif
            wait=TRUE;
         }
         ReleaseSemaphore(&decoder->source->sema);
      }
      if(!wait)
      {  if(!(decoder->flags&DECOF_EOF))
         {  if(!decoder->current)
            {  #ifdef DEBUG_PLUGINS
               if(AwebPluginBase)
               {  Aprintf("GIF: Readbyte: ERROR - current is NULL!\n");
               }
               #endif
               decoder->flags|=DECOF_EOF;
               return 0;
            }
            if(!decoder->current->data)
            {  #ifdef DEBUG_PLUGINS
               if(AwebPluginBase)
               {  Aprintf("GIF: Readbyte: ERROR - current->data is NULL!\n");
               }
               #endif
               decoder->flags|=DECOF_EOF;
               return 0;
            }
            if(decoder->currentbyte >= decoder->current->length)
            {  #ifdef DEBUG_PLUGINS
               if(AwebPluginBase)
               {  Aprintf("GIF: Readbyte: ERROR - currentbyte %ld >= length %ld!\n", decoder->currentbyte, decoder->current->length);
               }
               #endif
               decoder->flags|=DECOF_EOF;
               return 0;
            }
            if(decoder->currentbyte < 0)
            {  #ifdef DEBUG_PLUGINS
               if(AwebPluginBase)
               {  Aprintf("GIF: Readbyte: ERROR - currentbyte %ld < 0!\n", decoder->currentbyte);
               }
               #endif
               decoder->flags|=DECOF_EOF;
               return 0;
            }
            retval=decoder->current->data[decoder->currentbyte];
#ifdef DEBUG_PLUGINS
            if(AwebPluginBase && readbyte_count <= 10)
            {  Aprintf("GIF: Readbyte: Returning byte %d at offset %ld\n", retval, decoder->currentbyte);
            }
#endif
         }
         else
         {  #ifdef DEBUG_PLUGINS
            if(AwebPluginBase && readbyte_count <= 10)
            {  Aprintf("GIF: Readbyte: EOF flag set, returning 0\n");
            }
            #endif
         }
         break;
      }
#ifdef DEBUG_PLUGINS
      if(AwebPluginBase && readbyte_count <= 10)
      {  Aprintf("GIF: Readbyte: Waiting for data...\n");
      }
#endif
      Waittask(0);
   }
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase && readbyte_count <= 10)
   {  Aprintf("GIF: Readbyte: Exiting, returning %d\n", retval);
   }
#endif
   return retval;
}

/* Read or skip a number of bytes. Returns TRUE if ok, FALSE of EOF reached before
 * end of block, or task should stop. If block is passed as NULL, data is skipped. */
static BOOL Readblock(struct Decoder *decoder,UBYTE *block,long length)
{  UBYTE c;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase && length > 0)
   {  Aprintf("GIF: Readblock called, decoder=0x%08lx, length=%ld, block=0x%08lx\n", (ULONG)decoder, length, (ULONG)block);
   }
#endif
   while(length && !(decoder->flags&(DECOF_STOP|DECOF_EOF)))
   {  c=Readbyte(decoder);
      if(decoder->flags&(DECOF_STOP|DECOF_EOF))
      {  #ifdef DEBUG_PLUGINS
         if(AwebPluginBase)
         {  Aprintf("GIF: Readblock: EOF or STOP flag set, returning FALSE\n");
         }
         #endif
         return FALSE;
      }
      if(block) *block++=c;
      length--;
   }
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase && length == 0)
   {  Aprintf("GIF: Readblock: Done, read all %ld bytes\n", length);
   }
#endif
   return (BOOL)!(decoder->flags&(DECOF_STOP|DECOF_EOF));
}

/*--------------------------------------------------------------------*/
/* Read from the GIF file, decompress                                 */
/*--------------------------------------------------------------------*/

/* Read a byte from the GIF data block */
static UBYTE Readchar(struct Decoder *d)
{  UBYTE c;
   if(d->remaining==0)
   {  d->remaining=Readbyte(d);
      if(d->remaining==0)
      {  d->flags|=DECOF_EOF;
      }
   }
   c=Readbyte(d);
   d->remaining--;
   return c;
}

/* Read a code from the compressed input stream */
static int Getcode(struct Decoder *d)
{  int code;
   if(d->rembits==0)
   {  d->lastchar=Readchar(d);
      d->rembits=8;
   }
   code=d->lastchar>>(8-d->rembits);
   while(d->rembits<d->codesize)
   {  d->lastchar=Readchar(d);
      code|=d->lastchar<<d->rembits;
      d->rembits+=8;
   }
   d->rembits-=d->codesize;
   code&=(1<<d->codesize)-1;
   return code;
}

/* Get an uncompressed byte from the input stream */
static UBYTE Getgifbyte(struct Decoder *d)
{  int code,curcode;

   if(d->outsp > 0)
   {  return d->outstack[--d->outsp];
   }

   code=Getcode(d);
   if(d->flags&DECOF_EOF) return 0;

   if(code==d->eofcode)
   {  d->flags|=DECOF_EOF;
      return 0;
   }

   if(code==d->clearcode)
   {  while(code==d->clearcode)
      {  d->nextcode=d->eofcode+1;
         d->codesize=d->startcodesize;
         code=Getcode(d);
         if(d->flags&DECOF_EOF) return 0;
      }
      d->lastcode=code;
      d->lastgifbyte=code;
      return (UBYTE)code;
   }

   if(code>d->nextcode)
   {  d->flags|=DECOF_STOP;
      return 0;
   }
   if(code==d->nextcode)
   {  curcode=d->lastcode;
      /* olsen: paranoia; don't let the buffer overflow. */
      if(d->outsp < sizeof(d->outstack))
         d->outstack[d->outsp++]=d->lastgifbyte;
   }
   else
   {  curcode=code;
   }

   /* olsen: paranoia; keep the index in bounds. */
   while(curcode>d->eofcode && curcode < sizeof(d->suffix))
   {
      /* olsen: paranoia; don't let the buffer overflow. */
      if(d->outsp < sizeof(d->outstack))
         d->outstack[d->outsp++]=d->suffix[curcode];
      curcode=d->prefix[curcode];
   }
   d->lastgifbyte=curcode;
   if(d->nextcode<4096)
   {  d->prefix[d->nextcode]=d->lastcode;
      d->suffix[d->nextcode]=d->lastgifbyte;
      d->nextcode++;
      if(d->nextcode>=(1<<d->codesize) && d->codesize<12)
      {  d->codesize++;
      }
   }
   d->lastcode=code;
   return d->lastgifbyte;
}

static void Setcodesize(struct Decoder *d,int codesize)
{  d->codesize=d->startcodesize=codesize+1;
   d->clearcode=1<<codesize;
   d->eofcode=d->clearcode+1;
   d->nextcode=d->eofcode+1;
   d->rembits=0;
   d->outsp=0;
   d->remaining=0;  /* Initialize remaining bytes counter */
}

/*--------------------------------------------------------------------*/
/* Parse the GIF file and build bitmap                                */
/*--------------------------------------------------------------------*/

/* Convert little to big endian, give UBYTE * as argument */
#define BIGEND(p) (((p)[1]<<8)|((p)[0]))

#define GIFHDR_ID          0
#define GIFHDR_SIZEOF      6

#define GIFLSD_WIDTH       0
#define GIFLSD_HEIGHT      2
#define GIFLSD_PACKED      4
#define GIFLSD_BACKGROUND  5
#define GIFLSD_ASPECT      6
#define GIFLSD_SIZEOF      7

#define GIFID_LEFT         0
#define GIFID_TOP          2
#define GIFID_WIDTH        4
#define GIFID_HEIGHT       6
#define GIFID_PACKED       8
#define GIFID_SIZEOF       9

#define GIFGCE_BLOCKSIZE   0
#define GIFGCE_PACKED      1
#define GIFGCE_DELAYTIME   2
#define GIFGCE_TRANSPX     4
#define GIFGCE_SIZEOF      6

#define RGB(x) (((x)<<24)|((x)<<16)|((x)<<8)|(x))

/* Skip data blocks */
static BOOL Skipgifdata(struct Decoder *decoder)
{  UBYTE c;
   for(;;)
   {  c=Readbyte(decoder);
      if(decoder->flags&(DECOF_STOP|DECOF_EOF)) return FALSE;
      if(c==0) break;
      if(!Readblock(decoder,NULL,c)) return FALSE;
   }
   return TRUE;
}

/* Read general data */
static BOOL Parsegifgeneral(struct Decoder *decoder)
{  UBYTE buffer[20];
   int gctsize,i;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifgeneral called, decoder=0x%08lx\n", (ULONG)decoder);
   }
#endif
   /* Check signature */
   if(!Readblock(decoder,buffer,GIFHDR_SIZEOF))
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifgeneral: Readblock failed for header\n");
      }
      #endif
      return FALSE;
   }
   if(strncmp(buffer,"GIF87a",6) && strncmp(buffer,"GIF89a",6))
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifgeneral: Invalid GIF signature\n");
      }
      #endif
      return FALSE;
   }

   /* Logical screen descriptor */
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifgeneral: Reading logical screen descriptor\n");
   }
#endif
   if(!Readblock(decoder,buffer,GIFLSD_SIZEOF))
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifgeneral: Readblock failed for logical screen descriptor\n");
      }
      #endif
      return FALSE;
   }
   decoder->width=BIGEND(buffer+GIFLSD_WIDTH);

   /* olsen: due to how the decoder works, a picture cannot be
    *        wider than 4096 pixels.
    */
   if(decoder->width >= 4096)
      return(FALSE);

   decoder->height=BIGEND(buffer+GIFLSD_HEIGHT);

   /* olsen: more paranoia; the size of the picture should
    *        be sensible.
    */
   if(decoder->width <= 0 || decoder->height <= 0)
      return(FALSE);

   decoder->background=buffer[GIFLSD_BACKGROUND];
   decoder->source->width=decoder->width;
   decoder->source->height=decoder->height;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifgeneral: width=%ld, height=%ld\n", decoder->width, decoder->height);
   }
#endif
   gctsize=1<<((buffer[GIFLSD_PACKED]&0x07)+1);
   if(buffer[GIFLSD_PACKED]&0x80)
   {  /* Global color table. */
      #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifgeneral: Reading global color table, size=%d\n", gctsize);
      }
      #endif
      buffer[3]=0;
      for(i=0;i<gctsize;i++)
      {  if(!Readblock(decoder,buffer,3))
         {  #ifdef DEBUG_PLUGINS
            if(AwebPluginBase)
            {  Aprintf("GIF: Parsegifgeneral: Readblock failed for color table\n");
            }
            #endif
            return FALSE;
         }
         decoder->source->globalcolorrgb[i]=*(ULONG *)buffer;
      }
   }
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifgeneral: Done, returning TRUE\n");
   }
#endif
   return TRUE;
}

/* Return the display palette pen for this GIF pen */
static int Allocatepen(struct Decoder *decoder,UBYTE pen)
{  ULONG rgb;
   int map;
   if(decoder->validpen[pen])
   {  map=decoder->pen[pen];
   }
   else
   {  rgb=decoder->colorrgb[pen];
      map=ObtainBestPen(decoder->source->colormap,
         RGB((rgb>>24)&0xff),RGB((rgb>>16)&0xff),RGB((rgb>>8)&0xff),
         OBP_Precision,PRECISION_IMAGE,
         TAG_END);
      decoder->pen[pen]=map;
      decoder->validpen[pen]=TRUE;
      decoder->source->allocated[map]++;
   }
   return map;
}

/* Read the image data row by row. For each pixel, allocate color. After each row,
 * convert to planar. Than signal our main task that another row is ready. */
static BOOL Buildgifimage(struct Decoder *decoder)
{  long x,yi,y,col,row,fromrow,torow;
   int step=8,pass=0,copy=8,i,n;
   int pen;
   UBYTE *trow=NULL;
   BOOL phaseready;
   BOOL mergemask=FALSE;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Buildgifimage called, decoder=0x%08lx, bitmap=0x%08lx\n", (ULONG)decoder, (ULONG)decoder->bitmap);
   }
#endif

   if(decoder->flags&DECOF_SAVEPREVIOUS)
   {  struct Gifimage *gi=decoder->source->images.last->prev;
      if(gi->prev)
      {  if(!decoder->saverp.BitMap)
         {  decoder->saverp.BitMap=AllocBitMap(decoder->width,decoder->height,decoder->depth,
               BMF_MINPLANES|BMF_CLEAR,gi->bitmap);
         }
         if(decoder->saverp.BitMap)
         {  BltBitMap(gi->bitmap,0,0,
               decoder->saverp.BitMap,0,0,decoder->width,decoder->height,0xc0,0xff,NULL);
         }
         if(gi->mask)
         {  if(!decoder->savemask)
            {  decoder->savemask=(UBYTE *)AllocVec(decoder->maskw*decoder->height,
                  MEMF_PUBLIC|(decoder->flags&DECOF_CYBERMAP?0:MEMF_PUBLIC));
            }
            if(decoder->savemask)
            {  memcpy(decoder->savemask,gi->mask,decoder->maskw*decoder->height); /* olsen: replaced CopyMem() with memcpy */
            }
         }
      }
      decoder->flags&=~DECOF_SAVEPREVIOUS;
   }

   /* If the image is smaller than the bitmap, or transparent, initialize it first.
    * If there is a save bitmap, copy that one, else fill with background. If there is
    * a mask but not a save mask and there was a saved bitmap, set mask to all 1 to
    * allow previous image to be rendered. */
   if(decoder->iwidth<decoder->width || decoder->iheight<decoder->height
   || decoder->mask)
   {  if(decoder->saverp.BitMap)
      {  BltBitMap(decoder->saverp.BitMap,0,0,
            decoder->bitmap,0,0,decoder->width,decoder->height,0xc0,0xff,NULL);
         if(decoder->mask)
         {  if(decoder->savemask)
            {  memcpy(decoder->mask,decoder->savemask,decoder->maskw*decoder->height); /* olsen: replaced CopyMem() with memcpy */
            }
            else
            {  memset(decoder->mask,0xff,decoder->maskw*decoder->height);
            }
         }
         if(decoder->mask) mergemask=TRUE;
      }
      else
      {  if(decoder->flags&DECOF_CYBERDEEP)
         {  UBYTE *fillbuf;
            struct RenderInfo fillri;
            long y,x;
            ULONG bgcolor=decoder->colorrgb[decoder->background]>>8;
            if(fillbuf=(UBYTE *)AllocMem(decoder->width*3,MEMF_CLEAR))
            {  for(x=0;x<decoder->width;x++)
               {  memmove(fillbuf+3*x,(UBYTE *)(&bgcolor),3);
               }
               fillri.Memory=fillbuf;
               fillri.BytesPerRow=decoder->width*3;
               fillri.RGBFormat=RGBFB_R8G8B8;
               for(y=0;y<decoder->height;y++)
               {  p96WritePixelArray(&fillri,0,0,
                     &decoder->rp,0,y,decoder->width,1);
               }
               FreeMem(fillbuf,decoder->width*3);
            }
         }
         else
         {  SetRast(&decoder->rp,Allocatepen(decoder,decoder->background));
         }
      }
   }
   else if(decoder->iwidth==decoder->width && decoder->iheight==decoder->height)
   {  /* Complete image follows */
      decoder->source->ready=0;
   }

   y=-8;
   fromrow=0;
   decoder->progress=0;
   for(yi=0;yi<decoder->iheight;yi++)
   {  phaseready=FALSE;
      if(decoder->flags&DECOF_INTERLACED)
      {  y+=step;
         /* For very small images, one or more passes must be skipped */
         while(y>=decoder->iheight)
         {  switch(++pass)
            {  case 1:y=4;step=8;copy=4;break;
               case 2:y=2;step=4;copy=2;break;
               case 3:y=1;step=2;copy=1;break;
            }
            fromrow=y;
         }
         if(y+step>=decoder->iheight) phaseready=TRUE;
      }
      else
      {  y=yi;
      }
      row=y+decoder->itop;

      if(decoder->mask)
      {  trow=decoder->mask+row*decoder->maskw;
         if(mergemask)
         {  if(decoder->flags&DECOF_CYBERDEEP)
            {  p96ReadPixelArray(&decoder->renderinfo,0,0,
                  &decoder->rp,decoder->ileft,row,decoder->iwidth,1);
            }
            else
            {  ReadPixelLine8(&decoder->rp,decoder->ileft,row,decoder->iwidth,
                  decoder->chunky,&decoder->temprp);
            }
         }
      }
      else if(!mergemask)
      {  /* Clear chunky buffer when not merging - ensures clean data */
         if(decoder->flags&DECOF_CYBERDEEP)
         {  memset(decoder->chunky,0,decoder->iwidth*3);
         }
         else
         {  memset(decoder->chunky,0,decoder->iwidth);
         }
      }
      for(x=0;x<decoder->iwidth;x++)
      {  col=x+decoder->ileft;
         pen=Getgifbyte(decoder);
         if(decoder->flags&(DECOF_STOP|DECOF_EOF))
         {  return FALSE;
         }
/* Always store the colour, in case a subsequent image isn't transparent
 * any more but requests this bitmap as its base.
 * But don't store the colour if (mergemask), because the previous colour
 * must be kept then.
*/
         if(decoder->mask && pen!=decoder->transpcolor)
         {  trow[col>>3]|=0x80>>(col&0x7);
         }
         if(!mergemask || pen!=decoder->transpcolor)
         {  if(decoder->flags&DECOF_CYBERDEEP)
            {  memmove(decoder->chunky+3*x,(UBYTE *)(&decoder->colorrgb[pen]),3);
            }
            else
            {  decoder->chunky[x]=Allocatepen(decoder,pen);
            }
         }
      }
      if(decoder->flags&DECOF_CYBERDEEP)
      {  p96WritePixelArray(&decoder->renderinfo,0,0,
            &decoder->rp,decoder->ileft,row,decoder->iwidth,1);
      }
      else
      {  WritePixelLine8(&decoder->rp,decoder->ileft,row,decoder->iwidth,
            decoder->chunky,&decoder->temprp);
      }

      torow=row;
      if((decoder->flags&DECOF_INTERLACED) && !(decoder->flags&DECOF_TRANSPARENT))
      {  for(i=1;i<copy && row+i<decoder->iheight;i*=2)
         {  n=i;
            if(row+i+n>=decoder->iheight) n=decoder->iheight-1-row-i;
            if(n)
            {  BltBitMap(decoder->bitmap,decoder->ileft,row,
                  decoder->bitmap,decoder->ileft,row+i,decoder->iwidth,n,0xc0,0xff,NULL);
            }
         }
         torow=row+copy-1;
         if(torow>=decoder->iheight) torow=decoder->iheight-1;
      }

      if(++decoder->progress==decoder->source->progress
      || yi==decoder->iheight-1 || (phaseready && decoder->source->progress))
      {  Updatetaskattrs(
            AOGIF_Readyfrom,fromrow,
            AOGIF_Readyto,torow,
            AOGIF_Imgready,yi==decoder->iheight-1,
            AOGIF_Animframe,(decoder->flags&DECOF_ANIMFRAME)!=0,
            TAG_END);
         decoder->progress=0;
         decoder->flags&=~DECOF_ANIMFRAME;
         fromrow=torow+1;
      }
   }

   /* Process the disposal method */
   switch(decoder->disposal)
   {  case 0:  /* no special action, but some GIFs expect image to stay around. */
      case 1:  /* don't dispose */
         /* Instead of saving the bitmap here, defer until we know that there will be
          * a second image. We don't want to do this extra work for a single
          * image non-animating GIF. */
         decoder->flags|=DECOF_SAVEPREVIOUS;
         break;
      case 2:  /* restore to background */
         if(decoder->saverp.BitMap)
         {  if(decoder->flags&DECOF_CYBERDEEP)
            {  UBYTE *fillbuf;
               struct RenderInfo fillri;
               long y,x;
               ULONG bgcolor=decoder->colorrgb[decoder->background]>>8;
               if(fillbuf=(UBYTE *)AllocMem(decoder->iwidth*3,MEMF_CLEAR))
               {  for(x=0;x<decoder->iwidth;x++)
                  {  memmove(fillbuf+3*x,(UBYTE *)(&bgcolor),3);
                  }
                  fillri.Memory=fillbuf;
                  fillri.BytesPerRow=decoder->iwidth*3;
                  fillri.RGBFormat=RGBFB_R8G8B8;
                  for(y=0;y<decoder->iheight;y++)
                  {  p96WritePixelArray(&fillri,0,0,
                        &decoder->saverp,decoder->ileft,decoder->itop+y,decoder->iwidth,1);
                  }
                  FreeMem(fillbuf,decoder->iwidth*3);
               }
            }
            else
            {  SetAPen(&decoder->saverp,Allocatepen(decoder,decoder->background));
               RectFill(&decoder->saverp,decoder->ileft,decoder->itop,
                  decoder->ileft+decoder->iwidth-1,decoder->itop+decoder->iheight-1);
            }
         }
         if(decoder->mask)
         {  /*** .... ***/
         }
         break;
      case 3:  /* restore previous */
         break;
   }

#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Buildgifimage: Done, returning TRUE\n");
   }
#endif
   return TRUE;
}

/* Read image parameters and data */
static BOOL Parsegifimage(struct Decoder *decoder)
{  UBYTE buffer[128];
   UBYTE c;
   int lctsize,i,delay=100;
   struct Gifimage *gi;
   BOOL error=FALSE;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifimage called, decoder=0x%08lx\n", (ULONG)decoder);
   }
#endif
   decoder->flags&=~(DECOF_TRANSPARENT|DECOF_INTERLACED|DECOF_CYBERMAP|DECOF_CYBERDEEP);
   decoder->disposal=0;
   for(;;)
   {  c=Readbyte(decoder);
      if(decoder->flags&(DECOF_STOP|DECOF_EOF)) return FALSE;
      if(c==0x2c) break;
      if(c==0x21)
      {  c=Readbyte(decoder);
         if(decoder->flags&(DECOF_STOP|DECOF_EOF)) return FALSE;
         switch(c)
         {  case 0xf9:  /* Graphics control extension */
               if(!Readblock(decoder,buffer,GIFGCE_SIZEOF)) return FALSE;
               if(buffer[GIFGCE_PACKED]&0x01)
               {  decoder->flags|=DECOF_TRANSPARENT;
                  decoder->transpcolor=buffer[GIFGCE_TRANSPX];
               }
               decoder->disposal=(buffer[GIFGCE_PACKED]>>2)&0x07;
               delay=BIGEND(buffer+GIFGCE_DELAYTIME);
               break;
/* olsen: really disable this section. */
#if 0
/*
 Doesn't really work... if the image already exists, and a new copy is
 created, the animation starts somewhere in the middle and only a half
 loop is played.
*/
            case 0xff:  /* Application extension */
               c=Readbyte(decoder);
               if(decoder->flags&(DECOF_STOP|DECOF_EOF)) return FALSE;
               if(c)
               {  if(!Readblock(decoder,buffer,c)) return FALSE;
                  if(c==11
                  && (!strncmp(buffer,"NETSCAPE2.0",11) || !strncmp(buffer,"ANIMEXTS1.0",11)))
                  {  c=Readbyte(decoder);
                     while(c)
                     {  if(decoder->flags&(DECOF_STOP|DECOF_EOF)) return FALSE;
                        if(!Readblock(decoder,buffer,c)) return FALSE;
                        if(buffer[0]==0x01)
                        {  long loops=BIGEND(buffer+1);
                           if(loops>0)
                           {  Updatetaskattrs(AOGIF_Loops,loops+1,TAG_END);
                           }
                        }
                        c=Readbyte(decoder);
                     }
                  }
                  else
                  {  if(!Skipgifdata(decoder)) return FALSE;
                  }
               }
               break;

#endif
            default:
               if(!Skipgifdata(decoder)) return FALSE;
               break;
         }
      }
      if(c==0x3b)
      {  decoder->flags|=DECOF_EOF;
         return TRUE;
      }
   }
   /* Now we are at the image descriptor */
   if(!Readblock(decoder,buffer,GIFID_SIZEOF)) return FALSE;
   decoder->ileft=BIGEND(buffer+GIFID_LEFT);
   decoder->itop=BIGEND(buffer+GIFID_TOP);
   decoder->iwidth=BIGEND(buffer+GIFID_WIDTH);
   decoder->iheight=BIGEND(buffer+GIFID_HEIGHT);
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifimage: ileft=%ld, itop=%ld, iwidth=%ld, iheight=%ld\n", decoder->ileft, decoder->itop, decoder->iwidth, decoder->iheight);
   }
#endif
   if(decoder->ileft+decoder->iwidth>decoder->width)
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifimage: Image width exceeds screen width\n");
      }
      #endif
      return FALSE;
   }
   if(decoder->itop+decoder->iheight>decoder->height)
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifimage: Image height exceeds screen height\n");
      }
      #endif
      return FALSE;
   }
   if(buffer[GIFID_PACKED]&0x40) decoder->flags|=DECOF_INTERLACED;
   lctsize=1<<((buffer[GIFID_PACKED]&0x07)+1);
   memmove(decoder->colorrgb,decoder->source->globalcolorrgb,sizeof(decoder->colorrgb));
   memset(decoder->pen,0,sizeof(decoder->pen));
   memset(decoder->validpen,0,sizeof(decoder->validpen));
   if(buffer[GIFID_PACKED]&0x80)
   {  /* Local color table. */
      buffer[3]=0;
      for(i=0;i<lctsize;i++)
      {  if(!Readblock(decoder,buffer,3)) return FALSE;
         decoder->colorrgb[i]=*(ULONG *)buffer;
      }
   }
   c=Readbyte(decoder);
   if(decoder->flags&(DECOF_STOP|DECOF_EOF)) return FALSE;

   /* olsen: code size must be in valid bounds. */
   if(c < 2 || c > 9)
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifimage: Invalid code size %d\n", c);
      }
      #endif
      return(FALSE);
   }
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifimage: Setting code size to %d\n", c);
   }
#endif
   Setcodesize(decoder,c);

   /* Hereafter comes the image data. First allocate a bitmap.
    * Always allocate a bitmap of depth >=8. Even if our source has less
    * planes, the colours may be remapped to pen numbers up to 255. */
   decoder->bitmap=NULL;
   decoder->mask=NULL;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifimage: Allocating bitmap, width=%ld, height=%ld\n", decoder->width, decoder->height);
   }
#endif
   if(P96Base && decoder->source->friendbitmap
   && p96GetBitMapAttr(decoder->source->friendbitmap,P96BMA_ISP96))
   {  /* Same policy as PNG: never p96GetBitMapAttr(P96BMA_DEPTH) on a classic
       * screen friend BitMap — can deadlock. Non-P96 friend → classic master below. */
      decoder->depth=p96GetBitMapAttr(decoder->source->friendbitmap,P96BMA_DEPTH);
      #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifimage: P96 depth=%ld, friendbitmap=0x%08lx\n", decoder->depth, (ULONG)decoder->source->friendbitmap);
      }
      #endif
      decoder->bitmap=p96AllocBitMap(decoder->width,decoder->height,decoder->depth,
         BMF_MINPLANES|BMF_CLEAR,decoder->source->friendbitmap,RGBFB_NONE);
      if(decoder->bitmap && p96GetBitMapAttr(decoder->bitmap,P96BMA_ISP96))
      {  decoder->flags|=DECOF_CYBERMAP;
         if(decoder->depth>8)
         {  decoder->flags|=DECOF_CYBERDEEP;
         }
         #ifdef DEBUG_PLUGINS
         if(AwebPluginBase)
         {  Aprintf("GIF: Parsegifimage: P96 bitmap allocated, flags=0x%04x\n", decoder->flags);
         }
         #endif
      }
      else
      {  #ifdef DEBUG_PLUGINS
         if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifimage: P96 bitmap allocation failed, bitmap=0x%08lx\n", (ULONG)decoder->bitmap);
         }
         #endif
      }
   }
   else
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifimage: Allocating standard bitmap, P96Base=0x%08lx, friendbitmap=0x%08lx\n", (ULONG)P96Base, (ULONG)(decoder->source ? decoder->source->friendbitmap : 0));
      }
      #endif
      /* Non-displayable planar master, NULL friend: planes may live in Fast RAM
       * when available (same flags as PNG classic decode path). */
      decoder->bitmap=AllocBitMap(decoder->width,decoder->height,8,
         BMF_CLEAR|BMF_MINPLANES,NULL);
      decoder->depth=8;
      #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifimage: AllocBitMap returned 0x%08lx\n", (ULONG)decoder->bitmap);
      }
      #endif
   }
   if(!decoder->bitmap)
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifimage: Bitmap allocation failed for %ldx%ld\n", decoder->width, decoder->height);
      }
      #endif
      return FALSE;
   }
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifimage: Bitmap allocated successfully, bitmap=0x%08lx\n", (ULONG)decoder->bitmap);
   }
#endif
   if(decoder->flags&DECOF_TRANSPARENT)
   {  if(decoder->flags&DECOF_CYBERMAP)
      {  decoder->maskw=p96GetBitMapAttr(decoder->bitmap,P96BMA_WIDTH)/8;
      }
      else
      {  decoder->maskw=decoder->bitmap->BytesPerRow;
      }
      decoder->mask=(UBYTE *)AllocVec(decoder->maskw*decoder->height,
         MEMF_PUBLIC|MEMF_CLEAR|(decoder->flags&DECOF_CYBERMAP?0:MEMF_CHIP));
   }

   /* Save our bitmap and dimensions. */
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifimage: Allocating Gifimage structure\n");
   }
#endif
   if(!(gi=(struct Gifimage *)AllocVec(sizeof(struct Gifimage),MEMF_PUBLIC|MEMF_CLEAR)))
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifimage: Gifimage allocation failed\n");
      }
      #endif
      return FALSE;
   }
   gi->bitmap=decoder->bitmap;
   gi->mask=decoder->mask;
   gi->delay=delay;

   ObtainSemaphore(&decoder->source->sema);
   ADDTAIL(&decoder->source->images,gi);
   if(!(decoder->source->flags&GIFSF_TIMERWAIT)) decoder->source->current=gi;
   ReleaseSemaphore(&decoder->source->sema);
   Updatetaskattrs(
      AOGIF_Readyfrom,0,
      AOGIF_Readyto,-1,
      AOGIF_Imgready,FALSE,
      AOGIF_Memory,decoder->width*decoder->height*decoder->depth/8+
         (decoder->mask?(decoder->maskw*decoder->height/8):0),
      TAG_END);

   InitRastPort(&decoder->rp);
   decoder->rp.BitMap=decoder->bitmap;
   if(decoder->flags&DECOF_CYBERMAP)
   {  if(decoder->flags&DECOF_CYBERDEEP)
      {  decoder->chunkyw=decoder->width*3;
      }
      else
      {  decoder->chunkyw=((decoder->width+15)>>4)<<4;
      }
      if((decoder->chunky=AllocVec(decoder->chunkyw,MEMF_PUBLIC)))
      {  if(decoder->flags&DECOF_CYBERDEEP)
         {  /* Initialize RenderInfo for RGB mode */
            decoder->renderinfo.Memory=decoder->chunky;
            decoder->renderinfo.BytesPerRow=decoder->chunkyw;
            decoder->renderinfo.RGBFormat=RGBFB_R8G8B8;
         }
         #ifdef DEBUG_PLUGINS
         if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifimage: Calling Buildgifimage\n");
   }
         #endif
   error=!Buildgifimage(decoder);
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifimage: Buildgifimage returned, error=%ld\n", error);
   }
#endif
         FreeVec(decoder->chunky);
      }
   }
   else
   {  /* The colour mapping process needs a temporary RastPort plus BitMap
       * for the PixelLine8 functions. */
      InitRastPort(&decoder->temprp);
      if( (decoder->temprp.BitMap=AllocBitMap(8*(((decoder->iwidth+15)>>4)<<1),1,8,BMF_MINPLANES|BMF_CLEAR,decoder->bitmap)) )
      {  decoder->chunkyw=((decoder->width+15)>>4)<<4;
         if( (decoder->chunky=AllocVec(decoder->chunkyw,MEMF_PUBLIC)) )
         {  error=!Buildgifimage(decoder);
            FreeVec(decoder->chunky);
         }
         FreeBitMap(decoder->temprp.BitMap);
      }
   }

   /* Skip remaining characters of data block, and following spurious blocks */
   if(decoder->remaining)
   {  if(!Readblock(decoder,NULL,decoder->remaining)) return FALSE;
      decoder->remaining=0;
   }
   if(!Skipgifdata(decoder))
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Parsegifimage: Skipgifdata failed\n");
      }
      #endif
      return FALSE;
   }
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Parsegifimage: Done, returning %ld\n", (BOOL)!error);
   }
#endif
   return (BOOL)!error;
}


/* Main subtask process. */
static __saveds __asm void Decodetask(register __a0 struct Gifsource *is)
{
   struct Decoder decoderdata,*decoder=&decoderdata;
   struct Task *task=FindTask(NULL);

#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Decodetask started, is=0x%08lx\n", (ULONG)is);
   }
#endif
   if(!is)
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Decodetask: ERROR - is (Gifsource) is NULL!\n");
      }
      #endif
      return;
   }
   memset(&decoderdata,0,sizeof(decoderdata));
   decoder->source=is;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Decodetask: decoder->source set to 0x%08lx\n", (ULONG)decoder->source);
   }
#endif
   if(decoder->source->flags&GIFSF_LOWPRI)
   {  SetTaskPri(task,max(-128,task->tc_Node.ln_Pri-1)); /* olsen: keep this in range */
   }
   InitRastPort(&decoder->saverp);

#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Decodetask: Calling Parsegifgeneral\n");
   }
#endif
   if(!Parsegifgeneral(decoder))
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Decodetask: Parsegifgeneral failed\n");
      }
      #endif
      goto err;
   }
   while(!(decoder->flags&DECOF_EOF))
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Decodetask: Calling Parsegifimage\n");
      }
      #endif
      if(!Parsegifimage(decoder))
      {  #ifdef DEBUG_PLUGINS
         if(AwebPluginBase)
         {  Aprintf("GIF: Decodetask: Parsegifimage failed\n");
         }
         #endif
         goto err;
      }
      if(decoder->flags&DECOF_EOF) break;
      if(decoder->source->flags&GIFSF_NOANIMATE) break;
      decoder->flags|=DECOF_ANIMFRAME;
   }

#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Decodetask: Decoding complete, setting Decodeready\n");
   }
#endif
   Updatetaskattrs(AOGIF_Decodeready,TRUE,TAG_END);
   if(decoder->saverp.BitMap) FreeBitMap(decoder->saverp.BitMap);
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Decodetask: Done, exiting\n");
   }
#endif
   return;

err:
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Decodetask: ERROR path, setting Error flag\n");
   }
#endif
   Updatetaskattrs(AOGIF_Error,TRUE,TAG_END);
   if(decoder->saverp.BitMap) FreeBitMap(decoder->saverp.BitMap);
   if(decoder->savemask) FreeVec(decoder->savemask);
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Decodetask: ERROR path done, exiting\n");
   }
#endif
}

/*--------------------------------------------------------------------*/
/* Main task functions                                                */
/*--------------------------------------------------------------------*/

/* Save all our source data blocks in this AOTP_FILE object */
static void Savesource(struct Gifsource *gs,struct Aobject *file)
{  struct Datablock *db;
   for(db=gs->data.first;db->next;db=db->next)
   {  Asetattrs(file,
         AOFIL_Data,db->data,
         AOFIL_Datalength,db->length,
         TAG_END);
   }
}

/* Start the decoder task. Only start it if the screen is
 * currenty valid. */
static void Startdecoder(struct Gifsource *gs)
{  struct Screen *screen=NULL;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Startdecoder called, gs=0x%08lx\n", (ULONG)gs);
   }
#endif
   if(Agetattr(Aweb(),AOAPP_Screenvalid))
   {  Agetattrs(Aweb(),
         AOAPP_Screen,&screen,
         AOAPP_Colormap,&gs->colormap,
         TAG_END);
      if(screen) gs->friendbitmap=screen->RastPort.BitMap;
      if(gs->task=Anewobject(AOTP_TASK,
         AOTSK_Entry,Decodetask,
         AOTSK_Name,"AWebGif decoder",
         AOTSK_Userdata,gs,
         AOBJ_Target,gs,
         TAG_END))
      {  Asetattrs(gs->task,AOTSK_Start,TRUE,TAG_END);
      }
   }
}

/* Notify our related Gifcopy objects that the bitmap has gone.
 * Delete the bitmaps, and release all obtained pens. */
static void Releaseimage(struct Gifsource *gs)
{  int i;
   struct Gifimage *gi;
   Anotifyset(gs->source,AOGIF_Bitmap,NULL,TAG_END);
   while(gi=REMHEAD(&gs->images))
   {  if(gi->bitmap) FreeBitMap(gi->bitmap);
      if(gi->mask) FreeVec(gi->mask);
      FreeVec(gi);
   }
   gs->current=NULL;
   for(i=0;i<256;i++)
   {  while(gs->allocated[i])
      {  ReleasePen(gs->colormap,i);
         gs->allocated[i]--;
      }
   }
   gs->colormap=NULL;
   gs->flags&=~(GIFSF_IMAGEREADY|GIFSF_ANIMREADY);
   gs->memory=0;
   Asetattrs(gs->source,AOSRC_Memory,0,TAG_END);
}

/* Delete all source data */
static void Releasedata(struct Gifsource *gs)
{  struct Datablock *db;
   while(db=REMHEAD(&gs->data))
   {  if(db->data) FreeVec(db->data);
      FreeVec(db);
   }
}

/* Check if an AWeb window is active */
static BOOL Awebactive(void)
{  struct IntuitionBase *ibase=(struct IntuitionBase *)IntuitionBase;
   ULONG lock=LockIBase(0);
   BOOL active=FALSE;
   if(ibase->ActiveWindow && ibase->ActiveWindow->UserPort
   && ibase->ActiveWindow->UserPort->mp_SigTask==FindTask(NULL))
   {  active=TRUE;
   }
   UnlockIBase(lock);
   return active;
}

/* Start the animation timer for the current image */
static void Starttimer(struct Gifsource *gs,BOOL inactive)
{  long delay;
   if(!gs->timer)
   {  gs->timer=Anewobject(AOTP_TIMER,
         AOBJ_Target,gs,
         TAG_END);
   }
   ObtainSemaphore(&gs->sema);
   if(gs->timer)
   {  if(inactive)
      {  Asetattrs(gs->timer,
            AOTIM_Waitseconds,0,
            AOTIM_Waitmicros,500000,
            TAG_END);
         gs->flags|=GIFSF_TIMERWAIT;
      }
      else
      {  if(gs->current->delay>5) delay=gs->current->delay;
         else delay=5;
         Asetattrs(gs->timer,
            AOTIM_Waitseconds,delay/100,
            AOTIM_Waitmicros,(delay%100)*10000,
            TAG_END);
         gs->flags|=GIFSF_TIMERWAIT;
      }
   }
   ReleaseSemaphore(&gs->sema);
}

/* Next animation frame. If no next, restart with first frame if animation is ready.
 * If the new image is ready, start the timer (else the timer will be started when
 * the image decoding is finished).
 * If no AWeb window is active, don't proceed to next frame but wait a specified time. */
static void Nextanimation(struct Gifsource *gs)
{  struct Gifimage *gi;
   ObtainSemaphore(&gs->sema);
   if(gs->current && (gs->flags&GIFSF_DISPLAYED))
   {  if(animate && Awebactive())
      {  gi=gs->current->next;
         if(gi->next || (gs->flags&GIFSF_ANIMREADY))
         {  if(!gi->next) gi=gs->images.first;
            gs->current=gi;
            Anotifyset(gs->source,
               AOGIF_Bitmap,gs->current->bitmap,
               AOGIF_Mask,gs->current->mask,
               AOGIF_Width,gs->width,
               AOGIF_Height,gs->height,
               AOGIF_Readyfrom,0,
               AOGIF_Readyto,gs->ready,
               AOGIF_Imgready,gs->current->ready,
               AOGIF_Animframe,TRUE,
               AOGIF_Jsanim,gs->current==gs->images.last && (gs->flags&GIFSF_ANIMREADY),
               AOGIF_Maxloops,gs->maxloops,
               AOGIF_Newloop,gs->current==gs->images.first,
               TAG_END);
            if(gs->current->ready)
            {  Starttimer(gs,FALSE);
            }
         }
      }
      else
      {  Starttimer(gs,TRUE);
      }
   }
   ReleaseSemaphore(&gs->sema);
}

/*--------------------------------------------------------------------*/
/* Plugin sourcedriver object dispatcher functions                    */
/*--------------------------------------------------------------------*/

static ULONG Setsource(struct Gifsource *gs,struct Amset *amset)
{  struct TagItem *tag,*tstate;
   UBYTE *arg,*p;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Setsource called, gs=0x%08lx, amset=0x%08lx\n", (ULONG)gs, (ULONG)amset);
   }
#endif
   Amethodas(AOTP_SOURCEDRIVER,(struct Aobject *)gs,AOM_SET,amset->tags);
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            gs->source=(struct Aobject *)tag->ti_Data;
            break;
         case AOSDV_Savesource:
            Savesource(gs,(struct Aobject *)tag->ti_Data);
            break;
         case AOSDV_Displayed:
            if(tag->ti_Data)
            {  /* We are becoming displayed. Start our decoder task if
                * we have data but no bitmap yet, and task is not yet
                * running. */
               gs->flags|=GIFSF_DISPLAYED;
               ObtainSemaphore(&gs->sema);
               if(gs->data.first->next && !gs->current && !gs->task)
               {  Startdecoder(gs);
               }
               else if(gs->images.first->next && gs->images.first->ready
               && gs->images.first!=gs->images.last && !(gs->flags&GIFSF_TIMERWAIT))
               {  gs->current=gs->images.last;
                  Nextanimation(gs);
               }
               ReleaseSemaphore(&gs->sema);
            }
            else
            {  /* We are not displayed anywhere now. Leave the bitmap
                * intact so we don't need to decode it again when we
                * are becoming displayed again. */
               gs->flags&=~GIFSF_DISPLAYED;
            }
            break;
         case AOAPP_Screenvalid:
            if(tag->ti_Data)
            {  if(gs->data.first->next && (gs->flags&GIFSF_DISPLAYED)
               && !gs->task)
               {  Startdecoder(gs);
               }
            }
            else
            {  if(gs->task)
               {  Adisposeobject(gs->task);
                  gs->task=NULL;
               }
               Releaseimage(gs);
            }
            break;
         case AOSDV_Arguments:
            arg=(UBYTE *)tag->ti_Data;
            if(arg)
            {
               LONG value;

               for(p=arg;*p;p++)
               {  if(!Strnicmp(p,"PROGRESS=",9))
                  {
                     StrToLong(p+9,&value); /* olsen: don't pull in the atoi() code. */
                     gs->progress=value;
                     p+=9;
                  }
                  else if(!Strnicmp(p,"ANIMATE=",8))
                  {  if(p[8]=='0') gs->flags|=GIFSF_NOANIMATE;
                     p+=8;
                  }
                  else if(!Strnicmp(p,"LOWPRI",6))
                  {  gs->flags|=GIFSF_LOWPRI;
                     p+=6;
                  }
                  else if(!Strnicmp(p,"LOOPS=",6))
                  {
                     StrToLong(p+6,&value); /* olsen: don't pull in the atoi() code. */
                     gs->maxloops=value;
                     if(gs->maxloops<0) gs->maxloops=-1;
                     else if(gs->maxloops==0) gs->flags|=GIFSF_NOANIMATE;
                     p+=6;
                  }
               }
            }
            break;
      }
   }
   return 0;
}

static struct Gifsource *Newsource(struct Amset *amset)
{  struct Gifsource *gs;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Newsource called, amset=0x%08lx\n", (ULONG)amset);
   }
#endif
   if(gs=Allocobject(PluginBase->sourcedriver,sizeof(struct Gifsource),amset))
      {  #ifdef DEBUG_PLUGINS
         if(AwebPluginBase)
      {  Aprintf("GIF: Newsource: Allocobject succeeded, gs=0x%08lx\n", (ULONG)gs);
      }
         #endif
      InitSemaphore(&gs->sema);
      NEWLIST(&gs->data);
      NEWLIST(&gs->images);
      #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Newsource: Calling Aaddchild\n");
      }
      #endif
      Aaddchild(Aweb(),(struct Aobject *)gs,AOREL_APP_USE_SCREEN);
      #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Newsource: Aaddchild returned\n");
      }
      #endif
      gs->progress=4;
      gs->maxloops=-1;
      #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Newsource: Calling Setsource\n");
      }
      #endif
      Setsource(gs,amset);
      #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Newsource: Setsource returned\n");
      }
      #endif
      /* Workaround for missing AOSDV_Displayed in pre-0.132 */
      if(!(gs->flags&GIFSF_DISPLAYED))
      {  if(Agetattr(gs->source,AOSRC_Displayed))
         {  Asetattrs((struct Aobject *)gs, AOSDV_Displayed, TRUE, TAG_END);
         }
      }
      #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Newsource: returning gs=0x%08lx\n", (ULONG)gs);
      }
      #endif
   }
   else
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Newsource: Allocobject failed\n");
      }
      #endif
   }
   return gs;
}

static ULONG Getsource(struct Gifsource *gs,struct Amset *amset)
{  struct TagItem *tag,*tstate;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Getsource called, gs=0x%08lx, amset=0x%08lx\n", (ULONG)gs, (ULONG)amset);
   }
#endif
   AmethodasA(AOTP_SOURCEDRIVER,(struct Aobject *)gs, (struct Amessage *)amset);
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            PUTATTR(tag,gs->source);
            break;
         case AOSDV_Saveable:
            PUTATTR(tag,(gs->flags&GIFSF_EOF)?TRUE:FALSE);
            break;
      }
   }
   return 0;
}

static ULONG Srcupdatesource(struct Gifsource *gs,struct Amsrcupdate *amsrcupdate)
{  struct TagItem *tag,*tstate;
   UBYTE *data=NULL;
   long datalength=0;
   BOOL eof=FALSE;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Srcupdatesource called, gs=0x%08lx, amsrcupdate=0x%08lx\n", (ULONG)gs, (ULONG)amsrcupdate);
   }
#endif
   AmethodasA(AOTP_SOURCEDRIVER,(struct Aobject *)gs,(struct Amessage *)amsrcupdate);
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
               gs->flags|=GIFSF_EOF;
            }
            break;
      }
   }
   if(data && datalength)
   {  struct Datablock *db;
      if(db=AllocVec(sizeof(struct Datablock),MEMF_PUBLIC|MEMF_CLEAR))
      {  if(db->data=AllocVec(datalength,MEMF_PUBLIC))
         {  memmove(db->data,data,datalength);
            db->length=datalength;
            ObtainSemaphore(&gs->sema);
            ADDTAIL(&gs->data,db);
            ReleaseSemaphore(&gs->sema);
         }
         else
         {  FreeVec(db);
         }
      }
      if(!gs->task)
      {  Startdecoder(gs);
      }
   }
   if((data && datalength) || eof)
   {  if(gs->task)
      {  Asetattrsasync(gs->task,AOGIF_Data,TRUE,TAG_END);
      }
   }
   return 0;
}

static ULONG Updatesource(struct Gifsource *gs,struct Amset *amset)
{  struct TagItem *tag,*tstate;
   BOOL notify=FALSE,animframe=FALSE;
   long readyfrom=0,readyto=-1;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Updatesource called, gs=0x%08lx, amset=0x%08lx\n", (ULONG)gs, (ULONG)amset);
   }
#endif
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOGIF_Readyfrom:
            readyfrom=tag->ti_Data;
            notify=TRUE;
            break;
         case AOGIF_Readyto:
            readyto=tag->ti_Data;
            if(readyto>gs->ready || readyto<0) gs->ready=readyto;
            notify=TRUE;
            break;
         case AOGIF_Imgready:
            if(tag->ti_Data)
            {  gs->ready=gs->height-1;
               ObtainSemaphore(&gs->sema);
               gs->images.last->ready=TRUE;
               if(gs->current==gs->images.last && gs->current->delay
               && !(gs->flags&GIFSF_TIMERWAIT) && (gs->flags&GIFSF_DISPLAYED))
               {  Starttimer(gs,FALSE);
               }
               ReleaseSemaphore(&gs->sema);
            }
            break;
         case AOGIF_Decodeready:
            if(tag->ti_Data)
            {  if(gs->images.first==gs->images.last)
               {  gs->flags|=GIFSF_IMAGEREADY;
                  Anotifyset(gs->source,
                     AOGIF_Jsready,TRUE,
                     TAG_END);
               }
               else
               {  gs->flags|=GIFSF_ANIMREADY;
                  if(gs->current==gs->images.last)
                  {  Anotifyset(gs->source,
                        AOGIF_Jsanim,TRUE,
                        TAG_END);
                  }
                  if(!(gs->flags&GIFSF_TIMERWAIT))
                  {  Nextanimation(gs);
                  }
               }
            }
            else
            {  gs->flags&=~(GIFSF_IMAGEREADY|GIFSF_ANIMREADY);
            }
            break;
         case AOGIF_Animframe:
            animframe=tag->ti_Data;
            break;
         case AOGIF_Error:
            break;
         case AOGIF_Memory:
            gs->memory+=tag->ti_Data;
            Asetattrs(gs->source,
               AOSRC_Memory,gs->memory,
               TAG_END);
            break;
         case AOTIM_Ready:
            if(tag->ti_Data)
            {  ObtainSemaphore(&gs->sema);
               gs->flags&=~GIFSF_TIMERWAIT;
               if(gs->flags&GIFSF_DISPLAYED)
               {  Nextanimation(gs);
               }
               ReleaseSemaphore(&gs->sema);
            }
            break;
         case AOGIF_Loops:
            if(tag->ti_Data && tag->ti_Data<gs->maxloops)
            {  gs->maxloops=tag->ti_Data;
            }
            break;
      }
   }
   ObtainSemaphore(&gs->sema);
   if(notify && gs->current)
   {  Anotifyset(gs->source,
         AOGIF_Bitmap,gs->current->bitmap,
         AOGIF_Mask,gs->current->mask,
         AOGIF_Width,gs->width,
         AOGIF_Height,gs->height,
         AOGIF_Readyfrom,readyfrom,
         AOGIF_Readyto,readyto,
         AOGIF_Imgready,gs->current->ready,
         AOGIF_Animframe,animframe,
         AOGIF_Maxloops,gs->maxloops,
         TAG_END);
   }
   ReleaseSemaphore(&gs->sema);
   return 0;
}

static ULONG Addchildsource(struct Gifsource *gs,struct Amadd *amadd)
{  #ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Addchildsource called, gs=0x%08lx, amadd=0x%08lx, relation=%ld\n", (ULONG)gs, (ULONG)amadd, amadd ? amadd->relation : 0);
   }
#endif
   if(amadd->relation==AOREL_SRC_COPY)
   {  ObtainSemaphore(&gs->sema);
      if(gs->current && gs->current->bitmap)
      {  Asetattrs(amadd->child,
            AOGIF_Bitmap,gs->current->bitmap,
            AOGIF_Mask,gs->current->mask,
            AOGIF_Width,gs->width,
            AOGIF_Height,gs->height,
            AOGIF_Readyfrom,0,
            AOGIF_Readyto,gs->ready,
            AOGIF_Imgready,gs->current->ready,
            AOGIF_Jsready,(gs->flags&GIFSF_IMAGEREADY)
               || ((gs->flags&GIFSF_ANIMREADY) && gs->current==gs->images.last),
            AOGIF_Maxloops,gs->maxloops,
            TAG_END);
      }
      ReleaseSemaphore(&gs->sema);
   }
   return 0;
}

static void Disposesource(struct Gifsource *gs)
{  #ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Disposesource called, gs=0x%08lx\n", (ULONG)gs);
   }
#endif
   if(gs->task)
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Disposesource: Disposing task\n");
      }
      #endif
      Adisposeobject(gs->task);
   }
   if(gs->timer)
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Disposesource: Disposing timer\n");
      }
      #endif
      Adisposeobject(gs->timer);
   }
   Releaseimage(gs);
   Releasedata(gs);
   Aremchild(Aweb(), (struct Aobject *)gs, AOREL_APP_USE_SCREEN);
   Amethodas(AOTP_SOURCEDRIVER, (struct Aobject *)gs, AOM_DISPOSE);
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Disposesource: Done\n");
   }
#endif
}

__saveds __asm ULONG Dispatchsource(register __a0 struct Gifsource *gs, register __a1 struct Amessage *amsg)
{  ULONG result=0;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase && amsg)
   {  Aprintf("GIF: Dispatchsource called, gs=0x%08lx, method=0x%08lx\n", (ULONG)gs, amsg->method);
   }
#endif
   switch(amsg->method)
   {  case AOM_NEW:
         result=(ULONG)Newsource((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setsource(gs,(struct Amset *)amsg);

         break;
      case AOM_GET:
         result=Getsource(gs,(struct Amset *)amsg);

         break;
      case AOM_SRCUPDATE:
         result=Srcupdatesource(gs,(struct Amsrcupdate *)amsg);

         break;
      case AOM_UPDATE:
         result=Updatesource(gs,(struct Amset *)amsg);

         break;
      case AOM_ADDCHILD:
         result=Addchildsource(gs,(struct Amadd *)amsg);

         break;
      case AOM_DISPOSE:
         Disposesource(gs);
         break;
      default:
         result=AmethodasA(AOTP_SOURCEDRIVER,(struct Aobject *)gs,amsg);
         break;
   }
   return result;
}

