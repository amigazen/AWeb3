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

/* imgsource.c - AWeb image source interpreter object */

#include "aweb.h"
#include "source.h"
#include "sourcedriver.h"
#include "copy.h"
#include "url.h"
#include "file.h"
#include "imgprivate.h"
#include "application.h"
#include "task.h"
#include "cache.h"
#include "fetch.h"
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
/* PMODE_V42 and PMODE_V43 constants are defined in pictureclass.h */
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/datatypes.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/icon.h>
#include <dos/dosextens.h>
#include <dos/dos.h>
#include <datatypes/datatypes.h>
#include <stdlib.h>

#ifdef DEVELOPER
extern BOOL usetemp;
#else
#define usetemp FALSE
#endif

/*------------------------------------------------------------------------*/

struct Imagetask
{  struct Screen *screen;              /* Screen to use for remapping */
   struct SignalSemaphore screensema;  /* Protects the screen pointer */
   struct SignalSemaphore *procsema;   /* Only one task can process */
};

static struct Imagetask imagetask;

#define LAZY_IMAGE_DECODE  1
/* Experimental: if enabled, only start decode when the source is "displayed"
 * (i.e. has at least one active Copy in a window / JS image). EOF marks that
 * the bytes are available, but does not automatically kick off bitmap decode.
 * This reduces initial CPU/memory spikes on image-heavy pages. */

#define AOIMS_Dummy        AOBJ_DUMMYTAG(AOTP_IMGSOURCE)

#define AOIMS_Dtobject     (AOIMS_Dummy+1)
#define AOIMS_Bitmap       (AOIMS_Dummy+2)
#define AOIMS_Mask         (AOIMS_Dummy+3)
#define AOIMS_Width        (AOIMS_Dummy+4)
#define AOIMS_Height       (AOIMS_Dummy+5)
#define AOIMS_Memsize      (AOIMS_Dummy+6)
#define AOIMS_Ourmask      (AOIMS_Dummy+7)
#define AOIMS_Depth        (AOIMS_Dummy+8)

/*------------------------------------------------------------------------*/

struct Imgprocess
{  struct Imgsource *ims;
   struct Screen *screen;
   void *dto;
   struct BitMap *bitmap;
   UBYTE *mask;
   long width,height,depth;
   BOOL ourmask;
   long memsize;
};

/* Determine if this is a transparent gif */
static long Transparentgif(struct Imgprocess *imp)
{  long fh;
   UBYTE buf[16];
   long xptcolor=-1;
   long colortabsize,pos;
   if(fh=Open(imp->ims->filename,MODE_OLDFILE))
   {  if(Read(fh,buf,16)==16 && STRNEQUAL(buf,"GIF89",5))
      {  if(buf[0x0a]&0x80) colortabsize=3*(1<<((buf[0x0a]&0x07)+1));
         else colortabsize=0;
         Seek(fh,pos=0x0d+colortabsize,OFFSET_BEGINNING);
         for(;;)
         {  if(Read(fh,buf,7)==7)
            {  if(buf[0]==0x21 && buf[1]==0xf9 && (buf[3]&0x01))
               {  xptcolor=buf[6];
                  break;
               }
               if(buf[0]==0x21)
               {  pos+=2; // 3+buf[2];
                  do
                  {  Seek(fh,pos,OFFSET_BEGINNING);
                     if(Read(fh,buf,1)!=1) buf[0]=0;
                     pos+=1+buf[0];
                  } while(buf[0]);
               }
               else break;
            }
         }
      }
      Close(fh);
   }
   return xptcolor;
}

/* Extract alpha channel from pixel array and create mask plane */
static void Makealphamask(struct Imgprocess *imp,void *dto)
{  struct pdtBlitPixelArray pbpa={0};
   UBYTE *pixeldata=NULL;
   ULONG pixelformat=0;
   ULONG rowstride;
   ULONG width,height;
   UBYTE *maskptr;
   ULONG x,y;
   ULONG y0;
   ULONG maskbytesperrow;
   ULONG flags;
   ULONG chunkrows;
   ULONG thisrows;
   BOOL ok;
   
   /* Get bitmap dimensions */
   width=imp->width;
   height=imp->height;
   maskbytesperrow=GetBitMapAttr(imp->bitmap,BMA_WIDTH)/8;
   flags=GetBitMapAttr(imp->bitmap,BMA_FLAGS);
   if(flags&BMF_INTERLEAVED) maskbytesperrow/=GetBitMapAttr(imp->bitmap,BMA_DEPTH);
   
   /* Allocate small buffer for RGBA pixel data (streaming) */
   rowstride=width*4;  /* 4 bytes per pixel (RGBA or ARGB) */
   chunkrows=8;
   if(chunkrows>height) chunkrows=height;
   if(chunkrows<1) return;
   pixeldata=Allocmem(rowstride*chunkrows,MEMF_PUBLIC|MEMF_CLEAR);
   if(!pixeldata) return;
   
   /* Read first chunk to determine pixel format */
   pbpa.MethodID=PDTM_READPIXELARRAY;
   pbpa.pbpa_PixelData=pixeldata;
   pbpa.pbpa_PixelFormat=PBPAFMT_RGBA;  /* Try RGBA format first */
   pbpa.pbpa_PixelArrayMod=rowstride;
   pbpa.pbpa_Left=0;
   pbpa.pbpa_Top=0;
   pbpa.pbpa_Width=width;
   pbpa.pbpa_Height=chunkrows;
   
   ok=BOOLVAL(DoMethodA(dto,(Msg)&pbpa));
   if(!ok)
   {  /* Try ARGB format if RGBA failed */
      pbpa.pbpa_PixelFormat=PBPAFMT_ARGB;
      ok=BOOLVAL(DoMethodA(dto,(Msg)&pbpa));
      if(!ok)
      {  Freemem(pixeldata);
         return;
      }
      pixelformat=PBPAFMT_ARGB;
   }
   else
   {  pixelformat=PBPAFMT_RGBA;
   }
   
   /* Allocate mask plane */
   if(imp->mask=ALLOCTYPE(UBYTE,maskbytesperrow*height,
      MEMF_PUBLIC|MEMF_CHIP|MEMF_CLEAR))
   {  imp->memsize+=maskbytesperrow*height;
      
      /* Convert alpha channel to mask plane */
      /* Alpha values: 0=transparent, 255=opaque */
      /* Amiga mask convention: 1=opaque, 0=transparent */
      y0=0;
      while(y0<height)
      {  UBYTE *rowdata;
         if(y0==0)
         {  thisrows=chunkrows; /* first chunk already read */
         }
         else
         {  thisrows=chunkrows;
            if(y0+thisrows>height) thisrows=height-y0;
            pbpa.pbpa_PixelData=pixeldata;
            pbpa.pbpa_PixelFormat=pixelformat;
            pbpa.pbpa_PixelArrayMod=rowstride;
            pbpa.pbpa_Left=0;
            pbpa.pbpa_Top=y0;
            pbpa.pbpa_Width=width;
            pbpa.pbpa_Height=thisrows;
            if(!DoMethodA(dto,(Msg)&pbpa))
            {  break;
            }
         }
         for(y=0;y<thisrows;y++)
         {  rowdata=pixeldata+y*rowstride;
            maskptr=imp->mask+(y0+y)*maskbytesperrow;
            for(x=0;x<width;x++)
            {  UBYTE alpha;
               ULONG bitpos;
               ULONG bytepos;
               bitpos=x&7;
               bytepos=x>>3;
               if(bytepos<maskbytesperrow)
               {  if(pixelformat==PBPAFMT_RGBA)
                  {  alpha=rowdata[x*4+3];  /* RGBA: R=0, G=1, B=2, A=3 */
                  }
                  else
                  {  alpha=rowdata[x*4+0];  /* ARGB: A=0, R=1, G=2, B=3 */
                  }
                  /* Threshold alpha: values >= 128 are opaque, < 128 are transparent */
                  if(alpha>=128)
                  {  maskptr[bytepos]|=(1<<(7-bitpos));  /* Set bit for opaque */
                  }
               }
            }
         }
         y0+=thisrows;
      }
      imp->ourmask=TRUE;
   }
   
   Freemem(pixeldata);
}

