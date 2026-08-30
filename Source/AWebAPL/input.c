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

/* input.c - AWeb html document text input form field element object */

#include "aweb.h"
#include "input.h"
#include "form.h"
#include "frame.h"
#include "window.h"
#include "application.h"
#include "jslib.h"
#include "bulletfont.h"
#include <proto/exec.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <intuition/imageclass.h>
#include <images/bevel.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/keymap.h>
#include <proto/bevel.h>

/*------------------------------------------------------------------------*/

struct Ainput
{  struct Field field;
   void *frame;
   USHORT type;
   USHORT flags;
   long maxlength,size;
   struct Buffer buf;            /* Current value, initial value in (value) */
   long left;                    /* Current left scroll position */
   long pos;                     /* Current cursor position or -1 if not active */
   UBYTE *curvalue;              /* Temporary */
   UBYTE *savevalue;             /* Value before editing */
   UBYTE *onchange;
   UBYTE *onfocus;
   UBYTE *onblur;
   UBYTE *onselect;
   void *jsowner;                /* Owning filefield object or NULL */
};

#define INPF_CHANGED    0x0001   /* User has changed text */
#define INPF_NOJSEH     0x0002   /* Don't run JS event handler */

static void *bevel;
static long bevelw,bevelh;

/* Key codings */
enum KEY_EQVS
{  KEYEQV_TAB=1,KEYEQV_STAB,KEYEQV_ENTER,KEYEQV_LEFT,KEYEQV_SLEFT,KEYEQV_RIGHT,
   KEYEQV_SRIGHT,KEYEQV_DEL,KEYEQV_SDEL,KEYEQV_BS,KEYEQV_SBS,KEYEQV_WDEL,
   KEYEQV_CLEAR,KEYEQV_RESTORE,KEYEQV_PASTE,KEYEQV_COPY,KEYEQV_TEXT,
};

/*------------------------------------------------------------------------*/

/* Get font from element, with fallback to system font.
 * AWEB4 HTML fonts are Bullet stubs (empty CharData); form fields need
 * diskfont so graphics Text()/TextLength show glyphs. */
static struct TextFont *Getinputfont(struct Ainput *inp)
{  struct TextFont *font;
   
   /* Try to get font from element */
   font=(struct TextFont *)Agetattr(inp,AOELT_Font);
   
   /* Fallback to system font if not set */
   if(!font)
   {  font=(struct TextFont *)Agetattr(Aweb(),AOAPP_Systemfont);
   }
#ifdef AWEB4
   font=AwebFormDiskFont(font);
#endif
   
   return font;
}

/* Get X position of character at given index (handles variable-width fonts) */
static long Getcharxpos(struct Ainput *inp,long charpos,struct TextFont *font,struct RastPort *rp)
{  long xpos=0;
   long start=inp->left;
   long len;
   
   if(charpos>start)
   {  len=charpos-start;
      if(len>0 && len<=inp->buf.length-start)
      {  SetFont(rp,font);
         xpos=TextLength(rp,inp->buf.buffer+start,len);
      }
   }
   
   return xpos;
}

/* Convert mouse X position to character position (handles variable-width fonts) */
static long Xpostocharpos(struct Ainput *inp,long mousex,struct TextFont *font,struct RastPort *rp)
{  long x=mousex-inp->aox-bevelw-2;
   long charpos=inp->left;
   long len=MIN(inp->size,inp->buf.length-inp->left);
   long i;
   long nextwidth;
   
   if(x<0) return inp->left;
   
   SetFont(rp,font);
   
   /* Find character position by measuring text width */
   for(i=0;i<=len;i++)
   {  if(i<len)
      {  nextwidth=TextLength(rp,inp->buf.buffer+inp->left,i+1);
      }
      else
      {  nextwidth=TextLength(rp,inp->buf.buffer+inp->left,len);
         if(inp->size>len) nextwidth+=font->tf_XSize*(inp->size-len);
      }
      if(nextwidth>=x)
      {  charpos=inp->left+i;
         break;
      }
      if(i==len)
      {  charpos=inp->left+len;
      }
   }
   
   return charpos;
}

