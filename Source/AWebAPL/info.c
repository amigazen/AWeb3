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

/* info.c - AWeb info window object */

#include "aweb.h"
#include "info.h"
#include "application.h"
#include "url.h"
#include "window.h"
#include "frame.h"
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <classes/window.h>
#include <gadgets/listbrowser.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <images/label.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <reaction/reaction_class.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/listbrowser.h>
#include <proto/button.h>
#include <proto/layout.h>
#include <proto/window.h>
#include <proto/label.h>

#ifndef DEMOVERSION

/*------------------------------------------------------------------------*/

struct Info
{  struct Aobject object;
   void *cawin;                     /* The Reaction Window object. */
void *layout,*urlgad,*listgad;   /* The Reaction gadget objects. */
   struct Window *window;           /* The Intuition Window. */
   struct List textlist;            /* List of LB entries */
   void *url;                       /* URL this info is for */
   void *frame;                     /* Frame to notify wneh want to close */
   ULONG windowkey;
   struct DrawInfo *dri;
   USHORT flags;
   struct ColumnInfo cols[2];
   UBYTE *title;
};

static LIST(Info) infos;

static struct Hook renderhook;

static short lastx,lasty,lastw,lasth;

#define IGID_LINK 1

/*------------------------------------------------------------------------*/

static ULONG __saveds __asm Renderhook(register __a1 struct LBDrawMsg *msg,
   register __a2 struct Node *n)
{  struct RastPort *rp=msg->lbdm_RastPort;
   short pen1,pen2;
   short x1,x2,y1,y2;
   if(msg->lbdm_MethodID!=LV_DRAW) return LBCB_UNKNOWN;
   pen1=msg->lbdm_DrawInfo->dri_Pens[SHADOWPEN];
   pen2=msg->lbdm_DrawInfo->dri_Pens[SHINEPEN];
   x1=msg->lbdm_Bounds.MinX;
   y1=(msg->lbdm_Bounds.MinY+msg->lbdm_Bounds.MaxY)/2;
   x2=msg->lbdm_Bounds.MaxX;
   y2=y1+1;
   SetAPen(rp,pen1);
   Move(rp,x1,y2);
   Draw(rp,x1,y1);
   Draw(rp,x2,y1);
   SetAPen(rp,pen2);
   Draw(rp,x2,y2);
   Draw(rp,x1+1,y2);
   return LBCB_OK;
}

/*------------------------------------------------------------------------*/

/* Add a line of text */
static struct Node *Addline(struct Info *inf,UBYTE *text,void *link,BOOL header)
{  struct Node *node;
   if(header && !ISEMPTY(&inf->textlist))
   {  if(node=AllocListBrowserNode(1,
         LBNA_Flags,LBFLG_READONLY,
         LBNCA_RenderHook,&renderhook,
         LBNCA_HookHeight,4,
         TAG_END))
      {  AddTail(&inf->textlist,node);
      }
   }
   if(node=AllocListBrowserNode(1,
      LBNA_UserData,link,
      LBNA_Flags,LBFLG_CUSTOMPENS|(link?0:LBFLG_READONLY),
      LBNCA_CopyText,TRUE,
      LBNCA_Text,text,
      LBNCA_FGPen,inf->dri->dri_Pens[header?HIGHLIGHTTEXTPEN:TEXTPEN],
      TAG_END))
   {  AddTail(&inf->textlist,node);
   }
   return node;
}

/* Open a link */
static void Infolink(struct Info *inf)
{  struct Node *node;
   void *url=NULL,*win;
   if(node=(struct Node *)Getvalue(inf->listgad,LISTBROWSER_SelectedNode))
   {  GetListBrowserNodeAttrs(node,LBNA_UserData,&url,TAG_END);
      if(url)
      {  win=Findwindow(inf->windowkey);
         if(win)
         {  Inputwindoc(win,url,NULL,NULL);
         }
      }
   }
}