/* Make a transparent mask if the dt didn't create it */
static void Makegifmask(struct Imgprocess *imp,long xptcolor)
{  struct BitMap *bmap;
   short bmwidth; /* width of remapped bitmap = width of mask */
   UBYTE *p,*q;
   short d,h,w,c;
   void *object;
   struct gpLayout gpl={0};
   ULONG flags;
   if(object=NewDTObject(imp->ims->filename,
         DTA_SourceType,DTST_FILE,
         DTA_GroupID,GID_PICTURE,
         PDTA_Remap,FALSE,
         PDTA_DestMode,PMODE_V42, /* Use V42 mode for mask generation */
         PDTA_UseFriendBitMap,TRUE,
         OBP_Precision,PRECISION_IMAGE,
         TAG_END))
   {  gpl.MethodID=DTM_PROCLAYOUT;
      gpl.gpl_GInfo=NULL;
      gpl.gpl_Initial=TRUE;
      if(DoMethodA(object,(Msg)&gpl)
      && GetDTAttrs(object,
         PDTA_DestBitMap,&bmap,
         TAG_END)
      && bmap)
      {  flags=GetBitMapAttr(imp->bitmap,BMA_FLAGS);
         bmwidth=GetBitMapAttr(imp->bitmap,BMA_WIDTH)/8;
         if(flags&BMF_INTERLEAVED) bmwidth/=GetBitMapAttr(imp->bitmap,BMA_DEPTH);
         flags=GetBitMapAttr(bmap,BMA_FLAGS);
         if(flags&BMF_STANDARD)
         {  long srcbytesperrow=GetBitMapAttr(imp->bitmap,BMA_WIDTH)/8;
            long dstrows=GetBitMapAttr(bmap,BMA_HEIGHT);
            long dstdepth=GetBitMapAttr(bmap,BMA_DEPTH);
            long dstbytesperrow=GetBitMapAttr(bmap,BMA_WIDTH)/8;
            /* Mask uses source bitmap row size and destination bitmap row count */
            /* This matches AWeb 3.5.00 fix for transparency bug */
            if(imp->mask=ALLOCTYPE(UBYTE,srcbytesperrow*dstrows,
               MEMF_PUBLIC|MEMF_CHIP|MEMF_CLEAR))
            {  imp->memsize+=srcbytesperrow*dstrows;
               for(d=0;d<dstdepth;d++)
               {  if(xptcolor&(1<<d)) c=1;
                  else c=0;
                  for(h=0;h<dstrows;h++)
                  {  /* Access bitmap planes directly - required for reading pixel data */
                     p=bmap->Planes[d]+h*dstbytesperrow;
                     q=imp->mask+h*srcbytesperrow;
                     for(w=0;w<bmwidth;w++)
                     {  if(c) *q++|=~*p++;
                        else *q++|=*p++;
                     }
                  }
               }
               imp->ourmask=TRUE;
            }
         }
      }
      DisposeDTObject(object);
   }
}

/* Create a bitmap from an Amiga icon Image structure */
static BOOL Makebitmapfromicon(struct Imgprocess *imp,struct DiskObject *dob)
{  BOOL result=FALSE;
   struct Image *img;
   struct Image *selimg;
   struct BitMap *tempbitmap;
   struct RastPort temprp;
   UBYTE *maskdata;
   LONG maskrowbytes;
   LONG maskbytesperrow;
   LONG i;
   LONG depth;
   LONG y;
   LONG x;
   UBYTE bit;
   UBYTE *src;
   UBYTE *dst;
   
   if(!dob || !dob->do_Gadget.GadgetRender) return FALSE;
   
   img=dob->do_Gadget.GadgetRender;
   selimg=dob->do_Gadget.SelectRender;
   
   /* Get image dimensions */
   imp->width=img->Width;
   imp->height=img->Height;
   depth=imp->screen ? GetBitMapAttr(imp->screen->RastPort.BitMap,BMA_DEPTH) : 4;
   if(depth<2) depth=4; /* Minimum depth */
   if(depth>8) depth=8; /* Maximum for compatibility */
   imp->depth=depth;
   
   /* Allocate bitmap */
   tempbitmap=AllocBitMap(imp->width,imp->height,depth,
      BMF_CLEAR|BMF_DISPLAYABLE,
      imp->screen ? imp->screen->RastPort.BitMap : NULL);
   if(!tempbitmap) return FALSE;
   
   /* Initialize rastport */
   InitRastPort(&temprp);
   temprp.BitMap=tempbitmap;
   temprp.FgPen=1;
   
   /* Render the icon image into the bitmap */
   DrawImage(&temprp,img,0,0);
   
   /* Store bitmap before creating mask */
   imp->bitmap=tempbitmap;
   imp->memsize=imp->width*imp->height*imp->depth/8;
   result=TRUE; /* Set success flag early */
   
   /* Create mask if select image exists */
   if(selimg)
   {  maskrowbytes=(imp->width+7)/8;
      maskbytesperrow=GetBitMapAttr(tempbitmap,BMA_WIDTH)/8;
      if(maskdata=Allocmem(maskrowbytes*imp->height,MEMF_PUBLIC|MEMF_CLEAR))
      {  struct RastPort maskrp;
         struct BitMap maskbm;
         InitRastPort(&maskrp);
         InitBitMap(&maskbm,1,imp->width,imp->height);
         maskbm.Planes[0]=maskdata;
         maskbm.BytesPerRow=maskrowbytes;
         maskrp.BitMap=&maskbm;
         maskrp.FgPen=1;
         
         /* Render select image to create mask */
         DrawImage(&maskrp,selimg,0,0);
         
         /* Allocate mask buffer matching bitmap row size */
         imp->mask=Allocmem(maskbytesperrow*imp->height,MEMF_PUBLIC|MEMF_CLEAR);
         if(imp->mask)
         {  /* Copy 1-bit mask data, expanding to match bitmap row width */
            src=maskdata;
            for(y=0;y<imp->height;y++)
            {  dst=imp->mask+y*maskbytesperrow;
               /* Copy mask row, padding if necessary */
               for(x=0;x<maskrowbytes && x<maskbytesperrow;x++)
               {  dst[x]=src[x];
               }
               /* Fill remainder with zeros if bitmap row is wider */
               for(x=maskrowbytes;x<maskbytesperrow;x++)
               {  dst[x]=0;
               }
               src+=maskrowbytes;
            }
            imp->ourmask=TRUE;
            imp->memsize+=maskbytesperrow*imp->height;
         }
         Freemem(maskdata);
      }
   }
   
   /* If we failed after allocating bitmap, free it */
   if(!result && tempbitmap)
   {  FreeBitMap(tempbitmap);
      imp->bitmap=NULL;
      imp->memsize=0;
   }
   
   return result;
}

