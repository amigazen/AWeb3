/**********************************************************************
 * 
 * This file is part of the AWeb-II distribution
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

/* pngcopy.c - AWebpng copydriver */

#include "pluginlib.h"
#include "awebpng.h"
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

struct Pngcopy
{  struct Copydriver copydriver;    /* Our superclass object instance */
   struct Aobject *copy;            /* The AOTP_COPY object we belong to */
   struct BitMap *bitmap;           /* Same as source's bitmap or our own (scaled) */
   UBYTE *mask;                     /* Transparent mask (sources or our own) */
   long width,height;               /* Dimensions of our bitmap */
   long ready;                      /* Last complete row number in bitmap */
   long srcready;                   /* Last complete row in src bitmap */
   long swidth,sheight;             /* Suggested width and height or 0 */
   USHORT flags;                    /* See below */
   struct BitMap *srcbitmap;        /* Our source's bitmap for scaling */
   UBYTE *srcmask;                  /* Our source's mask for scaling */
   long srcwidth,srcheight;         /* Original source's height for scaling */
};

/* Pngcopy flags: */
#define PNGCF_DISPLAYED    0x0001   /* We are allowed to render ourselves
                                     * if we have a context frame */
#define PNGCF_READY        0x0002   /* Image is ready */
#define PNGCF_OURBITMAP    0x0004   /* Bitmap and mask are ours */

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
static void Renderrows(struct Pngcopy *pc,struct Coords *coo,
   long minx,long miny,long maxx,long maxy,long minrow,long maxrow)
{  long x,y;      /* Resulting rastport x,y to blit to */
   long dx,dy;    /* Offset within bitmap to blit from */
   long w,h;      /* Width and height of portion to blit */
   coo=Clipcoords(pc->cframe,coo);
   if(coo && coo->rp)
   {  x=pc->aox+coo->dx;
      y=pc->aoy+coo->dy+minrow;
      dx=0;
      dy=minrow;
      w=pc->width;
      h=maxrow-minrow+1;
      Clipcopy(&x,&dx,&w,coo->minx,coo->maxx);
      Clipcopy(&y,&dy,&h,coo->miny,coo->maxy);
      Clipcopy(&x,&dx,&w,minx+coo->dx,maxx+coo->dx);
      Clipcopy(&y,&dy,&h,miny+coo->dy,maxy+coo->dy);
      if(w>0 && h>0)
      {  if(pc->mask)
         {  BltMaskBitMapRastPort(pc->bitmap,dx,dy,coo->rp,x,y,w,h,0xe0,pc->mask);
         }
         else
         {  BltBitMapRastPort(pc->bitmap,dx,dy,coo->rp,x,y,w,h,0xc0);
         }
      }
   }
   Unclipcoords(coo);
   if(pc->flags&PNGCF_READY)
   {  Asetattrs(pc->copy,AOCPY_Onimgload,TRUE,TAG_END);
   }

   /* For animated PNG images, the situation is different. The image must
    * notify its owning copy (when decoding is complete) every time the
    * animation renders its last frame.
    * This must NOT happen after receiving every AOM_RENDER message (like
    * static images), but only when the animation playback sequence hits
    * the last frame.
    *
    * Probably this is not the location in the source where it would
    * happen, but anyway here is what has to be set:
         Asetattrs(pc->copy,AOCPY_Onimganim,TRUE,TAG_END);
    */
}

/* Set a new bitmap and mask. If scaling required, allocate our own bitmap
 * and mask. */
