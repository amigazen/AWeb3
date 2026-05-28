/**********************************************************************
 *
 * This file is part of the AWeb distribution
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

/* webpcopy.c - AWebwebp copydriver
 *
 * The WebP copydriver shadows pngcopy.c: it supports an optional
 * transparency mask (because WebP has a full alpha channel that the
 * source driver thresholds into a 1bpp planar mask) and the same
 * scaling code paths.  Comments are largely copied from pngcopy.c -
 * the algorithm is identical, only the tag names change. */

#include "pluginlib.h"
#include "awebwebp.h"
#include <libraries/awebplugin.h>
#include <libraries/Picasso96.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/scale.h>
#include <proto/awebplugin.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/Picasso96.h>

/*--------------------------------------------------------------------*/
/* General data structures                                            */
/*--------------------------------------------------------------------*/

struct Webpcopy
{  struct Copydriver copydriver;    /* Our superclass object instance */
   struct Aobject *copy;            /* The AOTP_COPY object we belong to */
   struct BitMap *bitmap;           /* Same as source's bitmap or our own (scaled) */
   UBYTE *mask;                     /* Transparent mask (source's or our own) */
   long width,height;               /* Dimensions of our bitmap */
   long ready;                      /* Last complete row number in bitmap */
   long srcready;                   /* Last complete row in src bitmap */
   long swidth,sheight;             /* Suggested width and height or 0 */
   USHORT flags;                    /* See below */
   struct BitMap *srcbitmap;        /* Our source's bitmap for scaling */
   UBYTE *srcmask;                  /* Our source's mask for scaling */
   long srcwidth,srcheight;         /* Original source's height for scaling */
};

/* Webpcopy flags: */
#define WEBPCF_DISPLAYED   0x0001   /* We are allowed to render ourselves
                                     * if we have a context frame */
#define WEBPCF_READY       0x0002   /* Image is ready */
#define WEBPCF_OURBITMAP   0x0004   /* Bitmap and mask are ours */

/*--------------------------------------------------------------------*/
/* Misc functions                                                     */
/*--------------------------------------------------------------------*/

/* Limit coordinate (x), offset (dx), width (w) to a region (minx,maxx) */
static void Clipcopy(long *x,long *dx,long *w,long minx,long maxx)
{  if(minx>*x)
   {  (*dx)+=minx-*x;
      (*w)-=minx-*x;
      *x=minx;
   }
   if(maxx<*x+*w)
   {  *w=maxx-*x+1;
   }
}

/* Render these rows of our bitmap. */
static void Renderrows(struct Webpcopy *wc,struct Coords *coo,
   long minx,long miny,long maxx,long maxy,long minrow,long maxrow)
{  long x,y;      /* Resulting rastport x,y to blit to */
   long dx,dy;    /* Offset within bitmap to blit from */
   long w,h;      /* Width and height of portion to blit */
   coo=Clipcoords(wc->cframe,coo);
   if(coo && coo->rp)
   {  x=wc->aox+coo->dx;
      y=wc->aoy+coo->dy+minrow;
      dx=0;
      dy=minrow;
      w=wc->width;
      h=maxrow-minrow+1;
      Clipcopy(&x,&dx,&w,coo->minx,coo->maxx);
      Clipcopy(&y,&dy,&h,coo->miny,coo->maxy);
      Clipcopy(&x,&dx,&w,minx+coo->dx,maxx+coo->dx);
      Clipcopy(&y,&dy,&h,miny+coo->dy,maxy+coo->dy);
      if(w>0 && h>0)
      {  if(wc->mask)
         {  BltMaskBitMapRastPort(wc->bitmap,dx,dy,coo->rp,x,y,w,h,
               0xe0,wc->mask);
         }
         else
         {  BltBitMapRastPort(wc->bitmap,dx,dy,coo->rp,x,y,w,h,0xc0);
         }
      }
   }
   Unclipcoords(coo);
   if(wc->flags&WEBPCF_READY)
   {  Asetattrs(wc->copy,AOCPY_Onimgload,TRUE,TAG_END);
   }
}

/* Set a new bitmap and mask. If scaling required, allocate our own bitmap
 * and mask. */