/* ICO file structures */
struct IcoHeader
{  USHORT reserved;  /* Should be 0 */
   USHORT type;      /* 1=ICO, 2=CUR */
   USHORT count;     /* Number of images */
};

struct IcoDirEntry
{  UBYTE width;      /* 0 means 256 */
   UBYTE height;     /* 0 means 256 */
   UBYTE colors;     /* 0 means no palette or 256 colors */
   UBYTE reserved;
   USHORT planes;    /* Usually 1 */
   USHORT bitcount;  /* Bits per pixel */
   ULONG size;       /* Size of image data */
   ULONG offset;     /* Offset to image data */
};

/* BMP/DIB header structures */
struct BmpFileHeader
{  UBYTE signature[2];  /* "BM" */
   ULONG filesize;
   ULONG reserved;
   ULONG dataoffset;
};

struct BmpInfoHeader
{  ULONG size;          /* Size of this header */
   LONG width;
   LONG height;         /* Positive = bottom-up, negative = top-down */
   USHORT planes;       /* Usually 1 */
   USHORT bitcount;     /* Bits per pixel */
   ULONG compression;   /* 0=BI_RGB, 1=BI_RLE8, 2=BI_RLE4, 3=BI_BITFIELDS */
   ULONG imagesize;
   LONG xpelspermeter;
   LONG ypelspermeter;
   ULONG colorsused;
   ULONG colorsimportant;
};

