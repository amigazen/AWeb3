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

/* textarea.c - AWeb html document textarea form field element object */

#include "aweb.h"
#include "textarea.h"
#include "scroller.h"
#include "frame.h"
#include "application.h"
#include "editor.h"
#include "window.h"
#include "jslib.h"
#include "bulletfont.h"
#include <proto/exec.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <intuition/imageclass.h>
#include <images/bevel.h>
#include <images/drawlist.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/utility.h>
#include <proto/bevel.h>
#include <proto/drawlist.h>

/*------------------------------------------------------------------------*/

struct Textarea
{  struct Field field;
   void *frame;
   void *capens;
   struct Buffer buf;         /* lines, separated by newlines, terminated with nullbyte */
   long rows,cols;            /* box dimensions */
   long width,height;         /* text dimensions */
   long left,top;             /* text scroll position */
   long curx,cury;            /* cursor position. If TXAF_CURSOR set, this is
                               * guaranteed to fall inside visible area. */
   long pos;                  /* position of current line in buf */
   long length;               /* length of current line (excluding newline separator) */
   USHORT flags;
   void *hscroll,*vscroll;    /* scrollers */
   short charw,charh;
   short scrw,scrh;
   UBYTE *fldvalue;           /* Buffer holds current value with CRLF */
   void *editor;              /* External editor running */
   UBYTE *onchange;
   UBYTE *onfocus;
   UBYTE *onblur;
   UBYTE *onselect;
};

#define TXAF_COMPLETE      0x0001   /* Definition is complete */
#define TXAF_CURSOR        0x0002   /* Active editing, cursor visible */
#define TXAF_EDITBUTTON    0x0004   /* Edit button input */
#define TXAF_EDITBUTACT    0x0008   /* Edit button active (depressed) */
#define TXAF_CHANGED       0x0010   /* User has changed text */
#define TXAF_NOJSEH        0x0020   /* Don't run JS event handlers */


static void *tbevel,*bbevel,*epict;
static long tbevelw,tbevelh,bbevelw,bbevelh;

static struct TagItem maphscroll[]={ AOSCR_Top,AOTXA_Left,TAG_END };
static struct TagItem mapvscroll[]={ AOSCR_Top,AOTXA_Top,TAG_END };

static struct DrawList epictdata[]=
{  { DLST_LINE,600,600,1748,600,1 },
   { DLST_LINE,600,600,600,3496,1 },
   { DLST_LINE,600,3496,1748,3496,1 },
   { DLST_LINE,600,2048,1365,2048,1 },
   { DLST_AMOVE,2348,600,0,0,1 },
   { DLST_ADRAW,3496,2048,0,0,1 },
   { DLST_ADRAW,2348,3496,0,0,1 },
   { DLST_ADRAW,2348,600,0,0,1 },
   { DLST_AFILL,0,0,0,0,1 },
   { DLST_END },
};

/*------------------------------------------------------------------------*/

/* Get font from element, with fallback to system font.
 * AWEB4: Bullet HTML stubs need a diskfont for Text(). */
static struct TextFont *Gettextareafont(struct Textarea *txa)
{  struct TextFont *font;
   
   /* Try to get font from element */
   font=(struct TextFont *)Agetattr(txa,AOELT_Font);
   
   /* Fallback to system font if not set */
   if(!font)
   {  font=(struct TextFont *)Agetattr(Aweb(),AOAPP_Systemfont);
   }
#ifdef AWEB4
   font=AwebFormDiskFont(font);
#endif
   
   return font;
}

/* Get X position of cursor on current line (handles variable-width fonts) */
static long Gettextareacursorx(struct Textarea *txa,struct TextFont *font,struct RastPort *rp)
{  long xpos=0;
   UBYTE *p,*q,*lineend;
   long i;
   long curx=txa->curx-txa->left;
   
   if(curx<=0) return 0;
   
   /* Find start of current line */
   p=txa->buf.buffer;
   for(i=0;i<txa->cury;i++)
   {  q=strchr(p,'\n');
      if(q) p=q+1;
      else break;
   }
   
   /* Find end of current line */
   q=strchr(p,'\n');
   if(!q) q=p+strlen(p);
   lineend=q;
   
   /* Measure text up to cursor position */
   p+=txa->left;
   if(p<lineend && curx>0)
   {  long len=MIN(curx,lineend-p);
      SetFont(rp,font);
      xpos=TextLength(rp,p,len);
   }
   
   return xpos;
}

/* Convert mouse X position to character position on a line (handles variable-width fonts) */
static long Xpostocharposinline(struct Textarea *txa,long mousex,long liney,struct TextFont *font,struct RastPort *rp)
{  long x=mousex;
   long charpos=txa->left;
   UBYTE *p,*q,*lineend;
   long i;
   long len;
   
   if(x<0) return txa->left;
   
   /* Find start of target line */
   p=txa->buf.buffer;
   for(i=0;i<liney;i++)
   {  q=strchr(p,'\n');
      if(q) p=q+1;
      else break;
   }
   
   /* Find end of target line */
   q=strchr(p,'\n');
   if(!q) q=p+strlen(p);
   lineend=q;
   
   /* Measure text to find character position */
   p+=txa->left;
   len=lineend-p;
   if(len>0)
   {  SetFont(rp,font);
      for(i=0;i<=len;i++)
      {  long nextwidth;
         if(i<len)
         {  nextwidth=TextLength(rp,p,i+1);
         }
         else
         {  nextwidth=TextLength(rp,p,len);
            if(txa->cols>len) nextwidth+=font->tf_XSize*(txa->cols-len);
         }
         if(nextwidth>=x)
         {  charpos=txa->left+i;
            break;
         }
         if(i==len)
         {  charpos=txa->left+len;
         }
      }
   }
   
   return charpos;
}

