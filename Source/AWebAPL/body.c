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

/* body.c - AWeb html document body object */

#include "aweb.h"
#include "body.h"
#include "element.h"
#include "docprivate.h"
#include "frprivate.h"
#include "copy.h"
#include "window.h"
#include "css.h"
#include "html.h"
#include "application.h"
#include "ttengine.h"
#include "copydriver.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/graphics.h>

/*------------------------------------------------------------------------*/
/* Childs (elements) are added dynamically, and a child can change later.
 * While LAYOUTing, the start of each horizontal line is remembered.
 * MEASURE with the CHANGED flag set will start at the beginning of the
 * last line.
 * LAYOUT with the CHANGED flag set will start at the beginning of the
 * last line that has no (floating object) margins and is not in the middle
 * of a multiline element.
 * RENDER with the CHANGED flag set will start at the beginning of the
 * last line, or if also the CLEAR flag is set with the last line that has
 * no (floating object) margins. Since LAYOUT will add lines it remembers
 * the y position of the first line added, and RENDER will start looking at
 * this line instead of the last line in the list.
 * Without CHANGED flag these methods will start at the first element.
 *
 * When AOBJ_Changedchild is set, the child is looked up and all remembered
 * lines after this child are removed. This ensures the next CHANGED methods
 * will start at or before the line containing the changed child.
 */

/*------------------------------------------------------------------------*/

struct Body
{  struct Aobject object;
   long aox,aoy,aow,aoh;      /* Body dimensions */
   void *frame;               /* FRAME object */
   void *cframe;              /* Owner BODY or FRAME object */
   void *pool;                /* memory pool */
   void *parent;              /* Parent document */
   void *tcell;               /* The table cell this body belongs to or NULL */
   LIST(Element) contents;    /* this body's children */
   short hmargin,vmargin;     /* body's outer margins */
   USHORT flags;
   void *win;                 /* pass to childs */
   LIST(Line) lines;          /* quick vertical index */
   struct Element *chchild;   /* first changed child */
   long rendery;              /* Y position of first line to render CHANGED */
   LIST(Margin) leftmargins;  /* Left side floating margins */
   LIST(Margin) rightmargins; /* Right side floating margins */
   short bgcolor;             /* Pen number for background or -1 */
   void *bgimage;             /* Background image or NULL */
   struct Aobject *bgalign;   /* Object to align background image to */
   ULONG bgupdate;            /* Value of bgupdate last time this body was rendered */
   LIST(Openfont) openfonts;  /* All fonts used (and therefore opened) by us */
   struct Bodybuild *bld;     /* Variables only needed during growth */
   UBYTE *tagname;            /* HTML tag name for CSS matching (e.g. "DIV", "P") */
   UBYTE *class;              /* CSS class name(s) for CSS matching */
   UBYTE *id;                 /* Element ID for CSS matching */
   UBYTE *position;          /* CSS position: "static", "relative", "absolute", "fixed" */
   long zindex;              /* CSS z-index value */
   UBYTE *display;           /* CSS display: "none", "block", "inline", "inline-block" */
   UBYTE *overflow;          /* CSS overflow: "visible", "hidden", "auto", "scroll" */
   UBYTE *clear;             /* CSS clear: "none", "left", "right", "both" */
   short verticalalign;      /* CSS vertical-align value */
   UBYTE *liststyle;         /* CSS list-style value */
   UBYTE *liststyleimage;    /* CSS list-style-image: url(...) value */
   long minwidth;            /* CSS min-width */
   long maxwidth;            /* CSS max-width */
   long minheight;           /* CSS min-height */
   long maxheight;           /* CSS max-height */
   long paddingtop;          /* CSS padding-top */
   long paddingright;       /* CSS padding-right */
   long paddingbottom;       /* CSS padding-bottom */
   long paddingleft;         /* CSS padding-left */
   long borderwidth;        /* CSS border width */
   struct Colorinfo *bordercolor; /* CSS border color */
   UBYTE *borderstyle;      /* CSS border style */
   struct Colorinfo *linktextcolor; /* Text color for current link (from CSS class-based selectors or inline styles) */
   long right;             /* CSS right position */
   long bottom;            /* CSS bottom position */
   UBYTE *cursor;          /* CSS cursor type */
   BOOL marginleftauto;    /* CSS margin-left: auto (for centering) */
   BOOL marginrightauto;   /* CSS margin-right: auto (for centering) */
   float lineheight;       /* CSS line-height multiplier for this body */
   UBYTE *texttransform;  /* CSS text-transform */
   UBYTE *whitespace;      /* CSS white-space */
   UBYTE *backgroundrepeat; /* CSS background-repeat: "repeat", "no-repeat", "repeat-x", "repeat-y" */
   UBYTE *backgroundposition; /* CSS background-position: "center", "top", "bottom", "left", "right", percentages, lengths */
   UBYTE *backgroundattachment; /* CSS background-attachment: "scroll", "fixed" */
   UBYTE *transform;       /* CSS transform: "translate(x, y)", "translateX(x)", "translateY(y)", etc. */
   long toppercent;        /* CSS top position as percentage (0-10000 for 0-100%) */
   long leftpercent;       /* CSS left position as percentage (0-10000 for 0-100%) */
   long marginright;       /* CSS margin-right value (can be negative) */
   long marginbottom;      /* CSS margin-bottom value (can be negative) */
   /* MARQUEE fields disabled
   UBYTE *marqueedirection;
   UBYTE *marqueebehavior;
   long marqueescrollamount;
   long marqueescrolldelay;
   long marqueeloop;
   long marqueescrollx;
   long marqueescrolly;
   long marqueeloopcount;
   BOOL marqueereversing;
   */
};

#define BDYF_SUB           0x0001   /* subscript mode */
#define BDYF_SUP           0x0002   /* superscript mode */
#define BDYF_CHANGEDCHILD  0x0004   /* we have a changed or added child */
#define BDYF_NOBR          0x0008   /* nobreak mode */
#define BDYF_NOBACKGROUND  0x0010   /* no backgrounds */
#define BDYF_FORCEBGCOLOR  0x0020   /* use bgcolor even is bg is off */
#define BDYF_LAYOUTREADY   0x0040   /* layout of all changed childs is ready */

struct Bodybuild
{  LIST(Fontinfo) font;       /* font stack */
   LIST(Listinfo) list;       /* Listinfo stack */
   USHORT hardstyle;          /* current hard style */
   short divalign,align;
   short fonttype;            /* current hard normal/fixed font type */
   short bqindent;            /* Current blockquote indent level */
   void *link;                /* current hyperlink */
};

/* Pushfont() "which" flags */
#define FONTW_STYLE     0x0001
#define FONTW_ABSSIZE   0x0002
#define FONTW_RELSIZE   0x0004
#define FONTW_COLOR     0x0008
#define FONTW_FACE      0x0010

struct Fontinfo               /* stacked character font info */
{  NODE(Fontinfo);            /* stack node */
   short type;                /* normal or fixed */
   short size;                /* size */
   USHORT flags;              /* see below */
   USHORT style;              /* resulting style */
   struct Colorinfo *color;   /* color for this font or NULL */
   UBYTE *face;               /* face name (reference) for this font or NULL */
   UBYTE *facestring;         /* dynamic string */
};

#define FONTF_RELSIZE   0x0001   /* size is relative */
#define FONTF_BASE      0x0002   /* can serve as base font size */
#define FONTF_COLOR     0x0004   /* color was explicitly set */
#define FONTF_FACE      0x0008   /* face was expliticly set */

struct Line
{  NODE(Line);
   struct Element *child;     /* First child on this line */
   long y;                    /* Smallest y coordinate on this line */
   long w;                    /* Width of this line */
   USHORT flags;
};

#define LINEF_MORE      0x0001   /* Line starts in the middle of a multiline element */
#define LINEF_MARGIN    0x0002   /* Line has floating margins */

#define LEFTINDENT(bd) ((ISEMPTY(&(bd)->bld->list)?0:\
   (bd)->bld->list.first->indent)+(bd)->bld->bqindent)
#define RIGHTINDENT(bd) ((bd)->bld->bqindent)

static struct Listinfo nolist={0};

#define INDENT(x) ((x)*40)

struct Margin                 /* Floating margin info */
{  NODE(Margin);
   long y;                    /* First ypos below margin */
   long eaten;                /* Current margin eaten from total */
   short indent;              /* List indentation level this object was placed at */
};

#define MARGIN_LEFT     0x0001
#define MARGIN_RIGHT    0x0002
#define MARGIN_ALL      0x0003   /* Only used in Findclearmargin() */

struct Openfont               /* Font opened by us */
{  NODE(Openfont);
   struct TextFont *font;     /* The font */
};

/*------------------------------------------------------------------------*/

/* Check if we already opened this font. If not, open it.
 * We must open all fonts we use ourselves, in case the preferences change
 * and the dynamic Fontinfo structure is disposed of and the font closed. */
struct Openfont *Addopenfont(struct Body *bd,struct TextFont *font)
{  struct Openfont *of;
   for(of=bd->openfonts.first;of->next;of=of->next)
   {  if(of->font==font)
      {  return of;
      }
   }
   if(of=ALLOCSTRUCT(Openfont,1,MEMF_CLEAR))
   {  struct TextAttr ta={0};
      ta.ta_Name=font->ln_Name;
      ta.ta_YSize=font->tf_YSize;
      of->font=OpenFont(&ta);
      ADDTAIL(&bd->openfonts,of);
   }
   return of;
}

/* Close this font and dispose structure */
void Freeopenfont(struct Openfont *of)
{  if(of)
   {  if(of->font) CloseFont(of->font);
      FREE(of);
   }
}

/*------------------------------------------------------------------------*/

/* Push a fontinfo on the stack. */
static BOOL Pushfont(struct Body *bd,short style,short size,struct Colorinfo *ci,
   UBYTE *face,USHORT which)
{  struct Fontinfo *fi=PALLOCSTRUCT(Fontinfo,1,MEMF_CLEAR|MEMF_PUBLIC,bd->pool);
   struct Styleprefs *sp;
   if(fi)
   {  ADDHEAD(&bd->bld->font,fi);
      if(which&FONTW_STYLE)
      {  sp=&prefs.browser.styles[style];
         fi->type=sp->fonttype;
         fi->size=sp->fontsize;
         fi->flags=sp->relsize?FONTF_RELSIZE:FONTF_BASE;
         fi->style=sp->style;
      }
      else
      {  fi->type=fi->next->type;
         fi->style=fi->next->style;
         if(which&FONTW_ABSSIZE)
         {  fi->size=size;
            fi->flags=0;
         }
         else if(which&FONTW_RELSIZE)
         {  fi->size=size;
            fi->flags=FONTF_RELSIZE;
         }
         else
         {  fi->size=fi->next->size;
            fi->flags=fi->next->flags&~FONTF_FACE;
         }
      }
      if(which&FONTW_COLOR)
      {  fi->color=ci;
         fi->flags|=FONTF_COLOR;
      }
      else if(fi->next->next)
      {  fi->color=fi->next->color;
      }
      if(which&FONTW_FACE)
      {  fi->facestring=Dupstr(face,-1);
         fi->face=fi->facestring;
         fi->flags|=FONTF_FACE;
      }
      else if(fi->next->next)
      {  fi->face=fi->next->face;
      }
//printf("pushfont color=%08x\n",fi->color);
   }
   return (BOOL)(fi!=NULL);
}

/* Pop a fontinfo from the stack */
static void Popfont(struct Body *bd)
{  struct Fontinfo *fi=bd->bld->font.first;
   if(fi->next->next)   /* leave at least one on stack */
   {  REMOVE(fi);
      if(fi->facestring) FREE(fi->facestring);
      FREE(fi);
//printf("popfont color=%08x\n",bd->bld->font.first->color);
   }
}

/* Sets basefont size. Just patch the size in the bottom fontinfo. */
static void Setbasefont(struct Body *bd,short size,BOOL rel)
{  short bsize=bd->bld->font.last->size;
   if(rel) bsize+=size;
   else bsize=size;
   if(bsize<1) bsize=1;
   if(bsize>7) bsize=7;
   bd->bld->font.last->size=bsize;
}

/* Sets basefont color. Patch in bottom fontinfo and in higher ones that didn't
 * set the color themselves */
static void Setbasecolor(struct Body *bd,struct Colorinfo *ci)
{  struct Fontinfo *fi;
   bd->bld->font.last->color=ci;
   for(fi=bd->bld->font.last->prev;fi->prev;fi=fi->prev)
   {  if(fi->flags&FONTF_COLOR) break;
      fi->color=ci;
   }
}

/* Sets basefont face. Patch in bottom fontinfo and in higher ones that didn't
 * set the face themselves */
static void Setbaseface(struct Body *bd,UBYTE *face)
{  struct Fontinfo *fi,*base;
   base=bd->bld->font.last;
   if(base->facestring) FREE(base->facestring);
   base->facestring=Dupstr(face,-1);
   base->face=base->facestring;
   for(fi=bd->bld->font.last->prev;fi->prev;fi=fi->prev)
   {  if(fi->flags&FONTF_FACE) break;
      fi->face=base->facestring;
   }
}