/* Create a bitmap from an ICO file using BMP datatype */
static BOOL Makebitmapfromico(struct Imgprocess *imp)
{  BOOL result=FALSE;
   long fh;
   struct IcoHeader icoheader;
   struct IcoDirEntry *entries=NULL;
   struct IcoDirEntry *bestentry=NULL;
   long bestscore=10000;  /* Lower is better */
   long i;
   UBYTE *imagedata=NULL;
   UBYTE *bmpdata=NULL;
   UBYTE *andmask=NULL;
   long imagesize;
   long bmpsize;
   long maskoffset;
   long maskrowbytes;
   long maskbytesperrow;
   long width,height;
   long bpp;
   long palettesize;
   struct BmpInfoHeader bmpinfo;
   long bmpwidth,bmpheight;
   long bmprowbytes;
   UBYTE *src,*dst;
   long x,y;
   void *dto=NULL;
   struct gpLayout gpl={0};
   void *maskplane=NULL;
   ULONG flags;
   long err;
   
   if(httpdebug)
   {  AwebLog("img","Makebitmapfromico: starting, filename=%s",
         imp->ims->filename?(char *)imp->ims->filename:"NULL");
   }
   
   if(!imp->ims->filename) return FALSE;
   
   fh=Open(imp->ims->filename,MODE_OLDFILE);
   if(!fh) return FALSE;
   
   /* Read ICO header (little-endian format) */
   {  UBYTE headerbuf[6];
      if(Read(fh,headerbuf,6)!=6)
      {  Close(fh);
         return FALSE;
      }
      /* Convert from little-endian to big-endian */
      icoheader.reserved=(USHORT)(headerbuf[0]|(headerbuf[1]<<8));
      icoheader.type=(USHORT)(headerbuf[2]|(headerbuf[3]<<8));
      icoheader.count=(USHORT)(headerbuf[4]|(headerbuf[5]<<8));
   }
   
   /* Check signature: reserved=0, type=1 (ICO) */
   if(httpdebug)
   {  AwebLog("img","Makebitmapfromico: header reserved=%d type=%d count=%d",
         icoheader.reserved,icoheader.type,icoheader.count);
   }
   if(icoheader.reserved!=0 || icoheader.type!=1 || icoheader.count==0)
   {  Close(fh);
      if(httpdebug)
      {  AwebLog("img","Makebitmapfromico: invalid ICO header");
      }
      return FALSE;
   }
   
   /* Limit to reasonable number of entries */
   if(icoheader.count>256) icoheader.count=256;
   
   /* Read directory entries (little-endian format) */
   entries=ALLOCSTRUCT(IcoDirEntry,icoheader.count,0);
   if(!entries)
   {  Close(fh);
      return FALSE;
   }
   
   {  UBYTE entrybuf[16];
      long i;
      for(i=0;i<icoheader.count;i++)
      {  if(Read(fh,entrybuf,16)!=16)
         {  FREE(entries);
            Close(fh);
            return FALSE;
         }
         /* Convert from little-endian to big-endian */
         entries[i].width=entrybuf[0];
         entries[i].height=entrybuf[1];
         entries[i].colors=entrybuf[2];
         entries[i].reserved=entrybuf[3];
         entries[i].planes=(USHORT)(entrybuf[4]|(entrybuf[5]<<8));
         entries[i].bitcount=(USHORT)(entrybuf[6]|(entrybuf[7]<<8));
         entries[i].size=(ULONG)(entrybuf[8]|(entrybuf[9]<<8)|(entrybuf[10]<<16)|(entrybuf[11]<<24));
         entries[i].offset=(ULONG)(entrybuf[12]|(entrybuf[13]<<8)|(entrybuf[14]<<16)|(entrybuf[15]<<24));
      }
   }
   
   /* Find best image: prefer 16x16 or 32x32, otherwise closest */
   for(i=0;i<icoheader.count;i++)
   {  long w=entries[i].width ? entries[i].width : 256;
      long h=entries[i].height ? entries[i].height : 256;
      long score;
      
      /* Prefer 16x16 or 32x32 for favicons */
      if(w==16 && h==16) score=0;
      else if(w==32 && h==32) score=1;
      else if(w==h)
      {  long dw=abs(w-16);
         long dh=abs(h-16);
         score=100+dw+dh;  /* Prefer square, close to 16 */
      }
      else
      {  long dw=abs(w-16);
         long dh=abs(h-16);
         score=1000+dw+dh;
      }
      
      if(score<bestscore)
      {  bestscore=score;
         bestentry=&entries[i];
      }
   }
   
   if(!bestentry)
   {  FREE(entries);
      Close(fh);
      return FALSE;
   }
   
   width=bestentry->width ? bestentry->width : 256;
   height=bestentry->height ? bestentry->height : 256;
   if(httpdebug)
   {  AwebLog("img","Makebitmapfromico: selected image %ldx%ld, dir bitcount=%d, size=%ld, offset=%ld",
         width,height,bestentry->bitcount,bestentry->size,bestentry->offset);
   }
   
   /* Read image data */
   imagesize=bestentry->size;
   if(imagesize<=0 || imagesize>1024*1024)  /* Sanity check: max 1MB */
   {  FREE(entries);
      Close(fh);
      return FALSE;
   }
   
   if(Seek(fh,bestentry->offset,OFFSET_BEGINNING)!=bestentry->offset)
   {  FREE(entries);
      Close(fh);
      return FALSE;
   }
   
   imagedata=Allocmem(imagesize,MEMF_PUBLIC|MEMF_CLEAR);
   if(!imagedata)
   {  FREE(entries);
      Close(fh);
      return FALSE;
   }
   
   if(Read(fh,imagedata,imagesize)!=imagesize)
   {  Freemem(imagedata);
      FREE(entries);
      Close(fh);
      return FALSE;
   }
   
   Close(fh);
   FREE(entries);
   
   /* Check if this is PNG data (PNG signature) */
   if(imagesize>=8 && imagedata[0]==0x89 && imagedata[1]==0x50 &&
      imagedata[2]==0x4e && imagedata[3]==0x47)
   {  /* PNG data in ICO - fall back to DataTypes */
      Freemem(imagedata);
      return FALSE;
   }
   
   /* Parse BMP/DIB header */
   if(imagesize<sizeof(struct BmpInfoHeader))
   {  Freemem(imagedata);
      return FALSE;
   }
   
   /* Read BMP info header (starts immediately in ICO, no file header, little-endian format) */
   {  UBYTE *p=imagedata;
      bmpinfo.size=(ULONG)(p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24));
      bmpinfo.width=(LONG)(p[4]|(p[5]<<8)|(p[6]<<16)|(p[7]<<24));
      bmpinfo.height=(LONG)(p[8]|(p[9]<<8)|(p[10]<<16)|(p[11]<<24));
      bmpinfo.planes=(USHORT)(p[12]|(p[13]<<8));
      bmpinfo.bitcount=(USHORT)(p[14]|(p[15]<<8));
      bmpinfo.compression=(ULONG)(p[16]|(p[17]<<8)|(p[18]<<16)|(p[19]<<24));
      bmpinfo.imagesize=(ULONG)(p[20]|(p[21]<<8)|(p[22]<<16)|(p[23]<<24));
      bmpinfo.xpelspermeter=(LONG)(p[24]|(p[25]<<8)|(p[26]<<16)|(p[27]<<24));
      bmpinfo.ypelspermeter=(LONG)(p[28]|(p[29]<<8)|(p[30]<<16)|(p[31]<<24));
      bmpinfo.colorsused=(ULONG)(p[32]|(p[33]<<8)|(p[34]<<16)|(p[35]<<24));
      bmpinfo.colorsimportant=(ULONG)(p[36]|(p[37]<<8)|(p[38]<<16)|(p[39]<<24));
   }
   
   /* Handle different BMP header sizes */
   if(bmpinfo.size<sizeof(struct BmpInfoHeader))
   {  Freemem(imagedata);
      return FALSE;
   }
   
   bmpwidth=abs(bmpinfo.width);
   bmpheight=abs(bmpinfo.height);
   /* ICO stores BMP height as 2x actual height (XOR mask + AND mask) */
   /* The actual image height is half of the BMP height */
   bpp=bmpinfo.bitcount;
   if(httpdebug)
   {  AwebLog("img","Makebitmapfromico: BMP info header size=%ld, width=%ld, height=%ld (actual=%ld), bitcount=%d, compression=%ld",
         bmpinfo.size,bmpwidth,bmpheight,bmpheight/2,bmpinfo.bitcount,bmpinfo.compression);
   }
   
   /* Calculate palette size */
   palettesize=0;
   if(bpp<=8 && bmpinfo.colorsused==0)
   {  palettesize=(1<<bpp);  /* Full palette */
   }
   else if(bmpinfo.colorsused>0)
   {  palettesize=bmpinfo.colorsused;
   }
   
   /* Calculate image data size (XOR mask only, exclude AND mask) */
   /* BMP height in ICO is 2x actual height, so XOR mask is half */
   /* Actual image height for row calculation */
   /* Row bytes calculation: DWORD-aligned (4-byte boundary) */
   if(bpp==1)
   {  bmprowbytes=((bmpwidth+31)/32)*4;  /* 1-bit: bits aligned to DWORD */
   }
   else if(bpp==4)
   {  bmprowbytes=((bmpwidth+1)/2+3)/4*4;  /* 4-bit: 2 pixels per byte, DWORD aligned */
   }
   else if(bpp==8)
   {  bmprowbytes=(bmpwidth+3)/4*4;  /* 8-bit: bytes aligned to DWORD */
   }
   else if(bpp==16)
   {  bmprowbytes=(bmpwidth*2+3)/4*4;  /* 16-bit: 2 bytes per pixel, DWORD aligned */
   }
   else if(bpp==24)
   {  bmprowbytes=(bmpwidth*3+3)/4*4;  /* 24-bit: 3 bytes per pixel, DWORD aligned */
   }
   else if(bpp==32)
   {  bmprowbytes=bmpwidth*4;  /* 32-bit: 4 bytes per pixel, already DWORD aligned */
   }
   else
   {  bmprowbytes=((bmpwidth*bpp+31)/32)*4;  /* Generic: bits aligned to DWORD */
   }
   maskrowbytes=(bmpwidth+7)/8;  /* 1-bit mask, byte aligned */
   /* XOR mask size is half the BMP height (the other half is AND mask) */
   {  long actualheight=bmpheight/2;
      maskoffset=bmpinfo.size+palettesize*4+bmprowbytes*actualheight;
      /* Size of BMP data (header + palette + XOR mask, excluding AND mask) */
      bmpsize=bmpinfo.size+palettesize*4+bmprowbytes*actualheight;
   }
   if(httpdebug)
   {  AwebLog("img","Makebitmapfromico: palette size=%ld, bmprowbytes=%ld, bmpsize=%ld, maskoffset=%ld",
         palettesize,bmprowbytes,bmpsize,maskoffset);
   }
   
   /* Create complete BMP file: file header + DIB data */
   bmpdata=Allocmem(sizeof(struct BmpFileHeader)+bmpsize,MEMF_PUBLIC|MEMF_CLEAR);
   if(!bmpdata)
   {  Freemem(imagedata);
      return FALSE;
   }
   
   /* Write BMP file header (little-endian format) */
   {  ULONG filesize=sizeof(struct BmpFileHeader)+bmpsize;
      ULONG dataoffset=sizeof(struct BmpFileHeader)+bmpinfo.size+palettesize*4;
      bmpdata[0]='B';
      bmpdata[1]='M';
      bmpdata[2]=(UBYTE)(filesize&0xff);
      bmpdata[3]=(UBYTE)((filesize>>8)&0xff);
      bmpdata[4]=(UBYTE)((filesize>>16)&0xff);
      bmpdata[5]=(UBYTE)((filesize>>24)&0xff);
      bmpdata[6]=0;  /* Reserved */
      bmpdata[7]=0;
      bmpdata[8]=0;
      bmpdata[9]=0;
      bmpdata[10]=(UBYTE)(dataoffset&0xff);
      bmpdata[11]=(UBYTE)((dataoffset>>8)&0xff);
      bmpdata[12]=(UBYTE)((dataoffset>>16)&0xff);
      bmpdata[13]=(UBYTE)((dataoffset>>24)&0xff);
   }
   {  ULONG filesize_le=(ULONG)(bmpdata[2]|(bmpdata[3]<<8)|(bmpdata[4]<<16)|(bmpdata[5]<<24));
      ULONG dataoffset_le=(ULONG)(bmpdata[10]|(bmpdata[11]<<8)|(bmpdata[12]<<16)|(bmpdata[13]<<24));
      if(httpdebug)
      {  AwebLog("img","Makebitmapfromico: BMP file header: signature=%.2s, filesize=%ld, dataoffset=%ld",
            bmpdata,filesize_le,dataoffset_le);
      }
   }
   
   /* Reconstruct BMP info header with correct height for standalone BMP */
   /* ICO stores height as 2x (XOR+AND masks), but standalone BMP needs actual image height */
   {  UBYTE *dib=bmpdata+sizeof(struct BmpFileHeader);
      long actualheight=bmpheight/2;
      ULONG newimagesize;
      
      /* Copy info header from ICO */
      memcpy(dib,imagedata,bmpinfo.size);
      
      /* Fix height field - write actual image height in little-endian */
      /* BMP height is signed: positive = bottom-up (normal), negative = top-down */
      /* ICO always uses bottom-up, so we use positive height */
      {  LONG signedheight=(LONG)actualheight;  /* Ensure positive (bottom-up) */
         dib[8]=(UBYTE)(signedheight&0xff);
         dib[9]=(UBYTE)((signedheight>>8)&0xff);
         dib[10]=(UBYTE)((signedheight>>16)&0xff);
         dib[11]=(UBYTE)((signedheight>>24)&0xff);
      }
      
      /* Update imagesize field if it was set (offset 20-23) */
      if(bmpinfo.imagesize==0 || bmpinfo.imagesize==bmprowbytes*bmpheight)
      {  newimagesize=bmprowbytes*actualheight;
         dib[20]=(UBYTE)(newimagesize&0xff);
         dib[21]=(UBYTE)((newimagesize>>8)&0xff);
         dib[22]=(UBYTE)((newimagesize>>16)&0xff);
         dib[23]=(UBYTE)((newimagesize>>24)&0xff);
      }
      
      /* Copy palette and pixel data */
      /* ICO format: DIB header + palette + XOR mask + AND mask */
      /* BMP format: DIB header + palette + XOR mask (no AND mask) */
      /* Both formats store rows bottom-up, so pixel data order is the same */
      if(bmpinfo.size<bmpsize)
      {  long palettesize_bytes=palettesize*4;
         long pixeldatasize=bmprowbytes*actualheight;
         long total_copy_size=bmpsize-bmpinfo.size;
         
         /* Verify we have enough data in ICO */
         if(bmpinfo.size+total_copy_size<=imagesize)
         {  /* Copy palette and pixel data together (they're contiguous in both formats) */
            memcpy(dib+bmpinfo.size,imagedata+bmpinfo.size,total_copy_size);
            if(httpdebug)
            {  AwebLog("img","Makebitmapfromico: Copied %ld bytes (palette %ld + pixels %ld)",
                  total_copy_size,palettesize_bytes,pixeldatasize);
            }
         }
         else
         {  if(httpdebug)
            {  AwebLog("img","Makebitmapfromico: ERROR - not enough data: need %ld, have %ld",
                  bmpinfo.size+total_copy_size,imagesize);
            }
         }
      }
      
      if(httpdebug)
      {  AwebLog("img","Makebitmapfromico: Reconstructed BMP DIB: size=%ld, width=%ld, height=%ld (was %ld)",
            bmpinfo.size,bmpwidth,actualheight,bmpheight);
      }
   }
   
   /* Debug: Check BMP file structure */
   if(httpdebug)
   {  AwebLog("img","Makebitmapfromico: BMP file start: %02x %02x %02x %02x %02x %02x %02x %02x",
         bmpdata[0],bmpdata[1],bmpdata[2],bmpdata[3],bmpdata[4],bmpdata[5],bmpdata[6],bmpdata[7]);
   }
   {  UBYTE *dib=bmpdata+sizeof(struct BmpFileHeader);
      if(httpdebug)
      {  AwebLog("img","Makebitmapfromico: BMP DIB header: size=%02x%02x%02x%02x, width=%02x%02x%02x%02x, height=%02x%02x%02x%02x",
            dib[0],dib[1],dib[2],dib[3],
            dib[4],dib[5],dib[6],dib[7],
            dib[8],dib[9],dib[10],dib[11]);
      }
   }
   
   /* Extract AND mask for transparency if available */
   if(maskoffset+maskrowbytes*height<=imagesize)
   {  andmask=imagedata+maskoffset;
   }
   
   /* Use BMP datatype to decode the image */
   if(httpdebug)
   {  AwebLog("img","Makebitmapfromico: Creating datatype object, bmpdata=%p, size=%ld",
         bmpdata,sizeof(struct BmpFileHeader)+bmpsize);
   }
   /* Try without picture-specific attributes first to see if datatype can identify BMP */
   dto=NewDTObject(NULL,
         DTA_SourceType,DTST_MEMORY,
         DTA_SourceAddress,bmpdata,
         DTA_SourceSize,sizeof(struct BmpFileHeader)+bmpsize,
         TAG_END);
   if(!dto)
   {  if(httpdebug)
      {  AwebLog("img","Makebitmapfromico: Auto-detect failed (IoErr=%ld), trying with GID_PICTURE",IoErr());
      }
      dto=NewDTObject(NULL,
            DTA_SourceType,DTST_MEMORY,
            DTA_SourceAddress,bmpdata,
            DTA_SourceSize,sizeof(struct BmpFileHeader)+bmpsize,
            DTA_GroupID,GID_PICTURE,
            TAG_END);
   }
   if(dto)
   {  /* Set picture-specific attributes after object creation */
      Asetattrs(dto,
         PDTA_Remap,TRUE,
         PDTA_Screen,imp->screen,
         PDTA_FreeSourceBitMap,TRUE,
         PDTA_DestMode,PMODE_V43,
         PDTA_UseFriendBitMap,TRUE,
         OBP_Precision,PRECISION_IMAGE,
         TAG_END);
      if(httpdebug)
      {  AwebLog("img","Makebitmapfromico: Datatype object created successfully, dto=%p",dto);
      }
      gpl.MethodID=DTM_PROCLAYOUT;
      gpl.gpl_GInfo=NULL;
      gpl.gpl_Initial=TRUE;
      if(httpdebug)
      {  AwebLog("img","Makebitmapfromico: Calling DTM_PROCLAYOUT");
      }
      if(DoMethodA(dto,(Msg)&gpl))
      {  
         if(httpdebug)
         {  AwebLog("img","Makebitmapfromico: DTM_PROCLAYOUT succeeded, getting attributes");
         }
         if(GetDTAttrs(dto,
            DTA_NominalHoriz,&imp->width,
            DTA_NominalVert,&imp->height,
            PDTA_DestBitMap,&imp->bitmap,
            PDTA_MaskPlane,&maskplane,
            TAG_END)
         && imp->bitmap)
         {  
            if(httpdebug)
            {  AwebLog("img","Makebitmapfromico: Got bitmap successfully: %ldx%ld, bitmap=%p, maskplane=%p",
                  imp->width,imp->height,imp->bitmap,maskplane);
            }
            imp->depth=GetBitMapAttr(imp->bitmap,BMA_DEPTH);
            imp->memsize=imp->width*imp->height*imp->depth/8;
            flags=GetBitMapAttr(imp->bitmap,BMA_FLAGS);
            if(httpdebug)
            {  AwebLog("img","Makebitmapfromico: Bitmap depth=%ld, flags=0x%lx",imp->depth,flags);
            }
            
            /* Use AND mask from ICO if datatype didn't provide one */
            if(!maskplane && andmask && (flags&BMF_STANDARD))
            {  maskbytesperrow=GetBitMapAttr(imp->bitmap,BMA_WIDTH)/8;
               if(imp->mask=ALLOCTYPE(UBYTE,maskbytesperrow*height,
                  MEMF_PUBLIC|MEMF_CLEAR))
               {  /* Copy AND mask (inverted for Amiga convention) */
                  for(y=0;y<height && y<bmpheight;y++)
                  {  src=andmask+(bmpheight-1-y)*maskrowbytes;  /* BMP is bottom-up */
                     dst=imp->mask+y*maskbytesperrow;
                     for(x=0;x<maskbytesperrow && x<maskrowbytes;x++)
                     {  dst[x]=~src[x];  /* Invert for Amiga mask convention */
                     }
                  }
                  imp->ourmask=TRUE;
                  imp->memsize+=maskbytesperrow*height;
               }
            }
            else if(maskplane)
            {  imp->mask=maskplane;
               imp->memsize*=2;
            }
            
            imp->dto=dto;  /* Keep datatype object for cleanup */
            /* Store bmpdata pointer so we can free it when datatype is disposed */
            imp->ims->bmpdata=bmpdata;
            imp->ims->bmpdatasize=sizeof(struct BmpFileHeader)+bmpsize;
            bmpdata=NULL;  /* Don't free it here - will be freed in Disposedto */
            result=TRUE;
            if(httpdebug)
            {  AwebLog("img","Makebitmapfromico: Success! result=TRUE");
            }
         }
         else
         {  
            if(httpdebug)
            {  AwebLog("img","Makebitmapfromico: Failed to get bitmap from datatype (bitmap=%p)",imp->bitmap);
            }
            DisposeDTObject(dto);
            dto=NULL;
         }
      }
      else
      {  
         if(httpdebug)
         {  AwebLog("img","Makebitmapfromico: DTM_PROCLAYOUT failed");
         }
         DisposeDTObject(dto);
         dto=NULL;
      }
   }
   else
   {  
      err=IoErr();
      if(httpdebug)
      {  AwebLog("img","Makebitmapfromico: Failed to create datatype object, IoErr()=%ld",err);
      }
      if(err==DTERROR_UNKNOWN_DATATYPE)
      {  if(httpdebug)
         {  AwebLog("img","Makebitmapfromico: Unknown datatype - BMP datatype may not be installed");
         }
      }
      else if(err==ERROR_OBJECT_WRONG_TYPE)
      {  if(httpdebug)
         {  AwebLog("img","Makebitmapfromico: Object wrong type - data may not be recognized as BMP");
         }
      }
      else if(err==ERROR_REQUIRED_ARG_MISSING)
      {  if(httpdebug)
         {  AwebLog("img","Makebitmapfromico: Required argument missing");
         }
      }
      else if(err==DTERROR_COULDNT_OPEN)
      {  if(httpdebug)
         {  AwebLog("img","Makebitmapfromico: Couldn't open data object");
         }
      }
   }
   
   /* Free bmpdata only if we didn't store it for later cleanup */
   if(bmpdata) Freemem(bmpdata);
   Freemem(imagedata);
   
   return result;
}