static void Newbitmap(struct Webpcopy *wc,struct BitMap *bitmap,UBYTE *mask)
{  /* height is only read inside the `if(width)' branch (where both
    * width and height have been written together), but SAS/C 6.58
    * cannot follow that flow and emits Warning 317.  Initialise
    * explicitly to silence the diagnostic. */
   short width=0,height=0,depth=0;
   ULONG memfchip=0;
   if(wc->flags&WEBPCF_OURBITMAP)
   {  if(wc->bitmap) FreeBitMap(wc->bitmap);
      if(wc->mask) FreeVec(wc->mask);
      wc->flags&=~WEBPCF_OURBITMAP;
   }

   if(bitmap)
   {  if(wc->swidth && !wc->sheight)
      {  wc->sheight=wc->srcheight*wc->swidth/wc->srcwidth;
         if(wc->sheight<1) wc->sheight=1;
      }
      if(wc->sheight && !wc->swidth)
      {  wc->swidth=wc->srcwidth*wc->sheight/wc->srcheight;
         if(wc->swidth<1) wc->swidth=1;
      }
   }

   if(bitmap && wc->swidth && wc->sheight
   && (wc->srcwidth!=wc->swidth || wc->srcheight!=wc->sheight))
   {  ULONG src_isp96;
      ULONG arch_depth;
      src_isp96=0UL;
      if(P96Base && p96GetBitMapAttr(bitmap,P96BMA_ISP96))
      {  src_isp96=1UL;
      }
      if(P96Base && src_isp96)
      {  depth=(short)p96GetBitMapAttr(bitmap,P96BMA_DEPTH);
      }
      else
      {  arch_depth=GetBitMapAttr(bitmap,BMA_DEPTH);
         depth=(short)arch_depth;
      }
      if(wc->bitmap=AllocBitMap(wc->swidth,wc->sheight,depth,
            BMF_MINPLANES,bitmap))
      {  wc->flags|=WEBPCF_OURBITMAP;
         wc->mask=NULL;
         wc->srcbitmap=bitmap;
         wc->srcmask=mask;
         wc->width=wc->swidth;
         wc->height=wc->sheight;

         if(mask)
         {  if(GetBitMapAttr(bitmap,BMA_FLAGS)&BMF_STANDARD)
            {  width=wc->bitmap->BytesPerRow;
               height=wc->bitmap->Rows;
               memfchip=MEMF_CHIP;
            }
            else if(P96Base && p96GetBitMapAttr(wc->bitmap,P96BMA_ISP96))
            {  width=p96GetBitMapAttr(wc->bitmap,P96BMA_WIDTH)/8;
               height=p96GetBitMapAttr(wc->bitmap,P96BMA_HEIGHT);
            }
            else width=0;
            if(width)
            {  wc->mask=(UBYTE *)AllocVec(width*height,
                  memfchip|MEMF_CLEAR);
            }
         }
      }
   }

   if(!(wc->flags&WEBPCF_OURBITMAP))
   {  wc->bitmap=bitmap;
      wc->mask=mask;
      wc->width=wc->srcwidth;
      wc->height=wc->srcheight;
   }
}

static void Scalebitmap(struct Webpcopy *wc,long sfrom,long sheight,long dfrom)
{  struct BitScaleArgs bsa={0};
   bsa.bsa_SrcX=0;
   bsa.bsa_SrcY=sfrom;
   bsa.bsa_SrcWidth=wc->srcwidth;
   bsa.bsa_SrcHeight=sheight;
   bsa.bsa_DestX=0;
   bsa.bsa_DestY=dfrom;
   bsa.bsa_XSrcFactor=wc->srcwidth;
   bsa.bsa_XDestFactor=wc->width;
   bsa.bsa_YSrcFactor=wc->srcheight;
   bsa.bsa_YDestFactor=wc->height;
   bsa.bsa_SrcBitMap=wc->srcbitmap;
   bsa.bsa_DestBitMap=wc->bitmap;
   bsa.bsa_Flags=0;
   BitMapScale(&bsa);
   if(wc->mask)
   {  struct BitMap sbm={0},dbm={0};
      if(GetBitMapAttr(wc->bitmap,BMA_FLAGS)&BMF_STANDARD)
      {  sbm.BytesPerRow=wc->srcbitmap->BytesPerRow;
         sbm.Rows=wc->srcbitmap->Rows;
         dbm.BytesPerRow=wc->bitmap->BytesPerRow;
         dbm.Rows=wc->bitmap->Rows;
      }
      else if(P96Base)
      {  if(p96GetBitMapAttr(wc->srcbitmap,P96BMA_ISP96))
         {  sbm.BytesPerRow=p96GetBitMapAttr(wc->srcbitmap,P96BMA_WIDTH)/8;
            sbm.Rows=p96GetBitMapAttr(wc->srcbitmap,P96BMA_HEIGHT);
         }
         if(p96GetBitMapAttr(wc->bitmap,P96BMA_ISP96))
         {  dbm.BytesPerRow=p96GetBitMapAttr(wc->bitmap,P96BMA_WIDTH)/8;
            dbm.Rows=p96GetBitMapAttr(wc->bitmap,P96BMA_HEIGHT);
         }
      }
      if(sbm.BytesPerRow && dbm.BytesPerRow)
      {  sbm.Depth=1;
         sbm.Planes[0]=wc->srcmask;
         dbm.Depth=1;
         dbm.Planes[0]=wc->mask;
         bsa.bsa_SrcX=0;
         bsa.bsa_SrcY=sfrom;
         bsa.bsa_SrcWidth=wc->srcwidth;
         bsa.bsa_SrcHeight=sheight;
         bsa.bsa_DestX=0;
         bsa.bsa_DestY=dfrom;
         bsa.bsa_XSrcFactor=wc->srcwidth;
         bsa.bsa_XDestFactor=wc->width;
         bsa.bsa_YSrcFactor=wc->srcheight;
         bsa.bsa_YDestFactor=wc->height;
         bsa.bsa_SrcBitMap=&sbm;
         bsa.bsa_DestBitMap=&dbm;
         bsa.bsa_Flags=0;
         BitMapScale(&bsa);
      }
   }
}

