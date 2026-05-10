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

/* hotviewer.o - AWeb hotlist viewer for hotlist AWebLib module */

#include "aweblib.h"
#include "task.h"
#include "hotlist.h"
#include "hotlisttask.h"
#define NOCFGLOCALE 1
#include "awebcfg.h"
#include <intuition/intuition.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/listbrowser.h>
#include <gadgets/chooser.h>
#include <images/label.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/graphics.h>

enum HOTVIEW_GADGET_IDS
{  GID_LIST=1,GID_URLS,GID_MANAGER,GID_FOLLOW,
};

static struct List gadlist;
static struct Gadget *toplayout,*listgad,*urlsgad,*followgad;

static struct Screen *screen;
static struct DrawInfo *dri;
static void *winobj;
static struct Window *window;
static ULONG winsigmask;

static struct Hook renderhook,idcmphook;

static BOOL urls=FALSE;
static long lastclick=-1;

static struct Image *showimg,*hideimg;

static struct DrawList showdata[]=
{  { DLST_LINE,0,0,998,0,1 },
   { DLST_LINE,998,0,998,998,1 },
   { DLST_LINE,998,998,0,998,1 },
   { DLST_LINE,0,998,0,0,1 },
   { DLST_LINE,149,499,799,499,1 },
   { DLST_LINE,499,149,499,799,1 },
   { DLST_END },
};
static struct DrawList hidedata[]=
{  { DLST_LINE,0,0,998,0,1 },
   { DLST_LINE,998,0,998,998,1 },
   { DLST_LINE,998,998,0,998,1 },
   { DLST_LINE,0,998,0,0,1 },
   { DLST_LINE,149,499,799,499,1 },
   { DLST_END },
};

/*-----------------------------------------------------------------------*/

static void Setgadgetattrs_local(struct Gadget *gad,struct Window *win,struct Requester *req,...)
{  struct TagItem *tags=(struct TagItem *)((ULONG *)&req+1);
   if(SetGadgetAttrsA(gad,win,req,tags) && win) RefreshGList(gad,win,req,1);
}

static long Getvalue_local(void *gad,ULONG tag)
{  long value=0;
   GetAttr(tag,gad,(ULONG *)&value);
   return value;
}

long Getlbnvalue(struct Node *node,ULONG tag)
{  long value=0;
   if(node) GetListBrowserNodeAttrs(node,tag,(ULONG *)&value,TAG_END);
   return value;
}

static struct Node *Getnode_local(struct List *list,long n)
{  struct Node *node=list->lh_Head;
   while(node->ln_Succ && n)
   {  node=node->ln_Succ;
      n--;
   }
   if(node->ln_Succ) return node;
   else return NULL;
}

/*-----------------------------------------------------------------------*/

/* Allocate nodes for this group */
static void Allocgroupnodes(LIST(Hotitem) *list,short level)
{  struct Hotitem *hi;
   struct Node *node;
   for(hi=list->first;hi->next;hi=hi->next)
   {  if(hi->type==HITEM_ENTRY)
      {  node=AllocListBrowserNode(1,
            LBNA_Generation,level,
            LBNA_UserData,hi,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,hi->base->title,
            urls?LBNCA_RenderHook:TAG_IGNORE,&renderhook,
            urls?LBNCA_HookHeight:TAG_IGNORE,dri->dri_Font->tf_YSize*2,
            TAG_END);
         if(node)
         {  AddTail(&gadlist,node);
         }
      }
      else
      {  node=AllocListBrowserNode(1,
            LBNA_Generation,level,
            LBNA_UserData,hi,
            LBNA_Flags,LBFLG_CUSTOMPENS|LBFLG_HASCHILDREN|
               (hi->type==HITEM_HGROUP?0:LBFLG_SHOWCHILDREN),
            LBNCA_CopyText,TRUE,
            LBNCA_Text,hi->name,
            LBNCA_FGPen,dri->dri_Pens[HIGHLIGHTTEXTPEN],
            TAG_END);
         if(node)
         {  AddTail(&gadlist,node);
         }
         Allocgroupnodes(&hi->subitems,level+1);
         if(node && hi->type==HITEM_HGROUP)
         {  HideListBrowserNodeChildren(node);
         }
      }
   }
}