/* Nested BODY (table cell, etc.) has parent TABLE/FRAME, not DOCUMENT; walk like text.c Docisutf8(). */
static struct Document *Bodyfinddocument(struct Body *bd)
{  void *walk;
   struct Aobject *ao;
   int guard;
   walk=(void *)bd;
   guard=0;
   while(walk && guard<32)
   {
      ao=(struct Aobject *)walk;
      if(ao->objecttype==AOTP_DOCUMENT)
      {
         return (struct Document *)walk;
      }
      walk=(void *)Agetattr(ao,AOBJ_Layoutparent);
      guard++;
   }
   return NULL;
}

/* Gets current fontprefs */
static struct Fontprefs *Getfontprefs(struct Body *bd,struct Fontinfo *fi,USHORT *style)
{  short fonttype,fontsize;
   UBYTE *face=fi->face;
   struct Document *doc;
   fonttype=bd->bld->fonttype || fi->type;
   fontsize=fi->size;
   doc=Bodyfinddocument(bd);
   /* UTF-8 docs used to force one Matchfont face for every run so bitmap metrics matched
    * ttengine's global TTF list; without ttengine that makes about:fonts (and similar)
    * show one font for all rows. Only squash the face when ttengine can actually bind. */
   if(doc && doc->charset==DOCCHARSET_UTF8 && TTEngineAvailable())
   {  face=(UBYTE *)AWEB_UTF8_FONTFACE;
      fonttype=FALSE;
   }
   else if(doc && doc->charset==DOCCHARSET_SHIFT_JIS)
   {  face=(UBYTE *)"JKFF";
      fonttype=FALSE;
   }
   if(fi->flags&FONTF_RELSIZE)
   {  for(fi=fi->next;fi->next && (fi->flags&FONTF_RELSIZE);fi=fi->next);
      if(fi->next)
      {  fonttype=fonttype || fi->type;
         fontsize+=fi->size;
         if(style) *style|=fi->style;
      }
   }
   if(fontsize<1) fontsize=1;
   if(fontsize>7) fontsize=7;
   return Matchfont(face,fontsize-1,fonttype);
}

/* Gets current font attributes */
static void Getcurrentfont(struct Body *bd,struct Fontprefs **fp,USHORT *style,
   struct Colorinfo **ci)
{  *style=bd->bld->hardstyle|bd->bld->font.first->style;
   *fp=Getfontprefs(bd,bd->bld->font.first,style);
   *ci=bd->bld->font.first->color;
}

/*------------------------------------------------------------------------*/

/* Add a new listinfo */
static void Addlist(struct Body *bd,struct Listinfo *lis)
{  struct Listinfo *li=PALLOCSTRUCT(Listinfo,1,MEMF_CLEAR,bd->pool);
   if(li)
   {  li->type=lis->type;
      li->bullettype=lis->bullettype;
      if(li->bullettype==BDBT_IMAGE)
      {  if(lis->bulletsrc) li->bulletsrc=Dupstr(lis->bulletsrc,-1);
      }
      li->bulletnr=lis->bulletnr;
      li->horizontal=lis->horizontal;  /* Copy horizontal flag for CSS display:inline support */
      if(ISEMPTY(&bd->bld->list))
      {  li->level=1;
      }
      else
      {  li->level=bd->bld->list.first->level+1;
      }
      li->indent=li->level;
      if(li->bullettype==BDBT_DEFAULT)
      {  switch(li->type)
         {  case BDLT_UL:
               li->bullettype=(li->level-1)%6+1;
               break;
            case BDLT_OL:
               li->bullettype=BDBT_NUMBER;
               break;
         }
      }
      ADDHEAD(&bd->bld->list,li);
   }
}

/* Remove the top listinfo */
static void Remlist(struct Body *bd)
{  struct Listinfo *li=REMHEAD(&bd->bld->list);
   if(li)
   {  if(li->bulletsrc) FREE(li->bulletsrc);
      FREE(li);
   }
}

/* Set or unset <DT> in top <DL> list */
static void Setdterm(struct Body *bd,BOOL set)
{  struct Listinfo *li=bd->bld->list.first;
   if(li->next && li->type==BDLT_DL)
   {  if(set)
      {  li->indent=li->level-1;
      }
      else
      {  li->indent=li->level;
      }
   }
}

/*------------------------------------------------------------------------*/

/* Dispose the Bodybuild */
static void Disposebodybuild(struct Body *bd)
{  struct Fontinfo *fi;
   if(bd->bld)
   {  while(fi=REMHEAD(&bd->bld->font))
      {  if(fi->facestring) FREE(fi->facestring);
         FREE(fi);
      }
      while(!ISEMPTY(&bd->bld->list)) Remlist(bd);
      FREE(bd->bld);
      bd->bld=NULL;
   }
}

/* Create a Bodybuild */
static BOOL Newbodybuild(struct Body *bd)
{  if(bd->bld) Disposebodybuild(bd);
   if(bd->bld=PALLOCSTRUCT(Bodybuild,1,MEMF_CLEAR,bd->pool))
   {  NEWLIST(&bd->bld->font);
      NEWLIST(&bd->bld->list);
   }
   return BOOLVAL(bd->bld);
}

/*------------------------------------------------------------------------*/

/* Add a line index. */
static struct Line *Addline(struct Body *bd,struct Element *child,long y,long w,
   BOOL more,BOOL margin)
{  struct Line *line;
   if(line=PALLOCSTRUCT(Line,1,MEMF_CLEAR,bd->pool))
   {  line->y=y;
      line->w=w;
      line->child=child;
      if(more) line->flags|=LINEF_MORE;
      if(margin) line->flags|=LINEF_MARGIN;
      ADDTAIL(&bd->lines,line);
   }
   return line;
}

/* Remove all lines below this y. Remove all lines for this y except the last one. */
static void Removelinesbelow(struct Body *bd,long y)
{  struct Line *line,*prev;
   for(line=bd->lines.last;line->prev && line->y>y;line=prev)
   {  prev=line->prev;
      REMOVE(line);
      FREE(line);
   }
   for(;line->prev && line->y==y;line=prev)
   {  prev=line->prev;
      if(prev->prev && prev->y==y)
      {  REMOVE(line);
         FREE(line);
      }
   }
}

/* Remove all lines from behind with one of these flags set */
static void Removelinesflags(struct Body *bd,USHORT flags)
{  struct Line *line,*prev;
   for(line=bd->lines.last;line->prev && (line->flags&flags);line=prev)
   {  prev=line->prev;
      REMOVE(line);
      FREE(line);
   }
}

/* Find first child from last line, or first child if there are no lines. */
static struct Element *Findchild(struct Body *bd)
{  if(bd->lines.last->prev) return bd->lines.last->child;
   else return bd->contents.first;
}

/* Remember the first changed child. */
static void Setchchild(struct Body *bd,struct Element *child)
{  struct Element *ch;
   if(bd->chchild)
   {  for(ch=bd->chchild->prev;ch->prev;ch=ch->prev)
      {  if(ch==child)
         {  bd->chchild=child;
            break;
         }
      }
   }
   else
   {  bd->chchild=child;
   }
}

/* Find last line before this Y, with these flags unset.
 * Start from end because for long bodies it's faster and for shorter bodies
 * it doesn't differ much. */
static struct Line *Findlinebefore(struct Body *bd,long y,USHORT flags)
{  struct Line *line;
   for(line=bd->lines.last;line->prev;line=line->prev)
   {  if(line->y<=y && !(line->flags&flags)) return line;
   }
   return NULL;
}

/* Find the maximum width of all existing lines */
static long Lineswidth(struct Body *bd)
{  struct Line *line;
   long w=0;
   for(line=bd->lines.first;line->next;line=line->next)
   {  if(line->w>w) w=line->w;
   }
   return w;
}

/*------------------------------------------------------------------------*/

/* Add a margin. */
static void Addmargin(struct Body *bd,long y,long eaten,short indent,USHORT side)
{  struct Margin *newm=PALLOCSTRUCT(Margin,1,MEMF_CLEAR,bd->pool);
   struct Margin *m,*nextm;
   LIST(Margin) *list;
   if(side==MARGIN_LEFT) list=&bd->leftmargins;
   else list=&bd->rightmargins;
   if(newm)
   {  /* Compute new margins starting from current */
      newm->eaten=eaten;
      m=list->first;
      if(m->next) newm->eaten+=m->eaten;
      newm->y=y;
      newm->indent=indent;
      /* Insert new margin in list sorted on ending y position.
       * Remove earlier ending margins on this side. */
      for(m=list->first;m->next;m=nextm)
      {  nextm=m->next;
         if(m->y>newm->y) break;
         REMOVE(m);
         FREE(m);
      }
      INSERT(list,newm,m->prev);
   }
}

/* Find the current margins. Remove any earlier ended margins. */
static void Currentmargin(struct Body *bd,long y,long *left,long *right)
{  struct Margin *m,*nextm;
   *left=*right=0;
   for(m=bd->leftmargins.first;m->next;m=nextm)
   {  nextm=m->next;
      if(m->y<=y)
      {  REMOVE(m);
         FREE(m);
      }
      else
      {  *left=m->eaten;
         break;
      }
   }
   for(m=bd->rightmargins.first;m->next;m=nextm)
   {  nextm=m->next;
      if(m->y<=y)
      {  REMOVE(m);
         FREE(m);
      }
      else
      {  *right=m->eaten;
         break;
      }
   }
}

/* Find the y position where eaten is not more than a given value */
static long Findmargin(struct Body *bd,long y,long eaten)
{  struct Margin *lm,*rm;
   long leaten,reaten,fy=y;
   /* Balance both lists */
   lm=bd->leftmargins.first;
   rm=bd->rightmargins.first;
   while(lm->next || rm->next)
   {  leaten=(lm->next)?lm->eaten:0;
      reaten=(rm->next)?rm->eaten:0;
      if(leaten+reaten<=eaten) break;
      /* Set fy to where the first one (left or right) ends */
      if(lm->next)
      {  fy=lm->y;
         if(rm->next) fy=MIN(fy,rm->y);
      }
      else if(rm->next) fy=rm->y;
      /* Advance list that ends on this fy (could be both) */
      if(lm->next && lm->y==fy) lm=lm->next;
      if(rm->next && rm->y==fy) rm=rm->next;
   }
   return MAX(y,fy);
}

/* Find the y position where margin is clear */
static long Findclearmargin(struct Body *bd,long y,USHORT side)
{  if((side&MARGIN_LEFT) && !ISEMPTY(&bd->leftmargins))
   {  y=MAX(y,bd->leftmargins.last->y);
   }
   if((side&MARGIN_RIGHT) && !ISEMPTY(&bd->rightmargins))
   {  y=MAX(y,bd->rightmargins.last->y);
   }
   return y;
}

/* Find the y position where no left margins for this or higher indent levels exist */
static long Findindentmargin(struct Body *bd,long y,short indent)
{  struct Margin *m;
   for(m=bd->leftmargins.first;m->next;m=m->next)
   {  if(m->indent<indent) break;
      if(m->y>y) y=m->y;
   }
   return y;
}

/*------------------------------------------------------------------------*/

static long Measurebody(struct Body *bd,struct Ammeasure *amm)
{  struct Element *child,*ch;
   struct Ammresult ammr;
   long w=0,totalw=0,totalminw=0,addwidth=0,indent,halign,left,right,minw,width;
   ULONG flags;
   BOOL isHidden;
   
   /* Check if body should be hidden (display: none) */
   isHidden = FALSE;
   if(bd->display && Stricmp((char *)bd->display, "none") == 0)
   {  isHidden = TRUE;
   }
   
   if(isHidden)
   {  /* Don't measure if display: none */
      if(amm->ammr)
      {  amm->ammr->width = 0;
         amm->ammr->minwidth = 0;
         amm->ammr->newline = TRUE;
      }
      return 0;
   }
   
   ammr.newline=TRUE;
   /* If AMMF_CHANGED, do measure only if we have a changed child */
   if(amm->flags&AMMF_CHANGED)
   {  if(!(bd->flags&BDYF_CHANGEDCHILD)) return 0;
      child=Findchild(bd);
   }
   else
   {  child=bd->contents.first;
   }
   for(;child->next;child=child->next)
   {  if(ammr.newline)
      {  /* Get left and right indent levels (valid for entire line)
          * and if it is a bullet (valid for first child only).
          * First find next visible child and start measuring from there. */
         for(ch=child;ch->next && !Agetattr(ch,AOELT_Visible);ch=ch->next);
         if(!ch->next) ch=child;
         Agetattrs(ch,
            AOELT_Halign,&halign,
            AOELT_Leftindent,&left,
            AOELT_Rightindent,&right,
            TAG_END);
         indent=INDENT(left)+INDENT(right);
         w=indent;
         addwidth=0;
         child=ch;
      }
      else halign=0;
      ammr.width=ammr.minwidth=ammr.addwidth=0;
      ammr.newline=FALSE;
      flags=amm->flags;
      if(child==bd->chchild) flags|=AMMF_CHANGED;
      Ameasure(child,amm->width-2*bd->hmargin,amm->height-2*bd->vmargin,
         addwidth,flags,amm->text,&ammr);
      if(halign&HALIGN_BULLET)
      {  width=MAX(0,ammr.width-indent);
         minw=MAX(0,ammr.minwidth-indent)+indent;
      }
      else
      {  width=ammr.width;
         minw=ammr.minwidth+indent;
      }
      w+=width;
      if(w>totalw) totalw=w;
      if(minw>totalminw) totalminw=minw;
      addwidth=ammr.addwidth;
   }
   if(amm->ammr)
   {  amm->ammr->width=totalw+2*bd->hmargin;
      {  amm->ammr->minwidth=totalminw+2*bd->hmargin;
      }
      
      /* Apply min-width constraint */
      if(bd->minwidth >= 0 && amm->ammr->width < bd->minwidth)
      {  amm->ammr->width = bd->minwidth;
      }
      if(bd->minwidth >= 0 && amm->ammr->minwidth < bd->minwidth)
      {  amm->ammr->minwidth = bd->minwidth;
      }
      
      /* Apply max-width constraint */
      if(bd->maxwidth >= 0 && amm->ammr->width > bd->maxwidth)
      {  amm->ammr->width = bd->maxwidth;
      }
      if(bd->maxwidth >= 0 && amm->ammr->minwidth > bd->maxwidth)
      {  amm->ammr->minwidth = bd->maxwidth;
      }
      
      amm->ammr->newline=TRUE;
   }
   return 0;
}

