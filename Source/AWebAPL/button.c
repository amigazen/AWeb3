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

/* button.c - AWeb html document button element object */

#include "aweb.h"
#include "button.h"
#include "form.h"
#include "body.h"
#include "text.h"
#include "application.h"
#include "url.h"
#include "window.h"
#include "jslib.h"
#include "ttengine.h"
#include <proto/exec.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <intuition/imageclass.h>
#include <images/bevel.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/bevel.h>

/* Forward declarations */
struct Element;

/* Minimal Body structure to access contents.first (full definition in body.c) */
/* LIST(Element) expands to: struct { struct Element *first; struct Element *tail; struct Element *last; } */
struct BodyMinimal
{  struct Aobject object;
   long aox,aoy,aow,aoh;
   void *frame;
   void *cframe;
   void *pool;
   void *parent;
   void *tcell;
   struct
   {  struct Element *first;
      struct Element *tail;
      struct Element *last;
   } contents;  /* LIST(Element) */
};

/*------------------------------------------------------------------------*/

struct Button
{  struct Field field;
   void *pool;
   USHORT type;
   void *capens;
   void *frame;
   void *parent;
   USHORT flags;
   UBYTE *onclick;
   UBYTE *onfocus;
   UBYTE *onblur;
   void *body;             /* For custom label */
   long minw,maxw;
};

#define BUTF_SELECTED   0x0001   /* Button is in selected state */
#define BUTF_REMEASURE  0x0002   /* Re-measure even if width is known */
#define BUTF_COMPLETE   0x0004   /* Custom label is complete */

static void *bevel;
static long bevelw,bevelh;

/*------------------------------------------------------------------------*/

/* Get font from element, with fallback to screen font */
/* INPUT value label: same ttengine binding on (rp) as document text so measure width matches
 * draw (about:home submit in a narrow table vs bitmap-only Textlength on mrp). */
static void Buttonbindvaluefont(struct RastPort *rp,struct TextFont *font)
{
   TTEngineSetFont(rp,font,NULL,0);
   if(!IsTTEngineFontActive(rp))
   {
      SetFont(rp,font);
      SetSoftStyle(rp,0,0x0f);
   }
}

static long Buttonvaluefit(struct RastPort *rp,UBYTE *s,long len,struct TextExtent *te,
   long maxw,long maxh)
{
   if(IsTTEngineFontActive(rp))
   {
      return (long)TTEngineTextFit(rp,s,(UWORD)len,te,NULL,1,(UWORD)maxw,(UWORD)maxh);
   }
   return (long)TextFit(rp,s,(WORD)len,te,NULL,1,(UWORD)maxw,(UWORD)maxh);
}

static void Buttonvaluedraw(struct RastPort *rp,UBYTE *s,long len)
{
   if(IsTTEngineFontActive(rp))
   {
      TTEngineText(rp,s,(ULONG)len);
   }
   else
   {
      Text(rp,s,len);
   }
}

static struct TextFont *Getbuttonfont(struct Button *but)
{  struct TextFont *font;
   
   /* Try to get font from element */
   font=(struct TextFont *)Agetattr(but,AOELT_Font);
   
   /* Fallback to screen font if not set */
   if(!font)
   {  font=(struct TextFont *)Agetattr(Aweb(),AOAPP_Screenfont);
   }
   
   return font;
}

/* Perform the actions for clicking the button */
static void Clickbutton(struct Button *but,BOOL js)
{  switch(but->type)
   {  case BUTTP_SUBMIT:
         if(!js || Runjavascriptwith(but->cframe,awebonclick,&but->jobject,but->form))
         {  /* Only submit if button is inside a form */
            if(but->form)
            {  Amethod(but->form,AFO_SUBMIT,but,0,0);
            }
         }
         break;
      case BUTTP_RESET:
         /* Only reset if button is inside a form */
         if(but->form)
         {  Amethod(but->form,AFO_RESET);
         }
         if(js) Runjavascriptwith(but->cframe,awebonclick,&but->jobject,but->form);
         break;
      case BUTTP_BUTTON:
         /* BUTTON type is safe outside forms - only runs JavaScript */
         if(js) Runjavascriptwith(but->cframe,awebonclick,&but->jobject,but->form);
         break;
   }
}

/*------------------------------------------------------------------------*/

/* Javascript methods */
static void Methodclick(struct Jcontext *jc)
{  struct Button *but=Jointernal(Jthis(jc));
   if(but) Clickbutton(but,FALSE);
}

