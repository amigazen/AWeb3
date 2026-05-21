/**********************************************************************
 *
 * This file is part of the AWeb distribution
 *
 * Copyright (C) 2026 amigazen project
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

/* svgcopy.c - AWeb SVG plugin copydriver.
 *
 * The copy driver owns the rendered bitmap inside the layout engine
 * and blits it into each render request.  All real work is done by
 * the source driver; this is mostly accessor and rendering glue. */

#include "pluginlib.h"
#include "awebsvg.h"

#include <libraries/awebplugin.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <proto/awebplugin.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>

struct Svgcopy
{  struct Copydriver copydriver;
   struct Aobject *copy;
   struct BitMap *bitmap;
   UBYTE *mask;
   long width,height;
   USHORT flags;
};

#define SVGCF_DISPLAYED    0x0001
#define SVGCF_READY        0x0002
#define SVGCF_OURBITMAP    0x0004
#define SVGCF_JSREADY      0x0008

#ifdef DEBUG_PLUGINS
#define SVGLOG(args) Aprintf args
#else
#define SVGLOG(args)
#endif

/* Forward decls */
static ULONG Setcopy(struct Svgcopy *sc, struct Amset *amset);

/* Clip the source-space rectangle (x..x+w-1) against the visible
 * (minx..maxx) range. */
static void Clipcopy(long *x, long *dx, long *w, long minx, long maxx)
{  if(minx>*x)
   {  (*dx)+=minx-*x;
      (*w)-=minx-*x;
      *x=minx;
   }
   if(maxx<*x+*w)
   {  *w=maxx-*x+1;
   }
}

/* Blit the (rows-)slice of our bitmap onto the framebuffer. */
static void Renderrows(struct Svgcopy *sc, struct Coords *coo,
   long minx, long miny, long maxx, long maxy, long minrow, long maxrow)
{  long x,y,dx,dy,w,h;
   coo=Clipcoords(sc->copydriver.cframe,coo);
   if(coo && coo->rp && sc->bitmap)
   {  x=sc->copydriver.aox+coo->dx;
      y=sc->copydriver.aoy+coo->dy+minrow;
      dx=0;
      dy=minrow;
      w=sc->width;
      h=maxrow-minrow+1;
      Clipcopy(&x,&dx,&w,coo->minx,coo->maxx);
      Clipcopy(&y,&dy,&h,coo->miny,coo->maxy);
      Clipcopy(&x,&dx,&w,minx+coo->dx,maxx+coo->dx);
      Clipcopy(&y,&dy,&h,miny+coo->dy,maxy+coo->dy);
      if(w>0 && h>0)
      {  if(sc->mask)
            BltMaskBitMapRastPort(sc->bitmap,dx,dy,coo->rp,x,y,w,h,0xe0,sc->mask);
         else
            BltBitMapRastPort(sc->bitmap,dx,dy,coo->rp,x,y,w,h,0xc0);
      }
   }
   Unclipcoords(coo);

   if(sc->flags&SVGCF_JSREADY)
   {  Asetattrs(sc->copy,AOCPY_Onimgload,TRUE,TAG_END);
   }
}

static struct Svgcopy *Newcopy(struct Amset *amset)
{  struct Svgcopy *sc;
   sc=(struct Svgcopy *)Allocobject(PluginBase->copydriver,sizeof(struct Svgcopy),amset);
   if(!sc) return NULL;
   sc->bitmap=NULL;
   sc->mask=NULL;
   sc->width=0;
   sc->height=0;
   sc->flags=0;
   Setcopy(sc,amset);
   return sc;
}

static void Disposecopy(struct Svgcopy *sc)
{  if(sc->flags&SVGCF_OURBITMAP)
   {  if(sc->bitmap) FreeBitMap(sc->bitmap);
      if(sc->mask) FreeVec(sc->mask);
   }
   Amethodas(AOTP_COPYDRIVER,(struct Aobject *)sc,AOM_DISPOSE);
}

static ULONG Measurecopy(struct Svgcopy *sc, struct Ammeasure *ammeasure)
{  if(sc->bitmap && (sc->flags&SVGCF_READY))
   {  sc->copydriver.aow=sc->width;
      sc->copydriver.aoh=sc->height;
      if(ammeasure->ammr)
      {  ammeasure->ammr->width=sc->copydriver.aow;
         ammeasure->ammr->minwidth=sc->copydriver.aow;
      }
   }
   return 0;
}

static ULONG Layoutcopy(struct Svgcopy *sc, struct Amlayout *amlayout)
{  if(sc->bitmap && (sc->flags&SVGCF_READY))
   {  sc->copydriver.aow=sc->width;
      sc->copydriver.aoh=sc->height;
   }
   (void)amlayout;
   return 0;
}

static ULONG Getcopy(struct Svgcopy *sc, struct Amset *amset)
{  struct TagItem *tag,*tstate;
   AmethodasA(AOTP_COPYDRIVER,(struct Aobject *)sc,(struct Amessage *)amset);
   tstate=amset->tags;
   while((tag=NextTagItem(&tstate)))
   {  switch(tag->ti_Tag)
      {  case AOCDV_Ready:
            PUTATTR(tag,sc->bitmap?TRUE:FALSE);
            break;
         case AOCDV_Imagebitmap:
            PUTATTR(tag,(sc->flags&SVGCF_READY)?sc->bitmap:NULL);
            break;
         case AOCDV_Imagemask:
            PUTATTR(tag,(sc->flags&SVGCF_READY)?sc->mask:NULL);
            break;
         case AOCDV_Imagewidth:
            PUTATTR(tag,(sc->bitmap && (sc->flags&SVGCF_READY))?sc->width:0);
            break;
         case AOCDV_Imageheight:
            PUTATTR(tag,(sc->bitmap && (sc->flags&SVGCF_READY))?sc->height:0);
            break;
      }
   }
   return 0;
}

