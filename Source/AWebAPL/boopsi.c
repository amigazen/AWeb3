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

/* boopsi.c aweb boopsi classes */

#include "aweb.h"
#include "application.h"
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
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
#include <intuition/classes.h>
#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/bevel.h>
#include <proto/layout.h>
#include <proto/string.h>
#include <proto/label.h>
#include <proto/fuelgauge.h>
#include <proto/window.h>


static Class *gadimgcls,*stagadcls,*ledgadcls;

struct ClassLibrary *BoingBallBase = NULL;

#define GA(o)  ((struct Gadget *)(o))

/*-----------------------------------------------------------------------*/
/* Gadimgcls - Image from 2-plane bitmap with select image */

struct Gadimgdata
{  UWORD *data1,*data2;
};

static ULONG NewGadimgcls(Class *cl,Object *o,struct TagItem *tags)
{  ULONG retval=0;
   UWORD *data1,*data2;
   struct DrawInfo *dri=(struct DrawInfo *)GetTagData(SYSIA_DrawInfo,NULL,tags);
   short width=GetTagData(IA_Width,0,tags);
   short height=GetTagData(IA_Height,0,tags);
   UWORD *src1=(UWORD *)GetTagData(IA_Data,NULL,tags);
   UWORD *src2;
   short wordwidth=(width+15)/16;
   short w,h,d,n,m;
   struct TagItem *imgtags=AllocateTagItems(5);
   struct Gadimgdata *bopdata;
   if(dri && width && height && src1 && imgtags)
   {  if(data1=ALLOCTYPE(UWORD,2*wordwidth*height*dri->dri_Depth,
         MEMF_CHIP|MEMF_CLEAR))
      {  src2=src1+wordwidth*height;
         data2=data1+wordwidth*height*dri->dri_Depth;
         n=0;
         for(d=0;d<dri->dri_Depth;d++)
         {  for(h=0;h<height;h++)
            {  for(w=0;w<wordwidth;w++)
               {  m=wordwidth*h+w;
                  if((1<<d)&dri->dri_Pens[SHADOWPEN]) data1[n]|=src1[m];
                  if((1<<d)&dri->dri_Pens[SHINEPEN]) data1[n]|=src2[m];
                  if((1<<d)&dri->dri_Pens[FILLPEN]) data2[n]|=0xffff;
                  if((1<<d)&dri->dri_Pens[SHINEPEN]) data2[n]|=src1[m];
                  else data2[n]&=~src1[m];
                  if((1<<d)&dri->dri_Pens[SHADOWPEN]) data2[n]|=src2[m];
                  else data2[n]&=~src2[m];
                  n++;
               }
            }
         }
         imgtags[0].ti_Tag=IA_Width;
         imgtags[0].ti_Data=width;
         imgtags[1].ti_Tag=IA_Height;
         imgtags[1].ti_Data=height;
         imgtags[2].ti_Tag=IA_Data;
         imgtags[2].ti_Data=(ULONG)data1;
         imgtags[3].ti_Tag=IA_FGPen;
         imgtags[3].ti_Data=0xff;
         imgtags[4].ti_Tag=TAG_END;
         if(retval=DoSuperMethod(cl,o,OM_NEW,imgtags,NULL))
         {  bopdata=INST_DATA(cl,retval);
            bopdata->data1=data1;
            bopdata->data2=data2;
         }
         else FREE(data1);
      }
   }
   if(imgtags) FreeTagItems(imgtags);
   return retval;
}