static long Layoutbody(struct Body *bd,struct Amlayout *amlp)
{  struct Element *child,*ch,*endch,*nextchild,*floatchild;
   struct Amlayout aml=*amlp;
   struct Amlresult amlr;
   struct Amalign ama;
   struct Line *line;
   long y,newy,endx;
   long above,below,toph,bottomh;
   long halign,left,right,need;
   long margleft,margright;
   long result=0;
   USHORT nextflags=0,clrmargin=0;
   BOOL align,wasmore,bullet,first=TRUE,softnl=FALSE;
   BOOL isHidden;
   BOOL needsClear;
   
   /* Check if body should be hidden (display: none) */
   isHidden = FALSE;
   if(bd->display && Stricmp((char *)bd->display, "none") == 0)
   {  isHidden = TRUE;
   }
   
   if(isHidden)
   {  /* Don't layout if display: none */
      bd->aow = 0;
      bd->aoh = 0;
      return 0;
   }
   
   /* Check if we need to clear floats */
   needsClear = FALSE;
   if(bd->clear)
   {  if(Stricmp((char *)bd->clear, "both") == 0)
      {  needsClear = TRUE;
      }
      else if(Stricmp((char *)bd->clear, "left") == 0)
      {  /* Would need to check if left margins exist */
         needsClear = TRUE;
      }
      else if(Stricmp((char *)bd->clear, "right") == 0)
      {  /* Would need to check if right margins exist */
         needsClear = TRUE;
      }
   }
   
   /* Apply position: fixed/absolute/relative positioning */
   /* Note: right/bottom calculation happens after body dimensions are known */
   /* For percentage-based positioning, we need to wait until we know parent dimensions */
   /* So we only set pixel-based positions here, percentages are handled later */
   if(bd->position && (Stricmp((char *)bd->position, "fixed") == 0 || 
                       Stricmp((char *)bd->position, "absolute") == 0 ||
                       Stricmp((char *)bd->position, "relative") == 0))
   {  long leftPos, topPos;
      
      /* Get left/top positions (only for pixel values, not percentages) */
      leftPos = Agetattr((void *)bd, AOBJ_Left);
      topPos = Agetattr((void *)bd, AOBJ_Top);
      
      /* Only apply pixel-based positioning here - percentages are handled after layout */
      /* Check if we have percentage values - if so, skip initial positioning */
      if(bd->leftpercent < 0 && bd->toppercent < 0)
      {  /* No percentage values, use pixel positioning */
         if(Stricmp((char *)bd->position, "relative") == 0)
         {  /* Relative positioning: offset from normal flow position */
            /* Normal flow position starts at 0, add offset */
            bd->aox = leftPos;
            bd->aoy = topPos;
         }
         else
         {  /* Fixed/absolute positioning: set absolute position */
            /* If right/bottom are set, they'll be recalculated after we know dimensions */
            bd->aox = leftPos;
            bd->aoy = topPos;
         }
      }
      else
      {  /* Percentage values present - initialize to 0, will be calculated after layout */
         bd->aox = 0;
         bd->aoy = 0;
      }
   }
   else
   {  bd->aox=bd->aoy=0;
   }
   /* If AMLF_CHANGED, do layout only if we have a changed child */
   if((amlp->flags&AMLF_CHANGED) && !(bd->flags&BDYF_CHANGEDCHILD)) return 0;
   /* If AMLF_CHANGED, remove all lines from the end with MORE or MARGIN set,
    * else remove all lines so we start from top. */
   if(amlp->flags&AMLF_CHANGED)
   {  Removelinesflags(bd,LINEF_MORE|LINEF_MARGIN);
   }
   else
   {  Removelinesbelow(bd,-1);
   }
   /* If there is a last line, use its Y as starting Y. Remove the line, a new
    * one will be added in the process. */
   if(bd->lines.last->prev)
   {  line=bd->lines.last;
      y=bd->rendery=line->y;
      child=line->child;
      /* If a FITHEIGHT layout is requested, check if we are not already too high */
      if((amlp->flags&AMLF_FITHEIGHT) && y>amlp->height)
      {  bd->aoh=y;
         return 0;
      }
      REMOVE(line);
      FREE(line);
   }
   else
   {  /* Start Y position includes top padding */
      y=bd->vmargin+bd->paddingtop;
      bd->rendery=0;
      child=bd->contents.first;
   }
   /* Handle margin: auto for horizontal centering BEFORE layout */
   /* Use max-width if available, otherwise will be calculated after layout */
   if(bd->marginleftauto && bd->marginrightauto)
   {  long parentWidth;
      void *parentObj;
      long contentWidth;
      
      /* Get parent width for centering calculation */
      parentWidth = amlp->width;
      parentObj = (void *)Agetattr((struct Aobject *)bd, AOBJ_Layoutparent);
      if(parentObj)
      {  long pw;
         Agetattrs(parentObj, AOBJ_Width, &pw, TAG_END);
         if(pw > 0) parentWidth = pw;
      }
      
      /* Use max-width if set, otherwise will use actual content width after layout */
      if(bd->maxwidth > 0)
      {  contentWidth = bd->maxwidth;
      }
      else
      {  /* Will calculate after we know actual width */
         contentWidth = 0;
      }
      
      /* Calculate equal left and right margins to center the body */
      if(contentWidth > 0 && parentWidth > contentWidth)
      {  long calculatedMargin;
         calculatedMargin = (parentWidth - contentWidth) / 2;
         bd->hmargin = calculatedMargin;
      }
      /* If max-width not set, we'll calculate margin after layout (see below) */
   }
   
   aml.amlr=&amlr;
   bd->aow=Lineswidth(bd);
   /* if floatchild is nonnull, set AMLF_RETRY until this child has been layed out. */
   floatchild=NULL;
   for(ch=child;ch->next;ch=ch->next)
   {  Asetattrs(ch,AOELT_Resetlayout,TRUE,TAG_END);
   }
   while(child && child->next)
   {  /* New line. Get horizontal align and indents from first visible child. */
      for(ch=child;ch->next && !Agetattr(ch,AOELT_Visible);ch=ch->next);
      if(!ch->next) ch=child;
      Agetattrs(ch,
         AOELT_Halign,&halign,
         AOELT_Leftindent,&left,
         AOELT_Rightindent,&right,
         TAG_END);
      bullet=BOOLVAL(halign&HALIGN_BULLET);
      above=below=toph=bottomh=0;
      if(bullet)
      {  /* Find correct margin for this bullet.
          * Don't mess with Y if it is a retry of a bullet, bullet is already correctly
          * placed then. */
         if(!(nextflags&AMLF_RETRY))
         {  y=Findindentmargin(bd,y,left);
         }
      }
      else
      {  /* Find correct margin for this level of text. That is, find a place
          * where all left margins of higher levels are clear. */
         y=Findindentmargin(bd,y,left+1);
      }
      Currentmargin(bd,y,&margleft,&margright);
      /* Apply padding and margin-right to content area */
      aml.startx=bd->hmargin+margleft+INDENT(left)+bd->paddingleft;
      /* Account for margin-right in available width calculation */
      aml.width=amlp->width-bd->hmargin-margright-INDENT(right)-bd->paddingleft-bd->paddingright;
      if(bd->marginright != 0)
      {  aml.width -= bd->marginright;
      }
      aml.flags=nextflags;
      wasmore=BOOLVAL(aml.flags&AMLF_MORE);
      endx=0;
      if(bullet)
      {  /* Place bullet right-aligned with real startx minus 8 pixels.
          * Note: (ch) is first visible child */
         aml.startx=MAX(bd->hmargin+margleft,aml.startx-8-ch->aow);
      }
      aml.flags|=AMLF_FIRST;
      /* (child) is first element on line, (ch) is running element */
      for(ch=child;ch->next;ch=ch->next)
      {  memset(&amlr,0,sizeof(amlr));
         if((amlp->flags&AMLF_CHANGED) && ch==bd->chchild) aml.flags|=AMLF_CHANGED;
         if(floatchild) aml.flags|=AMLF_RETRY;
         if(amlp->flags&AMLF_INTABLE) aml.flags|=AMLF_INTABLE;
         AmethodA(ch,&aml);
#ifdef BETAVERSION
if(SetSignal(0,0)&SIGBREAKF_CTRL_C) return 0;
#endif
         if(softnl && amlr.result==AMLR_NEWLINE && amlr.endx==aml.startx)
         {  /* Hard line break immediately after a soft newline, ignore */
            amlr.result=AMLR_OK;
         }
         else if(amlr.result&AMLRF_OK)
         {  above=MAX(above,amlr.above);
            below=MAX(below,amlr.below);
            toph=MAX(toph,amlr.toph);
            bottomh=MAX(bottomh,amlr.bottomh);
            endx=amlr.endx;
         }
         if(ch==floatchild)
         {  aml.flags&=~AMLF_RETRY;
            floatchild=NULL;
         }
         if(amlr.result!=AMLR_FBREAK)
         {  if(amlr.result!=AMLR_OK) break;
            if(amlr.endx>aml.width) break;
         }
         if(amlr.endx>aml.startx)
         {  aml.startx=amlr.endx;
            aml.flags&=~AMLF_FIRST;
         }
         aml.flags&=~(AMLF_MORE);
         softnl=FALSE;
         if(bullet && Agetattr(ch,AOELT_Visible))
         {  /* Restore startx after bullet, but allow long bullets */
            aml.startx=MAX(aml.startx,bd->hmargin+margleft+INDENT(left));
            bullet=FALSE;
         }
      }
      while(amlr.result==AMLR_BREAK && ch!=child)
      {  ch=ch->prev;
         aml.flags=AMLF_BREAK;
         AmethodA(ch,&aml);
         endx=amlr.endx;
      }
      switch(amlr.result)
      {  case AMLR_OK:
         case AMLR_FBREAK:
            /* (ch) fits because it's last before BREAK, or (ch) is past the end. */
            nextflags=0;
            if(ch->next)
            {  nextchild=ch->next;
               endch=ch->next;
            }
            else
            {  nextchild=ch;
               endch=ch;
            }
            align=TRUE;
            softnl=TRUE;
            break;
         case AMLR_NEWLINE:
            /* (ch) fits because it's last before newline */
            nextflags=0;
            nextchild=ch->next;
            endch=ch->next;
            align=TRUE;
            break;
         case AMLR_NLCLRLEFT:
         case AMLR_NLCLRRIGHT:
         case AMLR_NLCLRALL:
            /* (ch) fits because it's last before newline */
            nextflags=0;
            nextchild=ch->next;
            endch=ch->next;
            align=TRUE;
            if(amlr.result&AMLRF_CLRLEFT) clrmargin|=MARGIN_LEFT;
            if(amlr.result&AMLRF_CLRRIGHT) clrmargin|=MARGIN_RIGHT;
            break;
         case AMLR_MORE:
            /* (ch) fits but wants another try */
            nextflags=AMLF_MORE;
            nextchild=ch;
            endch=ch->next;
            align=TRUE;
            break;
         case AMLR_NOFIT:
         case AMLR_BREAK:
            /* (ch) doesn't fit.
             * If it is the first one, search for space and try again. If no space
             * found, try again forced.
             * If (ch) is not the first one, break before this one.
             * Set MORE flag just in case */
            if(ch==child)
            {  /* we need: amlr.endx - original startx + indentations.
                * (let compiler optimize -&+ INDENT(left)) */
               need=amlr.endx-(bd->hmargin+margleft+INDENT(left))
                  +INDENT(left)+INDENT(right)+2*bd->hmargin;
               newy=Findmargin(bd,y,amlp->width-need);
               if(newy>y) nextflags=0;
               else nextflags=AMLF_FORCE;
               y=newy;
               nextflags|=AMLF_MORE;
               nextchild=child;
               align=FALSE;
            }
            else
            {  nextflags=AMLF_MORE;
               nextchild=ch;
               endch=ch;
               align=TRUE;
               softnl=TRUE;
            }
            break;
         case AMLR_FLOATING:
            /* Align (ch) element now, and RETRY the others from start. Add a
             * new margin. */
            halign=Agetattr(ch,AOELT_Halign)&HALIGN_FLOATRIGHT;
            ama.method=AOM_ALIGN;
            if(halign==HALIGN_FLOATRIGHT)
            {  ama.dx=MAX(amlp->width-bd->hmargin-margright-INDENT(right)-ch->aox-ch->aow,
                  -ch->aox);
            }
            else
            {  ama.dx=bd->hmargin+margleft+INDENT(left)-ch->aox;
            }
            ama.y=y;
            AmethodA(ch,&ama);
            bd->aow=MAX(bd->aow,ch->aox+ch->aow+bd->hmargin);
            nextflags=AMLF_RETRY|AMLF_MORE;
            nextchild=child;
            align=FALSE;
            floatchild=ch;
            Addmargin(bd,ch->aoy+ch->aoh,ch->aow,left,
               (halign==HALIGN_FLOATRIGHT)?MARGIN_RIGHT:MARGIN_LEFT);
            break;
      }
      if(align)
      {  bottomh=MAX(bottomh,above+below);
         ama.method=AOM_ALIGN;
         switch(halign&0x0f)
         {  case HALIGN_LEFT:
               ama.dx=0;
               break;
            case HALIGN_CENTER:
               if(endx<aml.width) ama.dx=(aml.width-endx)/2;
               else ama.dx=0;
               break;
            case HALIGN_RIGHT:
               if(endx<aml.width) ama.dx=aml.width-endx;
               else ama.dx=0;
               break;
         }
         ama.y=y;
         ama.baseline=bottomh-below-1;
         ama.height=MAX(toph,bottomh);
         for(ch=child;ch!=endch;ch=ch->next)
         {  AmethodA(ch,&ama);
         }
         bd->aow=MAX(bd->aow,endx+bd->hmargin+ama.dx);
         /* Add a new line index */
         Addline(bd,child,y,endx+bd->hmargin,
            wasmore,!(ISEMPTY(&bd->leftmargins) && ISEMPTY(&bd->rightmargins)));
         /* Remember space above the baseline for first line */
         if(first)
         {  if(amlp->amlr)
            {  amlp->amlr->above=above;
            }
            first=FALSE;
         }
         /* Apply line-height multiplier if specified */
         if(bd->lineheight > 0.0)
         {  /* Multiply line height by line-height value (e.g., 1.6 means 60% more spacing) */
            y += (long)(ama.height * bd->lineheight);
         }
         else
         {  y += ama.height;
         }
         if(clrmargin)
         {  y=Findclearmargin(bd,y,clrmargin);
            clrmargin=0;
         }
         if((amlp->flags&AMLF_FITHEIGHT) && y>amlp->height)
         {  bd->aoh=y;
            Removelinesbelow(bd,bd->rendery);
            result=AMLR_FHAGAIN;
            break;
         }
      }
      child=nextchild;
   }
   /* Include all pending floating objects in body height */
   /* Add padding to body height */
   bd->aoh=Findmargin(bd,y,0)+bd->vmargin+bd->paddingtop+bd->paddingbottom;
   
   /* Apply min-height constraint */
   if(bd->minheight >= 0 && bd->aoh < bd->minheight)
   {  bd->aoh = bd->minheight;
   }
   
   /* Apply max-height constraint */
   if(bd->maxheight >= 0 && bd->aoh > bd->maxheight)
   {  bd->aoh = bd->maxheight;
   }
   
   /* Handle margin: auto for horizontal centering AFTER layout */
   /* If max-width wasn't set, calculate margin based on actual content width */
   if(bd->marginleftauto && bd->marginrightauto && bd->maxwidth <= 0)
   {  /* Both left and right margins are auto - center the body horizontally */
      long parentWidth;
      void *parentObj;
      long calculatedMargin;
      
      /* Get parent width for centering calculation */
      parentWidth = amlp->width;
      parentObj = (void *)Agetattr((struct Aobject *)bd, AOBJ_Layoutparent);
      if(parentObj)
      {  long pw;
         Agetattrs(parentObj, AOBJ_Width, &pw, TAG_END);
         if(pw > 0) parentWidth = pw;
      }
      
      /* Calculate equal left and right margins to center the body */
      /* Use actual content width now that we know it */
      if(bd->aow > 0 && parentWidth > bd->aow)
      {  calculatedMargin = (parentWidth - bd->aow) / 2;
         bd->hmargin = calculatedMargin;
         /* Note: This will require a re-layout to take effect properly,
          * but for now we set it for the next layout pass */
      }
   }
   else if(bd->marginleftauto)
   {  /* Only left margin is auto - this is less common, treat as 0 for now */
      bd->hmargin = 0;
   }
   else if(bd->marginrightauto)
   {  /* Only right margin is auto - this is less common, treat as 0 for now */
      /* Right margin is implicit, so we don't need to set it explicitly */
   }
   
   /* Recalculate right/bottom positioning now that we know body dimensions */
   if(bd->position && (Stricmp((char *)bd->position, "fixed") == 0 || 
                       Stricmp((char *)bd->position, "absolute") == 0))
   {  long parentWidth, parentHeight;
      void *parentObj;
      BOOL isFixed;
      
      /* Check if this is fixed positioning */
      isFixed = (bd->position && Stricmp((char *)bd->position, "fixed") == 0);
      
      if(isFixed)
      {  /* For fixed positioning, use viewport (window inner) dimensions */
         void *winObj;
         void *frameObj;
         
         /* Try to get window object from frame or directly */
         frameObj = bd->frame;
         if(!frameObj && bd->cframe)
         {  frameObj = bd->cframe;
         }
         
         if(frameObj)
         {  /* Get window from frame */
            winObj = (void *)Agetattr(frameObj, AOBJ_Window);
         }
         else
         {  /* Try to get window directly from body */
            winObj = bd->win;
         }
         
         if(winObj)
         {  /* Get viewport dimensions from window */
            long vw, vh;
            Agetattrs(winObj, AOWIN_Innerwidth, &vw, AOWIN_Innerheight, &vh, TAG_END);
            if(vw > 0) parentWidth = vw;
            if(vh > 0) parentHeight = vh;
         }
         else
         {  /* Fallback to layout dimensions if window not available */
            parentWidth = amlp->width;
            parentHeight = amlp->height;
         }
      }
      else
      {  /* For absolute positioning, use parent container dimensions */
         /* Get parent dimensions for right/bottom calculation */
         parentWidth = amlp->width;
         parentHeight = amlp->height;
         
         /* Try to get parent object dimensions if available */
         parentObj = (void *)Agetattr((struct Aobject *)bd, AOBJ_Layoutparent);
         if(parentObj)
         {  long pw, ph;
            Agetattrs(parentObj, AOBJ_Width, &pw, AOBJ_Height, &ph, TAG_END);
            if(pw > 0) parentWidth = pw;
            if(ph > 0) parentHeight = ph;
         }
      }
      
      /* Recalculate position if right/bottom are set */
      if(bd->right >= 0 && bd->aow > 0)
      {  /* right is set - recalculate left from right */
         long leftPos;
         leftPos = parentWidth - bd->aow - bd->right;
         bd->aox = leftPos;
      }
      
      if(bd->bottom >= 0 && bd->aoh > 0)
      {  /* bottom is set - recalculate top from bottom */
         long topPos;
         topPos = parentHeight - bd->aoh - bd->bottom;
         bd->aoy = topPos;
      }
      
      /* Apply percentage-based top/left positioning */
      /* This must come before transform so transform can adjust from the percentage position */
      /* Percentage positioning takes precedence over pixel positioning */
      if(bd->toppercent >= 0)
      {  /* Calculate top position from percentage */
         long topPos;
         topPos = (parentHeight * bd->toppercent) / 10000;
         bd->aoy = topPos;
      }
      else
      {  /* No percentage, check if pixel value was set earlier */
         /* Pixel values were already set in initial positioning, so no change needed */
      }
      
      if(bd->leftpercent >= 0)
      {  /* Calculate left position from percentage */
         long leftPos;
         leftPos = (parentWidth * bd->leftpercent) / 10000;
         bd->aox = leftPos;
      }
      else
      {  /* No percentage, check if pixel value was set earlier */
         /* Pixel values were already set in initial positioning, so no change needed */
      }
      
      /* Apply transform: translate() adjustments */
      if(bd->transform)
      {  UBYTE *transformStr;
         UBYTE *p;
         long translateX, translateY;
         struct Number numX, numY;
         BOOL foundTranslate;
         
         transformStr = bd->transform;
         p = transformStr;
         foundTranslate = FALSE;
         
         /* Parse transform: translate(x, y) or translateX(x) or translateY(y) */
         /* Skip whitespace */
         while(*p && isspace(*p)) p++;
         
         /* Check for translate( */
         if(Strnicmp((char *)p, "translate", 9) == 0)
         {  p += 9;
            while(*p && isspace(*p)) p++;
            if(*p == '(')
            {  p++;  /* Skip '(' */
               while(*p && isspace(*p)) p++;
               
               /* Parse first value (X) */
               translateX = ParseCSSLengthValue(p, &numX);
               /* Skip past the parsed value */
               while(*p && *p != ',' && *p != ')' && !isspace(*p)) p++;
               while(*p && isspace(*p)) p++;
               if(*p == ',')
               {  p++;  /* Skip ',' */
                  while(*p && isspace(*p)) p++;
                  /* Parse second value (Y) */
                  translateY = ParseCSSLengthValue(p, &numY);
                  /* Skip past the parsed value */
                  while(*p && *p != ')' && !isspace(*p)) p++;
               }
               else
               {  /* Only one value - use for both X and Y */
                  translateY = translateX;
                  numY = numX;
               }
               
               /* Apply translate adjustments */
               /* For percentages, calculate based on element dimensions */
               /* ParseCSSLengthValue returns 0-100 for percentages, convert to 0-10000 scale */
               if(numX.type == NUMBER_PERCENT)
               {  long offsetX;
                  offsetX = (bd->aow * translateX * 100) / 10000;
                  bd->aox += offsetX;
               }
               else if(numX.type == NUMBER_NUMBER || numX.type == NUMBER_SIGNED)
               {  /* Pixel-based translation */
                  bd->aox += translateX;
               }
               
               if(numY.type == NUMBER_PERCENT)
               {  long offsetY;
                  offsetY = (bd->aoh * translateY * 100) / 10000;
                  bd->aoy += offsetY;
               }
               else if(numY.type == NUMBER_NUMBER || numY.type == NUMBER_SIGNED)
               {  /* Pixel-based translation */
                  bd->aoy += translateY;
               }
               
               foundTranslate = TRUE;
            }
         }
         else if(Strnicmp((char *)p, "translateX", 10) == 0)
         {  p += 10;
            while(*p && isspace(*p)) p++;
            if(*p == '(')
            {  p++;  /* Skip '(' */
               while(*p && isspace(*p)) p++;
               translateX = ParseCSSLengthValue(p, &numX);
               /* Skip past the parsed value */
               while(*p && *p != ')' && !isspace(*p)) p++;
               if(numX.type == NUMBER_PERCENT)
               {  long offsetX;
                  /* ParseCSSLengthValue returns 0-100 for percentages, convert to 0-10000 scale */
                  offsetX = (bd->aow * translateX * 100) / 10000;
                  bd->aox += offsetX;
               }
               else if(numX.type == NUMBER_NUMBER || numX.type == NUMBER_SIGNED)
               {  bd->aox += translateX;
               }
               foundTranslate = TRUE;
            }
         }
         else if(Strnicmp((char *)p, "translateY", 10) == 0)
         {  p += 10;
            while(*p && isspace(*p)) p++;
            if(*p == '(')
            {  p++;  /* Skip '(' */
               while(*p && isspace(*p)) p++;
               translateY = ParseCSSLengthValue(p, &numY);
               /* Skip past the parsed value */
               while(*p && *p != ')' && !isspace(*p)) p++;
               if(numY.type == NUMBER_PERCENT)
               {  long offsetY;
                  /* ParseCSSLengthValue returns 0-100 for percentages, convert to 0-10000 scale */
                  offsetY = (bd->aoh * translateY * 100) / 10000;
                  bd->aoy += offsetY;
               }
               else if(numY.type == NUMBER_NUMBER || numY.type == NUMBER_SIGNED)
               {  bd->aoy += translateY;
               }
               foundTranslate = TRUE;
            }
         }
      }
   }
   
   /* Apply margin-bottom (affects spacing after body) */
   if(bd->marginbottom != 0)
   {  /* Add margin-bottom to body height to create spacing after the body */
      bd->aoh += bd->marginbottom;
   }
   
   /* Clear all pending margins, as MORE starts before the margins anyway. */
   Currentmargin(bd,bd->aoh,&margleft,&margright);
   
   /* If clear property is set, clear the appropriate margins */
   if(needsClear)
   {  /* Clear left margins if clear: left or both */
      if(bd->clear && (Stricmp((char *)bd->clear, "left") == 0 || 
                       Stricmp((char *)bd->clear, "both") == 0))
      {  /* Find highest left margin and clear below it */
         struct Margin *m;
         long highestY = 0;
         for(m = bd->leftmargins.first; m->next; m = m->next)
         {  if(m->y > highestY) highestY = m->y;
         }
         if(highestY > 0 && bd->aoh < highestY)
         {  bd->aoh = highestY;
         }
      }
      /* Clear right margins if clear: right or both */
      if(bd->clear && (Stricmp((char *)bd->clear, "right") == 0 || 
                       Stricmp((char *)bd->clear, "both") == 0))
      {  /* Find highest right margin and clear below it */
         struct Margin *m;
         long highestY = 0;
         for(m = bd->rightmargins.first; m->next; m = m->next)
         {  if(m->y > highestY) highestY = m->y;
         }
         if(highestY > 0 && bd->aoh < highestY)
         {  bd->aoh = highestY;
         }
      }
   }
   
   bd->flags|=BDYF_LAYOUTREADY;
   return result;
}

