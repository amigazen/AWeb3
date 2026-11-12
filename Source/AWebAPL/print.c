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

/* print.c - AWeb print process */

#include "aweb.h"
#include "window.h"
#include "frame.h"
#include "application.h"
#include "printwin.h"
#include "winhis.h"
#include "print.h"
static struct Library *CyberGfxBase;
#include <awebgfx.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/soundclass.h>
#include <devices/audio.h>
#include <devices/clipboard.h>
#include <devices/printer.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/stdio.h>
#include <dos/rdargs.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/semaphores.h>
#include <graphics/gfxmacros.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/displayinfo.h>
#include <graphics/scale.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <intuition/imageclass.h>
#include <intuition/pointerclass.h>
#include <intuition/sghooks.h>
#include <intuition/intuition.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <images/label.h>
#include <gadgets/colorwheel.h>
#include <gadgets/gradientslider.h>
#include <gadgets/chooser.h>
#include <gadgets/integer.h>
#include <gadgets/checkbox.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <libraries/iffparse.h>
#include <libraries/locale.h>
#include <rexx/rxslib.h>
#include <rexx/storage.h>
#include <utility/date.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <reaction/reaction.h>
#include <reaction/reaction_author.h>
#include <reaction/reaction_macros.h>
#include <reaction/reaction_class.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>

#include <ezlists.h>

#include <proto/alib.h>
#include <proto/asl.h>
#include <proto/colorwheel.h>
#include <proto/datatypes.h>
#include <proto/diskfont.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/iffparse.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/layers.h>
#include <proto/locale.h>
#include <proto/timer.h>
#include <proto/utility.h>
#include <proto/wb.h>
#include <proto/chooser.h>
#include <proto/integer.h>
#include <proto/checkbox.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/label.h>
#ifndef NETDEMO

/*------------------------------------------------------------------------*/

enum PRINT_PARAM_GADGET_IDS
{  PPGID_PRINT=1,PPGID_CANCEL
};

#define AOPRT_Dummy        AOBJ_DUMMYTAG(AOTP_PRINT)

#define AOPRT_Activate     (AOPRT_Dummy+1)
   /* (BOOL) Activate current print requester */

#define AOPRT_Width        (AOPRT_Dummy+2)
#define AOPRT_Height       (AOPRT_Dummy+3)
   /* (long) Current window dimensions */

#define AOPRT_Scale        (AOPRT_Dummy+4)
   /* (long) Given scale. */

#define AOPRT_Flags        (AOPRT_Dummy+5)
   /* (USHORT) Given flags. Reset scale to 100%. */

#define AOPRT_Wait         (AOPRT_Dummy+6)
   /* (struct Arexxcmd *) ARexx msg to return when finished. */

#define AOPRT_Debug        (AOPRT_Dummy+7)
   /* (BOOL) Debug printer actions */

#define AOPRT_Docwidth     (AOPRT_Dummy+8)
   /* (long) Current document dimensions */

#define AOPRT_    (AOPRT_Dummy+)

static struct Print *print;

/* Initial/remembered values */
static short scale=100;
static USHORT flags=PRTF_FORMFEED|PRTF_BACKGROUND;

static struct Library *AwebPrintBase;

/*---------------------------------------------------------------------*/

/* Print one strip. Render the document, with 1 pixel row overlap at top and
 * bottom to allow smoothing.
 * Notwithstanding this overlap, prt->top is the top row actually printed (but
 * prt->printheight is the height including overlaps).
 * If this print would be the last one on the page (and not the last one of the
 * document), add a formfeed.
 */
static void Printsection(struct Print *prt)
{  if(prt->top==0)
   {  /* first strip, leave top row blank. AOPRW_Cleartop was already set. */
      Asetattrs(prt->prwin,AOBJ_Top,prt->top,TAG_END);
   }
   else
   {  /* render with 1 row overlap */
      Asetattrs(prt->prwin,
         AOPRW_Cleartop,FALSE,
         AOBJ_Top,prt->top-1,
         TAG_END);
   }
   Printprintsection(prt);
   prt->flags|=PRTF_PRINTING;
   prt->top+=prt->printheight-2;
   Setprogressreq(prt->progressreq,prt->top,prt->totalheight);
}