/* Adjust size, initialize scrollers */
static void Adjusttextarea(struct Textarea *txa)
{  struct TextFont *tf=Gettextareafont(txa);
   long min;
   txa->charw=tf->tf_XSize;
   txa->charh=tf->tf_YSize;
   txa->scrw=Agetattr(txa->vscroll,AOBJ_Width);
   txa->scrh=Agetattr(txa->hscroll,AOBJ_Height);
   min=(Agetattr(txa->vscroll,AOSCR_Minheight)-2*tbevelh-4+txa->charh-1) / txa->charh;
   if(txa->rows<min) txa->rows=min;
   min=(Agetattr(txa->hscroll,AOSCR_Minwidth)-2*tbevelw-4+txa->charw-1) / txa->charw;
   if(txa->cols<min) txa->cols=min;
   Asetattrs(txa->vscroll,
      AOSCR_Total,txa->height,
      AOSCR_Visible,txa->rows,
      AOSCR_Top,txa->top,
      TAG_END);
   Asetattrs(txa->hscroll,
      AOSCR_Total,txa->width,
      AOSCR_Visible,txa->cols,
      AOSCR_Top,txa->left,
      TAG_END);
}

/* Measure text, return TRUE if scrollers changed. */
static BOOL Measuretext(struct Textarea *txa)
{  long oldw=txa->width,oldh=txa->height;
   long l;
   UBYTE *p,*q;
   p=txa->buf.buffer;
   txa->width=txa->height=0;
   for(;;)
   {  q=strchr(p,'\n');
      if(q) l=q-p;
      else l=strlen(p);
      if(l>txa->width) txa->width=l;
      txa->height++;
      if(!q) break;
      p=q+1;
   }
   return (BOOL)(txa->width!=oldw || txa->height!=oldh);
}

/* Definition complete. Append nullbute, find text width and height,
 * remember initial value, and initialize scrollers */
static void Completetextarea(struct Textarea *txa)
{  if(Addtobuffer(&txa->buf,"",1))
   {  txa->flags|=TXAF_COMPLETE;
      Measuretext(txa);
      if(txa->value) FREE(txa->value);
      txa->value=Dupstr(txa->buf.buffer,txa->buf.length);
      if(txa->cframe) Adjusttextarea(txa);
   }
}

/* Find the current line. */
static void Findline(struct Textarea *txa)
{  UBYTE *p,*q;
   long n;
   if(!txa->buf.length)
   {  txa->curx=0;
      txa->cury=0;
      txa->pos=0;
      txa->length=0;
   }
   else
   {  p=txa->buf.buffer;
      for(n=0;n<txa->cury;n++)
      {  q=strchr(p,'\n');
         if(!q)
         {  txa->cury=n;
            break;
         }
         p=q+1;
      }
      txa->pos=p-txa->buf.buffer;
      if(q=strchr(p,'\n')) txa->length=q-p;
      else txa->length=strlen(p);
   }
}

/* Adjust cursor and scroll positions, returns TRUE if scrolled */
static BOOL Curposadjust(struct Textarea *txa)
{  long top=txa->top,left=txa->left;
   Findline(txa);
   if(txa->curx>txa->length) txa->curx=txa->length;
   if(txa->width<txa->cols) txa->left=0;
   else if(txa->curx<txa->left) txa->left=txa->curx;
   else if(txa->curx>=txa->left+txa->cols)
      txa->left=txa->curx-txa->cols+1;
   else if(txa->left+txa->cols>txa->width+1)
   {  txa->left=txa->width+1-txa->cols;
      if(txa->left<0) txa->left=0;
   }
   if(txa->height<txa->rows) txa->top=0;
   else if(txa->cury<txa->top) txa->top=txa->cury;
   else if(txa->cury>=txa->top+txa->rows)
      txa->top=txa->cury-txa->rows+1;
   else if(txa->top+txa->rows>txa->height)
   {  txa->top=txa->height-txa->rows;
      if(txa->top<0) txa->top=0;
   }
   return (BOOL)(top!=txa->top || left!=txa->left);
}

