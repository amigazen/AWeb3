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

/* element.c - AWeb general HTML element object superclass */

#include "aweb.h"
#include "element.h"
#include "ttengine.h"
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>

static struct RastPort eltrp;
struct RastPort *mrp;

#define Halignvalue(h,v) (((v)&0x0f)|((h)&0xf0))

/*----------------------------------------------------------------------*/

static long Setelement(struct Element *elt,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Left:
            elt->aox=tag->ti_Data;
            break;
         case AOBJ_Top:
            elt->aoy=tag->ti_Data;
            break;
         case AOBJ_Width:
            elt->aow=tag->ti_Data;
            break;
         case AOBJ_Height:
            elt->aoh=tag->ti_Data;
            break;
         case AOBJ_Cframe:
            elt->cframe=(void *)tag->ti_Data;
            elt->eltflags&=~(ELTF_LAYEDOUT|ELTF_ALIGNED);
            break;
         case AOELT_Textpos:
            elt->textpos=tag->ti_Data;
            break;
         case AOELT_Textlength:
            elt->length=tag->ti_Data;
            break;
         case AOELT_Preformat:
            if(tag->ti_Data) elt->eltflags|=ELTF_PREFORMAT;
            else elt->eltflags&=~ELTF_PREFORMAT;
            break;
         case AOELT_Halign:
            elt->halign=Halignvalue(elt->halign,tag->ti_Data);
            elt->eltflags|=ELTF_HALIGN;
            break;
         case AOELT_Valign:
            elt->valign=tag->ti_Data;
            break;
         case AOELT_Font:
            elt->font=(struct TextFont *)tag->ti_Data;
            break;
         case AOELT_Style:
            elt->style=tag->ti_Data;
            break;
         case AOELT_Defhalign:
            if(!(elt->eltflags&ELTF_HALIGN))
            {  elt->halign=Halignvalue(elt->halign,tag->ti_Data);
            }
            break;
         case AOELT_Link:
            if(elt->link) Aremchild(elt->link,elt,0);
            elt->link=(void *)tag->ti_Data;
            if(elt->link) Aaddchild(elt->link,elt,0);
            break;
         case AOELT_Leftindent:
            elt->leftindent=tag->ti_Data;
            break;
         case AOELT_Rightindent:
            elt->rightindent=tag->ti_Data;
            break;
         case AOELT_Bullet:
            if(tag->ti_Data) elt->halign|=HALIGN_BULLET;
            else elt->halign&=~HALIGN_BULLET;
            break;
         case AOELT_Floating:
            elt->halign|=(tag->ti_Data&0x60);
            break;
         case AOELT_Resetlayout:
            elt->eltflags&=~(ELTF_LAYEDOUT|ELTF_ALIGNED);
            break;
         case AOELT_Visible:
            SETFLAG(elt->eltflags,ELTF_INVISIBLE,!tag->ti_Data);
            break;
         case AOELT_Nobr:
            SETFLAG(elt->eltflags,ELTF_NOBR,tag->ti_Data);
            break;
         case AOELT_TagName:
            if(elt->tagname) FREE(elt->tagname);
            elt->tagname = (UBYTE *)tag->ti_Data;
            break;
         case AOELT_Class:
            if(elt->class) FREE(elt->class);
            elt->class = (UBYTE *)tag->ti_Data;
            break;
         case AOELT_Id:
            if(elt->id) FREE(elt->id);
            elt->id = (UBYTE *)tag->ti_Data;
            break;
         case AOBJ_Layoutparent:
            elt->layoutparent=(void *)tag->ti_Data;
            break;
      }
   }
   if(elt->halign&HALIGN_FLOATLEFT) elt->valign=VALIGN_TOP;
   return 0;
}

