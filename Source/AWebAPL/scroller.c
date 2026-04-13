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

/* object.c - AWeb re-usable objects */

#include "aweb.h"
#include "scroller.h"
#include "application.h"
#include "window.h"
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <intuition/imageclass.h>
#include <intuition/intuition.h>
#include <graphics/gfxmacros.h>
#include <images/bevel.h>
#include <images/bitmap.h>
#include <images/drawlist.h>
#include <images/glyph.h>
#include <images/label.h>
#include <images/led.h>
#include <images/penmap.h>
#include <gadgets/button.h>
#include <gadgets/checkbox.h>
#include <gadgets/chooser.h>
#include <gadgets/clicktab.h>
#include <gadgets/colorwheel.h>
#include <gadgets/fuelgauge.h>
#include <gadgets/getfile.h>
#include <gadgets/getfont.h>
#include <gadgets/getscreenmode.h>
#include <gadgets/integer.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/listview.h>
#include <gadgets/page.h>
#include <gadgets/radiobutton.h>
#include <gadgets/scroller.h>
#include <gadgets/slider.h>
#include <gadgets/space.h>
#include <gadgets/string.h>
#include <gadgets/tabs.h>
#include <gadgets/virtual.h>
#include <intuition/intuition.h>
#include <intuition/imageclass.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/utility.h>
#include <proto/exec.h>

#include <proto/bevel.h>
#include <proto/layout.h>
#include <proto/string.h>
#include <proto/label.h>
#include <proto/fuelgauge.h>
#include <proto/window.h>
#include <proto/glyph.h>

static struct Image *upimage,*downimage,*leftimage,*rightimage;
static void *bevel;
static long bevelw,bevelh;
static long buttonw,buttonh;
static void *knob;
static long knobw,knobh;

#define VERTWIDTH       buttonw    /* Width of vertical gadgets */
#define VERTHEIGHT      buttonh    /* Height of vertical buttons */
#define HORIZWIDTH      buttonw    /* Width of horizontal buttons */
#define HORIZHEIGHT     buttonh    /* Height of horizontal gadgets */

/*------------------------------------------------------------------------*/

struct Scroller
{  struct Aobject object;
   long aox,aoy,aow,aoh;
   void *cframe;
   void *capens;
   long total,visible,top;
   BOOL horizontal;
   long delta,delay;
   long contsize;             /* inner container size */
   long pos,body;             /* knob pixel sizes within inner container */
   short active;              /* which part is active */
   short grab;                /* distance between knob pos and mouse pointer */
   short wait;                /* current number of ticks to wait before move */
   struct Aobject *target;    /* target object */
   struct TagItem *maplist;
};

#define ACTIVE_KNOB     0x0001
#define ACTIVE_UP       0x0002
#define ACTIVE_LEFT     0x0002
#define ACTIVE_DOWN     0x0004
#define ACTIVE_RIGHT    0x0004
#define ACTIVE_UPDATE   0x0008   /* Automatic render */

#define MAXPOS(s) MAX(((s)->contsize-(s)->body),0)
#define MAXTOP(s) MAX(((s)->total-(s)->visible),0)

static UWORD apat[2]={ 0xaaaa, 0x5555 };

/*------------------------------------------------------------------------*/

