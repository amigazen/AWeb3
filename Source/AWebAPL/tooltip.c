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

/* tooltip.c - AWeb tooltip manager */

#include "aweb.h"
#include "application.h"
#include <intuition/intuition.h>
#include <graphics/gfxmacros.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <images/label.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layout.h>
#include <proto/label.h>
#include <proto/window.h>

static struct Buffer ttbuf;
static BOOL tttick;
static ULONG ttsecs,ttmics;
static long ttx,tty;
static BOOL ttoverlap;

static struct Hook tthook;
static struct Abackfilldata hookdata={0};
static void *ttlabel;
static void *ttwinobject;
static struct Window *ttwindow;
static struct MsgPort *ttport;

/*-----------------------------------------------------------------------*/

// Copy text to ttbuf, wordwrapping as necessary.
static void Wordwrap(UBYTE *text)
{  UBYTE *p;
   long line,lastsp;
   line=0;
   lastsp=-1;
   Freebuffer(&ttbuf);
   for(p=text;*p;p++)
   {  if(isspace(*p)) lastsp=ttbuf.length;
      Addtobuffer(&ttbuf,p,1);
      if(*p=='\n')
      {  line=ttbuf.length;
         lastsp=-1;
      }
      else if(ttbuf.length-line>70)
      {  if(lastsp>=0)
         {  ttbuf.buffer[lastsp]='\n';
            line=lastsp+1;
            lastsp=-1;
         }
         else
         {  Addtobuffer(&ttbuf,"\n",1);
            line=ttbuf.length;
            lastsp=-1;
         }
      }
   }
   Addtobuffer(&ttbuf,"",1);
}

// Close tooltip window
static void Closetooltip(void)
{  if(ttwinobject)
   {  DisposeObject(ttwinobject);
      ttwinobject=NULL;
      ttwindow=NULL;
   }
   if(ttport)
   {  Setprocessfun(ttport->mp_SigBit,NULL);
      DeleteMsgPort(ttport);
      ttport=NULL;
   }
   tttick=FALSE;
}

// Check the tooltip message queue
static void Processtooltip(void)
{  ULONG result;
   while((result=RA_HandleInput(ttwinobject,NULL))!=WMHI_LASTMSG);
}

// Check if mouse is at same position for 0.5 seconds. If so, open tooltip.
static void Ticktooltip(long x,long y)
{  struct Screen *screen;
   struct DrawInfo *dri;
   void *label,*layout;
   struct LayoutLimits limits={0};
   ULONG secs,mics;
   long fgpen,bgpen;
   if(tttick)
   {  if(x==ttx && y==tty)
      {  CurrentTime(&secs,&mics);
         if(1000000*(secs-ttsecs)+((long)mics-(long)ttmics)>500000)
         {  tttick=FALSE;
            Agetattrs(Aweb(),
               AOAPP_Screen,&screen,
               AOAPP_Drawinfo,&dri,
               AOAPP_Tooltippen,&bgpen,
               AOAPP_Blackpen,&fgpen,
               TAG_END);
            hookdata.fgpen=fgpen;
            hookdata.bgpen=bgpen;
            hookdata.flags=ABKFIL_BACKFILL|ABKFIL_BORDER;
            tthook.h_Entry=(HOOKFUNC)Awebbackfillhook;
            tthook.h_Data=&hookdata;
            ttport=CreateMsgPort();
            label=LabelObject,
               LABEL_DrawInfo,dri,
               IA_FGPen,fgpen,
               LABEL_Text,ttbuf.buffer,
               LABEL_Underscore,0,
            End;
            ttwinobject=WindowObject,
               WA_AutoAdjust,TRUE,
               WA_PubScreen,screen,
               WA_Borderless,TRUE,
               WINDOW_SharedPort,ttport,
               WINDOW_Layout,layout=HLayoutObject,
                  LAYOUT_SpaceInner,FALSE,
                  LAYOUT_SpaceOuter,FALSE,
                  LAYOUT_TopSpacing,2,
                  LAYOUT_BottomSpacing,2,
                  LAYOUT_LeftSpacing,2,
                  LAYOUT_RightSpacing,2,
                  LAYOUT_BackFill,&tthook,
                  LAYOUT_AddImage,label,
               End,
            EndWindow;
            if(ttport && label && ttwinobject)
            {  LayoutLimits(layout,&limits,NULL,screen);
               SetAttrs(ttwinobject,
                  WA_Left,x+3,
                  WA_Top,y-limits.MinHeight-3,
                  TAG_END);
               if(ttwindow=(struct Window *)RA_OpenWindow(ttwinobject))
               {  Setprocessfun(ttport->mp_SigBit,Processtooltip);
                  ttoverlap=
                     (x>=ttwindow->LeftEdge && x<ttwindow->LeftEdge+ttwindow->Width
                     && y>=ttwindow->TopEdge && y<ttwindow->TopEdge+ttwindow->Height);
               }
            }
            if(!ttwindow)
            {  Closetooltip();
            }
         }
      }
      else
      {  ttx=x;
         tty=y;
         CurrentTime(&ttsecs,&ttmics);
      }
   }
}

// Check if mouse is over tooltip window. If so, close tooltip and set ticking again.
// But allow first time overlap when the tooltip window is opened.
static void Checktooltip(long x,long y)
{  if(ttwindow)
   {  if(x>=ttwindow->LeftEdge && x<ttwindow->LeftEdge+ttwindow->Width
      && y>=ttwindow->TopEdge && y<ttwindow->TopEdge+ttwindow->Height)
      {  if(!ttoverlap)
         {
            Closetooltip();
            CurrentTime(&ttsecs,&ttmics);
            ttx=x;
            tty=y;
            tttick=TRUE;
         }
      }
      else ttoverlap=FALSE;
   }
}

/*-----------------------------------------------------------------------*/

void Tooltip(UBYTE *text,long x,long y)
{  Closetooltip();
   if(text && prefs.browser.tooltips)
   {  Wordwrap(text);
      CurrentTime(&ttsecs,&ttmics);
      ttx=x;
      tty=y;
      tttick=TRUE;
   }
}

void Tooltipmove(long x,long y)
{  if(tttick)
   {  Ticktooltip(x,y);
   }
   else
   {  Checktooltip(x,y);
   }
}

void Freetooltip(void)
{  Closetooltip();
   Freebuffer(&ttbuf);
}