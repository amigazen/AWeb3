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

/* netstat.c - AWeb network status window */

#include "aweb.h"
#include "url.h"
#include "window.h"
#include "fetch.h"
#include "application.h"
#include "timer.h"
#include "arexx.h"
#include <intuition/imageclass.h>
#include <intuition/gadgetclass.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <reaction/reaction_class.h>
#include <classes/window.h>
#include <gadgets/listbrowser.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <images/label.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/listbrowser.h>
#include <proto/button.h>
#include <proto/layout.h>
#include <proto/window.h>
#include <proto/label.h>

struct Netstat             /* Also a ListBroeser node */
{  FULLNODE(Netstat);
   void *fetch;
   UBYTE *name;            /* Dynamic string */
   long read,total;
   long sec,mic;           /* Start time of transfer */
   USHORT flags;
   ULONG status;
};

#define NSF_CPS      0x0001   /* Joins in CPS rating */
#define NSF_CPSACT   0x0002   /* Active in CPS rating */

static LIST(Netstat) netstats;

static short lastx,lasty,lastw,lasth;

struct Netstatwin
{  struct Aobject object;
   void *winobj;
   struct Window *window;
   struct Gadget *listgad,*cpsgad;
   struct Image *cancelimg,*canallimg;
};

enum NWGADGET_IDS
{  NWGID_LIST,NWGID_CANCEL,NWGID_CANALL,
};

static struct Netstatwin *netstatwin;

/* To compute the CPS rate, time is divided into cells of about 1 second.
 * A history is kept of 60 cells in a wraparound array.
 * After each timer tick, one of three can happen:
 * - No NSF_CPS transfers going on, and no bytes were read in the current
 *   cell.
 *   The array is reset and the start time of the first cell is set to
 *   current time. Displayed CPS rate doesn't change.
 * - Only NSF_CPS but no NSF_CPSACT transfers are going on, and no
 *   bytes were read in the current cell.
 *   Start time of current cell is set to current time. Displayed CPS
 *   rate doesn't change.
 * - Otherwise (active transfers are going on, or the last one has ended
 *   but left something in the current cell).
 *   Elapsed time for current cell is stored. Current cell index advances
 *   one step; start time of new current cell is set to current. New
 *   CPS rate is calculated and displayed.
 *
 * CPS rate is calculated by adding all elapsed times, and all read bytes.
 */
struct Cps
{  long sec,mic;           /* Start time for this cell */
   long elapsed;           /* Elapsed time in milliseconds */
   long read;              /* Total read in this cell */
};

static struct Cps cpc[60]; /* Wraparound array */
static ULONG cps;          /* Current CPS rate */

static short cpsi,cpsn;    /* Start, current index in cpsread[] */
static ULONG ncps;         /* Number of transfers joining in cps */
static ULONG ncpsa;        /* Number of transfers active in cps */

static void *nstimer;

static struct GadgetInfo gadgetinfo;

static struct Hook renderhook;

static short buttonw,buttonh;

#define WINDOWWIDTH  200
#define WINDOWHEIGHT 100

/*-----------------------------------------------------------------------*/

/* width:16 height:14 */
static UWORD canceldata[]=
{  0x0000,0x0000,0x0002,0x2004,0x1008,0x0810,0x0420,0x0000,
   0x0000,0x0180,0x0240,0x3c3e,0x0000,0x0000,
   0x0000,0x0000,0x7c3c,0x0240,0x0180,0x0000,0x0000,0x0420,
   0x0810,0x1008,0x2004,0x4000,0x0000,0x0000,
};
static UWORD canalldata[]=
/* width:36 height:14 */
{  0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
   0x1000,0x4008,0x0100,0x2000,0x2014,0x0280,0x4000,0x1022,
   0x0440,0x8000,0x0841,0x0821,0x0000,0x0000,0x0000,0x0000,
   0x0000,0x0000,0x0000,0x0300,0x600c,0x0000,0x0480,0x9012,
   0x0000,0x787f,0x0fe1,0xf000,0x0000,0x0000,0x0000,0x0000,
   0x0000,0x0000,
   0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0xf87f,0x0fe1,
   0xe000,0x0480,0x9012,0x0000,0x0300,0x600c,0x0000,0x0000,
   0x0000,0x0000,0x0000,0x0000,0x0000,0x0841,0x0821,0x0000,
   0x1022,0x0440,0x8000,0x2014,0x0280,0x4000,0x4008,0x0100,
   0x2000,0x8000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
   0x0000,0x0000,
};