/* Process input events */
static void Processinfo(void)
{  struct Info *inf,*next;
   BOOL done,link;
   ULONG result;
   for(inf=infos.first;inf->next;inf=next)
   {  next=inf->next;
      if(inf->cawin)
      {  done=FALSE;
         link=FALSE;
         while(!done && (result=RA_HandleInput(inf->cawin,NULL))!=WMHI_LASTMSG)
         {  switch(result&WMHI_CLASSMASK)
            {  case WMHI_CLOSEWINDOW:
                  done=TRUE;
                  break;
               case WMHI_GADGETUP:
                  switch(result&WMHI_GADGETMASK)
                  {  case IGID_LINK:
                        /* Only set flag here because following the link now
                         * may dispose us while we are looping */
                        link=TRUE;
                        break;
                  }
                  break;
               case WMHI_RAWKEY:
                  switch(result&WMHI_GADGETMASK)
                  {  case 0x45:  /* esc */
                        done=TRUE;
                        break;
                  }
                  break;
            }
         }
         if(done)
         {  Asetattrs(inf->frame,AOFRM_Info,FALSE,TAG_END);
         }
         else if(link)
         {  Infolink(inf);
         }
      }
   }
}

/* Set new window title */
static void Settitle(struct Info *inf)
{  UBYTE *title,*newtitle;
   title=AWEBSTR(MSG_INFO_TITLE);
   if(inf->windowkey)
   {  if(newtitle=ALLOCTYPE(UBYTE,strlen(title)+20,0))
      {  sprintf(newtitle,"AWEB.%d %s",
            Agetattr(Findwindow(inf->windowkey),AOWIN_Windownr),title);
         if(inf->cawin)
         {  SetAttrs(inf->cawin,WA_Title,newtitle,TAG_END);
         }
         if(inf->title) FREE(inf->title);
         inf->title=newtitle;
      }
   }
}

/*------------------------------------------------------------------------*/

static long Setinfo(struct Info *inf,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *text=NULL;
   void *link=NULL;
   BOOL header=FALSE;
   struct Node *node;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOINF_Target:
            if(inf->listgad)
            {  Setgadgetattrs(inf->listgad,inf->window,NULL,
                  LISTBROWSER_Labels,~0,TAG_END);
            }
            while(node=RemHead(&inf->textlist)) FreeListBrowserNode(node);
            if(tag->ti_Data)
            {  Asetattrs((void *)tag->ti_Data,AOINF_Inquire,inf,TAG_END);
            }
            if(inf->listgad)
            {  Setgadgetattrs(inf->listgad,inf->window,NULL,
                  LISTBROWSER_Labels,&inf->textlist,TAG_END);
            }
            break;
         case AOINF_Text:
            text=(UBYTE *)tag->ti_Data;
            break;
         case AOINF_Header:
            header=tag->ti_Data;
            break;
         case AOINF_Link:
            link=(void *)tag->ti_Data;
            break;
         case AOINF_Windowkey:
            inf->windowkey=tag->ti_Data;
            Settitle(inf);
            break;
         case AOINF_Url:
            inf->url=(void *)tag->ti_Data;
            if(inf->urlgad)
            {  UBYTE *urlname=(UBYTE *)Agetattr(inf->url,AOURL_Url);
               Setgadgetattrs(inf->urlgad,inf->window,NULL,
                  GA_Text,urlname?urlname:NULLSTRING,TAG_END);
            }
            break;
         case AOINF_Frame:
            inf->frame=(void *)tag->ti_Data;
            break;
         case AOAPP_Screenvalid:
            if(!tag->ti_Data)
            {  if(inf->cawin)
               {  DisposeObject(inf->cawin);
                  inf->cawin=NULL;
               }
               return 0;
            }
            break;
      }
   }
   if(text)
   {  Addline(inf,text,link,header);
   }
   return 0;
}

static void Disposeinfo(struct Info *inf)
{  struct Node *node;
   if(inf->cawin)
   {  if(inf->window)
      {  lastx=inf->window->LeftEdge;
         lasty=inf->window->TopEdge;
         lastw=inf->window->Width-inf->window->BorderLeft-inf->window->BorderRight;
         lasth=inf->window->Height-inf->window->BorderTop-inf->window->BorderBottom;
      }
      DisposeObject(inf->cawin);
   }
   while(node=RemHead(&inf->textlist))
   {  FreeListBrowserNode(node);
   }
   if(inf->title) FREE(inf->title);
   Aremchild(Aweb(),inf,AOREL_APP_USE_SCREEN);
   REMOVE(inf);
   Amethodas(AOTP_OBJECT,inf,AOM_DISPOSE);
}