/* Render the field contents */
static void Rendercontents(struct Ainput *inp,struct Coords *coo)
{  struct TextFont *sysfont=Getinputfont(inp);
   struct RastPort *rp;
   long x,y,len,i;
   coo=Clipcoords(inp->cframe,coo);
   if(coo && coo->rp)
   {  rp=coo->rp;
      SetFont(rp,sysfont);
      SetSoftStyle(rp,0,0x0f);
      SetABPenDrMd(rp,coo->dri->dri_Pens[TEXTPEN],0,JAM2);
      len=MIN(inp->size,inp->buf.length-inp->left);
      x=inp->aox+coo->dx+bevelw+2;
      y=inp->aoy+coo->dy+bevelh+2;
      if(len)
      {  Move(rp,x,y+rp->TxBaseline);
         switch(inp->type)
         {  case INPTP_TEXT:
               Text(rp,inp->buf.buffer+inp->left,len);
               break;
            case INPTP_PASSWORD:
               for(i=0;i<len;i++)
               {  Text(rp,"*",1);
               }
               break;
         }
      }
      SetABPenDrMd(rp,0,0,JAM1);
      /* Fill remaining area - handle variable-width fonts */
      {  long textwidth=TextLength(rp,inp->buf.buffer+inp->left,len);
         long fieldwidth=inp->size*sysfont->tf_XSize;
         if(textwidth<fieldwidth)
         {  RectFill(rp,x+textwidth,y,x+fieldwidth-1,y+sysfont->tf_YSize-1);
         }
      }
      if(inp->pos>=0)
      {  long cursorx=Getcharxpos(inp,inp->pos,sysfont,rp);
         long cursorw=1;
         /* For variable-width fonts, cursor should be 1 pixel wide */
         SetDrMd(rp,COMPLEMENT);
         RectFill(rp,x+cursorx,y,x+cursorx+cursorw-1,y+sysfont->tf_YSize-1);
         SetDrMd(rp,JAM1);
      }
   }
   Unclipcoords(coo);
}

/* Ensure the current position is visible */
static void Makevisible(struct Ainput *inp)
{  struct Arect vis;
   struct TextFont *sysfont=Getinputfont(inp);
   struct RastPort *rp;
   long cursorx;
   
   /* Get a rastport for measuring - use mrp if available */
   rp=mrp;
   if(rp)
   {  cursorx=Getcharxpos(inp,inp->pos,sysfont,rp);
   }
   else
   {  /* Fallback to fixed-width calculation if no rastport available */
      cursorx=(inp->pos-inp->left)*sysfont->tf_XSize;
   }
   
   vis.minx=inp->aox+bevelw+2+cursorx;
   vis.miny=inp->aoy+bevelh+2;
   vis.maxx=vis.minx+1;
   vis.maxy=vis.miny+sysfont->tf_YSize-1;
   Asetattrs(inp->frame,AOFRM_Makevisible,&vis,TAG_END);
}

/* Paste from the clipboard */
static void Pasteinput(struct Ainput *inp)
{  UBYTE buf[512];
   UBYTE *p;
   long l;
   l=Clippaste(buf,sizeof(buf)-1);
   if(l)
   {  buf[l]='\0';
      for(p=buf;*p && Isprint(*p);p++);
      *p='\0';
      l=strlen(buf);
/*
      Deleteinbuffer(&inp->buf,0,inp->buf.length);
      Addtobuffer(&inp->buf,buf,l);
      inp->pos=l;
*/
      Insertinbuffer(&inp->buf,buf,l,inp->pos);
      inp->pos+=l;
      inp->left=MAX(0,inp->pos+1-inp->size);
      inp->flags|=INPF_CHANGED;
   }
}