static long Renderbody(struct Body *bd,struct Amrender *amr)
{  struct Coords *coo;
   struct Element *child;
   struct Line *line;
   USHORT flags=amr->flags&~AMRF_CLEAR;
   short bgcolor;
   void *bgimage;
   long y;
   BOOL isHidden;
   long clipMinX, clipMinY, clipMaxX, clipMaxY;
   
   /* Check if body should be hidden (display: none) */
   isHidden = FALSE;
   if(bd->display && Stricmp((char *)bd->display, "none") == 0)
   {  isHidden = TRUE;
   }
   
   if(isHidden)
   {  /* Don't render if display: none */
      return 0;
   }
   
   /* prevent multiple rendering by putting the value of bgupdate in to bd->bgupdate */
   bd->bgupdate = bgupdate;
   /* If AMRF_CHANGED, start with first child from rendery, but only if something
    * was changed.
    * Otherwise render everything. */
   child=bd->contents.first;
   y=0;
   if(amr->flags&AMRF_CHANGED)
   {  if(!(bd->flags&BDYF_CHANGEDCHILD)) return 0;
      if(amr->flags&AMRF_CLEAR) line=Findlinebefore(bd,bd->rendery,LINEF_MARGIN);
      else line=Findlinebefore(bd,bd->rendery,0);
      if(line)
      {  child=line->child;
         y=line->y;
      }
   }
   if((coo=Clipcoords(bd,amr->coords)) && coo->rp)
   {  bgcolor=coo->bgcolor;
      bgimage=coo->bgimage;
      
      /* Apply overflow clipping if needed */
      clipMinX = amr->rect.minx;
      clipMinY = amr->rect.miny;
      clipMaxX = amr->rect.maxx;
      clipMaxY = amr->rect.maxy;
      
      if(bd->overflow && (Stricmp((char *)bd->overflow, "hidden") == 0 || 
                          Stricmp((char *)bd->overflow, "auto") == 0 ||
                          Stricmp((char *)bd->overflow, "scroll") == 0))
      {  /* Clip to body bounds */
         long bodyMinX, bodyMinY, bodyMaxX, bodyMaxY;
         bodyMinX = bd->aox;
         bodyMinY = bd->aoy;
         bodyMaxX = bd->aox + bd->aow;
         bodyMaxY = bd->aoy + bd->aoh;
         
         /* Intersect clipping region with body bounds */
         if(bodyMinX > clipMinX) clipMinX = bodyMinX;
         if(bodyMinY > clipMinY) clipMinY = bodyMinY;
         if(bodyMaxX < clipMaxX) clipMaxX = bodyMaxX;
         if(bodyMaxY < clipMaxY) clipMaxY = bodyMaxY;
      }
      
      /* Always apply background color if set, regardless of docolors setting */
      if(bd->bgcolor>=0 && !(bd->flags&BDYF_NOBACKGROUND))
      {  coo->bgcolor=bd->bgcolor;
      }
      /* Only apply background image if docolors is enabled */
      if(prefs.browser.docolors && bd->bgimage && !(bd->flags&BDYF_NOBACKGROUND))
      {  coo->bgimage=bd->bgimage;
         coo->bgalign=bd->bgalign;
      }
      else if(bd->bgcolor>=0 && !(bd->flags&BDYF_NOBACKGROUND))
      {  /* If body has background color but no background image,
           * clear parent's background image so child's color is used */
         coo->bgimage=NULL;
         coo->bgalign=NULL;
      }
      /* Fallback: apply bgcolor even when docolors is off if FORCEBGCOLOR flag is set */
      if(!prefs.browser.docolors && (bd->flags&BDYF_FORCEBGCOLOR) && bd->bgcolor>=0)
      {  coo->bgcolor=bd->bgcolor;
         /* Clear background image when forcing background color */
         coo->bgimage=NULL;
         coo->bgalign=NULL;
      }
      /* Clear background only if CLEAR requested,
       * or if CLEARBG requested and this body has a different background
       * i.e. it is a table cell body, so frame!=cframe. */
      if((amr->flags&AMRF_CLEAR)
      || ((amr->flags&AMRF_CLEARBG) && (bd->bgimage || bd->bgcolor>=0) &&
            (bd->frame!=bd->cframe) && !(bd->flags&BDYF_NOBACKGROUND)))
      {  if(amr->flags&AMRF_CHANGED)
         {  while(child->next && !Agetattr(child,AOELT_Visible)) child=child->next;
            if(child->next) y+=Agetattr(child,AOELT_Incrementaly);
         }
         Erasebg(bd->cframe,coo,clipMinX,MAX(y,clipMinY),clipMaxX,clipMaxY);
         flags|=AMRF_CLEARBG;
      }
#if 0 /* MARQUEE: scroll offset render path (struct Body marquee fields removed) */
      /* Apply marquee scroll offset if this is a marquee */
      if(bd->marqueedirection)
      {  /* Create modified coordinate structure with scroll offset */
         struct Coords marquee_coo;
         long marqueeMinX,marqueeMinY,marqueeMaxX,marqueeMaxY;
         long finalMinX,finalMinY,finalMaxX,finalMaxY;
         long childRastX,childRastY;
         /* Copy parent coordinates - they already have correct textcolor, rastport, etc. */
         marquee_coo = *coo;
         /* Always ensure textcolor is set from DrawInfo to guarantee correct pen */
         /* Parent coo should have correct textcolor, but ensure it's valid */
         if(marquee_coo.dri && (marquee_coo.textcolor <= 0 || marquee_coo.textcolor == marquee_coo.bgcolor))
         {  marquee_coo.textcolor = marquee_coo.dri->dri_Pens[TEXTPEN];
         }
         if(marquee_coo.dri && marquee_coo.linkcolor <= 0)
         {  marquee_coo.linkcolor = marquee_coo.dri->dri_Pens[TEXTPEN];
         }
         if(marquee_coo.dri && marquee_coo.vlinkcolor <= 0)
         {  marquee_coo.vlinkcolor = marquee_coo.dri->dri_Pens[TEXTPEN];
         }
         if(marquee_coo.dri && marquee_coo.alinkcolor <= 0)
         {  marquee_coo.alinkcolor = marquee_coo.dri->dri_Pens[TEXTPEN];
         }
         /* Add scroll offset to transform: children are positioned relative to body,
          * coo->dx transforms from parent-relative to rastport, bd->aox is body position
          * relative to parent, so transform from body-relative to rastport is:
          * coo->dx + bd->aox + scroll offset */
         marquee_coo.dx = coo->dx + bd->aox + bd->marqueescrollx;
         marquee_coo.dy = coo->dy + bd->aoy + bd->marqueescrolly;
         /* Clipping: marquee viewport bounds in rastport coordinates */
         /* Body's rastport position is coo->dx + bd->aox (parent->rastport + body offset) */
         marqueeMinX = coo->dx + bd->aox;
         marqueeMinY = coo->dy + bd->aoy;
         marqueeMaxX = marqueeMinX + bd->aow;
         marqueeMaxY = marqueeMinY + bd->aoh;
         /* Intersect with existing clip bounds (which are in rastport coords) */
         finalMinX = MAX(clipMinX, marqueeMinX);
         finalMinY = MAX(clipMinY, marqueeMinY);
         finalMaxX = MIN(clipMaxX, marqueeMaxX);
         finalMaxY = MIN(clipMaxY, marqueeMaxY);
         for(;child->next;child=child->next)
         {  /* Check visibility: child position with scroll offset in rastport coords */
            /* child->aox is relative to body, marquee_coo.dx includes scroll offset,
             * so child renders at child->aox + marquee_coo.dx in rastport */
            childRastX = child->aox + marquee_coo.dx;
            childRastY = child->aoy + marquee_coo.dy;
            if(childRastX<=finalMaxX && childRastX+child->aow>finalMinX 
            && childRastY<=finalMaxY && childRastY+child->aoh>finalMinY)
            {  Arender(child,&marquee_coo,finalMinX,finalMinY,finalMaxX,finalMaxY,flags,amr->text);
            }
         }
      }
      else
#endif
      {  /* Normal rendering (MARQUEE path disabled) */
         for(;child->next;child=child->next)
         {  /* Apply overflow clipping to child rendering */
            if(child->aox<=clipMaxX && child->aox+child->aow>clipMinX 
            && child->aoy<=clipMaxY && child->aoy+child->aoh>clipMinY)
            {  Arender(child,coo,clipMinX,clipMinY,clipMaxX,clipMaxY,flags,amr->text);
            }
         }
      }
      /* Render border if border width > 0 */
      if(bd->borderwidth > 0 && coo->rp)
      {  short borderPen;
         long borderX1, borderY1, borderX2, borderY2;
         
         /* Get border color pen */
         if(bd->bordercolor && bd->bordercolor->pen >= 0)
         {  borderPen = bd->bordercolor->pen;
         }
         else
         {  borderPen = coo->dri->dri_Pens[SHADOWPEN];
         }
         
         /* Calculate border rectangle */
         borderX1 = bd->aox + coo->dx;
         borderY1 = bd->aoy + coo->dy;
         borderX2 = borderX1 + bd->aow - 1;
         borderY2 = borderY1 + bd->aoh - 1;
         
         /* Only draw if within clipping region */
         if(borderX2 >= clipMinX && borderX1 <= clipMaxX &&
            borderY2 >= clipMinY && borderY1 <= clipMaxY)
         {  SetAPen(coo->rp, borderPen);
            
            /* Draw top border */
            if(borderY1 >= clipMinY && borderY1 <= clipMaxY)
            {  Move(coo->rp, MAX(borderX1, clipMinX), borderY1);
               Draw(coo->rp, MIN(borderX2, clipMaxX), borderY1);
            }
            
            /* Draw bottom border */
            if(borderY2 >= clipMinY && borderY2 <= clipMaxY)
            {  Move(coo->rp, MAX(borderX1, clipMinX), borderY2);
               Draw(coo->rp, MIN(borderX2, clipMaxX), borderY2);
            }
            
            /* Draw left border */
            if(borderX1 >= clipMinX && borderX1 <= clipMaxX)
            {  Move(coo->rp, borderX1, MAX(borderY1, clipMinY));
               Draw(coo->rp, borderX1, MIN(borderY2, clipMaxY));
            }
            
            /* Draw right border */
            if(borderX2 >= clipMinX && borderX2 <= clipMaxX)
            {  Move(coo->rp, borderX2, MAX(borderY1, clipMinY));
               Draw(coo->rp, borderX2, MIN(borderY2, clipMaxY));
         }
      }
      }
      
      coo->bgcolor=bgcolor;
      coo->bgimage=bgimage;
      Unclipcoords(coo);
      if(bd->flags&BDYF_LAYOUTREADY)
      {  bd->flags&=~BDYF_CHANGEDCHILD;
      }
      bd->chchild=NULL;
   }
   return 0;
}