static void Methodfocus(struct Jcontext *jc)
{
}

static void Methodblur(struct Jcontext *jc)
{
}

static void Methodtostring(struct Jcontext *jc)
{  struct Button *but=Jointernal(Jthis(jc));
   struct Buffer buf={0};
   UBYTE *p;
   if(but)
   {  Addtagstr(&buf,"<input",ATSF_NONE,0);
      switch(but->type)
      {  case BUTTP_SUBMIT:p="submit";break;
         case BUTTP_RESET: p="reset";break;
         default:          p="button";break;
      }
      Addtagstr(&buf,"type",ATSF_STRING,p);
      if(but->name) Addtagstr(&buf,"name",ATSF_STRING,but->name);
      if(but->value) Addtagstr(&buf,"value",ATSF_STRING,but->value);
      Addtobuffer(&buf,">",2);
      Jasgstring(jc,NULL,buf.buffer);
      Freebuffer(&buf);
   }
}

static BOOL Propertyvalue(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Button *but=vd->hookdata;
   UBYTE *p;
   if(but)
   {  switch(vd->code)
      {  case VHC_SET:
            if(p=Dupstr(Jtostring(vd->jc,vd->value),-1))
            {  if(but->value) FREE(but->value);
               but->value=p;
               Arender(but,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
            }
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,but->value);
            result=TRUE;
            break;
      }
   }
   return result;
}

/*------------------------------------------------------------------------*/

static long Measurebutton(struct Button *but,struct Ammeasure *amm)
{  /* Only re-measure if we haven't got a width yet. Because the text
    * can change under JS, a re-measure shouldn't resize the button.
    * There is no other way the size can change anyway. */
   if(!but->aow || (but->flags&BUTF_REMEASURE))
   {  if(but->body)
      {  /* BUTTON element - ALWAYS measure the body, NEVER use but->value */
         struct Ammresult ammr={0};
         Ameasure(but->body,amm->width,amm->height,0,0,amm->text,&ammr);
         but->aow=ammr.width;
         but->minw=ammr.minwidth;
         but->maxw=ammr.width;
         if(amm->ammr)
         {  amm->ammr->width=ammr.width;
            if(but->eltflags&(ELTF_NOBR|ELTF_PREFORMAT))
            {  amm->ammr->minwidth=amm->addwidth+ammr.minwidth;
               amm->ammr->addwidth=amm->ammr->minwidth;
            }
            else
            {  amm->ammr->minwidth=ammr.minwidth;
            }
         }
         but->flags&=~BUTF_REMEASURE;
      }
      else
      {  /* INPUT type="button" elements, measure based on value attribute */
         struct TextFont *font=Getbuttonfont(but);
         Buttonbindvaluefont(mrp,font);
         but->aow=Textlength(mrp,but->value,(long)strlen((char *)but->value))+2*bevelw+8;
         /* Match input field height calculation for consistent appearance */
         but->aoh=font->tf_YSize+2*bevelh+4;
         but->flags&=~BUTF_REMEASURE;
         AmethodasA(AOTP_FIELD,but,amm);
      }
   }
   else
   {  AmethodasA(AOTP_FIELD,but,amm);
   }
   return 0;
}

static long Layoutbutton(struct Button *but,struct Amlayout *aml)
{  if(but->body)
   {  /* For BUTTON elements, layout the body even if not complete yet.
       * This allows buttons with simple text content to be laid out and rendered. */
      long winw,bodyw;
      if(!(aml->flags&(AMLF_BREAK|AMLF_RETRY)))
      {  /* Make button its max width unless we are first in the line, then make it fit. */
         if(aml->flags&AMLF_FIRST)
         {  /* Subtract bevels AND the 8px padding to find max available width for body */
            winw=aml->width-aml->startx-2*bevelw-8;
            /* Extract raw body width from our padded maxw */
            bodyw=but->maxw-2*bevelw-8;
            if(bodyw>winw) bodyw=winw;
            if(bodyw<but->minw-2*bevelw-8) bodyw=but->minw-2*bevelw-8;
         }
         else
         {  /* Extract raw body width from our padded maxw */
            bodyw=but->maxw-2*bevelw-8;
         }
         /* Layout the body with the calculated content width */
         Alayout(but->body,bodyw,aml->height,0,aml->text,0,NULL);
         /* FIX: Final width is BodyWidth + Bevels + 8px Padding */
         but->aow=Agetattr(but->body,AOBJ_Width)+2*bevelw+8;
         but->aoh=Agetattr(but->body,AOBJ_Height)+2*bevelh;
      }
      AmethodasA(AOTP_FIELD,but,aml);
   }
   else
   {  /* For INPUT type="button" elements, use field layout */
      AmethodasA(AOTP_FIELD,but,aml);
   }
   return 0;
}