/* Process a keypress */
static long Processkey(struct Ainput *inp,USHORT code,USHORT qual,void *iaddress,
   struct Amiresult *amir)
{  BOOL shift=(qual&(IEQUALIFIER_RSHIFT|IEQUALIFIER_LSHIFT));
   BOOL ramiga=(qual&IEQUALIFIER_RCOMMAND);
   BOOL control=(qual&IEQUALIFIER_CONTROL);
   long result=AMR_ACTIVE;
   struct InputEvent ie={0};
   UBYTE buffer[8];
   short keyeqv=0,i;
   BOOL render=TRUE;
   void *form;
   switch(code)
   {  case 0x42:  /* Tab */
         if(shift) keyeqv=KEYEQV_STAB;
         else keyeqv=KEYEQV_TAB;
         break;
      case 0x43:  /* Num enter */
      case 0x44:  /* Enter */
         keyeqv=KEYEQV_ENTER;
         break;
      case 0x4f:  /* Left */
         if(shift) keyeqv=KEYEQV_SLEFT;
         else keyeqv=KEYEQV_LEFT;
         break;
      case 0x4e:  /* Right */
         if(shift) keyeqv=KEYEQV_SRIGHT;
         else keyeqv=KEYEQV_RIGHT;
         break;
      case 0x46:  /* Del */
         if(shift) keyeqv=KEYEQV_SDEL;
         else keyeqv=KEYEQV_DEL;
         break;
      case 0x41:  /* BS */
         if(shift) keyeqv=KEYEQV_SBS;
         else keyeqv=KEYEQV_BS;
         break;
      default:
         ie.ie_Class=IECLASS_RAWKEY;
         ie.ie_SubClass=0;
         ie.ie_Code=code;
         ie.ie_Qualifier=qual&~IEQUALIFIER_CONTROL;
         ie.ie_EventAddress=*(APTR *)iaddress;
         if(MapRawKey(&ie,buffer,8,NULL)==1)
         {  if(ramiga)
            {  switch(toupper(buffer[0]))
               {  case 'X':   keyeqv=KEYEQV_CLEAR;break;
                  case 'V':   keyeqv=KEYEQV_PASTE;break;
                  case 'Q':   keyeqv=KEYEQV_RESTORE;break;
                  case 'C':   keyeqv=KEYEQV_COPY;break;
               }
            }
            else if(control)
            {  switch(toupper(buffer[0]))
               {  case 'A':   keyeqv=KEYEQV_SLEFT;break;
                  case 'H':   keyeqv=KEYEQV_BS;break;
                  case 'K':   keyeqv=KEYEQV_SDEL;break;
                  case 'M':   keyeqv=KEYEQV_ENTER;break;
                  case 'W':   keyeqv=KEYEQV_WDEL;break;
                  case 'U':   keyeqv=KEYEQV_SBS;break;
                  case 'X':   keyeqv=KEYEQV_CLEAR;break;
                  case 'Z':   keyeqv=KEYEQV_SRIGHT;break;
               }
            }
            if(!keyeqv && Isprint(buffer[0])) keyeqv=KEYEQV_TEXT;
         }
   }
   switch(keyeqv)
   {  case KEYEQV_TAB:
         result=AMR_NOREUSE;
         if(amir)
         {  if(inp->form)
            {  amir->newobject=(struct Aobject *)Amethod(inp->form,AFO_NEXTINPUT,inp,1);
            }
            else if(inp->jsowner)
            {  form=(void *)Agetattr(inp->jsowner,AOFLD_Form);
               amir->newobject=(struct Aobject *)Amethod(form,AFO_NEXTINPUT,inp->jsowner,1);
            }
         }
         break;
      case KEYEQV_STAB:
         result=AMR_NOREUSE;
         if(amir)
         {  if(inp->form)
            {  amir->newobject=(struct Aobject *)Amethod(inp->form,AFO_NEXTINPUT,inp,-1);
            }
            else if(inp->jsowner)
            {  form=(void *)Agetattr(inp->jsowner,AOFLD_Form);
               amir->newobject=(struct Aobject *)Amethod(form,AFO_NEXTINPUT,inp->jsowner,-1);
            }
         }
         break;
      case KEYEQV_ENTER:
         Amethod(inp->form,AFO_SUBMIT,inp,0,0);
         result=AMR_NOREUSE;
         break;
      case KEYEQV_SLEFT:
         inp->pos=0;
         inp->left=0;
         break;
      case KEYEQV_LEFT:
         if(inp->pos>0) inp->pos--;
         if(inp->left>inp->pos) inp->left=inp->pos;
         break;
      case KEYEQV_SRIGHT:
         inp->pos=inp->buf.length;
         if(inp->pos>=inp->size) inp->left=inp->pos-inp->size+1;
         else inp->left=0;
         break;
      case KEYEQV_RIGHT:
         if(inp->pos<inp->buf.length) inp->pos++;
         if(inp->pos>=inp->left+inp->size) inp->left=inp->pos-inp->size+1;
         break;
      case KEYEQV_DEL:
      case KEYEQV_SDEL:
         Deleteinbuffer(&inp->buf,inp->pos,keyeqv==KEYEQV_SDEL?inp->buf.length:1);
         if(inp->buf.length<inp->left+inp->size)
         {  inp->left=inp->buf.length-inp->size+1;
            if(inp->left<0) inp->left=0;
            inp->flags|=INPF_CHANGED;
         }
         break;
      case KEYEQV_SBS:
         Deleteinbuffer(&inp->buf,0,inp->pos);
         inp->pos=0;
         inp->left=0;
         inp->flags|=INPF_CHANGED;
         break;
      case KEYEQV_BS:
         if(inp->pos>0)
         {  inp->pos--;
            Deleteinbuffer(&inp->buf,inp->pos,1);
            if(inp->left>inp->pos) inp->left=inp->pos;
            if(inp->buf.length<inp->left+inp->size)
            {  inp->left=inp->buf.length-inp->size+1;
               if(inp->left<0) inp->left=0;
            }
            inp->flags|=INPF_CHANGED;
         }
         break;
      case KEYEQV_WDEL:
         i=inp->pos-1;
         if(i>=0 && inp->buf.buffer[i]==' ') i--;
         for(;i>=0 && inp->buf.buffer[i]!=' ';i--);
         i++;
         if(i<inp->pos)
         {  Deleteinbuffer(&inp->buf,i,inp->pos-i);
            inp->pos=i;
            if(inp->left>inp->pos) inp->left=inp->pos;
            if(inp->buf.length<inp->left+inp->size)
            {  inp->left=inp->buf.length-inp->size+1;
               if(inp->left<0) inp->left=0;
            }
            inp->flags|=INPF_CHANGED;
         }
         break;
      case KEYEQV_CLEAR:
         Deleteinbuffer(&inp->buf,0,inp->buf.length);
         inp->pos=0;
         inp->left=0;
         inp->flags|=INPF_CHANGED;
         break;
      case KEYEQV_RESTORE:
         Deleteinbuffer(&inp->buf,0,inp->buf.length);
         if(inp->savevalue)
         {  Addtobuffer(&inp->buf,inp->savevalue,strlen(inp->savevalue));
         }
         inp->pos=inp->buf.length;
         inp->left=MAX(0,inp->buf.length+1-inp->size);
         inp->flags&=~INPF_CHANGED;
         break;
      case KEYEQV_PASTE:
         Pasteinput(inp);
         break;
      case KEYEQV_COPY:
         Clipcopy(inp->buf.buffer,inp->buf.length);
         break;
      case KEYEQV_TEXT:
         if(!inp->maxlength || inp->buf.length<inp->maxlength)
         {  if(Insertinbuffer(&inp->buf,buffer,1,inp->pos))
            {  inp->pos++;
               if(inp->pos>=inp->left+inp->size)
                  inp->left=inp->pos-inp->size+1;
            }
         }
         inp->flags|=INPF_CHANGED;
         break;
   }
/*
   inp->buf.buffer[inp->buf.length]='\0';
*/
   if(result!=AMR_ACTIVE) render=FALSE;
   if(render)
   {  Makevisible(inp);
      Rendercontents(inp,NULL);
   }
   return result;
}

