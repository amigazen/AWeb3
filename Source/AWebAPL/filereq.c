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

/* filereq.c - AWeb asynchroneous filerequester object */

#include "aweb.h"
#include "filereq.h"
#include "application.h"
#include "window.h"
#include "task.h"
#include "arexx.h"
#include <libraries/asl.h>
#include <graphics/displayinfo.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asl.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <reaction/reaction_macros.h>

/*------------------------------------------------------------------------*/

#define MAXSTRLEN 255

struct Filereq
{  struct Aobject object;
   void *task;
   void *target;
   ULONG targetwindow;
   struct TagItem *maplist;
   ULONG userdata;
   struct Arexxcmd *arexx;
   UBYTE *unkurl;
   UBYTE *unktype;
   UBYTE *unkext;
   UBYTE buffer[MAXSTRLEN+1];       /* holds final file name */
   UBYTE *title;
   UBYTE *pattern;
   USHORT flags;
   UBYTE *screenname;
   UBYTE drawer[MAXSTRLEN];
   UBYTE file[MAXSTRLEN];
   UBYTE *exitpattern;
   short x,y,w,h;
};

#define FRQF_SAVEMODE      0x0001   /* Save mode requester */
#define FRQF_SAVECHECK     0x0002   /* Do an existence check on save file */
#define FRQF_APPEND        0x0004   /* Append was selected */
#define FRQF_OK            0x0008   /* File name is ok. */
#define FRQF_DIRSONLY      0x0010   /* Drawers only */
#define FRQF_DISPOSING     0x0020   /* Already disposing */

/* Internal update tag */
#define AOFRQ_Requestok       (AOFRQ_Dummy+101)

/*------------------------------------------------------------------------*/

