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

/* select.c - AWeb html document selector form field element object */

#include "aweb.h"
#include "select.h"
#include "scroller.h"
#include "application.h"
#include "window.h"
#include "jslib.h"
#include <proto/exec.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <intuition/imageclass.h>
#include <images/bevel.h>
#include <images/glyph.h>
#include <gadgets/listbrowser.h>
#include <gadgets/layout.h>
#include <classes/window.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/listbrowser.h>
#include <proto/bevel.h>
#include <proto/layout.h>
#include <proto/window.h>
#include <proto/glyph.h>

/*------------------------------------------------------------------------*/

LIST(Selpopup) selpopups;

struct Selpopup                  /* The select field popup window */
{  NODE(Selpopup);
   struct Select *select;
   struct List nodes;            /* Option nodes */
   void *cawin;                  /* Reaction Window object */
   void *listgad;                /* Listbrowser object */
   struct Window *window;        /* Window */
   USHORT flags;
   struct Hook hook;             /* Our IDCMP hook */
   long inactx,inacty;           /* Screen coordinates of mouse on window inactivation */
};

#define SPUF_DONE    0x0001      /* Popup window closed */

/*------------------------------------------------------------------------*/

struct Select
{  struct Field field;
   void *pool;
   void *capens;
   void *parent;                 /* Parent to notify if size changes */
   short size;                   /* Size of object */
   short orgsize;                /* Initially defined size of object */
   LIST(Option) options;
   long nroptions;               /* Number of options. */
   long width;                   /* Width of widest option text */
   long listw;                   /* Width of list bevel */
   long itemh;                   /* Height of item in list */
   long top;                     /* First displayed item in list */
   long wasselected;             /* Previously selected item */
   USHORT flags;
   void *scroll;                 /* Scroller if list */
   UBYTE *multivalue;
   UBYTE *onchange;
   UBYTE *onfocus;
   UBYTE *onblur;
   struct Selpopup *spu;         /* The popup window */
};

#define SELF_MULTIPLE      0x0001   /* Multiple selection allowed. */
#define SELF_COMPLETE      0x0002   /* Definition is complete. */
#define SELF_POPUP         0x0004   /* Otherwise it's a list. */
#define SELF_SCROLLER      0x0008   /* Scroller should be rendered. */
#define SELF_SELECTED      0x0010   /* In active selected state */
#define SELF_CHANGED       0x0020   /* Popup selection was changed so run onChange. */
#define SELF_SIZECHANGED   0x0040   /* Nr of options has changed */
#define SELF_DEFERCOMPLETE 0x0080   /* Do completion when we got a window */
#define SELF_IGNORECLICK   0x0100   /* Ignore next mouse click */

/* Option data must not be allocated in the document pool because it may
 * live longer than the document when it is in a JS variable somewhere. */
struct Option
{  NODE(Option);
   UBYTE *text;                  /* Text displayed for option. */
   UBYTE *value;                 /* Value of option or NULL. */
   USHORT flags;
   long width;                   /* Width of option text in pixels. */
   struct Select *sel;           /* Link back for sake of JS */
   struct Jobject *jobject;      /* JS object */
};

#define OPTF_SELECTED      0x0001   /* Option is selected */
#define OPTF_INITIAL       0x0002   /* Option was initially selected */
#define OPTF_INLIST        0x0004   /* Option is member of list */

static void *bevel,*check;
static long bevelw,bevelh;
static long checkw,checkh;

static struct TagItem mapscroll[]={ AOSCR_Top,AOSEL_Listtop,TAG_END };

/*------------------------------------------------------------------------*/

/* Get font from element, with fallback to screen font */
static struct TextFont *Getselectfont(struct Select *sel)
{  struct TextFont *font;
   
   /* Try to get font from element */
   font=(struct TextFont *)Agetattr(sel,AOELT_Font);
   
   /* Fallback to screen font if not set */
   if(!font)
   {  font=(struct TextFont *)Agetattr(Aweb(),AOAPP_Screenfont);
   }
   
   return font;
}

static void Processselpopup(void);

static long __saveds __asm Selpopupidcmphook(register __a0 struct Hook *hook,
   register __a1 struct IntuiMessage *msg)
{  struct Selpopup *spu=(struct Selpopup *)hook->h_Data;
   switch(msg->Class)
   {  case IDCMP_INACTIVEWINDOW:
         spu->inactx=msg->MouseX+msg->IDCMPWindow->LeftEdge;
         spu->inacty=msg->MouseY+msg->IDCMPWindow->TopEdge;
         break;
   }
   return 0;
}

static struct Selpopup *Openselpopup(struct Select *sel)
{  struct Selpopup *spu=NULL;
   struct Option *opt;
   struct Node *node;
   struct Screen *screen;
   struct DrawInfo *dri;
   struct MsgPort *port;
   struct Coords coords={0};
   struct Window *refwin;
   struct TextFont *font;
   long n,height,screenh,selected=-1;
   BOOL fits=TRUE;
   if(Agetattr(Aweb(),AOAPP_Screenvalid))
   {  Agetattrs(Aweb(),
         AOAPP_Screen,&screen,
         AOAPP_Drawinfo,&dri,
         AOAPP_Windowport,&port,
         AOAPP_Screenheight,&screenh,
         TAG_END);
      Asetattrs(Aweb(),
         AOAPP_Processtype,AOTP_SELECT,
         AOAPP_Processfun,Processselpopup,
         TAG_END);
      /* Get font from select element */
      font=Getselectfont(sel);
      if(spu=ALLOCSTRUCT(Selpopup,1,MEMF_CLEAR))
      {  spu->select=sel;
         NewList(&spu->nodes);
         ADDTAIL(&selpopups,spu);
         Framecoords(sel->cframe,&coords);
         refwin=(struct Window *)Agetattr(coords.win,AOWIN_Window);
         for(n=0,opt=sel->options.first;opt->next;opt=opt->next,n++)
         {  if(opt->flags&OPTF_SELECTED) selected=n;
            node=AllocListBrowserNode(1,
               LBNA_Flags,(dri->dri_NumPens>BARBLOCKPEN)?LBFLG_CUSTOMPENS:0,
               LBNCA_Text,opt->text,
               LBNCA_FGPen,(dri->dri_NumPens>BARBLOCKPEN)?dri->dri_Pens[BARDETAILPEN]:0,
               LBNCA_BGPen,(dri->dri_NumPens>BARBLOCKPEN)?dri->dri_Pens[BARBLOCKPEN]:0,
               TAG_END);
            if(node) AddTail(&spu->nodes,node);
         }
         screenh-=4*bevelh;
         /* Use default screen font height for popup, not element font */
         {  struct TextFont *screenfont=(struct TextFont *)Agetattr(Aweb(),AOAPP_Screenfont);
            long itemh=screenfont?screenfont->tf_YSize:sel->itemh;
            height=sel->nroptions*itemh+4*bevelh;
            if(height>screenh)
            {  height=screenh;
               fits=FALSE;
            }
         }
         spu->hook.h_Entry=(HOOKFUNC)Selpopupidcmphook;
         spu->hook.h_Data=spu;
         spu->inactx=spu->inacty=-1;
         spu->cawin=WindowObject,
            WA_Borderless,TRUE,
            WA_Activate,TRUE,
            WA_PubScreen,screen,
            WA_Left,sel->aox+coords.dx+(refwin?refwin->LeftEdge:0),
            WA_Top,sel->aoy+coords.dy+sel->aoh+(refwin?refwin->TopEdge:0),
            WA_IDCMP,IDCMP_VANILLAKEY|IDCMP_MOUSEBUTTONS,
            WA_RMBTrap,TRUE,
            WINDOW_SharedPort,port,
            WINDOW_UserData,sel,
            WINDOW_IDCMPHook,&spu->hook,
            WINDOW_IDCMPHookBits,IDCMP_INACTIVEWINDOW,
            WINDOW_Layout,VLayoutObject,
               LAYOUT_SpaceOuter,FALSE,
               LAYOUT_SpaceInner,FALSE,
               StartMember,spu->listgad=ListBrowserObject,
                  GA_RelVerify,TRUE,
                  LISTBROWSER_ShowSelected,TRUE,
                  LISTBROWSER_Labels,&spu->nodes,
                  LISTBROWSER_Selected,selected,
                  LISTBROWSER_VerticalProp,!fits,
               EndMember,
               CHILD_MinWidth,fits?sel->aow:
                  (sel->width+Agetattr(Firstwindow(),AOWIN_Borderright)+2*bevelw+16),
               CHILD_MinHeight,height,
            End,
         End;
         if(spu->cawin)
         {  spu->window=(struct Window *)RA_OpenWindow(spu->cawin);
            /* Set font on window's RastPort before ListBrowser renders */
            if(font && spu->window && spu->window->RPort)
            {  SetFont(spu->window->RPort,font);
            }
            Setgadgetattrs(spu->listgad,spu->window,NULL,
               LISTBROWSER_MakeVisible,selected,
               TAG_END);
            /* Force ListBrowser to refresh with new font */
            if(font && spu->window && spu->listgad)
            {  Setgadgetattrs(spu->listgad,spu->window,NULL,
                  LISTBROWSER_Labels,~0,
                  TAG_END);
               Setgadgetattrs(spu->listgad,spu->window,NULL,
                  LISTBROWSER_Labels,&spu->nodes,
                  TAG_END);
            }
         }
      }
   }
   return spu;
}
               