static long Setbody(struct Body *bd,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   USHORT fontw=0;
   short fontstyle=0,fontsize=0;
   struct Colorinfo *fontcolor=NULL;
   UBYTE *fontface=NULL;
   BOOL setframe=FALSE,setwin=FALSE,setwhis=FALSE;
   void *whis;
   struct Element *child;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Left:
            bd->aox=tag->ti_Data;
            break;
         case AOBJ_Top:
            bd->aoy=tag->ti_Data;
            break;
         case AOBJ_Width:
            bd->aow=tag->ti_Data;
            break;
         case AOBJ_Height:
            bd->aoh=tag->ti_Data;
            break;
         case AOBJ_Cframe:
            bd->cframe=(void *)tag->ti_Data;
#if 0 /* MARQUEE app timer registration */
            /* Register marquee for animation when it becomes visible */
            if(tag->ti_Data && bd->marqueedirection)
            {  Aaddchild(Aweb(),bd,AOREL_APP_WANT_MARQUEE);
            }
            else if(!tag->ti_Data && bd->marqueedirection)
            {  Aremchild(Aweb(),bd,AOREL_APP_WANT_MARQUEE);
            }
#endif
            break;
         case AOBJ_Frame:
            /* AOBJ_Frame is set/cleared when in/out display.
             * Forward it to children, with ourself as Cframe */
            bd->frame=(void *)tag->ti_Data;
            setframe=TRUE;
            /* Let layout and render start from top */
            Removelinesbelow(bd,-1);
            bd->rendery=0;
            bd->flags|=BDYF_CHANGEDCHILD;
            bd->flags&=~BDYF_LAYOUTREADY;
            break;
         case AOBJ_Window:
            bd->win=(void *)tag->ti_Data;
            setwin=TRUE;
            break;
         case AOBJ_Winhis:
            whis=(void *)tag->ti_Data;
            setwhis=TRUE;
            break;
         case AOBJ_Nobackground:
            SETFLAG(bd->flags,BDYF_NOBACKGROUND,tag->ti_Data);
            break;
         case AOBJ_Changedbgimage:
            if(bd->parent && tag->ti_Data)
            {  Asetattrs(bd->parent,AOBJ_Changedbgimage,tag->ti_Data,TAG_END);
            }
            break;
         case AOBDY_Sethardstyle:
            if(bd->bld) bd->bld->hardstyle|=tag->ti_Data;
            break;
         case AOBDY_Unsethardstyle:
            if(bd->bld) bd->bld->hardstyle&=~tag->ti_Data;
            break;
         case AOBDY_Align:
            if(bd->bld)
            {  if((short)tag->ti_Data<0)
               {  bd->bld->align=bd->bld->divalign;
               }
               else
               {  bd->bld->align=tag->ti_Data;
               }
            }
            break;
         case AOBDY_Divalign:
            if(bd->bld)
            {  if((short)tag->ti_Data<0)
               {  bd->bld->align=bd->bld->divalign=HALIGN_LEFT;
               }
               else
               {  bd->bld->align=bd->bld->divalign=tag->ti_Data;
               }
            }
            break;
         case AOBJ_Pool:
            bd->pool=(void *)tag->ti_Data;
            break;
         case AOBDY_Style:
            fontstyle=tag->ti_Data;
            fontw|=FONTW_STYLE;
            break;
         case AOBDY_Fixedfont:
            if(bd->bld) bd->bld->fonttype=BOOLVAL(tag->ti_Data);
            break;
         case AOBDY_Fontsize:
            fontsize=tag->ti_Data;
            fontw|=FONTW_ABSSIZE;
            break;
         case AOBDY_Fontsizerel:
            fontsize=tag->ti_Data;
            fontw|=FONTW_RELSIZE;
            break;
         case AOBDY_Fontcolor:
            fontcolor=(struct Colorinfo *)tag->ti_Data;
            fontw|=FONTW_COLOR;
            break;
         case AOBDY_Fontface:
            fontface=(UBYTE *)tag->ti_Data;
            fontw|=FONTW_FACE;
            break;
         case AOBDY_Fontend:
            Popfont(bd);
            break;
         case AOBDY_Basefont:
            Setbasefont(bd,tag->ti_Data,FALSE);
            break;
         case AOBDY_Basefontrel:
            Setbasefont(bd,tag->ti_Data,TRUE);
            break;
         case AOBDY_Basecolor:
            Setbasecolor(bd,(struct Colorinfo *)tag->ti_Data);
            break;
         case AOBDY_Baseface:
            Setbaseface(bd,(UBYTE *)tag->ti_Data);
            break;
         case AOBDY_Subscript:
            if(tag->ti_Data) bd->flags|=BDYF_SUB;
            else bd->flags&=~BDYF_SUB;
            break;
         case AOBDY_Superscript:
            if(tag->ti_Data) bd->flags|=BDYF_SUP;
            else bd->flags&=~BDYF_SUP;
            break;
         case AOBDY_Link:
            if(bd->bld) bd->bld->link=(void *)tag->ti_Data;
            break;
         case AOBJ_Layoutparent:
            bd->parent=(void *)tag->ti_Data;
            break;
         case AOBJ_Changedchild:
            child=(struct Element *)tag->ti_Data;
            Removelinesbelow(bd,child->aoy);
            Setchchild(bd,child);
            bd->flags|=BDYF_CHANGEDCHILD;
            bd->flags&=~BDYF_LAYOUTREADY;
            Asetattrs(bd->parent,AOBJ_Changedchild,bd,TAG_END);
            Changedlayout();
            break;
         case AOBDY_Blockquote:
            if(bd->bld)
            {  if(tag->ti_Data)
               {  bd->bld->bqindent++;
               }
               else if(bd->bld->bqindent)
               {  bd->bld->bqindent--;
               }
            }
            break;
         case AOBDY_List:
            if(bd->bld)
            {  if(tag->ti_Data)
               {  Addlist(bd,(struct Listinfo *)tag->ti_Data);
               }
               else
               {  Remlist(bd);
               }
            }
            break;
         case AOBDY_Dterm:
            Setdterm(bd,BOOLVAL(tag->ti_Data));
            break;
         case AOBDY_Leftmargin:
            bd->hmargin=MAX(0,(short)tag->ti_Data);
            break;
         case AOBDY_Topmargin:
            bd->vmargin=MAX(0,(short)tag->ti_Data);
            break;
         case AOBDY_Bgcolor:
            bd->bgcolor=(short)tag->ti_Data;
            Removelinesbelow(bd,-1);
            bd->flags|=BDYF_CHANGEDCHILD;
            bd->flags&=~(BDYF_FORCEBGCOLOR|BDYF_LAYOUTREADY);
            break;
         case AOBDY_Forcebgcolor:
            bd->bgcolor=(short)tag->ti_Data;
            Removelinesbelow(bd,-1);
            bd->flags|=BDYF_CHANGEDCHILD|BDYF_FORCEBGCOLOR;
            bd->flags&=~BDYF_LAYOUTREADY;
            break;
         case AOBDY_Bgimage:
            bd->bgimage=(void *)tag->ti_Data;
            /* Register this body as a user of the background image */
            if(bd->bgimage)
            {  void *framecopy;
               struct Document *doc;
               struct Bguser *bgu;
               struct Bgimage *bgi;
               struct Frame *fr;
               /* get the document we belong to ... */
               /* but abort if we sometimes don't have a frame */
               /* NOTE: at sometime investigate just why this can happen */
               if(bd->frame)
               {  fr = (struct Frame *)bd->frame;
                  framecopy = fr->copy;
                  if(framecopy)
                  {  doc = (struct Document *)Agetattr((struct Aobject*)framecopy,AOCPY_Driver);
                     if(doc)
                     {  for(bgi=doc->bgimages.first;bgi->next;bgi=bgi->next)
                        {  if((void *)tag->ti_Data == bgi->copy)
                           {  if((bgu = ALLOCSTRUCT(Bguser,1,MEMF_CLEAR)))
                              {  bgu->user = bd;
                                 ADDTAIL(&bgi->bgusers,bgu);
                              }
                              break;
                           }
                        }
                     }
                  }
               }
            }
            Removelinesbelow(bd,-1);
            bd->flags|=BDYF_CHANGEDCHILD;
            bd->flags&=~BDYF_LAYOUTREADY;
            break;
         case AOBDY_Bgalign:
            bd->bgalign=(struct Aobject *)tag->ti_Data;
            break;
         case AOBDY_Tcell:
            bd->tcell=(void *)tag->ti_Data;
            break;
         case AOBDY_Nobr:
            SETFLAG(bd->flags,BDYF_NOBR,tag->ti_Data);
            break;
         case AOBDY_End:
            Disposebodybuild(bd);
            break;
         case AOBDY_TagName:
            if(bd->tagname && bd->tagname != (UBYTE *)tag->ti_Data)
            {  FREE(bd->tagname);
            }
            bd->tagname = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_Class:
            if(bd->class && bd->class != (UBYTE *)tag->ti_Data)
            {  FREE(bd->class);
            }
            bd->class = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_Id:
            if(bd->id && bd->id != (UBYTE *)tag->ti_Data)
            {  FREE(bd->id);
            }
            bd->id = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_LinkTextColor:
            bd->linktextcolor = (struct Colorinfo *)tag->ti_Data;
            break;
         case AOBDY_Position:
            if(bd->position && bd->position != (UBYTE *)tag->ti_Data)
            {  FREE(bd->position);
            }
            bd->position = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_ZIndex:
            bd->zindex = (long)tag->ti_Data;
            break;
         case AOBDY_Display:
            if(bd->display && bd->display != (UBYTE *)tag->ti_Data)
            {  FREE(bd->display);
            }
            bd->display = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_Overflow:
            if(bd->overflow && bd->overflow != (UBYTE *)tag->ti_Data)
            {  FREE(bd->overflow);
            }
            bd->overflow = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_Clear:
            if(bd->clear && bd->clear != (UBYTE *)tag->ti_Data)
            {  FREE(bd->clear);
            }
            bd->clear = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_VerticalAlign:
            bd->verticalalign = (short)tag->ti_Data;
            break;
         case AOBDY_ListStyle:
            if(bd->liststyle && bd->liststyle != (UBYTE *)tag->ti_Data)
            {  FREE(bd->liststyle);
            }
            bd->liststyle = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_ListStyleImage:
            if(bd->liststyleimage && bd->liststyleimage != (UBYTE *)tag->ti_Data)
            {  FREE(bd->liststyleimage);
            }
            bd->liststyleimage = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_MinWidth:
            bd->minwidth = (long)tag->ti_Data;
            break;
         case AOBDY_MaxWidth:
            bd->maxwidth = (long)tag->ti_Data;
            break;
         case AOBDY_MinHeight:
            bd->minheight = (long)tag->ti_Data;
            break;
         case AOBDY_MaxHeight:
            bd->maxheight = (long)tag->ti_Data;
            break;
         case AOBDY_PaddingTop:
            bd->paddingtop = (long)tag->ti_Data;
            break;
         case AOBDY_PaddingRight:
            bd->paddingright = (long)tag->ti_Data;
            break;
         case AOBDY_PaddingBottom:
            bd->paddingbottom = (long)tag->ti_Data;
            break;
         case AOBDY_PaddingLeft:
            bd->paddingleft = (long)tag->ti_Data;
            break;
         case AOBDY_BorderWidth:
            bd->borderwidth = (long)tag->ti_Data;
            break;
         case AOBDY_BorderColor:
            bd->bordercolor = (struct Colorinfo *)tag->ti_Data;
            break;
         case AOBDY_BorderStyle:
            if(bd->borderstyle && bd->borderstyle != (UBYTE *)tag->ti_Data)
            {  FREE(bd->borderstyle);
            }
            bd->borderstyle = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_Right:
            bd->right = (long)tag->ti_Data;
            break;
         case AOBDY_Bottom:
            bd->bottom = (long)tag->ti_Data;
            break;
         case AOBDY_Cursor:
            if(bd->cursor && bd->cursor != (UBYTE *)tag->ti_Data)
            {  FREE(bd->cursor);
            }
            bd->cursor = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_TextTransform:
            if(bd->texttransform && bd->texttransform != (UBYTE *)tag->ti_Data)
            {  FREE(bd->texttransform);
            }
            bd->texttransform = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_WhiteSpace:
            if(bd->whitespace && bd->whitespace != (UBYTE *)tag->ti_Data)
            {  FREE(bd->whitespace);
            }
            bd->whitespace = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_BackgroundRepeat:
            if(bd->backgroundrepeat && bd->backgroundrepeat != (UBYTE *)tag->ti_Data)
            {  FREE(bd->backgroundrepeat);
            }
            bd->backgroundrepeat = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_BackgroundPosition:
            if(bd->backgroundposition && bd->backgroundposition != (UBYTE *)tag->ti_Data)
            {  FREE(bd->backgroundposition);
            }
            bd->backgroundposition = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_BackgroundAttachment:
            if(bd->backgroundattachment && bd->backgroundattachment != (UBYTE *)tag->ti_Data)
            {  FREE(bd->backgroundattachment);
            }
            bd->backgroundattachment = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_Transform:
            if(bd->transform && bd->transform != (UBYTE *)tag->ti_Data)
            {  FREE(bd->transform);
            }
            bd->transform = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_TopPercent:
            bd->toppercent = tag->ti_Data;
            break;
         case AOBDY_LeftPercent:
            bd->leftpercent = tag->ti_Data;
            break;
         case AOBDY_MarginRight:
            bd->marginright = tag->ti_Data;
            break;
         case AOBDY_MarginBottom:
            bd->marginbottom = tag->ti_Data;
            break;
#if 0 /* MARQUEE AOM_SET tags (AOBDY_Dummy+68 .. AOAPP marquee tick) */
         case AOBDY_MarqueeDirection:
            if(bd->marqueedirection && bd->marqueedirection != (UBYTE *)tag->ti_Data)
            {  FREE(bd->marqueedirection);
            }
            bd->marqueedirection = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_MarqueeBehavior:
            if(bd->marqueebehavior && bd->marqueebehavior != (UBYTE *)tag->ti_Data)
            {  FREE(bd->marqueebehavior);
            }
            bd->marqueebehavior = (UBYTE *)tag->ti_Data;
            break;
         case AOBDY_MarqueeScrollAmount:
            bd->marqueescrollamount = tag->ti_Data;
            break;
         case AOBDY_MarqueeScrollDelay:
            bd->marqueescrolldelay = tag->ti_Data;
            break;
         case AOBDY_MarqueeLoop:
            bd->marqueeloop = tag->ti_Data;
            break;
         case AOBDY_MarqueeScrollX:
            bd->marqueescrollx = tag->ti_Data;
            break;
         case AOBDY_MarqueeScrollY:
            bd->marqueescrolly = tag->ti_Data;
            break;
         case AOAPP_Marquee:
            /* Update marquee scroll position */
            if(bd->marqueedirection)
            {  long dx=0,dy=0;
               long contentw,contenth;
               void *parent;
               
               /* Get content dimensions */
               Agetattrs(bd,
                  AOBJ_Width,&contentw,
                  AOBJ_Height,&contenth,
                  TAG_END);
               
               /* Calculate scroll delta based on direction */
               if(Stricmp((char *)bd->marqueedirection,"left")==0)
               {  dx = -bd->marqueescrollamount;
               }
               else if(Stricmp((char *)bd->marqueedirection,"right")==0)
               {  dx = bd->marqueescrollamount;
               }
               else if(Stricmp((char *)bd->marqueedirection,"up")==0)
               {  dy = -bd->marqueescrollamount;
               }
               else if(Stricmp((char *)bd->marqueedirection,"down")==0)
               {  dy = bd->marqueescrollamount;
               }
               
               /* Update scroll position */
               bd->marqueescrollx += dx;
               bd->marqueescrolly += dy;
               
               /* Handle behavior and bounds */
               if(bd->marqueebehavior)
               {  if(Stricmp((char *)bd->marqueebehavior,"alternate")==0)
                  {  /* Alternate: bounce back and forth */
                     if(!bd->marqueereversing)
                     {  /* Scrolling forward */
                        if(dx<0 && bd->marqueescrollx <= -(contentw-bd->aow))
                        {  bd->marqueereversing=TRUE;
                           bd->marqueescrollx = -(contentw-bd->aow);
                        }
                     }
                     else
                     {  /* Scrolling backward */
                        if(bd->marqueescrollx >= 0)
                        {  bd->marqueereversing=FALSE;
                           bd->marqueescrollx = 0;
                        }
                        else
                        {  /* Reverse direction */
                           dx = -dx;
                           bd->marqueescrollx += dx*2;  /* Compensate for earlier addition */
                        }
                     }
                  }
                  else if(Stricmp((char *)bd->marqueebehavior,"slide")==0)
                  {  /* Slide: stop at end */
                     if(dx<0 && bd->marqueescrollx <= -(contentw-bd->aow))
                     {  bd->marqueescrollx = -(contentw-bd->aow);
                        /* Stop scrolling - could unregister here */
                     }
                     else if(dx>0 && bd->marqueescrollx >= 0)
                     {  bd->marqueescrollx = 0;
                        /* Stop scrolling */
                     }
                  }
                  else /* scroll - continuous */
                  {  /* Wrap around */
                     if(dx<0 && bd->marqueescrollx <= -(contentw-bd->aow))
                     {  bd->marqueescrollx = bd->aow;
                        if(bd->marqueeloop>0)
                        {  bd->marqueeloopcount++;
                           if(bd->marqueeloopcount>=bd->marqueeloop)
                           {  /* Stop - could unregister */
                           }
                        }
                     }
                     else if(dx>0 && bd->marqueescrollx >= contentw)
                     {  bd->marqueescrollx = -(contentw-bd->aow);
                        if(bd->marqueeloop>0)
                        {  bd->marqueeloopcount++;
                           if(bd->marqueeloopcount>=bd->marqueeloop)
                           {  /* Stop */
                           }
                        }
                     }
                  }
               }
               
               /* Trigger render with new scroll position */
               parent=bd->parent;
               if(parent)
               {  /* Don't call Arender() directly from the app timer tick.
                   * Schedule the normal incremental update path instead.
                   *
                   * Important: avoid forcing a full layout pass from a periodic
                   * timer tick. Marquees should only request a redraw/update. */
                  Asetattrs(parent,AOBJ_Changedchild,bd,TAG_END);
               }
            }
            break;
#endif
      }
   }
   if(fontw) Pushfont(bd,fontstyle,fontsize,fontcolor,fontface,fontw);
   if(setframe || setwin || setwhis)
   {  for(child=bd->contents.first;child->next;child=child->next)
      {  Asetattrs(child,
            setframe?AOBJ_Cframe:TAG_IGNORE,bd->frame?bd:NULL,
            setframe?AOBJ_Frame:TAG_IGNORE,bd->frame,
            setwin?AOBJ_Window:TAG_IGNORE,bd->win,
            setwhis?AOBJ_Winhis:TAG_IGNORE,whis,
            TAG_END);
      }
   }
   return 0;
}

