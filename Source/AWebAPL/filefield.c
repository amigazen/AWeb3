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

/* filefield.c - AWeb html document file upload form field element object */

#include "aweb.h"
#include "filefield.h"
#include "input.h"
#include "window.h"
#include "application.h"
#include "filereq.h"
#include "jslib.h"
#include <proto/exec.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <intuition/imageclass.h>
#include <images/bevel.h>
#include <images/glyph.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/bevel.h>
#include <proto/glyph.h>

/*------------------------------------------------------------------------*/

struct Filefield
{  struct Field field;
   void *pool;
   void *frame;
   void *capens;
   USHORT flags;
   long size;
   short butw;             /* Width of button added */
   UBYTE *onchange;
   UBYTE *onfocus;
   UBYTE *onblur;
   struct Field *inp;      /* Embedded INPUT type object */
   void *filereq;
};

#define FUFF_SELECTED   0x0001   /* Button is in selected state */

static void *bevel;
static long bevelw,bevelh;
static void *glyph;

/*------------------------------------------------------------------------*/

/* Get or set value property (js) */
static BOOL Propertyvalue(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Filefield *fuf=vd->hookdata;
   UBYTE *p;
   if(fuf)
   {  switch(vd->code)
      {  case VHC_SET:
            p=Jtostring(vd->jc,vd->value);
            Asetattrs(fuf->inp,
               AOFLD_Value,p,
               AOFLD_Reset,TRUE,
               TAG_END);
            result=TRUE;
            break;
         case VHC_GET:
            p=(UBYTE *)Agetattr(fuf->inp,AOFLD_Value);
            Jasgstring(vd->jc,vd->value,p);
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Javascript methods */
static void Methodfocus(struct Jcontext *jc)
{  struct Filefield *fuf=Jointernal(Jthis(jc));
   if(fuf)
   {  Asetattrs(fuf->inp,AOBJ_Jsfocus,jc,TAG_END);
   }
}

static void Methodblur(struct Jcontext *jc)
{  struct Filefield *fuf=Jointernal(Jthis(jc));
   if(fuf)
   {  Asetattrs(fuf->inp,AOBJ_Jsblur,jc,TAG_END);
   }
}

static void Methodtostring(struct Jcontext *jc)
{  struct Filefield *fuf=Jointernal(Jthis(jc));
   struct Buffer buf={0};
   UBYTE *p;
   if(fuf)
   {  Addtagstr(&buf,"<input",ATSF_NONE,0);
      Addtagstr(&buf,"type",ATSF_STRING,"file");
      if(fuf->name) Addtagstr(&buf,"name",ATSF_STRING,fuf->name);
      p=(UBYTE *)Agetattr(fuf->inp,AOFLD_Value);
      Addtagstr(&buf,"value",ATSF_STRING,p?p:NULLSTRING);
      if(fuf->size>0) Addtagstr(&buf,"size",ATSF_NUMBER,fuf->size-1);
      Addtobuffer(&buf,">",2);
      Jasgstring(jc,NULL,buf.buffer);
      Freebuffer(&buf);
   }
}

/*------------------------------------------------------------------------*/

/* Render only the popup button */
static void Renderpopup(struct Filefield *fuf,struct Coords *coo)
{  struct ColorMap *colormap;
   ULONG state=IDS_NORMAL;
   long bpen=~0,tpen=~0;
   coo=Clipcoords(fuf->cframe,coo);
   if(coo && coo->rp)
   {  colormap=(struct ColorMap *)Agetattr(Aweb(),AOAPP_Colormap);
      if(fuf->flags&FUFF_SELECTED)
      {  state=IDS_SELECTED;
         tpen=coo->dri->dri_Pens[FILLTEXTPEN];
         bpen=coo->dri->dri_Pens[FILLPEN];
      }
      SetAttrs(bevel,
         IA_Width,fuf->butw,
         IA_Height,fuf->aoh,
         BEVEL_FillPen,bpen,
         BEVEL_TextPen,tpen,
         BEVEL_ColorMap,colormap,
         BEVEL_Flags,BFLG_XENFILL,
         REACTION_SpecialPens,fuf->capens,
         TAG_END);
      DrawImageState(coo->rp,bevel,
         fuf->aox+fuf->aow-fuf->butw+coo->dx,fuf->aoy+coo->dy,state,coo->dri);
      SetAttrs(glyph,
         IA_Width,fuf->butw-2*bevelw-2,
         IA_Height,fuf->aoh-2*bevelh-2,
         TAG_END);
      DrawImageState(coo->rp,glyph,
         fuf->aox+fuf->aow-fuf->butw+bevelw+1+coo->dx,fuf->aoy+bevelh+1+coo->dy,
         IDS_NORMAL,coo->dri);
      Unclipcoords(coo);
   }
}

/*------------------------------------------------------------------------*/

static long Measurefilefield(struct Filefield *fuf,struct Ammeasure *amm)
{  struct Ammresult ammr={0};
   if(fuf->inp)
   {  Ameasure(fuf->inp,amm->width,amm->height,0,0,amm->text,&ammr);
      /* Add a sqare button */
      fuf->butw=fuf->inp->aoh;
      fuf->aow=ammr.width+fuf->butw;
      fuf->aoh=fuf->inp->aoh;
   }
   AmethodasA(AOTP_FIELD,fuf,amm);
   return 0;
}

static long Layoutfilefield(struct Filefield *fuf,struct Amlayout *aml)
{  if(fuf->inp)
   {  AmethodA(fuf->inp,aml);
   }
   AmethodasA(AOTP_FIELD,fuf,aml);
   return 0;
}

static long Alignfilefield(struct Filefield *fuf,struct Amalign *ama)
{  AmethodasA(AOTP_FIELD,fuf,ama);
   if(fuf->inp)
   {  fuf->inp->aox=fuf->aox;
      fuf->inp->aoy=fuf->aoy;
   }
   return 0;
}

static long Movefilefield(struct Filefield *fuf,struct Ammove *amm)
{  AmethodasA(AOTP_FIELD,fuf,amm);
   if(fuf->inp)
   {  fuf->inp->aox=fuf->aox;
      fuf->inp->aoy=fuf->aoy;
   }
   return 0;
}

static long Renderfilefield(struct Filefield *fuf,struct Amrender *amr)
{  AmethodA(fuf->inp,amr);
   Renderpopup(fuf,amr->coords);
   return 0;
}

static long Setfilefield(struct Filefield *fuf,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   BOOL newcframe=FALSE,newframe=FALSE,newwin=FALSE;
   Amethodas(AOTP_FIELD,fuf,AOM_SET,ams->tags);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Window:
            if(tag->ti_Data)
            {  fuf->capens=(void *)Agetattr((void *)tag->ti_Data,AOWIN_Specialpens);
            }
            else
            {  fuf->capens=NULL;
               fuf->flags&=~FUFF_SELECTED;
            }
            newwin=TRUE;
            break;
         case AOBJ_Frame:
            fuf->frame=(void *)tag->ti_Data;
            newframe=TRUE;
            break;
         case AOBJ_Cframe:
            newcframe=TRUE;
            break;
         case AOBJ_Pool:
            fuf->pool=(void *)tag->ti_Data;
            break;
         case AOFLD_Onchange:
            if(fuf->onchange) FREE(fuf->onchange);
            fuf->onchange=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onfocus:
            if(fuf->onfocus) FREE(fuf->onfocus);
            fuf->onfocus=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onblur:
            if(fuf->onblur) FREE(fuf->onblur);
            fuf->onblur=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Reset:
            Asetattrs(fuf->inp,
               AOFLD_Value,"",
               AOFLD_Reset,TRUE,
               TAG_END);
            break;
         case AOFUF_Size:
            fuf->size=tag->ti_Data;
            break;
      }
   }
   if(fuf->inp && (newcframe || newframe || newwin))
   {  Asetattrs(fuf->inp,
         newcframe?AOBJ_Cframe:TAG_IGNORE,fuf->cframe,
         newframe?AOBJ_Frame:TAG_IGNORE,fuf->frame,
         newwin?AOBJ_Window:TAG_IGNORE,fuf->win,
         TAG_END);
   }
   return 0;
}

static long Updatefilefield(struct Filefield *fuf,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFRQ_Filename:
            if(tag->ti_Data)
            {  Asetattrs(fuf->inp,
                  AOFLD_Value,tag->ti_Data,
                  AOFLD_Reset,TRUE, /* To refresh the display */
                  TAG_END);
            }
            fuf->filereq=NULL;
            break;
         case AOBJ_Jsfocus:
            if(fuf->onfocus || AWebJSBase)
            {  Runjavascriptwith(fuf->cframe,awebonfocus,&fuf->jobject,fuf->form);
            }
            break;
         case AOBJ_Jsblur:
            if(fuf->onblur || AWebJSBase)
            {  Runjavascriptwith(fuf->cframe,awebonblur,&fuf->jobject,fuf->form);
            }
            break;
         case AOBJ_Jschange:
            if(fuf->onchange || AWebJSBase)
            {  Runjavascriptwith(fuf->cframe,awebonchange,&fuf->jobject,fuf->form);
            }
            break;
      }
   }
   return 0;
}