static void Closeselpopup(struct Selpopup *spu)
{  struct Node *node;
   if(spu)
   {  spu->select->spu=NULL;
      if(spu->cawin) DisposeObject(spu->cawin);
      while(node=RemHead(&spu->nodes)) FreeListBrowserNode(node);
      REMOVE(spu);
      FREE(spu);
   }
}

/*------------------------------------------------------------------------*/

/* Truely dispose an option */
static void Disposeoption(struct Option *opt)
{  if(opt)
   {  if(opt->text) FREE(opt->text);
      if(opt->value) FREE(opt->value);
      /* Object can only be disposed if no jsobject exists, or as
       * a result of the JS dispose hook. So no need to dispose
       * the jsobject. */
      FREE(opt);
   }
}

/* Dispose an option as far as Select is concerned */
static void Sdisposeoption(struct Option *opt)
{  if(opt)
   {  opt->flags=0;
      opt->sel=NULL;
      if(!opt->jobject) Disposeoption(opt);
   }
}

/* Add an option at the end of the list */
static struct Option *Addoption(struct Select *sel,UBYTE *value,BOOL selected)
{  struct Option *opt;
   if(opt=ALLOCSTRUCT(Option,1,MEMF_CLEAR))
   {  opt->value=Dupstr(value,-1);
      opt->sel=sel;
      if(selected) opt->flags|=OPTF_SELECTED|OPTF_INITIAL;
      ADDTAIL(&sel->options,opt);
      opt->flags|=OPTF_INLIST;
      sel->nroptions++;
   }
   return opt;
}

/* Add text to an option */
static void Addoptiontext(struct Select *sel,struct Option *opt,UBYTE *text)
{  long len=strlen(text);
   UBYTE *newp;
   if(len || !opt->text)
   {  if(opt->text) len+=strlen(opt->text);
      if(newp=ALLOCTYPE(UBYTE,len+1,MEMF_CLEAR))
      {  if(opt->text)
         {  strcpy(newp,opt->text);
            FREE(opt->text);
         }
         strcat(newp,text);
         opt->text=newp;
      }
   }
}

/* Find the Nth option */
static struct Option *Findoption(struct Select *sel,long n)
{  struct Option *opt;
   for(opt=sel->options.first;n && opt->next;n--,opt=opt->next);
   if(!opt->next) opt=NULL;
   return opt;
}

/* Find selected option number */
static long Selectedoption(struct Select *sel)
{  long n;
   struct Option *opt;
   for(n=0,opt=sel->options.first;opt->next;n++,opt=opt->next)
   {  if(opt->flags&OPTF_SELECTED) return n;
   }
   return -1;
}

/*------------------------------------------------------------------------*/
static void Completeselect(struct Select *sel)
{  long minh,listh,size;
   struct Option *opt;
   BOOL select=FALSE;
   struct TextFont *font=Getselectfont(sel);
   UBYTE *p;
   long i,n;
   if(!font)
   {  /* We are iconified, defer completion until we got a window */
      sel->flags|=SELF_DEFERCOMPLETE;
      return;
   }
   sel->flags|=SELF_COMPLETE;
   sel->flags&=~(SELF_POPUP|SELF_SCROLLER|SELF_DEFERCOMPLETE);
   sel->size=sel->orgsize;
   if(sel->size>sel->nroptions) sel->size=MAX(1,sel->nroptions);
   if(sel->size==1 && (sel->flags&SELF_MULTIPLE))
   {  sel->size=MIN(4,sel->nroptions);
   }
   if(sel->size==1 && !(sel->flags&SELF_MULTIPLE))
   {  sel->flags|=SELF_POPUP;
   }
   sel->itemh=font->tf_YSize;
   if(!(sel->flags&SELF_POPUP))
   {  sel->valign=VALIGN_TOP;
      if(!sel->scroll)
      {  sel->scroll=Anewobject(AOTP_SCROLLER,
            AOSCR_Orient,AOSCRORIENT_VERT,
            AOSCR_Total,sel->nroptions,
            AOSCR_Visible,sel->size,
            AOBJ_Target,sel,
            AOBJ_Map,mapscroll,
            AOBJ_Cframe,sel->cframe,
            TAG_END);
      }
      minh=Agetattr(sel->scroll,AOSCR_Minheight);
      if((sel->flags&SELF_MULTIPLE) && sel->itemh<checkh) sel->itemh=checkh;
      listh=sel->size*sel->itemh+2*bevelh;
      /* If list height less then minimum scroller height, increase size */
      if(listh<minh)
      {  size=((minh-1)/sel->itemh)+1;
         if(size<sel->nroptions)
         {  sel->size=size;
            sel->flags|=SELF_SCROLLER;
         }
         else
         {  /* Not enough items to allow a scroller. Use nroptions as size. */
            sel->size=MAX(1,sel->nroptions);
         }
      }
      else if(sel->size<sel->nroptions)
      {  sel->flags|=SELF_SCROLLER;
      }
   }
   /* Strip trailing spaces from option text */
   for(opt=sel->options.first;opt->next;opt=opt->next)
   {  if(opt->text)
      {  p=opt->text+strlen(opt->text)-1;
         while(p>opt->text && *p==' ') p--;
         p[1]='\0';
      }
      else
      {  opt->text=Dupstr("",-1);
      }
   }
   /* Make sure that exactly one option is selected for single select */
   if(!(sel->flags&SELF_MULTIPLE))
   {  for(i=0,opt=sel->options.first;opt->next;i++,opt=opt->next)
      {  if(opt->flags&OPTF_SELECTED)
         {  if(select) opt->flags&=~(OPTF_SELECTED|OPTF_INITIAL);
            else
            {  select=TRUE;
               n=i;
            }
         }
      }
      if(!select && sel->options.first->next)
      {  sel->options.first->flags|=(OPTF_SELECTED|OPTF_INITIAL);
         n=0;
      }
      if(n>=sel->size) sel->top=n-sel->size+1;
   }
   if(sel->top>sel->nroptions-sel->size) sel->top=sel->nroptions-sel->size;
   if(sel->top<0) sel->top=0;
   if(sel->flags&SELF_SCROLLER)
   {  Asetattrs(sel->scroll,
         AOSCR_Top,sel->top,
         AOSCR_Total,sel->nroptions,
         AOSCR_Visible,sel->size,
         TAG_END);
   }
}