static __asm __saveds ULONG DispatchGadimgcls(register __a0 Class *cl,
   register __a2 Object *o,register __a1 Msg msg)
{  ULONG retval=0;
   struct Gadimgdata *data;
   switch(msg->MethodID)
   {  case OM_NEW:
         retval=NewGadimgcls(cl,o,((struct opSet *)msg)->ops_AttrList);
         break;
      case IM_DRAW:
         data=INST_DATA(cl,o);
         switch(((struct impDraw *)msg)->imp_State)
         {  case IDS_SELECTED:
            case IDS_INACTIVESELECTED:
               ((struct Image *)o)->ImageData=data->data2;
               break;
            default:
               ((struct Image *)o)->ImageData=data->data1;
         }
         retval=DoSuperMethodA(cl,o,msg);
         break;
      case OM_DISPOSE:
         data=INST_DATA(cl,o);
         if(data->data1) FREE(data->data1);
         retval=DoSuperMethodA(cl,o,msg);
         break;
      default:
         retval=DoSuperMethodA(cl,o,msg);
   }
   return retval;
}

static Class *InitGadimgcls(void)
{  Class *cls;
   if(cls=MakeClass(NULL,"imageclass",NULL,sizeof(struct Gadimgdata),0))
   {  cls->cl_Dispatcher.h_Entry=(HOOKFUNC)DispatchGadimgcls;
   }
   return cls;
}

/*-----------------------------------------------------------------------*/

/* Stagadcls - status gadget, progress bar, framed with relwidth support */

struct Stagaddata
{  struct Image *frameimg;
   short bevelw,bevelh;
   UBYTE *text;            /* allocated copy */
   UBYTE *hptext;          /* allocated copy */
   long hplen1,hplen2;
   long textlength;
   long hptextlength;
   long total,visible;
   void *capens;
};

static ULONG NewStagadcls(Class *cl,Object *o,struct TagItem *tags)
{  ULONG retval=0;
   struct Stagaddata *data;
   void *frameimg;
   struct TagItem *imgtag=AllocateTagItems(5);
   void *colormap=(void *)Agetattr(Aweb(),AOAPP_Colormap);
   if(imgtag)
   {  imgtag[0].ti_Tag=BEVEL_Style;
      imgtag[0].ti_Data=BVS_BUTTON;
      imgtag[1].ti_Tag=IA_Recessed;
      imgtag[1].ti_Data=TRUE;
      imgtag[2].ti_Tag=BEVEL_Transparent;
      imgtag[2].ti_Data=TRUE;
      imgtag[3].ti_Tag=BEVEL_ColorMap;
      imgtag[3].ti_Data=(ULONG)colormap;
      imgtag[4].ti_Tag=TAG_END;
      if(frameimg=NewObjectA(BEVEL_GetClass(),NULL,imgtag))
      {  if(retval=DoSuperMethod(cl,o,OM_NEW,tags,NULL))
         {  data=INST_DATA(cl,retval);
            data->frameimg=frameimg;
            data->bevelw=Getvalue(frameimg,BEVEL_VertSize);
            data->bevelh=Getvalue(frameimg,BEVEL_HorizSize);
            data->text=Dupstr((UBYTE *)GetTagData(GA_Text,NULL,tags),-1);
            if(data->text) data->textlength=strlen(data->text);
            data->hptext=Dupstr((UBYTE *)GetTagData(STATGA_HPText,NULL,tags),-1);
            if(data->hptext) data->hptextlength=strlen(data->hptext);
            data->total=GetTagData(PGA_Total,0,tags);
            data->visible=GetTagData(PGA_Visible,0,tags);
            data->capens=(void *)GetTagData(STATGA_SpecialPens,NULL,tags);
         }
         else DisposeObject(frameimg);
      }
      FreeTagItems(imgtag);
   }
   return retval;
}