/* Create a datatype object and the mask */
static BOOL Makeobject(struct Imgprocess *imp)
{  BOOL result=FALSE;
   struct gpLayout gpl={0};
   ULONG flags;
   void *maskplane=NULL;
   long xptcolor=Transparentgif(imp);
   UBYTE *filename;
   UBYTE *ext;
   LONG len;
   struct DiskObject *dob;
   
   filename=imp->ims->filename;
   if(!filename) return FALSE;
   
   /* Check if this is a .info file */
   len=strlen(filename);
   if(len>=5)
   {  ext=filename+len-5;
      if(STRNIEQUAL(ext,".info",5))
      {  /* Try to load as Amiga icon */
         dob=GetDiskObject(filename);
         if(dob)
         {  result=Makebitmapfromicon(imp,dob);
            FreeDiskObject(dob);
            return result;
         }
      }
      else if(len>=4)
      {  ext=filename+len-4;
         if(STRNIEQUAL(ext,".ico",4))
         {  /* Try to load as ICO file */
            if(Makebitmapfromico(imp))
            {  return TRUE;
            }
            /* Fall through to DataTypes if builtin decoder fails */
         }
      }
   }
   
   /* Normal datatypes path */
   if(imp->dto=NewDTObject(imp->ims->filename,
         DTA_SourceType,DTST_FILE,
         DTA_GroupID,GID_PICTURE,
         PDTA_Remap,TRUE,
         PDTA_Screen,imp->screen,
         PDTA_FreeSourceBitMap,TRUE,
         PDTA_DestMode,PMODE_V43, /* Use proper PMODE_V43 constant */
         PDTA_UseFriendBitMap,TRUE,
         OBP_Precision,PRECISION_IMAGE,
         TAG_END))
   {  gpl.MethodID=DTM_PROCLAYOUT;
      gpl.gpl_GInfo=NULL;
      gpl.gpl_Initial=TRUE;
      if(DoMethodA(imp->dto,(Msg)&gpl)
      && GetDTAttrs(imp->dto,
         DTA_NominalHoriz,&imp->width,
         DTA_NominalVert,&imp->height,
         PDTA_DestBitMap,&imp->bitmap,
         PDTA_MaskPlane,&maskplane,
         TAG_END)
      && imp->bitmap)
      {  BOOL hasalpha=FALSE;
         imp->depth=GetBitMapAttr(imp->bitmap,BMA_DEPTH);
         imp->memsize=imp->width*imp->height*imp->depth/8;
         flags=GetBitMapAttr(imp->bitmap,BMA_FLAGS);
         
         /* Check if alpha channel is available */
         if(GetDTAttrs(imp->dto,PDTA_AlphaChannel,&hasalpha,TAG_END) && hasalpha)
         {  /* Alpha channel available - extract it and create mask */
            Makealphamask(imp,imp->dto);
         }
         else if(xptcolor>=0)
         {  /* Fall back to GIF transparency handling */
            if(maskplane)
            {  imp->mask=maskplane;
               imp->memsize*=2;
            }
            else if(flags&BMF_STANDARD)
            {  Makegifmask(imp,xptcolor);
            }
         }
         else if(maskplane)
         {  /* Use mask plane provided by datatype */
            imp->mask=maskplane;
            imp->memsize*=2;
         }
         result=TRUE;
      }
   }
   return result;
}