/* Reset to initial value */
static void Resettextarea(struct Textarea *txa)
{  Freebuffer(&txa->buf);
   Addtobuffer(&txa->buf,txa->value,strlen(txa->value)+1);
   txa->top=txa->left=0;
   Measuretext(txa);
   Adjusttextarea(txa);
   Arender(txa,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
}

/* Create a CRLF separated copy if the current value */
static UBYTE *Fieldvalue(struct Textarea *txa)
{  UBYTE *p,*q,*r;
   if(txa->fldvalue) FREE(txa->fldvalue);
   if(txa->fldvalue=ALLOCTYPE(UBYTE,txa->buf.length+txa->height+1,0))
   {  r=txa->fldvalue;
      p=txa->buf.buffer;
      for(;;)
      {  if(!(q=strchr(p,'\n'))) q=p+strlen(p);
         memmove(r,p,q-p);
         r+=q-p;
         if(!*q) break;
         memmove(r,"\r\n",2);
         r+=2;
         p=q+1;
      }
      *r='\0';
   }
   return txa->fldvalue;
}

/*------------------------------------------------------------------------*/

/* Render the edit area. */

#define TXRF_CURRENTONLY   0x0001   /* Only current line, default entire contents */
#define TXRF_BEVEL         0x0002   /* Bevel too */
#define TXRF_NOCURSOR      0x0004   /* Render without cursor */

static void Rendertext(struct Textarea *txa,struct Coords *coo,USHORT flags)
{  long i,x,y,n;
   struct RastPort *rp;
   struct ColorMap *colormap=NULL;
   struct DrawInfo *drinfo=NULL;
   struct TextFont *font=NULL;
   UBYTE *p,*q;
   if((coo=Clipcoords(txa->cframe,coo)) && coo->rp)
   {  rp=coo->rp;
      x=txa->aox+coo->dx+tbevelw+2;
      y=txa->aoy+coo->dy+tbevelh+2;
      Agetattrs(Aweb(),
         AOAPP_Colormap,&colormap,
         AOAPP_Drawinfo,&drinfo,
         TAG_END);
      font=Gettextareafont(txa);
      if(flags&TXRF_BEVEL)
      {  SetAttrs(tbevel,
            IA_Width,txa->aow-txa->scrw,
            IA_Height,txa->aoh-txa->scrh,
            BEVEL_ColorMap,colormap,
            REACTION_SpecialPens,txa->capens,
            TAG_END);
         DrawImageState(rp,tbevel,x-tbevelw-2,y-tbevelh-2,IDS_NORMAL,drinfo);
      }
      SetFont(rp,font);
      SetSoftStyle(rp,0,0x0f);
      SetABPenDrMd(rp,0,0,JAM2);
      for(p=txa->buf.buffer,i=0;i<txa->top;i++)
      {  q=strchr(p,'\n');
         if(q) p=q+1;
      }
      for(;i<txa->top+txa->rows && i<txa->height;i++)
      {  q=strchr(p,'\n');
         if(!q) q=p+strlen(p);
         if(!(flags&TXRF_CURRENTONLY) || i==txa->cury)
         {  p+=txa->left;
            if(p<q)
            {  n=MIN(q-p,txa->cols);
               SetAPen(rp,coo->dri->dri_Pens[TEXTPEN]);
               Move(rp,x,y+(i-txa->top)*txa->charh+rp->TxBaseline);
               Text(rp,p,n);
            }
            else n=0;
            if(n<txa->cols && !(flags&TXRF_BEVEL))
            {  SetAPen(rp,0);
               RectFill(rp,x+n*txa->charw,y+(i-txa->top)*txa->charh,
                  x+txa->cols*txa->charw-1,y+(i-txa->top+1)*txa->charh-1);
            }
            if(flags&TXRF_CURRENTONLY) break;
         }
         if(!*q) break;
         p=q+1;
      }
      if(!(flags&(TXRF_BEVEL|TXRF_CURRENTONLY)))
      {  /* Clear window below text */
         if(txa->height-txa->top<txa->rows)
         {  SetAPen(rp,0);
            RectFill(rp,x,y+(txa->height-txa->top)*txa->charh,
               x+txa->cols*txa->charw-1,y+txa->rows*txa->charh-1);
         }
      }
      if(txa->flags&TXAF_CURSOR && !(flags&TXRF_NOCURSOR))
      {  long cursorx=Gettextareacursorx(txa,font,rp);
         SetDrMd(rp,COMPLEMENT);
         RectFill(rp,x+cursorx,y+(txa->cury-txa->top)*txa->charh,
            x+cursorx+1-1,y+(txa->cury-txa->top+1)*txa->charh-1);
      }
      SetDrMd(rp,JAM1);
   }
   Unclipcoords(coo);
}

/* Ensure the current position is visible */
static void Makevisible(struct Textarea *txa)
{  struct Arect vis;
   struct TextFont *font=Gettextareafont(txa);
   struct RastPort *rp;
   long cursorx;
   
   /* Get a rastport for measuring - use mrp if available */
   rp=mrp;
   if(rp)
   {  cursorx=Gettextareacursorx(txa,font,rp);
   }
   else
   {  /* Fallback to fixed-width calculation if no rastport available */
      cursorx=(txa->curx-txa->left)*txa->charw;
   }
   
   vis.minx=txa->aox+tbevelw+2+cursorx;
   vis.miny=txa->aoy+tbevelh+2+(txa->cury-txa->top)*txa->charh;
   vis.maxx=vis.minx+1;
   vis.maxy=vis.miny+txa->charh-1;
   Asetattrs(txa->frame,AOFRM_Makevisible,&vis,TAG_END);
}

/* Copy the textarea buffer to the clipboard */
static void Copytextarea(struct Textarea *txa)
{  Clipcopy(txa->buf.buffer,txa->buf.length);
}

/* Paste the clipboard into the textarea buffer at the current cursor pos */
static void Pastetextarea(struct Textarea *txa)
{  UBYTE *buf=NULL;
   long bufl,l;
   /* Try again with larger buffers until clip fits */
   for(bufl=1024;;bufl+=1024)
   {  if(buf) FREE(buf);
      if(buf=ALLOCTYPE(UBYTE,bufl,MEMF_PUBLIC|MEMF_CLEAR))
      {  l=Clippaste(buf,bufl-1);
         if(l<bufl-1) break;
      }
   }
   Insertinbuffer(&txa->buf,buf,l,txa->pos+txa->curx);
   txa->flags|=TXAF_CHANGED;
}

/* Handles keyboard edit input */
static long Handlekeytext(struct Textarea *txa,struct Coords *coo,
   USHORT code,USHORT qual,void *iaddress)
{  BOOL shift=(qual&(IEQUALIFIER_RSHIFT|IEQUALIFIER_LSHIFT));
   BOOL ramiga=(qual&IEQUALIFIER_RCOMMAND);
   long result=AMR_ACTIVE;
   struct InputEvent ie={0};
   UBYTE buffer[8];
   long max=0x7fffffff;
   BOOL render=FALSE,renderall=FALSE,measure=FALSE,scroll=FALSE;
   short linediff=0; /* resulting change in current line to allow cursor movement */
   switch(code)
   {  case 0x42:  /* Tab */
      case 0x45:  /* Esc */
         result=AMR_NOREUSE;
         break;
      case 0x43:  /* Num Enter */
      case 0x44:  /* Enter */
         if(Insertinbuffer(&txa->buf,"\n",1,txa->pos+txa->curx))
         {  txa->cury++;
            txa->curx=0;
            measure=TRUE;
            renderall=TRUE;
            txa->flags|=TXAF_CHANGED;
         }
         break;
      case 0x4f:  /* Left */
         if(shift) txa->curx=0;
         else
         {  if(txa->curx>0) txa->curx--;
            else if(txa->cury>0)
            {  txa->cury--;
               txa->curx=max;
               linediff=-1;
            }
         }
         render=TRUE;
         break;
      case 0x4e:  /* Right */
         if(shift) txa->curx=max;
         else
         {  if(txa->curx<txa->length) txa->curx++;
            else if(txa->cury<txa->height-1)
            {  txa->cury++;
               txa->curx=0;
               linediff=1;
            }
         }
         render=TRUE;
         break;
      case 0x4c:  /* Up */
         if(shift)
         {  if(txa->cury<txa->rows) txa->cury=0;
            else txa->cury-=txa->rows-1;
            renderall=TRUE;
         }
         else if(txa->cury>0)
         {  txa->cury--;
            linediff=-1;
            render=TRUE;
         }
         break;
      case 0x4d:  /* Down */
         if(shift)
         {  if(txa->cury>txa->height-txa->rows) txa->cury=txa->height-1;
            else txa->cury+=txa->rows-1;
            renderall=TRUE;
         }
         else if(txa->cury<txa->height-1)
         {  txa->cury++;
            linediff=1;
            render=TRUE;
         }
         break;
      case 0x46:  /* Del */
         if(txa->curx<txa->length)
         {  do
            {  Deleteinbuffer(&txa->buf,txa->pos+txa->curx,1);
               measure=TRUE;
               render=TRUE;
               txa->flags|=TXAF_CHANGED;
               txa->length--;
            }while (shift && txa->curx<txa->length);
         }
         else if(txa->buf.buffer[txa->pos+txa->curx])
         {  Deleteinbuffer(&txa->buf,txa->pos+txa->curx,1);
            measure=TRUE;
            renderall=TRUE;
            txa->flags|=TXAF_CHANGED;
         }
         break;
      case 0x41:  /* BS */
         if(txa->curx>0)
         {  do
            {  Deleteinbuffer(&txa->buf,txa->pos+txa->curx-1,1);
               txa->curx--;
               measure=TRUE;
               render=TRUE;
               txa->flags|=TXAF_CHANGED;
            }while(shift && txa->curx>0);
         }
         else if(txa->cury>0)
         {  long pos=txa->pos-1;
            Deleteinbuffer(&txa->buf,txa->pos-1,1);
            txa->cury--;
            Findline(txa);
            txa->curx=pos-txa->pos;
            measure=TRUE;
            renderall=TRUE;
            txa->flags|=TXAF_CHANGED;
         }
         break;
      default:
         ie.ie_Class=IECLASS_RAWKEY;
         ie.ie_SubClass=0;
         ie.ie_Code=code;
         ie.ie_Qualifier=qual;
         ie.ie_EventAddress=*(APTR *)iaddress;
         if(MapRawKey(&ie,buffer,8,NULL)==1)
         {  if(ramiga)
            {  switch(toupper(buffer[0]))
               {  case 'C':
                     Copytextarea(txa);
                     break;
                  case 'V':
                     Pastetextarea(txa);
                     measure=TRUE;
                     scroll=TRUE;
                     renderall=TRUE;
                     break;
               }
            }
            else if(buffer[0]==0x19)  /* Ctrl-Y */
            {  long start,length;
//Unclipcoords(coo);coo=NULL; /* Prevent CPR lockup */
               start=txa->pos;
               length=txa->length;
               /* Remove newline after line too. Except for last line,
                * there remove the newline in front of the line, except if it is
                * also the first line. */
               if(txa->cury<txa->height-1) length++;
               else if(txa->cury>0)
               {  start--;
                  length++;
               }
               Deleteinbuffer(&txa->buf,start,length);
               txa->curx=0;
               if(txa->cury>0 && txa->cury>=txa->height-1) txa->cury--;
               Findline(txa);
               measure=TRUE;
               renderall=TRUE;
               txa->flags|=TXAF_CHANGED;
            }
            else if(Isprint(buffer[0]))
            {  if(Insertinbuffer(&txa->buf,buffer,1,txa->pos+txa->curx))
               {  txa->curx++;
                  txa->length++;
                  render=TRUE;
                  if(txa->length>txa->width)
                  {  txa->width=txa->length;
                     Asetattrs(txa->hscroll,
                        AOSCR_Total,txa->width,
                        AOSCR_Update,TRUE,
                        TAG_END);
                     txa->flags|=TXAF_CHANGED;
                  }
               }
            }
         }
   }
   if(result==AMR_ACTIVE)
   {  if(measure) scroll=Measuretext(txa);
      if(Curposadjust(txa))
      {  renderall=TRUE;
         scroll=TRUE;
      }
      Makevisible(txa); /* Can't use coo after this */
      if(renderall) Rendertext(txa,NULL,0);
      else if(render)
      {  if(linediff)
         {  txa->cury-=linediff;
            Rendertext(txa,coo,TXRF_CURRENTONLY|TXRF_NOCURSOR);
            txa->cury+=linediff;
         }
         Rendertext(txa,NULL,TXRF_CURRENTONLY);
      }
      if(scroll)
      {  Asetattrs(txa->vscroll,
            AOSCR_Total,txa->height,
            AOSCR_Visible,txa->rows,
            AOSCR_Top,txa->top,
            AOSCR_Update,TRUE,
            TAG_END);
         Asetattrs(txa->hscroll,
            AOSCR_Total,txa->width,
            AOSCR_Visible,txa->cols,
            AOSCR_Top,txa->left,
            AOSCR_Update,TRUE,
            TAG_END);
      }
      if(renderall || linediff) Findline(txa);
   }
   return result;
}

/* Activate text area. x and y are relative to textarea object */
static long Goactivetext(struct Textarea *txa,struct Amgoactive *amg,long x,long y)
{  long result=0;
   x=x-tbevelw-2;
   y=y-tbevelh-2;
   if(!amg)
   {  /* Activated via focus() method, use old cursor position */
      txa->flags|=TXAF_CURSOR;
   }
   else if(x>=0 && x<txa->cols*txa->charw && y>=0 && y<txa->rows*txa->charh)
   {  struct TextFont *font=Gettextareafont(txa);
      struct RastPort *rp=mrp;
      long liney=y/txa->charh+txa->top;
      txa->cury=liney;
      if(rp)
      {  txa->curx=Xpostocharposinline(txa,x,liney,font,rp);
      }
      else
      {  /* Fallback to fixed-width calculation if no rastport available */
         txa->curx=x/txa->charw+txa->left;
      }
      txa->flags|=TXAF_CURSOR;
   }
   if(txa->flags&TXAF_CURSOR)
   {  Curposadjust(txa);
      Findline(txa);
      Makevisible(txa);
      Rendertext(txa,NULL,TXRF_CURRENTONLY);
      result=AMR_ACTIVE;
   }
   return result;
}

static long Handleinputtext(struct Textarea *txa,struct Aminput *ami)
{  struct Coords *coo=Clipcoords(txa->cframe,NULL);
   long x,y;
   long result=AMR_REUSE;
   if(coo && ami->imsg)
   {  switch(ami->imsg->Class)
      {  case IDCMP_MOUSEBUTTONS:
            if(ami->imsg->Code==SELECTDOWN)
            {  x=ami->imsg->MouseX-txa->aox-coo->dx-tbevelw-2;
               y=ami->imsg->MouseY-txa->aoy-coo->dy-tbevelh-2;
               if(x>=0 && x<txa->cols*txa->charw && y>=0 && y<txa->rows*txa->charh)
               {  struct TextFont *font=Gettextareafont(txa);
                  struct RastPort *rp=coo->rp;
                  long liney=y/txa->charh+txa->top;
                  if(y!=txa->cury)
                  {  Rendertext(txa,coo,TXRF_CURRENTONLY|TXRF_NOCURSOR);
                  }
                  txa->cury=liney;
                  if(rp)
                  {  txa->curx=Xpostocharposinline(txa,x,liney,font,rp);
                  }
                  else
                  {  /* Fallback to fixed-width calculation if no rastport available */
                     txa->curx=x/txa->charw+txa->left;
                  }
                  Curposadjust(txa);
                  Findline(txa);
                  Makevisible(txa); /* can't use coords after this */
                  Rendertext(txa,NULL,TXRF_CURRENTONLY);
                  result=AMR_ACTIVE;
               }
               else
               {  /* Click outside edit area */
                  result=AMR_REUSE;
               }
            }
            else if(ami->imsg->Code==SELECTUP)
            {  result=AMR_ACTIVE;
            }
            break;
         case IDCMP_RAWKEY:
            result=Handlekeytext(txa,coo,
               ami->imsg->Code,ami->imsg->Qualifier,ami->imsg->IAddress);
            break;
         case IDCMP_MOUSEMOVE:
         case IDCMP_INTUITICKS:
            result=AMR_ACTIVE;
            break;
      }
   }
   Unclipcoords(coo);
   return result;
}

/*------------------------------------------------------------------------*/

static void Renderbutton(struct Textarea *txa,struct Coords *coo)
{  short i,pen,bpen,state;
   struct RastPort *rp;
   struct ColorMap *colormap=NULL;
   struct DrawInfo *drinfo=NULL;
   if((coo=Clipcoords(txa->cframe,coo)) && coo->rp)
   {  rp=coo->rp;
      Agetattrs(Aweb(),
         AOAPP_Colormap,&colormap,
         AOAPP_Drawinfo,&drinfo,
         TAG_END);
      if(txa->flags&TXAF_EDITBUTACT)
      {  state=IDS_SELECTED;
         pen=coo->dri->dri_Pens[FILLTEXTPEN];
         bpen=coo->dri->dri_Pens[FILLPEN];
      }
      else
      {  state=IDS_NORMAL;
         pen=coo->dri->dri_Pens[TEXTPEN];
         bpen=0;
      }
      SetAttrs(bbevel,
         IA_Width,txa->scrw,
         IA_Height,txa->scrh,
         BEVEL_FillPen,bpen,
         BEVEL_ColorMap,colormap,
         BEVEL_Flags,BFLG_XENFILL,
         REACTION_SpecialPens,txa->capens,
         TAG_END);
      DrawImageState(rp,bbevel,
         txa->aox+coo->dx+txa->aow-txa->scrw,txa->aoy+coo->dy+txa->aoh-txa->scrh,
         state,drinfo);
      for(i=0;epictdata[i].dl_Directive;i++)
      {  epictdata[i].dl_Pen=pen;
      }
      SetAttrs(epict,
         IA_Width,txa->scrw-2*bbevelw,
         IA_Height,txa->scrh-2*bbevelh,
         DRAWLIST_Directives,epictdata,
         TAG_END);
      DrawImageState(rp,epict,
         txa->aox+coo->dx+txa->aow-txa->scrw+bbevelw,
         txa->aoy+coo->dy+txa->aoh-txa->scrh+bbevelh,
         state,drinfo);
   }
   Unclipcoords(coo);
}

static long Handleinputbutton(struct Textarea *txa,struct Aminput *ami)
{  struct Coords *coo=Clipcoords(txa->cframe,NULL);
   long x,y;
   long result=AMR_REUSE;
   if(coo && ami->imsg)
   {  switch(ami->imsg->Class)
      {  case IDCMP_MOUSEMOVE:
            x=ami->imsg->MouseX-txa->aox-coo->dx-txa->aow+txa->scrw;
            y=ami->imsg->MouseY-txa->aoy-coo->dy-txa->aoh+txa->scrh;
            if(x>=0 && x<txa->scrw && y>=0 && y<txa->scrh)
            {  if(!(txa->flags&TXAF_EDITBUTACT))
               {  txa->flags|=TXAF_EDITBUTACT;
                  Renderbutton(txa,coo);
               }
            }
            else
            {  if(txa->flags&TXAF_EDITBUTACT)
               {  txa->flags&=~TXAF_EDITBUTACT;
                  Renderbutton(txa,coo);
               }
            }
            result=AMR_ACTIVE;
            break;
         case IDCMP_MOUSEBUTTONS:
            if(ami->imsg->Code==SELECTUP)
            {  x=ami->imsg->MouseX-txa->aox-coo->dx-txa->aow+txa->scrw;
               y=ami->imsg->MouseY-txa->aoy-coo->dy-txa->aoh+txa->scrh;
               if(x>=0 && x<txa->scrw && y>=0 && y<txa->scrh)
               {  if(txa->editor)
                  {  Adisposeobject(txa->editor);
                  }
                  txa->editor=Anewobject(AOTP_EDITOR,
                     AOEDT_Data,txa->buf.buffer,
                     AOEDT_Datalength,txa->buf.length-1,
                     AOBJ_Target,txa,
                     TAG_END);
               }
            }
            result=AMR_NOREUSE;
            break;
         case IDCMP_RAWKEY:
         case IDCMP_INTUITICKS:
            result=AMR_ACTIVE;
            break;
      }
   }
   Unclipcoords(coo);
   return result;
}

/*------------------------------------------------------------------------*/

/* Get or set value property (js) */
static BOOL Propertyvalue(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Textarea *txa=vd->hookdata;
   UBYTE *p;
   long l;
   if(txa)
   {  switch(vd->code)
      {  case VHC_SET:
            p=Jtostring(vd->jc,vd->value);
            l=strlen(p);
            Deleteinbuffer(&txa->buf,0,txa->buf.length);
            Addtobuffer(&txa->buf,p,l);
            Addtobuffer(&txa->buf,"",1);
            Measuretext(txa);
            txa->top=txa->left=0;
            txa->curx=txa->cury=0;
            Asetattrs(txa->vscroll,
               AOSCR_Total,txa->height,
               AOSCR_Visible,txa->rows,
               AOSCR_Top,txa->top,
               TAG_END);
            Asetattrs(txa->hscroll,
               AOSCR_Total,txa->width,
               AOSCR_Visible,txa->cols,
               AOSCR_Top,txa->left,
               TAG_END);
            if(txa->eltflags&ELTF_ALIGNED)
            {  Arender(txa->vscroll,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
               Arender(txa->hscroll,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
               Rendertext(txa,NULL,0);
            }
            result=TRUE;
            break;
         case VHC_GET:
            p=Dupstr(txa->buf.buffer,txa->buf.length);
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

static void Methodfocus(struct Jcontext *jc)
{  struct Textarea *txa=Jointernal(Jthis(jc));
   if(txa)
   {  txa->flags|=TXAF_NOJSEH;
      Asetattrs(txa->win,AOWIN_Activeobject,txa,TAG_END);
      txa->flags&=~TXAF_NOJSEH;
   }
}

static void Methodblur(struct Jcontext *jc)
{  struct Textarea *txa=Jointernal(Jthis(jc));
   if(txa && txa->flags&TXAF_CURSOR)
   {  /* This object has focus */
      txa->flags|=TXAF_NOJSEH;
      Asetattrs(txa->win,AOWIN_Activeobject,NULL,TAG_END);
      txa->flags&=~TXAF_NOJSEH;
   }
}

static void Methodtostring(struct Jcontext *jc)
{  struct Textarea *txa=Jointernal(Jthis(jc));
   struct Buffer buf={0};
   if(txa)
   {  Addtagstr(&buf,"<textarea",ATSF_NONE,0);
      if(txa->name) Addtagstr(&buf,"name",ATSF_STRING,txa->name);
      Addtagstr(&buf,"rows",ATSF_NUMBER,txa->rows);
      Addtagstr(&buf,"cols",ATSF_NUMBER,txa->cols);
      Addtobuffer(&buf,">\n",2);
      if(txa->value) Addtobuffer(&buf,txa->value,strlen(txa->value));
      Addtobuffer(&buf,"\n</textarea>",13);  /* Including nullbyte */
      Jasgstring(jc,NULL,buf.buffer);
      Freebuffer(&buf);
   }
}

/*------------------------------------------------------------------------*/

static long Measuretextarea(struct Textarea *txa,struct Ammeasure *amm)
{  if(txa->flags&TXAF_COMPLETE)
   {  txa->aow=txa->cols*txa->charw+2*tbevelw+txa->scrw+4;
      txa->aoh=txa->rows*txa->charh+2*tbevelh+txa->scrh+4;
      AmethodasA(AOTP_FIELD,txa,amm);
   }
   return 0;
}

static long Aligntextarea(struct Textarea *txa,struct Amalign *ama)
{  long result=AmethodasA(AOTP_FIELD,txa,ama);
   Asetattrs(txa->hscroll,
      AOBJ_Left,txa->aox,
      AOBJ_Top,txa->aoy+txa->rows*txa->charh+2*tbevelh+4,
      AOBJ_Width,txa->aow-txa->scrw,
      TAG_END);
   Asetattrs(txa->vscroll,
      AOBJ_Left,txa->aox+txa->cols*txa->charw+2*tbevelw+4,
      AOBJ_Top,txa->aoy,
      AOBJ_Height,txa->aoh-txa->scrh,
      TAG_END);
   return result;
}

static long Movetextarea(struct Textarea *txa,struct Ammove *amm)
{  long result=AmethodasA(AOTP_FIELD,txa,amm);
   Asetattrs(txa->hscroll,
      AOBJ_Left,txa->aox,
      AOBJ_Top,txa->aoy+txa->rows*txa->charh+2*tbevelh+4,
      AOBJ_Width,txa->aow-txa->scrw,
      TAG_END);
   Asetattrs(txa->vscroll,
      AOBJ_Left,txa->aox+txa->cols*txa->charw+2*tbevelw+4,
      AOBJ_Top,txa->aoy,
      AOBJ_Height,txa->aoh-txa->scrh,
      TAG_END);
   return result;
}

static long Rendertextarea(struct Textarea *txa,struct Amrender *amr)
{  struct Coords *coo;
   struct RastPort *rp;
   if((txa->flags&TXAF_COMPLETE) && (txa->eltflags&ELTF_ALIGNED))
   {  if((coo=Clipcoords(txa->cframe,amr->coords)) && coo->rp)
      {  rp=coo->rp;
         Rendertext(txa,coo,TXRF_BEVEL);
         Arender(txa->vscroll,coo,0,0,AMRMAX,AMRMAX,0,NULL);
         Arender(txa->hscroll,coo,0,0,AMRMAX,AMRMAX,0,NULL);
         Renderbutton(txa,coo);
      }
      Unclipcoords(coo);
   }
   return 0;
}

static long Settextarea(struct Textarea *txa,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   long n;
   UBYTE *data=NULL;
   long length=0;
   result=Amethodas(AOTP_FIELD,txa,AOM_SET,ams->tags);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOTXA_Cols:
            txa->cols=MAX(2,tag->ti_Data);
            break;
         case AOTXA_Rows:
            txa->rows=MAX(2,tag->ti_Data);
            break;
         case AOTXA_Text:
            Addtobuffer(&txa->buf,(UBYTE *)tag->ti_Data,strlen((UBYTE *)tag->ti_Data));
            break;
         case AOTXA_Complete:
            if(tag->ti_Data) Completetextarea(txa);
            break;
         case AOBJ_Cframe:
            Asetattrs(txa->hscroll,AOBJ_Cframe,tag->ti_Data,TAG_END);
            Asetattrs(txa->vscroll,AOBJ_Cframe,tag->ti_Data,TAG_END);
            if(txa->cframe && (txa->flags&TXAF_COMPLETE)) Adjusttextarea(txa);
            break;
         case AOBJ_Frame:
            txa->frame=(void *)tag->ti_Data;
            break;
         case AOBJ_Window:
            txa->win=(void *)tag->ti_Data;
            if(tag->ti_Data)
            {  txa->capens=(void *)Agetattr(txa->win,AOWIN_Specialpens);
            }
            else
            {  txa->capens=NULL;
            }
            Asetattrs(txa->hscroll,AOBJ_Window,tag->ti_Data,TAG_END);
            Asetattrs(txa->vscroll,AOBJ_Window,tag->ti_Data,TAG_END);
            break;
         case AOTXA_Top:
            n=MIN(tag->ti_Data,txa->height-txa->rows);
            if(n<0) n=0;
            if(n!=txa->top)
            {  txa->top=n;
               Rendertext(txa,NULL,0);
            }
            break;
         case AOTXA_Left:
            n=MIN(tag->ti_Data,txa->width-txa->cols);
            if(n<0) n=0;
            if(n!=txa->left)
            {  txa->left=n;
               Rendertext(txa,NULL,0);
            }
            break;
         case AOFLD_Reset:
            Resettextarea(txa);
            break;
         case AOFLD_Onchange:
            if(txa->onchange) FREE(txa->onchange);
            txa->onchange=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onfocus:
            if(txa->onfocus) FREE(txa->onfocus);
            txa->onfocus=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onblur:
            if(txa->onblur) FREE(txa->onblur);
            txa->onblur=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onselect:
            if(txa->onselect) FREE(txa->onselect);
            txa->onselect=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOEDT_Data:
            data=(UBYTE *)tag->ti_Data;
            break;
         case AOEDT_Datalength:
            length=tag->ti_Data;
            break;
      }
   }
   if(data)
   {  Freebuffer(&txa->buf);
      Addtobuffer(&txa->buf,data,length);
      Addtobuffer(&txa->buf,"",1);
      Measuretext(txa);
      txa->top=txa->left=0;
      txa->curx=txa->cury=0;
      Asetattrs(txa->vscroll,
         AOSCR_Total,txa->height,
         AOSCR_Visible,txa->rows,
         AOSCR_Top,txa->top,
         TAG_END);
      Asetattrs(txa->hscroll,
         AOSCR_Total,txa->width,
         AOSCR_Visible,txa->cols,
         AOSCR_Top,txa->left,
         TAG_END);
      Arender(txa->vscroll,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
      Arender(txa->hscroll,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
      Rendertext(txa,NULL,0);
      txa->flags|=TXAF_CHANGED;
   }
   return result;
}

static long Gettextarea(struct Textarea *txa,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=AmethodasA(AOTP_FIELD,txa,ams);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFLD_Value:
            PUTATTR(tag,Fieldvalue(txa));
            break;
      }
   }
   return result;
}

static struct Textarea *Newtextarea(struct Amset *ams)
{  struct Textarea *txa;
   if(txa=Allocobject(AOTP_TEXTAREA,sizeof(struct Textarea),ams))
   {  txa->rows=5;
      txa->cols=20;
      txa->hscroll=Anewobject(AOTP_SCROLLER,
         AOSCR_Orient,AOSCRORIENT_HORIZ,
         AOBJ_Target,txa,
         AOBJ_Map,maphscroll,
         AOBJ_Cframe,txa->cframe,
         TAG_END);
      txa->vscroll=Anewobject(AOTP_SCROLLER,
         AOSCR_Orient,AOSCRORIENT_VERT,
         AOBJ_Target,txa,
         AOBJ_Map,mapvscroll,
         AOBJ_Cframe,txa->cframe,
         TAG_END);
      Settextarea(txa,ams);
      txa->valign=VALIGN_TOP;
   }
   return txa;
}

static long Hittesttextarea(struct Textarea *txa,struct Amhittest *amh)
{  long result=0;
   long x,y;
   BOOL myhit=FALSE;
   if(amh->coords)
   {  x=amh->xco-txa->aox-amh->coords->dx;
      y=amh->yco-txa->aoy-amh->coords->dy;
      if(x<txa->aow-txa->scrw)
      {  if(y<txa->aoh-txa->scrh)
         {  myhit=TRUE;
         }
         else result=AmethodA(txa->hscroll,amh);
      }
      else
      {  if(y<txa->aoh-txa->scrh)
         {  result=AmethodA(txa->vscroll,amh);
         }
         else
         {  myhit=TRUE;
         }
      }
   }
   if(myhit)
   {  if(amh->oldobject==txa)
      {  result=AMHR_OLDHIT;
      }
      else
      {  result=AMHR_NEWHIT;
         if(amh->amhr)
         {  amh->amhr->object=txa;
            amh->amhr->ptrtype=APTR_TEXT;
         }
      }
   }
   return result;
}

static long Goactivetextarea(struct Textarea *txa,struct Amgoactive *amg)
{  struct Coords *coo=Clipcoords(txa->cframe,NULL);
   long x,y,result=0;
   if(coo && amg->imsg)
   {  x=amg->imsg->MouseX-txa->aox-coo->dx;
      y=amg->imsg->MouseY-txa->aoy-coo->dy;
      if(x<txa->aow-txa->scrw)
      {  if(y<txa->aoh-txa->scrh)
         {  result=Goactivetext(txa,amg,x,y);
         }
      }
      else
      {  if(y>=txa->aoh-txa->scrh)
         {  txa->flags|=TXAF_EDITBUTTON|TXAF_EDITBUTACT;
            result=AMR_ACTIVE;
            Renderbutton(txa,coo);
         }
      }
   }
   else if(!amg->imsg)
   {  /* Activated via focus() method */
      result=Goactivetext(txa,NULL,0,0);
   }
   Unclipcoords(coo);
   Asetattrs(txa->win,AOWIN_Rmbtrap,TRUE,TAG_END);
   if(txa->flags&TXAF_CURSOR)
   {  txa->flags&=~TXAF_CHANGED;
      if(!(txa->flags&TXAF_NOJSEH))
      {  if(txa->onfocus || AWebJSBase)
         {  Runjavascriptwith(txa->cframe,awebonfocus,&txa->jobject,txa->form);
         }
      }
   }
   return result;
}

static long Handleinputtextarea(struct Textarea *txa,struct Aminput *ami)
{  long result=0;
   if(txa->flags&TXAF_CURSOR)
   {  result=Handleinputtext(txa,ami);
   }
   else if(txa->flags&TXAF_EDITBUTTON)
   {  result=Handleinputbutton(txa,ami);
   }
   return result;
}

static long Goinactivetextarea(struct Textarea *txa)
{  if(txa->flags&TXAF_CURSOR)
   {  txa->flags&=~TXAF_CURSOR;
      Rendertext(txa,NULL,TXRF_CURRENTONLY);
      if(txa->flags&TXAF_CHANGED)
      {  if(txa->onchange || AWebJSBase)
         {  Runjavascriptwith(txa->cframe,awebonchange,&txa->jobject,txa->form);
         }
      }
      if(!(txa->flags&TXAF_NOJSEH))
      {  if(txa->onblur || AWebJSBase)
         {  Runjavascriptwith(txa->cframe,awebonblur,&txa->jobject,txa->form);
         }
      }
   }
   else if(txa->flags&TXAF_EDITBUTTON)
   {  if(txa->flags&TXAF_EDITBUTACT)
      {  txa->flags&=~TXAF_EDITBUTACT;
         Renderbutton(txa,NULL);
      }
      txa->flags&=~TXAF_EDITBUTTON;
   }
   Asetattrs(txa->win,AOWIN_Rmbtrap,FALSE,TAG_END);
   return 0;
}

static long Jsetuptextarea(struct Textarea *txa,struct Amjsetup *amj)
{  struct Jvar *jv;
   AmethodasA(AOTP_FIELD,txa,amj);
   if(txa->jobject)
   {  if(jv=Jproperty(amj->jc,txa->jobject,"defaultValue"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgstring(amj->jc,jv,txa->value);
      }
      if(jv=Jproperty(amj->jc,txa->jobject,"value"))
      {  Setjproperty(jv,Propertyvalue,txa);
      }
      if(jv=Jproperty(amj->jc,txa->jobject,"type"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgstring(amj->jc,jv,"textarea");
      }
      Addjfunction(amj->jc,txa->jobject,"focus",Methodfocus,NULL);
      Addjfunction(amj->jc,txa->jobject,"blur",Methodblur,NULL);
      Addjfunction(amj->jc,txa->jobject,"select",Methodselect,NULL);
      Addjfunction(amj->jc,txa->jobject,"toString",Methodtostring,NULL);
      Jaddeventhandler(amj->jc,txa->jobject,"onfocus",txa->onfocus);
      Jaddeventhandler(amj->jc,txa->jobject,"onblur",txa->onblur);
      Jaddeventhandler(amj->jc,txa->jobject,"onchange",txa->onchange);
      Jaddeventhandler(amj->jc,txa->jobject,"onselect",txa->onselect);
   }
   return 0;
}

static void Disposetextarea(struct Textarea *txa)
{  if(txa->hscroll) Adisposeobject(txa->hscroll);
   if(txa->vscroll) Adisposeobject(txa->vscroll);
   Freebuffer(&txa->buf);
   if(txa->fldvalue) FREE(txa->fldvalue);
   if(txa->editor) Adisposeobject(txa->editor);
   if(txa->onblur) FREE(txa->onblur);
   if(txa->onchange) FREE(txa->onchange);
   if(txa->onfocus) FREE(txa->onfocus);
   if(txa->onselect) FREE(txa->onselect);
   Amethodas(AOTP_FIELD,txa,AOM_DISPOSE);
}

static void Deinstalltextarea(void)
{  if(bbevel) DisposeObject(bbevel);
   if(tbevel) DisposeObject(tbevel);
   if(epict) DisposeObject(epict);
}

static long Dispatch(struct Textarea *txa,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newtextarea((struct Amset *)amsg);
         break;
      case AOM_SET:
      case AOM_UPDATE:
         result=Settextarea(txa,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Gettextarea(txa,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measuretextarea(txa,(struct Ammeasure *)amsg);
         break;
      case AOM_ALIGN:
         result=Aligntextarea(txa,(struct Amalign *)amsg);
         break;
      case AOM_MOVE:
         result=Movetextarea(txa,(struct Ammove *)amsg);
         break;
      case AOM_RENDER:
         result=Rendertextarea(txa,(struct Amrender *)amsg);
         break;
      case AOM_HITTEST:
         result=Hittesttextarea(txa,(struct Amhittest *)amsg);
         break;
      case AOM_GOACTIVE:
         result=Goactivetextarea(txa,(struct Amgoactive *)amsg);
         break;
      case AOM_HANDLEINPUT:
         result=Handleinputtextarea(txa,(struct Aminput *)amsg);
         break;
      case AOM_GOINACTIVE:
         result=Goinactivetextarea(txa);
         break;
      case AOM_JSETUP:
         result=Jsetuptextarea(txa,(struct Amjsetup *)amsg);
         break;
      case AOM_DISPOSE:
         Disposetextarea(txa);
         break;
      case AOM_DEINSTALL:
         Deinstalltextarea();
         break;
      default:
         result=AmethodasA(AOTP_FIELD,txa,amsg);
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installtextarea(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_TEXTAREA,Dispatch)) return FALSE;
   if(!(tbevel=BevelObject,
      BEVEL_Style,BVS_FIELD,
      End)) return FALSE;
   GetAttr(BEVEL_VertSize,tbevel,(ULONG *)&tbevelw);
   GetAttr(BEVEL_HorizSize,tbevel,(ULONG *)&tbevelh);
   if(!(bbevel=BevelObject,
      BEVEL_Style,BVS_BUTTON,
      End)) return FALSE;
   GetAttr(BEVEL_VertSize,bbevel,(ULONG *)&bbevelw);
   GetAttr(BEVEL_HorizSize,bbevel,(ULONG *)&bbevelh);
   if(!(epict=DrawListObject,
      DRAWLIST_Directives,epictdata,
      DRAWLIST_RefHeight,4096,
      DRAWLIST_RefWidth,4096,
      End)) return FALSE;
   return TRUE;
}