static ULONG RenderStagadcls(Class *cl,Object *o,struct gpRender *gpr)
{  struct Stagaddata *data;
   ULONG retval=0;
   struct Gadget *g=(struct Gadget *)o;
   struct impDraw imp={0};
   struct DrawInfo *dri=gpr->gpr_GInfo->gi_DrInfo;
   struct RastPort *rp=gpr->gpr_RPort;
   long fill=0;
   UBYTE *text;
   long textlength=0,l1,l2,l3,txw;
   data=INST_DATA(cl,o);
   imp.MethodID=IM_DRAWFRAME;
   imp.imp_RPort=gpr->gpr_RPort;
   imp.X=g->LeftEdge;
   if(g->Flags&GFLG_RELRIGHT) imp.X+=gpr->gpr_GInfo->gi_Domain.Width-1;
   imp.Y=g->TopEdge;
   if(g->Flags&GFLG_RELBOTTOM) imp.Y+=gpr->gpr_GInfo->gi_Domain.Height-1;
   imp.imp_State=IDS_NORMAL;
   imp.imp_DrInfo=gpr->gpr_GInfo->gi_DrInfo;
   imp.Width=g->Width;
   if(g->Flags&GFLG_RELWIDTH) imp.Width+=gpr->gpr_GInfo->gi_Domain.Width;
   imp.Height=g->Height;
   if(g->Flags&GFLG_RELHEIGHT) imp.Height+=gpr->gpr_GInfo->gi_Domain.Height;
   SetAttrs((Object *)data->frameimg,
      REACTION_SpecialPens,data->capens,
      TAG_END);
   DoMethodA((Object *)data->frameimg,&imp);
   if(data->total && data->visible)
   {  long width=imp.Width-2*data->bevelw;
      if(data->visible>data->total) data->visible=data->total;
      if(width>0 && data->visible>0x7fffffff/width)
      {  fill=(data->visible>>8)*width/(data->total>>8);
      }
      else
      {  fill=data->visible*width/data->total;
      }
      if(fill>0)
      {  if(dri) SetAPen(gpr->gpr_RPort,dri->dri_Pens[FILLPEN]);
         RectFill(gpr->gpr_RPort,imp.X+data->bevelw,imp.Y+data->bevelh,
            imp.X+data->bevelw+fill-1,imp.Y+imp.Height-2*data->bevelh);
      }
   }
   if(fill>=0 && fill<imp.Width-2*data->bevelw)
   {  if(dri) SetAPen(gpr->gpr_RPort,dri->dri_Pens[BACKGROUNDPEN]);
      RectFill(gpr->gpr_RPort,imp.X+data->bevelw+fill,imp.Y+data->bevelh,
         imp.X+imp.Width-data->bevelw-1,imp.Y+imp.Height-2*data->bevelh);
   }
   if(data->hptextlength)
   {  text=data->hptext;
      textlength=data->hptextlength;
      l1=data->hplen1;
      l2=data->hplen2;
   }
   else if(data->textlength)
   {  text=data->text;
      textlength=data->textlength;
      l1=l2=0;
   }
   if(textlength)
   {  struct TextExtent te={0};
      ULONG fit;
      l3=textlength-l1-l2;
      txw=imp.Width-2*data->bevelw-4;
      if(dri) SetABPenDrMd(rp,dri->dri_Pens[TEXTPEN],0,JAM1);
      Move(rp,imp.X+data->bevelw+2,
         imp.Y+data->bevelh+rp->TxBaseline+(imp.Height-2*data->bevelh-rp->TxHeight)/2);
      fit=TextFit(rp,text+l1,l2,&te,NULL,1,txw,imp.Height-2*data->bevelh);
      txw-=TextLength(rp,text+l1,fit);
      if(fit>=l2)
      {  fit=TextFit(rp,text,l1,&te,NULL,1,txw,imp.Height-2*data->bevelh);
         if(fit>=l1)
         {  txw-=TextLength(rp,text,fit);
            Text(rp,text,fit);
         }
         else
         {  fit=TextFit(rp,"...",3,&te,NULL,1,txw,imp.Height-2*data->bevelh);
            txw-=TextLength(rp,"...",fit);
            if(fit>=3)
            {  fit=TextFit(rp,text,l1,&te,NULL,1,txw,imp.Height-2*data->bevelh);
               txw-=TextLength(rp,text,fit);
               Text(rp,text,fit);
               fit=3;
            }
            Text(rp,"...",fit);
         }
         fit=l2;
      }
      Text(rp,text+l1,fit);
      if(l3>0)
      {  fit=TextFit(rp,text+l1+l2,l3,&te,NULL,1,txw,imp.Height-2*data->bevelh);
         Text(rp,text+l1+l2,fit);
      }
   }
   return retval;
}

