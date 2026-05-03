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

/* text.c - AWeb html document text element object */

#include "aweb.h"
#include "text.h"
#include "link.h"
#include "application.h"
#include "body.h"
#include "docprivate.h"
#include "object.h"
#include "ttengine.h"
#include <string.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/diskfont.h>
#include <graphics/gfxmacros.h>

static UBYTE *GetFontfaceFromBody(struct Text *tx);

/*------------------------------------------------------------------------*/

extern struct Library *DiskfontBase;

struct Text
{  struct Element elt;
   void *pool;
   struct Tsection *sections,*lastsection;
   long height;            /* Text (font) height */
   struct Colorinfo *color;
   struct Colorinfo *bgcolor;  /* Background color for text highlight */
   struct TextFont *supfont;
   long histart,hilength;  /* Highlighted section. */
   USHORT flags;
   struct Buffer *text;
   UBYTE *ttfontface;      /* Dupstr copy of body font stack face at Addchild (UTF-8 ttengine). */
};

#define TXTF_BLINK      0x0001   /* Blinking text */
#define TXTF_BLINKING   0x0002   /* Is child in WANT_BLINK relationship */
#define TXTF_BLINKON    0x0004   /* Visible blink phase (or not blinking) */

/* A Tsection is a section of this Text's text. Text elemens can be split
 * up into several sections when they don't fit */
struct Tsection
{  struct Tsection *next;  /* Single-linked list to save memory */
   long x,y,w,h;           /* Hitbox of section */
   long textpos,length;    /* Displayed text */
   USHORT flags;
};

#define TSF_ALIGNED     0x0001   /* This section is aligned */
#define TSF_SPACE       0x0002   /* Section ends in space, not counted in length */

#define SHY             0xad     /* Soft hyphen */

/*------------------------------------------------------------------------*/

/* Heuristic Shift_JIS pair detection inside a buffer (lead 0x81-0x9F / 0xE0-0xFC).
 * Only consult this when the document charset is SHIFT_JIS: Latin-1 bytes such as
 * umlaut 0xF6 sit in the SJIS lead range and would false-trigger JKFF on Western pages. */
static BOOL Hassjisbytes(UBYTE *s,long len)
{  UBYTE b0,b1;
   long i;
   if(!s || len<=0) return FALSE;
   for(i=0;i<len;i++)
   {  b0=s[i];
      /* Avoid false positives on UTF-8: many UTF-8 multibyte sequences start with
       * bytes in the 0xE0.. range which overlap Shift_JIS lead bytes. */
      if(b0>=0xC2 && b0<=0xDF)
      {  if(i+1<len)
         {  b1=s[i+1];
            if((b1 & 0xC0)==0x80)
            {  i+=1;
               continue;
            }
         }
      }
      if(b0>=0xE0 && b0<=0xEF)
      {  if(i+2<len)
         {  if(((s[i+1] & 0xC0)==0x80) && ((s[i+2] & 0xC0)==0x80))
            {  i+=2;
               continue;
            }
         }
      }
      if(b0>=0xF0 && b0<=0xF4)
      {  if(i+3<len)
         {  if(((s[i+1] & 0xC0)==0x80) && ((s[i+2] & 0xC0)==0x80) && ((s[i+3] & 0xC0)==0x80))
            {  i+=3;
               continue;
            }
         }
      }
      if((b0>=0x81 && b0<=0x9F) || (b0>=0xE0 && b0<=0xFC))
      {  if(i+1>=len) return TRUE;
         b1=s[i+1];
         if((b1>=0x40 && b1<=0x7E) || (b1>=0x80 && b1<=0xFC))
         {  if(b1!=0x7F) return TRUE;
         }
         return TRUE;
      }
   }
   return FALSE;
}

/* Cache JKFF fonts by y-size. We deliberately keep these open for the duration
 * of the process to avoid per-element open/close churn. */
static struct TextFont *Getjkfffont(short ysize)
{  static struct TextFont *cache[65];
   struct TextAttr ta;
   struct TextFont *font;
   if(ysize<1) ysize=1;
   if(ysize>64) ysize=64;
   if(cache[ysize]) return cache[ysize];
   if(!DiskfontBase) return NULL;
   memset(&ta,0,sizeof(ta));
   ta.ta_Name=(STRPTR)"JKFF.font";
   ta.ta_YSize=ysize;
   font=OpenDiskFont(&ta);
   cache[ysize]=font;
   return font;
}

/* Shift_JIS HTML: same parent walk as Docisutf8; run-pair heuristics only in this mode. */
static BOOL Docisshiftjis(struct Text *tx)
{
   void *walk;
   struct Aobject *ao;
   struct Document *doc;
   int guard;
   if(!tx) return FALSE;
   walk=(void *)Agetattr((struct Aobject *)tx,AOBJ_Layoutparent);
   guard=0;
   while(walk && guard<24)
   {
      ao=(struct Aobject *)walk;
      if(ao->objecttype==AOTP_DOCUMENT)
      {
         doc=(struct Document *)walk;
         return BOOLVAL(doc->charset==DOCCHARSET_SHIFT_JIS);
      }
      walk=(void *)Agetattr(ao,AOBJ_Layoutparent);
      guard++;
   }
   return FALSE;
}

/* UTF-8 HTML: document charset is DOCCHARSET_UTF8; walk AOBJ_Layoutparent to Document
 * (TEXT->BODY->TABLE->...->DOCUMENT). Guard limits runaway chains. */
static BOOL Docisutf8(struct Text *tx)
{
   void *walk;
   struct Aobject *ao;
   struct Document *doc;
   int guard;
   if(!tx) return FALSE;
   walk=(void *)Agetattr((struct Aobject *)tx,AOBJ_Layoutparent);
   guard=0;
   while(walk && guard<24)
   {
      ao=(struct Aobject *)walk;
      if(ao->objecttype==AOTP_DOCUMENT)
      {
         doc=(struct Document *)walk;
         return BOOLVAL(doc->charset==DOCCHARSET_UTF8);
      }
      walk=(void *)Agetattr(ao,AOBJ_Layoutparent);
      guard++;
   }
   return FALSE;
}