static long Getfilefield(struct Filefield *fuf,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   AmethodasA(AOTP_FIELD,fuf,ams);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFLD_Value:
            PUTATTR(tag,Agetattr(fuf->inp,AOFLD_Value));
            break;
         case AOFUF_Input:
            PUTATTR(tag,fuf->inp);
            break;
      }
   }
   return 0;
}

static void Disposefilefield(struct Filefield *fuf)
{  if(fuf->filereq) Adisposeobject(fuf->filereq);
   if(fuf->inp) Adisposeobject(fuf->inp);
   if(fuf->onchange) FREE(fuf->onchange);
   if(fuf->onfocus) FREE(fuf->onfocus);
   if(fuf->onblur) FREE(fuf->onblur);
   Amethodas(AOTP_FIELD,fuf,AOM_DISPOSE);
}

static struct Filefield *Newfilefield(struct Amset *ams)
{  struct Filefield *fuf;
   if(fuf=Allocobject(AOTP_FILEFIELD,sizeof(struct Filefield),ams))
   {  fuf->size=-1;
      Setfilefield(fuf,ams);
      fuf->inp=Anewobject(AOTP_INPUT,
         AOBJ_Pool,fuf->pool,
         AOFLD_Value,"",
         AOINP_Type,INPTP_TEXT,
         (fuf->size>0)?AOINP_Size:TAG_IGNORE,fuf->size,
         AOBJ_Jsowner,fuf,
         TAG_END);
      if(!fuf->inp)
      {  Disposefilefield(fuf);
         fuf=NULL;
      }
   }
   return fuf;
}