/* Get the single selected value */
static UBYTE *Singlevalue(struct Select *sel)
{  struct Option *opt;
   UBYTE *value=NULL;
   for(opt=sel->options.first;opt->next;opt=opt->next)
   {  if(opt->flags&OPTF_SELECTED)
      {  if(opt->value) value=opt->value;
         else value=opt->text;
         break;
      }
   }
   return value;
}

/* Build a multiple selected value */
static UBYTE *Multivalue(struct Select *sel)
{  struct Option *opt;
   long len=0,n=0;
   UBYTE *p;
   if(sel->multivalue) FREE(sel->multivalue);
   for(opt=sel->options.first;opt->next;opt=opt->next)
   {  if(opt->flags&OPTF_SELECTED)
      {  n++;
         if(!(p=opt->value)) p=opt->text;
         len+=strlen(p)+1;
      }
   }
   len+=4;  /* longword */
   if(sel->multivalue=PALLOCTYPE(UBYTE,len,0,sel->pool))
   {  *(long *)sel->multivalue=n;
      p=sel->multivalue+4;
      for(opt=sel->options.first;opt->next;opt=opt->next)
      {  if(opt->flags&OPTF_SELECTED)
         {  if(opt->value) strcpy(p,opt->value);
            else strcpy(p,opt->text);
            p+=strlen(p)+1;
         }
      }
   }
   return sel->multivalue;
}