/* Document face (HTML FONT / CSS) first so TT_OpenFont tries Georgia, Arial, etc.; then
 * AWEB_UTF8_FONTFACE as fallback for glyphs missing in that face. Previously the global
 * string was always first, so DejaVu won for every element; AOBDY_Fontface GET was missing
 * so GetFontfaceFromBody always saw NULL and only the global list was ever passed. */
static void Utf8TtengineSelectForText(struct Text *tx,struct RastPort *rp)
{
   UBYTE *ff;
   UBYTE buf[512];
   ULONG bl;
   ULONG rem;
   if(!tx || !rp || !Docisutf8(tx)) return;
   ff=GetFontfaceFromBody(tx);
   if(ff && *ff)
   {
      strncpy((char *)buf,(char *)ff,sizeof(buf)-1);
      buf[sizeof(buf)-1]='\0';
      bl=(ULONG)strlen((char *)buf);
      rem=(ULONG)sizeof(buf)-1UL-bl;
      if(rem>2UL)
      {
         buf[bl]=',';
         buf[bl+1]='\0';
         strncat((char *)buf,(char *)AWEB_UTF8_FONTFACE,(size_t)(rem-1UL));
      }
      TTEngineSetFont(rp,tx->font,buf,tx->style);
   }
   else
   {
      TTEngineSetFont(rp,tx->font,(UBYTE *)AWEB_UTF8_FONTFACE,tx->style);
   }
   /* ttengine defaults to Latin-1; UTF-8 document buffers need TT_Encoding_UTF8 or metrics wrap wrong. */
   if(IsTTEngineFontActive(rp))
   {
      TTEngineSetRastPortTextEncoding(rp,AWEB_TT_ENCODING_UTF8);
   }
}

/* Word-wrap width probe: use ttengine TextFit when a TT font is active. */
static long Textfitcommon(struct Text *tx,struct RastPort *rp,UBYTE *s,long cnt,
   struct TextExtent *te,long maxw,long maxh)
{
   long got;
   (void)tx;
   if(IsTTEngineFontActive(rp))
   {
      got=(long)TTEngineTextFit(rp,s,(UWORD)cnt,te,NULL,1,(UWORD)maxw,(UWORD)maxh);
      return got;
   }
   return (long)TextFit(rp,s,(WORD)cnt,te,NULL,1,(UWORD)maxw,(UWORD)maxh);
}

/* Clear all sections, or only non-aligned sections */
static void Clearsections(struct Text *tx,BOOL all)
{  struct Tsection *ts,*next;
   tx->lastsection=NULL;
   for(ts=tx->sections;ts;ts=next)
   {  next=ts->next;
      if(!(ts->flags&TSF_ALIGNED)) all=TRUE;
      if(all) FREE(ts);
      else tx->lastsection=ts;
   }
   if(tx->lastsection) tx->lastsection->next=NULL;
   else tx->sections=NULL;
}

/* Create a new section */
static struct Tsection *Newsection(struct Text *tx)
{  struct Tsection *ts=PALLOCSTRUCT(Tsection,1,MEMF_CLEAR,tx->pool);
   if(ts)
   {  if(tx->lastsection) tx->lastsection->next=ts;
      else tx->sections=ts;
      tx->lastsection=ts;
   }
   return ts;
}

/* Return next text position after last section */
static long Nextpos(struct Text *tx)
{  struct Tsection *ts=tx->lastsection;
   long pos;
   if(ts)
   {  pos=ts->textpos+ts->length;
      if(ts->flags&TSF_SPACE) pos++;
   }
   else pos=tx->textpos;
   return pos;
}

/* Check if point falls within text sections */
static BOOL Pointintext(struct Text *tx,long x,long y)
{  struct Tsection *ts;
   for(ts=tx->sections;ts;ts=ts->next)
   {  if((ts->flags&TSF_ALIGNED)
       && x>=ts->x && x<ts->x+ts->w && y>=ts->y && y<ts->y+ts->h)
      {  return TRUE;
      }
   }
   return FALSE;
}

/* Only complement this section (start,end). Pass a valid and clipped Coords. */
static void Highlighttext(struct Text *tx,struct Coords *coo,long start,long end)
{  struct Tsection *ts;
   long histart,hiend;  /* Pixels */
   if(Docisutf8(tx)) Utf8TtengineSelectForText(tx,coo->rp);
   else
   {  TTEngineSetFont(coo->rp,tx->font,NULL,tx->style);
      if(!IsTTEngineFontActive(coo->rp))
      {  SetSoftStyle(coo->rp,0,0x0f);
      }
   }
   SetDrMd(coo->rp,COMPLEMENT);
   for(ts=tx->sections;ts;ts=ts->next)
   {  if(ts->flags&TSF_ALIGNED)
         {  if(start<ts->textpos+ts->length && end>=ts->textpos)
         {  if(start<=ts->textpos)
            {  histart=0;
            }
            else
            {  histart=Textlength(coo->rp,tx->text->buffer+ts->textpos,start-ts->textpos);
            }
            if(end+1>=ts->textpos+ts->length)
            {  hiend=ts->w;
            }
            else
            {  hiend=Textlength(coo->rp,tx->text->buffer+ts->textpos,end+1-ts->textpos);
            }
            RectFill(coo->rp,
               ts->x+histart+coo->dx,ts->y+coo->dy,
               ts->x+hiend-1+coo->dx,ts->y+tx->height-1+coo->dy);
         }
      }
   }
   SetDrMd(coo->rp,JAM1);
}

/* If text is followed by other text with the same style, use the smaller width,
 * else use the larger realw */
static long Realwidth(struct Text *tx,long width,long realw)
{  struct Element *next=tx->next;
   if(next->next && next->objecttype==AOTP_TEXT
   && (next->style&(FSF_BOLD|FSF_ITALIC))==(tx->style&(FSF_BOLD|FSF_ITALIC)))
   {  realw=width;
   }
   return realw;
}