static long Alignbutton(struct Button *but,struct Amalign *ama)
{  if(but->body)
   {  /* For BUTTON elements, align the body even if not complete yet.
       * This allows buttons with simple text content to be aligned and rendered. */
      AmethodasA(AOTP_FIELD,but,ama);
      Amove(but->body,but->aox+bevelw,but->aoy+bevelh);
   }
   else
   {  /* For INPUT type="button" elements, use field alignment */
      AmethodasA(AOTP_FIELD,but,ama);
   }
   return 0;
}

static long Renderbutton(struct Button *but,struct Amrender *amr)
{  struct Coords *coo,coords={0};
   BOOL clip=FALSE;
   ULONG clipkey=NULL;
   struct RastPort *rp;
   ULONG state=IDS_NORMAL;
   struct ColorMap *colormap=NULL;
   struct TextFont *font;
   long bpen=~0,tpen=~0;
   long textw,textl,textx,buttonw;
   struct TextExtent te={0};
   if(!(coo=amr->coords))
   {  Framecoords(but->cframe,&coords);
      coo=&coords;
      clip=TRUE;
   }
   if(coo->rp)
   {  rp=coo->rp;
      Agetattrs(Aweb(),
         AOAPP_Colormap,&colormap,
         TAG_END);
      font=Getbuttonfont(but);
      if(but->flags&BUTF_SELECTED)
      {  state=IDS_SELECTED;
         tpen=coo->dri->dri_Pens[FILLTEXTPEN];
         bpen=coo->dri->dri_Pens[FILLPEN];
      }
      if(clip) clipkey=Clipto(rp,coo->minx,coo->miny,coo->maxx,coo->maxy);
      SetAttrs(bevel,
         IA_Width,but->aow,
         IA_Height,but->aoh,
         BEVEL_FillPen,bpen,
         BEVEL_TextPen,tpen,
         BEVEL_ColorMap,colormap,
         BEVEL_Flags,BFLG_XENFILL,
         REACTION_SpecialPens,but->capens,
         TAG_END);
      DrawImageState(rp,bevel,but->aox+coo->dx,but->aoy+coo->dy,state,coo->dri);
      if(but->body)
      {  /* BUTTON element - extract text from body the same way measurement does and render it.
          * Measurement uses Ameasure(but->body,amm->text) which reads from amm->text->buffer.
          * We need to read from the same buffer (amr->text) using the same textpos/textlen. */
         struct BodyMinimal *bd;
         void *child;
         void *nextchild;
         long textpos,textlen;
         UBYTE *textptr;
         BOOL rendered=FALSE;
         bd=(struct BodyMinimal *)but->body;
         child=bd->contents.first;
         if(child && amr->text)
         {  nextchild=((struct Aobject *)child)->next;
            /* Single element if: first==last OR next is NULL OR next points to list tail */
            if((bd->contents.first==bd->contents.last) || !nextchild || (nextchild==bd->contents.last))
            {  if(((struct Aobject *)child)->objecttype==AOTP_TEXT)
               {  textpos=(long)Agetattr(child,AOELT_Textpos);
                  textlen=(long)Agetattr(child,AOELT_Textlength);
                  if(textpos>=0 && textlen>0 && textpos+textlen<=amr->text->length)
                  {  textptr=amr->text->buffer+textpos;
                     /* Render the text directly, same way measurement reads it from amm->text->buffer */
                     Buttonbindvaluefont(rp,font);
                     SetAPen(rp,coo->dri->dri_Pens[TEXTPEN]);
                     textw=Textlength(rp,textptr,textlen);
                     buttonw=but->aow-2*bevelw-8;
                     if(textw<=buttonw)
                     {  textx=(buttonw-textw)/2;
                        textl=textlen;  /* Use full text length when it fits */
                     }
                     else
                     {  /* Text doesn't fit, find out how much fits */
                        textl=Buttonvaluefit(rp,textptr,textlen,&te,buttonw,but->aoh);
                        textx=0;
                     }
                     Move(rp,but->aox+coo->dx+bevelw+4+textx,but->aoy+coo->dy+bevelh+1+rp->TxBaseline);
                     Buttonvaluedraw(rp,textptr,textl);
                     rendered=TRUE;
                  }
               }
            }
         }
         /* If we didn't render text above, fall back to rendering the body */
         if(!rendered && but->flags&BUTF_COMPLETE)
         {  if(bpen==~0) bpen=coo->dri->dri_Pens[BACKGROUNDPEN];
            Asetattrs(but->body,AOBDY_Forcebgcolor,bpen,TAG_END);
            Arender(but->body,coo,0,0,AMRMAX,AMRMAX,0,amr->text);
         }
      }
      else
      {  /* INPUT type="button" elements (custom=FALSE), render the value attribute */
         Buttonbindvaluefont(rp,font);
         SetAPen(rp,coo->dri->dri_Pens[TEXTPEN]);
         /* With JS the value can have changed after measure */
         textl=(long)strlen((char *)but->value);
         textw=Textlength(rp,but->value,textl);
         buttonw=but->aow-2*bevelw-8;
         if(textw<=buttonw)
         {  /* Text still fits, center it */
            textx=(buttonw-textw)/2;
         }
         else
         {  /* Text doesn't fit, find out how much fits */
            textl=Buttonvaluefit(rp,but->value,textl,&te,buttonw,but->aoh);
            textx=0;
         }
         Move(rp,but->aox+coo->dx+bevelw+4+textx,but->aoy+coo->dy+bevelh+1+rp->TxBaseline);
         Buttonvaluedraw(rp,but->value,textl);
      }
      if(clip) Unclipto(clipkey);
   }
   return 0;
}