static void Newbitmap(struct Pngcopy *pc,struct BitMap *bitmap,UBYTE *mask)
{  short width,height,depth;
   ULONG memfchip=0;
   if(pc->flags&PNGCF_OURBITMAP)
   {  if(pc->bitmap) FreeBitMap(pc->bitmap);
      if(pc->mask) FreeVec(pc->mask);
      pc->flags&=~PNGCF_OURBITMAP;
   }
   
   if(bitmap)
   {  if(pc->swidth && !pc->sheight)
      {  pc->sheight=pc->srcheight*pc->swidth/pc->srcwidth;
         if(pc->sheight<1) pc->sheight=1;
      }
      if(pc->sheight && !pc->swidth)
      {  pc->swidth=pc->srcwidth*pc->sheight/pc->srcheight;
         if(pc->swidth<1) pc->swidth=1;
      }
   }
   
   if(bitmap && pc->swidth && pc->sheight
   && (pc->srcwidth!=pc->swidth || pc->srcheight!=pc->sheight))
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
      if(pc->bitmap=AllocBitMap(pc->swidth,pc->sheight,depth,BMF_MINPLANES,bitmap))
      {  pc->flags|=PNGCF_OURBITMAP;
         pc->mask=NULL;
         pc->srcbitmap=bitmap;
         pc->srcmask=mask;
         pc->width=pc->swidth;
         pc->height=pc->sheight;
         
         if(mask)
         {  if(GetBitMapAttr(bitmap,BMA_FLAGS)&BMF_STANDARD)
            {  width=pc->bitmap->BytesPerRow;
               height=pc->bitmap->Rows;
               memfchip=MEMF_CHIP;
            }
            else if(P96Base && p96GetBitMapAttr(pc->bitmap,P96BMA_ISP96))
            {  width=p96GetBitMapAttr(pc->bitmap,P96BMA_WIDTH)/8;
               height=p96GetBitMapAttr(pc->bitmap,P96BMA_HEIGHT);
            }
            else width=0;
            if(width)
            {  pc->mask=(UBYTE *)AllocVec(width*height,memfchip|MEMF_CLEAR);
            }
         }
      }
   }
   
   if(!(pc->flags&PNGCF_OURBITMAP))
   {  pc->bitmap=bitmap;
      pc->mask=mask;
      pc->width=pc->srcwidth;
      pc->height=pc->srcheight;
   }
}