/*------------------------------------------------------------------------*/

/* Get fontface from body context if available */
static UBYTE *GetFontfaceFromBody(struct Text *tx)
{
   void *body;
   void *cframe;
   struct Aobject *obj;
   UBYTE *fontface;
   
   if(!tx || !tx->cframe)
   {
      return NULL;
   }
   /* Face captured when BODY added this TEXT (FONT/CSS stack); live AOBDY_Fontface is only base after parse. */
   if(tx->ttfontface && tx->ttfontface[0])
   {
      return tx->ttfontface;
   }
   
   cframe = tx->cframe;
   obj = (struct Aobject *)cframe;
   
   /* cframe can be either a Body or Frame object */
   if(obj)
   {
      /* Check if it's a Body object type */
      if(obj->objecttype == AOTP_BODY)
      {
         /* cframe is already a Body, use it directly */
         body = cframe;
         fontface = (UBYTE *)Agetattr(body, AOBDY_Fontface);
         if(fontface)
         {
            return fontface;
         }
      }
      else if(obj->objecttype == AOTP_FRAME)
      {
         /* cframe is a Frame - try to get the Body from the Frame's cframe */
         /* Frames have a cframe that points to their owner Body or Frame */
         cframe = (void *)Agetattr(cframe, AOBJ_Cframe);
         if(cframe)
         {
            obj = (struct Aobject *)cframe;
            if(obj && obj->objecttype == AOTP_BODY)
            {
               body = cframe;
               fontface = (UBYTE *)Agetattr(body, AOBDY_Fontface);
               if(fontface)
               {
                  return fontface;
               }
            }
         }
      }
   }
   
   /* If we couldn't find a Body with fontface, return NULL */
   /* This will cause TTEngineSetFont to fall back to TextFont name mapping */
   return NULL;
}

/*------------------------------------------------------------------------*/

static long Measuretext(struct Text *tx,struct Ammeasure *amm)
{  long w,width,realw;
   UBYTE *p,*q,*end;
   p=amm->text->buffer+tx->textpos;
   if(Docisutf8(tx))
   {  Utf8TtengineSelectForText(tx,mrp);
   }
   else if(tx && tx->text && tx->text->buffer && tx->length>0)
   {  if(Docisshiftjis(tx) && Hassjisbytes((UBYTE *)tx->text->buffer+tx->textpos,tx->length))
      {  struct TextFont *jkff;
         jkff=Getjkfffont((short)tx->font->tf_YSize);
         if(jkff)
         {  tx->font=jkff;
         }
         TTEngineSetFont(mrp,tx->font,NULL,tx->style);
      }
      else
      {
         TTEngineSetFont(mrp,tx->font,GetFontfaceFromBody(tx),tx->style);
      }
   }
   else
   {
      TTEngineSetFont(mrp,tx->font,GetFontfaceFromBody(tx),tx->style);
   }
   if(!IsTTEngineFontActive(mrp))
   {
      SetFont(mrp,tx->font);
      SetSoftStyle(mrp,tx->style,0x0f);
   }
   width=Textlengthext(mrp,p,tx->length,&realw);
   if(realw!=width) width=Realwidth(tx,width,realw);
   /* Set aow to ensure correct alignment of text bullets: */
   tx->aow=width;
   tx->height=(long)TTEngineLineBoxHeight(mrp,tx->font);
   if(amm->ammr)
   {  amm->ammr->width=width;
      /* Compute minimum width = width of longest word */
      if(amm->flags&AMMF_NOWRAP)
      {  amm->ammr->minwidth=amm->addwidth+width;
         amm->ammr->addwidth=amm->ammr->minwidth;
      }
      else if(tx->eltflags&(ELTF_NOBR|ELTF_PREFORMAT))
      {  amm->ammr->minwidth=amm->addwidth+width;
         amm->ammr->addwidth=amm->ammr->minwidth;
      }
      else
      {  end=p+tx->length;
         w=amm->addwidth;
         while(p<end)
         {  long wlen;
            for(q=p;q<end && *q!=' ' && *q!=SHY;q++);
            wlen=q-p+((q<end && *q==SHY)?1L:0L);
            width=Textlengthext(mrp,p,wlen,&realw);
            if(width!=realw && q>=end) width=Realwidth(tx,width,realw);
            w+=width;
            if(w>amm->ammr->minwidth) amm->ammr->minwidth=w;
            if(q>=end) amm->ammr->addwidth=w;
            else amm->ammr->addwidth=0;
            p=q+1;
            w=0;
         }
      }
   }
   return 0;
}

