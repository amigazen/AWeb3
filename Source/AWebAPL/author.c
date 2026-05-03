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

/* author.c - AWeb Basic HTTP authorization */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include "aweb.h"
#include "url.h"
#include "fetch.h"
#include "application.h"
#include "task.h"
#include "fetchdriver.h"
#include "authorlib.h"
#include <intuition/intuition.h>
#include <reaction/reaction.h>

#define AUTHSAVENAME "AWeb:AWeb.auth"

static LIST(Authnode) auths;
static struct SignalSemaphore authsema;
static BOOL changed;

static struct Authedit *authedit;

/*-----------------------------------------------------------------------*/

/* Signal edit task that list has changed */
static void Modifiedlist(void)
{  if(authedit && authedit->task)
   {  Asetattrsasync(authedit->task,AOAEW_Modified,TRUE,TAG_END);
   }
}

static struct Authnode *Findauth(UBYTE *server,UBYTE *realm)
{  struct Authnode *an,*anf=NULL;
   ObtainSemaphore(&authsema);
   for(an=auths.first;an->next;an=an->next)
   {  if(STRIEQUAL(an->server,server) && STRIEQUAL(an->realm,realm))
      {  anf=an;
         break;
      }
   }
   ReleaseSemaphore(&authsema);
   return anf;
}

/* Remember info from this auth. Add to head so Guessauthorize() will find
 * the most recent. */
static void Rememberauth(struct Authorize *auth)
{  struct Authnode *an;
   ObtainSemaphore(&authsema);
   if(!(an=Findauth(auth->server,auth->realm)))
   {  if(an=ALLOCSTRUCT(Authnode,1,MEMF_PUBLIC|MEMF_CLEAR))
      {  ADDHEAD(&auths,an);
         if(!(an->server=Dupstr(auth->server,-1))
         || !(an->realm=Dupstr(auth->realm,-1)))
         {  if(an->server) FREE(an->server);
            if(an->realm) FREE(an->realm);
            REMOVE(an);
            FREE(an);
            an=NULL;
         }
      }
   }
   if(an && auth->cookie)
   {  if(an->cookie) FREE(an->cookie);
      an->cookie=Dupstr(auth->cookie,-1);
   }
   changed=TRUE;
   ReleaseSemaphore(&authsema);
   Modifiedlist();
}

static void Loadauthor(void)
{  struct Authorize auth={0};
   long fh;
   UBYTE *buffer=NULL;
   UBYTE *p,*end;
   __aligned struct FileInfoBlock fib={0};
   if(fh=Open(AUTHSAVENAME,MODE_OLDFILE))
   {  if(ExamineFH(fh,&fib))
      {  if(fib.fib_Size)
         {  if(buffer=ALLOCTYPE(UBYTE,fib.fib_Size,0))
            {  Read(fh,buffer,fib.fib_Size);
               end=buffer+fib.fib_Size;
            }
         }
      }
      Close(fh);
   }
   if(buffer)
   {  p=buffer+strlen(buffer)+1;   /* warning line */
      while(p<end)
      {  auth.server=p;
         auth.realm=auth.server+strlen(auth.server)+1;
         if(auth.realm>=end) break;
         auth.cookie=auth.realm+strlen(auth.realm)+1;
         if(auth.cookie>=end) break;
         p=auth.cookie+strlen(auth.cookie)+1;
         if(p>end) break;
         Rememberauth(&auth);
      }
      FREE(buffer);
   }
   changed=FALSE;
}

/*-----------------------------------------------------------------------*/

void Authorize(struct Fetchdriver *fd,struct Authorize *auth,BOOL proxy)
{  struct Authorreq areq={0};
   void *libbase;
   void (*fun)(struct Authorreq *areq);
   areq.auth=auth;
   areq.name=fd->name;
   areq.proxy=proxy;
   areq.Rememberauth=Rememberauth;
   if(libbase=Openaweblib("AWeb:aweblib/authorize.aweblib"))
   {  fun=AWEBLIBENTRY(libbase,0);
      fun(&areq);
      CloseLibrary(libbase);
   }
}

/*-----------------------------------------------------------------------*/

/* Open aweblib and start edit task */
static BOOL Starttask(struct Authedit *aew)
{  Agetattrs(Aweb(),
      AOAPP_Screenname,&aew->screenname,
      AOAPP_Pwfont,&aew->pwfont,
      TAG_END);
   if(aew->libbase=Openaweblib("AWeb:aweblib/authorize.aweblib"))
   {  aew->task=Anewobject(AOTP_TASK,
         AOTSK_Entry,AWEBLIBENTRY(aew->libbase,1),
         AOTSK_Name,"AWeb authorization edit",
         AOTSK_Userdata,aew,
         AOBJ_Target,aew,
         TAG_END);
      if(aew->task)
      {  Asetattrs(aew->task,AOTSK_Start,TRUE,TAG_END);
      }
      else
      {  CloseLibrary(aew->libbase);
         aew->libbase=NULL;
      }
   }
   return BOOLVAL(aew->task);
}