/*------------------------------------------------------------------------*/

/* Get or set value property (js) */
static BOOL Propertyvalue(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Ainput *inp=vd->hookdata;
   UBYTE *p;
   long l;
   if(inp)
   {  switch(vd->code)
      {  case VHC_SET:
            p=Jtostring(vd->jc,vd->value);
            l=strlen(p);
            Deleteinbuffer(&inp->buf,0,inp->buf.length);
            Addtobuffer(&inp->buf,p,l);
            inp->left=0;
            if(inp->pos>=0) inp->pos=0;
            if(inp->eltflags&ELTF_ALIGNED)
            {  Rendercontents(inp,NULL);
            }
            result=TRUE;
            break;
         case VHC_GET:
            p=Dupstr(inp->buf.buffer,inp->buf.length);
            Jasgstring(vd->jc,vd->value,p);
            if(p) FREE(p);
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Javascript methods */
static void Methodselect(struct Jcontext *jc)
{
}

static void Domethodfocus(struct Ainput *inp,struct Jcontext *jc)
{  if(inp)
   {  inp->flags|=INPF_NOJSEH;
      Asetattrs(inp->win,AOWIN_Activeobject,inp,TAG_END);
      inp->flags&=~INPF_NOJSEH;
   }
}

static void Methodfocus(struct Jcontext *jc)
{  Domethodfocus(Jointernal(Jthis(jc)),jc);
}

static void Domethodblur(struct Ainput *inp,struct Jcontext *jc)
{  if(inp && inp->pos>=0)
   {  /* This object has focus */
      inp->flags|=INPF_NOJSEH;
      Asetattrs(inp->win,AOWIN_Activeobject,NULL,TAG_END);
      inp->flags&=~INPF_NOJSEH;
   }
}

static void Methodblur(struct Jcontext *jc)
{  Domethodblur(Jointernal(Jthis(jc)),jc);
}

static void Methodtostring(struct Jcontext *jc)
{  struct Ainput *inp=Jointernal(Jthis(jc));
   struct Buffer buf={0};
   if(inp)
   {  Addtagstr(&buf,"<input",ATSF_NONE,0);
      Addtagstr(&buf,"type",ATSF_STRING,(inp->type==INPTP_PASSWORD)?"password":"input");
      if(inp->name) Addtagstr(&buf,"name",ATSF_STRING,inp->name);
      Addtagstr(&buf,"value",ATSF_STRING,inp->value);
      Addtagstr(&buf,"size",ATSF_NUMBER,inp->size-1);
      if(inp->maxlength) Addtagstr(&buf,"maxlength",ATSF_NUMBER,inp->maxlength);
      Addtobuffer(&buf,">",2);
      Jasgstring(jc,NULL,buf.buffer);
      Freebuffer(&buf);
   }
}

/*------------------------------------------------------------------------*/

static long Measureinput(struct Ainput *inp,struct Ammeasure *amm)
{  struct TextFont *sysfont=Getinputfont(inp);
   inp->aow=inp->size*sysfont->tf_XSize+2*bevelw+4;
   inp->aoh=sysfont->tf_YSize+2*bevelh+4;
   AmethodasA(AOTP_FIELD,inp,amm);
   return 0;
}

static long Renderinput(struct Ainput *inp,struct Amrender *amr)
{  struct Coords *coo,coords={0};
   BOOL clip=FALSE;
   ULONG clipkey=NULL;
   struct RastPort *rp;
   struct ColorMap *colormap=NULL;
   struct DrawInfo *drinfo=NULL;
   long bpen=~0,tpen=~0;
   if(!(coo=amr->coords))
   {  Framecoords(inp->cframe,&coords);
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
         IA_Width,inp->aow,
         IA_Height,inp->aoh,
         BEVEL_FillPen,bpen,
         BEVEL_TextPen,tpen,
         BEVEL_ColorMap,colormap,
         TAG_END);
      DrawImageState(rp,bevel,inp->aox+coo->dx,inp->aoy+coo->dy,IDS_NORMAL,drinfo);
      Rendercontents(inp,coo);
      if(clip) Unclipto(clipkey);
   }
   return 0;
}

static long Setinput(struct Ainput *inp,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=Amethodas(AOTP_FIELD,inp,AOM_SET,ams->tags);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOINP_Type:
            inp->type=tag->ti_Data;
            break;
         case AOINP_Maxlength:
            inp->maxlength=MAX(1,tag->ti_Data);
            break;
         case AOINP_Size:
            inp->size=MIN(4000,tag->ti_Data+1);
            break;
         case AOBJ_Frame:
            inp->frame=(void *)tag->ti_Data;
            break;
         case AOBJ_Window:
            if(!tag->ti_Data)
            {  inp->pos=-1;
            }
            break;
         case AOFLD_Reset:
            Freebuffer(&inp->buf);
            Addtobuffer(&inp->buf,inp->value,strlen(inp->value));
            inp->left=0;
            inp->pos=-1;
            Rendercontents(inp,NULL);
            break;
         case AOFLD_Onchange:
            if(inp->onchange) FREE(inp->onchange);
            inp->onchange=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onfocus:
            if(inp->onfocus) FREE(inp->onfocus);
            inp->onfocus=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onblur:
            if(inp->onblur) FREE(inp->onblur);
            inp->onblur=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onselect:
            if(inp->onselect) FREE(inp->onselect);
            inp->onselect=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOBJ_Jsowner:
            inp->jsowner=(void *)tag->ti_Data;
            break;
         case AOBJ_Jsfocus:
            if(tag->ti_Data) Domethodfocus(inp,(struct Jcontext *)tag->ti_Data);
            break;
         case AOBJ_Jsblur:
            if(tag->ti_Data) Domethodblur(inp,(struct Jcontext *)tag->ti_Data);
            break;
      }
   }
   if(!inp->value)
   {  inp->value=Dupstr("",-1);
   }
   return result;
}