static long Layouttext(struct Text *tx,struct Amlayout *aml)
{  short result;
   long pos,width,length,realw;
   struct TextExtent te;
   UBYTE *p,*q;
   struct Tsection *ts,*tsa;
   BOOL space=FALSE;
   if(aml->flags&AMLF_BREAK)
   {  /* Shorten last section to the break position.
       * If no break found, remove the section completely. */
      ts=tx->lastsection;
      if(ts)
      {  p=aml->text->buffer+ts->textpos;
         q=p+ts->length-1;
         if(ts->flags&TSF_SPACE) q++;
         /* Now q points to the last character in this section.
          * Search for the last space or soft hyphen. */
         while(q>p && *q!=' ' && *q!=SHY) q--;
         if(*q==' ' && !(tx->eltflags&(ELTF_NOBR|ELTF_PREFORMAT)))
         {  /* Space found. Shorten this section, but include the space. */
            ts->flags|=TSF_SPACE;  /* Don't include space in length */
            ts->length=q-p;
            if(Docisutf8(tx)) Utf8TtengineSelectForText(tx,mrp);
            else TTEngineSetFont(mrp,tx->font,NULL,tx->style);
            if(!IsTTEngineFontActive(mrp))
            {
               SetSoftStyle(mrp,tx->style,0x0f);
            }
            ts->w=Textlength(mrp,p,ts->length);
            /* See if this section (plus the trailing space) fits */
            if(ts->textpos+ts->length+1<tx->textpos+tx->length) result=AMLR_MORE;
            else result=AMLR_OK;
            if(aml->amlr) aml->amlr->endx=ts->x+ts->w;
         }
         else if(*q==SHY && !(tx->eltflags&(ELTF_NOBR|ELTF_PREFORMAT)))
         {  /* Soft hyphen found. Shorten this section and include the SHY. */
            ts->length=q-p+1;
            if(Docisutf8(tx)) Utf8TtengineSelectForText(tx,mrp);
            else TTEngineSetFont(mrp,tx->font,NULL,tx->style);
            if(!IsTTEngineFontActive(mrp))
            {
               SetSoftStyle(mrp,tx->style,0x0f);
            }
            ts->w=Textlength(mrp,p,ts->length);
            /* See if this section fits */
            if(ts->textpos+ts->length<tx->textpos+tx->length) result=AMLR_MORE;
            else result=AMLR_OK;
            if(aml->amlr) aml->amlr->endx=ts->x+ts->w;
         }
         else
         {  /* No break found. Remove section. */
            FREE(ts);
            if(tx->sections==ts)
            {  tx->sections=tx->lastsection=NULL;
            }
            else
            {  for(tsa=tx->sections;tsa->next!=ts;tsa=tsa->next);
               tsa->next=NULL;
               tx->lastsection=tsa;
            }
            result=AMLR_BREAK;
         }
      }
      else result=AMLR_BREAK;
   }
   else
   {  /* First set endx, may be overwritten later. */
      if(aml->amlr) aml->amlr->endx=aml->startx;
      /* If not MORE, it's a new layout. Clear all sections.
       * If RETRY, clear only non-aligned sections. */
      if(!(aml->flags&AMLF_MORE)) Clearsections(tx,TRUE);
      else if(aml->flags&AMLF_RETRY) Clearsections(tx,FALSE);
      /* Find the current starting position */
      pos=Nextpos(tx);
      length=tx->length-(pos-tx->textpos);
      p=aml->text->buffer+pos;
      if(length>0)
      {  /* Latin-1 layout on mrp must match Rendertext ttengine+FACE; else widths come from
          * bitmap TextLength and bold Georgia runs past the following sibling (e.g. <B>Note:</B>). */
         if(Docisutf8(tx))
         {  Utf8TtengineSelectForText(tx,mrp);
            if(!IsTTEngineFontActive(mrp))
            {
               SetSoftStyle(mrp,tx->style,0x0f);
            }
         }
         else if(tx->text && tx->text->buffer
            && Docisshiftjis(tx) && Hassjisbytes(p,length))
         {  struct TextFont *jkff;
            jkff=Getjkfffont((short)tx->font->tf_YSize);
            if(jkff)
            {
               tx->font=jkff;
            }
            TTEngineSetFont(mrp,tx->font,NULL,tx->style);
            if(!IsTTEngineFontActive(mrp))
            {
               SetSoftStyle(mrp,tx->style,0x0f);
            }
         }
         else
         {
            TTEngineSetFont(mrp,tx->font,GetFontfaceFromBody(tx),tx->style);
            if(!IsTTEngineFontActive(mrp))
            {
               SetFont(mrp,tx->font);
               SetSoftStyle(mrp,tx->style,0x0f);
            }
         }
         width=Textlengthext(mrp,p,length,&realw);
         if(realw!=width) width=Realwidth(tx,width,realw);
      }
      else
      {  width=0;
      }
      tx->height=(long)TTEngineLineBoxHeight(mrp,tx->font);
      if(aml->startx+width<=aml->width)
      {  /* Everything fits */
         result=AMLR_OK;
      }
      else if(tx->eltflags&(ELTF_NOBR|ELTF_PREFORMAT))
      {  /* Text doesn't fit, but don't line break. */
         if(aml->amlr)
         {  aml->amlr->endx=aml->startx+width;
         }
         if(aml->flags&AMLF_FORCE)
         {  result=AMLR_FBREAK;
         }
         else
         {  result=AMLR_BREAK;
            length=0;   /* Prevent creation of section. */
         }
      }
      else
      {  /* Text doesn't fit. Try to find a break position within target width. */
         /* Word wrap: TextFit on bitmap font is wrong for UTF-8; use ttengine when active. */
         length=Textfitcommon(tx,mrp,p,length,&te,aml->width-aml->startx,tx->height);
         q=p+length;
         /* now q points to first character not to fit.
          * Search for last space or soft hyphen. Accept a space in the last character
          * but not a SHY. */
         if(q>p && *q!=' ') q--; /* Skip over any SHY at the end */
         while(q>p && *q!=' ' && *q!=SHY) q--;
         if(*q==' ')
         {  /* Space found. Break the text after the space but don't include
             * the space in the section. If the space found is the last character,
             * then we fit after all. */
            if(q==aml->text->buffer+pos+tx->length-1)
            {  result=AMLR_OK;
            }
            else
            {  result=AMLR_MORE;
            }
            length=q-p;
            /* Make sure font is set before measuring */
            if(Docisutf8(tx)) Utf8TtengineSelectForText(tx,mrp);
            else TTEngineSetFont(mrp,tx->font,NULL,tx->style);
            if(!IsTTEngineFontActive(mrp))
            {
               SetSoftStyle(mrp,tx->style,0x0f);
            }
            width=Textlength(mrp,p,length);
            space=TRUE;
         }
         else if(*q==SHY)
         {  /* Soft hyphen found (not the last character). Break the text after the SHY. */
            result=AMLR_MORE;
            length=q-p+1;
            if(Docisutf8(tx)) Utf8TtengineSelectForText(tx,mrp);
            else TTEngineSetFont(mrp,tx->font,NULL,tx->style);
            if(!IsTTEngineFontActive(mrp))
            {
               SetSoftStyle(mrp,tx->style,0x0f);
            }
            width=Textlength(mrp,p,length);
            space=FALSE;
         }
         else
         {  /* No space or SHY found. Find a break position after the target width. */
            UBYTE *r=aml->text->buffer+tx->textpos+tx->length-1; /* r = last char of text */
            q=p+length;
            while(q<r && *q!=' ' && *q!=SHY) q++;
            /* Now q points either to a space, to a SHY before the last character,
             * or to the last character. Break here */
            if(*q==' ') space=TRUE;
            else q++;   /* include SHY or last character */
            length=q-p;
            if(Docisutf8(tx)) Utf8TtengineSelectForText(tx,mrp);
            else TTEngineSetFont(mrp,tx->font,NULL,tx->style);
            if(!IsTTEngineFontActive(mrp))
            {
               SetSoftStyle(mrp,tx->style,0x0f);
            }
            width=Textlength(mrp,p,length);
            /* If FORCE, use this text. Else set the resulting endx to show our needs. */
            if(aml->flags&AMLF_FORCE)
            {  if(q<=r) result=AMLR_MORE;
               else result=AMLR_FBREAK;
            }
            else
            {  length=0;
               space=FALSE;
               result=AMLR_BREAK;
               if(aml->amlr)
               {  aml->amlr->endx=aml->startx+width;
               }
            }
         }
      }
      /* Now create a section for the fitting text. */
      if(length>0 || space)
      {  if(ts=Newsection(tx))
         {  ts->x=aml->startx;
            /* ts->y will be set when aligned */
            ts->w=width;
            ts->h=tx->height;
            ts->textpos=p-aml->text->buffer;
            ts->length=length;
            if(space) ts->flags|=TSF_SPACE;
            if(aml->amlr)
            {  aml->amlr->endx=ts->x+ts->w;
               switch(tx->valign)
               {  case VALIGN_SUB:  /* align middle of font with baseline */
                     aml->amlr->above=tx->font->tf_YSize/2;
                     aml->amlr->below=tx->font->tf_YSize-aml->amlr->above;
                     break;
                  case VALIGN_SUP:  /* align 1/3 of font with top of supfont */
                     if(tx->supfont)
                     {  aml->amlr->above=tx->supfont->tf_Baseline +
                           tx->font->tf_YSize/3;
                        aml->amlr->below=tx->supfont->tf_YSize-aml->amlr->above;
                        break;
                     }
                     /* else fall through: */
                  default:
                     aml->amlr->above=tx->font->tf_Baseline+1;
                     aml->amlr->below=tx->height-aml->amlr->above;
               }
            }
         }
         else
         {  /* No more memory, avoid loop (document is shown distorted anyway) */
            result=AMLR_OK;
         }
      }
   }
   if(aml->amlr) aml->amlr->result=result;
   return 0;
}