/* returns 1 if couldn't render */
static long Renderscroller(struct Scroller *scr,struct Coords *coo,struct Amrender *amr,
   BOOL clip,USHORT update)
{  long result=0;
   struct Coords coords={0};
   struct RastPort *rp;
   struct ColorMap *colormap=(struct ColorMap *)Agetattr(Aweb(),AOAPP_Colormap);
   long x,y;
   USHORT state;
   ULONG clipkey=NULL;
   long bpen=~0;
   if(!coo)
   {  if(amr) coo=amr->coords;
      if(!coo)
      {  Framecoords(scr->cframe,&coords);
         coo=&coords;
         clip=TRUE;
      }
   }
   if(coo->rp && coo->minx<=coo->maxx && coo->miny<=coo->maxy)
   {  rp=coo->rp;
      if(clip) clipkey=Clipto(rp,coo->minx,coo->miny,coo->maxx,coo->maxy);
      x=scr->aox+coo->dx;
      y=scr->aoy+coo->dy;
      if(scr->horizontal)
      {  /* Slider knob bevel */
         if(!update)
         {  SetAttrs(bevel,
               IA_Width,scr->aow-2*HORIZWIDTH-4*bevelw,
               IA_Height,scr->aoh,
               BEVEL_FillPen,~0,
               BEVEL_ColorMap,colormap,
               REACTION_SpecialPens,scr->capens,
               TAG_END);
            DrawImageState(rp,bevel,x,y,IDS_NORMAL,coo->dri);
         }
         /* Slider knob */
         if(!update || (update&(ACTIVE_KNOB|ACTIVE_UPDATE)))
         {  SetAttrs(knob,
               IA_Width,scr->body,
               IA_Height,scr->aoh-2*bevelh-4,
               BEVEL_FillPen,
                  coo->dri->dri_Pens[scr->active==ACTIVE_KNOB?FILLPEN:BACKGROUNDPEN],
               BEVEL_ColorMap,colormap,
               REACTION_SpecialPens,scr->capens,
               TAG_END);
            DrawImageState(rp,knob,x+bevelw+2+scr->pos,y+bevelh+2,
               scr->active==ACTIVE_KNOB?IDS_SELECTED:IDS_NORMAL,coo->dri);
            SetAfPt(rp,apat,1);
            SetABPenDrMd(rp,coo->dri->dri_Pens[TEXTPEN],coo->dri->dri_Pens[BACKGROUNDPEN],JAM2);
            if(scr->pos>0)
            {  RectFill(rp,x+bevelw+2,y+bevelh+2,
                  x+bevelw+2+scr->pos-1,y+scr->aoh-bevelh-3);
            }
            if(scr->pos+scr->body<scr->contsize)
            {  RectFill(rp,x+bevelw+2+scr->pos+scr->body,y+bevelh+2,
                  x+bevelw+2+scr->contsize-1,y+scr->aoh-bevelh-3);
            }
            SetAfPt(rp,NULL,0);
            SetDrMd(rp,JAM1);
         }
         /* Left button */
         if(!update || (update&ACTIVE_LEFT))
         {  if(scr->active==ACTIVE_LEFT)
            {  state=IDS_SELECTED;
               bpen=coo->dri->dri_Pens[FILLPEN];
            }
            else
            {  state=IDS_NORMAL;
               bpen=~0;
            }
            SetAttrs(bevel,
               IA_Width,HORIZWIDTH+2*bevelw,
               IA_Height,HORIZHEIGHT+2*bevelh,
               BEVEL_FillPen,bpen,
               BEVEL_ColorMap,colormap,
               REACTION_SpecialPens,scr->capens,
               TAG_END);
            DrawImageState(rp,bevel,x+scr->aow-2*HORIZWIDTH-4*bevelw,y,state,coo->dri);
            DrawImageState(rp,leftimage,x+scr->aow-2*HORIZWIDTH-3*bevelw,y+bevelh,state,coo->dri);
         }
         /* Right button */
         if(!update || (update&ACTIVE_RIGHT))
         {  if(scr->active==ACTIVE_RIGHT)
            {  state=IDS_SELECTED;
               bpen=coo->dri->dri_Pens[FILLPEN];
            }
            else
            {  state=IDS_NORMAL;
               bpen=~0;
            }
            SetAttrs(bevel,
               IA_Width,HORIZWIDTH+2*bevelw,
               IA_Height,HORIZHEIGHT+2*bevelh,
               BEVEL_FillPen,bpen,
               BEVEL_ColorMap,colormap,
               REACTION_SpecialPens,scr->capens,
               TAG_END);
            DrawImageState(rp,bevel,x+scr->aow-HORIZWIDTH-2*bevelw,y,state,coo->dri);
            DrawImageState(rp,rightimage,x+scr->aow-HORIZWIDTH-bevelw,y+bevelh,state,coo->dri);
         }
      }
      else
      {  /* Slider knob bevel */
         if(!update)
         {  SetAttrs(bevel,
               IA_Width,scr->aow,
               IA_Height,scr->aoh-2*VERTHEIGHT-4*bevelh,
               BEVEL_FillPen,~0,
               BEVEL_ColorMap,colormap,
               REACTION_SpecialPens,scr->capens,
               TAG_END);
            DrawImageState(rp,bevel,x,y,IDS_NORMAL,coo->dri);
         }
         /* Slider knob */
         if(!update || (update&(ACTIVE_KNOB|ACTIVE_UPDATE)))
         {  SetAttrs(knob,
               IA_Width,scr->aow-2*bevelw-4,
               IA_Height,scr->body,
               BEVEL_FillPen,
                  coo->dri->dri_Pens[(scr->active==ACTIVE_KNOB)?FILLPEN:BACKGROUNDPEN],
               BEVEL_ColorMap,colormap,
               REACTION_SpecialPens,scr->capens,
               TAG_END);
            DrawImageState(rp,knob,x+bevelw+2,y+bevelh+2+scr->pos,
               scr->active==ACTIVE_KNOB?IDS_SELECTED:IDS_NORMAL,coo->dri);
            SetAfPt(rp,apat,1);
            SetABPenDrMd(rp,coo->dri->dri_Pens[TEXTPEN],coo->dri->dri_Pens[BACKGROUNDPEN],JAM2);
            if(scr->pos>0)
            {  RectFill(rp,x+bevelw+2,y+bevelh+2,
                  x+scr->aow-bevelw-3,y+bevelh+2+scr->pos-1);
            }
            if(scr->pos+scr->body<scr->contsize)
            {  RectFill(rp,x+bevelw+2,y+bevelh+2+scr->pos+scr->body,
                  x+scr->aow-bevelw-3,y+bevelh+2+scr->contsize-1);
            }
            SetAfPt(rp,NULL,0);
            SetDrMd(rp,JAM1);
         }
         /* Up button */
         if(!update || (update&ACTIVE_UP))
         {  if(scr->active==ACTIVE_UP)
            {  state=IDS_SELECTED;
               bpen=coo->dri->dri_Pens[FILLPEN];
            }
            else
            {  state=IDS_NORMAL;
               bpen=~0;
            }
            SetAttrs(bevel,
               IA_Width,VERTWIDTH+2*bevelw,
               IA_Height,VERTHEIGHT+2*bevelh,
               BEVEL_FillPen,bpen,
               BEVEL_ColorMap,colormap,
               REACTION_SpecialPens,scr->capens,
               TAG_END);
            DrawImageState(rp,bevel,x,y+scr->aoh-2*VERTHEIGHT-4*bevelh,state,coo->dri);
            DrawImageState(rp,upimage,x+bevelw,y+scr->aoh-2*VERTHEIGHT-3*bevelh,state,coo->dri);
         }
         /* Down button */
         if(!update || (update&ACTIVE_DOWN))
         {  if(scr->active==ACTIVE_DOWN)
            {  state=IDS_SELECTED;
               bpen=coo->dri->dri_Pens[FILLPEN];
            }
            else
            {  state=IDS_NORMAL;
               bpen=~0;
            }
            SetAttrs(bevel,
               IA_Width,VERTWIDTH+2*bevelw,
               IA_Height,VERTHEIGHT+2*bevelh,
               BEVEL_FillPen,bpen,
               BEVEL_ColorMap,colormap,
               REACTION_SpecialPens,scr->capens,
               TAG_END);
            DrawImageState(rp,bevel,x,y+scr->aoh-VERTHEIGHT-2*bevelh,state,coo->dri);
            DrawImageState(rp,downimage,x+bevelw,y+scr->aoh-VERTHEIGHT-bevelh,state,coo->dri);
         }
      }
      if(clip) Unclipto(clipkey);
   }
   else result=1;
   return result;
}