/*-----------------------------------------------------------------------*/

static ULONG __saveds __asm Renderhook(register __a1 struct LBDrawMsg *msg,
   register __a2 struct Netstat *ns)
{  struct RastPort *rp=msg->lbdm_RastPort;
   struct TextExtent extent;
   short fit,x,y,y2,slack,w,d;
   short pen1,pen2;
   UBYTE *line=NULL;
   if(msg->lbdm_MethodID!=LB_DRAW) return LBCB_UNKNOWN;
   GetListBrowserNodeAttrs(ns,LBNCA_Text,&line,TAG_END);
   if(!line) return LBCB_OK;
   if(ns->total)
   {  d=msg->lbdm_Bounds.MaxX-msg->lbdm_Bounds.MinX-2;
      if(ns->read>ns->total)
      {  w=d;
      }
      else if(d>0 && ns->read>0x7fffffff/d)
      {  w=d*(ns->read>>8)/(ns->total>>8);
      }
      else
      {  w=d*ns->read/ns->total;
      }
   }
   else w=0;
   pen1=msg->lbdm_DrawInfo->dri_Pens[BACKGROUNDPEN];
   pen2=msg->lbdm_DrawInfo->dri_Pens[FILLPEN];
   if(msg->lbdm_State!=LBR_NORMAL)
   {  short penx=pen1;
      pen1=pen2;
      pen2=penx;
   }
   x=msg->lbdm_Bounds.MinX;
   y=msg->lbdm_Bounds.MinY;
   y2=msg->lbdm_Bounds.MaxY;
   if(w)
   {  SetAPen(rp,pen2);
      RectFill(rp,x+2,y+1,x+2+w-1,y2-1);
      SetAPen(rp,pen1);
      Move(rp,x+2,y);Draw(rp,x+2+w-1,y);
      Move(rp,x+2,y2);Draw(rp,x+2+w-1,y2);
   }
   else SetAPen(rp,pen1);
   RectFill(rp,x,y,x+1,y2);
   RectFill(rp,x+2+w,y,msg->lbdm_Bounds.MaxX,y2);
   SetABPenDrMd(rp,msg->lbdm_DrawInfo->dri_Pens[TEXTPEN],0,JAM1);
   fit=TextFit(rp,line,strlen(line),&extent,NULL,1,
      msg->lbdm_Bounds.MaxX-msg->lbdm_Bounds.MinX,
      msg->lbdm_Bounds.MaxY-msg->lbdm_Bounds.MinY+1);
   slack=(msg->lbdm_Bounds.MaxY-msg->lbdm_Bounds.MinY)- 
      (extent.te_Extent.MaxY-extent.te_Extent.MinY);
   x=msg->lbdm_Bounds.MinX-extent.te_Extent.MinX+1;
   y=msg->lbdm_Bounds.MinY-extent.te_Extent.MinY+((slack+1)/2);
   Move(rp,x,y);Text(rp,line,fit);
   return LBCB_OK;
}

/* Custom LB method wrappers */
static struct Node *Dolbmaddnode(struct Gadget *gad,struct Window *win,struct Requester *req,
   struct Node *after,...)
{  struct Node *r;
   gadgetinfo.gi_Domain.Left=netstatwin->listgad->LeftEdge;
   gadgetinfo.gi_Domain.Top=netstatwin->listgad->TopEdge;
   gadgetinfo.gi_Domain.Width=netstatwin->listgad->Width;
   gadgetinfo.gi_Domain.Height=netstatwin->listgad->Height;
   r=(struct Node *)DoMethod((Object *)gad,LBM_ADDNODE,&gadgetinfo,after,VARARG(after));
   return r;
}