static long Aligntext(struct Text *tx,struct Amalign *ama)
{  struct Tsection *ts;
   /* Align the last section (previous sections should have been aligned already) */
   ts=tx->lastsection;
   if(ts && !(ts->flags&TSF_ALIGNED))
   {  ts->x+=ama->dx;
      switch(tx->valign)
      {  case VALIGN_SUB:
            ts->y=ama->y+ama->baseline-tx->font->tf_Baseline/2;
            break;
         case VALIGN_SUP:
            if(tx->supfont)
            {  ts->y=ama->y+ama->baseline-tx->supfont->tf_Baseline -
                  tx->font->tf_YSize/3;
               break;
            }
            /* else fall through: */
         default:
            ts->y=ama->y+ama->baseline-tx->font->tf_Baseline;
      }
      ts->flags|=TSF_ALIGNED;
      /* Make sure tx->aox/y/w/h is outer box */
      if(ts==tx->sections)
      {  /* first section */
         tx->aox=ts->x;
         tx->aoy=ts->y;
         tx->aow=ts->w;
         tx->aoh=ts->h;
      }
      else
      {  if(ts->x<tx->aox)
         {  tx->aow+=tx->aox-ts->x;
            tx->aox=ts->x;
         }
         if(ts->y<tx->aoy)
         {  tx->aoh+=tx->aoy-ts->y;
            tx->aoy=ts->y;
         }
         if(ts->x+ts->w>tx->aox+tx->aow) tx->aow=ts->x+ts->w-tx->aox;
         if(ts->y+ts->h>tx->aoy+tx->aoh) tx->aoh=ts->y+ts->h-tx->aoy;
      }
   }
   return 0;
}