static struct Info *Newinfo(struct Amset *ams)
{  struct Info *inf;
   UBYTE *urlname;
   struct Screen *screen;
   struct MsgPort *port;
   if(inf=Allocobject(AOTP_INFO,sizeof(struct Info),ams))
   {  NEWLIST(&inf->textlist);
      ADDTAIL(&infos,inf);
      Aaddchild(Aweb(),inf,AOREL_APP_USE_SCREEN);
      inf->cols[0].ci_Width=100;
      inf->cols[1].ci_Width=-1;
      if(Agetattr(Aweb(),AOAPP_Screenvalid))
      {  Agetattrs(Aweb(),
            AOAPP_Screen,&screen,
            AOAPP_Drawinfo,&inf->dri,
            AOAPP_Windowport,&port,
            TAG_END);
         Asetattrs(Aweb(),
            AOAPP_Processtype,AOTP_INFO,
            AOAPP_Processfun,Processinfo,
            TAG_END);
         Setinfo(inf,ams);
         urlname=(UBYTE *)Agetattr(inf->url,AOURL_Url);
         if(!lastw)
         {  lastx=prefs.window.infx;
            lasty=prefs.window.infy;
            lastw=prefs.window.infw;
            lasth=prefs.window.infh;
         }
         inf->cawin=WindowObject,
            WA_Title,inf->title?inf->title:AWEBSTR(MSG_INFO_TITLE),
            WA_Left,lastx,
            WA_Top,lasty,
            WA_InnerWidth,lastw,
            WA_InnerHeight,lasth,
            WA_SizeGadget,TRUE,
            WA_DepthGadget,TRUE,
            WA_DragBar,TRUE,
            WA_CloseGadget,TRUE,
            WA_Activate,TRUE,
            WA_AutoAdjust,TRUE,
            WA_SimpleRefresh,TRUE,
            WA_PubScreen,screen,
            WINDOW_SharedPort,port,
            WINDOW_UserData,inf,
            WINDOW_Layout,inf->layout=VLayoutObject,
               LAYOUT_SpaceOuter,TRUE,
               LAYOUT_SpaceInner,FALSE,
               LAYOUT_DeferLayout,TRUE,
               StartMember,inf->urlgad=ButtonObject,
                  GA_ReadOnly,TRUE,
                  GA_Text,urlname?urlname:NULLSTRING,
                  BUTTON_Justification,BCJ_LEFT,
                  LABEL_Underscore,0,
               EndMember,
               CHILD_WeightedHeight,0,
               StartMember,inf->listgad=ListBrowserObject,
                  GA_ID,IGID_LINK,
                  GA_RelVerify,TRUE,
                  LISTBROWSER_Labels,&inf->textlist,
                  LISTBROWSER_ShowSelected,FALSE,
                  LISTBROWSER_AutoFit,TRUE,
                  LISTBROWSER_HorizontalProp,TRUE,
                  LISTBROWSER_ColumnInfo,inf->cols,
               EndMember,
               CHILD_NominalSize,TRUE,
               CHILD_MinWidth,300,
            End,
         End;
      }
      if(inf->cawin)
      {  inf->window=RA_OpenWindow(inf->cawin);
      }
      if(!inf->window)
      {  Disposeinfo(inf);
         inf=NULL;
      }
   }
   return inf;
}

static long Dispatch(struct Info *inf,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newinfo((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setinfo(inf,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposeinfo(inf);
         break;
      case AOM_DEINSTALL:
         break;
   }
   return result;
}

#endif /* !DEMOVERSION */

/*------------------------------------------------------------------------*/

BOOL Installinfo(void)
{  
#ifndef DEMOVERSION
   NEWLIST(&infos);
   renderhook.h_Entry=(HOOKFUNC)Renderhook;
   if(!Amethod(NULL,AOM_INSTALL,AOTP_INFO,Dispatch)) return FALSE;
#endif
   return TRUE;
}

void Getinfodim(short *x,short *y,short *w,short *h)
{  
#ifndef DEMOVERSION
   struct Window *window;
   if(!ISEMPTY(&infos) && (window=infos.first->window))
   {  *x=window->LeftEdge;
      *y=window->TopEdge;
      *w=window->Width-window->BorderLeft-window->BorderRight;
      *h=window->Height-window->BorderTop-window->BorderBottom;
   }
   else
   {  *x=lastx;
      *y=lasty;
      *w=lastw;
      *h=lasth;
   }
#endif
}