/* Dispose task and close aweblib */
static void Stoptask(struct Authedit *aew)
{  if(aew->libbase)
   {  if(aew->task)
      {  Adisposeobject(aew->task);
         aew->task=NULL;
      }
      CloseLibrary(aew->libbase);
      aew->libbase=NULL;
   }
}

/*------------------------------------------------------------------------*/

static void Finddeletenode(struct Authnode *andel)
{  struct Authnode *an;
   ObtainSemaphore(&authsema);
   for(an=auths.first;an->next;an=an->next)
   {  if(an==andel)
      {  REMOVE(an);
         if(an->server) FREE(an->server);
         if(an->realm) FREE(an->realm);
         if(an->cookie) FREE(an->cookie);
         FREE(an);
         changed=TRUE;
         break;
      }
   }
   ReleaseSemaphore(&authsema);
}

static void Findchangenode(struct Authnode *anchg,struct Authorize *auth)
{  struct Authnode *an;
   ObtainSemaphore(&authsema);
   for(an=auths.first;an->next;an=an->next)
   {  if(an==anchg)
      {  if(auth->cookie)
         {  if(an->cookie) FREE(an->cookie);
            an->cookie=Dupstr(auth->cookie,-1);
            changed=TRUE;
         }
         break;
      }
   }
   ReleaseSemaphore(&authsema);
}

static long Updateauthedit(struct Authedit *aew,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   BOOL close=FALSE;
   struct Authnode *an=NULL;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOAEW_Close:
            if(tag->ti_Data) close=TRUE;
            break;
         case AOAEW_Dimx:
            prefs.window.autx=aew->x=tag->ti_Data;
            break;
         case AOAEW_Dimy:
            prefs.window.auty=aew->y=tag->ti_Data;
            break;
         case AOAEW_Dimw:
            prefs.window.autw=aew->w=tag->ti_Data;
            break;
         case AOAEW_Dimh:
            prefs.window.auth=aew->h=tag->ti_Data;
            break;
         case AOAEW_Authnode:
            an=(struct Authnode *)tag->ti_Data;
            break;
         case AOAEW_Delete:
            if(tag->ti_Data)
            {  Finddeletenode(an);
            }
            break;
         case AOAEW_Change:
            if(tag->ti_Data)
            {  Findchangenode(an,(struct Authorize *)tag->ti_Data);
            }
            break;
      }
   }
   if(close && !(aew->flags&AEWF_BREAKING))
   {  Stoptask(aew);
   }
   return 0;
}

static long Setauthedit(struct Authedit *aew,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOAPP_Screenvalid:
            if(!tag->ti_Data)
            {  if(aew->task)
               {  aew->flags|=AEWF_BREAKING|AEWF_WASOPEN;
                  Stoptask(aew);
                  aew->flags&=~AEWF_BREAKING;
               }
            }
            else
            {  if((aew->flags&AEWF_WASOPEN) && !aew->task)
               {  Starttask(aew);
               }
            }
            break;
      }
   }
   return 0;
}

static struct Authedit *Newauthedit(struct Amset *ams)
{  struct Authedit *aew;
   if(aew=Allocobject(AOTP_AUTHEDIT,sizeof(struct Authedit),ams))
   {  Aaddchild(Aweb(),aew,AOREL_APP_USE_SCREEN);
      if(Agetattr(Aweb(),AOAPP_Screenvalid))
      {  aew->authsema=&authsema;
         aew->auths=&auths;
         aew->x=prefs.window.autx;
         aew->y=prefs.window.auty;
         aew->w=prefs.window.autw;
         aew->h=prefs.window.auth;
         if(!Starttask(aew))
         {  Adisposeobject(aew);
            aew=NULL;
         }
      }
   }
   return aew;
}

static void Disposeauthedit(struct Authedit *aew)
{  if(aew->task)
   {  aew->flags|=AEWF_BREAKING;
      Stoptask(aew);
   }
   Aremchild(Aweb(),aew,AOREL_APP_USE_SCREEN);
   Amethodas(AOTP_OBJECT,aew,AOM_DISPOSE);
}

static void Deinstallauthedit(void)
{  if(authedit) Adisposeobject(authedit);
}