static long Rendertext(struct Text *tx,struct Amrender *amr)
{  struct Coords *coo,coords={0};
   BOOL clip=FALSE;
   ULONG clipkey=NULL;
   struct RastPort *rp;
   struct Tsection *ts;
   short pen;
   USHORT pattern=0;
   BOOL clearpattern=FALSE,highlight,blinkoff;
   long histart,hiend;
   if(!(coo=amr->coords))
   {  Framecoords(tx->cframe,&coords);
      coo=&coords;
      clip=TRUE;
   }
   if(coo->rp)
   {  rp=coo->rp;
      if(clip) clipkey=Clipto(rp,coo->minx,coo->miny,coo->maxx,coo->maxy);
      /* Non-UTF-8: one font for the whole element. UTF-8 uses ttengine once before drawing sections. */
      if(!Docisutf8(tx))
      {  if(tx && tx->text && tx->text->buffer && tx->length>0)
         {  if(Docisshiftjis(tx) && Hassjisbytes((UBYTE *)tx->text->buffer+tx->textpos,tx->length))
            {  struct TextFont *jkff;
               jkff=Getjkfffont((short)tx->font->tf_YSize);
               if(jkff)
               {  tx->font=jkff;
               }
            }
            TTEngineSetFont(rp,tx->font,GetFontfaceFromBody(tx),tx->style);
         }
         else
         {  TTEngineSetFont(rp,tx->font,GetFontfaceFromBody(tx),tx->style);
         }
         /* Only use SetSoftStyle if ttengine is not active (ttengine handles styles via font selection) */
         if(!IsTTEngineFontActive(rp))
         {
            SetSoftStyle(rp,tx->style,0x0f);
         }
      }
      else
      {  Utf8TtengineSelectForText(tx,rp);
         if(!IsTTEngineFontActive(rp))
         {
            SetSoftStyle(rp,tx->style,0x0f);
         }
      }
      if(tx->link)
      {  BOOL nodecoration;
         nodecoration = Agetattr(tx->link,AOLNK_NoDecoration);
         if(Agetattr(tx->link,AOLNK_Visited))
         {  pen=coo->vlinkcolor;
            if(prefs.browser.ullink && !nodecoration)
            {  pattern=0xcccc;
               clearpattern=TRUE;
            }
         }
         else
         {  pen=coo->linkcolor;
            if(prefs.browser.ullink && !nodecoration) pattern=0xffff;
         }
         if(Agetattr(tx->link,AOLNK_Selected))
         {  pen=coo->alinkcolor;
         }
      }
      else pen=coo->textcolor;
      /* Always apply text color if set, regardless of background images setting */
      if(tx->color && tx->color->pen>=0) pen=tx->color->pen;
      SetAPen(rp,pen);
      blinkoff=(tx->flags&TXTF_BLINKING) && !(tx->flags&TXTF_BLINKON);
      for(ts=tx->sections;ts;ts=ts->next)
      {  if(ts->flags&TSF_ALIGNED)
         {  /* Link underline follows the TextFont cell (tf_YSize), not TTEngineLineBoxHeight:
             * line spacing uses the taller box so rows do not collide; decoration stays under glyphs. */
            long linkunderliney;
            linkunderliney=ts->y+(long)tx->font->tf_YSize-1L;
            highlight=FALSE;
            if(tx->hilength
            && tx->histart<ts->textpos+ts->length && tx->histart+tx->hilength>ts->textpos)
            {  if(tx->histart<=ts->textpos)
               {  histart=0;
               }
               else
               {  histart=Textlength(rp,tx->text->buffer+ts->textpos,tx->histart-ts->textpos);
               }
               if(tx->histart+tx->hilength>=ts->textpos+ts->length)
               {  hiend=ts->w;
               }
               else
               {  hiend=Textlength(rp,tx->text->buffer+ts->textpos,
                     tx->histart+tx->hilength-ts->textpos);
               }
               highlight=TRUE;
            }
            /* Clear background before highlighting */
            if(highlight && !(amr->flags&AMRF_CLEAR))
            {  Erasebg(tx->cframe,coo,ts->x+histart,ts->y,ts->x+hiend-1,ts->y+tx->height-1);
            }
            /* Clear background before dashed underlining */
            if(clearpattern)
            {  Erasebg(tx->cframe,coo,ts->x,linkunderliney,ts->x+ts->w-1,linkunderliney);
            }
            /* Clear background in blink off phase or draghighlight */
            if(blinkoff || (amr->flags&AMRF_CLEAR))
            {  Erasebg(tx->cframe,coo,ts->x,ts->y,ts->x+ts->w-1,ts->y+tx->height-1);
            }
            /* Draw background color if set (like a text highlight) */
            if(tx->bgcolor && tx->bgcolor->pen >= 0 && !blinkoff && !(amr->flags&AMRF_CLEAR))
            {  short oldpen;
               oldpen = GetAPen(rp);
               SetAPen(rp,tx->bgcolor->pen);
               RectFill(rp,
                  ts->x+coo->dx,ts->y+coo->dy,
                  ts->x+ts->w-1+coo->dx,ts->y+tx->height-1+coo->dy);
               SetAPen(rp,oldpen);
            }
            if(!blinkoff)
            {  Move(rp,ts->x+coo->dx,ts->y+tx->font->tf_Baseline+coo->dy);
               TTEngineText(rp,tx->text->buffer+ts->textpos,ts->length);
               /* ttengine TT_SoftStyle_Overstriked already paints strike into the glyph pixmap. */
               if((tx->style&FSF_STRIKE) && !IsTTEngineFontActive(rp))
               {  Move(rp,ts->x+coo->dx,ts->y+tx->font->tf_YSize/2+coo->dy);
                  Draw(rp,ts->x+ts->w+coo->dx-1,ts->y+tx->font->tf_YSize/2+coo->dy);
               }
               if(pattern)
               {  SetDrPt(rp,pattern);
                  Move(rp,ts->x+coo->dx,linkunderliney+coo->dy);
                  Draw(rp,ts->x+coo->dx+ts->w-1,linkunderliney+coo->dy);
                  SetDrPt(rp,0xffff);
               }
            }
            if(highlight && !(amr->flags&AMRF_CLEARHL))
            {  SetDrMd(rp,COMPLEMENT);
               RectFill(rp,
                  ts->x+histart+coo->dx,ts->y+coo->dy,
                  ts->x+hiend-1+coo->dx,ts->y+tx->height-1+coo->dy);
               SetDrMd(rp,JAM1);
            }
         }
      }
      if(clip) Unclipto(clipkey);
   }
   return 0;
}