static long Getinput(struct Ainput *inp,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=AmethodasA(AOTP_FIELD,inp,ams);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOINP_Type:
            PUTATTR(tag,inp->type);
            break;
         case AOFLD_Value:
            if(inp->curvalue) FREE(inp->curvalue);
            inp->curvalue=Dupstr(inp->buf.buffer,inp->buf.length);
            PUTATTR(tag,inp->curvalue);
            break;
      }
   }
   return result;
}

static struct Ainput *Newinput(struct Amset *ams)
{  struct Ainput *inp;
   if(inp=Allocobject(AOTP_INPUT,sizeof(struct Ainput),ams))
   {  inp->size=20;
      inp->pos=-1;
      Setinput(inp,ams);
      Addtobuffer(&inp->buf,inp->value,strlen(inp->value));
   }
   return inp;
}

static long Hittestinput(struct Ainput *inp,struct Amhittest *amh)
{  long result;
   if(amh->oldobject==inp)
   {  result=AMHR_OLDHIT;
   }
   else
   {  result=AMHR_NEWHIT;
      if(amh->amhr)
      {  amh->amhr->object=inp;
         amh->amhr->ptrtype=APTR_TEXT;
      }
   }
   return result;
}

static long Goactiveinput(struct Ainput *inp,struct Amgoactive *amg)
{  struct Coords *coo=NULL;
   struct TextFont *sysfont=Getinputfont(inp);
   long x;
   coo=Clipcoords(inp->cframe,coo);  /* no clip needed */
   if(coo)
   {  if(amg->imsg && amg->imsg->Class==IDCMP_MOUSEBUTTONS)
      {  x=amg->imsg->MouseX-inp->aox-coo->dx;
         inp->pos=Xpostocharpos(inp,x,sysfont,coo->rp);
         if(inp->pos>inp->buf.length) inp->pos=inp->buf.length;
         if(inp->pos<inp->left) inp->pos=inp->left;
      }
      else inp->pos=MIN(inp->buf.length,inp->size+inp->left-1);
   }
   Unclipcoords(coo);
   Makevisible(inp);
   Rendercontents(inp,NULL);  /* Can't use coo because may have been moved */
   Asetattrs(inp->win,AOWIN_Rmbtrap,TRUE,TAG_END);
   if(inp->savevalue) FREE(inp->savevalue);
   inp->savevalue=Dupstr(inp->buf.buffer,inp->buf.length);
   inp->flags&=~INPF_CHANGED;
   if(!(inp->flags&INPF_NOJSEH))
   {  if(inp->jsowner) Aupdateattrs(inp->jsowner,NULL,AOBJ_Jsfocus,NULL,TAG_END);
      else if(inp->onfocus || AWebJSBase)
      {  Runjavascriptwith(inp->cframe,awebonfocus,&inp->jobject,inp->form);
      }
   }
   return AMR_ACTIVE;
}