/* Image processing subtask */
static void Imagetask(struct Imgsource *ims)
{  struct Imgprocess imp={0};
   imp.ims=ims;
   if(!imagetask.procsema || Obtaintasksemaphore(imagetask.procsema))
   {  ObtainSemaphore(&imagetask.screensema);
      imp.screen=imagetask.screen;
      ReleaseSemaphore(&imagetask.screensema);
      if(imp.screen)
      {  Makeobject(&imp);
         Updatetaskattrs(
            AOIMS_Dtobject,imp.dto,
            AOIMS_Bitmap,imp.bitmap,
            AOIMS_Mask,imp.mask,
            AOIMS_Width,imp.width,
            AOIMS_Height,imp.height,
            AOIMS_Depth,imp.depth,
            AOIMS_Memsize,imp.memsize,
            AOIMS_Ourmask,imp.ourmask,
            TAG_END);
      }
      if(imagetask.procsema) ReleaseSemaphore(imagetask.procsema);
   }
}

/*------------------------------------------------------------------------*/

/* Create a new file object if data isn't stored in cache */
static void *Newfile(struct Imgsource *ims,struct Amsrcupdate *ams)
{  void *url,*cache;
   UBYTE *urlname,*ext=NULL,*ctype;
   void *file=NULL;
   url=(void *)Agetattr(ims->source,AOSRC_Url);
   cache=(void *)Agetattr(url,AOURL_Cache);
   urlname=(UBYTE *)Agetattr(url,AOURL_Url);
   if(cache && !usetemp)
   {  ims->filename=(UBYTE *)Agetattr(cache,AOCAC_Name);
      ims->flags|=IMSF_CACHEFILE;
      if(ams) Asetattrs(ams->fetch,AOFCH_Cancellocal,TRUE,TAG_END);
   }
   else if(urlname && (ims->filename=Urllocalfilename(urlname)) && !usetemp)
   {  ims->flags|=IMSF_CACHEFILE;
      if(ams) Asetattrs(ams->fetch,AOFCH_Cancellocal,TRUE,TAG_END);
   }
   else
   {  if(urlname)
      {  ext=Urlfileext(urlname);
      }
      if(!ext)
      {  if(ctype=(UBYTE *)Agetattr(url,AOURL_Contenttype))
         {  if(Isxbm(ctype)) ext=Dupstr("xbm",3);
         }
      }
      file=Anewobject(AOTP_FILE,
         AOFIL_Extension,ext,
         TAG_END);
      if(ext) FREE(ext);
      ims->flags&=~IMSF_CACHEFILE;
   }
   return file;
}