static long Settext(struct Text *tx,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=Amethodas(AOTP_ELEMENT,tx,AOM_SET,ams->tags);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Pool:
            tx->pool=(void *)tag->ti_Data;
            break;
         case AOELT_Color:
            tx->color=(struct Colorinfo *)tag->ti_Data;
            break;
         case AOELT_Supfont:
            tx->supfont=(struct TextFont *)tag->ti_Data;
            break;
         case AOELT_Resetlayout:
            Clearsections(tx,TRUE);
            break;
         case AOBJ_Cframe:
            if(tag->ti_Data)
            {  if(tx->flags&TXTF_BLINK)
               {  Aaddchild(Aweb(),tx,AOREL_APP_WANT_BLINK);
                  tx->flags|=TXTF_BLINKING;
               }
            }
            else
            {  /* Clear highlighting if becoming undisplayed */
               tx->hilength=0;
               if(tx->flags&TXTF_BLINKING)
               {  Aremchild(Aweb(),tx,AOREL_APP_WANT_BLINK);
                  tx->flags&=~TXTF_BLINKING;
               }
            }
            break;
         case AOTXT_Blink:
            SETFLAG(tx->flags,TXTF_BLINK,tag->ti_Data);
            break;
         case AOTXT_Text:
            tx->text=(struct Buffer *)tag->ti_Data;
            break;
         case AOTXT_Bgcolor:
            tx->bgcolor=(struct Colorinfo *)tag->ti_Data;
            break;
         case AOELT_Fontfacestr:
            if(tx->ttfontface)
            {  FREE(tx->ttfontface);
               tx->ttfontface=NULL;
            }
            tx->ttfontface=(UBYTE *)tag->ti_Data;
            break;
         case AOAPP_Blink:
            SETFLAG(tx->flags,TXTF_BLINKON,tag->ti_Data);
            Arender(tx,NULL,0,0,AMRMAX,AMRMAX,0,tx->text);
            break;
      }
   }
   return result;
}

static struct Text *Newtext(struct Amset *ams)
{  struct Text *tx;
   if(tx=Allocobject(AOTP_TEXT,sizeof(struct Text),ams))
   {  tx->flags|=TXTF_BLINKON;
      Settext(tx,ams);
   }
   return tx;
}

static long Gettext(struct Text *tx,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=AmethodasA(AOTP_ELEMENT,tx,ams);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {           case AOBJ_Clipdrag:
            PUTATTR(tag,TRUE);
            break;
         case AOELT_Fontfacestr:
            PUTATTR(tag,tx->ttfontface);
            break;
      }
   }
   return result;
}

static long Hittesttext(struct Text *tx,struct Amhittest *amh)
{  long result=0;
   struct Coords *coo,coords={0};
   if(!(coo=amh->coords))
   {  Framecoords(tx->cframe,&coords);
      coo=&coords;
   }
   if(Pointintext(tx,amh->xco-coo->dx,amh->yco-coo->dy))
   {  result=AmethodasA(AOTP_ELEMENT,tx,amh);
   }
   return result;
}

static long Movetext(struct Text *tx,struct Ammove *amm)
{  struct Tsection *ts;
   for(ts=tx->sections;ts;ts=ts->next)
   {  ts->x+=amm->dx;
      ts->y+=amm->dy;
   }
   AmethodasA(AOTP_ELEMENT,tx,amm);
   return 0;
}

static long Searchpostext(struct Text *tx,struct Amsearch *ams)
{  struct Tsection *ts;
   for(ts=tx->sections;ts;ts=ts->next)
   {  if(ams->top<ts->y+ts->h)
      {  ams->pos=ts->textpos;
         return 1;
      }
   }
   return 0;
}

static long Searchsettext(struct Text *tx,struct Amsearch *ams)
{  struct Tsection *ts;
   if(ams->pos<tx->textpos+tx->length)
   {  for(ts=tx->sections;ts;ts=ts->next)
      {  if(ams->pos<ts->textpos+ts->length)
         {  /* Fill in top if it isn't yet set. Break out if no highlight
             * or if highlight ends in this text. Otherwise continue
             * and let successors know the highlight. */
            if(ams->top<0)
            {  ams->top=ts->y;
               if(ams->pos<=ts->textpos)
               {  ams->left=ts->x;
               }
               else
               {  if(Docisutf8(tx)) Utf8TtengineSelectForText(tx,mrp);
                  else TTEngineSetFont(mrp,tx->font,NULL,tx->style);
                  if(!IsTTEngineFontActive(mrp))
                  {
                     SetSoftStyle(mrp,tx->style,0x0f);
                  }
                  ams->left=ts->x+
                     Textlength(mrp,ams->text->buffer+ts->textpos,ams->pos-ts->textpos);
               }
            }
            if(ams->flags&AMSF_HIGHLIGHT)
            {  tx->histart=ams->pos;
               tx->hilength=ams->length;
               Arender(tx,NULL,0,0,AMRMAX,AMRMAX,0,ams->text);
               if(ams->pos+ams->length<=tx->textpos+tx->length) return 1;
               else break;
            }
            if(ams->flags&AMSF_UNHIGHLIGHT)
            {  Arender(tx,NULL,0,0,AMRMAX,AMRMAX,AMRF_CLEARHL,ams->text);
               tx->hilength=0;
               if(ams->pos+ams->length<=tx->textpos+tx->length) return 1;
               else break;
            }
            return 1;
         }
      }
   }
   return 0;
}

static long Dragtesttext(struct Text *tx,struct Amdragtest *amd)
{  struct Tsection *ts;
   struct TextExtent te={0};
   long result=0;
   long x,y;
   x=amd->xco-amd->coords->dx;
   y=amd->yco-amd->coords->dy;
   for(ts=tx->sections;ts;ts=ts->next)
   {  if((ts->y<=y && ts->y+ts->h>y && ts->x+ts->w>x) || ts->y>y)
      {  /* Matching section */
         result=AMDR_HIT;
         if(amd->amdr)
         {  amd->amdr->object=tx;
            /* Object position is buffer position of character hit */
            if(x<ts->x || y<ts->y) 
            {  amd->amdr->objpos=ts->textpos;
            }
            else
            {  if(Docisutf8(tx)) Utf8TtengineSelectForText(tx,mrp);
               else TTEngineSetFont(mrp,tx->font,NULL,0);  /* No style because italics extension */
               if(!IsTTEngineFontActive(mrp))
               {
                  SetSoftStyle(mrp,0,0x0f);
               }
               amd->amdr->objpos=ts->textpos+Textfitcommon(tx,mrp,
                  tx->text->buffer+ts->textpos,ts->length,&te,x-ts->x,tx->height);
            }
         }
         break;
      }
   }
   return result;
}