static long Getelement(struct Element *elt,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Left:
            PUTATTR(tag,elt->aox);
            break;
         case AOBJ_Top:
            PUTATTR(tag,elt->aoy);
            break;
         case AOBJ_Width:
            PUTATTR(tag,elt->aow);
            break;
         case AOBJ_Height:
            PUTATTR(tag,elt->aoh);
            break;
         case AOBJ_Cframe:
            PUTATTR(tag,elt->cframe);
            break;
         case AOELT_Textpos:
            PUTATTR(tag,elt->textpos);
            break;
         case AOELT_Textlength:
            PUTATTR(tag,elt->length);
            break;
         case AOELT_Preformat:
            PUTATTR(tag,BOOLVAL(elt->eltflags&ELTF_PREFORMAT));
            break;
         case AOELT_Halign:
            PUTATTR(tag,elt->halign);
            break;
         case AOELT_Valign:
            PUTATTR(tag,elt->valign);
            break;
         case AOELT_Font:
            PUTATTR(tag,elt->font);
            break;
         case AOELT_Style:
            PUTATTR(tag,elt->style);
            break;
         case AOELT_Link:
            PUTATTR(tag,elt->link);
            break;
         case AOELT_Leftindent:
            PUTATTR(tag,elt->leftindent);
            break;
         case AOELT_Rightindent:
            PUTATTR(tag,elt->rightindent);
            break;
         case AOELT_Floating:
            PUTATTR(tag,elt->halign&0x60);
            break;
         case AOELT_Visible:
            PUTATTR(tag,!BOOLVAL(elt->eltflags&ELTF_INVISIBLE));
            break;
         case AOELT_Nobr:
            PUTATTR(tag,BOOLVAL(elt->eltflags&ELTF_NOBR));
            break;
         case AOELT_TagName:
            PUTATTR(tag,elt->tagname);
            break;
         case AOELT_Class:
            PUTATTR(tag,elt->class);
            break;
         case AOELT_Id:
            PUTATTR(tag,elt->id);
            break;
         case AOBJ_Layoutparent:
            PUTATTR(tag,elt->layoutparent);
            break;
      }
   }
   return 0;
}

static long Measureelement(struct Element *elt,struct Ammeasure *amm)
{  if(amm->ammr)
   {  amm->ammr->width=elt->aow;
//      if(elt->eltflags&ELTF_NOBR)
      if(elt->eltflags&(ELTF_NOBR|ELTF_PREFORMAT))
      {  amm->ammr->minwidth=amm->addwidth+elt->aow;
         amm->ammr->addwidth=amm->ammr->minwidth;
      }
      else
      {  amm->ammr->minwidth=elt->aow;
      }
   }
   return 0;
}

static long Layoutelement(struct Element *elt,struct Amlayout *aml)
{  short result;
   /* In case of RETRY, don't do anything if we are aligned or we are a bullet */
   if((aml->flags&AMLF_RETRY) &&
      ((elt->eltflags&ELTF_ALIGNED) || (elt->halign&HALIGN_BULLET)))
   {  if(aml->amlr)
      {  aml->amlr->result=AMLR_OK;
         aml->amlr->endx=aml->startx;
      }
   }
   else
   {  if(aml->flags&AMLF_BREAK)
      {  if(elt->eltflags&(ELTF_PREFORMAT|ELTF_NOBR))
         {  result=AMLR_BREAK;
         }
         else
         {  result=AMLR_OK;
         }
      }
      else
      {  elt->aox=aml->startx;
         if(elt->aox+elt->aow>aml->width && !(aml->flags&AMLF_FORCE))
         {  if(elt->eltflags&(ELTF_NOBR|ELTF_PREFORMAT))
            {  result=AMLR_BREAK;
            }
            else
            {  result=AMLR_NOFIT;
            }
         }
         else if(elt->halign&HALIGN_FLOATLEFT)
         {  result=AMLR_FLOATING;
         }
         else if((elt->eltflags&(ELTF_NOBR|ELTF_PREFORMAT)) && (aml->flags&AMLF_FORCE))
         {  result=AMLR_FBREAK;
         }
         else
         {  result=AMLR_OK;
         }
      }
      if(aml->amlr)
      {  aml->amlr->result=result;
         aml->amlr->endx=elt->aox+elt->aow;
         switch(elt->valign)
         {  case VALIGN_BOTTOM:
               aml->amlr->above=elt->aoh;
               break;
            case VALIGN_MIDDLE:
               aml->amlr->above=elt->aoh/2;
               aml->amlr->below=elt->aoh-aml->amlr->above;
               break;
            case VALIGN_TOP:
               aml->amlr->toph=elt->aoh;
               break;
            default:
               aml->amlr->toph=elt->aoh;
         }
      }
      elt->eltflags&=~ELTF_ALIGNED;
   }
   return 0;
}