static __asm __saveds ULONG DispatchStagadcls(register __a0 Class *cl,
   register __a2 Object *o,register __a1 Msg msg)
{  struct Stagaddata *data;
   ULONG retval=0;
   struct gpRender *gpr=(struct gpRender *)msg;
   struct opSet *ops=(struct opSet *)msg;
   struct opGet *opg=(struct opGet *)msg;
   struct gpDomain *gpd=(struct gpDomain *)msg;
   struct TagItem *tag;
   switch(msg->MethodID)
   {  case OM_NEW:
         retval=NewStagadcls(cl,o,ops->ops_AttrList);
         break;
      case OM_SET:
         retval=DoSuperMethodA(cl,o,msg);
         data=INST_DATA(cl,o);
         if(tag=FindTagItem(GA_Text,ops->ops_AttrList))
         {  if(data->text) FREE(data->text);
            data->text=Dupstr((UBYTE *)tag->ti_Data,-1);
            if(data->text) data->textlength=strlen(data->text);
            else data->textlength=0;
            retval=1;
         }
         if(tag=FindTagItem(STATGA_HPText,ops->ops_AttrList))
         {  if(data->hptext) FREE(data->hptext);
            data->hptext=Dupstr((UBYTE *)tag->ti_Data,-1);
            data->hplen1=data->hplen2=0;
            data->hptextlength=0;
            if(data->hptext)
            {  data->hptextlength=strlen(data->hptext);
               data->hplen1=GetTagData(STATGA_HPLen1,0,ops->ops_AttrList);
               data->hplen2=GetTagData(STATGA_HPLen2,0,ops->ops_AttrList);
            }
            retval=1;
         }
         if(tag=FindTagItem(PGA_Total,ops->ops_AttrList))
         {  data->total=tag->ti_Data;
            retval=1;
         }
         if(tag=FindTagItem(PGA_Visible,ops->ops_AttrList))
         {  data->visible=tag->ti_Data;
            retval=1;
         }
         break;
      case OM_GET:
         data=INST_DATA(cl,o);
         retval=DoSuperMethodA(cl,o,msg);
         switch(opg->opg_AttrID)
         {  case GA_Text:
               *opg->opg_Storage=(ULONG)data->text;
               break;
            case STATGA_HPText:
               *opg->opg_Storage=(ULONG)data->hptext;
               break;
         }
         break;
      case GM_RENDER:
         retval=DoSuperMethodA(cl,o,msg);
         if(RenderStagadcls(cl,o,gpr)) retval=1;
         break;
      case GM_DOMAIN:
         data=INST_DATA(cl,o);
         gpd->gpd_Domain.Left=0;
         gpd->gpd_Domain.Top=0;
         switch(gpd->gpd_Which)
         {  case GDOMAIN_MINIMUM:
            case GDOMAIN_NOMINAL:
               gpd->gpd_Domain.Width=20;
               gpd->gpd_Domain.Height=gpd->gpd_RPort->TxHeight+2*data->bevelh+2;
               break;
            default:
               gpd->gpd_Domain.Width=8192;
               gpd->gpd_Domain.Height=8192;
               break;
         }
         break;
      case OM_DISPOSE:
         data=INST_DATA(cl,o);
         if(data->frameimg) DisposeObject(data->frameimg);
         if(data->text) FREE(data->text);
         if(data->hptext) FREE(data->hptext);
         retval=DoSuperMethodA(cl,o,msg);
         break;
      default:
         retval=DoSuperMethodA(cl,o,msg);
   }
   return retval;
}

static Class *InitStagadcls(void)
{  Class *cl;
   if(cl=MakeClass(NULL,"gadgetclass",NULL,sizeof(struct Stagaddata),0))
   {  cl->cl_Dispatcher.h_Entry=(HOOKFUNC)DispatchStagadcls;
   }
   return cl;
}