/*--------------------------------------------------------------------*/
/* Plugin copydriver object dispatcher functions                      */
/*--------------------------------------------------------------------*/

static ULONG Measurecopy(struct Webpcopy *wc,struct Ammeasure *ammeasure)
{  if(wc->bitmap)
   {  wc->aow=wc->width;
      wc->aoh=wc->height;
      if(ammeasure->ammr)
      {  ammeasure->ammr->width=wc->aow;
         ammeasure->ammr->minwidth=wc->aow;
      }
   }
   return 0;
}

static ULONG Layoutcopy(struct Webpcopy *wc,struct Amlayout *amlayout)
{  if(wc->bitmap)
   {  wc->aow=wc->width;
      wc->aoh=wc->height;
   }
   return 0;
}

static ULONG Rendercopy(struct Webpcopy *wc,struct Amrender *amrender)
{  struct Coords *coo;
   if(wc->bitmap
      && !(amrender->flags&(AMRF_UPDATESELECTED|AMRF_UPDATENORMAL)))
   {  coo=Clipcoords(wc->cframe,amrender->coords);
      if(coo && coo->rp)
      {  if(amrender->flags&AMRF_CLEAR)
         {  Erasebg(wc->cframe,coo,amrender->minx,amrender->miny,
               amrender->maxx,amrender->maxy);
         }
         Renderrows(wc,coo,amrender->minx,amrender->miny,
            amrender->maxx,amrender->maxy,0,wc->ready);
      }
      Unclipcoords(coo);
   }
   return 0;
}