static long Alignelement(struct Element *elt,struct Amalign *ama)
{  if(!(elt->eltflags&ELTF_ALIGNED))
   {  elt->aox+=ama->dx;
      switch(elt->valign)
      {  case VALIGN_BOTTOM:
            elt->aoy=ama->y+ama->baseline-elt->aoh+1;
            break;
         case VALIGN_MIDDLE:
            elt->aoy=ama->y+ama->baseline-elt->aoh/2+1;
            break;
         case VALIGN_TOP:
            elt->aoy=ama->y;
            break;
         default:
            elt->aoy=ama->y;
      }
      elt->eltflags|=ELTF_ALIGNED;
   }
   return 0;
}

static long Moveelement(struct Element *elt,struct Ammove *amm)
{  elt->aox+=amm->dx;
   elt->aoy+=amm->dy;
   return 0;
}

static long Hittestelement(struct Element *elt,struct Amhittest *amh)
{  long result=AMHR_NOHIT;
   if(elt->link)
   {  result=AmethodA(elt->link,amh);
   }
   return result;
}

static void Disposeelement(struct Element *elt)
{  if(elt->link) Aremchild(elt->link,elt,0);
   if(elt->tagname) FREE(elt->tagname);
   if(elt->class) FREE(elt->class);
   if(elt->id) FREE(elt->id);
   Amethodas(AOTP_OBJECT,elt,AOM_DISPOSE);
}

static long Dispatch(struct Element *elt,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_SET:
         result=Setelement(elt,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getelement(elt,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measureelement(elt,(struct Ammeasure *)amsg);
         break;
      case AOM_LAYOUT:
         result=Layoutelement(elt,(struct Amlayout *)amsg);
         break;
      case AOM_ALIGN:
         result=Alignelement(elt,(struct Amalign *)amsg);
         break;
      case AOM_MOVE:
         result=Moveelement(elt,(struct Ammove *)amsg);
         break;
      case AOM_HITTEST:
         result=Hittestelement(elt,(struct Amhittest *)amsg);
         break;
      case AOM_DISPOSE:
         Disposeelement(elt);
         break;
      case AOM_DEINSTALL:
         break;
      default:
         break;
   }
   return result;
}

/*----------------------------------------------------------------------*/

BOOL Installelement(void)
{  InitRastPort(&eltrp);
   mrp=&eltrp;
   if(!Amethod(NULL,AOM_INSTALL,AOTP_ELEMENT,Dispatch)) return FALSE;
   return TRUE;
}

long Textlengthext(struct RastPort *rp,UBYTE *text,long count,long *extent)
{  long length=0,part;
   struct TextExtent te={0};
   if(extent) *extent=0;
   /* Use ttengine wrapper if ttengine font is active, else use standard graphics.library function */
   if(IsTTEngineFontActive(rp))
   {  /* ttengine: te_Width is advance; ink span is MaxX-MinX+1 (SDK says they need not match). */
      long lim;
      long ink;
      long adv;
      if(count > 0)
      {  lim=count;
         if(lim>32767L)
         {
            lim=32767L;
         }
         TTEngineTextExtent(rp,text,(WORD)lim,&te);
         adv=(long)te.te_Width;
         ink=(long)te.te_Extent.MaxX-(long)te.te_Extent.MinX+1L;
         if(ink<1L)
         {
            ink=adv;
         }
         length=adv;
         if(ink>length)
         {
            length=ink;
         }
         length+=(long)TTEngineBoldInkPad(rp);
         if(extent)
         {
            *extent=ink+(long)TTEngineBoldInkPad(rp);
         }
      }
   }
   else
   {  /* Standard graphics.library approach - use TextFit in a loop for very long strings */
      while(count)
      {  part=TextFit(rp,text,count,&te,NULL,1,32000,32000);
         length+=te.te_Width;
         text+=part;
         count-=part;
         if(extent) *extent+=te.te_Extent.MaxX+1;
      }
   }
   return length;
}

long Textlength(struct RastPort *rp,UBYTE *text,long count)
{  /* Use ttengine wrapper if ttengine font is active, else use standard function */
   if(IsTTEngineFontActive(rp))
   {  /* ttengine handles whole strings at once - no need for loops */
      if(count > 0)
      {  return (long)TTEngineTextLength(rp,text,count);
      }
      return 0;
   }
   else
   {  return Textlengthext(rp,text,count,NULL);
   }
}