static ULONG Dolbmeditnode(struct Gadget *gad,struct Window *win,struct Requester *req,
   struct Node *node,...)
{  ULONG r;
   gadgetinfo.gi_Domain.Left=netstatwin->listgad->LeftEdge;
   gadgetinfo.gi_Domain.Top=netstatwin->listgad->TopEdge;
   gadgetinfo.gi_Domain.Width=netstatwin->listgad->Width;
   gadgetinfo.gi_Domain.Height=netstatwin->listgad->Height;
   r=DoMethod((Object *)gad,LBM_EDITNODE,&gadgetinfo,node,VARARG(node));
   return r;
}

static ULONG Dolbmremnode(struct Gadget *gad,struct Window *win,struct Requester *req,
   struct Node *node)
{  ULONG r;
   gadgetinfo.gi_Domain.Left=netstatwin->listgad->LeftEdge;
   gadgetinfo.gi_Domain.Top=netstatwin->listgad->TopEdge;
   gadgetinfo.gi_Domain.Width=netstatwin->listgad->Width;
   gadgetinfo.gi_Domain.Height=netstatwin->listgad->Height;
   r=DoMethod((Object *)gad,LBM_REMNODE,&gadgetinfo,node);
   return r;
}

/*-----------------------------------------------------------------------*/

static UBYTE *Line(UBYTE *name,ULONG status,ULONG read,ULONG total)
{  UBYTE *stat=NULL;
   static UBYTE buffer[32];
   UBYTE *line;
   switch(status)
   {  case NWS_QUEUED:     stat=AWEBSTR(MSG_NWS_QUEUED);break;
      case NWS_STARTED:    stat=AWEBSTR(MSG_NWS_STARTED);break;
      case NWS_LOOKUP:     stat=AWEBSTR(MSG_NWS_LOOKUP);break;
      case NWS_CONNECT:    stat=AWEBSTR(MSG_NWS_CONNECT);break;
      case NWS_LOGIN:      stat=AWEBSTR(MSG_NWS_LOGIN);break;
      case NWS_NEWSGROUP:  stat=AWEBSTR(MSG_NWS_NEWSGROUP);break;
      case NWS_UPLOAD:     stat=AWEBSTR(MSG_NWS_UPLOAD);break;
      case NWS_WAIT:       stat=AWEBSTR(MSG_NWS_WAIT);break;
      case NWS_READ:
         if(total) sprintf(buffer,"%d/%d",read,total);
         else sprintf(buffer,"%d",read);
         stat=buffer;
         break;
      case NWS_PROCESS:    stat=AWEBSTR(MSG_NWS_PROCESS);break;
      default:             stat="--";break;
   }
   line=ALLOCTYPE(UBYTE,strlen(name)+strlen(stat)+2,0);
   if(line)
   {  strcpy(line,name);
      strcat(line,"=");
      strcat(line,stat);
   }
   return line;
}

static void Cancel(BOOL all)
{  struct Netstat *ns,*next;
   ULONG selected;
   for(ns=netstats.first;ns->next;ns=next)
   {  next=ns->next;
      if(ns->fetch)
      {  if(!all) GetListBrowserNodeAttrs(ns,LBNA_Selected,&selected,TAG_END);
         if(all || selected)
         {  Asetattrs(ns->fetch,AOFCH_Cancel,TRUE,TAG_END);
         }
      }
   }
}