/*-----------------------------------------------------------------------*/

/* Ledgadcls - progress LEDs, framed with relwidth support */

/* boingball.image is a 24x24 canvas whose visible imagery is only the central
 * 16x16 area (the surrounding 4-pixel border is transparent). We report the
 * 16x16 visible size to the layout so the toolbar row matches the other small
 * controls, and centre the full 24x24 image when drawing (its transparent
 * border then falls outside the gadget rect and, thanks to PENMAP_MaskBlit,
 * paints nothing). */
#define BOINGBALL_NATIVE   24
#define BOINGBALL_VISIBLE  16

struct Ledgaddata
{  struct Image *frameimg;
   short bevelw,bevelh;
   BOOL active;
   long n;                 /* current frame */
   struct BitMap *bitmap;
   long x,y,w,h;
   long dx,dy;
   long frames;
   long restx,resty;
   void *capens;
   /* Optional boingball.image (V47+) substitute for the classic LED 
    * spinner. The object is owned by the Application, so this class never
    * disposes it. When non-NULL it takes priority over the bitmap/LED
    * fallbacks below. */
   Object *boingball;
   /* TRUE while a "next frame" tick from Setanimgads() is pending render.
    * Set by OM_SET(LEDGGA_Active,TRUE) when the gadget is already active,
    * consumed by RenderLedgadcls() which then sends IM_MOVE to the ball
    * (which both advances and redraws the next frame). */
   BOOL needmove;
   /* boingball.image should get one normal IM_DRAW before the first IM_MOVE.
    * V47.3 appears to do part of its internal penmap setup on the first draw;
    * jumping straight into IM_MOVE can leave it rendering as a silhouette. */
   BOOL ballready;
};

static ULONG NewLedgadcls(Class *cl,Object *o,struct TagItem *tags)
{  ULONG retval=0;
   struct Ledgaddata *data;
   void *frameimg;
   struct TagItem *imgtag=AllocateTagItems(5);
   struct TagItem *tag,*tstate=tags;
   void *colormap=(void *)Agetattr(Aweb(),AOAPP_Colormap);
   if(imgtag)
   {  imgtag[0].ti_Tag=BEVEL_Style;
      imgtag[0].ti_Data=BVS_BUTTON;
      imgtag[1].ti_Tag=IA_Recessed;
      imgtag[1].ti_Data=TRUE;
      imgtag[2].ti_Tag=IA_EdgesOnly;
      imgtag[2].ti_Data=FALSE;
      imgtag[3].ti_Tag=BEVEL_ColorMap;
      imgtag[3].ti_Data=(ULONG)colormap;
      imgtag[4].ti_Tag=TAG_END;
      if(frameimg=NewObjectA(BEVEL_GetClass(),NULL,imgtag))
      {  if(retval=DoSuperMethod(cl,o,OM_NEW,tags,NULL))
         {  data=INST_DATA(cl,retval);
            data->frameimg=frameimg;
            data->bevelw=Getvalue(frameimg,BEVEL_VertSize);
            data->bevelh=Getvalue(frameimg,BEVEL_HorizSize);
            data->restx=data->resty=-1;
            while(tag=NextTagItem(&tstate))
            {  switch(tag->ti_Tag)
               {  case LEDGGA_AnimBitMap:
                     data->bitmap=(struct BitMap *)tag->ti_Data;
                     break;
                  case LEDGGA_AnimX:
                     data->x=tag->ti_Data;
                     break;
                  case LEDGGA_AnimY:
                     data->y=tag->ti_Data;
                     break;
                  case LEDGGA_AnimWidth:
                     data->w=tag->ti_Data;
                     break;
                  case LEDGGA_AnimHeight:
                     data->h=tag->ti_Data;
                     break;
                  case LEDGGA_AnimDeltaX:
                     data->dx=tag->ti_Data;
                     break;
                  case LEDGGA_AnimDeltaY:
                     data->dy=tag->ti_Data;
                     break;
                  case LEDGGA_AnimFrames:
                     data->frames=tag->ti_Data;
                     break;
                  case LEDGGA_RestX:
                     data->restx=tag->ti_Data;
                     break;
                  case LEDGGA_RestY:
                     data->resty=tag->ti_Data;
                     break;
                  case LEDGGA_SpecialPens:
                     data->capens=(void *)tag->ti_Data;
                     break;
                  case LEDGGA_BoingBall:
                     data->boingball=(Object *)tag->ti_Data;
                     break;
               }
            }
            if(!data->w || !data->h || !data->frames) data->bitmap=NULL;
            if(!data->bitmap)
            {  data->frames=8;
               data->w=GA(retval)->Width-2*data->bevelw;
               data->h=GA(retval)->Height-2*data->bevelh;
            }
            if(data->restx<0 || data->resty<0) data->restx=data->resty=-1;
         }
         else DisposeObject(frameimg);
      }
      FreeTagItems(imgtag);
   }
   return retval;
}

