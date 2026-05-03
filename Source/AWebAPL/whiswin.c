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

/* winhis.c - AWeb window history */

#include "aweb.h"
#include "winhis.h"
#include "whisprivate.h"
#include "application.h"
#include "window.h"
#include "task.h"
#include <proto/exec.h>
#include <proto/utility.h>

static struct Whiswindow *whiswindow=NULL;

/*-----------------------------------------------------------------------*/

/* Open aweblib and start task */
static BOOL Starttask(struct Whiswindow *whw)
{  if(whw->libbase=Openaweblib("AWeb:aweblib/history.aweblib"))
   {  whw->task=Anewobject(AOTP_TASK,
         AOTSK_Entry,AWEBLIBENTRY(whw->libbase,0),
         AOTSK_Name,"AWeb window history",
         AOTSK_Userdata,whw,
         AOBJ_Target,whw,
         TAG_END);
      if(whw->task)
      {  Asetattrs(whw->task,AOTSK_Start,TRUE,TAG_END);
      }
      else
      {  CloseLibrary(whw->libbase);
         whw->libbase=NULL;
      }
   }
   return BOOLVAL(whw->task);
}

/* Dispose task and close aweblib */
static void Stoptask(struct Whiswindow *whw)
{  if(whw->libbase)
   {  if(whw->task)
      {  Adisposeobject(whw->task);
         whw->task=NULL;
      }
      CloseLibrary(whw->libbase);
      whw->libbase=NULL;
   }
}

/*-----------------------------------------------------------------------*/

static long Updatewhiswindow(struct Whiswindow *whw,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   BOOL close=FALSE;
   struct Winhis *whis,*twhis;
   void *win;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOWHW_Close:
            close=TRUE;
            break;
         case AOWHW_Dimx:
            prefs.window.whisx=whw->x=tag->ti_Data;
            break;
         case AOWHW_Dimy:
            prefs.window.whisy=whw->y=tag->ti_Data;
            break;
         case AOWHW_Dimw:
            prefs.window.whisw=whw->w=tag->ti_Data;
            break;
         case AOWHW_Dimh:
            prefs.window.whish=whw->h=tag->ti_Data;
            break;
         case AOWHW_Display:
            twhis=(struct Winhis *)tag->ti_Data;
            if(!(win=Findwindownr(twhis->windownr)))
            {  win=Firstwindow();
            }
            if(Agetattr(win,AOWIN_Key)!=twhis->key)
            {  whis=Anewobject(AOTP_WINHIS,
                  AOWHS_Copyfrom,twhis,
                  AOWHS_Key,Agetattr(win,AOWIN_Key),
                  AOWHS_Windownr,Agetattr(win,AOWIN_Windownr),
                  TAG_END);
               if(whis->frameid) FREE(whis->frameid);
               whis->frameid=Dupstr(twhis->frameid,-1);
            }
            else
            {  whis=twhis;
            }
            whis->wflags|=WINHF_HISTORY;
            Asetattrs((void *)Agetattr(win,AOBJ_Frame),AOBJ_Winhis,whis,TAG_END);
            break;
         case AOWHW_Getcurrent:
            if(win=Findwindownr(whw->windownr))
            {  whw->current=(void *)Agetattr(win,AOBJ_Winhis);
            }
            else whw->current=NULL;
            break;
      }
   }
   if(close && !(whw->flags&WHWF_BREAKING))
   {  Stoptask(whw);
   }
   return 0;
}

static long Asetwhiswindow(struct Whiswindow *whw,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOAPP_Screenvalid:
            if(!tag->ti_Data)
            {  if(whw->task)
               {  whw->flags|=WHWF_BREAKING|WHWF_WASOPEN;
                  Stoptask(whw);
                  whw->flags&=~WHWF_BREAKING;
               }
            }
            else
            {  if((whw->flags&WHWF_WASOPEN) && !whw->task)
               {  whw->screenname=(UBYTE *)Agetattr(Aweb(),AOAPP_Screenname);
                  Starttask(whw);
                  whw->flags&=~WHWF_WASOPEN;
               }
            }
            break;
      }
   }
   return 0;
}

static struct Whiswindow *Newwhiswindow(struct Amset *ams)
{  struct Whiswindow *whw;
   if(whw=Allocobject(AOTP_WHISWINDOW,sizeof(struct Whiswindow),ams))
   {  Aaddchild(Aweb(),whw,AOREL_APP_USE_SCREEN);
      whw->windownr=GetTagData(AOWHW_Windownr,0,ams->tags);
      whw->current=(void *)GetTagData(AOWHW_Current,NULL,ams->tags);
      if(Agetattr(Aweb(),AOAPP_Screenvalid))
      {  whw->whissema=&whissema;
         whw->winhislist=&winhis;
         whw->x=prefs.window.whisx;
         whw->y=prefs.window.whisy;
         whw->w=prefs.window.whisw;
         whw->h=prefs.window.whish;
         whw->autoclose=prefs.program.whautoclose;
         whw->screenname=(UBYTE *)Agetattr(Aweb(),AOAPP_Screenname);
         if(!Starttask(whw))
         {  Adisposeobject(whw);
            whw=NULL;
         }
      }
   }
   return whw;
}

static void Disposewhiswindow(struct Whiswindow *whw)
{  if(whw->task)
   {  whw->flags|=WHWF_BREAKING;
      Stoptask(whw);
   }
   Aremchild(Aweb(),whw,AOREL_APP_USE_SCREEN);
   Amethodas(AOTP_OBJECT,whw,AOM_DISPOSE);
}

static void Deinstallwhiswindow(void)
{  if(whiswindow) Adisposeobject(whiswindow);
}

static long Dispatch(struct Whiswindow *whw,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newwhiswindow((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Asetwhiswindow(whw,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatewhiswindow(whw,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposewhiswindow(whw);
         break;
      case AOM_DEINSTALL:
         Deinstallwhiswindow();
         break;
   }
   return result;
}

/*-----------------------------------------------------------------------*/

BOOL Installwhiswindow(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_WHISWINDOW,Dispatch)) return FALSE;
   return TRUE;
}

void Openwhiswindow(void *current,long windownr)
{  if(whiswindow && whiswindow->task)
   {  Asetattrsasync(whiswindow->task,AOWHW_Tofront,windownr,TAG_END);
   }
   else
   {  if(whiswindow) Adisposeobject(whiswindow);
      whiswindow=Anewobject(AOTP_WHISWINDOW,
         AOWHW_Current,current,
         AOWHW_Windownr,windownr,
         TAG_END);
   }
}

void Closewhiswindow(void)
{  if(whiswindow)
   {  Adisposeobject(whiswindow);
      whiswindow=NULL;
   }
}

void Changewhiswindow(long windownr)
{  if(whiswindow && whiswindow->task)
   {  Asetattrsasync(whiswindow->task,
         AOWHW_Changed,windownr,
         TAG_END);
   }
}

void Setwhiswindow(void *whis)
{  if(whiswindow)
   {  if(whiswindow->task)
      {  Asetattrsasync(whiswindow->task,
            AOWHW_Current,whis,
            TAG_END);
      }
      else
      {  whiswindow->current=whis;
      }
   }
}

BOOL Isopenwhiswindow(void)
{  return (BOOL)(whiswindow && whiswindow->task);
}