/* Compute CPS rate. */
static void Updatecps(struct Netstatwin *nsw)
{  ULONG newcps=cps;
   ULONG sec,mic;
   double mil,read;
   short i;
   Agetattrs(nstimer,
      AOTIM_Seconds,&sec,
      AOTIM_Micros,&mic,
      TAG_END);
   if(!ncps && !cpc[cpsn].read)
   {  /* Dead; reset all. */
      cpsi=cpsn=0;
   }
   else if(ncps && !ncpsa && !cpc[cpsn].read)
   {  /* Intermediate; reuse current cell */
   }
   else
   {  /* Close this cell and advance. Compute new cps rate */
      cpc[cpsn].elapsed=(long)((sec-cpc[cpsn].sec)*1000)+((long)mic-(long)cpc[cpsn].mic)/1000;
      mil=0.0;
      read=0.0;
      i=cpsi;
      for(;;)
      {  mil+=cpc[i].elapsed;
         read+=cpc[i].read;
         if(i==cpsn) break;
         if(++i>=60) i=0;
      }
      if(mil>0)
      {  newcps=read*1000/mil;
      }
      cpsn++;
      if(cpsn>=60) cpsn=0;
      if(cpsn==cpsi)
      {  if(++cpsi>=60) cpsi=0;
      }
   }
   /* Initialize (new) current cell */
   cpc[cpsn].sec=sec;
   cpc[cpsn].mic=mic;
   cpc[cpsn].elapsed=0;
   cpc[cpsn].read=0;
   if(newcps!=cps && nsw && nsw->window)
   {  Setgadgetattrs(nsw->cpsgad,nsw->window,NULL,
         BUTTON_VarArgs,&newcps,
         TAG_END);
   }
   cps=newcps;
}

/* Initialize CPS array */
static void Initcps(void)
{  cpsi=cpsn=0;
   Agetattrs(nstimer,
      AOTIM_Seconds,&cpc[0].sec,
      AOTIM_Micros,&cpc[0].mic,
      TAG_END);
   cpc[0].elapsed=0;
   cpc[0].read=0;
}

/*-----------------------------------------------------------------------*/

static void Processnetstat(void)
{  struct Netstatwin *nsw=netstatwin;
   ULONG result;
   BOOL done=FALSE;
   if(nsw)
   {  while((result=RA_HandleInput(nsw->winobj,NULL))!=WMHI_LASTMSG)
      {  switch(result&WMHI_CLASSMASK)
         {  case WMHI_CLOSEWINDOW:
               done=TRUE;
               break;
            case WMHI_GADGETUP:
               switch(result&WMHI_GADGETMASK)
               {  case NWGID_CANCEL:
                     Cancel(FALSE);
                     break;
                  case NWGID_CANALL:
                     Cancel(TRUE);
                     break;
               }
               break;
         }
      }
      if(done && netstatwin) Adisposeobject(netstatwin);
   }
}

static void Closenetstatwin(struct Netstatwin *nsw)
{  if(nsw->winobj)
   {  if(nsw->window)
      {  lastx=nsw->window->LeftEdge;
         lasty=nsw->window->TopEdge;
         lastw=nsw->window->Width-nsw->window->BorderLeft-nsw->window->BorderRight;
         lasth=nsw->window->Height-nsw->window->BorderTop-nsw->window->BorderBottom;
      }
      DisposeObject(nsw->winobj);
      nsw->winobj=NULL;
      nsw->window=NULL;
   }
   if(nsw->cancelimg) DisposeObject(nsw->cancelimg);nsw->cancelimg=NULL;
   if(nsw->canallimg) DisposeObject(nsw->canallimg);nsw->canallimg=NULL;
}