/* Compute acceptable rastport height. Loop (increasing or decreasing scale)
 * until a height is actually found */
static BOOL Finddimensions(struct Print *prt)
{  Printfinddimensions(prt);
   return BOOLVAL(prt->printheight);
}

/* If this is in the 3rd longword of pd_OldStk, it's a turboprint device. If
 * device version >=39 it can handle >8 bit bitmaps.
 * Standard printer.device can handle >8 bit bitmaps from version 44.
 * Copied from turboprint.h: */
#define TPMATCHWORD  0xf10a57ef

/* Create offscreen bitmap, layer. Create mimic windowinfo. Measure and layout doc.
 * Print first section. */
static void Startprintdoc(struct Print *prt)
{  struct Screen *screen=(struct Screen *)Agetattr(Aweb(),AOAPP_Screen);
   struct PrinterData *pd=(struct PrinterData *)prt->ioreq->io_Device;
   BOOL candodeeprp;
   pd->pd_Preferences.PrintAspect=ASPECT_HORIZ;
   /* Check if printer can handle >8 bit bitmaps */
   candodeeprp = ((((ULONG *)(pd->pd_OldStk))[2]==TPMATCHWORD) && (pd->pd_Device.dd_Device.lib_Version>=39))
                 || (pd->pd_Device.dd_Device.lib_Version>=44);
   if(!(prt->progressreq=Openprogressreq(AWEBSTR(MSG_PRINT_PROGRESS)))) goto err;
   if(!(prt->prwin=Anewobject(AOTP_PRINTWINDOW,
      AOPRW_Width,prt->printwidth,
      AOPRW_Height,PRINTWINH,
      AOPRW_Layoutheight,prt->height,
      AOBJ_Nobackground,!(prt->flags&PRTF_BACKGROUND),
      AOPRW_Turboprint,candodeeprp,
      TAG_END))) goto err;
   prt->rp=(struct RastPort *)Agetattr(prt->prwin,AOWIN_Rastport);
   prt->cmap=screen->ViewPort.ColorMap;
   prt->screenmode=GetVPModeID(&screen->ViewPort);
   CyberGfxBase=AwebOpenRtgGfx();
   if(!CyberGfxBase || !IsCyberModeID(prt->screenmode))
   {  /* Get aspect ratio right for old hardware 1:2 screens */
      prt->screenmode=HIRES|LACE;
   }
   AwebCloseRtgGfx(CyberGfxBase);
   CyberGfxBase=NULL;
   if(!Finddimensions(prt)) goto err;
   Asetattrs(prt->prwin,
      AOPRW_Height,prt->printheight,
      AOBJ_Winhis,prt->whis,
      AOPRW_Cleartop,TRUE,
      TAG_END);
   Asetattrs(prt->prwin,AOPRW_Update,TRUE,TAG_END);
   prt->totalheight=Agetattr(prt->prwin,AOPRW_Totalheight);
   prt->numprinted=0;
   Printsection(prt);
   return;
err:
   Adisposeobject(prt);
}

/* Process print i/o request returning */
static void Processprint(void)
{  if(print && (print->flags&PRTF_PRINTING) && CheckIO(print->ioreq))
   {  WaitIO(print->ioreq);
      if(Checkprogressreq(print->progressreq)
      || print->top>=print->totalheight
      || print->ioreq->io_Error)
      {  Adisposeobject(print);
      }
      else
      {  Printsection(print);
      }
   }
}

/*---------------------------------------------------------------------*/

/* Close the parameter requester */
static void Closeparam(struct Print *prt)
{  struct Node *node;
   if(prt->winobj)
   {  DisposeObject(prt->winobj);
      prt->winobj=NULL;
      prt->window=NULL;
   }
   while(node=RemHead(&prt->layoutlist)) FreeChooserNode(node);
}