static void Disposebody(struct Body *bd)
{  void *p;
   while(p=REMHEAD(&bd->contents)) Adisposeobject(p);
   while(p=REMHEAD(&bd->lines)) FREE(p);
   while(p=REMHEAD(&bd->leftmargins)) FREE(p);
   while(p=REMHEAD(&bd->rightmargins)) FREE(p);
   while(p=REMHEAD(&bd->openfonts)) Freeopenfont(p);
   if(bd->bld) Disposebodybuild(bd);
   if(bd->tagname) FREE(bd->tagname);
   if(bd->class) FREE(bd->class);
   if(bd->id) FREE(bd->id);
   if(bd->position) FREE(bd->position);
   if(bd->display) FREE(bd->display);
   if(bd->overflow) FREE(bd->overflow);
   if(bd->clear) FREE(bd->clear);
      if(bd->liststyle) FREE(bd->liststyle);
      if(bd->liststyleimage) FREE(bd->liststyleimage);
      if(bd->borderstyle) FREE(bd->borderstyle);
   if(bd->cursor) FREE(bd->cursor);
   if(bd->texttransform) FREE(bd->texttransform);
   if(bd->whitespace) FREE(bd->whitespace);
   if(bd->backgroundrepeat) FREE(bd->backgroundrepeat);
   if(bd->backgroundposition) FREE(bd->backgroundposition);
   if(bd->backgroundattachment) FREE(bd->backgroundattachment);
   if(bd->transform) FREE(bd->transform);
#if 0 /* MARQUEE string fields */
   if(bd->marqueedirection) FREE(bd->marqueedirection);
   if(bd->marqueebehavior) FREE(bd->marqueebehavior);
#endif
   Amethodas(AOTP_OBJECT,bd,AOM_DISPOSE);
}