static long Hittestfilefield(struct Filefield *fuf,struct Amhittest *amh)
{  long result=AMHR_NOHIT;
   long x;
   if(amh->coords)
   {  x=amh->xco-fuf->aox-amh->coords->dx;
      if(fuf->inp && x<fuf->aow-fuf->butw)
      {  result=AmethodA(fuf->inp,amh);
      }
   }
   if(!result)
   {  if(amh->oldobject==fuf)
      {  result=AMHR_OLDHIT;
      }
      else
      {  result=AMHR_NEWHIT;
         if(amh->amhr)
         {  amh->amhr->object=fuf;
         }
      }
   }
   return result;
}

static long Goactivefilefield(struct Filefield *fuf,struct Amgoactive *amg)
{  fuf->flags|=FUFF_SELECTED;
   Renderpopup(fuf,NULL);
   return AMR_ACTIVE;
}

static long Handleinputfilefield(struct Filefield *fuf,struct Aminput *ami)
{  struct Coords coords={0};
   long result=AMR_REUSE;
   long x,y;
   if(ami->imsg)
   {  switch(ami->imsg->Class)
      {  case IDCMP_MOUSEMOVE:
         case IDCMP_RAWKEY:
            /* Check if mouse is still over popup button */
            Framecoords(fuf->cframe,&coords);
            x=ami->imsg->MouseX-coords.dx;
            y=ami->imsg->MouseY-coords.dy;
            if(x>=fuf->aox+fuf->aow-fuf->butw && x<fuf->aox+fuf->aow
            && y>=fuf->aoy && y<fuf->aoy+fuf->aoh)
            {  if(!(fuf->flags&FUFF_SELECTED))
               {  fuf->flags|=FUFF_SELECTED;
                  Renderpopup(fuf,NULL);
               }
            }
            else
            {  if(fuf->flags&FUFF_SELECTED)
               {  fuf->flags&=~FUFF_SELECTED;
                  Renderpopup(fuf,NULL);
               }
            }
            result=AMR_ACTIVE;
            break;
         case IDCMP_MOUSEBUTTONS:
            if(ami->imsg->Code==SELECTUP && (fuf->flags&FUFF_SELECTED))
            {  if(!fuf->filereq)
               {  UBYTE *value=(UBYTE *)Agetattr(fuf->inp,AOFLD_Value);
                  if(!value || !*value) value=fuf->value;
                  fuf->filereq=Anewobject(AOTP_FILEREQ,
                     AOBJ_Target,fuf,
                     AOFRQ_Title,AWEBSTR(MSG_FILE_UPLOADTITLE),
                     AOFRQ_Filename,value,
                     TAG_END);
               }
            }
            result=AMR_NOREUSE;
            break;
         case IDCMP_INTUITICKS:
            result=AMR_ACTIVE;
            break;
      }
   }
   return result;
}