/* Dispose the datatype object and related stuff */
static void Disposedto(struct Imgsource *ims)
{  if(ims->dto)
   {  DisposeDTObject(ims->dto);
      ims->dto=NULL;
   }
   /* Free BMP data if it was allocated for ICO decoder */
   if(ims->bmpdata)
   {  Freemem(ims->bmpdata);
      ims->bmpdata=NULL;
      ims->bmpdatasize=0;
   }
   if(ims->mask && (ims->flags&IMSF_OURMASK))
   {  FREE(ims->mask);
   }
   ims->bitmap=NULL;
   ims->mask=NULL;
   ims->width=0;
   ims->height=0;
   ims->depth=0;
   ims->flags&=~IMSF_OURMASK;
   Asetattrs(ims->source,AOSRC_Memory,0,TAG_END);
}

/* Start processing of image */
static void Startprocessimg(struct Imgsource *ims)
{  ObtainSemaphore(&imagetask.screensema);
   if(!imagetask.screen)
   {  imagetask.screen=(struct Screen *)Agetattr(Aweb(),AOAPP_Screen);
   }
   ReleaseSemaphore(&imagetask.screensema);
   if(!imagetask.procsema)
   {  imagetask.procsema=(struct SignalSemaphore *)Agetattr(Aweb(),AOAPP_Semaphore);
   }
   if(ims->task=Anewobject(AOTP_TASK,
      AOTSK_Entry,Imagetask,
      AOTSK_Name,"AWebIP",
      AOTSK_Userdata,ims,
      AOBJ_Target,ims,
      TAG_END))
   {  Asetattrs(ims->task,AOTSK_Start,TRUE,TAG_END);
   }
}

static void Trydecodeimg(struct Imgsource *ims)
{  if(!ims) return;
   if(ims->bitmap) return;
   if(ims->task) return;
   if(!(ims->flags&IMSF_EOF)) return;
   if(ims->flags&IMSF_ERROR) return;
   if(!Agetattr(Aweb(),AOAPP_Screenvalid)) return;
   Startprocessimg(ims);
}

/*------------------------------------------------------------------------*/