/* Returns new state */
static USHORT Draglimits(struct Text *tx,void *startobject,ULONG startobjpos,
   void *endobject,ULONG endobjpos,USHORT state,long *startp,long *lengthp)
{  long s=0,l=0;
   if(tx==startobject)
   {  switch(state)
      {  case AMDS_BEFORE:
            state=AMDS_RENDER;
            if(tx==endobject)
            {  state=AMDS_AFTER;
               if(endobjpos>=startobjpos)
               {  s=startobjpos;
                  l=endobjpos-s+1;
               }
               else
               {  s=endobjpos;
                  l=startobjpos-s+1;
               }
            }
            else
            {  s=startobjpos;
               l=tx->textpos+tx->length-s;
            }
            break;
         case AMDS_REVERSE:
            state=AMDS_AFTER;
            s=tx->textpos;
            l=startobjpos-tx->textpos+1;
            break;
      }
   }
   else if(tx==endobject)
   {  switch(state)
      {  case AMDS_BEFORE:
            state=AMDS_REVERSE;
            s=endobjpos;
            l=tx->textpos+tx->length-s;
            break;
         case AMDS_RENDER:
            state=AMDS_AFTER;
            s=tx->textpos;
            l=endobjpos-s+1;
            break;
      }
   }
   else
   {  switch(state)
      {  case AMDS_BEFORE:
            s=0;
            l=0;
            break;
         case AMDS_RENDER:
         case AMDS_REVERSE:
            s=tx->textpos;
            l=tx->length;
            break;
         case AMDS_AFTER:
            s=0;
            l=0;
            if(!tx->histart)
            {  state=AMDS_DONE;
            }
            break;
      }
   }
   *startp=s;
   *lengthp=l;
   return state;
}

static long Dragrendertext(struct Text *tx,struct Amdragrender *amd)
{  long s,l,e,olde;
   struct Coords *coo;
   amd->state=Draglimits(tx,amd->startobject,amd->startobjpos,
      amd->endobject,amd->endobjpos,amd->state,&s,&l);
   if(tx->histart!=s || tx->hilength!=l)
   {  coo=Clipcoords(tx->cframe,amd->coords);
      e=s+l-1;
      olde=tx->histart+tx->hilength-1;
      if(!tx->hilength)
      {  /* Only highlight new */
         if(l) Highlighttext(tx,coo,s,e);
      }
      else if(!l)
      {  /* Only clear old */
         if(tx->hilength) Highlighttext(tx,coo,tx->histart,olde);
      }
      else if(olde<s || e<tx->histart)
      {  /* Old and new sections are disjunct */
         Highlighttext(tx,coo,tx->histart,olde);
         Highlighttext(tx,coo,s,e);
      }
      else
      {  /* Overlapping sections */
         if(tx->histart<s)
         {  Highlighttext(tx,coo,tx->histart,s-1);
         }
         else if(s<tx->histart)
         {  Highlighttext(tx,coo,s,tx->histart-1);
         }
         if(olde>e)
         {  Highlighttext(tx,coo,e+1,olde);
         }
         else if(e>olde)
         {  Highlighttext(tx,coo,olde+1,e);
         }
      }
      tx->histart=s;
      tx->hilength=l;
      Unclipcoords(coo);
   }
   return 0;
}

static long Dragcopytext(struct Text *tx,struct Amdragcopy *amd)
{  long s,l;
   short i,n;
   amd->state=Draglimits(tx,amd->startobject,amd->startobjpos,
      amd->endobject,amd->endobjpos,amd->state,&s,&l);
   if(l)
   {  if(tx->leftindent
      && (!amd->clip->length || amd->clip->buffer[amd->clip->length-1]=='\n'))
      {  n=tx->leftindent;
         if(tx->halign&HALIGN_BULLET) n--;
         for(i=0;i<n;i++)
         {  Addtobuffer(amd->clip,"    ",4);
         }
      }
      if(tx->halign&HALIGN_BULLET)
      {  Addtobuffer(amd->clip,"  ",2);
      }
      Addtobuffer(amd->clip,tx->text->buffer+s,l);
   }
   return 0;
}

static void Disposetext(struct Text *tx)
{  Clearsections(tx,TRUE);
   if(tx->flags&TXTF_BLINKING)
   {  Aremchild(Aweb(),tx,AOREL_APP_WANT_BLINK);
   }
   if(tx->ttfontface)
   {  FREE(tx->ttfontface);
      tx->ttfontface=NULL;
   }
   Amethodas(AOTP_ELEMENT,tx,AOM_DISPOSE);
}

static long Dispatch(struct Text *tx,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newtext((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Settext(tx,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Gettext(tx,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measuretext(tx,(struct Ammeasure *)amsg);
         break;
      case AOM_LAYOUT:
         result=Layouttext(tx,(struct Amlayout *)amsg);
         break;
      case AOM_ALIGN:
         result=Aligntext(tx,(struct Amalign *)amsg);
         break;
      case AOM_RENDER:
         result=Rendertext(tx,(struct Amrender *)amsg);
         break;
      case AOM_HITTEST:
         result=Hittesttext(tx,(struct Amhittest *)amsg);
         break;
      case AOM_MOVE:
         result=Movetext(tx,(struct Ammove *)amsg);
         break;
      case AOM_SEARCHPOS:
         result=Searchpostext(tx,(struct Amsearch *)amsg);
         break;
      case AOM_SEARCHSET:
         result=Searchsettext(tx,(struct Amsearch *)amsg);
         break;
      case AOM_DRAGTEST:
         result=Dragtesttext(tx,(struct Amdragtest *)amsg);
         break;
      case AOM_DRAGRENDER:
         result=Dragrendertext(tx,(struct Amdragrender *)amsg);
         break;
      case AOM_DRAGCOPY:
         result=Dragcopytext(tx,(struct Amdragcopy *)amsg);
         break;
      case AOM_DISPOSE:
         Disposetext(tx);
         break;
      case AOM_DEINSTALL:
         break;
      default:
         AmethodasA(AOTP_ELEMENT,tx,amsg);
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installtext(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_TEXT,Dispatch)) return FALSE;
   return TRUE;
}
