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

/* cabrowse.c - AWeb cache browser */

#include "aweb.h"
#include "cache.h"
#include "application.h"
#include "task.h"
#include "url.h"
#include "window.h"
#include <intuition/intuition.h>
#include <libraries/locale.h>
#include <reaction/reaction.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>

#ifndef LOCALONLY

#include "caprivate.h"

static struct Cabrwindow *cabrwindow=NULL;

/*-----------------------------------------------------------------------*/

/* Open aweblib and start task */
static BOOL Starttask(struct Cabrwindow *cbw)
{  cbw->currentsize=cadisksize;
   if(cbw->libbase=Openaweblib("AWeb:aweblib/cachebrowser.aweblib"))
   {  cbw->task=Anewobject(AOTP_TASK,
         AOTSK_Entry,AWEBLIBENTRY(cbw->libbase,0),
         AOTSK_Name,"AWeb cachebrowser",
         AOTSK_Userdata,cbw,
         AOBJ_Target,cbw,
         TAG_END);
      if(cbw->task)
      {  Asetattrs(cbw->task,AOTSK_Start,TRUE,TAG_END);
      }
      else
      {  CloseLibrary(cbw->libbase);
         cbw->libbase=NULL;
      }
   }
   return BOOLVAL(cbw->task);
}

/* Dispose task and close aweblib */
static void Stoptask(struct Cabrwindow *cbw)
{  if(cbw->libbase)
   {  if(cbw->task)
      {  Adisposeobject(cbw->task);
         cbw->task=NULL;
      }
      CloseLibrary(cbw->libbase);
      cbw->libbase=NULL;
   }
}

/*------------------------------------------------------------------------*/

static long Updatecabrwindow(struct Cabrwindow *cbw,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   BOOL close=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCBR_Close:
            if(tag->ti_Data) close=TRUE;
            break;
         case AOCBR_Dimx:
            prefs.window.cabrx=cbw->x=tag->ti_Data;
            break;
         case AOCBR_Dimy:
            prefs.window.cabry=cbw->y=tag->ti_Data;
            break;
         case AOCBR_Dimw:
            prefs.window.cabrw=cbw->w=tag->ti_Data;
            break;
         case AOCBR_Dimh:
            prefs.window.cabrh=cbw->h=tag->ti_Data;
            break;
#ifndef DEMOVERSION
         case AOCBR_Open:
            if(tag->ti_Data)
            {  Inputwindocnoref(Activewindow(),(void *)tag->ti_Data,NULL,0);
            }
            break;
         case AOCBR_Save:
            if(tag->ti_Data)
            {  Auload((void *)tag->ti_Data,AUMLF_DOWNLOAD,NULL,NULL,NULL);
            }
            break;
         case AOCBR_Delete:
            if(tag->ti_Data)
            {  Auspecial((void *)tag->ti_Data,AUMST_DELETECACHE);
            }
            break;
#endif
      }
   }
   if(close && !(cbw->flags&CBRF_BREAKING))
   {  Stoptask(cbw);
   }
   return 0;
}

static long Setcabrwindow(struct Cabrwindow *cbw,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOAPP_Screenvalid:
            if(!tag->ti_Data)
            {  if(cbw->task)
               {  cbw->flags|=CBRF_BREAKING|CBRF_WASOPEN;
                  Stoptask(cbw);
                  cbw->flags&=~CBRF_BREAKING;
               }
            }
            else
            {  if((cbw->flags&CBRF_WASOPEN) && !cbw->task)
               {  cbw->screenname=(UBYTE *)Agetattr(Aweb(),AOAPP_Screenname);
                  Starttask(cbw);
               }
            }
            break;
      }
   }
   return 0;
}

static struct Cabrwindow *Newcabrwindow(struct Amset *ams)
{  struct Cabrwindow *cbw;
   if(cbw=Allocobject(AOTP_CABROWSE,sizeof(struct Cabrwindow),ams))
   {  Aaddchild(Aweb(),cbw,AOREL_APP_USE_SCREEN);
      if(Agetattr(Aweb(),AOAPP_Screenvalid))
      {  cbw->cachesema=&cachesema;
         cbw->cache=&cache;
         cbw->cfnameshort=Cfnameshort;
         cbw->x=prefs.window.cabrx;
         cbw->y=prefs.window.cabry;
         cbw->w=prefs.window.cabrw;
         cbw->h=prefs.window.cabrh;
         cbw->disksize=prefs.network.cadisksize;
         cbw->currentsize=cadisksize;
         cbw->screenname=(UBYTE *)Agetattr(Aweb(),AOAPP_Screenname);
         if(!Starttask(cbw))
         {  Adisposeobject(cbw);
            cbw=NULL;
         }
      }
   }
   return cbw;
}

static void Disposecabrwindow(struct Cabrwindow *cbw)
{  if(cbw->task)
   {  cbw->flags|=CBRF_BREAKING;
      Stoptask(cbw);
   }
   Aremchild(Aweb(),cbw,AOREL_APP_USE_SCREEN);
   Amethodas(AOTP_OBJECT,cbw,AOM_DISPOSE);
}

static void Deinstallcabrwindow(void)
{  if(cabrwindow) Adisposeobject(cabrwindow);
}

static long Dispatch(struct Cabrwindow *cbw,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newcabrwindow((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setcabrwindow(cbw,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatecabrwindow(cbw,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposecabrwindow(cbw);
         break;
      case AOM_DEINSTALL:
         Deinstallcabrwindow();
         break;
   }
   return result;
}

#endif   /* !LOCALONLY */

/*------------------------------------------------------------------------*/

BOOL Installcabrowse(void)
{  
#ifndef LOCALONLY
   if(!Amethod(NULL,AOM_INSTALL,AOTP_CABROWSE,Dispatch)) return FALSE;
#endif
   return TRUE;
}

void Opencabrowse(void)
{  
#ifndef LOCALONLY
   if(cabrwindow && cabrwindow->task)
   {  Asetattrsasync(cabrwindow->task,AOCBR_Tofront,TRUE,TAG_END);
   }
   else
   {  if(cabrwindow) Adisposeobject(cabrwindow);
      cabrwindow=Anewobject(AOTP_CABROWSE,TAG_END);
   }
#endif
}

void Closecabrowse(void)
{  
#ifndef DEMOVERSION
   if(cabrwindow)
   {  Adisposeobject(cabrwindow);
      cabrwindow=NULL;
   }
#endif
}

void Addcabrobject(struct Cache *cac)
{  
#ifndef LOCALONLY
   if(cabrwindow && cabrwindow->task && !cac->brnode)
   {  Asetattrsasync(cabrwindow->task,
         AOCBR_Addobject,cac,
         AOCBR_Disksize,cadisksize,
         TAG_END);
   }
#endif
}

void Remcabrobject(struct Cache *cac)
{  
#ifndef LOCALONLY
   if(cabrwindow && cabrwindow->task && cac->brnode)
   {  Asetattrsasync(cabrwindow->task,
         AOCBR_Remobject,cac->brnode,
         AOCBR_Disksize,cadisksize,
         TAG_END);
   }
   cac->brnode=NULL;
#endif
}

BOOL Isopencabrowse(void)
{  
   return
#ifdef LOCALONLY
      FALSE
#else
      (BOOL)(cabrwindow && cabrwindow->task)
#endif
   ;
}