/* Set attributes. Returns 1 if redraw needed */
static long Setscroller(struct Scroller *scr,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   BOOL update=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Left:
            scr->aox=tag->ti_Data;
            break;
         case AOBJ_Top:
            scr->aoy=tag->ti_Data;
            break;
         case AOBJ_Width:
            scr->aow=tag->ti_Data;
            break;
         case AOBJ_Height:
            scr->aoh=tag->ti_Data;
            break;
         case AOBJ_Cframe:
            scr->cframe=(void *)tag->ti_Data;
            break;
         case AOBJ_Target:
            scr->target=(struct Aobject *)tag->ti_Data;
            break;
         case AOBJ_Map:
            scr->maplist=(struct TagItem *)tag->ti_Data;
            break;
         case AOBJ_Window:
            if(tag->ti_Data)
            {  scr->capens=(void *)Agetattr((void *)tag->ti_Data,AOWIN_Specialpens);
            }
            else
            {  scr->capens=NULL;
            }
            break;
         case AOSCR_Orient:
            scr->horizontal=BOOLVAL(tag->ti_Data);
            break;
         case AOSCR_Total:
            scr->total=MAX(0,(long)tag->ti_Data);
            break;
         case AOSCR_Visible:
            scr->visible=MAX(0,(long)tag->ti_Data);
            break;
         case AOSCR_Top:
            scr->top=MAX(0,(long)tag->ti_Data);
            break;
         case AOSCR_Delta:
            scr->delta=tag->ti_Data;
            break;
         case AOSCR_Delay:
            scr->delay=tag->ti_Data;
            break;
         case AOSCR_Update:
            update=BOOLVAL(tag->ti_Data);
            break;
      }
   }
   if(scr->horizontal)
   {  scr->aoh=HORIZHEIGHT+2*bevelh;
      scr->contsize=scr->aow-2*HORIZWIDTH-6*bevelw-4;
   }
   else
   {  scr->aow=VERTWIDTH+2*bevelw;
      scr->contsize=scr->aoh-2*VERTHEIGHT-6*bevelh-4;
   }
   if(scr->visible>=scr->total)
   {  scr->top=0;
      scr->pos=0;
      scr->body=scr->contsize;
   }
   else
   {  scr->body=MAX(scr->contsize*scr->visible/scr->total,4);
      if(scr->top>scr->total-scr->visible) scr->top=scr->total-scr->visible;
      scr->pos=scr->top*MAXPOS(scr)/MAXTOP(scr);
   }
   if(update)
   {  Renderscroller(scr,NULL,NULL,TRUE,ACTIVE_UPDATE);
   }
   return 0;
}