/* Reset selection(s) */
static void Resetselect(struct Select *sel)
{  struct Option *opt;
   long i,n;
   for(i=0,opt=sel->options.first;opt->next;i++,opt=opt->next)
   {  if(opt->flags&OPTF_INITIAL)
      {  opt->flags|=OPTF_SELECTED;
         n=i;
      }
      else opt->flags&=~OPTF_SELECTED;
   }
   if((sel->flags&SELF_SCROLLER) && !(sel->flags&SELF_MULTIPLE))
   {  if(n>=sel->size) sel->top=n-sel->size+1;
      else sel->top=0;
      Asetattrs(sel->scroll,AOSCR_Top,sel->top,TAG_END);
   }
   Arender(sel,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
}

/* Free the JS objects */
static void Freejselect(struct Select *sel)
{  if(sel->jobject)
   {  Disposejobject(sel->jobject);
      sel->jobject=NULL;
   }
   /* Individual option object are disposed as a result of disposing the sel->jobject */
}

/*------------------------------------------------------------------------*/

static void Renderpopup(struct Select *sel,struct Coords *coo)
{  struct RastPort *rp=coo->rp;
   struct Option *opt;
   long x,x2,y,y2;
   BOOL select=BOOLVAL(sel->flags&SELF_SELECTED);
   struct ColorMap *colormap=NULL;
   struct DrawInfo *drinfo=NULL;
   struct TextFont *font=NULL;
   x=sel->aox+coo->dx;
   y=sel->aoy+coo->dy;
      Agetattrs(Aweb(),
         AOAPP_Colormap,&colormap,
         AOAPP_Drawinfo,&drinfo,
         TAG_END);
      font=Getselectfont(sel);
   SetAttrs(bevel,
      IA_Width,sel->aow,
      IA_Height,sel->aoh,
      BEVEL_FillPen,~0,
      BEVEL_ColorMap,colormap,
      BEVEL_Flags,BFLG_XENFILL,
      REACTION_SpecialPens,sel->capens,
      TAG_END);
   DrawImageState(rp,bevel,x,y,select?IDS_SELECTED:IDS_NORMAL,drinfo);
   x+=bevelw;
   x2=sel->aox+coo->dx+sel->aow-bevelw-19;
   y+=bevelh;
   y2=sel->aoy+coo->dy+sel->aoh-bevelh;
   SetAPen(rp,coo->dri->dri_Pens[select?SHINEPEN:SHADOWPEN]);
   Move(rp,x2,y+1);
   Draw(rp,x2,y2-2);
   SetAPen(rp,coo->dri->dri_Pens[select?SHADOWPEN:SHINEPEN]);
   Move(rp,x2+1,y+1);
   Draw(rp,x2+1,y2-2);
   SetAPen(rp,coo->dri->dri_Pens[select?FILLTEXTPEN:TEXTPEN]);
   Move(rp,x2+4,y+3);
   Draw(rp,x2+14,y+3);
   Draw(rp,x2+9,y2-4);
   Draw(rp,x2+4,y+3);
   for(opt=sel->options.first;opt->next && !(opt->flags&OPTF_SELECTED);opt=opt->next);
   if(opt->next)
   {  SetFont(rp,font);
      Move(rp,x+2+(sel->width-opt->width)/2,(y+y2)/2-rp->TxHeight/2+rp->TxBaseline);
      Text(rp,opt->text,strlen(opt->text));
   }
}

static long Hittestpopup(struct Select *sel,struct Amhittest *amh)
{  long result;
   if(amh->oldobject==sel)
   {  result=AMHR_OLDHIT;
   }
   else
   {  result=AMHR_NEWHIT;
      if(amh->amhr)
      {  amh->amhr->object=sel;
      }
   }
   return result;
}

/* Select the nth option, and deselect the previously selected one. Render
   the change. */
static void Selectpopup(struct Select *sel,struct Coords *coo,long n)
{  struct Option *opt;
   long i;
   for(i=0,opt=sel->options.first;opt->next;i++,opt=opt->next)
   {  if(i==n)
      {  if(opt->flags&OPTF_SELECTED) return;    /* No change */
         opt->flags|=OPTF_SELECTED;
      }
      else if(opt->flags&OPTF_SELECTED)
      {  opt->flags&=~OPTF_SELECTED;
      }
   }
   Arender(sel,coo,0,0,AMRMAX,AMRMAX,0,NULL);
}

static long Goactivepopup(struct Select *sel,struct Amgoactive *amg)
{  if(sel->flags&SELF_IGNORECLICK)
   {  sel->flags&=~SELF_IGNORECLICK;
   }
   else
   {  sel->flags&=~SELF_CHANGED;
      sel->wasselected=Selectedoption(sel);
      sel->spu=Openselpopup(sel);
   }
   return AMR_ACTIVE;
}

static long Handleinputpopup(struct Select *sel,struct Aminput *ami)
{  return AMR_REUSE;
}

static long Goinactivepopup(struct Select *sel)
{  return 0;
}

static void Processselpopup(void)
{  struct Selpopup *spu,*next;
   struct Select *sel;
   ULONG result;
   WORD code;
   for(spu=selpopups.first;spu->next;spu=next)
   {  next=spu->next;
      while((result=RA_HandleInput(spu->cawin,&code))!=WMHI_LASTMSG)
      {  switch(result&WMHI_CLASSMASK)
         {  case WMHI_INACTIVE:
               spu->flags|=SPUF_DONE;
               break;
            case WMHI_GADGETUP:
               {  long x=Getvalue(spu->listgad,LISTBROWSER_MouseX);
                  long y=Getvalue(spu->listgad,LISTBROWSER_MouseY);
                  if(x>=0 && y>=0 && x<spu->window->Width && y<spu->window->Height)
                  {  short selected=Getvalue(spu->listgad,LISTBROWSER_Selected);
                     struct Coords *coo=Clipcoords(spu->select->cframe,NULL);
                     Selectpopup(spu->select,coo,selected);
                     Unclipcoords(coo);
                  }
                  spu->flags|=SPUF_DONE;
               }
               break;
            case WMHI_VANILLAKEY:
               if((result&WMHI_GADGETMASK)==27) spu->flags|=SPUF_DONE;
               break;
            case WMHI_MOUSEBUTTONS:
               if(code==IECODE_RBUTTON)
               {  spu->flags|=SPUF_DONE;
               }
               break;
         }
      }
      if(spu->flags&SPUF_DONE)
      {  sel=spu->select;
         if(spu->inactx>=0 && spu->inacty>=0)
         {  struct Coords coords={0};
            struct Window *win=(struct Window *)Agetattr(sel->win,AOWIN_Window);
            long x,y;
            if(win)
            {  Framecoords(sel->cframe,&coords);
               x=spu->inactx-win->LeftEdge-coords.dx-sel->aox;
               y=spu->inacty-win->TopEdge-coords.dy-sel->aoy;
               if(x>=0 && x<sel->aow && y>=0 && y<sel->aoh)
               {  /* Popup was closed by clicking in the gadget */
                  sel->flags|=SELF_IGNORECLICK;
               }
            }
         }
         Closeselpopup(spu);
         if(Selectedoption(sel)!=sel->wasselected)
         {  if(sel->onchange || AWebJSBase)
            {  Runjavascriptwith(sel->cframe,awebonchange,
                  &sel->jobject,sel->form);
            }
         }
      }
   }
}

/*------------------------------------------------------------------------*/

/* Render the nth option. x,y is inner list left,top. */
static void Renderlistitem(struct Select *sel,struct Coords *coo,struct Option *opt,
   long x,long y,long n)
{  long w;
   struct RastPort *rp=coo->rp;
   struct DrawInfo *drinfo=NULL;
   struct TextFont *font=Getselectfont(sel);
   Agetattrs(Aweb(),
      AOAPP_Drawinfo,&drinfo,
      TAG_END);
   y+=(n-sel->top)*sel->itemh;
   w=sel->listw-2*bevelw;
   if(sel->flags&SELF_MULTIPLE)
   {  SetAPen(rp,0);
      RectFill(rp,x,y,x+w-1,y+sel->itemh-1);
      x+=2;
      if(opt->flags&OPTF_SELECTED)
      {  /* Center checkmark vertically with text */
         long checky=y+(sel->itemh-checkh)/2;
         DrawImageState(rp,check,x,checky,IDS_NORMAL,drinfo);
      }
      x+=2+checkw;
      SetAPen(rp,coo->dri->dri_Pens[TEXTPEN]);
   }
   else
   {  SetAPen(rp,coo->dri->dri_Pens[(opt->flags&OPTF_SELECTED)?FILLPEN:BACKGROUNDPEN]);
      RectFill(rp,x,y,x+w-1,y+sel->itemh-1);
      x+=2;
      SetAPen(rp,coo->dri->dri_Pens[(opt->flags&OPTF_SELECTED)?FILLTEXTPEN:TEXTPEN]);
   }
   SetFont(rp,font);
   SetSoftStyle(rp,0,0x0f);
   Move(rp,x,y+rp->TxBaseline);
   Text(rp,opt->text,strlen(opt->text));
}

static void Renderlistcontents(struct Select *sel,struct Coords *coo)
{  long x,y,i;
   struct Option *opt;
   coo=Clipcoords(sel->cframe,coo);
   if(coo)
   {  x=sel->aox+coo->dx+bevelw;
      y=sel->aoy+coo->dy+bevelh;
      for(i=0,opt=sel->options.first;i<sel->top;i++,opt=opt->next);
      for(i=0;i<sel->size;i++,opt=opt->next)
      {  Renderlistitem(sel,coo,opt,x,y,i+sel->top);
      }
   }
   Unclipcoords(coo);
}

static void Renderlist(struct Select *sel,struct Coords *coo)
{  struct RastPort *rp=coo->rp;
   struct ColorMap *colormap=NULL;
   struct DrawInfo *drinfo=NULL;
   Agetattrs(Aweb(),
      AOAPP_Colormap,&colormap,
      AOAPP_Drawinfo,&drinfo,
      TAG_END);
   SetAttrs(bevel,
      IA_Width,sel->listw,
      IA_Height,sel->aoh,
      BEVEL_FillPen,~0,
      BEVEL_ColorMap,colormap,
      REACTION_SpecialPens,sel->capens,
      TAG_END);
   DrawImageState(rp,bevel,sel->aox+coo->dx,sel->aoy+coo->dy,IDS_NORMAL,drinfo);
   Renderlistcontents(sel,coo);
   if(sel->flags&SELF_SCROLLER)
   {  Arender(sel->scroll,coo,0,0,AMRMAX,AMRMAX,0,NULL);
   }
}

static long Hittestlist(struct Select *sel,struct Amhittest *amh)
{  long result;
   struct Coords coords={0};
   long x,y;
   Framecoords(sel->cframe,&coords);
   x=amh->xco-coords.dx-sel->aox;
   y=amh->yco-coords.dy-sel->aoy;
   if((sel->flags&SELF_SCROLLER)
   && x>=sel->listw && x<sel->aow && y>=0 && y<sel->aoh)
   {  result=AmethodA(sel->scroll,amh);
   }
   else if(x>=bevelw && x<sel->listw-bevelw && y>=bevelh && y<sel->aoh-bevelh)
   {  if(amh->oldobject==sel)
      {  result=AMHR_OLDHIT;
      }
      else
      {  result=AMHR_NEWHIT;
         if(amh->amhr)
         {  amh->amhr->object=sel;
         }
      }
   }
   return result;
}

/* Select the nth option, and deselect the previously selected one. Render
   all changes. */
static void Selectoption(struct Select *sel,struct Coords *coo,long n)
{  struct Option *opt;
   long i;
   for(i=0,opt=sel->options.first;opt->next;i++,opt=opt->next)
   {  if(i==n)
      {  if(opt->flags&OPTF_SELECTED) break;    /* No change */
         opt->flags|=OPTF_SELECTED;
         if(i>=sel->top && i<sel->top+sel->size)
         {  Renderlistitem(sel,coo,opt,sel->aox+coo->dx+bevelw,sel->aoy+coo->dy+bevelh,i);
         }
      }
      else if(opt->flags&OPTF_SELECTED)
      {  opt->flags&=~OPTF_SELECTED;
         if(i>=sel->top && i<sel->top+sel->size)
         {  Renderlistitem(sel,coo,opt,sel->aox+coo->dx+bevelw,sel->aoy+coo->dy+bevelh,i);
         }
      }
   }
}

static long Goactivelist(struct Select *sel,struct Amgoactive *amg)
{  long result=0;
   struct Coords *coo=NULL;
   long n;
   struct Option *opt;
   sel->flags&=~SELF_CHANGED;
   sel->wasselected=Selectedoption(sel);
   if(coo=Clipcoords(sel->cframe,coo))
   {  n=sel->top+(amg->imsg->MouseY-coo->dy-sel->aoy-bevelh)/sel->itemh;
      if(sel->flags&SELF_MULTIPLE)
      {  if(opt=Findoption(sel,n))
         {  opt->flags^=OPTF_SELECTED;
            Renderlistitem(sel,coo,opt,sel->aox+coo->dx+bevelw,sel->aoy+coo->dy+bevelh,n);
            result=AMR_CHANGED;
            sel->flags|=SELF_CHANGED;
         }
      }
      else
      {  Selectoption(sel,coo,n);
         result=AMR_ACTIVE;
      }
   }
   Unclipcoords(coo);
   
   return result;
}

/* Handleinput is only issued for a single select list */
static long Handleinputlist(struct Select *sel,struct Aminput *ami)
{  struct Coords *coo=NULL;
   long result=AMR_REUSE;
   long n;
   if(ami->imsg)
   {  switch(ami->imsg->Class)
      {  case IDCMP_MOUSEMOVE:
            if(coo=Clipcoords(sel->cframe,coo))
            {  n=sel->top+(ami->imsg->MouseY-coo->dy-sel->aoy-bevelh)/sel->itemh;
               if(n<sel->top) n=sel->top;
               if(n>=sel->top+sel->size) n=sel->top+sel->size-1;
               Selectoption(sel,coo,n);
               result=AMR_ACTIVE;
            }
            Unclipcoords(coo);
            break;
         case IDCMP_MOUSEBUTTONS:
            result=AMR_NOREUSE;
            break;
         case IDCMP_RAWKEY:
         case IDCMP_INTUITICKS:
            result=AMR_ACTIVE;
            break;
      }
   }
   return result;
}

/*------------------------------------------------------------------------*/

/* Get or set the text property */
static BOOL Propertytext(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Option *opt=vd->hookdata;
   UBYTE *text;
   if(opt)
   {  switch(vd->code)
      {  case VHC_SET:
            text=Jtostring(vd->jc,vd->value);
            if(opt->text) FREE(opt->text);
            opt->text=Dupstr(text,-1);
            Asetattrs(opt->sel->parent,AOBJ_Changedchild,opt->sel,TAG_END);
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,opt->text);
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Get or set the value property */
static BOOL Propertyvalue(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Option *opt=vd->hookdata;
   UBYTE *value;
   if(opt)
   {  switch(vd->code)
      {  case VHC_SET:
            value=Jtostring(vd->jc,vd->value);
            if(opt->value) FREE(opt->value);
            opt->value=Dupstr(value,-1);
            result=TRUE;
            break;
         case VHC_GET:
            if(opt->value) value=opt->value;
            else value=opt->text;
            Jasgstring(vd->jc,vd->value,value);
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Get the index property */
static BOOL Propertyindex(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Option *opt=vd->hookdata,*o=NULL;
   struct Select *sel;
   long n;
   if(opt)
   {  switch(vd->code)
      {  case VHC_SET:
            /* Property is read-only */
            result=TRUE;
            break;
         case VHC_GET:
            if(sel=opt->sel)
            {  for(n=0,o=sel->options.first;o->next;n++,o=o->next)
               {  if(o==opt)
                  {  break;
                  }
               }
            }
            Jasgnumber(vd->jc,vd->value,(o && o->next)?n:-1);
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Get or set the selected property */
static BOOL Propertyselected(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Option *opt=vd->hookdata,*o;
   struct Select *sel;
   long n;
   BOOL selected;
   struct Coords *coo=NULL;
   if(opt)
   {  sel=opt->sel;
      switch(vd->code)
      {  case VHC_SET:
            if(sel)
            {  selected=Jtoboolean(vd->jc,vd->value);
               for(n=0,o=sel->options.first;o->next && o!=opt;n++,o=o->next);
               if(coo=Clipcoords(sel->cframe,coo))
               {  if(sel->flags&SELF_MULTIPLE)
                  {  SETFLAG(opt->flags,OPTF_SELECTED,selected);
                     if(n>=sel->top && n<sel->top+sel->size)
                     {  Renderlistitem(sel,coo,opt,sel->aox+coo->dx+bevelw,sel->aoy+coo->dy+bevelh,n);
                     }
                  }
                  else
                  {  if(selected)
                     {  if(sel->flags&SELF_POPUP) Selectpopup(sel,coo,n);
                        else Selectoption(sel,coo,n);
                     }
                  }
               }
               Unclipcoords(coo);
            }
            result=TRUE;
            break;
         case VHC_GET:
            Jasgboolean(vd->jc,vd->value,BOOLVAL(opt->flags&OPTF_SELECTED));
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Hook called when JS option object is disposed */
static void Seljdisposeoption(struct Option *opt)
{  if(opt)
   {  opt->jobject=NULL;
      if(!(opt->flags&OPTF_INLIST))
      {  Disposeoption(opt);
      }
   }
}

/* Actually a property hook function of the select object */
static BOOL Propertyoptionelt(struct Varhookdata *vd);

/* Create a JS object for this option */
static void Makejoption(struct Jcontext *jc,struct Select *sel,struct Option *opt,long i)
{  UBYTE buf[16];
   struct Jvar *jv;
   if(!opt->jobject)
   {  opt->jobject=Newjobject(jc);
   }
   if(opt->jobject)
   {  Setjobject(opt->jobject,NULL,opt,Seljdisposeoption);
      sprintf(buf,"%d",i);
      if(jv=Jproperty(jc,sel->jobject,buf))
      {  Setjproperty(jv,Propertyoptionelt,sel);
         Jasgobject(jc,jv,opt->jobject);
      }
      if(jv=Jproperty(jc,opt->jobject,"defaultSelected"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgboolean(jc,jv,BOOLVAL(opt->flags&OPTF_INITIAL));
      }
      if(jv=Jproperty(jc,opt->jobject,"text"))
      {  Setjproperty(jv,Propertytext,opt);
      }
      if(jv=Jproperty(jc,opt->jobject,"value"))
      {  Setjproperty(jv,Propertyvalue,opt);
      }
      if(jv=Jproperty(jc,opt->jobject,"index"))
      {  Setjproperty(jv,Propertyindex,opt);
      }
      if(jv=Jproperty(jc,opt->jobject,"selected"))
      {  Setjproperty(jv,Propertyselected,opt);
      }
      /* opt->jobject is stored on sel->jobject via Jasgobject above; do not Freejobject. */
   }
}

/* Option() constructor. This will be called after creation of a new
 * object to turn it into an Option. */
static void Optionconstructor(struct Jcontext *jc)
{  struct Jobject *jthis=Jthis(jc);
   struct Jvar *jv;
   UBYTE *p;
   struct Option *opt;
   if(opt=ALLOCSTRUCT(Option,1,MEMF_CLEAR))
   {  opt->jobject=jthis;
      if(jv=Jfargument(jc,0))
      {  p=Jtostring(jc,jv);
         if(!p) p="";
         opt->text=Dupstr(p,-1);
      }
      if(jv=Jfargument(jc,1))
      {  p=Jtostring(jc,jv);
         if(p) opt->value=Dupstr(p,-1);
      }
      Setjobject(jthis,NULL,opt,Seljdisposeoption);
      if(jv=Jproperty(jc,opt->jobject,"defaultSelected"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgboolean(jc,jv,FALSE);
      }
      if(jv=Jproperty(jc,opt->jobject,"text"))
      {  Setjproperty(jv,Propertytext,opt);
      }
      if(jv=Jproperty(jc,opt->jobject,"value"))
      {  Setjproperty(jv,Propertyvalue,opt);
      }
      if(jv=Jproperty(jc,opt->jobject,"index"))
      {  Setjproperty(jv,Propertyindex,opt);
      }
      if(jv=Jproperty(jc,opt->jobject,"selected"))
      {  Setjproperty(jv,Propertyselected,opt);
      }
   }
}


/*------------------------------------------------------------------------*/

/* Get or set the selectedIndex property (JS) */
static BOOL Propertyselectedindex(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Select *sel=vd->hookdata;
   struct Option *opt;
   long n,i;
   struct Coords *coo=NULL;
   if(sel)
   {  switch(vd->code)
      {  case VHC_SET:
            n=Jtonumber(vd->jc,vd->value);
            if(n>=0 && n<sel->nroptions)
            {  if(coo=Clipcoords(sel->cframe,coo))
               {  if(sel->flags&SELF_MULTIPLE)
                  {  for(i=0,opt=sel->options.first;opt->next;i++,opt=opt->next)
                     {  if((i==n) != BOOLVAL(opt->flags&OPTF_SELECTED))
                        {  SETFLAG(opt->flags,OPTF_SELECTED,i==n);
                           if(i>=sel->top && i<sel->top+sel->size)
                           {  Renderlistitem(sel,coo,opt,
                                 sel->aox+coo->dx+bevelw,sel->aoy+coo->dy+bevelh,i);
                           }
                        }
                     }
                  }
                  else
                  {  Selectoption(sel,coo,n);
                  }
               }
               Unclipcoords(coo);
            }
            result=TRUE;
            break;
         case VHC_GET:
            n=Selectedoption(sel);
            Jasgnumber(vd->jc,vd->value,n);
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Set or get the select.length property (JS). */
static BOOL Propertylength(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Select *sel=vd->hookdata;
   struct Option *opt,*next;
   long length,i;
   UBYTE buf[16];
   struct Jvar *jv;
   if(sel)
   {  switch(vd->code)
      {  case VHC_SET:
            length=Jtonumber(vd->jc,vd->value);
            if(length<sel->nroptions)
            {  /* Remove excess options */
               for(i=0,opt=sel->options.first;opt->next;i++,opt=next)
               {  next=opt->next;
                  if(i>=length)
                  {  REMOVE(opt);
                     Sdisposeoption(opt);
                  }
               }
               /* Clear out excess JS options */
               for(i=length;i<sel->nroptions;i++)
               {  sprintf(buf,"%d",i);
                  if(jv=Jproperty(vd->jc,sel->jobject,buf))
                  {  Jasgobject(vd->jc,jv,NULL);
                  }
               }
               sel->nroptions=length;
               sel->flags|=SELF_SIZECHANGED;
               Asetattrs(sel->parent,AOBJ_Changedchild,sel,TAG_END);
            }
            else if(length>sel->nroptions)
            {  /* Add empty options */
               for(i=sel->nroptions;i<length;i++)
               {  if(opt=Addoption(sel,NULL,FALSE))
                  {  Addoptiontext(sel,opt,"");
                     Makejoption(vd->jc,sel,opt,i);
                  }
               }
               sel->nroptions=length;
               sel->flags|=SELF_SIZECHANGED;
               Asetattrs(sel->parent,AOBJ_Changedchild,sel,TAG_END);
            }
            result=TRUE;
            break;
         case VHC_GET:
            Jasgnumber(vd->jc,vd->value,sel->nroptions);
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Set an option array element */
static BOOL Propertyoptionelt(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Select *sel=vd->hookdata;
   struct Jobject *jo;
   struct Jvar *jv;
   struct Option *opt=NULL,*opta,*optb;
   UBYTE buf[16];
   long n,i,nroptions;
   if(sel)
   {  switch(vd->code)
      {  case VHC_SET:
            jo=Jtoobject(vd->jc,vd->value);
            if(!jo
            || ((Jdisposehook(jo)==Seljdisposeoption) && (opt=Jointernal(jo))))
            {  /* Set to either NULL or a valid Option object */
               nroptions=sel->nroptions;  /* old number of options */
               n=atoi(vd->name);          /* index number to set */
               opta=sel->options.last;    /* option to insert after */
               if(n<nroptions)
               {  /* Find old option to replace (remove) */
                  for(i=0,optb=sel->options.first;i<n && optb->next;i++,optb=optb->next);
                  if(optb->next)
                  {  opta=optb->prev;
                     REMOVE(optb);
                     Sdisposeoption(optb);
                     sel->nroptions--;
                  }
               }
               if(opt)
               {  /* Replace with, or add a new option */
                  if(n>nroptions)
                  {  /* Add empty options as necessary */
                     for(i=nroptions;i<n-1;i++)
                     {  if(opta=Addoption(sel,NULL,FALSE))
                        {  Addoptiontext(sel,opta,"");
                           Makejoption(vd->jc,sel,opta,i);
                        }
                     }
                  }
                  /* Add given option after (opta) */
                  if(opt->flags&OPTF_INLIST)
                  {  /* This option is already attached to a list; create duplicate */
                     if(optb=Addoption(sel,opt->value,FALSE))
                     {  Addoptiontext(sel,optb,opt->text);
                        /* Addoption() has insetred the option at the tail,
                         * remove it now and insert it in the right place. */
                        REMOVE(optb);
                        INSERT(&sel->options,optb,opta);
                     }
                  }
                  else
                  {  /* Option isn't used yet, just insert it. */
                     INSERT(&sel->options,opt,opta);
                     opt->flags|=OPTF_INLIST;
                     opt->sel=sel;
                     sel->nroptions++;
                  }
               }
               else if(n<nroptions)
               {  /* Replace with NULL, pack the JS list.
                   * (sel->nroptions) is already decremented. */
                  for(i=0,opt=sel->options.first;
                     i<sel->nroptions && opt->next;
                     i++,opt=opt->next)
                  {  if(i>=n) Makejoption(vd->jc,sel,opt,i);
                  }
                  /* Clear out old last element */
                  sprintf(buf,"%d",nroptions);
                  if(jv=Jproperty(vd->jc,sel->jobject,buf))
                  {  Jasgobject(vd->jc,jv,NULL);
                  }
               }
               if(sel->nroptions!=nroptions) sel->flags|=SELF_SIZECHANGED;
               Asetattrs(sel->parent,AOBJ_Changedchild,sel,TAG_END);
            }
            result=TRUE;
            break;
         case VHC_GET:
            /* Leave result FALSE so JS will use the actual property contents */
            break;
      }
   }
   return result;
}

static void Methodfocus(struct Jcontext *jc)
{
}

static void Methodblur(struct Jcontext *jc)
{
}

static void Methodtostring(struct Jcontext *jc)
{  struct Select *sel=Jointernal(Jthis(jc));
   struct Option *opt;
   struct Buffer buf={0};
   if(sel)
   {  Addtagstr(&buf,"<select",ATSF_NONE,0);
      if(sel->name) Addtagstr(&buf,"name",ATSF_STRING,sel->name);
      if(sel->orgsize) Addtagstr(&buf,"size",ATSF_NUMBER,sel->orgsize);
      if(sel->flags&SELF_MULTIPLE) Addtagstr(&buf,"multiple",ATSF_NONE,0);
      Addtagstr(&buf,">\n",ATSF_NONE,0);
      for(opt=sel->options.first;opt->next;opt=opt->next)
      {  Addtagstr(&buf,"<option",ATSF_NONE,0);
         if(opt->value) Addtagstr(&buf,"value",ATSF_STRING,opt->value);
         if(opt->flags&OPTF_INITIAL) Addtagstr(&buf,"selected",ATSF_NONE,0);
         Addtobuffer(&buf,">",1);
         if(opt->text) Addtobuffer(&buf,opt->text,strlen(opt->text));
         Addtobuffer(&buf,"\n",1);
      }
      Addtobuffer(&buf,"</select>",10);   /* Including nullbyte */
      Jasgstring(jc,NULL,buf.buffer);
      Freebuffer(&buf);
   }
}

/*------------------------------------------------------------------------*/

/* Hook called when element is added to options array */
static BOOL Seljaddoption(struct Objhookdata *od)
{  BOOL result=FALSE;
   struct Select *sel=Jointernal(od->jo);
   struct Option *opt;
   UBYTE buf[16];
   long n,i;
   if(sel)
   {  switch(od->code)
      {  case OHC_ADDPROPERTY:
            n=atoi(od->name);
            sprintf(buf,"%d",n);
            if(STREQUAL(buf,od->name) && n>=sel->nroptions)
            {  /* Real numeric name, add empty options upto and including this number */
               for(i=sel->nroptions;i<=n;i++)
               {  if(opt=Addoption(sel,NULL,FALSE))
                  {  Addoptiontext(sel,opt,"");
                     Makejoption(od->jc,sel,opt,i);
                     /* This creates the property too */
                  }
               }
               sel->flags|=SELF_SIZECHANGED;
               Asetattrs(sel->parent,AOBJ_Changedchild,sel,TAG_END);
               result=TRUE;
            }
            break;
      }
   }
   return result;
}

/*------------------------------------------------------------------------*/

static long Measureselect(struct Select *sel,struct Ammeasure *amm)
{  struct Option *opt;
   struct TextFont *font=Getselectfont(sel);
   if(sel->flags&SELF_COMPLETE)
   {  if(sel->flags&SELF_SIZECHANGED)
      {  /* re-compute size etc. */
         Completeselect(sel);
         sel->flags&=~SELF_SIZECHANGED;
      }
      SetFont(mrp,font);
      SetSoftStyle(mrp,0,0x0f);
      sel->width=0;
      for(opt=sel->options.first;opt->next;opt=opt->next)
      {  opt->width=Textlength(mrp,opt->text,strlen(opt->text));
         if(opt->width>sel->width) sel->width=opt->width;
      }
      if(sel->flags&SELF_POPUP)
      {  sel->aow=sel->width+24+2*bevelw;
         sel->aoh=MAX(12,mrp->TxHeight+2)+2*bevelh;
      }
      else
      {  /* Add extra padding for variable-width fonts */
         sel->listw=sel->width+8+2*bevelw;
         if(sel->flags&SELF_MULTIPLE) sel->listw+=checkw+2;
         sel->aow=sel->listw;
         if(sel->flags&SELF_SCROLLER) sel->aow+=Agetattr(sel->scroll,AOBJ_Width);
         sel->aoh=sel->size*sel->itemh+2*bevelh;
      }
      AmethodasA(AOTP_FIELD,sel,amm);
   }
   return 0;
}

static long Alignselect(struct Select *sel,struct Amalign *ama)
{  long result=AmethodasA(AOTP_FIELD,sel,ama);
   if(sel->scroll)
   {  Asetattrs(sel->scroll,
         AOBJ_Left,sel->aox+sel->listw,
         AOBJ_Top,sel->aoy,
         AOBJ_Height,sel->aoh,
         TAG_END);
   }
   return result;
}

static long Moveselect(struct Select *sel,struct Ammove *amm)
{  long result=AmethodasA(AOTP_FIELD,sel,amm);
   if(sel->scroll)
   {  AmethodA(sel->scroll,amm);
   }
   return result;
}

static long Renderselect(struct Select *sel,struct Amrender *amr)
{  struct Coords *coo,coords={0};
   BOOL clip=FALSE;
   ULONG clipkey=NULL;
   if(!(coo=amr->coords))
   {  Framecoords(sel->cframe,&coords);
      coo=&coords;
      clip=TRUE;
   }
   if(coo->rp && (sel->flags&SELF_COMPLETE) && (sel->eltflags&ELTF_ALIGNED))
   {  if(clip) clipkey=Clipto(coo->rp,coo->minx,coo->miny,coo->maxx,coo->maxy);
      if(sel->flags&SELF_POPUP)
      {  Renderpopup(sel,coo);
      }
      else
      {  Renderlist(sel,coo);
      }
      if(clip) Unclipto(clipkey);
   }
   return 0;
}

static long Setselect(struct Select *sel,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   BOOL newoption=FALSE,optselected=FALSE;
   UBYTE *optvalue=NULL;
   result=Amethodas(AOTP_FIELD,sel,AOM_SET,ams->tags);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Pool:
            sel->pool=(void *)tag->ti_Data;
            break;
         case AOBJ_Frame:
            if(sel->scroll) Asetattrs(sel->scroll,AOBJ_Frame,tag->ti_Data,TAG_END);
            if(!tag->ti_Data)
            {  Closeselpopup(sel->spu);
               Freejselect(sel);
            }
            break;
         case AOBJ_Cframe:
            if(sel->scroll) Asetattrs(sel->scroll,AOBJ_Cframe,tag->ti_Data,TAG_END);
            break;
         case AOBJ_Window:
            if(tag->ti_Data)
            {  sel->capens=(void *)Agetattr((void *)tag->ti_Data,AOWIN_Specialpens);
               if(sel->flags&SELF_DEFERCOMPLETE) Completeselect(sel);
            }
            else
            {  Closeselpopup(sel->spu);
               sel->capens=NULL;
            }
            if(sel->scroll) Asetattrs(sel->scroll,AOBJ_Window,tag->ti_Data,TAG_END);
            break;
         case AOBJ_Layoutparent:
            sel->parent=(void *)tag->ti_Data;
            break;
         case AOSEL_Size:
            sel->size=MAX(1,tag->ti_Data);
            sel->orgsize=sel->size;
            break;
         case AOSEL_Multiple:
            if(tag->ti_Data) sel->flags|=SELF_MULTIPLE;
            else sel->flags&=~SELF_MULTIPLE;
            break;
         case AOSEL_Complete:
            if(tag->ti_Data)
            {  Completeselect(sel);
            }
            break;
         case AOSEL_Option:
            newoption=TRUE;
            break;
         case AOSEL_Optionvalue:
            optvalue=(UBYTE *)tag->ti_Data;
            break;
         case AOSEL_Selected:
            optselected=BOOLVAL(tag->ti_Data);
            break;
         case AOSEL_Optiontext:
            if(sel->options.last->prev && tag->ti_Data)
            {  Addoptiontext(sel,sel->options.last,(UBYTE *)tag->ti_Data);
            }
            break;
         case AOFLD_Reset:
            if(tag->ti_Data) Resetselect(sel);
            break;
         case AOSEL_Listtop:
            if(!(sel->flags&SELF_POPUP))
            {  if(tag->ti_Data!=sel->top)
               {  sel->top=tag->ti_Data;
                  Renderlistcontents(sel,NULL);
               }
            }
            break;
         case AOFLD_Onchange:
            if(sel->onchange) FREE(sel->onchange);
            sel->onchange=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onfocus:
            if(sel->onfocus) FREE(sel->onfocus);
            sel->onfocus=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Onblur:
            if(sel->onblur) FREE(sel->onblur);
            sel->onblur=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
      }
   }
   if(newoption)
   {  Addoption(sel,optvalue,optselected);
   }
   return result;
}

static long Getselect(struct Select *sel,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=AmethodasA(AOTP_FIELD,sel,ams);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFLD_Value:
            if(!(sel->flags&SELF_MULTIPLE))
            {  PUTATTR(tag,Singlevalue(sel));
            }
            break;
         case AOFLD_Multivalue:
            if(sel->flags&SELF_MULTIPLE)
            {  PUTATTR(tag,Multivalue(sel));
            }
            break;
      }
   }
   return result;
}

static struct Select *Newselect(struct Amset *ams)
{  struct Select *sel;
   if(sel=Allocobject(AOTP_SELECT,sizeof(struct Select),ams))
   {  NEWLIST(&sel->options);
      sel->orgsize=sel->size=1;
      Setselect(sel,ams);
   }
   return sel;
}

static long Hittestselect(struct Select *sel,struct Amhittest *amh)
{  long result;
   if(sel->flags&SELF_POPUP)
   {  result=Hittestpopup(sel,amh);
   }
   else
   {  result=Hittestlist(sel,amh);
   }
   return result;
}

static long Goactiveselect(struct Select *sel,struct Amgoactive *amg)
{  long result;
   if(sel->flags&SELF_POPUP)
   {  result=Goactivepopup(sel,amg);
   }
   else
   {  result=Goactivelist(sel,amg);
   }
   return result;
}

static long Handleinputselect(struct Select *sel,struct Aminput *ami)
{  long result;
   if(sel->flags&SELF_POPUP)
   {  result=Handleinputpopup(sel,ami);
   }
   else
   {  result=Handleinputlist(sel,ami);
   }
   return result;
}

static long Goinactiveselect(struct Select *sel)
{  if(!(sel->flags&SELF_POPUP))
   {  if((sel->flags&SELF_CHANGED) || Selectedoption(sel)!=sel->wasselected)
      {  if(sel->onchange || AWebJSBase)
         {  Runjavascriptwith(sel->cframe,awebonchange,&sel->jobject,sel->form);
         }
      }
   }
   return 0;
}

static long Dragtestselect(struct Select *sel,struct Amdragtest *amd)
{  return AMDR_STOP;
}

static long Jsetupselect(struct Select *sel,struct Amjsetup *amj)
{  struct Jvar *jv;
   UBYTE *p;
   struct Option *opt;
   long i;
   AmethodasA(AOTP_FIELD,sel,amj);
   if(sel->jobject)
   {  Jsetobjasfunc(sel->jobject,TRUE);
      Setjobject(sel->jobject,Seljaddoption,sel,NULL);
      if(jv=Jproperty(amj->jc,sel->jobject,"type"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         if(sel->flags&SELF_MULTIPLE) p="select-multiple";
         else p="select-one";
         Jasgstring(amj->jc,jv,p);
      }
      if(jv=Jproperty(amj->jc,sel->jobject,"selectedIndex"))
      {  Setjproperty(jv,Propertyselectedindex,sel);
      }
      if(jv=Jproperty(amj->jc,sel->jobject,"length"))
      {  Setjproperty(jv,Propertylength,sel);
      }
      if(jv=Jproperty(amj->jc,sel->jobject,"options"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgobject(amj->jc,jv,sel->jobject);
      }
      Addjfunction(amj->jc,sel->jobject,"focus",Methodfocus,NULL);
      Addjfunction(amj->jc,sel->jobject,"blur",Methodblur,NULL);
      Addjfunction(amj->jc,sel->jobject,"toString",Methodtostring,NULL);
      Jaddeventhandler(amj->jc,sel->jobject,"onfocus",sel->onfocus);
      Jaddeventhandler(amj->jc,sel->jobject,"onblur",sel->onblur);
      Jaddeventhandler(amj->jc,sel->jobject,"onchange",sel->onchange);
   }
   for(i=0,opt=sel->options.first;opt->next;i++,opt=opt->next)
   {  Makejoption(amj->jc,sel,opt,i);
   }
   return 0;
}

static void Disposeselect(struct Select *sel)
{  void *p;
   if(sel->spu) Closeselpopup(sel->spu);
   while(p=REMHEAD(&sel->options)) Sdisposeoption(p);
   if(sel->scroll) Adisposeobject(sel->scroll);
   if(sel->multivalue) FREE(sel->multivalue);
   if(sel->onblur) FREE(sel->onblur);
   if(sel->onchange) FREE(sel->onchange);
   if(sel->onfocus) FREE(sel->onfocus);
   Amethodas(AOTP_FIELD,sel,AOM_DISPOSE);
}

static void Deinstallselect(void)
{  if(bevel) DisposeObject(bevel);
   if(check) DisposeObject(check);
}

static long Dispatch(struct Select *sel,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newselect((struct Amset *)amsg);
         break;
      case AOM_UPDATE:
      case AOM_SET:
         result=Setselect(sel,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getselect(sel,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measureselect(sel,(struct Ammeasure *)amsg);
         break;
      case AOM_ALIGN:
         result=Alignselect(sel,(struct Amalign *)amsg);
         break;
      case AOM_MOVE:
         result=Moveselect(sel,(struct Ammove *)amsg);
         break;
      case AOM_RENDER:
         result=Renderselect(sel,(struct Amrender *)amsg);
         break;
      case AOM_HITTEST:
         result=Hittestselect(sel,(struct Amhittest *)amsg);
         break;
      case AOM_GOACTIVE:
         result=Goactiveselect(sel,(struct Amgoactive *)amsg);
         break;
      case AOM_HANDLEINPUT:
         result=Handleinputselect(sel,(struct Aminput *)amsg);
         break;
      case AOM_GOINACTIVE:
         result=Goinactiveselect(sel);
         break;
      case AOM_DRAGTEST:
         result=Dragtestselect(sel,(struct Amdragtest *)amsg);
         break;
      case AOM_JSETUP:
         result=Jsetupselect(sel,(struct Amjsetup *)amsg);
         break;
      case AOM_DISPOSE:
         Disposeselect(sel);
         break;
      case AOM_DEINSTALL:
         Deinstallselect();
         break;
      default:
         result=AmethodasA(AOTP_FIELD,sel,amsg);
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installselect(void)
{  NEWLIST(&selpopups);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_SELECT,Dispatch)) return FALSE;
   if(!(bevel=BevelObject,
      BEVEL_Style,BVS_BUTTON,
      End)) return FALSE;
   GetAttr(BEVEL_VertSize,bevel,(ULONG *)&bevelw);
   GetAttr(BEVEL_HorizSize,bevel,(ULONG *)&bevelh);
   if(!(check=GlyphObject,
      GLYPH_Glyph,GLYPH_CHECKMARK,
      End)) return FALSE;
   return TRUE;
}

BOOL Initselect(void)
{  struct TextFont *font;
   /* Use screen font for initialization - this is called before elements exist */
   font=(struct TextFont *)Agetattr(Aweb(),AOAPP_Screenfont);
   checkh=font->tf_YSize;
   checkw=2*font->tf_XSize;
   SetAttrs(check,
      IA_Width,checkw,
      IA_Height,checkh,
      TAG_END);
   return TRUE;
}

void Addoptionconstructor(struct Jcontext *jc,struct Jobject *parent)
{  struct Jobject *jo,*proto;
   if(jo=Addjfunction(jc,parent,"Option",Optionconstructor,"text","value",NULL))
   {  if(proto=Newjobject(jc))
      {  /* All properties have hooks so can't be set here */
         Jsetprototype(jc,jo,proto);
         /* proto remains owned via constructor prototype link; do not Freejobject. */
      }
   }
}