static ULONG RenderLedgadcls(Class *cl,Object *o,struct gpRender *gpr)
{  struct Ledgaddata *data;
   ULONG retval=0;
   struct Gadget *g=(struct Gadget *)o;
   struct impDraw imp={0};
   struct impDraw ballimp={0};
   struct DrawInfo *dri=gpr->gpr_GInfo->gi_DrInfo;
   long x,y,x2,y2;
   long bgpen=0;     /* used only on the boingball.image render path */
   long ballx=0,bally=0; /* centred top-left for the 24x24 boingball image */
   UBYTE oldapen=0;  /* preserve caller's RastPort foreground pen */
   data=INST_DATA(cl,o);
   imp.MethodID=IM_DRAWFRAME;
   imp.imp_RPort=gpr->gpr_RPort;
   imp.X=g->LeftEdge;
   if(g->Flags&GFLG_RELRIGHT) imp.X+=gpr->gpr_GInfo->gi_Domain.Width-1;
   imp.Y=g->TopEdge;
   if(g->Flags&GFLG_RELBOTTOM) imp.Y+=gpr->gpr_GInfo->gi_Domain.Height-1;
   imp.imp_State=IDS_NORMAL;
   imp.imp_DrInfo=gpr->gpr_GInfo->gi_DrInfo;
   imp.Width=g->Width;
   if(g->Flags&GFLG_RELWIDTH) imp.Width+=gpr->gpr_GInfo->gi_Domain.Width;
   imp.Height=g->Height;
   if(g->Flags&GFLG_RELHEIGHT) imp.Height+=gpr->gpr_GInfo->gi_Domain.Height;
   /* Preferred path: render the boingball.image (V47+) when the host
    * Application managed to create one.  The class manages its own
    * palette, transparency and animation state internally, so all we
    * need to do here is:
    *
    *   1. Erase the gadget area to BACKGROUNDPEN so any prior pixels
    *      are gone before the ball blits over the top.  Preserve the
    *      RastPort foreground pen while doing this; boingball.image uses
    *      internal penmap/mask rendering and must not inherit the erase pen.
    *   2. Give the class one ordinary IM_DRAW before the first IM_MOVE,
    *      then send IM_MOVE during activity ticks (which both advances
    *      the frame AND renders it). */
   if(data->boingball)
   {  oldapen=gpr->gpr_RPort->FgPen;
      bgpen=dri?dri->dri_Pens[BACKGROUNDPEN]:0;
      SetAPen(gpr->gpr_RPort,bgpen);
      RectFill(gpr->gpr_RPort,
         imp.X,imp.Y,
         imp.X+imp.Width-1,imp.Y+imp.Height-1);
      SetAPen(gpr->gpr_RPort,oldapen);
      /* Centre the 24x24 ball within the gadget rect; its transparent border
       * spills outside but paints nothing (PENMAP_MaskBlit). */
      ballx=imp.X+(imp.Width-BOINGBALL_NATIVE)/2;
      bally=imp.Y+(imp.Height-BOINGBALL_NATIVE)/2;
      if(data->active && data->needmove && data->ballready)
      {  
         ballimp.MethodID=IM_MOVE;
         ballimp.imp_RPort=gpr->gpr_RPort;
         ballimp.X=ballx;
         ballimp.Y=bally;
         ballimp.imp_State=IDS_NORMAL;
         ballimp.imp_DrInfo=dri;
         DoMethodA(data->boingball,(Msg)&ballimp);
         data->needmove=FALSE;
      }
      else
      { 
         DrawImage(gpr->gpr_RPort,(struct Image *)data->boingball,ballx,bally);
         data->ballready=TRUE;
         data->needmove=FALSE;
      }
      return retval;
   }
   if(data->active)
   {  if(data->bitmap)
      {  SetAttrs(data->frameimg,
            IA_EdgesOnly,TRUE,
            REACTION_SpecialPens,data->capens,
            TAG_END);
         DoMethodA((Object *)data->frameimg,&imp);
         BltBitMapRastPort(data->bitmap,data->x+data->n*data->dx,data->y+data->n*data->dy,
            gpr->gpr_RPort,imp.X+data->bevelw,imp.Y+data->bevelh,data->w,data->h,0xc0);
      }
      else
      {  SetAttrs(data->frameimg,
            IA_EdgesOnly,FALSE,
            REACTION_SpecialPens,data->capens,
            TAG_END);
         DoMethodA((Object *)data->frameimg,&imp);
         if(data->n<4)
         {  x=imp.X+data->bevelw+data->n*(imp.Width-2*data->bevelw)/4;
            y=imp.Y+data->bevelh;
         }
         else
         {  x=imp.X+data->bevelw+(7-data->n)*(imp.Width-2*data->bevelw)/4;
            y=imp.Y+imp.Height/2;
         }
         x2=x+(imp.Width-2*data->bevelw)/4-1;
         y2=y+(imp.Height-2*data->bevelh)/2-1;
         if(dri) SetAPen(gpr->gpr_RPort,dri->dri_Pens[SHADOWPEN]);
         Move(gpr->gpr_RPort,x+1,y2);
         Draw(gpr->gpr_RPort,x2,y2);
         Draw(gpr->gpr_RPort,x2,y);
         if(dri) SetAPen(gpr->gpr_RPort,dri->dri_Pens[SHINEPEN]);
         Move(gpr->gpr_RPort,x,y2);
         Draw(gpr->gpr_RPort,x,y);
         Draw(gpr->gpr_RPort,x2-1,y);
         if(dri) SetAPen(gpr->gpr_RPort,dri->dri_Pens[FILLPEN]);
         RectFill(gpr->gpr_RPort,x+1,y+1,x2-1,y2-1);
      }
   }
   else  /* not active */
   {  if(data->bitmap && data->restx>=0)
      {  SetAttrs(data->frameimg,
            BEVEL_Transparent,TRUE,
            REACTION_SpecialPens,data->capens,
            TAG_END);
         DoMethodA((Object *)data->frameimg,&imp);
         BltBitMapRastPort(data->bitmap,data->restx,data->resty,
            gpr->gpr_RPort,imp.X+data->bevelw,imp.Y+data->bevelh,data->w,data->h,0xc0);
      }
      else
      {  SetAttrs(data->frameimg,
            BEVEL_Transparent,FALSE,
            REACTION_SpecialPens,data->capens,
            TAG_END);
         DoMethodA((Object *)data->frameimg,&imp);
      }
   }
   return retval;
}