static ULONG Setcopy(struct Svgcopy *sc, struct Amset *amset)
{  struct TagItem *tag,*tstate;
   BOOL newbitmap=FALSE;
   BOOL chgbitmap=FALSE;
   BOOL dimchanged=FALSE;
   Amethodas(AOTP_COPYDRIVER,(struct Aobject *)sc,AOM_SET,amset->tags);
   tstate=amset->tags;
   while((tag=NextTagItem(&tstate)))
   {  switch(tag->ti_Tag)
      {  case AOCDV_Copy:
            sc->copy=(struct Aobject *)tag->ti_Data;
            break;
         case AOCDV_Sourcedriver:
            break;
         case AOCDV_Displayed:
            if(tag->ti_Data)
            {  sc->flags|=SVGCF_DISPLAYED;
               if(sc->bitmap && (sc->flags&SVGCF_READY) && sc->copydriver.cframe)
                  chgbitmap=TRUE;
            }
            else
            {  sc->flags&=~SVGCF_DISPLAYED;
            }
            break;
         case AOSVG_Bitmap:
            sc->bitmap=(struct BitMap *)tag->ti_Data;
            newbitmap=TRUE;
            if(sc->bitmap && (sc->flags&SVGCF_READY)) chgbitmap=TRUE;
            if(!sc->bitmap)
            {  sc->width=0;
               sc->height=0;
               sc->flags&=~SVGCF_READY;
               dimchanged=TRUE;
            }
            break;
         case AOSVG_Mask:
            sc->mask=(UBYTE *)tag->ti_Data;
            break;
         case AOSVG_Width:
            if(tag->ti_Data!=sc->width) dimchanged=TRUE;
            sc->width=tag->ti_Data;
            break;
         case AOSVG_Height:
            if(tag->ti_Data!=sc->height) dimchanged=TRUE;
            sc->height=tag->ti_Data;
            break;
         case AOSVG_Imgready:
            if(tag->ti_Data)
            {  sc->flags|=SVGCF_READY;
               chgbitmap=TRUE;
            }
            else
            {  sc->flags&=~SVGCF_READY;
            }
            break;
         case AOSVG_Jsready:
            if(tag->ti_Data) sc->flags|=SVGCF_JSREADY;
            else             sc->flags&=~SVGCF_JSREADY;
            break;
      }
   }
   if(dimchanged)
   {  Asetattrs(sc->copy,AOBJ_Changedchild,sc,TAG_END);
   }
   if(chgbitmap && (sc->flags&SVGCF_DISPLAYED) && sc->copydriver.cframe
   && sc->bitmap && sc->width>0 && sc->height>0)
   {  Renderrows(sc,NULL,0,0,AMRMAX,AMRMAX,0,sc->height-1);
   }
   else if(chgbitmap && !(sc->flags&SVGCF_DISPLAYED) && (sc->flags&SVGCF_READY))
   {  Asetattrs(sc->copy,AOBJ_Changedchild,sc,TAG_END);
   }
   if(newbitmap && (sc->flags&SVGCF_READY))
   {  Asetattrs(sc->copy,AOCPY_Onimgload,TRUE,TAG_END);
   }
   return 0;
}

static ULONG Rendercopy(struct Svgcopy *sc, struct Amrender *amrender)
{  struct Coords *coo;
   /* Skip selection-only update passes - the layout engine sends these
    * when only a highlight/cursor needs touching, and our bitmap is
    * unchanged.  Repainting here would clobber the layout engine's
    * own selection overlay. */
   if(sc->bitmap && (sc->flags&SVGCF_READY)
   && !(amrender->flags&(AMRF_UPDATESELECTED|AMRF_UPDATENORMAL)))
   {  coo=Clipcoords(sc->copydriver.cframe,amrender->coords);
      if(coo && coo->rp)
      {  /* AMRF_CLEAR means the layout engine wants the cell's
          * background painted before our image goes on top (e.g.
          * scrolling has just made this region visible again). */
         if(amrender->flags&AMRF_CLEAR)
         {  Erasebg(sc->copydriver.cframe,coo,
               amrender->minx,amrender->miny,
               amrender->maxx,amrender->maxy);
         }
         /* Honour the layout engine's damage rectangle.  Renderrows
          * intersects this with both the rastport's visible region
          * and our own bitmap bounds. */
         Renderrows(sc,coo,
            amrender->minx,amrender->miny,
            amrender->maxx,amrender->maxy,
            0,sc->height-1);
      }
      Unclipcoords(coo);
   }
   return 0;
}

__asm __saveds ULONG Dispatchcopy(register __a0 struct Aobject *obj,
                                  register __a1 struct Amessage *amsg)
{  struct Svgcopy *sc=(struct Svgcopy *)obj;
   ULONG result=0;
   switch(amsg->method)
   {  case AOM_NEW:     result=(ULONG)Newcopy((struct Amset *)amsg); break;
      case AOM_DISPOSE: Disposecopy(sc); break;
      case AOM_GET:     result=Getcopy(sc,(struct Amset *)amsg); break;
      case AOM_SET:     result=Setcopy(sc,(struct Amset *)amsg); break;
      case AOM_MEASURE: result=Measurecopy(sc,(struct Ammeasure *)amsg); break;
      case AOM_LAYOUT:  result=Layoutcopy(sc,(struct Amlayout *)amsg); break;
      case AOM_RENDER:  result=Rendercopy(sc,(struct Amrender *)amsg); break;
      default:          result=AmethodasA(AOTP_COPYDRIVER,(struct Aobject *)sc,amsg); break;
   }
   return result;
}