static struct Scroller *Newscroller(struct Amset *ams)
{  struct Scroller *scr=Allocobject(AOTP_SCROLLER,sizeof(struct Scroller),ams);
   if(scr)
   {  scr->delta=1;
      scr->delay=1;
      Setscroller(scr,ams);
   }
   return scr;
}

static long Getscroller(struct Scroller *scr,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Left:
            PUTATTR(tag,scr->aox);
            break;
         case AOBJ_Top:
            PUTATTR(tag,scr->aoy);
            break;
         case AOBJ_Width:
            PUTATTR(tag,scr->aow);
            break;
         case AOBJ_Height:
            PUTATTR(tag,scr->aoh);
            break;
         case AOSCR_Top:
            PUTATTR(tag,scr->top);
            break;
         case AOSCR_Minwidth:
            if(scr->horizontal)
            {  PUTATTR(tag,8+2*HORIZWIDTH+6*bevelw);
            }
            else
            {  PUTATTR(tag,scr->aow);
            }
            break;
         case AOSCR_Minheight:
            if(scr->horizontal)
            {  PUTATTR(tag,scr->aoh);
            }
            else
            {  PUTATTR(tag,8+2*VERTHEIGHT+6*bevelh);
            }
            break;
      }
   }
   return 0;
}

static long Movescroller(struct Scroller *scr,struct Ammove *amm)
{  scr->aox+=amm->dx;
   scr->aoy+=amm->dy;
   return 0;
}

static long Hittestscroller(struct Scroller *scr,struct Amhittest *amh)
{  long result;
   if(amh->oldobject==scr)
   {  result=AMHR_OLDHIT;
   }
   else
   {  result=AMHR_NEWHIT;
      if(amh->amhr)
      {  amh->amhr->object=scr;
      }
   }
   return result;
}

/* Shift knob as result of arrow key */
static void Shiftknob(struct Scroller *scr,struct Coords *coo,USHORT update)
{  Asetattrs(scr,AOSCR_Top,scr->top+(scr->active==ACTIVE_LEFT?-scr->delta:scr->delta),TAG_END);
   Renderscroller(scr,NULL,NULL,TRUE,ACTIVE_UPDATE|update);
   Aupdateattrs(scr->target,scr->maplist,AOSCR_Top,scr->top,TAG_END);
}