/* Process input from parameter requester */
static void Processparam(void)
{  ULONG result;
   BOOL done=FALSE,doprint=FALSE;
   int sel;
   if(print && print->winobj)
   {  while((result=RA_HandleInput(print->winobj,NULL))!=WMHI_LASTMSG)
      {  switch(result&WMHI_CLASSMASK)
         {  case WMHI_CLOSEWINDOW:
               done=TRUE;
               break;
            case WMHI_RAWKEY:
               switch(result&WMHI_GADGETMASK)
               {  case 0x45:     /* esc */
                     done=TRUE;
                     break;
               }
               break;
            case WMHI_GADGETUP:
               switch(result&WMHI_GADGETMASK)
               {  case PPGID_PRINT:
                     doprint=TRUE;
                     break;
                  case PPGID_CANCEL:
                     done=TRUE;
                     break;
               }
               break;
         }
      }
      if(done) Adisposeobject(print);
      else if(doprint)
      {  sel=Getvalue(print->layoutgad,CHOOSER_Active);
         switch(sel)
         {  case 0:  print->printwidth=print->width;break;
            case 1:  print->printwidth=print->docwidth;break;
            case 2:  print->printwidth=print->scrwidth;break;
         }
         SETFLAG(print->flags,PRTF_BACKGROUND,Getselected(print->bggad));
         SETFLAG(print->flags,PRTF_CENTER,Getselected(print->centergad));
         SETFLAG(print->flags,PRTF_FORMFEED,Getselected(print->ffgad));
         print->scale=Getvalue(print->scalegad,INTEGER_Number);
         Closeparam(print);
         scale=print->scale;
         flags=print->flags;
         Startprintdoc(print);
      }
   }
}

/* Open print parameter requester */
static BOOL Openparam(struct Print *prt)
{  struct PrinterData *pd;
   struct Screen *screen;
   struct MsgPort *port;
   static ULONG msgs[]={ MSG_PRINTP_LW_WINDOW,MSG_PRINTP_LW_DOCUMENT,MSG_PRINTP_LW_SCREEN };
   struct Node *node;
   int i;
   pd=(struct PrinterData *)prt->ioreq->io_Device;
   Agetattrs(Aweb(),
      AOAPP_Screen,&screen,
      AOAPP_Windowport,&port,
      AOAPP_Screenwidth,&prt->scrwidth,
      TAG_END);
   if(screen)
   {  Asetattrs(Aweb(),
         AOAPP_Processtype,AOTP_PRINT,
         AOAPP_Processfun,Processparam,
         TAG_END);
      for(i=0;i<3;i++)
      {  if(node=AllocChooserNode(
            CNA_Text,AWEBSTR(msgs[i]),
            TAG_END))
            AddTail(&prt->layoutlist,node);
      }
      prt->winobj=WindowObject,
         WA_Title,AWEBSTR(MSG_PRINTP_TITLE),
         WA_DragBar,TRUE,
         WA_DepthGadget,TRUE,
         WA_CloseGadget,TRUE,
         WA_Activate,TRUE,
         WA_SimpleRefresh,TRUE,
         WA_PubScreen,screen,
         WINDOW_SharedPort,port,
         WINDOW_UserData,prt,
         WINDOW_Position,WPOS_CENTERSCREEN,
         WINDOW_Layout,VLayoutObject,
            LAYOUT_SpaceInner,FALSE,
            LAYOUT_DeferLayout,TRUE,
            StartMember,VLayoutObject,
               LAYOUT_SpaceOuter,TRUE,
               StartMember,VLayoutObject,
                  StartMember,prt->layoutgad=ChooserObject,
                     CHOOSER_PopUp,TRUE,
                     CHOOSER_Labels,&prt->layoutlist,
                     CHOOSER_Active,(prt->width<prt->docwidth)?1:0,
                  EndMember,
                  MemberLabel(AWEBSTR(MSG_PRINTP_LAYOUTWIDTH)),
                  StartMember,prt->scalegad=IntegerObject,
                     INTEGER_Minimum,1,
                     INTEGER_Maximum,100,
                     INTEGER_Number,prt->scale,
                  EndMember,
                  MemberLabel(AWEBSTR(MSG_PRINTP_SCALE)),
               EndMember,
               StartMember,prt->centergad=CheckBoxObject,
                  GA_Text,AWEBSTR(MSG_PRINTP_CENTER),
                  GA_Selected,BOOLVAL(prt->flags&PRTF_CENTER),
               EndMember,
               StartMember,prt->ffgad=CheckBoxObject,
                  GA_Text,AWEBSTR(MSG_PRINTP_FORMFEED),
                  GA_Selected,BOOLVAL(prt->flags&PRTF_FORMFEED),
               EndMember,
               StartMember,prt->bggad=CheckBoxObject,
                  GA_Text,AWEBSTR(MSG_PRINTP_PRINTBG),
                  GA_Selected,BOOLVAL(prt->flags&PRTF_BACKGROUND),
               EndMember,
            EndMember,
            StartMember,HLayoutObject,
               LAYOUT_BevelStyle,BVS_SBAR_VERT,
               LAYOUT_SpaceOuter,TRUE,
               LAYOUT_EvenSize,TRUE,
               StartMember,ButtonObject,
                  GA_ID,PPGID_PRINT,
                  GA_RelVerify,TRUE,
                  GA_Text,AWEBSTR(MSG_PRINTP_PRINT),
               EndMember,
               CHILD_WeightedWidth,0,
               StartMember,ButtonObject,
                  GA_ID,PPGID_CANCEL,
                  GA_RelVerify,TRUE,
                  GA_Text,AWEBSTR(MSG_PRINTP_CANCEL),
               EndMember,
               CHILD_WeightedWidth,0,
            EndMember,
         EndMember,
      End;
      if(prt->winobj)
      {  prt->window=RA_OpenWindow(prt->winobj);
      }
   }
   return BOOLVAL(prt->window);
}