static __asm __saveds ULONG DispatchLedgadcls(register __a0 Class *cl,
   register __a2 Object *o,register __a1 Msg msg)
{  struct Ledgaddata *data;
   ULONG retval=0;
   struct gpRender *gpr=(struct gpRender *)msg;
   struct opSet *ops=(struct opSet *)msg;
   struct gpDomain *gpd=(struct gpDomain *)msg;
   struct TagItem *tag;
   switch(msg->MethodID)
   {  case OM_NEW:
         retval=NewLedgadcls(cl,o,ops->ops_AttrList);
         break;
      case OM_SET:
         retval=DoSuperMethodA(cl,o,msg);
         data=INST_DATA(cl,o);
         if(tag=FindTagItem(LEDGGA_Active,ops->ops_AttrList))
         {  if(tag->ti_Data)
            {  if(data->active)
               {  if(++data->n>=data->frames) data->n=0;
                  if(data->boingball) data->needmove=TRUE;
               }
               else data->active=TRUE;
            }
            else
            {  data->active=FALSE;
               data->n=0;
               /* Going idle: drop any pending step so the next refresh
                * just paints the resting frame instead of spinning once. */
               data->needmove=FALSE;
            }
            retval=1;
         }
         if(tag=FindTagItem(LEDGGA_BoingBall,ops->ops_AttrList))
         {  data->boingball=(Object *)tag->ti_Data;
            data->needmove=FALSE;
            data->ballready=FALSE;
            retval=1;
         }
         break;
      case GM_RENDER:
         retval=DoSuperMethodA(cl,o,msg);
         if(RenderLedgadcls(cl,o,gpr)) retval=1;
         break;
      case GM_DOMAIN:
         data=INST_DATA(cl,o);
         gpd->gpd_Domain.Left=0;
         gpd->gpd_Domain.Top=0;
         if(data->boingball)
         {  /* Only the central 16x16 of the boingball is visible, so report
             * that rather than the (larger) classic anim dimensions; this
             * keeps the modern toolbar row the same height as the adjacent
             * 16x14 button/URL controls instead of stretching to fit. */
            gpd->gpd_Domain.Width=BOINGBALL_VISIBLE;
            gpd->gpd_Domain.Height=BOINGBALL_VISIBLE;
         }
         else
         {  gpd->gpd_Domain.Width=data->w+2*data->bevelw;
            gpd->gpd_Domain.Height=data->h+2*data->bevelh;
         }
         break;
      case OM_DISPOSE:
         data=INST_DATA(cl,o);
         if(data->frameimg) DisposeObject(data->frameimg);
         retval=DoSuperMethodA(cl,o,msg);
         break;
      default:
         retval=DoSuperMethodA(cl,o,msg);
   }
   return retval;
}