/* Handle activation for horizontal scroller. Vertical scroller will be remapped */
static ULONG Activate(struct Scroller *scr,struct Coords *coo,long x,long w,long gw,long bw)
{  ULONG result=AMR_NOREUSE;
   if(x<w-2*gw)
   {  if(x<bw+2+scr->pos)
      {  Asetattrs(scr,AOSCR_Top,scr->top-scr->visible+1,TAG_END);
         Renderscroller(scr,coo,NULL,TRUE,ACTIVE_KNOB);
         Aupdateattrs(scr->target,scr->maplist,AOSCR_Top,scr->top,TAG_END);
         result=AMR_CHANGED;
      }
      else if(x>=bw+2+scr->pos+scr->body)
      {  Asetattrs(scr,AOSCR_Top,scr->top+scr->visible-1,TAG_END);
         Renderscroller(scr,coo,NULL,TRUE,ACTIVE_KNOB);
         Aupdateattrs(scr->target,scr->maplist,AOSCR_Top,scr->top,TAG_END);
         result=AMR_CHANGED;
      }
      else
      {  scr->active=ACTIVE_KNOB;
         scr->grab=x-scr->pos;
         result=AMR_ACTIVE;
         Renderscroller(scr,coo,NULL,TRUE,ACTIVE_KNOB);
      }
   }
   else if(x<w-gw)
   {  scr->active=ACTIVE_LEFT;
      Shiftknob(scr,coo,ACTIVE_LEFT);
      scr->wait=2*scr->delay;
      result=AMR_ACTIVE;
   }
   else
   {  scr->active=ACTIVE_RIGHT;
      Shiftknob(scr,coo,ACTIVE_RIGHT);
      scr->wait=2*scr->delay;
      result=AMR_ACTIVE;
   }
   return result;
}

static ULONG Goactive(struct Scroller *scr,struct Aminput *ami)
{  ULONG result=AMR_NOREUSE;
   struct Coords coo={0};
   long x,y;
   Framecoords(scr->cframe,&coo);
   if(ami->imsg)
   {  x=ami->imsg->MouseX-scr->aox-coo.dx;
      y=ami->imsg->MouseY-scr->aoy-coo.dy;
      if(scr->horizontal)
      {  result=Activate(scr,&coo,x,scr->aow,HORIZWIDTH+2*bevelw,bevelw);
      }
      else
      {  result=Activate(scr,&coo,y,scr->aoh,VERTHEIGHT+2*bevelh,bevelh);
      }
   }
   return result;
}

/* Move knob as result of mouse move. Only compute new top and render knob,
 * defer update of our target */
static void Moveknob(struct Scroller *scr,struct Coords *coo,long x)
{  scr->pos=x-scr->grab;
   if(scr->pos>=scr->contsize-scr->body) scr->pos=scr->contsize-scr->body;
   if(scr->pos<0) scr->pos=0;
   if(scr->contsize>scr->body)
   {  scr->top=(scr->pos*MAXTOP(scr)+MAXPOS(scr)/2)/MAXPOS(scr);
   }
   else
   {  scr->top=0;
   }
   Renderscroller(scr,coo,NULL,TRUE,ACTIVE_KNOB);
}

static ULONG Handleinput(struct Scroller *scr,struct Aminput *ami)
{  ULONG result=AMR_NOCARE;
   struct Coords coo={0};
   Framecoords(scr->cframe,&coo);
   if(ami->flags&AMHF_DEFER)
   {  Aupdateattrs(scr->target,scr->maplist,AOSCR_Top,scr->top,TAG_END);
      result=AMR_ACTIVE;
   }
   else if(ami->imsg)
   {  switch(ami->imsg->Class)
      {  case IDCMP_INTUITICKS:
            switch(scr->active)
            {  case ACTIVE_LEFT:
               case ACTIVE_RIGHT:
                  if(--scr->wait<=0)
                  {  Shiftknob(scr,NULL,0);
                     scr->wait=scr->delay;
                  }
                  break;
            }
            result=AMR_ACTIVE;
            break;
         case IDCMP_MOUSEBUTTONS:
            if(ami->imsg->Code==SELECTUP) result=AMR_NOREUSE;
            break;
         case IDCMP_MOUSEMOVE:
            if(scr->active==ACTIVE_KNOB)
            {  if(scr->horizontal)
               {  Moveknob(scr,&coo,ami->imsg->MouseX-scr->aox-coo.dx);
               }
               else
               {  Moveknob(scr,&coo,ami->imsg->MouseY-scr->aoy-coo.dy);
               }
               result=AMR_DEFER;
            }
            else
            {  result=AMR_ACTIVE;
            }
            break;
      }
   }
   return result;
}

static ULONG Goinactive(struct Scroller *scr)
{  short active=scr->active;
   if(scr->active)
   {  scr->active=0;
      Renderscroller(scr,NULL,NULL,TRUE,active);
   }
   return 0;
}

static void Disposescroller(struct Scroller *scr)
{  Amethodas(AOTP_OBJECT,scr,AOM_DISPOSE);
}