static void Filereqtask(struct Filereq *fr)
{  struct FileRequester *afr;
   long lock,len,height;
   BOOL again,ok=FALSE,noname=FALSE;
   UBYTE *buf,*p;
   struct Screen *screen=LockPubScreen(fr->screenname);
   struct DimensionInfo dim={0};
   ok=TRUE;
   if(fr->unkurl && fr->unktype && fr->unkext)
   {  p=AWEBSTR(MSG_UNKMIME_TEXT);
      len=strlen(p)+strlen(fr->unkurl)+strlen(fr->unktype)+strlen(fr->unkext);
      if(buf=ALLOCTYPE(UBYTE,len,0))
      {  Lprintf(buf,p,fr->unkurl,fr->unktype,fr->unkext);
         ok=Syncrequestcc(AWEBSTR(MSG_UNKMIME_TITLE),haiku?HAIKU10:buf,
            AWEBSTR(MSG_UNKMIME_BUTTONS),fr->unkurl);
         FREE(buf);
      }
   }
   if(Checktaskbreak()) ok=FALSE;
   if(ok)
   {  if(GetDisplayInfoData(NULL,(UBYTE *)&dim,sizeof(dim),DTAG_DIMS,
         GetVPModeID(&screen->ViewPort)))
      {  height=(dim.Nominal.MaxY-dim.Nominal.MinY+1)*4/5;
      }
      else
      {  height=screen->Height*4/5;
      }
      do
      {  again=FALSE;
         if(afr=AllocAslRequestTags(ASL_FileRequest,
            ASLFR_Screen,screen,
            ASLFR_TitleText,fr->title,
            fr->w?ASLFR_InitialLeftEdge:TAG_IGNORE,fr->x,
            fr->w?ASLFR_InitialTopEdge:TAG_IGNORE,fr->y,
            fr->w?ASLFR_InitialWidth:TAG_IGNORE,fr->w,
            ASLFR_InitialHeight,fr->h?fr->h:height,
            ASLFR_InitialDrawer,fr->drawer,
            ASLFR_InitialFile,fr->file,
            ASLFR_PositiveText,AWEBSTR((fr->flags&FRQF_SAVEMODE)?MSG_FILE_SAVE:MSG_FILE_OPEN),
            ASLFR_RejectIcons,TRUE,
            fr->pattern?ASLFR_InitialPattern:TAG_IGNORE,fr->pattern,
            ASLFR_DoPatterns,BOOLVAL(fr->pattern),
            ASLFR_DoSaveMode,BOOLVAL(fr->flags&FRQF_SAVEMODE),
            ASLFR_DrawersOnly,BOOLVAL(fr->flags&FRQF_DIRSONLY),
            TAG_END))
         {  ok=FALSE;
            if(AslRequest(afr,NULL))
            {  strncpy(fr->drawer,afr->fr_Drawer,MAXSTRLEN);
               strncpy(fr->file,afr->fr_File,MAXSTRLEN);
               strcpy(fr->buffer,fr->drawer);
               ok=AddPart(fr->buffer,afr->fr_File,MAXSTRLEN);
               fr->exitpattern=Dupstr(afr->fr_Pattern,-1);
               noname=!*afr->fr_File;
            }
            fr->x=afr->fr_LeftEdge;
            fr->y=afr->fr_TopEdge;
            fr->w=afr->fr_Width;
            fr->h=afr->fr_Height;
            FreeAslRequest(afr);
            if(Checktaskbreak()) ok=FALSE;
            if(ok)
            {  if(fr->flags&FRQF_SAVECHECK)
               {  if(lock=Lock(fr->buffer,SHARED_LOCK))
                  {  UnLock(lock);
                     /* File exists on save */
                     p=AWEBSTR(MSG_FILE_EXISTTEXT);
                     len=strlen(fr->buffer)+strlen(p)+4;
                     if(buf=ALLOCTYPE(UBYTE,len,0))
                     {  sprintf(buf,p,fr->buffer);
                        switch(Syncrequest(AWEBSTR(MSG_REQUEST_TITLE),buf,
                           AWEBSTR(MSG_FILE_EXISTBUTTONS),0))
                        {  case 0:  /* cancel */
                              ok=FALSE;
                              break;
                           case 1:  /* ok */
                              break;
                           case 2:  /* append */
                              fr->flags|=FRQF_APPEND;
                              break;
                           case 3:  /* new name */
                              again=TRUE;
                              break;
                        }
                        FREE(buf);
                     }
                  }
               }
               else if(!(fr->flags&FRQF_SAVEMODE))
               {  if(lock=Lock(fr->buffer,SHARED_LOCK))
                  {  ok=NameFromLock(lock,fr->buffer,MAXSTRLEN);
                     UnLock(lock);
                     /* If no file name was given, append '/' to full name */
                     if(noname)
                     {  p=fr->buffer+strlen(fr->buffer)-1;
                        if(*p!=':' && *p!='/')
                        {  strncat(fr->buffer,"/",MAXSTRLEN-1);
                        }
                     }
                  }
               }
            }
         }
      } while(again);
   }
   if(screen) UnlockPubScreen(NULL,screen);
   if(Checktaskbreak()) ok=FALSE;
   if(ok) fr->flags|=FRQF_OK;
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOFRQ_Requestok,ok,
      TAG_END);
}

/*------------------------------------------------------------------------*/

static long Setfilereq(struct Filereq *fr,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFRQ_Title:
            if(fr->title) FREE(fr->title);
            fr->title=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFRQ_Filename:
            if(tag->ti_Data) strncpy(fr->buffer,(UBYTE *)tag->ti_Data,MAXSTRLEN);
            break;
         case AOFRQ_Pattern:
            fr->pattern=(UBYTE *)tag->ti_Data;
            break;
         case AOFRQ_Userdata:
            fr->userdata=tag->ti_Data;
            break;
         case AOFRQ_Targetwindow:
            fr->targetwindow=tag->ti_Data;
            break;
         case AOFRQ_Savemode:
            SETFLAG(fr->flags,FRQF_SAVEMODE,tag->ti_Data);
            break;
         case AOFRQ_Savecheck:
            if(tag->ti_Data) fr->flags|=FRQF_SAVEMODE|FRQF_SAVECHECK;
            else fr->flags&=~FRQF_SAVECHECK;
            break;
         case AOFRQ_Dirsonly:
            SETFLAG(fr->flags,FRQF_DIRSONLY,tag->ti_Data);
            break;
         case AOFRQ_Arexx:
            fr->arexx=(struct Arexxcmd *)tag->ti_Data;
            break;
         case AOFRQ_Unkurl:
            if(!fr->unkurl) fr->unkurl=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFRQ_Unktype:
            if(!fr->unktype) fr->unktype=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFRQ_Unkext:
            if(!fr->unkext) fr->unkext=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOBJ_Target:
            fr->target=(void *)tag->ti_Data;
            break;
         case AOBJ_Map:
            fr->maplist=(struct TagItem *)tag->ti_Data;
            break;
      }
   }
   return 0;
}