static long Setbutton(struct Button *but,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   BOOL newcframe=FALSE,newframe=FALSE,newwin=FALSE,custom=FALSE;
   BOOL complete_in_tags=FALSE;
   /* First pass: check if AOBUT_Complete is in the tags and if this is a custom button */
   while(tag=NextTagItem(&tstate))
   {  if(tag->ti_Tag==AOBUT_Custom && tag->ti_Data) custom=TRUE;
      if(tag->ti_Tag==AOBUT_Complete && tag->ti_Data) complete_in_tags=TRUE;
   }
   /* Reset tag iterator for second pass */
   tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBUT_Custom:
            if(tag->ti_Data && !but->body)
            {  custom=TRUE;
               /* For BUTTON elements, clear any value that might have been set from VALUE attribute.
                * BUTTON elements use inner text, not VALUE attribute. */
               if(but->value) FREE(but->value);
               but->value=NULL;
            }
            break;
      }
   }
   /* Reset tag iterator for second pass */
   tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBUT_Type:
            but->type=tag->ti_Data;
            break;
         case AOBJ_Window:
            if(tag->ti_Data)
            {  but->capens=(void *)Agetattr((void *)tag->ti_Data,AOWIN_Specialpens);
            }
            else
            {  but->capens=NULL;
               but->flags&=~BUTF_SELECTED;
            }
            newwin=TRUE;
            break;
         case AOBJ_Frame:
            if(!tag->ti_Data)
            {  but->flags|=BUTF_REMEASURE;
            }
            but->frame=(void *)tag->ti_Data;
            newframe=TRUE;
            break;
         case AOBJ_Cframe:
            newcframe=TRUE;
            break;
         case AOFLD_Onclick:
            if(but->onclick) FREE(but->onclick);
            but->onclick=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onfocus:
            if(but->onfocus) FREE(but->onfocus);
            but->onfocus=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onblur:
            if(but->onblur) FREE(but->onblur);
            but->onblur=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Value:
            /* For custom buttons (BUTTON elements), ignore VALUE attribute - use inner text instead.
             * For INPUT buttons, let the superclass handle it. */
            if(!custom && !Agetattr(but,AOBUT_Custom))
            {  /* Let superclass handle it for INPUT buttons */
            }
            else
            {  /* For BUTTON elements, ignore VALUE attribute - don't set but->value from it */
               if(but->value) FREE(but->value);
               but->value=NULL;
            }
            break;
         case AOBJ_Pool:
            but->pool=(void *)tag->ti_Data;
            break;
         case AOBUT_Custom:
            if(tag->ti_Data && !but->body)
            {  custom=TRUE;
               /* For BUTTON elements, clear any value that might have been set from VALUE attribute.
                * BUTTON elements use inner text, not VALUE attribute. */
               if(but->value) FREE(but->value);
               but->value=NULL;
            }
            break;
         case AOBUT_Complete:
            SETFLAG(but->flags,BUTF_COMPLETE,tag->ti_Data);
            /* When button body is complete, extract plain text from it for rendering.
             * For BUTTON elements, always use the inner text (content between tags),
             * not the VALUE attribute. This allows BUTTON elements to work like INPUT buttons
             * for simple text content.
             * IMPORTANT: For custom buttons, ALWAYS extract text and overwrite any existing value
             * (including defaults that might have been set earlier). */
            if(tag->ti_Data && but->body && (custom || Agetattr(but,AOBUT_Custom)))
            {  struct BodyMinimal *bd;
               void *child;
               void *nextchild;
               UBYTE *text;
               long textpos,textlen;
               struct Buffer *textbuf;
               long totaltextlen;
               UBYTE *p;
               long start,end;
               bd=(struct BodyMinimal *)but->body;
               /* Extract text from body - handle single text element or concatenate multiple text elements */
               child=bd->contents.first;
               if(child)
               {  /* First, check if we have a single text element (most common case) */
                  nextchild=((struct Aobject *)child)->next;
                  if((bd->contents.first==bd->contents.last) || !nextchild || (nextchild==bd->contents.last))
                  {  /* Single child - check if it's a text element */
                     if(((struct Aobject *)child)->objecttype==AOTP_TEXT)
                     {  textpos=(long)Agetattr(child,AOELT_Textpos);
                        textlen=(long)Agetattr(child,AOELT_Textlength);
                        textbuf=(struct Buffer *)Agetattr(child,AOTXT_Text);
                        if(textbuf && textpos>=0 && textlen>0 && textpos+textlen<=textbuf->length)
                        {  text=ALLOCTYPE(UBYTE,textlen+1,0);
                           if(text)
                           {  memcpy(text,textbuf->buffer+textpos,textlen);
                              text[textlen]='\0';
                              /* Trim leading and trailing whitespace */
                              p=text;
                              while(*p && isspace(*p)) p++;
                              start=p-text;
                              end=textlen;
                              while(end>start && isspace(text[end-1])) end--;
                              if(end>start)
                              {  if(start>0 || end<textlen)
                                 {  memmove(text,text+start,end-start);
                                    text[end-start]='\0';
                                 }
                                 /* For BUTTON elements, always use inner text, not VALUE attribute.
                                  * ALWAYS free any existing value (including defaults) and replace with extracted inner text.
                                  * This ensures we never use defaults for custom buttons. */
                                 if(but->value) FREE(but->value);
                                 but->value=text;
                                 /* Trigger re-measure so button width is calculated based on extracted text */
                                 but->flags|=BUTF_REMEASURE;
                                 /* Mark that we've extracted text so defaults are never set */
                                 custom=TRUE;
                              }
                              else
                              {  FREE(text);
                              }
                           }
                        }
                     }
                  }
                  else
                  {  /* Multiple children - try to concatenate all text elements */
                     totaltextlen=0;
                     for(child=bd->contents.first;child;child=((struct Aobject *)child)->next)
                     {  if(((struct Aobject *)child)->objecttype==AOTP_TEXT)
                        {  textlen=(long)Agetattr(child,AOELT_Textlength);
                           if(textlen>0) totaltextlen+=textlen;
                        }
                     }
                     if(totaltextlen>0)
                     {  text=ALLOCTYPE(UBYTE,totaltextlen+1,0);
                        if(text)
                        {  UBYTE *dest=text;
                           for(child=bd->contents.first;child;child=((struct Aobject *)child)->next)
                           {  if(((struct Aobject *)child)->objecttype==AOTP_TEXT)
                              {  textpos=(long)Agetattr(child,AOELT_Textpos);
                                 textlen=(long)Agetattr(child,AOELT_Textlength);
                                 textbuf=(struct Buffer *)Agetattr(child,AOTXT_Text);
                                 if(textbuf && textpos>=0 && textlen>0 && textpos+textlen<=textbuf->length)
                                 {  memcpy(dest,textbuf->buffer+textpos,textlen);
                                    dest+=textlen;
                                 }
                              }
                           }
                           *dest='\0';
                           /* Trim leading and trailing whitespace */
                           p=text;
                           while(*p && isspace(*p)) p++;
                           start=p-text;
                           end=totaltextlen;
                           while(end>start && isspace(text[end-1])) end--;
                           if(end>start)
                           {  if(start>0 || end<totaltextlen)
                              {  memmove(text,text+start,end-start);
                                 text[end-start]='\0';
                              }
                              /* ALWAYS free any existing value (including defaults) and replace with extracted inner text */
                              if(but->value) FREE(but->value);
                              but->value=text;
                              but->flags|=BUTF_REMEASURE;
                              /* Mark that we've extracted text so defaults are never set */
                              custom=TRUE;
                           }
                           else
                           {  FREE(text);
                           }
                        }
                     }
                  }
               }
            }
            break;
         case AOBJ_Layoutparent:
            but->parent=(void *)tag->ti_Data;
            break;
         case AOBJ_Changedchild:
            if((void *)tag->ti_Data==but->body)
            {  Asetattrs(but->parent,AOBJ_Changedchild,but,TAG_END);
               /* When content is added to button body, try to extract text immediately.
                * This ensures we get the text as soon as it's available, not just when complete. */
               if(but->body && Agetattr(but,AOBUT_Custom) && !but->value)
               {  struct BodyMinimal *bd;
                  void *child;
                  void *nextchild;
                  UBYTE *text;
                  long textpos,textlen;
                  struct Buffer *textbuf;
                  UBYTE *p;
                  long start,end;
                  bd=(struct BodyMinimal *)but->body;
                  child=bd->contents.first;
                  if(child)
                  {  nextchild=((struct Aobject *)child)->next;
                     if((bd->contents.first==bd->contents.last) || !nextchild || (nextchild==bd->contents.last))
                     {  if(((struct Aobject *)child)->objecttype==AOTP_TEXT)
                        {  textpos=(long)Agetattr(child,AOELT_Textpos);
                           textlen=(long)Agetattr(child,AOELT_Textlength);
                           textbuf=(struct Buffer *)Agetattr(child,AOTXT_Text);
                           if(textbuf && textpos>=0 && textlen>0 && textpos+textlen<=textbuf->length)
                           {  text=ALLOCTYPE(UBYTE,textlen+1,0);
                              if(text)
                              {  memcpy(text,textbuf->buffer+textpos,textlen);
                                 text[textlen]='\0';
                                 p=text;
                                 while(*p && isspace(*p)) p++;
                                 start=p-text;
                                 end=textlen;
                                 while(end>start && isspace(text[end-1])) end--;
                                 if(end>start)
                                 {  if(start>0 || end<textlen)
                                    {  memmove(text,text+start,end-start);
                                       text[end-start]='\0';
                                    }
                                    but->value=text;
                                    but->flags|=BUTF_REMEASURE;
                                 }
                                 else
                                 {  FREE(text);
                                 }
                              }
                           }
                        }
                     }
                  }
               }
            }
            break;
      }
   }
   /* Superclass does AOM_ADDCHILD which needs type */
   result=Amethodas(AOTP_FIELD,but,AOM_SET,ams->tags);
   /* Determine if this is a custom button (BUTTON element, not INPUT) */
   if(!custom) custom=Agetattr(but,AOBUT_Custom)?TRUE:FALSE;
   /* For custom buttons (BUTTON elements), clear any value that was set from AOFLD_Value.
    * BUTTON elements use inner text, not VALUE attribute. The superclass may have set
    * but->value to NULL or empty from AOFLD_Value, which would trigger defaults below.
    * We prevent that by ensuring but->value stays NULL for custom buttons until text is extracted.
    * BUT: If AOBUT_Complete was already processed above and extracted text, don't clear it! */
   if(custom && !complete_in_tags)
   {  /* For BUTTON elements, we don't use VALUE attribute - clear it if it was set */
      if(but->value)
      {  FREE(but->value);
         but->value=NULL;
      }
   }
   /* Only set default values for INPUT type="button" elements (custom=FALSE).
    * BUTTON elements (custom=TRUE) should use their inner text content, not defaults.
    * Also don't set defaults if AOBUT_Complete was processed (text extraction happened). */
   if(!but->value && !custom && !complete_in_tags)
   {  switch(but->type)
      {  case BUTTP_SUBMIT:
            but->value=Dupstr(AWEBSTR(MSG_AWEB_FORMSUBMIT),-1);
            break;
         case BUTTP_RESET:
            but->value=Dupstr(AWEBSTR(MSG_AWEB_FORMRESET),-1);
            break;
         default:
            but->value=Dupstr(AWEBSTR(MSG_AWEB_FORMBUTTON),-1);
      }
   }
   if(custom && !but->body)
   {  but->body=Anewobject(AOTP_BODY,
         AOBJ_Pool,but->pool,
         AOBJ_Frame,but->frame,
         AOBJ_Cframe,but->cframe,
         AOBJ_Window,but->win,
         AOBJ_Layoutparent,but,
         AOBDY_Leftmargin,2,
         AOBDY_Topmargin,1,
         TAG_END);
      /* Note: Button body should inherit font and color from document context
       * automatically when text is added to it. If text is not visible, the issue
       * may be elsewhere (e.g., text not being added to body, or rendering issue). */
   }
   if(but->body && (newcframe || newframe || newwin))
   {  Asetattrs(but->body,
         newcframe?AOBJ_Cframe:TAG_IGNORE,but->cframe,
         newframe?AOBJ_Frame:TAG_IGNORE,but->frame,
         newwin?AOBJ_Window:TAG_IGNORE,but->win,
         TAG_END);
   }
   return result;
}