/*---------------------------------------------------------------------*/

static long Setprint(struct Print *prt,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOAPP_Screenvalid:
            if(!tag->ti_Data)
            {  Adisposeobject(prt);
               return 0;
            }
            break;
         case AOPRT_Activate:
            if(prt->window)
            {  WindowToFront(prt->window);
               ActivateWindow(prt->window);
            }
            if(prt->progressreq)
            {  Progresstofront(prt->progressreq);
            }
            break;
         case AOPRT_Width:
            prt->width=tag->ti_Data;
            break;
         case AOPRT_Height:
            prt->height=tag->ti_Data;
            break;
         case AOPRT_Docwidth:
            prt->docwidth=tag->ti_Data;
            break;
         case AOPRT_Flags:
            prt->flags=tag->ti_Data|PRTF_NOOPTIONS;
            prt->scale=100;
            break;
         case AOPRT_Scale:
            if(tag->ti_Data>0 && tag->ti_Data<=100)
            {  prt->scale=tag->ti_Data;
            }
            break;
         case AOPRT_Wait:
            prt->wait=(struct Arexxcmd *)tag->ti_Data;
            break;
         case AOPRT_Debug:
            SETFLAG(prt->flags,PRTF_DEBUG,tag->ti_Data);
            break;
         case AOBJ_Winhis:
            if(tag->ti_Data)
            {  prt->whis=Anewobject(AOTP_WINHIS,
                  AOWHS_Copyfrom,tag->ti_Data,
                  AOWHS_Key,0,
                  AOWHS_History,TRUE,
                  TAG_END);
            }
            break;
      }
   }
   return 0;
}

static void Disposeprint(struct Print *prt)
{  Closeparam(prt);
   if(prt->debugfile) Printclosedebug(prt);
   if(prt->prwin) Adisposeobject(prt->prwin);
   if(prt->whis) Adisposeobject(prt->whis);
   if(prt->progressreq) Closeprogressreq(prt->progressreq);
   if(prt->ioreq)
   {  if(prt->flags&PRTF_PRINTING)
      {  if(!CheckIO(prt->ioreq)) AbortIO(prt->ioreq);
         WaitIO(prt->ioreq);
      }
      if(prt->ioreq->io_Device) CloseDevice(prt->ioreq);
      DeleteExtIO(prt->ioreq);
   }
   if(prt->ioport)
   {  Setprocessfun(prt->ioport->mp_SigBit,NULL);
      DeleteMsgPort(prt->ioport);
   }
   Aremchild(Aweb(),prt,AOREL_APP_USE_SCREEN);
   if(prt->wait) Replyarexxcmd(prt->wait);
   Amethodas(AOTP_OBJECT,prt,AOM_DISPOSE);
   CloseLibrary(AwebPrintBase);
   if(prt==print) print=NULL;
}

