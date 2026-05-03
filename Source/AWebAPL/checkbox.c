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

/* checkbox.c - AWeb html document checkbox element object */

#include "aweb.h"
#include "checkbox.h"
#include "form.h"
#include "application.h"
#include "window.h"
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

struct Checkbox
{  struct Field field;
   void *capens;
   USHORT flags;
   UBYTE *onclick;
   UBYTE *onfocus;
   UBYTE *onblur;
};

#define CHBF_CHECKED    0x0001   /* Checkbox is in selected state */
#define CHBF_INITIAL    0x0002   /* Checkbox was initially selected */

static void *bevel;
static long bevelw,bevelh;
static void *checkmark;
static long checkmarkw,checkmarkh;

static void *checkbox;           /* Dummy single object to catch prefs changes */

/*------------------------------------------------------------------------*/

/* Determine the size */
static void Checkmarksize(void)
{  short type=prefs.browser.styles[STYLE_NORMAL].fonttype;
   short size=prefs.browser.styles[STYLE_NORMAL].fontsize;
   struct TextFont *font=prefs.browser.font[type][size-1].font;
   checkmarkh=font->tf_Baseline-2*bevelh;
   checkmarkw=2*checkmarkh;
   SetAttrs(checkmark,
      IA_Width,checkmarkw,
      IA_Height,checkmarkh,
      TAG_END);
}

/*------------------------------------------------------------------------*/

/* Perform the actions for clicking the checkbox. First toggle, then test
   the onclick script and toggle back. */