static struct Body *Newbody(struct Amset *ams)
{  struct Body *bd;
   if(bd=Allocobject(AOTP_BODY,sizeof(struct Body),ams))
   {  NEWLIST(&bd->contents);
      NEWLIST(&bd->lines);
      NEWLIST(&bd->leftmargins);
      NEWLIST(&bd->rightmargins);
      NEWLIST(&bd->openfonts);
      bd->tagname = NULL;
      bd->class = NULL;
      bd->id = NULL;
      bd->position = NULL;
      bd->zindex = 0;
      bd->display = NULL;
      bd->overflow = NULL;
      bd->clear = NULL;
      bd->verticalalign = -1;
      bd->liststyle = NULL;
      bd->minwidth = -1;
      bd->maxwidth = -1;
      bd->minheight = -1;
      bd->maxheight = -1;
      bd->paddingtop = 0;
      bd->paddingright = 0;
      bd->paddingbottom = 0;
      bd->paddingleft = 0;
      bd->borderwidth = 0;
      bd->bordercolor = NULL;
      bd->borderstyle = NULL;
      bd->linktextcolor = NULL;
      bd->right = -1;
      bd->bottom = -1;
      bd->cursor = NULL;
      bd->texttransform = NULL;
      bd->whitespace = NULL;
      bd->backgroundrepeat = NULL;
      bd->liststyleimage = NULL;
      bd->backgroundposition = NULL;
      bd->backgroundattachment = NULL;
      bd->transform = NULL;
      bd->toppercent = -1;  /* -1 means not set */
      bd->leftpercent = -1;  /* -1 means not set */
      bd->marginright = 0;
      bd->marginbottom = 0;
      bd->bgalign = NULL;
      bd->tcell = NULL;
      bd->marginleftauto = FALSE;
      bd->marginrightauto = FALSE;
      bd->lineheight = 0.0;  /* 0 means use default (no line-height specified) */
#if 0 /* MARQUEE defaults */
      bd->marqueedirection = NULL;
      bd->marqueebehavior = NULL;
      bd->marqueescrollamount = 6;
      bd->marqueescrolldelay = 85;
      bd->marqueeloop = -1;
      bd->marqueescrollx = 0;
      bd->marqueescrolly = 0;
      bd->marqueeloopcount = 0;
      bd->marqueereversing = FALSE;
#endif
      if(Newbodybuild(bd))
      {  Pushfont(bd,STYLE_NORMAL,0,NULL,NULL,FONTW_STYLE);
         bd->bgcolor=-1;
         Setbody(bd,ams);
      }
      else
      {  Disposebody(bd);
         bd=NULL;
      }
   }
   return bd;
}

static long Getbody(struct Body *bd,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Left:
            PUTATTR(tag,bd->aox);
            break;
         case AOBJ_Top:
            PUTATTR(tag,bd->aoy);
            break;
         case AOBJ_Width:
            PUTATTR(tag,bd->aow);
            break;
         case AOBJ_Height:
            PUTATTR(tag,bd->aoh);
            break;
         case AOBJ_Cframe:
            PUTATTR(tag,bd->cframe);
            break;
         case AOBJ_Frame:
            PUTATTR(tag,bd->frame);
            break;
         case AOBJ_Layoutparent:
            /* Document for root BODY; enclosing TABLE for TD/TH bodies (see table.c). */
            PUTATTR(tag,bd->parent);
            break;
         case AOBDY_List:
            if(!bd->bld || ISEMPTY(&bd->bld->list))
            {  PUTATTR(tag,&nolist);
            }
            else
            {  PUTATTR(tag,bd->bld->list.first);
            }
            break;
         case AOBDY_Style:
            PUTATTR(tag,bd->bld->hardstyle|bd->bld->font.first->style);
            break;
         case AOBDY_Fontface:
            /* Current HTML/CSS font stack (innermost face); used by ttengine UTF-8 binding in text.c. */
            if(bd->bld && bd->bld->font.first && bd->bld->font.first->face)
            {  PUTATTR(tag,bd->bld->font.first->face);
            }
            else
            {  PUTATTR(tag,NULL);
            }
            break;
         case AOBDY_Fontdepth:
            {  short depth = 0;
               struct Fontinfo *fi;
               if(bd->bld)
               {  for(fi = bd->bld->font.first; fi && fi->next; fi = fi->next)
                  {  depth++;
                  }
               }
               PUTATTR(tag, depth);
            }
            break;
         case AOBDY_Bgupdate:
            PUTATTR(tag,bd->bgupdate);
            break;
         case AOBDY_Bgalign:
            PUTATTR(tag,bd->bgalign);
            break;
         case AOBDY_Tcell:
            PUTATTR(tag,bd->tcell);
            break;
         case AOBDY_TagName:
            PUTATTR(tag,bd->tagname);
            break;
         case AOBDY_Class:
            PUTATTR(tag,bd->class);
            break;
         case AOBDY_Id:
            PUTATTR(tag,bd->id);
            break;
         case AOBDY_Position:
            PUTATTR(tag,bd->position);
            break;
         case AOBDY_ZIndex:
            PUTATTR(tag,bd->zindex);
            break;
         case AOBDY_Display:
            PUTATTR(tag,bd->display);
            break;
         case AOBDY_Overflow:
            PUTATTR(tag,bd->overflow);
            break;
         case AOBDY_Clear:
            PUTATTR(tag,bd->clear);
            break;
         case AOBDY_VerticalAlign:
            PUTATTR(tag,bd->verticalalign);
            break;
         case AOBDY_ListStyle:
            PUTATTR(tag,bd->liststyle);
            break;
         case AOBDY_ListStyleImage:
            PUTATTR(tag,bd->liststyleimage);
            break;
         case AOBDY_MinWidth:
            PUTATTR(tag,bd->minwidth);
            break;
         case AOBDY_MaxWidth:
            PUTATTR(tag,bd->maxwidth);
            break;
         case AOBDY_MinHeight:
            PUTATTR(tag,bd->minheight);
            break;
         case AOBDY_MaxHeight:
            PUTATTR(tag,bd->maxheight);
            break;
         case AOBDY_PaddingTop:
            PUTATTR(tag,bd->paddingtop);
            break;
         case AOBDY_PaddingRight:
            PUTATTR(tag,bd->paddingright);
            break;
         case AOBDY_PaddingBottom:
            PUTATTR(tag,bd->paddingbottom);
            break;
         case AOBDY_PaddingLeft:
            PUTATTR(tag,bd->paddingleft);
            break;
         case AOBDY_LinkTextColor:
            PUTATTR(tag,bd->linktextcolor);
            break;
         case AOBDY_BorderWidth:
            PUTATTR(tag,bd->borderwidth);
            break;
         case AOBDY_BorderColor:
            PUTATTR(tag,bd->bordercolor);
            break;
         case AOBDY_BorderStyle:
            PUTATTR(tag,bd->borderstyle);
            break;
         case AOBDY_Right:
            PUTATTR(tag,bd->right);
            break;
         case AOBDY_Bottom:
            PUTATTR(tag,bd->bottom);
            break;
         case AOBDY_Cursor:
            PUTATTR(tag,bd->cursor);
            break;
         case AOBDY_TextTransform:
            PUTATTR(tag,bd->texttransform);
            break;
         case AOBDY_WhiteSpace:
            PUTATTR(tag,bd->whitespace);
            break;
         case AOBDY_BackgroundRepeat:
            PUTATTR(tag,bd->backgroundrepeat);
            break;
         case AOBDY_BackgroundPosition:
            PUTATTR(tag,bd->backgroundposition);
            break;
         case AOBDY_BackgroundAttachment:
            PUTATTR(tag,bd->backgroundattachment);
            break;
         case AOBDY_Transform:
            PUTATTR(tag,bd->transform);
            break;
         case AOBDY_TopPercent:
            PUTATTR(tag,bd->toppercent);
            break;
         case AOBDY_LeftPercent:
            PUTATTR(tag,bd->leftpercent);
            break;
         case AOBDY_MarginRight:
            PUTATTR(tag,bd->marginright);
            break;
         case AOBDY_MarginBottom:
            PUTATTR(tag,bd->marginbottom);
            break;
      }
   }
   return 0;
}