static BOOL Opennetstatwin(struct Netstatwin *nsw)
{  struct Screen *scr;
   struct MsgPort *port;
   Agetattrs(Aweb(),
      AOAPP_Screen,&scr,
      AOAPP_Windowport,&port,
      TAG_END);
   Asetattrs(Aweb(),
      AOAPP_Processtype,AOTP_NETSTATWIN,
      AOAPP_Processfun,Processnetstat,
      TAG_END);
   buttonw=buttonh=0;
   nsw->cancelimg=Buttonimage(Aweb(),BUTF_NSCANCEL,canceldata,16,14);
   if(nsw->cancelimg)
   {  if(nsw->cancelimg->Width>buttonw) buttonw=nsw->cancelimg->Width;
      if(nsw->cancelimg->Height>buttonh) buttonh=nsw->cancelimg->Height;
   }
   nsw->canallimg=Buttonimage(Aweb(),BUTF_NSCANCELALL,canalldata,36,14);
   if(nsw->canallimg)
   {  if(nsw->canallimg->Width>buttonw) buttonw=nsw->canallimg->Width;
      if(nsw->canallimg->Height>buttonh) buttonh=nsw->canallimg->Height;
   }
   buttonw+=8;
   buttonh+=4;
   nsw->winobj=WindowObject,
      WA_Left,lastx,
      WA_Top,lasty,
      WA_InnerWidth,lastw,
      WA_InnerHeight,lasth,
      WA_PubScreen,scr,
      WA_Title,AWEBSTR(MSG_NWS_TITLE),
      WA_CloseGadget,TRUE,
      WA_DragBar,TRUE,
      WA_DepthGadget,TRUE,
      WA_SizeGadget,TRUE,
      WA_AutoAdjust,TRUE,
      WINDOW_SharedPort,port,
      WINDOW_UserData,nsw,
      WINDOW_Layout,VLayoutObject,
         LAYOUT_SpaceOuter,TRUE,
         LAYOUT_DeferLayout,FALSE,
         StartMember,nsw->listgad=ListBrowserObject,
            GA_ID,NWGID_LIST,
            GA_RelVerify,TRUE,
            LISTBROWSER_MultiSelect,TRUE,
            LISTBROWSER_ShowSelected,TRUE,
            LISTBROWSER_Labels,&netstats,
         EndMember,
         StartMember,HLayoutObject,
            StartMember,ButtonObject,
               GA_ID,NWGID_CANCEL,
               GA_RelVerify,TRUE,
               GA_Image,nsw->cancelimg,
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,nsw->cpsgad=ButtonObject,
               GA_ReadOnly,TRUE,
               GA_Text,AWEBSTR(MSG_NWS_CPS),
               BUTTON_VarArgs,&cps,
               BUTTON_Justification,BCJ_RIGHT,
            EndMember,
            StartMember,ButtonObject,
               GA_ID,NWGID_CANALL,
               GA_RelVerify,TRUE,
               GA_Image,nsw->canallimg,
            EndMember,
            CHILD_WeightedWidth,0,
         EndMember,
         CHILD_WeightedHeight,0,
      EndMember,
   EndWindow;
   if(nsw->winobj)
   {  if(nsw->window=RA_OpenWindow(nsw->winobj))
      {  Asetattrs(nstimer,AOTIM_Waitseconds,1,TAG_END);
         Initcps();
         gadgetinfo.gi_Screen=scr;
         gadgetinfo.gi_Window=nsw->window;
         gadgetinfo.gi_RastPort=nsw->window->RPort;
         gadgetinfo.gi_Layer=gadgetinfo.gi_RastPort->Layer;
         gadgetinfo.gi_DrInfo=(struct DrawInfo *)Agetattr(Aweb(),AOAPP_Drawinfo);
         gadgetinfo.gi_Pens.DetailPen=gadgetinfo.gi_DrInfo->dri_Pens[DETAILPEN];
         gadgetinfo.gi_Pens.BlockPen=gadgetinfo.gi_DrInfo->dri_Pens[BLOCKPEN];
      }
   }
   if(!nsw->window) Closenetstatwin(nsw);
   return BOOLVAL(nsw->window);
}

/*-----------------------------------------------------------------------*/

static long Asetnetstatwin(struct Netstatwin *nsw,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOAPP_Screenvalid:
            if(!tag->ti_Data)
            {  Closenetstatwin(nsw);
            }
            else
            {  Opennetstatwin(nsw);
            }
            break;
         case AOTIM_Ready:
            Updatecps(nsw);
            Asetattrs(nstimer,AOTIM_Waitseconds,1,TAG_END);
            break;
      }
   }
   return 0;
}