static void Scalebitmap(struct Pngcopy *pc,long sfrom,long sheight,long dfrom)
{  struct BitScaleArgs bsa={0};
   bsa.bsa_SrcX=0;
   bsa.bsa_SrcY=sfrom;
   bsa.bsa_SrcWidth=pc->srcwidth;
   bsa.bsa_SrcHeight=sheight;
   bsa.bsa_DestX=0;
   bsa.bsa_DestY=dfrom;
   bsa.bsa_XSrcFactor=pc->srcwidth;
   bsa.bsa_XDestFactor=pc->width;
   bsa.bsa_YSrcFactor=pc->srcheight;
   bsa.bsa_YDestFactor=pc->height;
   bsa.bsa_SrcBitMap=pc->srcbitmap;
   bsa.bsa_DestBitMap=pc->bitmap;
   bsa.bsa_Flags=0;
   BitMapScale(&bsa);
   if(pc->mask)
   {  struct BitMap sbm={0},dbm={0};
      if(GetBitMapAttr(pc->bitmap,BMA_FLAGS)&BMF_STANDARD)
      {  sbm.BytesPerRow=pc->srcbitmap->BytesPerRow;
         sbm.Rows=pc->srcbitmap->Rows;
         dbm.BytesPerRow=pc->bitmap->BytesPerRow;
         dbm.Rows=pc->bitmap->Rows;
      }
               else if(P96Base)
      {  if(p96GetBitMapAttr(pc->srcbitmap,P96BMA_ISP96))
         {  sbm.BytesPerRow=p96GetBitMapAttr(pc->srcbitmap,P96BMA_WIDTH)/8;
            sbm.Rows=p96GetBitMapAttr(pc->srcbitmap,P96BMA_HEIGHT);
         }
         if(p96GetBitMapAttr(pc->bitmap,P96BMA_ISP96))
         {  dbm.BytesPerRow=p96GetBitMapAttr(pc->bitmap,P96BMA_WIDTH)/8;
            dbm.Rows=p96GetBitMapAttr(pc->bitmap,P96BMA_HEIGHT);
         }
      }
      if(sbm.BytesPerRow && dbm.BytesPerRow)
      {  sbm.Depth=1;
         sbm.Planes[0]=pc->srcmask;
         dbm.Depth=1;
         dbm.Planes[0]=pc->mask;
         bsa.bsa_SrcX=0;
         bsa.bsa_SrcY=sfrom;
         bsa.bsa_SrcWidth=pc->srcwidth;
         bsa.bsa_SrcHeight=sheight;
         bsa.bsa_DestX=0;
         bsa.bsa_DestY=dfrom;
         bsa.bsa_XSrcFactor=pc->srcwidth;
         bsa.bsa_XDestFactor=pc->width;
         bsa.bsa_YSrcFactor=pc->srcheight;
         bsa.bsa_YDestFactor=pc->height;
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

static ULONG Measurecopy(struct Pngcopy *pc,struct Ammeasure *ammeasure)
{  if(pc->bitmap)
   {  pc->aow=pc->width;
      pc->aoh=pc->height;
      if(ammeasure->ammr)
      {  ammeasure->ammr->width=pc->aow;
         ammeasure->ammr->minwidth=pc->aow;
      }
   }
   return 0;
}

static ULONG Layoutcopy(struct Pngcopy *pc,struct Amlayout *amlayout)
{  if(pc->bitmap)
   {  pc->aow=pc->width;
      pc->aoh=pc->height;
   }
   return 0;
}

static ULONG Rendercopy(struct Pngcopy *pc,struct Amrender *amrender)
{  struct Coords *coo;
   if(pc->bitmap && !(amrender->flags&(AMRF_UPDATESELECTED|AMRF_UPDATENORMAL)))
   {  coo=Clipcoords(pc->cframe,amrender->coords);
      if(coo && coo->rp)
      {  if(amrender->flags&AMRF_CLEAR)
         {  Erasebg(pc->cframe,coo,amrender->minx,amrender->miny,
               amrender->maxx,amrender->maxy);
         }
         Renderrows(pc,coo,amrender->minx,amrender->miny,
            amrender->maxx,amrender->maxy,0,pc->ready);
      }
      Unclipcoords(coo);
   }
   return 0;
}

static ULONG Setcopy(struct Pngcopy *pc,struct Amset *amset)
{  struct TagItem *tag,*tstate;
   BOOL newbitmap=FALSE;   /* Remember if we got a new bitmap */
   BOOL chgbitmap=FALSE;   /* Remember if we got a bitmap data change */
   BOOL dimchanged=FALSE;  /* If our dimenasions have changed */
   BOOL rescale=FALSE;     /* Create new scaled bitmap as demanded by owner */
   long readyfrom=0,readyto=-1;
   struct BitMap *bitmap=NULL;
   UBYTE *mask=NULL;
   Amethodas(AOTP_COPYDRIVER,pc,AOM_SET,amset->tags);
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCDV_Copy:
            pc->copy=(struct Aobject *)tag->ti_Data;
            break;
         case AOCDV_Sourcedriver:
            break;
         case AOCDV_Displayed:
            if(tag->ti_Data) pc->flags|=PNGCF_DISPLAYED;
            else pc->flags&=~PNGCF_DISPLAYED;
            break;
         case AOCDV_Width:
            if(tag->ti_Data && tag->ti_Data!=pc->width) rescale=TRUE;
            pc->swidth=tag->ti_Data;
            break;
         case AOCDV_Height:
            if(tag->ti_Data && tag->ti_Data!=pc->height) rescale=TRUE;
            pc->sheight=tag->ti_Data;
            break;
         case AOPNG_Bitmap:
            bitmap=(struct BitMap *)tag->ti_Data;
            newbitmap=TRUE;
            if(!pc->bitmap)
            {  pc->width=0;
               pc->height=0;
               pc->ready=-1;
               pc->flags&=~PNGCF_READY;
               dimchanged=TRUE;
            }
            break;
         case AOPNG_Mask:
            mask=(UBYTE *)tag->ti_Data;
            break;
         case AOPNG_Width:
            if(tag->ti_Data!=pc->srcwidth) dimchanged=TRUE;
            pc->srcwidth=tag->ti_Data;
            break;
         case AOPNG_Height:
            if(tag->ti_Data!=pc->srcheight) dimchanged=TRUE;
            pc->srcheight=tag->ti_Data;
            break;
         case AOPNG_Readyfrom:
            readyfrom=tag->ti_Data;
            chgbitmap=TRUE;
            break;
         case AOPNG_Readyto:
            readyto=tag->ti_Data;
            pc->srcready=readyto;
            chgbitmap=TRUE;
            break;
         case AOPNG_Imgready:
            if(tag->ti_Data) pc->flags|=PNGCF_READY;
            else pc->flags&=~PNGCF_READY;
            chgbitmap=TRUE;
            break;
      }
   }

   /* If a new scaling is requested, set magic so that a new bitmap is allocated and
    * scaled into. */
   if(rescale)
   {  if(!bitmap) bitmap=pc->srcbitmap;
      if(!mask) mask=pc->srcmask;
   }

   if((newbitmap && bitmap!=pc->bitmap) || rescale)
   {  Newbitmap(pc,bitmap,mask);
   }
   if(((chgbitmap && readyto>=readyfrom) || rescale) && (pc->flags&PNGCF_OURBITMAP))
   {  long sfrom,sheight;
      if(pc->flags&PNGCF_READY)
      {  readyfrom=0;
         readyto=pc->height-1;
         sfrom=0;
         sheight=pc->srcheight;
         pc->ready=readyto;
      }
      else
      {  if(rescale)
         {  sfrom=0;
            sheight=pc->srcready+1;
            readyfrom=0;
         }
         else
         {  sfrom=readyfrom;
            sheight=readyto-readyfrom+1;
            if(pc->height>pc->srcheight && sfrom>0)
            {  /* Make sure there is no gap */
               sfrom--;
               sheight++;
            }
            readyfrom=sfrom*pc->height/pc->srcheight;
         }
         readyto=readyfrom+ScalerDiv(sheight,pc->height,pc->srcheight)-1;
         if(rescale) pc->ready=readyto;
      }
      Scalebitmap(pc,sfrom,sheight,readyfrom);
   }

   if(readyto>pc->ready) pc->ready=readyto;

   /* If our dimensions have changed, let our AOTP_COPY object know, and
    * eventually we will receive an AOM_RENDER message. */
   if(dimchanged)
   {  Asetattrs(pc->copy,AOBJ_Changedchild,pc,TAG_END);
   }
   /* If the bitmap was changed but the dimensions stayed the same,
    * and we are allowed to render ourselves, render the new row(s) now. */
   else if(chgbitmap && (pc->flags&PNGCF_DISPLAYED) && pc->cframe)
   {  Renderrows(pc,NULL,0,0,AMRMAX,AMRMAX,readyfrom,readyto);
   }
   /* If we are not allowed to render ourselves, let our AOTP_COPY object
    * know when the bitmap is complete. */
   else if(chgbitmap && !(pc->flags&PNGCF_DISPLAYED) && pc->flags&PNGCF_READY)
   {  Asetattrs(pc->copy,AOBJ_Changedchild,pc,TAG_END);
   }

   if(newbitmap && pc->flags&PNGCF_READY)
   {  Asetattrs(pc->copy,AOCPY_Onimgload,TRUE,TAG_END);
   }

   /* More notes for animations (animated PNG) only:
    *
    * When decoding is completed, you must set both AOCPY_Onimgload (because
    * decoding is completed), and AOCPY_Onimganim (because only now the last
    * animation frame is fully displayed). Setting both attributes is safe,
    * provided this is done during the same invokation of this object's
    * dispatcher.
    *
    * However, if that is more convenient for the implementation, it is
    * allowed to set only AOCPY_Onimgload in this situation, and omit
    * setting of AOCPY_Onimganim this first time.
    *
    * When decoding is complete, you MUST set AOCPY_Onimgload regardless of
    * whether you set AOCPY_Onimganim this first time or not.
    */

   return 0;
}

static struct Pngcopy *Newcopy(struct Amset *amset)
{  struct Pngcopy *pc;
   if(pc=Allocobject(PluginBase->copydriver,sizeof(struct Pngcopy),amset))
   {  pc->ready=-1;
      Setcopy(pc,amset);
   }
   return pc;
}

static ULONG Getcopy(struct Pngcopy *pc,struct Amset *amset)
{  struct TagItem *tag,*tstate;
   AmethodasA(AOTP_COPYDRIVER,pc,amset);
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCDV_Ready:
            PUTATTR(tag,pc->bitmap?TRUE:FALSE);
            break;
         case AOCDV_Imagebitmap:
            PUTATTR(tag,(pc->flags&PNGCF_READY)?pc->bitmap:NULL);
            break;
         case AOCDV_Imagemask:
            PUTATTR(tag,(pc->flags&PNGCF_READY)?pc->mask:NULL);
            break;
         case AOCDV_Imagewidth:
            PUTATTR(tag,(pc->bitmap && pc->flags&PNGCF_READY)?pc->width:0);
            break;
         case AOCDV_Imageheight:
            PUTATTR(tag,(pc->bitmap && pc->flags&PNGCF_READY)?pc->height:0);
            break;
      }
   }
   return 0;
}

static void Disposecopy(struct Pngcopy *pc)
{  if(pc->flags&PNGCF_OURBITMAP)
   {  if(pc->bitmap) FreeBitMap(pc->bitmap);
      if(pc->mask) FreeVec(pc->mask);
   }
   Amethodas(AOTP_COPYDRIVER,pc,AOM_DISPOSE);
}

__saveds __asm ULONG Dispatchcopy(
   register __a0 struct Pngcopy *pc,
   register __a1 struct Amessage *amsg)
{  ULONG result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newcopy((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setcopy(pc,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getcopy(pc,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measurecopy(pc,(struct Ammeasure *)amsg);
         break;
      case AOM_LAYOUT:
         result=Layoutcopy(pc,(struct Amlayout *)amsg);
         break;
      case AOM_RENDER:
         result=Rendercopy(pc,(struct Amrender *)amsg);
         break;
      case AOM_DISPOSE:
         Disposecopy(pc);
         break;
      default:
         result=AmethodasA(AOTP_COPYDRIVER,pc,amsg);
         break;
   }
   return result;
}