static long Goinactivefilefield(struct Filefield *fuf)
{  if(fuf->flags&FUFF_SELECTED)
   {  fuf->flags&=~FUFF_SELECTED;
      Renderpopup(fuf,NULL);
   }
   return 0;
}

static long Jsetupfilefield(struct Filefield *fuf,struct Amjsetup *amj)
{  struct Jvar *jv;
   AmethodasA(AOTP_FIELD,fuf,amj);
   if(fuf->jobject)
   {  if(jv=Jproperty(amj->jc,fuf->jobject,"value"))
      {  Setjproperty(jv,Propertyvalue,fuf);
      }
      if(jv=Jproperty(amj->jc,fuf->jobject,"type"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgstring(amj->jc,jv,"file");
      }
      Addjfunction(amj->jc,fuf->jobject,"focus",Methodfocus,NULL);
      Addjfunction(amj->jc,fuf->jobject,"blur",Methodblur,NULL);
      Addjfunction(amj->jc,fuf->jobject,"toString",Methodtostring,NULL);
      Jaddeventhandler(amj->jc,fuf->jobject,"onfocus",fuf->onfocus);
      Jaddeventhandler(amj->jc,fuf->jobject,"onblur",fuf->onblur);
      Jaddeventhandler(amj->jc,fuf->jobject,"onchange",fuf->onchange);
   }
   return 0;
}

static void Deinstallfilefield(void)
{  if(glyph)
   {  DisposeObject(glyph);
      glyph=NULL;
   }
   if(bevel)
   {  DisposeObject(bevel);
      bevel=NULL;
   }
}

static long Dispatch(struct Filefield *fuf,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newfilefield((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setfilefield(fuf,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatefilefield(fuf,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getfilefield(fuf,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measurefilefield(fuf,(struct Ammeasure *)amsg);
         break;
      case AOM_LAYOUT:
         result=Layoutfilefield(fuf,(struct Amlayout *)amsg);
         break;
      case AOM_ALIGN:
         result=Alignfilefield(fuf,(struct Amalign *)amsg);
         break;
      case AOM_MOVE:
         result=Movefilefield(fuf,(struct Ammove *)amsg);
         break;
      case AOM_RENDER:
         result=Renderfilefield(fuf,(struct Amrender *)amsg);
         break;
      case AOM_HITTEST:
         result=Hittestfilefield(fuf,(struct Amhittest *)amsg);
         break;
      case AOM_GOACTIVE:
         result=Goactivefilefield(fuf,(struct Amgoactive *)amsg);
         break;
      case AOM_HANDLEINPUT:
         result=Handleinputfilefield(fuf,(struct Aminput *)amsg);
         break;
      case AOM_GOINACTIVE:
         result=Goinactivefilefield(fuf);
         break;
      case AOM_JSETUP:
         result=Jsetupfilefield(fuf,(struct Amjsetup *)amsg);
         break;
      case AOM_DISPOSE:
         Disposefilefield(fuf);
         break;
      case AOM_DEINSTALL:
         Deinstallfilefield();
         break;
      default:
         result=AmethodasA(AOTP_FIELD,fuf,amsg);
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installfilefield(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_FILEFIELD,Dispatch)) return FALSE;
   /* Allow safe re-entry (e.g. failed init then retry) without leaking objects. */
   if(glyph)
   {  DisposeObject(glyph);
      glyph=NULL;
   }
   if(bevel)
   {  DisposeObject(bevel);
      bevel=NULL;
   }
   if(!(bevel=BevelObject,
      BEVEL_Style,BVS_BUTTON,
      End)) return FALSE;
   GetAttr(BEVEL_VertSize,bevel,(ULONG *)&bevelw);
   GetAttr(BEVEL_HorizSize,bevel,(ULONG *)&bevelh);
   /* If glyph creation fails, dispose bevel so Installfilefield retry does not leak. */
   if(!(glyph=GlyphObject,
      GLYPH_Glyph,GLYPH_POPFILE,
      End))
   {  DisposeObject(bevel);
      bevel=NULL;
      return FALSE;
   }
   return TRUE;
}