/* Allocate all nodes, for groups and rest */
static void Allocnodes(struct Hotwindow *how)
{  struct Hotbase *hb;
   struct Node *node;
   Allocgroupnodes(how->hotlist,1);
   for(hb=how->hotbase->first;hb->next;hb=hb->next)
   {  if(!hb->used)
      {  node=AllocListBrowserNode(1,
            LBNA_Generation,1,
            LBNA_UserData,hb,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,hb->title,
            urls?LBNCA_RenderHook:TAG_IGNORE,&renderhook,
            urls?LBNCA_HookHeight:TAG_IGNORE,dri->dri_Font->tf_YSize*2,
            TAG_END);
         if(node)
         {  AddTail(&gadlist,node);
         }
      }
   }
}

/* Free all nodes */
static void Freenodes(void)
{  struct Node *node;
   while(node=RemHead(&gadlist))
   {  FreeListBrowserNode(node);
   }
}

/* Enable the follow gadget */
static void Setgadgets(struct Hotwindow *how)
{  struct Node *node=(struct Node *)Getvalue_local(listgad,LISTBROWSER_SelectedNode);
   struct Hotitem *hi=(struct Hotitem *)Getlbnvalue(node,LBNA_UserData);
   Setgadgetattrs_local(followgad,window,NULL,
      GA_Disabled,!(hi && (hi->nodetype==HNT_BASE || hi->type==HITEM_ENTRY)),
      TAG_END);
}