static struct Netstatwin *Newnetstatwin(struct Amset *ams)
{  struct Netstatwin *nsw;
   if(nsw=Allocobject(AOTP_NETSTATWIN,sizeof(struct Netstatwin),ams))
   {  Aaddchild(Aweb(),nsw,AOREL_APP_USE_SCREEN);
      if(!nstimer)
      {  nstimer=Anewobject(AOTP_TIMER,TAG_END);
      }
      Asetattrs(nstimer,AOBJ_Target,nsw,TAG_END);
      if(Agetattr(Aweb(),AOAPP_Screenvalid))
      {  if(!Opennetstatwin(nsw))
         {  Adisposeobject(nsw);
            nsw=NULL;
         }
      }
   }
   return nsw;
}

static void Disposenetstatwin(struct Netstatwin *nsw)
{  Aremchild(Aweb(),nsw,AOREL_APP_USE_SCREEN);
   Closenetstatwin(nsw);
   Asetattrs(nstimer,AOBJ_Target,NULL,TAG_END);
   if(nsw==netstatwin) netstatwin=NULL;
   Amethodas(AOTP_OBJECT,nsw,AOM_DISPOSE);
}

static void Deinstallnetstatwin(void)
{  struct Netstat *ns;
   if(netstatwin) Adisposeobject(netstatwin);
   if(netstats.first)
   {  while(ns=(struct Netstat *)REMHEAD(&netstats))
      {  if(ns->name) FREE(ns->name);
         FreeListBrowserNode(ns);
      }
   }
   if(nstimer) Adisposeobject(nstimer);
}

static long Dispatch(struct Netstatwin *nsw,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newnetstatwin((struct Amset *)amsg);
         break;
      case AOM_SET:
      case AOM_UPDATE:
         result=Asetnetstatwin(nsw,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposenetstatwin(nsw);
         break;
      case AOM_DEINSTALL:
         Deinstallnetstatwin();
         break;
   }
   return result;
}

/*-----------------------------------------------------------------------*/

BOOL Installnetstatwin(void)
{  NEWLIST(&netstats);
   lastx=prefs.window.nwsx;
   lasty=prefs.window.nwsy;
   lastw=prefs.window.nwsw;
   lasth=prefs.window.nwsh;
   renderhook.h_Entry=(HOOKFUNC)Renderhook;
   if(!Amethod(NULL,AOM_INSTALL,AOTP_NETSTATWIN,Dispatch)) return FALSE;
   return TRUE;
}

void Opennetstat(void)
{  if(netstatwin)
   {  if(netstatwin->window) WindowToFront(netstatwin->window);
   }
   else
   {  netstatwin=Anewobject(AOTP_NETSTATWIN,TAG_END);
   }
}

void Closenetstat(void)
{  if(netstatwin)
   {  Adisposeobject(netstatwin);
      netstatwin=NULL;
   }
}

BOOL Isopennetstat(void)
{  return (BOOL)(netstatwin && netstatwin->window);
}

void *Addnetstat(void *fetch,UBYTE *url,ULONG status,BOOL cps)
{  struct Netstat *ns=NULL;
   UBYTE *name,*line=NULL;
   if(!nstimer)
   {  nstimer=Anewobject(AOTP_TIMER,TAG_END);
   }
   if(name=Urlfilenamefb(url))
   {  line=Line(name,status,0,0);
      if(netstatwin && netstatwin->window)
      {  ns=(struct Netstat *)Dolbmaddnode(netstatwin->listgad,netstatwin->window,NULL,
            netstats.last,
            LBNA_NodeSize,sizeof(struct Netstat),
            LBNCA_CopyText,TRUE,
            LBNCA_Text,line?line:name,
            LBNCA_RenderHook,&renderhook,
            TAG_END);
      }
      else
      {  ns=(struct Netstat *)AllocListBrowserNode(1,
            LBNA_NodeSize,sizeof(struct Netstat),
            LBNCA_CopyText,TRUE,
            LBNCA_Text,line?line:name,
            LBNCA_RenderHook,&renderhook,
            TAG_END);
         if(ns) ADDTAIL(&netstats,ns);
      }
      if(ns)
      {  ns->name=name;
         ns->fetch=fetch;
         ns->status=status;
         if(cps)
         {  ns->flags|=NSF_CPS;
            ncps++;
         }
      }
   }
   if(line) FREE(line);
   if(!ns)
   {  if(name) FREE(name);
   }
   Setanimgads(TRUE);
   return ns;
}