static void Deinstall(void)
{  if(upimage)
   {  DisposeObject(upimage);
      upimage=NULL;
   }
   if(downimage)
   {  DisposeObject(downimage);
      downimage=NULL;
   }
   if(leftimage)
   {  DisposeObject(leftimage);
      leftimage=NULL;
   }
   if(rightimage)
   {  DisposeObject(rightimage);
      rightimage=NULL;
   }
   if(knob)
   {  DisposeObject(knob);
      knob=NULL;
   }
   if(bevel)
   {  DisposeObject(bevel);
      bevel=NULL;
   }
}

static long Dispatch(struct Scroller *scr,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newscroller((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setscroller(scr,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getscroller(scr,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposescroller(scr);
         break;
      case AOM_MOVE:
         result=Movescroller(scr,(struct Ammove *)amsg);
         break;
      case AOM_RENDER:
         result=Renderscroller(scr,NULL,(struct Amrender *)amsg,FALSE,0);
         break;
      case AOM_HITTEST:
         result=Hittestscroller(scr,(struct Amhittest *)amsg);
         break;
      case AOM_GOACTIVE:
         result=Goactive(scr,(struct Aminput *)amsg);
         break;
      case AOM_HANDLEINPUT:
         result=Handleinput(scr,(struct Aminput *)amsg);
         break;
      case AOM_GOINACTIVE:
         result=Goinactive(scr);
         break;
      case AOM_UPDATE:
         result=Renderscroller(scr,NULL,(struct Amrender *)amsg,FALSE,scr->active);
         break;
      case AOM_DEINSTALL:
         Deinstall();
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installscroller(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_SCROLLER,Dispatch)) return FALSE;
   /* Safe re-entry and failed-init retry: drop any prior shared Reaction objects. */
   Deinstall();
   if(!(bevel=BevelObject,
      BEVEL_Style,BVS_STANDARD,
      End)) return FALSE;
   GetAttr(BEVEL_VertSize,bevel,(ULONG *)&bevelw);
   GetAttr(BEVEL_HorizSize,bevel,(ULONG *)&bevelh);
   if(!(knob=BevelObject,
      BEVEL_Style,BVS_BUTTON,
      End))
   {  Deinstall();
      return FALSE;
   }
   GetAttr(BEVEL_VertSize,knob,(ULONG *)&knobw);
   GetAttr(BEVEL_HorizSize,knob,(ULONG *)&knobh);
   if(!(upimage=GlyphObject,
      GLYPH_Glyph,GLYPH_UPARROW,
      End))
   {  Deinstall();
      return FALSE;
   }
   if(!(downimage=GlyphObject,
      GLYPH_Glyph,GLYPH_DOWNARROW,
      End))
   {  Deinstall();
      return FALSE;
   }
   if(!(leftimage=GlyphObject,
      GLYPH_Glyph,GLYPH_LEFTARROW,
      End))
   {  Deinstall();
      return FALSE;
   }
   if(!(rightimage=GlyphObject,
      GLYPH_Glyph,GLYPH_RIGHTARROW,
      End))
   {  Deinstall();
      return FALSE;
   }
   return TRUE;
}

BOOL Initscroller(void)
{  void *win;
   /* Firstwindow() returns NULL when there is only one window (e.g. at init) */
   win=Firstwindow();
   if(win)
   {  Agetattrs(win,
         AOWIN_Borderright,&buttonw,
         AOWIN_Borderbottom,&buttonh,
         TAG_END);
   }
   else
   {  buttonw=0;
      buttonh=0;
   }
   buttonw-=2*bevelw;
   buttonw=MAX(buttonw,2*knobw+5);
   buttonh-=2*bevelh;
   buttonh=MAX(buttonh,2*knobh+5);
   SetAttrs(upimage,
      IA_Width,VERTWIDTH,
      IA_Height,VERTHEIGHT,
      TAG_END);
   SetAttrs(downimage,
      IA_Width,VERTWIDTH,
      IA_Height,VERTHEIGHT,
      TAG_END);
   SetAttrs(leftimage,
      IA_Width,HORIZWIDTH,
      IA_Height,HORIZHEIGHT,
      TAG_END);
   SetAttrs(rightimage,
      IA_Width,HORIZWIDTH,
      IA_Height,HORIZHEIGHT,
      TAG_END);
   return TRUE;
}