static ULONG Setcopy(struct Webpcopy *wc,struct Amset *amset)
{  struct TagItem *tag,*tstate;
   BOOL newbitmap=FALSE;
   BOOL chgbitmap=FALSE;
   BOOL dimchanged=FALSE;
   BOOL rescale=FALSE;
   long readyfrom=0,readyto=-1;
   struct BitMap *bitmap=NULL;
   UBYTE *mask=NULL;
   Amethodas(AOTP_COPYDRIVER,wc,AOM_SET,amset->tags);
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCDV_Copy:
            wc->copy=(struct Aobject *)tag->ti_Data;
            break;
         case AOCDV_Sourcedriver:
            break;
         case AOCDV_Displayed:
            if(tag->ti_Data) wc->flags|=WEBPCF_DISPLAYED;
            else wc->flags&=~WEBPCF_DISPLAYED;
            break;
         case AOCDV_Width:
            if(tag->ti_Data && tag->ti_Data!=wc->width) rescale=TRUE;
            wc->swidth=tag->ti_Data;
            break;
         case AOCDV_Height:
            if(tag->ti_Data && tag->ti_Data!=wc->height) rescale=TRUE;
            wc->sheight=tag->ti_Data;
            break;
         case AOWEBP_Bitmap:
            bitmap=(struct BitMap *)tag->ti_Data;
            newbitmap=TRUE;
            if(!wc->bitmap)
            {  wc->width=0;
               wc->height=0;
               wc->ready=-1;
               wc->flags&=~WEBPCF_READY;
               dimchanged=TRUE;
            }
            break;
         case AOWEBP_Mask:
            mask=(UBYTE *)tag->ti_Data;
            break;
         case AOWEBP_Width:
            if(tag->ti_Data!=wc->srcwidth) dimchanged=TRUE;
            wc->srcwidth=tag->ti_Data;
            break;
         case AOWEBP_Height:
            if(tag->ti_Data!=wc->srcheight) dimchanged=TRUE;
            wc->srcheight=tag->ti_Data;
            break;
         case AOWEBP_Readyfrom:
            readyfrom=tag->ti_Data;
            chgbitmap=TRUE;
            break;
         case AOWEBP_Readyto:
            readyto=tag->ti_Data;
            wc->srcready=readyto;
            chgbitmap=TRUE;
            break;
         case AOWEBP_Imgready:
            if(tag->ti_Data) wc->flags|=WEBPCF_READY;
            else wc->flags&=~WEBPCF_READY;
            chgbitmap=TRUE;
            break;
      }
   }

   if(rescale)
   {  if(!bitmap) bitmap=wc->srcbitmap;
      if(!mask) mask=wc->srcmask;
   }

   if((newbitmap && bitmap!=wc->bitmap) || rescale)
   {  Newbitmap(wc,bitmap,mask);
   }
   if(((chgbitmap && readyto>=readyfrom) || rescale)
      && (wc->flags&WEBPCF_OURBITMAP))
   {  long sfrom,sheight;
      if(wc->flags&WEBPCF_READY)
      {  readyfrom=0;
         readyto=wc->height-1;
         sfrom=0;
         sheight=wc->srcheight;
         wc->ready=readyto;
      }
      else
      {  if(rescale)
         {  sfrom=0;
            sheight=wc->srcready+1;
            readyfrom=0;
         }
         else
         {  sfrom=readyfrom;
            sheight=readyto-readyfrom+1;
            if(wc->height>wc->srcheight && sfrom>0)
            {  sfrom--;
               sheight++;
            }
            readyfrom=sfrom*wc->height/wc->srcheight;
         }
         readyto=readyfrom+ScalerDiv(sheight,wc->height,wc->srcheight)-1;
         if(rescale) wc->ready=readyto;
      }
      Scalebitmap(wc,sfrom,sheight,readyfrom);
   }

   if(readyto>wc->ready) wc->ready=readyto;

   if(dimchanged)
   {  Asetattrs(wc->copy,AOBJ_Changedchild,wc,TAG_END);
   }
   else if(chgbitmap && (wc->flags&WEBPCF_DISPLAYED) && wc->cframe)
   {  Renderrows(wc,NULL,0,0,AMRMAX,AMRMAX,readyfrom,readyto);
   }
   else if(chgbitmap && !(wc->flags&WEBPCF_DISPLAYED)
         && wc->flags&WEBPCF_READY)
   {  Asetattrs(wc->copy,AOBJ_Changedchild,wc,TAG_END);
   }

   if(newbitmap && wc->flags&WEBPCF_READY)
   {  Asetattrs(wc->copy,AOCPY_Onimgload,TRUE,TAG_END);
   }

   return 0;
}

static struct Webpcopy *Newcopy(struct Amset *amset)
{  struct Webpcopy *wc;
   if(wc=Allocobject(PluginBase->copydriver,sizeof(struct Webpcopy),amset))
   {  wc->ready=-1;
      Setcopy(wc,amset);
   }
   return wc;
}

static ULONG Getcopy(struct Webpcopy *wc,struct Amset *amset)
{  struct TagItem *tag,*tstate;
   AmethodasA(AOTP_COPYDRIVER,wc,amset);
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCDV_Ready:
            PUTATTR(tag,wc->bitmap?TRUE:FALSE);
            break;
         case AOCDV_Imagebitmap:
            PUTATTR(tag,(wc->flags&WEBPCF_READY)?wc->bitmap:NULL);
            break;
         case AOCDV_Imagemask:
            PUTATTR(tag,(wc->flags&WEBPCF_READY)?wc->mask:NULL);
            break;
         case AOCDV_Imagewidth:
            PUTATTR(tag,(wc->bitmap && wc->flags&WEBPCF_READY)
               ?wc->width:0);
            break;
         case AOCDV_Imageheight:
            PUTATTR(tag,(wc->bitmap && wc->flags&WEBPCF_READY)
               ?wc->height:0);
            break;
      }
   }
   return 0;
}

static void Disposecopy(struct Webpcopy *wc)
{  if(wc->flags&WEBPCF_OURBITMAP)
   {  if(wc->bitmap) FreeBitMap(wc->bitmap);
      if(wc->mask) FreeVec(wc->mask);
   }
   Amethodas(AOTP_COPYDRIVER,wc,AOM_DISPOSE);
}

__saveds __asm ULONG Dispatchcopy(
   register __a0 struct Webpcopy *wc,
   register __a1 struct Amessage *amsg)
{  ULONG result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newcopy((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setcopy(wc,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getcopy(wc,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measurecopy(wc,(struct Ammeasure *)amsg);
         break;
      case AOM_LAYOUT:
         result=Layoutcopy(wc,(struct Amlayout *)amsg);
         break;
      case AOM_RENDER:
         result=Rendercopy(wc,(struct Amrender *)amsg);
         break;
      case AOM_DISPOSE:
         Disposecopy(wc);
         break;
      default:
         result=AmethodasA(AOTP_COPYDRIVER,wc,amsg);
         break;
   }
   return result;
}