static long Updatefilereq(struct Filereq *fr,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   BOOL ready=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFRQ_Requestok:
            ready=TRUE;
            break;
      }
   }
   if(ready)
   {  if(fr->arexx)
      {  if(fr->flags&FRQF_OK)
         {  fr->arexx->result=Dupstr(fr->buffer,-1);
         }
         else
         {  fr->arexx->errorlevel=RXERR_WARNING;
         }
         Replyarexxcmd(fr->arexx);
      }
      if(fr->targetwindow)
      {  fr->target=Findwindow(fr->targetwindow);
      }
      if(fr->target)
      {  Aupdateattrs(fr->target,fr->maplist,
            AOFRQ_Filename,(fr->flags&FRQF_OK)?fr->buffer:NULL,
            AOFRQ_Userdata,fr->userdata,
            AOFRQ_Append,BOOLVAL(fr->flags&FRQF_APPEND),
            (fr->pattern&&(fr->flags&FRQF_OK))?AOFRQ_Pattern:TAG_IGNORE,fr->exitpattern,
            TAG_END);
      }
      if(fr->flags&FRQF_SAVEMODE)
      {  prefs.window.savex=fr->x;
         prefs.window.savey=fr->y;
         prefs.window.savew=fr->w;
         prefs.window.saveh=fr->h;
      }
      else
      {  prefs.window.openx=fr->x;
         prefs.window.openy=fr->y;
         prefs.window.openw=fr->w;
         prefs.window.openh=fr->h;
      }
      if(!(fr->flags&FRQF_DISPOSING))
      {  Adisposeobject(fr);
      }
   }
   return 0;
}

static void Disposefilereq(struct Filereq *fr)
{  fr->flags|=FRQF_DISPOSING;
   if(fr->task) Adisposeobject(fr->task);
   if(fr->exitpattern) FREE(fr->exitpattern);
   if(fr->title) FREE(fr->title);
   if(fr->unkurl) FREE(fr->unkurl);
   if(fr->unktype) FREE(fr->unktype);
   if(fr->unkext) FREE(fr->unkext);
   Amethodas(AOTP_OBJECT,fr,AOM_DISPOSE);
}

static struct Filereq *Newfilereq(struct Amset *ams)
{  struct Filereq *fr;
   UBYTE *p;
   if(fr=Allocobject(AOTP_FILEREQ,sizeof(struct Filereq),ams))
   {  fr->title=Dupstr("AWeb",-1);
      Setfilereq(fr,ams);
      p=PathPart(fr->buffer);
      strncpy(fr->drawer,fr->buffer,p-fr->buffer);
      p=FilePart(fr->buffer);
      strcpy(fr->file,p);
      fr->screenname=(UBYTE *)Agetattr(Aweb(),AOAPP_Screenname);
      if(fr->flags&FRQF_SAVEMODE)
      {  fr->x=prefs.window.savex;
         fr->y=prefs.window.savey;
         fr->w=prefs.window.savew;
         fr->h=prefs.window.saveh;
      }
      else
      {  fr->x=prefs.window.openx;
         fr->y=prefs.window.openy;
         fr->w=prefs.window.openw;
         fr->h=prefs.window.openh;
      }
      if(fr->task=Anewobject(AOTP_TASK,
         AOTSK_Entry,Filereqtask,
         AOTSK_Name,"AWeb file requester",
         AOTSK_Userdata,fr,
         AOBJ_Target,fr,
         TAG_END))
      {  Asetattrs(fr->task,AOTSK_Start,TRUE,TAG_END);
      }
      else
      {  Disposefilereq(fr);
         fr=NULL;
      }
         }
   return fr;
}

static long Dispatch(struct Filereq *fr,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newfilereq((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setfilereq(fr,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatefilereq(fr,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposefilereq(fr);
         break;
      case AOM_DEINSTALL:
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installfilereq(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_FILEREQ,Dispatch)) return FALSE;
   return TRUE;
}