static void Clickcheckbox(struct Checkbox *chb,BOOL testjs)
{  chb->flags^=CHBF_CHECKED;
   Arender(chb,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
   if(testjs && !Runjavascriptwith(chb->cframe,awebonclick,&chb->jobject,chb->form))
   {  chb->flags^=CHBF_CHECKED;
      Arender(chb,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
   }
}

/*------------------------------------------------------------------------*/

/* Get or set value property (JS) */
static BOOL Propertyvalue(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Checkbox *chb=vd->hookdata;
   UBYTE *value;
   if(chb)
   {  switch(vd->code)
      {  case VHC_SET:
            if(value=Jtostring(vd->jc,vd->value))
            {  if(chb->value) FREE(chb->value);
               chb->value=Dupstr(value,-1);
            }
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,chb->value);
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Get or set checked property (js) */
static BOOL Propertychecked(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Checkbox *chb=vd->hookdata;
   BOOL checked;
   if(chb)
   {  switch(vd->code)
      {  case VHC_SET:
            checked=Jtoboolean(vd->jc,vd->value);
            SETFLAG(chb->flags,CHBF_CHECKED,checked);
            Arender(chb,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
            result=TRUE;
            break;
         case VHC_GET:
            Jasgboolean(vd->jc,vd->value,BOOLVAL(chb->flags&CHBF_CHECKED));
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Javascript methods */
static void Methodclick(struct Jcontext *jc)
{  struct Checkbox *chb=Jointernal(Jthis(jc));
   if(chb) Clickcheckbox(chb,FALSE);
}

static void Methodfocus(struct Jcontext *jc)
{
}

static void Methodblur(struct Jcontext *jc)
{
}

static void Methodtostring(struct Jcontext *jc)
{  struct Checkbox *chb=Jointernal(Jthis(jc));
   struct Buffer buf={0};
   if(chb)
   {  Addtagstr(&buf,"<input",ATSF_NONE,0);
      Addtagstr(&buf,"type",ATSF_STRING,"checkbox");
      if(chb->name) Addtagstr(&buf,"name",ATSF_STRING,chb->name);
      if(chb->value) Addtagstr(&buf,"value",ATSF_STRING,chb->value);
      if(chb->flags&CHBF_INITIAL) Addtagstr(&buf,"checked",ATSF_NONE,0);
      Addtobuffer(&buf,">",2);
      Jasgstring(jc,NULL,buf.buffer);
      Freebuffer(&buf);
   }
}

/*------------------------------------------------------------------------*/

static long Measurecheckbox(struct Checkbox *chb,struct Ammeasure *amm)
{  chb->aow=checkmarkw+2*bevelw+2;
   chb->aoh=checkmarkh+2*bevelh+2;
   AmethodasA(AOTP_FIELD,chb,amm);
   return 0;
}

static long Rendercheckbox(struct Checkbox *chb,struct Amrender *amr)
{  struct Coords *coo,coords={0};
   BOOL clip=FALSE;
   ULONG clipkey=NULL;
   struct RastPort *rp;
   struct ColorMap *colormap=NULL;
   struct DrawInfo *drinfo=NULL;
   if(!(coo=amr->coords))
   {  Framecoords(chb->cframe,&coords);
      coo=&coords;
      clip=TRUE;
   }
   if(coo->rp)
   {  rp=coo->rp;
      Agetattrs(Aweb(),
         AOAPP_Colormap,&colormap,
         AOAPP_Drawinfo,&drinfo,
         TAG_END);
      if(clip) clipkey=Clipto(rp,coo->minx,coo->miny,coo->maxx,coo->maxy);
      SetAttrs(bevel,
         IA_Width,chb->aow,
         IA_Height,chb->aoh,
         BEVEL_FillPen,~0,
         BEVEL_ColorMap,colormap,
         BEVEL_Flags,BFLG_XENFILL,
         REACTION_SpecialPens,chb->capens,
         TAG_END);
      DrawImageState(rp,bevel,chb->aox+coo->dx,chb->aoy+coo->dy,IDS_NORMAL,drinfo);
      if(chb->flags&CHBF_CHECKED)
      {  DrawImageState(rp,checkmark,
            chb->aox+coo->dx+bevelw+1,chb->aoy+coo->dy+bevelh+1,IDS_NORMAL,drinfo);
      }
      if(clip) Unclipto(clipkey);
   }
   return 0;
}

static long Setcheckbox(struct Checkbox *chb,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=Amethodas(AOTP_FIELD,chb,AOM_SET,ams->tags);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCHB_Checked:
            if(tag->ti_Data) chb->flags|=CHBF_CHECKED;
            else chb->flags&=~CHBF_CHECKED;
            break;
         case AOFLD_Reset:
            if(chb->flags&CHBF_INITIAL) chb->flags|=CHBF_CHECKED;
            else chb->flags&=~CHBF_CHECKED;
            Arender(chb,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
            break;
         case AOAPP_Browsersettings:
            Checkmarksize();
            break;
         case AOBJ_Window:
            if(tag->ti_Data)
            {  chb->capens=(void *)Agetattr((void *)tag->ti_Data,AOWIN_Specialpens);
            }
            else
            {  chb->capens=NULL;
            }
            break;
         case AOFLD_Onclick:
            if(chb->onclick) FREE(chb->onclick);
            chb->onclick=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onfocus:
            if(chb->onfocus) FREE(chb->onfocus);
            chb->onfocus=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onblur:
            if(chb->onblur) FREE(chb->onblur);
            chb->onblur=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
      }
   }
   if(!chb->value)
   {  chb->value=Dupstr("on",-1);
   }
   return result;
}

static long Getcheckbox(struct Checkbox *chb,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=AmethodasA(AOTP_FIELD,chb,ams);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCHB_Checked:
            PUTATTR(tag,BOOLVAL(chb->flags&CHBF_CHECKED));
            break;
         case AOFLD_Name:
            /* NULLify name and value set by superclass if unselected */
            if(!(chb->flags&CHBF_CHECKED))
            {  PUTATTR(tag,NULL);
            }
            break;
         case AOFLD_Value:
            if(!(chb->flags&CHBF_CHECKED))
            {  PUTATTR(tag,NULL);
            }
            break;
      }
   }
   return result;
}

static struct Checkbox *Newcheckbox(struct Amset *ams)
{  struct Checkbox *chb;
   if(chb=Allocobject(AOTP_CHECKBOX,sizeof(struct Checkbox),ams))
   {  Setcheckbox(chb,ams);
      if(chb->flags&CHBF_CHECKED) chb->flags|=CHBF_INITIAL;
   }
   return chb;
}

static long Hittestcheckbox(struct Checkbox *chb,struct Amhittest *amh)
{  long result;
   if(amh->oldobject==chb)
   {  result=AMHR_OLDHIT;
   }
   else
   {  result=AMHR_NEWHIT;
      if(amh->amhr)
      {  amh->amhr->object=chb;
      }
   }
   return result;
}

static long Goactivecheckbox(struct Checkbox *chb,struct Amgoactive *amg)
{  Clickcheckbox(chb,chb->onclick || AWebJSBase);
   return AMR_CHANGED;
}

static long Dragtestcheckbox(struct Checkbox *chb,struct Amdragtest *amd)
{  return AMDR_STOP;
}

static long Jsetupcheckbox(struct Checkbox *chb,struct Amjsetup *amj)
{  struct Jvar *jv;
   AmethodasA(AOTP_FIELD,chb,amj);
   if(chb->jobject)
   {  if(jv=Jproperty(amj->jc,chb->jobject,"value"))
      {  Setjproperty(jv,Propertyvalue,chb);
      }
      if(jv=Jproperty(amj->jc,chb->jobject,"type"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgstring(amj->jc,jv,"checkbox");
      }
      if(jv=Jproperty(amj->jc,chb->jobject,"checked"))
      {  Setjproperty(jv,Propertychecked,chb);
      }
      if(jv=Jproperty(amj->jc,chb->jobject,"defaultChecked"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgboolean(amj->jc,jv,BOOLVAL(chb->flags&CHBF_INITIAL));
      }
      Addjfunction(amj->jc,chb->jobject,"click",Methodclick,NULL);
      Addjfunction(amj->jc,chb->jobject,"focus",Methodfocus,NULL);
      Addjfunction(amj->jc,chb->jobject,"blur",Methodblur,NULL);
      Addjfunction(amj->jc,chb->jobject,"toString",Methodtostring,NULL);
      Jaddeventhandler(amj->jc,chb->jobject,"onfocus",chb->onfocus);
      Jaddeventhandler(amj->jc,chb->jobject,"onblur",chb->onblur);
      Jaddeventhandler(amj->jc,chb->jobject,"onclick",chb->onclick);
   }
   return 0;
}

static void Disposecheckbox(struct Checkbox *chb)
{  if(chb->onclick) FREE(chb->onclick);
   if(chb->onfocus) FREE(chb->onfocus);
   if(chb->onblur) FREE(chb->onblur);
   Amethodas(AOTP_FIELD,chb,AOM_DISPOSE);
}

static void Deinstallcheckbox(void)
{  if(checkbox)
   {  Aremchild(Aweb(),checkbox,AOREL_APP_USE_BROWSER);
      Adisposeobject(checkbox);
   }
   if(checkmark) DisposeObject(checkmark);
   if(bevel) DisposeObject(bevel);
}

static long Dispatch(struct Checkbox *chb,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newcheckbox((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setcheckbox(chb,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getcheckbox(chb,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measurecheckbox(chb,(struct Ammeasure *)amsg);
         break;
      case AOM_RENDER:
         result=Rendercheckbox(chb,(struct Amrender *)amsg);
         break;
      case AOM_HITTEST:
         result=Hittestcheckbox(chb,(struct Amhittest *)amsg);
         break;
      case AOM_GOACTIVE:
         result=Goactivecheckbox(chb,(struct Amgoactive *)amsg);
         break;
      case AOM_DRAGTEST:
         result=Dragtestcheckbox(chb,(struct Amdragtest *)amsg);
         break;
      case AOM_JSETUP:
         result=Jsetupcheckbox(chb,(struct Amjsetup *)amsg);
         break;
      case AOM_DISPOSE:
         Disposecheckbox(chb);
         break;
      case AOM_DEINSTALL:
         Deinstallcheckbox();
         break;
      default:
         result=AmethodasA(AOTP_FIELD,chb,amsg);
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installcheckbox(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_CHECKBOX,Dispatch)) return FALSE;
   if(!(bevel=BevelObject,
      BEVEL_Style,BVS_BUTTON,
      End)) return FALSE;
   GetAttr(BEVEL_VertSize,bevel,(ULONG *)&bevelw);
   GetAttr(BEVEL_HorizSize,bevel,(ULONG *)&bevelh);
   if(!(checkmark=GlyphObject,
      GLYPH_Glyph,GLYPH_CHECKMARK,
      End)) return FALSE;
   return TRUE;
}

BOOL Initcheckbox(void)
{  Checkmarksize();
   if(!(checkbox=Anewobject(AOTP_CHECKBOX,TAG_END))) return FALSE;
   Aaddchild(Aweb(),checkbox,AOREL_APP_USE_BROWSER);
   return TRUE;
}