/* Change the show urls status */
static void Seturls(struct Hotwindow *how)
{  Setgadgetattrs_local(listgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freenodes();
   urls=Getvalue_local(urlsgad,GA_Selected);
   Allocnodes(how);
   Setgadgetattrs_local(listgad,window,NULL,LISTBROWSER_Labels,&gadlist,TAG_END);
   Setgadgets(how);
}

/* Process enter on node; show or hide group, follow link */
static BOOL Enternode(struct Hotwindow *how,BOOL realenter)
{  struct Node *node=(struct Node *)Getvalue_local(listgad,LISTBROWSER_SelectedNode);
   struct Hotitem *hi=(struct Hotitem *)Getlbnvalue(node,LBNA_UserData);
   struct Hotbase *hb=NULL;
   ULONG flags;
   BOOL result=FALSE;
   if(hi)
   {  if(hi->nodetype==HNT_BASE)
      {  hb=(struct Hotbase *)hi;
         hi=NULL;
      }
      else
      {  if(hi->type==HITEM_ENTRY)
         {  hb=hi->base;
            hi=NULL;
         }
      }
   }
   if(hi)
   {  flags=Getlbnvalue(node,LBNA_Flags);
      if(realenter)
      {  Setgadgetattrs(listgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
         if(flags&LBFLG_SHOWCHILDREN)
         {  HideListBrowserNodeChildren(node);
            hi->type=HITEM_HGROUP;
         }
         else
         {  ShowListBrowserNodeChildren(node,1);
            hi->type=HITEM_GROUP;
         }
         Setgadgetattrs(listgad,window,NULL,LISTBROWSER_Labels,&gadlist,TAG_END);
         Updatetaskattrs(AOTSK_Async,TRUE,
            AOHOT_Changed,TRUE,
            TAG_END);
      }
   }
   if(hb)
   {  Updatetaskattrs(AOHOT_Follow,hb->url,TAG_END);
      result=how->autoclose;
   }
   return result;
}

/* Remember opening or closing a group */
static void Showhide(struct Hotwindow *how)
{  struct Node *node=(struct Node *)Getvalue_local(listgad,LISTBROWSER_SelectedNode);
   struct Hotitem *hi=(struct Hotitem *)Getlbnvalue(node,LBNA_UserData);
   ULONG flags;
   if(hi && hi->nodetype==HNT_ITEM && hi->type!=HITEM_ENTRY)
   {  flags=Getlbnvalue(node,LBNA_Flags);
      if(flags&LBFLG_SHOWCHILDREN) hi->type=HITEM_GROUP;
      else hi->type=HITEM_HGROUP;
      Updatetaskattrs(AOTSK_Async,TRUE,
         AOHOT_Changed,TRUE,
         TAG_END);
   }
}

static long Nextlistnode(struct List *list,long selected)
{  struct Node *node=Getnode_local(list,selected);
   long nr=selected;
   ULONG flags;
   ULONG gen1;
   if(!node) return 0;
   flags=Getlbnvalue(node,LBNA_Flags);
   gen1=Getlbnvalue(node,LBNA_Generation);
   if((flags&LBFLG_HASCHILDREN) && !(flags&LBFLG_SHOWCHILDREN))
   {  for(node=node->ln_Succ;node->ln_Succ;node=node->ln_Succ)
      {  nr++;
         if(Getlbnvalue(node,LBNA_Generation)<=gen1) break;
      }
   }
   else
   {  node=node->ln_Succ;
      nr++;
   }
   if(node->ln_Succ) return nr;
   else return selected;
}

static long Prevlistnode(struct List *list,long selected)
{  struct Node *node=Getnode_local(list,selected);
   long nra=selected,nrb;
   ULONG flags;
   ULONG gen1,gen2,gen3;
   if(!node) return -1;
   gen1=Getlbnvalue(node,LBNA_Generation);
   node=node->ln_Pred;
   if(!node->ln_Pred) return selected;
   nra--;
   gen2=Getlbnvalue(node,LBNA_Generation);
   if(gen2<=gen1) return nra;
   /* Previous node is member of other group. Check if it is visible */
   /* nra will be number of visible node, nrb = number of node looked at */
   /* gen1=level of original; gen3=group level to look for */
   nrb=nra;
   gen3=gen2-1;
   for(node=node->ln_Pred;node->ln_Pred;node=node->ln_Pred)
   {  nrb--;
      flags=Getlbnvalue(node,LBNA_Flags);
      if(flags&LBFLG_HASCHILDREN)                  /* got a group */
      {  gen2=Getlbnvalue(node,LBNA_Generation);
         if(gen2==gen1)       /* same level as original */
         {  if(flags&LBFLG_SHOWCHILDREN) return nra; /* group is open */
            else return nrb;  /* group is closed, return group level */
         }
         /* got a group. Is it the level we're looking for? */
         if(gen2==gen3)
         {  if(!(flags&LBFLG_SHOWCHILDREN))  /* this group is closed */
            {  nra=nrb;       /* return at most this group, not its member */
            }
            gen3--;           /* look out for parent level */
         }
      }
   }
   return selected;
}

static void Moveselected_local(struct Hotwindow *how,long d)
{  long selected=Getvalue_local(listgad,LISTBROWSER_Selected);
   if(d>0) selected=Nextlistnode(&gadlist,selected);
   else selected=Prevlistnode(&gadlist,selected);
   if(selected>=0)
   {  Setgadgetattrs_local(listgad,window,NULL,
         LISTBROWSER_Selected,selected,
         LISTBROWSER_MakeVisible,selected,
         TAG_END);
      lastclick=-1;
   }
   Setgadgets(how);
}

static void Addentry(struct Hotwindow *how)
   {  Setgadgetattrs_local(listgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
      Freenodes();
      Allocnodes(how);
      Setgadgetattrs_local(listgad,window,NULL,LISTBROWSER_Labels,&gadlist,TAG_END);
}

static void Disposehotlist(struct Hotwindow *how)
{  Setgadgetattrs_local(listgad,NULL,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Setgadgetattrs_local(toplayout,window,NULL,GA_ReadOnly,TRUE,TAG_END);
   Freenodes();
}

static void Newhotlist(struct Hotwindow *how)
{  Allocnodes(how);
   Setgadgetattrs_local(listgad,NULL,NULL,LISTBROWSER_Labels,&gadlist,TAG_END);
   Setgadgetattrs_local(toplayout,window,NULL,GA_ReadOnly,FALSE,TAG_END);
}

/*-----------------------------------------------------------------------*/

/* Erase any part of "old" which is not covered by "nw" */
static void Fillouter(struct RastPort *rp,struct Rectangle *old,struct Rectangle *nw)
{   if(old->MinX<nw->MinX) RectFill(rp,old->MinX,old->MinY,nw->MinX-1,old->MaxY);
    if(old->MaxX>nw->MaxX) RectFill(rp,nw->MaxX+1,old->MinY,old->MaxX,old->MaxY);
    if(old->MaxY>nw->MaxY) RectFill(rp,old->MinX,nw->MaxY+1,old->MaxX,old->MaxY);
    if(old->MinY<nw->MinY) RectFill(rp,old->MinX,old->MinY,old->MaxX,nw->MinY-1);
}

/* Hook called to render entries with title and url */
static ULONG __saveds __asm Renderhook(register __a0 struct Hook *hook,
   register __a1 struct LBDrawMsg *msg,
   register __a2 struct Node *node)
{  ULONG result=LBCB_UNKNOWN;
   struct RastPort *rp=msg->lbdm_RastPort;
   struct Rectangle rect;
   struct TextExtent te;
   short fit,slack,apen,bpen,x,y;
   struct Hotitem *hi;
   struct Hotbase *hb;
   if(msg->lbdm_MethodID==LB_DRAW)
   {  hi=(struct Hotitem *)Getlbnvalue(node,LBNA_UserData);
      if(hi->nodetype==HNT_BASE)
      {  hb=(struct Hotbase *)hi;
      }
      else
      {  hb=hi->base;
      }
      if(msg->lbdm_State==LBR_NORMAL)
      {  apen=msg->lbdm_DrawInfo->dri_Pens[TEXTPEN];
         bpen=msg->lbdm_DrawInfo->dri_Pens[BACKGROUNDPEN];
      }
      else
      {  apen=msg->lbdm_DrawInfo->dri_Pens[FILLTEXTPEN];
         bpen=msg->lbdm_DrawInfo->dri_Pens[FILLPEN];
      }
      
      SetABPenDrMd(rp,apen,bpen,JAM2);
      SetSoftStyle(rp,(urls?FSF_BOLD:0),0x0f);
      rect=msg->lbdm_Bounds;
      if(urls) rect.MaxY=(rect.MinY+rect.MaxY)/2;
      fit=TextFit(rp,hb->title,strlen(hb->title),&te,NULL,1,
         rect.MaxX-rect.MinX,rect.MaxY-rect.MinY+1);
      slack=(rect.MaxY-rect.MinY)-(te.MaxY-te.MinY);
      x=rect.MinX-te.MinX+1;
      y=rect.MinY-te.MinY+(slack+1)/2;
      Move(rp,x,y);
      Text(rp,hb->title,fit);
      te.MinX+=x;
      te.MinY+=y;
      te.MaxX+=x;
      te.MaxY+=y;
      SetAPen(rp,bpen);
      Fillouter(rp,&rect,&te.te_Extent);
      
      if(urls)
      {  SetABPenDrMd(rp,apen,bpen,JAM2);
         SetSoftStyle(rp,0,0x0f);
         rect=msg->lbdm_Bounds;
         if(urls) rect.MinY=(rect.MinY+rect.MaxY)/2+1;
         rect.MinX+=12;
         fit=TextFit(rp,hb->url,strlen(hb->url),&te,NULL,1,
            rect.MaxX-rect.MinX,rect.MaxY-rect.MinY+1);
         slack=(rect.MaxY-rect.MinY)-(te.MaxY-te.MinY);
         x=rect.MinX-te.MinX+1;
         y=rect.MinY-te.MinY+(slack+1)/2;
         Move(rp,x,y);
         Text(rp,hb->url,fit);
         te.MinX+=x;
         te.MinY+=y;
         te.MaxX+=x;
         te.MaxY+=y;
         SetAPen(rp,bpen);
         Fillouter(rp,&rect,&te.te_Extent);
      }

      SetSoftStyle(rp,0,0x0f);
      result=LBCB_OK;
   }
   return result;
}

static long __saveds __asm Idcmphook(register __a0 struct Hook *hook,
   register __a1 struct IntuiMessage *msg)
{  switch(msg->Class)
   {  case IDCMP_CHANGEWINDOW:
         Updatetaskattrs(AOHOT_Dimx,window->LeftEdge,
            AOHOT_Dimy,window->TopEdge,
            AOHOT_Dimw,window->Width-window->BorderLeft-window->BorderRight,
            AOHOT_Dimh,window->Height-window->BorderTop-window->BorderBottom,
            TAG_END);
         break;
   }
   return 0;
}

/*-----------------------------------------------------------------------*/

static void Buildhotviewwindow(struct Hotwindow *how)
{  short imgsize;
   NewList(&gadlist);
   urls=FALSE;
   lastclick=-1;
   renderhook.h_Entry=(HOOKFUNC)Renderhook;
   idcmphook.h_Entry=(HOOKFUNC)Idcmphook;
   idcmphook.h_Data=how;
   if((screen=LockPubScreen(how->screenname))
   && (dri=GetScreenDrawInfo(screen)))
   {  ObtainSemaphore(how->hotsema);
      Allocnodes(how);
      ReleaseSemaphore(how->hotsema);
      if(!how->w)
      {  how->x=screen->Width/4;
         how->y=screen->Height/4;
         how->w=screen->Width/2;
         how->h=screen->Height/2;
      }
      imgsize=((dri->dri_Font->tf_YSize+1)&~1)-1;
      showimg=DrawListObject,
         IA_Width,imgsize,
         IA_Height,MAX(7,imgsize-2),
         DRAWLIST_Directives,showdata,
         DRAWLIST_RefWidth,999,
         DRAWLIST_RefHeight,999,
      End;
      hideimg=DrawListObject,
         IA_Width,imgsize,
         IA_Height,MAX(7,imgsize-2),
         DRAWLIST_Directives,hidedata,
         DRAWLIST_RefWidth,999,
         DRAWLIST_RefHeight,999,
      End;
      winobj=WindowObject,
         WA_Title,AWEBSTR(MSG_HOTL_TITLE_VIEWER),
         WA_Left,how->x,
         WA_Top,how->y,
         WA_InnerWidth,how->w,
         WA_InnerHeight,how->h,
         WA_SizeGadget,TRUE,
         WA_DepthGadget,TRUE,
         WA_DragBar,TRUE,
         WA_CloseGadget,TRUE,
         WA_Activate,TRUE,
         WA_AutoAdjust,TRUE,
         WA_SimpleRefresh,TRUE,
         WA_PubScreen,screen,
         WA_IDCMP,IDCMP_RAWKEY,
         WINDOW_IDCMPHook,&idcmphook,
         WINDOW_IDCMPHookBits,IDCMP_CHANGEWINDOW,
         WINDOW_Layout,toplayout=VLayoutObject,
            LAYOUT_SpaceOuter,TRUE,
            LAYOUT_DeferLayout,TRUE,
            StartMember,listgad=ListBrowserObject,
               GA_ID,GID_LIST,
               GA_RelVerify,TRUE,
               LISTBROWSER_Labels,&gadlist,
               LISTBROWSER_ShowSelected,TRUE,
               LISTBROWSER_Hierarchical,TRUE,
               LISTBROWSER_ShowImage,showimg,
               LISTBROWSER_HideImage,hideimg,
               LISTBROWSER_Spacing,0,
            EndMember,
            StartMember,HLayoutObject,
               StartMember,HLayoutObject,
                  StartMember,urlsgad=CheckBoxObject,
                     GA_ID,GID_URLS,
                     GA_RelVerify,TRUE,
                     GA_Text,AWEBSTR(MSG_HOTL_VURLS),
                     GA_Selected,urls,
                  EndMember,
                  CHILD_WeightedWidth,0,
               EndMember,
               StartMember,ButtonObject,
                  GA_ID,GID_MANAGER,
                  GA_RelVerify,TRUE,
                  GA_Text,AWEBSTR(MSG_HOTL_MANAGER),
               EndMember,
               StartMember,followgad=ButtonObject,
                  GA_ID,GID_FOLLOW,
                  GA_RelVerify,TRUE,
                  GA_Text,AWEBSTR(MSG_HOTL_FOLLOW),
                  GA_Disabled,TRUE,
               EndMember,
            EndMember,
            CHILD_WeightedHeight,0,
         EndMember,
      EndWindow;
      if(winobj)
      {  if(window=RA_OpenWindow(winobj))
         {  GetAttr(WINDOW_SigMask,winobj,&winsigmask);
         }
      }
   }
}

static BOOL Handlehotviewwindow(struct Hotwindow *how)
{  ULONG result,relevent;
   BOOL done=FALSE;
   USHORT click;
   while((result=RA_HandleInput(winobj,&click))!=WMHI_LASTMSG)
   {  ObtainSemaphore(how->hotsema);
      switch(result&WMHI_CLASSMASK)
      {  case WMHI_CLOSEWINDOW:
            done=TRUE;
            break;
         case WMHI_GADGETUP:
            switch(result&WMHI_GADGETMASK)
            {  case GID_LIST:
                  relevent=Getvalue_local(listgad,LISTBROWSER_RelEvent);
                  if(relevent&LBRE_DOUBLECLICK)
                  {  if(click==lastclick) done=Enternode(how,FALSE);
                     else Showhide(how);
                  }
                  if(relevent&(LBRE_HIDECHILDREN|LBRE_SHOWCHILDREN))
                  {  Showhide(how);
                  }
                  Setgadgets(how);
                  lastclick=click;
                  break;
               case GID_URLS:
                  Seturls(how);
                  break;
               case GID_MANAGER:
                  Updatetaskattrs(AOHOT_Manager,TRUE,TAG_END);
                  break;
               case GID_FOLLOW:
                  done=Enternode(how,FALSE);
                  break;
            }
            break;
         case WMHI_RAWKEY:
            switch(result&WMHI_GADGETMASK)
            {  case 0x45:  /* esc */
                  done=TRUE;
                  break;
               case 0x43:  /* num enter */
               case 0x44:  /* enter */
                  done=Enternode(how,TRUE);
                  break;
               case 0x4c:  /* up */
                  Moveselected_local(how,-1);
                  break;
               case 0x4d:  /* down */
                  Moveselected_local(how,1);
                  break;
            }
            break;
/*
         case WMHI_CHANGEWINDOW:
            Updatetaskattrs(AOHOT_Dimx,window->LeftEdge,
               AOHOT_Dimy,window->TopEdge,
               AOHOT_Dimw,window->Width-window->BorderLeft-window->BorderRight,
               AOHOT_Dimh,window->Height-window->BorderTop-window->BorderBottom,
               TAG_END);
            break;
*/
      }
      ReleaseSemaphore(how->hotsema);
   }
   return done;
}

static void Closehotviewwindow(struct Hotwindow *how)
{  if(winobj)
   {  if(window)
      {  Updatetaskattrs(AOHOT_Dimx,window->LeftEdge,
            AOHOT_Dimy,window->TopEdge,
            AOHOT_Dimw,window->Width-window->BorderLeft-window->BorderRight,
            AOHOT_Dimh,window->Height-window->BorderTop-window->BorderBottom,
            TAG_END);
      }
      DisposeObject(winobj);
   }
   Freenodes();
   if(showimg) DisposeObject(showimg);showimg=NULL;
   if(hideimg) DisposeObject(hideimg);hideimg=NULL;
   if(dri) FreeScreenDrawInfo(screen,dri);dri=NULL;
   if(screen) UnlockPubScreen(NULL,screen);screen=NULL;
}

__asm __saveds void Hotviewtask(register __a0 struct Hotwindow *how)
{  struct Taskmsg *hm;
   ULONG done=FALSE;
   ULONG getmask;
   struct TagItem *tag,*tstate;
   Buildhotviewwindow(how);
   if(window)
   {  while(!done)
      {  getmask=Waittask(winsigmask);
         while(!done && (hm=Gettaskmsg()))
         {  if(hm->amsg && hm->amsg->method==AOM_SET)
            {  tstate=((struct Amset *)hm->amsg)->tags;
               while(tag=NextTagItem(&tstate))
               {  switch(tag->ti_Tag)
                  {  case AOTSK_Stop:
                        if(tag->ti_Data) done=TRUE;
                        break;
                     case AOHOT_Tofront:
                        WindowToFront(window);
                        ActivateWindow(window);
                        break;
                     case AOHOT_Addentry:
                        ObtainSemaphore(how->hotsema);
                        Addentry(how);
                        ReleaseSemaphore(how->hotsema);
                        break;
                     case AOHOT_Dispose:
                        ObtainSemaphore(how->hotsema);
                        Disposehotlist(how);
                        ReleaseSemaphore(how->hotsema);
                        break;
                     case AOHOT_Newlist:
                        ObtainSemaphore(how->hotsema);
                        Newhotlist(how);
                        ReleaseSemaphore(how->hotsema);
                        break;
                  }
               }
            }
            Replytaskmsg(hm);
         }
         if(!done && (getmask&winsigmask))
         {  done=Handlehotviewwindow(how);
         }
      }
      Closehotviewwindow(how);
   }
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOHOT_Close,TRUE,
      TAG_END);
}