static struct Print *Newprint(struct Amset *ams)
{  struct Print *prt=NULL;
   if((AwebPrintBase=OpenLibrary("aweblib/print.aweblib",AWEBLIBVERSION))
   || (AwebPrintBase=OpenLibrary("AWeb:aweblib/print.aweblib",AWEBLIBVERSION)))
   {  if(prt=Allocobject(AOTP_PRINT,sizeof(struct Print),ams))
      {  NewList(&prt->layoutlist);
         prt->scale=scale;
         prt->flags=flags;
         Setprint(prt,ams);
         Aaddchild(Aweb(),prt,AOREL_APP_USE_SCREEN);
         if(!(prt->ioport=CreateMsgPort())) goto err;
         Setprocessfun(prt->ioport->mp_SigBit,Processprint);
         if(!(prt->ioreq=(struct IODRPReq *)CreateExtIO(prt->ioport,sizeof(struct IODRPReq)))) goto err;
         if(OpenDevice("printer.device",0,prt->ioreq,0))
         {  UBYTE *buf=Dupstr(AWEBSTR(MSG_PRINT_NOPRINTER),-1);
            if(buf) Asyncrequest(AWEBSTR(MSG_REQUEST_TITLE),buf,AWEBSTR(MSG_PRINT_OK),NULL,NULL);
            goto err;
         }
         Printdebugprefs(prt);
         if((prt->flags&PRTF_NOOPTIONS) && Agetattr(Aweb(),AOAPP_Screenvalid))
         {  Startprintdoc(prt);
         }
         else if(!Openparam(prt)) goto err;
      }
      else CloseLibrary(AwebPrintBase);
   }
   return prt;

err:
   prt->wait=NULL;
   Disposeprint(prt);
   return NULL;
}

static long Dispatch(struct Print *prt,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newprint((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setprint(prt,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposeprint(prt);
         break;
      case AOM_DEINSTALL:
         break;
   }
   return result;
}

#endif /* !NETDEMO */

/*------------------------------------------------------------------------*/

BOOL Installprint(void)
{  
#ifndef NETDEMO
   if(!Amethod(NULL,AOM_INSTALL,AOTP_PRINT,Dispatch)) return FALSE;
#endif
   return TRUE;
}

void Printdoc(void *win,BOOL debug)
{  
#ifndef NETDEMO
   long w=0,h=0,dw=0;
   void *whis=NULL,*frame=NULL;
   if(print)
   {  Asetattrs(print,AOPRT_Activate,TRUE,TAG_END);
   }
   else
   {  Agetattrs(win,
         AOWIN_Innerwidth,&w,
         AOWIN_Innerheight,&h,
         AOBJ_Winhis,&whis,
         AOBJ_Frame,&frame,
         TAG_END);
      if(frame)
      {  dw=Agetattr(frame,AOFRM_Contentwidth);
      }
      print=Anewobject(AOTP_PRINT,
         AOPRT_Width,w,
         AOPRT_Height,h,
         AOPRT_Docwidth,dw,
         AOBJ_Winhis,whis,
         AOPRT_Debug,debug,
         TAG_END);
   }
#endif
}

BOOL Printarexx(void *win,long scale,BOOL center,BOOL ff,BOOL bg,struct Arexxcmd *wait,
   BOOL debug)
{  long w=0,h=0;
   void *whis=NULL;
   USHORT flags=0;
   BOOL result=FALSE;
#ifndef NETDEMO
   if(print)
   {  Asetattrs(print,AOPRT_Activate,TRUE,TAG_END);
   }
   else
   {  if(center) flags|=PRTF_CENTER;
      if(ff) flags|=PRTF_FORMFEED;
      if(bg) flags|=PRTF_BACKGROUND;
      if(debug) flags|=PRTF_DEBUG;
      Agetattrs(win,
         AOWIN_Innerwidth,&w,
         AOWIN_Innerheight,&h,
         AOBJ_Winhis,&whis,
         TAG_END);
      print=Anewobject(AOTP_PRINT,
         AOPRT_Width,w,
         AOPRT_Height,h,
         AOBJ_Winhis,whis,
         AOPRT_Flags,flags,
         scale?AOPRT_Scale:TAG_IGNORE,scale,
         AOPRT_Wait,wait,
         TAG_END);
      if(print) result=TRUE;
   }
#endif
   return result;
}
