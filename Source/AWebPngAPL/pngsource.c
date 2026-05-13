/**********************************************************************
 * 
 * This file is part of the AWeb distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
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
 * because it will never change (but the ln_Succ pointer will!). */
struct Datablock
{  NODE(Datablock);
   UBYTE *data;                     /* The data */
   long length;                     /* Length of the data */
};

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

/* This structure holds vital data for the decoding subprocess */
struct Decoder
{  struct Datablock *current;       /* The current datablock */
   long currentbyte;                /* The current byte in the current datablock */
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

   for(;;)
   {  wait=FALSE;
      while(!(decoder->flags&DECOF_STOP) && (tm=Gettaskmsg()))
      {  if(tm->amsg && tm->amsg->method==AOM_SET)
         {  tstate=((struct Amset *)tm->amsg)->tags;
            while(tag=NextTagItem(&tstate))
            {  switch(tag->ti_Tag)
                   {  case AOTSK_Stop:
                         if(tag->ti_Data) decoder->flags|=DECOF_STOP;
                         break;
                      case AOPNG_Data:
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
                if(db->next)
                {  decoder->current=db;
                   decoder->currentbyte=0;
                }
                else if(decoder->source->flags&PNGSF_EOF)
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
             ObtainSemaphore(&decoder->source->sema);
             db=decoder->source->data.first;
             if(db->next)
             {  decoder->current=db;
                decoder->currentbyte=0;
             }
             else if(decoder->source->flags&PNGSF_EOF)
             {  decoder->flags|=DECOF_EOF;
             }
             else
             {  /* No block yet; wait for next block */
                wait=TRUE;
             }
             ReleaseSemaphore(&decoder->source->sema);
          }
      if(!wait)
      {  if(!(decoder->flags&DECOF_EOF))
         {  retval=decoder->current->data[decoder->currentbyte];
         }
         break;
      }
          Waittask(0);
       }
       return retval;
    }

/* Read or skip a number of bytes. Returns TRUE if ok, FALSE of EOF reached before
 * end of block, or task should stop. If block is passed as NULL, data is skipped.
 * This function reads directly from data blocks in chunks for efficiency.
 * Optimized to minimize semaphore operations and message handling. */
static BOOL Readblock(struct Decoder *decoder,UBYTE *block,long length)
{  struct Taskmsg *tm;
   struct TagItem *tag,*tstate;
   struct Datablock *db;
   long available,copylen;
   BOOL wait;
   if(!decoder || !decoder->source) return FALSE;
   if(decoder->flags&(DECOF_STOP|DECOF_EOF)) return FALSE;
   while(length>0 && !(decoder->flags&(DECOF_STOP|DECOF_EOF)))
   {  /* Fast path: read from current block without semaphore if we have data */
      if(decoder->current && decoder->current->data)
      {  available=decoder->current->length-decoder->currentbyte;
         if(available>0)
         {  copylen=(available<length) ? available : length;
            if(block)
            {  memmove(block,decoder->current->data+decoder->currentbyte,copylen);
               block+=copylen;
            }
            decoder->currentbyte+=copylen;
            length-=copylen;
            continue; /* Continue to read more if needed */
         }
      }
      /* Need to get a new block - now we need semaphore protection */
      wait=FALSE;
      /* Check for messages only when we need to wait for data */
      while(!(decoder->flags&DECOF_STOP) && (tm=Gettaskmsg()))
      {  if(tm->amsg && tm->amsg->method==AOM_SET)
         {  tstate=((struct Amset *)tm->amsg)->tags;
            while(tag=NextTagItem(&tstate))
            {  switch(tag->ti_Tag)
               {  case AOTSK_Stop:
                     if(tag->ti_Data) decoder->flags|=DECOF_STOP;
                     break;
                  case AOPNG_Data:
                     /* Ignore these now */
                     break;
               }
            }
         }
         Replytaskmsg(tm);
      }
      if(decoder->flags&DECOF_STOP) break;
      ObtainSemaphore(&decoder->source->sema);
      if(decoder->current)
      {  db=decoder->current->next;
      }
      else
      {  db=decoder->source->data.first;
      }
      if(db->next)
      {  /* We have a valid next block */
         decoder->current=db;
         decoder->currentbyte=0;
         ReleaseSemaphore(&decoder->source->sema);
         continue; /* Loop back to read from new block */
      }
      else if(!db || (decoder->source->flags&PNGSF_EOF))
      {  /* EOF reached or no blocks available */
         decoder->flags|=DECOF_EOF;
         ReleaseSemaphore(&decoder->source->sema);
         break;
      }
      else
      {  /* No more blocks; wait for next block */
         wait=TRUE;
         ReleaseSemaphore(&decoder->source->sema);
      }
      if(wait)
      {  Waittask(0);
      }
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

/* Libpng has no way to override the standard memory allocation (unlike
 * it says in the documentation), so just redefine the standard calls
 * with functions using AllocMem */

void *malloc(size_t size)
{  void *p=AllocMem(size+4,MEMF_PUBLIC);
   if(p)
   {  *(ULONG *)p=size+4;
      return (void *)((ULONG)p+4);
   }
   else
   return NULL;
}

void free(void *p)
{  if(p)
   {  void *ap=(void *)((ULONG)p-4);
      FreeMem(ap,*(ULONG *)ap);
   }
}

void *realloc(void *p,size_t size)
{  void *ap,*np;
   long oldsize,copysize;
   if(p)
   {  ap=(void *)((ULONG)p-4);
      oldsize=*(ULONG *)ap;
   }
   else
   {  ap=NULL;
      oldsize=0;
   }
   np=AllocMem(size+4,MEMF_PUBLIC);
   if(np)
   {  *(ULONG *)np=size+4;
      if(ap)
      {  if(size<oldsize) copysize=size;
         else copysize=oldsize;
         if(copysize) memmove((void *)((ULONG)np+4),p,copysize);
      }
   }
   if(ap) FreeMem(ap,oldsize);
   if(np) return (void *)((ULONG)np+4);
   else return NULL;
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
   short r=0,g=0,b=0,pen,map;
   UBYTE *trow=NULL;
   BOOL error=FALSE,transpixel;
   
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
   if((png=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL))
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
         if(png_get_valid(png,pnginfo,PNG_INFO_tRNS))
         {  if(colortype==PNG_COLOR_TYPE_PALETTE)
            {  png_get_tRNS(png,pnginfo,&trans,&ntrans,NULL);
            }
            else
            {  /* Expand grayscale or color to alpha channel */
               png_set_expand(png);
            }
            decoder->flags|=DECOF_TRANSPARENT;
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
         {  if(!(buffer=AllocVec(rowbytes,0))) longjmp(png_jmpbuf(png),1);
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
         
         if(colortype==PNG_COLOR_TYPE_PALETTE)
         {  png_get_PLTE(png,pnginfo,&palette,&npalette);
         }
#ifdef DEBUG_PLUGINS
         PngLog("parse","row buffers ready, entering %ld pass(es)",(long)npass);
#endif
         
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

               if(decoder->flags&DECOF_P96DEEP)
               {  for(x=0;x<width;x++)
                  {  transpixel=FALSE;
                     if(decoder->mask)
                     {  trow=decoder->mask+y*decoder->maskw;
                     }
                     switch(colortype)
                     {  case PNG_COLOR_TYPE_GRAY:
                           memset(decoder->chunky+3*x,buffer[x],3);
                           break;
                        case PNG_COLOR_TYPE_GRAY_ALPHA:
                           memset(decoder->chunky+3*x,buffer[2*x],3);
                           transpixel=(buffer[2*x+1]<THRESHOLD);
                           break;
                        case PNG_COLOR_TYPE_PALETTE:
                           pen=buffer[x];
                           decoder->chunky[3*x]=palette[pen].red;
                           decoder->chunky[3*x+1]=palette[pen].green;
                           decoder->chunky[3*x+2]=palette[pen].blue;
                           transpixel=(trans && pen<ntrans && trans[pen]<THRESHOLD);
                           break;
                        case PNG_COLOR_TYPE_RGB:
                           memmove(decoder->chunky+3*x,buffer+3*x,3);
                           break;
                        case PNG_COLOR_TYPE_RGB_ALPHA:
                           memmove(decoder->chunky+3*x,buffer+4*x,3);
                           transpixel=(buffer[4*x+3]<THRESHOLD);
                           break;
                     }
                     if(decoder->mask && !transpixel)
                     {  trow[x>>3]|=0x80>>(x&0x7);
                     }
                  }
                  p96WritePixelArray(&decoder->renderinfo,0,0,
                     &decoder->rp,0,y,decoder->width,1);
               }
               else
               {  for(x=0;x<decoder->width;x++)
                  {  map=-1;
                     pen=-1;
                     transpixel=FALSE;
                     if(decoder->mask)
                     {  trow=decoder->mask+y*decoder->maskw;
                     }
                     switch(colortype)
                     {  case PNG_COLOR_TYPE_GRAY:
                           /* See if a pen is allocated for this shade */
                           pen=buffer[x];
                           if(decoder->source->mapped[pen])
                           {  map=decoder->source->map[pen];
                           }
                           else
                           {  r=g=b=pen;
                           }
                           break;
                        case PNG_COLOR_TYPE_GRAY_ALPHA:
                           /* See if a pen is allocated for this shade */
                           pen=buffer[2*x];
                           if(decoder->source->mapped[pen])
                           {  map=decoder->source->map[pen];
                           }
                           else
                           {  r=g=b=pen;
                           }
                           transpixel=(buffer[2*x+1]<THRESHOLD);
                           break;
                        case PNG_COLOR_TYPE_PALETTE:
                           pen=buffer[x];
                           if(decoder->source->mapped[pen])
                           {  map=decoder->source->map[pen];
                           }
                           else
                           {  r=palette[pen].red;
                              g=palette[pen].green;
                              b=palette[pen].blue;
                           }
                           transpixel=(trans && pen<ntrans && trans[pen]<THRESHOLD);
                           break;
                        case PNG_COLOR_TYPE_RGB:
                           r=buffer[3*x];
                           g=buffer[3*x+1];
                           b=buffer[3*x+2];
                           break;
                        case PNG_COLOR_TYPE_RGB_ALPHA:
                           r=buffer[4*x];
                           g=buffer[4*x+1];
                           b=buffer[4*x+2];
                           transpixel=(buffer[4*x+3]<THRESHOLD);
                           break;
                     }
                     if(map<0)
                     {  map=ObtainBestPen(decoder->source->colormap,
                           RGB(r),RGB(g),RGB(b),TAG_END);
                        if(decoder->source->allocated[map])
                        {  ReleasePen(decoder->source->colormap,map);
                        }
                        else
                        {  decoder->source->allocated[map]=TRUE;
                        }
                        if(pen>=0)
                        {  decoder->source->map[pen]=map;
                           decoder->source->mapped[pen]=TRUE;
                        }
                     }
                     decoder->chunky[x]=map;
                     if(decoder->mask && !transpixel)
                     {  trow[x>>3]|=0x80>>(x&0x7);
                     }
                  }
                  if(decoder->flags&DECOF_P96MAP)
                  {  /* For Picasso96 8-bit mode, use WriteChunkyPixels if available */
                     if(GfxBase && GfxBase->lib_Version >= 40)
                     {  /* Use WriteChunkyPixels for faster conversion (OS 3.1+) */
                        WriteChunkyPixels(&decoder->rp,0,y,decoder->width-1,y,
                           decoder->chunky,decoder->width);
                     }
                     else
                     {  /* Fallback to WritePixelLine8 for OS 3.0 */
                        WritePixelLine8(&decoder->rp,0,y,decoder->width,decoder->chunky,
                           &decoder->temprp);
                     }
                  }
                  else
                  {  /* For standard bitmaps, use WriteChunkyPixels if available */
                     if(GfxBase && GfxBase->lib_Version >= 40)
                     {  /* Use WriteChunkyPixels for faster conversion (OS 3.1+) */
                        WriteChunkyPixels(&decoder->rp,0,y,decoder->width-1,y,
                           decoder->chunky,decoder->width);
                     }
                     else
                     {  /* Fallback to WritePixelLine8 for OS 3.0 */
                        WritePixelLine8(&decoder->rp,0,y,decoder->width,decoder->chunky,
                           &decoder->temprp);
                     }
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
   png_destroy_read_struct(&png,&pnginfo,NULL);
   if(npass==1)
   {  if(buffer) FreeVec(buffer);
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

/* Delete all source data */
static void Releasedata(struct Pngsource *ps)
{  struct Datablock *db;
   while(db=REMHEAD(&ps->data))
   {  if(db->data) FreeVec(db->data);
      FreeVec(db);
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
      if(db=AllocVec(sizeof(struct Datablock),MEMF_ANY|MEMF_CLEAR))
      {  if(db->data=AllocVec(datalength,MEMF_ANY))
         {  memmove(db->data,data,datalength);
            db->length=datalength;
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
            PngLog("src","Srcupdatesource: ERROR - Failed to allocate data buffer");
            #endif
            FreeVec(db);
         }
      }
      else
      {  #ifdef DEBUG_PLUGINS
         PngLog("src","Srcupdatesource: ERROR - Failed to allocate Datablock structure");
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