static long Handleinputinput(struct Ainput *inp,struct Aminput *ami)
{  struct Coords *coo=NULL;
   long result=AMR_REUSE;
   struct TextFont *sysfont=Getinputfont(inp);
   long x,y;
   if(ami->imsg)
   {  switch(ami->imsg->Class)
      {  case IDCMP_RAWKEY:
            if(ami->imsg->Code&IECODE_UP_PREFIX)
            {  result=AMR_ACTIVE;
            }
            else
            {  result=Processkey(inp,ami->imsg->Code,ami->imsg->Qualifier,
                  ami->imsg->IAddress,ami->amir);
            }
            break;
         case IDCMP_MOUSEBUTTONS:
            if(ami->imsg->Code==SELECTDOWN)
            {  coo=Clipcoords(inp->cframe,coo);  /* no clip needed */
               if(coo)
               {  x=ami->imsg->MouseX-coo->dx-inp->aox;
                  y=ami->imsg->MouseY-coo->dy-inp->aoy;
                  if(x>=0 && x<inp->aow && y>=0 && y<inp->aoh)
                  {  /* Click inside container */
                     inp->pos=Xpostocharpos(inp,ami->imsg->MouseX-coo->dx,sysfont,coo->rp);
                     if(inp->pos>inp->buf.length) inp->pos=inp->buf.length;
                     if(inp->pos<inp->left) inp->pos=inp->left;
                     Makevisible(inp);
                     Rendercontents(inp,NULL);
                     result=AMR_ACTIVE;
                  }
               }
               Unclipcoords(coo);
            }
            else if(ami->imsg->Code==SELECTUP)
            {  result=AMR_ACTIVE;
            }
            break;
         case IDCMP_INTUITICKS:
         case IDCMP_MOUSEMOVE:
            result=AMR_ACTIVE;
            break;
      }
   }
   return result;
}