static long Dispatch(struct Authedit *aew,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newauthedit((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setauthedit(aew,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updateauthedit(aew,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposeauthedit(aew);
         break;
      case AOM_DEINSTALL:
         Deinstallauthedit();
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installauthedit(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_AUTHEDIT,Dispatch)) return FALSE;
   return TRUE;
}

void Openauthedit(void)
{  if(authedit && authedit->task)
   {  Asetattrsasync(authedit->task,AOAEW_Tofront,TRUE,TAG_END);
   }
   else
   {  if(authedit) Adisposeobject(authedit);
      authedit=Anewobject(AOTP_AUTHEDIT,TAG_END);
   }
}

void Closeauthedit(void)
{  if(authedit)
   {  Adisposeobject(authedit);
      authedit=NULL;
   }
}

BOOL Isopenauthedit(void)
{  return (BOOL)(authedit && authedit->task);
}

/*-----------------------------------------------------------------------*/

BOOL Initauthor(void)
{  NEWLIST(&auths);
   InitSemaphore(&authsema);
   Loadauthor();
   return TRUE;
}

void Freeauthor(void)
{  if(auths.first)
   {  if(changed) Saveauthor();
      Flushauthor();
   }
}

struct Authorize *Newauthorize(UBYTE *server,UBYTE *realm)
{  struct Authnode *an;
   struct Authorize *auth=ALLOCSTRUCT(Authorize,1,MEMF_PUBLIC|MEMF_CLEAR);
   if(auth)
   {  auth->server=Dupstr(server,-1);
      auth->realm=Dupstr(realm,-1);
      ObtainSemaphore(&authsema);
      an=Findauth(server,realm);
      if(an && an->cookie)
      { auth->cookie=Dupstr(an->cookie,-1);
      }
      ReleaseSemaphore(&authsema);
   }
   return auth;
}

void Freeauthorize(struct Authorize *auth)
{  if(auth->server) FREE(auth->server);
   if(auth->realm) FREE(auth->realm);
   if(auth->cookie) FREE(auth->cookie);
   FREE(auth);
}

struct Authorize *Guessauthorize(UBYTE *server)
{  struct Authnode *an;
   struct Authorize *auth=NULL;
   ObtainSemaphore(&authsema);
   for(an=auths.first;an->next;an=an->next)
   {  if(STRIEQUAL(an->server,server))
      {  auth=ALLOCSTRUCT(Authorize,1,MEMF_PUBLIC|MEMF_CLEAR);
         if(auth)
         {  auth->server=Dupstr(an->server,-1);
            auth->realm=Dupstr(an->realm,-1);
            auth->cookie=Dupstr(an->cookie,-1);
         }
         break;
      }
   }
   ReleaseSemaphore(&authsema);
   return auth;
}

struct Authorize *Dupauthorize(struct Authorize *auth)
{  struct Authorize *newauth=ALLOCSTRUCT(Authorize,1,MEMF_PUBLIC|MEMF_CLEAR);
   if(newauth)
   {  if((!auth->server || (newauth->server=Dupstr(auth->server,-1)))
      && (!auth->realm || (newauth->realm=Dupstr(auth->realm,-1)))
      && (!auth->cookie || (newauth->cookie=Dupstr(auth->cookie,-1)))
      )  return newauth;
      Freeauthorize(newauth);
   }
   return NULL;
}

void Flushauthor(void)
{  struct Authnode *an;
   ObtainSemaphore(&authsema);
   while(an=REMHEAD(&auths))
   {  if(an->server) FREE(an->server);
      if(an->realm) FREE(an->realm);
      if(an->cookie) FREE(an->cookie);
      FREE(an);
   }
   ReleaseSemaphore(&authsema);
   Modifiedlist();
}

void Saveauthor(void)
{  long fh;
   struct Authnode *an;
   UBYTE *warning=" AWeb private file - DO NOT MODIFY";
   if(fh=Open(AUTHSAVENAME,MODE_NEWFILE))
   {  Write(fh,warning,strlen(warning)+1);
      ObtainSemaphore(&authsema);
      for(an=auths.first;an->next;an=an->next)
      {  if(an->cookie)
         {  Write(fh,an->server,strlen(an->server)+1);
            Write(fh,an->realm,strlen(an->realm)+1);
            Write(fh,an->cookie,strlen(an->cookie)+1);
         }
      }
      changed=FALSE;
      ReleaseSemaphore(&authsema);
      Close(fh);
   }
}

void Forgetauthorize(struct Authorize *auth)
{  struct Authnode *an;
   ObtainSemaphore(&authsema);
   for(an=auths.first;an->next;an=an->next)
   {  if(STRIEQUAL(an->server,auth->server) && STRIEQUAL(an->realm,auth->realm))
      {  REMOVE(an);
         if(an->server) FREE(an->server);
         if(an->realm) FREE(an->realm);
         if(an->cookie) FREE(an->cookie);
         FREE(an);
         break;
      }
   }
   changed=TRUE;
   ReleaseSemaphore(&authsema);
   Modifiedlist();
}

void Setauthorize(struct Authorize *auth,UBYTE *userid,UBYTE *passwd)
{  void *libbase;
   void (*fun)(struct Authorize *auth,UBYTE *userid,UBYTE *passwd);
   if(libbase=Openaweblib("AWeb:aweblib/authorize.aweblib"))
   {  fun=AWEBLIBENTRY(libbase,2);
      fun(auth,userid,passwd);
      CloseLibrary(libbase);
   }
}