static Class *InitLedgadcls(void)
{  Class *cl;
   if(cl=MakeClass(NULL,"gadgetclass",NULL,sizeof(struct Ledgaddata),0))
   {  cl->cl_Dispatcher.h_Entry=(HOOKFUNC)DispatchLedgadcls;
   }
   return cl;
}

/*-----------------------------------------------------------------------*/

BOOL Initboopsi(void)
{  if(!(gadimgcls=InitGadimgcls())) return FALSE;
   if(!(stagadcls=InitStagadcls()))
   {  if(gadimgcls)
      {  FreeClass(gadimgcls);
         gadimgcls=NULL;
      }
      return FALSE;
   }
   if(!(ledgadcls=InitLedgadcls()))
   {  if(stagadcls)
      {  FreeClass(stagadcls);
         stagadcls=NULL;
      }
      if(gadimgcls)
      {  FreeClass(gadimgcls);
         gadimgcls=NULL;
      }
      return FALSE;
   }
   return TRUE;
}

/* Call only after every Gadimg/Stagad/Ledgad Intuition object has been OM_DISPOSEd (e.g. after
 * Adisposeobject on the application); otherwise OM_DISPOSE cannot run the class dispatcher
 * and planar image buffers from NewGadimgcls() stay allocated. */
void Freeboopsi(void)
{  if(ledgadcls) FreeClass(ledgadcls);
   if(stagadcls) FreeClass(stagadcls);
   if(gadimgcls) FreeClass(gadimgcls);
}

void *Gadimgclass(void)
{  return gadimgcls;
}

void *Stagadclass(void)
{  return stagadcls;
}

void *Ledgadclass(void)
{  return ledgadcls;
}