static long Goinactiveinput(struct Ainput *inp)
{  if(inp->pos>=0)
   {  inp->pos=-1;
      Rendercontents(inp,NULL);
   }
   Asetattrs(inp->win,AOWIN_Rmbtrap,FALSE,TAG_END);
   if(inp->flags&INPF_CHANGED)
   {  if(inp->jsowner) Aupdateattrs(inp->jsowner,NULL,AOBJ_Jschange,NULL,TAG_END);
      else if(inp->onchange || AWebJSBase)
      {  Runjavascriptwith(inp->cframe,awebonchange,&inp->jobject,inp->form);
      }
   }
   if(!(inp->flags&INPF_NOJSEH))
   {  if(inp->jsowner) Aupdateattrs(inp->jsowner,NULL,AOBJ_Jsblur,NULL,TAG_END);
      else if(inp->onblur || AWebJSBase)
      {  Runjavascriptwith(inp->cframe,awebonblur,&inp->jobject,inp->form);
      }
   }
   return 0;
}

static long Jsetupinput(struct Ainput *inp,struct Amjsetup *amj)
{  struct Jvar *jv;
   UBYTE *p;
   AmethodasA(AOTP_FIELD,inp,amj);
   if(inp->jobject)
   {  if(jv=Jproperty(amj->jc,inp->jobject,"defaultValue"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgstring(amj->jc,jv,inp->value);
      }
      if(jv=Jproperty(amj->jc,inp->jobject,"value"))
      {  Setjproperty(jv,Propertyvalue,inp);
      }
      if(jv=Jproperty(amj->jc,inp->jobject,"type"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         switch(inp->type)
         {  case INPTP_TEXT:     p="text";break;
            case INPTP_PASSWORD: p="password";break;
            default:             p="unknown";
         }
         Jasgstring(amj->jc,jv,p);
      }
      Addjfunction(amj->jc,inp->jobject,"focus",Methodfocus,NULL);
      Addjfunction(amj->jc,inp->jobject,"blur",Methodblur,NULL);
      Addjfunction(amj->jc,inp->jobject,"select",Methodselect,NULL);
      Addjfunction(amj->jc,inp->jobject,"toString",Methodtostring,NULL);
      Jaddeventhandler(amj->jc,inp->jobject,"onfocus",inp->onfocus);
      Jaddeventhandler(amj->jc,inp->jobject,"onblur",inp->onblur);
      Jaddeventhandler(amj->jc,inp->jobject,"onchange",inp->onchange);
      Jaddeventhandler(amj->jc,inp->jobject,"onselect",inp->onselect);
   }
   return 0;
}

static void Disposeinput(struct Ainput *inp)
{  Freebuffer(&inp->buf);
   if(inp->curvalue) FREE(inp->curvalue);
   if(inp->savevalue) FREE(inp->savevalue);
   if(inp->onblur) FREE(inp->onblur);
   if(inp->onchange) FREE(inp->onchange);
   if(inp->onfocus) FREE(inp->onfocus);
   if(inp->onselect) FREE(inp->onselect);
   Amethodas(AOTP_FIELD,inp,AOM_DISPOSE);
}

static void Deinstallinput(void)
{  if(bevel) DisposeObject(bevel);
}

static long Dispatch(struct Ainput *inp,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newinput((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setinput(inp,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getinput(inp,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measureinput(inp,(struct Ammeasure *)amsg);
         break;
      case AOM_RENDER:
         result=Renderinput(inp,(struct Amrender *)amsg);
         break;
      case AOM_HITTEST:
         result=Hittestinput(inp,(struct Amhittest *)amsg);
         break;
      case AOM_GOACTIVE:
         result=Goactiveinput(inp,(struct Amgoactive *)amsg);
         break;
      case AOM_HANDLEINPUT:
         result=Handleinputinput(inp,(struct Aminput *)amsg);
         break;
      case AOM_GOINACTIVE:
         result=Goinactiveinput(inp);
         break;
      case AOM_JSETUP:
         result=Jsetupinput(inp,(struct Amjsetup *)amsg);
         break;
      case AOM_DISPOSE:
         Disposeinput(inp);
         break;
      case AOM_DEINSTALL:
         Deinstallinput();
         break;
      default:
         result=AmethodasA(AOTP_FIELD,inp,amsg);
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installinput(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_INPUT,Dispatch)) return FALSE;
   if(!(bevel=BevelObject,
      BEVEL_Style,BVS_FIELD,
      End)) return FALSE;
   GetAttr(BEVEL_VertSize,bevel,(ULONG *)&bevelw);
   GetAttr(BEVEL_HorizSize,bevel,(ULONG *)&bevelh);
   return TRUE;
}