void Chgnetstat(void *key,ULONG status,ULONG read,ULONG total)
{  struct Netstat *ns=key;
   UBYTE *line;
   long oldread;
   if(!ns) return;
   if(status==NWS_END)
   {  if(ns->name) FREE(ns->name);
      if(ns->flags&NSF_CPSACT)
      {  ncpsa--;
      }
      if(ns->flags&NSF_CPS)
      {  ncps--;
      }
      if(netstatwin && netstatwin->window)
      {  Dolbmremnode(netstatwin->listgad,netstatwin->window,NULL,ns);
      }
      else
      {  REMOVE(ns);
         FreeListBrowserNode(ns);
      }
      Setanimgads(!ISEMPTY(&netstats));
   }
   else
   {  line=Line(ns->name,status,read,total);
      oldread=ns->read;
      ns->read=read;
      ns->total=total;
      ns->status=status;
      if(netstatwin && netstatwin->window)
      {  Dolbmeditnode(netstatwin->listgad,netstatwin->window,NULL,ns,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,line?line:ns->name,
            TAG_END);
      }
      else
      {  SetListBrowserNodeAttrs(ns,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,line?line:ns->name,
            TAG_END);
      }
      if(line) FREE(line);
      if((ns->flags&NSF_CPS) && !ns->read)
      {  Agetattrs(nstimer,
            AOTIM_Seconds,&ns->sec,
            AOTIM_Micros,&ns->mic,
            TAG_END);
      }
      if(ns->flags&NSF_CPS)
      {  /* Start counting only when something is read */
         if(read && !(ns->flags&NSF_CPSACT))
         {  ncpsa++;
            ns->flags|=NSF_CPSACT;
         }
         if(oldread>read) oldread=0;
         cpc[cpsn].read+=(read-oldread);
      }
      Setanimgads(TRUE);
   }
}

void Getnetstatdim(short *x,short *y,short *w,short *h)
{  if(netstatwin && netstatwin->window)
   {  *x=netstatwin->window->LeftEdge;
      *y=netstatwin->window->TopEdge;
      *w=netstatwin->window->Width-netstatwin->window->BorderLeft-netstatwin->window->BorderRight;
      *h=netstatwin->window->Height-netstatwin->window->BorderTop-netstatwin->window->BorderBottom;
   }
   else
   {  *x=lastx;
      *y=lasty;
      *w=lastw;
      *h=lasth;
   }
}

void Setnetstatdim(short x,short y,short w,short h)
{  lastx=x;
   lasty=y;
   lastw=w;
   lasth=h;
   if(netstatwin && netstatwin->window)
   {  ChangeWindowBox(netstatwin->window,x,y,
         w+netstatwin->window->BorderLeft+netstatwin->window->BorderRight,
         h+netstatwin->window->BorderTop+netstatwin->window->BorderBottom);
   }
}

void Cancelnetstatall(void)
{  Cancel(TRUE);
}

void Gettransfers(struct Arexxcmd *ac,UBYTE *stem)
{  UBYTE buf[32];
   struct Netstat *ns;
   void *url;
   long i=0;
   for(ns=netstats.first;ns->next;ns=ns->next)
   {  i++;
      url=(void *)Agetattr(ns->fetch,AOFCH_Url);
      Setstemvar(ac,stem,i,"URL",(UBYTE *)Agetattr(url,AOURL_Realurl));
      sprintf(buf,"%d",ns->status);
      Setstemvar(ac,stem,i,"STATUS",buf);
      sprintf(buf,"%d",ns->read);
      Setstemvar(ac,stem,i,"READ",buf);
      sprintf(buf,"%d",ns->total);
      Setstemvar(ac,stem,i,"TOTAL",buf);
      sprintf(buf,"%d",Agetattr(url,AOURL_Loadnr));
      Setstemvar(ac,stem,i,"LOADID",buf);
   }
   sprintf(buf,"%d",i);
   Setstemvar(ac,stem,0,NULL,buf);
}