static long Getbutton(struct Button *but,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=AmethodasA(AOTP_FIELD,but,ams);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBUT_Type:
            PUTATTR(tag,but->type);
            break;
         case AOBUT_Body:
            /* If this is a custom button but body doesn't exist yet, create it */
            if(!but->body && Agetattr(but,AOBUT_Custom))
            {  /* Body should have been created in Setbutton, but create it now if missing */
               void *pool,*frame,*cframe,*win;
               pool=(void *)Agetattr(but,AOBJ_Pool);
               frame=(void *)Agetattr(but,AOBJ_Frame);
               cframe=(void *)Agetattr(but,AOBJ_Cframe);
               win=(void *)Agetattr(but,AOBJ_Window);
               if(pool)
               {  but->body=Anewobject(AOTP_BODY,
                     AOBJ_Pool,pool,
                     AOBJ_Frame,frame,
                     AOBJ_Cframe,cframe,
                     AOBJ_Window,win,
                     AOBJ_Layoutparent,but,
                     AOBDY_Leftmargin,2,
                     AOBDY_Topmargin,1,
                     TAG_END);
               }
            }
            PUTATTR(tag,but->body);
            break;
      }
   }
   return result;
}

static struct Button *Newbutton(struct Amset *ams)
{  struct Button *but;
   if(but=Allocobject(AOTP_BUTTON,sizeof(struct Button),ams))
   {  Setbutton(but,ams);
   }
   return but;
}