static long Setimgsource(struct Imgsource *ims,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=Amethodas(AOTP_OBJECT,ims,AOM_SET,ams->tags);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            ims->source=(void *)tag->ti_Data;
            break;
         case AOSDV_Savesource:
            if(ims->filename)
            {  Asetattrs((void *)tag->ti_Data,
                  AOFIL_Copyfile,ims->filename,
                  TAG_END);
            }
            break;
         case AOAPP_Screenvalid:
            if(tag->ti_Data)
            {  /* Start processing if we have got all data and are displayed */
               if((ims->flags&IMSF_EOF) && !ims->task
               && Agetattr(ims->source,AOSRC_Displayed))
               {
#if !LAZY_IMAGE_DECODE
                  Startprocessimg(ims);
#endif
               }
            }
            else
            {  ObtainSemaphore(&imagetask.screensema);
               imagetask.screen=NULL;
               ReleaseSemaphore(&imagetask.screensema);
               if(ims->task)
               {  Adisposeobject(ims->task);
                  ims->task=NULL;
               }
               Disposedto(ims);
               Anotifyset(ims->source,AOIMP_Srcupdate,TRUE,TAG_END);
            }
            break;
         case AOSDV_Displayed:
            /* If becoming displayed and no bitmap and screen valid, process. */
            if(tag->ti_Data && (ims->flags&IMSF_EOF) && !ims->task
            && Agetattr(Aweb(),AOAPP_Screenvalid))
            {
#if !LAZY_IMAGE_DECODE
               Startprocessimg(ims);
#endif
            }
            break;
         case AOIMS_Requestdecode:
            if(tag->ti_Data)
            {  ims->flags|=IMSF_DECODEWAIT;
               Trydecodeimg(ims);
            }
            break;
      }
   }
   return result;
}

static struct Imgsource *Newimgsource(struct Amset *ams)
{  struct Imgsource *ims;
   if(ims=Allocobject(AOTP_IMGSOURCE,sizeof(struct Imgsource),ams))
   {  Aaddchild(Aweb(),ims,AOREL_APP_USE_SCREEN);
      Setimgsource(ims,ams);
   }
   return ims;
}

static long Getimgsource(struct Imgsource *ims,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=AmethodasA(AOTP_OBJECT,ims,ams);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            PUTATTR(tag,ims->source);
            break;
         case AOSDV_Saveable:
            PUTATTR(tag,BOOLVAL(ims->filename));
            break;
      }
   }
   return result;
}

static long Updateimgsource(struct Imgsource *ims,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOIMS_Dtobject:
            ims->dto=(void *)tag->ti_Data;
            break;
         case AOIMS_Bitmap:
            ims->bitmap=(struct BitMap *)tag->ti_Data;
            break;
         case AOIMS_Mask:
            ims->mask=(UBYTE *)tag->ti_Data;
            break;
         case AOIMS_Width:
            ims->width=tag->ti_Data;
            break;
         case AOIMS_Height:
            ims->height=tag->ti_Data;
            break;
         case AOIMS_Depth:
            ims->depth=tag->ti_Data;
            break;
         case AOIMS_Ourmask:
            SETFLAG(ims->flags,IMSF_OURMASK,tag->ti_Data);
            break;
         case AOIMS_Memsize:
            Asetattrs(ims->source,AOSRC_Memory,tag->ti_Data,TAG_END);
            break;
      }
   }
   Anotifyset(ims->source,AOIMP_Srcupdate,TRUE,TAG_END);
   /* Layout refresh is driven from copy.c when the image Copy handles
    * AOIMP_Srcupdate (Changedcopy / Changedlayout). Calling Changedlayout
    * here too scheduled Doupdateframes for every decoded bitmap even when
    * the embedded-image fast path only repaints the icon rectangle. */
   return 0;
}

static long Srcupdateimgsource(struct Imgsource *ims,struct Amsrcupdate *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   long length=0;
   UBYTE *data=NULL;
   BOOL eof=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOURL_Data:
            data=(UBYTE *)tag->ti_Data;
            break;
         case AOURL_Datalength:
            length=tag->ti_Data;
            break;
         case AOURL_Reload:
            ims->flags&=~(IMSF_EOF|IMSF_ERROR|IMSF_DECODEWAIT);
            if(ims->task)
            {  Adisposeobject(ims->task);
               ims->task=NULL;
               ims->flags&=~IMSF_CACHEFILE;
            }
            Disposedto(ims);
            break;
         case AOURL_Eof:
            if(tag->ti_Data) eof=TRUE;
            break;
         case AOURL_Error:
            SETFLAG(ims->flags,IMSF_ERROR,tag->ti_Data);
            break;
      }
   }
   if(data && !(ims->flags&IMSF_CACHEFILE))
   {  if(!ims->file) ims->file=Newfile(ims,ams);
      if(ims->file)
      {  Asetattrs(ims->file,
            AOFIL_Data,data,
            AOFIL_Datalength,length,
            TAG_END);
      }
   }
   if(eof && ims->file)
   {  Asetattrs(ims->file,AOFIL_Eof,TRUE,TAG_END);
      ims->filename=(UBYTE *)Agetattr(ims->file,AOFIL_Name);
   }
   if(eof && ims->filename && !(ims->flags&IMSF_ERROR))
   {  ims->flags|=IMSF_EOF;
#if !LAZY_IMAGE_DECODE
      if(!ims->task && Agetattr(Aweb(),AOAPP_Screenvalid))
      {  ObtainSemaphore(&imagetask.screensema);
         if(!imagetask.screen)
         {  imagetask.screen=(struct Screen *)Agetattr(Aweb(),AOAPP_Screen);
         }
         ReleaseSemaphore(&imagetask.screensema);
         Startprocessimg(ims);
      }
#endif
#if LAZY_IMAGE_DECODE
      if(ims->flags&IMSF_DECODEWAIT)
      {  Trydecodeimg(ims);
      }
#endif
   }
   return 0;
}

static long Addchildimgsource(struct Imgsource *ims,struct Amadd *ama)
{  if(ama->relation==AOREL_SRC_COPY)
   {  if(ims->bitmap)
      {  Asetattrs(ama->child,AOIMP_Srcupdate,TRUE,TAG_END);
      }
   }
   return 0;
}

static void Disposeimgsource(struct Imgsource *ims)
{  if(ims->task) Adisposeobject(ims->task);
   Disposedto(ims);
   Asetattrs(ims->source,AOSRC_Memory,0,TAG_END);
   if(ims->file) Adisposeobject(ims->file);
   Aremchild(Aweb(),ims,AOREL_APP_USE_SCREEN);
   Amethodas(AOTP_OBJECT,ims,AOM_DISPOSE);
}

static void Deinstallimgsource(void)
{
}

static long Dispatch(struct Imgsource *ims,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newimgsource((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setimgsource(ims,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getimgsource(ims,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updateimgsource(ims,(struct Amset *)amsg);
         break;
      case AOM_SRCUPDATE:
         result=Srcupdateimgsource(ims,(struct Amsrcupdate *)amsg);
         break;
      case AOM_ADDCHILD:
         result=Addchildimgsource(ims,(struct Amadd *)amsg);
         break;
      case AOM_DISPOSE:
         Disposeimgsource(ims);
         break;
      case AOM_DEINSTALL:
         Deinstallimgsource();
         break;
      default:
         result=AmethodasA(AOTP_OBJECT,ims,amsg);
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installimgsource(void)
{  InitSemaphore(&imagetask.screensema);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_IMGSOURCE,Dispatch)) return FALSE;
   return TRUE;
}