static long Hittestbody(struct Body *bd,struct Amhittest *amh)
{  long result=0;
   struct Coords *coo,coords={0};
   struct Element *child;
   long x,y;
   if(!(coo=amh->coords))
   {  Framecoords(bd->cframe,&coords);
      coo=&coords;
   }
   if(coo->win)
   {  x=amh->xco-coo->dx;
      y=amh->yco-coo->dy;
      for(child=bd->contents.first;child->next;child=child->next)
      {  if(y>=child->aoy && y<child->aoy+child->aoh
         && x>=child->aox && x<child->aox+child->aow)
         {  result=Ahittest(child,coo,amh->xco,amh->yco,amh->flags,amh->oldobject,amh->amhr);
            if(result) break;
         }
      }
   }
   return result;
}

static long Addchild(struct Body *bd,struct Amadd *ama)
{  struct Fontprefs *fp,*supfp=NULL;
   struct Openfont *of,*supof=NULL;
   USHORT style;
   struct Colorinfo *ci;
   short valign=-1;
   BOOL ispre;
   short savedfonttype;
   BOOL forcedfixed;
   UBYTE *savedface;
   UWORD savedflags;
   struct Fontinfo *fi;
   struct Document *doc;
   UBYTE *utfacedup;
   if(bd->bld && ama->child)
   {  ispre = BOOLVAL(Agetattr(ama->child,AOELT_Preformat));
      savedfonttype = 0;
      forcedfixed = FALSE;
      savedface = NULL;
      savedflags = 0;
      fi = NULL;
      if(ispre)
      {  /* Ensure deterministic fixed-width font selection for preformatted text.
          * Without this, timing/style updates can cause mixed fonts within a PRE block,
          * making columns look "haphazard" on reload even though whitespace is preserved. */
         savedfonttype = bd->bld->fonttype;
         bd->bld->fonttype = 1;
         forcedfixed = TRUE;
         /* Also ignore any explicit face (e.g. CSS font-family on BODY/TD) while
          * selecting the font for preformatted text. If we keep a proportional face
          * like "Verdana,...", Matchfont() may still choose a proportional font even
          * when fonttype is forced, breaking column alignment. */
         fi = bd->bld->font.first;
         if(fi)
         {  savedface = fi->face;
            savedflags = fi->flags;
            fi->face = NULL;
            fi->flags &= ~FONTF_FACE;
         }
      }
      Getcurrentfont(bd,&fp,&style,&ci);
      if(forcedfixed)
      {  bd->bld->fonttype = savedfonttype;
         if(fi)
         {  fi->face = savedface;
            fi->flags = savedflags;
         }
      }
      of=Addopenfont(bd,fp->font);
      if(bd->flags&BDYF_SUB) valign=VALIGN_SUB;
      else if(bd->flags&BDYF_SUP)
      {  valign=VALIGN_SUP;
         supfp=Getfontprefs(bd,bd->bld->font.first->next,NULL);
         supof=Addopenfont(bd,supfp->font);
      }
      /* Use link text color if link is active and custom color is set */
      if(bd->bld->link && bd->linktextcolor)
      {  ci = bd->linktextcolor;
      }
      utfacedup=NULL;
      doc=Bodyfinddocument(bd);
      if(((struct Aobject *)ama->child)->objecttype==AOTP_TEXT
      && doc && doc->charset==DOCCHARSET_UTF8
      && bd->bld->font.first && bd->bld->font.first->face)
      {  utfacedup=Dupstr(bd->bld->font.first->face,-1);
      }
      Asetattrs(ama->child,
         AOELT_Link,bd->bld->link,
         AOELT_Font,of->font,
         AOELT_Style,style,
         AOELT_Defhalign,bd->bld->align,
         AOELT_Color,ci,
         AOELT_Leftindent,LEFTINDENT(bd),
         AOELT_Rightindent,RIGHTINDENT(bd),
         AOELT_Nobr,BOOLVAL(bd->flags&BDYF_NOBR),
         AOBJ_Frame,bd->frame,
         AOBJ_Cframe,bd->frame?bd:NULL,
         AOBJ_Window,bd->win,
         (valign>=0)?AOELT_Valign:TAG_IGNORE,valign,
         supof?AOELT_Supfont:TAG_IGNORE,supof?supof->font:NULL,
         AOBJ_Layoutparent,bd,
         AOBJ_Nobackground,BOOLVAL(bd->flags&BDYF_NOBACKGROUND),
         utfacedup?AOELT_Fontfacestr:TAG_IGNORE,(ULONG)utfacedup,
         TAG_END);
      ADDTAIL(&bd->contents,ama->child);
      /* Set changed child flag so layout CHANGED will do something. */
      bd->flags|=BDYF_CHANGEDCHILD;
      bd->flags&=~BDYF_LAYOUTREADY;
   }
   return 0;
}

static long Movebody(struct Body *bd,struct Ammove *amm)
{  struct Element *child;
   bd->aox+=amm->dx;
   bd->aoy+=amm->dy;
   for(child=bd->contents.first;child->next;child=child->next)
   {  AmethodA(child,amm);
   }
   return 0;
}

static long Notifybody(struct Body *bd,struct Amnotify *amn)
{  struct Element *child;
   for(child=bd->contents.first;child->next;child=child->next)
   {  AmethodA(child,amn);
   }
   return 0;
}

static long Searchposbody(struct Body *bd,struct Amsearch *ams)
{  struct Element *child;
   long result=0;
   for(child=bd->contents.first;child->next;child=child->next)
   {  if(ams->top>=child->aoy && ams->top<child->aoy+child->aoh) break;
   }
   for(;!result && child->next;child=child->next)
   {  result=AmethodA(child,ams);
   }
   return result;
}

static long Searchsetbody(struct Body *bd,struct Amsearch *ams)
{  struct Element *child;
   long result=0;
   for(child=bd->contents.first;!result && child->next;child=child->next)
   {  result=AmethodA(child,ams);
   }
   return result;
}

static long Dragtestbody(struct Body *bd,struct Amdragtest *amd)
{  long result=0;
   struct Element *child;
   long x,y;
   BOOL hit;
   if(amd->coords && amd->coords->win)
   {  x=amd->xco-amd->coords->dx;
      y=amd->yco-amd->coords->dy;
      for(child=bd->contents.first;child->next && !result;child=child->next)
      {  hit=FALSE;
         if(child->aoy<=y && child->aoy+child->aoh>y)
         {  if(child->objecttype==AOTP_BREAK || child->aox+child->aow>x) hit=TRUE;
         }
         else if(child->aoy>y) hit=TRUE;
         if(hit)
         {  result=AmethodA(child,amd);
            /* In case of floating right object, the test on x above could pick the
             * wrong child. So if child is floating right, revert AMDR_STOP to AMDR_NOHIT. */
            if((child->halign&HALIGN_FLOATRIGHT)==HALIGN_FLOATRIGHT && result==AMDR_STOP)
            {  result=AMDR_NOHIT;
            }
         }
      }
   }
   return result;
}

static long Dragrenderbody(struct Body *bd,struct Amdragrender *amd)
{  long result=0;
   struct Element *child;
   short bgcolor;
   void *bgimage;
   if(amd->coords && amd->coords->win)
   {  bgcolor=amd->coords->bgcolor;
      bgimage=amd->coords->bgimage;
      /* Always apply background color if set, regardless of docolors setting */
      if(bd->bgcolor>=0)
      {  amd->coords->bgcolor=bd->bgcolor;
      }
      /* Only apply background image if docolors is enabled */
      if(prefs.browser.docolors && bd->bgimage)
      {  amd->coords->bgimage=bd->bgimage;
      }
      else if(bd->bgcolor>=0)
      {  /* If body has background color but no background image,
           * clear parent's background image so child's color is used */
         amd->coords->bgimage=NULL;
      }
      /* Fallback: apply bgcolor even when docolors is off if FORCEBGCOLOR flag is set */
      if(!prefs.browser.docolors && (bd->flags&BDYF_FORCEBGCOLOR) && bd->bgcolor>=0)
      {  amd->coords->bgcolor=bd->bgcolor;
         /* Clear background image when forcing background color */
         amd->coords->bgimage=NULL;
      }
      for(child=bd->contents.first;child->next && amd->state!=AMDS_DONE;child=child->next)
      {  result=AmethodA(child,amd);
      }
      amd->coords->bgcolor=bgcolor;
      amd->coords->bgimage=bgimage;
   }
   return result;
}

static long Dragcopybody(struct Body *bd,struct Amdragcopy *amd)
{  long result=0;
   struct Element *child;
   for(child=bd->contents.first;child->next && amd->state!=AMDS_DONE;child=child->next)
   {  result=AmethodA(child,amd);
   }
   return result;
}

static long Jsetupbody(struct Body *bd,struct Amjsetup *amj)
{  struct Element *child;
   for(child=bd->contents.first;child->next;child=child->next)
   {  AmethodA(child,amj);
   }
   return 0;
}

/* Accessor functions for Body structure fields (for CSS support) */
void SetBodyMarginLeftAuto(struct Body *bd, BOOL isAuto)
{  if(bd) bd->marginleftauto = isAuto;
}

void SetBodyMarginRightAuto(struct Body *bd, BOOL isAuto)
{  if(bd) bd->marginrightauto = isAuto;
}

void SetBodyLineHeight(struct Body *bd, float lineheight)
{  if(bd) bd->lineheight = lineheight;
}

static long Dispatch(struct Body *bd,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newbody((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setbody(bd,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getbody(bd,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measurebody(bd,(struct Ammeasure *)amsg);
         break;
      case AOM_LAYOUT:
         result=Layoutbody(bd,(struct Amlayout *)amsg);
         break;
      case AOM_RENDER:
         result=Renderbody(bd,(struct Amrender *)amsg);
         break;
      case AOM_HITTEST:
         result=Hittestbody(bd,(struct Amhittest *)amsg);
         break;
      case AOM_ADDCHILD:
         result=Addchild(bd,(struct Amadd *)amsg);
         break;
      case AOM_MOVE:
         result=Movebody(bd,(struct Ammove *)amsg);
         break;
      case AOM_NOTIFY:
         result=Notifybody(bd,(struct Amnotify *)amsg);
         break;
      case AOM_SEARCHPOS:
         result=Searchposbody(bd,(struct Amsearch *)amsg);
         break;
      case AOM_SEARCHSET:
         result=Searchsetbody(bd,(struct Amsearch *)amsg);
         break;
      case AOM_DRAGTEST:
         result=Dragtestbody(bd,(struct Amdragtest *)amsg);
         break;
      case AOM_DRAGRENDER:
         result=Dragrenderbody(bd,(struct Amdragrender *)amsg);
         break;
      case AOM_DRAGCOPY:
         result=Dragcopybody(bd,(struct Amdragcopy *)amsg);
         break;
      case AOM_JSETUP:
         result=Jsetupbody(bd,(struct Amjsetup *)amsg);
         break;
      case AOM_DISPOSE:
         Disposebody(bd);
         break;
      case AOM_DEINSTALL:
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installbody(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_BODY,Dispatch)) return FALSE;
   return TRUE;
}

void *Bodyframe(struct Body *bd)
{  if(bd && bd->objecttype==AOTP_BODY)
   {  return bd->frame;
   }
   else
   {  return bd;
   }
}

void Bodycoords(struct Body *bd,struct Coords *coo)
{  if(bd->objecttype==AOTP_BODY)
   {  Framecoords(bd->cframe,coo);
      /* Always apply background color if set, regardless of docolors setting */
      if(bd->bgcolor>=0 && !(bd->flags&BDYF_NOBACKGROUND))
      {  coo->bgcolor=bd->bgcolor;
      }
      /* Only apply background image if docolors is enabled */
      if(prefs.browser.docolors && bd->bgimage && !(bd->flags&BDYF_NOBACKGROUND))
      {  coo->bgimage=bd->bgimage;
         coo->bgalign=bd->bgalign;
      }
      else if(bd->bgcolor>=0 && !(bd->flags&BDYF_NOBACKGROUND))
      {  /* If body has background color but no background image,
           * clear parent's background image so child's color is used */
         coo->bgimage=NULL;
         coo->bgalign=NULL;
      }
      /* Fallback: apply bgcolor even when docolors is off if FORCEBGCOLOR flag is set */
      if(!prefs.browser.docolors && (bd->flags&BDYF_FORCEBGCOLOR) && bd->bgcolor>=0)
      {  coo->bgcolor=bd->bgcolor;
         /* Clear background image when forcing background color */
         coo->bgimage=NULL;
         coo->bgalign=NULL;
      }
   }
   else
   {  Framecoords(bd,coo);
   }
}