static long Hittestbutton(struct Button *but,struct Amhittest *amh)
{  long result=AMHR_NOHIT;
   long popup=0;
   if(but->body && (but->flags&BUTF_COMPLETE))
   {  result=AmethodA(but->body,amh);
      popup=result&AMHR_POPUP;
      result&=~AMHR_POPUP;
   }
   if(!result)
   {  if(amh->oldobject==but)
      {  result=AMHR_OLDHIT;
      }
      else
      {  result=AMHR_NEWHIT;
         if(amh->amhr)
         {  amh->amhr->object=but;
            if(but->type==BUTTP_SUBMIT && but->form)
            {  void *url=(void *)Agetattr(but->form,AOFOR_Action);
               if(url)
               {  amh->amhr->text=Dupstr((UBYTE *)Agetattr(url,AOURL_Url),-1);
               }
            }
         }
      }
   }
   return result|popup;
}

static long Goactivebutton(struct Button *but,struct Amgoactive *amg)
{  but->flags|=BUTF_SELECTED;
   Arender(but,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
   return AMR_ACTIVE;
}

static long Handleinputbutton(struct Button *but,struct Aminput *ami)
{  struct Coords coords={0};
   long result=AMR_REUSE;
   long x,y;
   if(ami->imsg)
   {  switch(ami->imsg->Class)
      {  case IDCMP_MOUSEMOVE:
         case IDCMP_RAWKEY:
            /* Check if mouse is still over us */
            Framecoords(but->cframe,&coords);
            x=ami->imsg->MouseX-coords.dx;
            y=ami->imsg->MouseY-coords.dy;
            if(x>=but->aox && x<but->aox+but->aow
            && y>=but->aoy && y<but->aoy+but->aoh)
            {  if(!(but->flags&BUTF_SELECTED))
               {  but->flags|=BUTF_SELECTED;
                  Arender(but,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
               }
               result=AMR_ACTIVE;
            }
            else
            {  if(but->flags&BUTF_SELECTED)
               {  but->flags&=~BUTF_SELECTED;
                  Arender(but,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
               }
               result=AMR_ACTIVE;
            }
            break;
         case IDCMP_MOUSEBUTTONS:
            if(ami->imsg->Code==SELECTUP && (but->flags&BUTF_SELECTED))
            {  Clickbutton(but,but->onclick || AWebJSBase);
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

static long Goinactivebutton(struct Button *but)
{  if(but->flags&BUTF_SELECTED)
   {  but->flags&=~BUTF_SELECTED;
      Arender(but,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
   }
   return 0;
}

static long Notifybutton(struct Button *but,struct Amnotify *amn)
{  if(but->body)
   {  AmethodA(but->body,amn);
   }
   return (long)AmethodasA(AOTP_FIELD,but,amn);
}

static long Jsetupbutton(struct Button *but,struct Amjsetup *amj)
{  struct Jvar *jv;
   UBYTE *p;
   AmethodasA(AOTP_FIELD,but,amj);
   if(but->jobject)
   {  if(jv=Jproperty(amj->jc,but->jobject,"value"))
      {  Setjproperty(jv,Propertyvalue,but);
      }
      if(jv=Jproperty(amj->jc,but->jobject,"type"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         switch(but->type)
         {  case BUTTP_SUBMIT:p="submit";break;
            case BUTTP_RESET: p="reset";break;
            case BUTTP_BUTTON:p="button";break;
            default:          p="unknown";
         }
         Jasgstring(amj->jc,jv,p);
      }
      Addjfunction(amj->jc,but->jobject,"click",Methodclick,NULL);
      Addjfunction(amj->jc,but->jobject,"focus",Methodfocus,NULL);
      Addjfunction(amj->jc,but->jobject,"blur",Methodblur,NULL);
      Addjfunction(amj->jc,but->jobject,"toString",Methodtostring,NULL);
      Jaddeventhandler(amj->jc,but->jobject,"onclick",but->onclick);
   }
   if(but->body)
   {  /* Don't add the label contents to the form object, but wait until
       * we are called again with the document object as parent. */
      struct Jobject *fjo=NULL;
      if(but->form)
      {  fjo=(struct Jobject *)Agetattr(but->form,AOBJ_Jobject);
      }
      if(amj->parent!=fjo)
      {  AmethodA(but->body,amj);
      }
   }
   return 0;
}

static void Disposebutton(struct Button *but)
{  if(but->onclick) FREE(but->onclick);
   if(but->onfocus) FREE(but->onfocus);
   if(but->onblur) FREE(but->onblur);
   if(but->body) Adisposeobject(but->body);
   Amethodas(AOTP_FIELD,but,AOM_DISPOSE);
}

static void Deinstallbutton(void)
{  if(bevel) DisposeObject(bevel);
}

static long Dispatch(struct Button *but,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newbutton((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setbutton(but,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getbutton(but,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measurebutton(but,(struct Ammeasure *)amsg);
         break;
      case AOM_LAYOUT:
         result=Layoutbutton(but,(struct Amlayout *)amsg);
         break;
      case AOM_ALIGN:
         result=Alignbutton(but,(struct Amalign *)amsg);
         break;
      case AOM_RENDER:
         result=Renderbutton(but,(struct Amrender *)amsg);
         break;
      case AOM_HITTEST:
         result=Hittestbutton(but,(struct Amhittest *)amsg);
         break;
      case AOM_GOACTIVE:
         result=Goactivebutton(but,(struct Amgoactive *)amsg);
         break;
      case AOM_HANDLEINPUT:
         result=Handleinputbutton(but,(struct Aminput *)amsg);
         break;
      case AOM_GOINACTIVE:
         result=Goinactivebutton(but);
         break;
      case AOM_NOTIFY:
         result=Notifybutton(but,(struct Amnotify *)amsg);
         break;
      case AOM_JSETUP:
         result=Jsetupbutton(but,(struct Amjsetup *)amsg);
         break;
      case AOM_DISPOSE:
         Disposebutton(but);
         break;
      case AOM_DEINSTALL:
         Deinstallbutton();
         break;
      default:
         result=AmethodasA(AOTP_FIELD,but,amsg);
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installbutton(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_BUTTON,Dispatch)) return FALSE;
   if(!(bevel=BevelObject,
      BEVEL_Style,BVS_BUTTON,
      End)) return FALSE;
   GetAttr(BEVEL_VertSize,bevel,(ULONG *)&bevelw);
   GetAttr(BEVEL_HorizSize,bevel,(ULONG *)&bevelh);
   return TRUE;
}
