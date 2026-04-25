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

/* fetch.c - AWeb html fetch object */

#include "aweb.h"
#include "fetch.h"
#include "url.h"
#include "fetchdriver.h"
#include "application.h"
#include "task.h"
#include "window.h"
#include "jslib.h"
#include "form.h"
#include "awebtcp.h"
#include "ciddataurls.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

/*------------------------------------------------------------------------*/

struct Fetch
{  struct Aobject object;
   void *url;
   struct Fetchdriver *fd;
   UBYTE *name;
   UBYTE *postmsg;
   struct Multipartdata *mpd;
   UBYTE *referer;
   ULONG flags;
   void *jframe;
   long total,sofar;
   ULONG serverdate;
   UBYTE statusbuf[STATUSBUFSIZE];
   void *netstat;
   ULONG validate;
   UBYTE *etag;                  /* ETag value for If-None-Match header */
   ULONG windowkey;
   ULONG loadflags;
   long channelid;
   struct Library *fdbase;
   void (*driverfun)(struct Fetchdriver *);
   void *task;
};

#define FCHF_RUNNING       0x00000001  /* Fetchdriver is running */
#define FCHF_NOCACHE       0x00000002  /* Don't use caches */
#define FCHF_CACHE         0x00000004  /* Cache reload */
#define FCHF_CANCELLED     0x00000008  /* Cancelled, don't show 'incomplete' requester */
#define FCHF_NOTMODIFIED   0x00000010  /* Not modified, terminate when data arrives */
#define FCHF_DISPOSED      0x00000020  /* We are disposed, don't forward anything but terminate */
#define FCHF_INCOMPLETE    0x00000040  /* Incomplete requester is open */
#define FCHF_COMMANDS      0x00000080  /* Shell, ARexx commands are allowed */
#define FCHF_LOCALSLOT     0x00000100  /* Takes up local file slot. */
#define FCHF_NETSLOT       0x00000200  /* Takes up network connection slot. */
#define FCHF_QUEUED        0x00000400  /* Queued, must be removed if disposed. */
#define FCHF_USESLOT       0x00000800  /* Occupies a slot */
#define FCHF_NOPROXY       0x00001000  /* Don't use proxy */
#define FCHF_POSTNOGOOD    0x00002000  /* Retry with GET when driver terminates */
#define FCHF_IMAGE         0x00004000  /* Fetch counts as image fetch */
#define FCHF_FORMWARN      0x00008000  /* Warn if form is sent over unsecure link */
#define FCHF_CHANNEL       0x00010000  /* This is a channel fetch */
#define FCHF_VIEWSOURCE    0x00020000  /* view-source: URL - render as plain text */

static LIST(Fetch) netqueue;
static LIST(Fetch) localqueue;
static LIST(Fetch) running;         /* Also fetches in authorization state */
static LIST(Fetch) channels;

static long nrlocal,nrnet;

static long channelid=0;

static BOOL Startdriver(struct Fetch *fch);

struct Waitrequest
{  NODE(Waitrequest);
   struct Arexxcmd *cmd;
   ULONG windowkey;
   BOOL doc,img;
   void *url;
};

static LIST(Waitrequest) waitrequests;

#define AOFCC_Arexxcmd     (AOFCC_Dummy+64)

/*------------------------------------------------------------------------*/

/* Error scheme task. The validate field is mis-used as msg id. */
void Errorschemetask(struct Fetchdriver *fd)
{  UBYTE *scheme=NULL,*p;
   long length;
   if(fd->name)
   {  p=strchr(fd->name,':');
      if(p) scheme=Dupstr(fd->name,p-fd->name);
      else scheme=Dupstr(fd->name,-1);
      if(scheme)
      {  if(haiku)
         {  strcpy(fd->block,"<html>");
            switch(fd->validate)
            {  case MSG_EPART_NOPROGRAM:strcat(fd->block,HAIKU9);break;
               case MSG_EPART_NOAWEBLIB:strcat(fd->block,HAIKU14);break;
               case MSG_EPART_ADDRSCHEME:strcat(fd->block,HAIKU15);break;
            }
            length=strlen(fd->block);
         }
         else
         {  length=sprintf(fd->block,"<html><h1>%s</h1>%s %.1000s<p><strong>",
               AWEBSTR(MSG_EPART_ERROR),AWEBSTR(MSG_EPART_RETURL),fd->name);
            length+=sprintf(fd->block+length,AWEBSTR(fd->validate),scheme);
         }
         Updatetaskattrs(
            AOURL_Error,TRUE,
            AOURL_Data,fd->block,
            AOURL_Datalength,length,
            TAG_END);
         FREE(scheme);
      }
   }
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOURL_Eof,TRUE,
      AOURL_Terminate,TRUE,
      TAG_END);
}

/*------------------------------------------------------------------------*/

/* Javascript: URL generated text task. The postmsg field is mis-used as
 * text buffer; the validate field is mis-used as date-to-expire. */
void Javascripttask(struct Fetchdriver *fd)
{  if(fd->postmsg)
   {  Updatetaskattrs(
         AOURL_Expires,fd->validate,
         AOURL_Data,fd->postmsg,
         AOURL_Datalength,strlen(fd->postmsg),
         TAG_END);
   }
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOURL_Eof,TRUE,
      AOURL_Terminate,TRUE,
      TAG_END);
}

/*------------------------------------------------------------------------*/

/* JS generated text task. The postmsg field is mis-used as text buffer. */
void Jsgeneratedtask(struct Fetchdriver *fd)
{  if(fd->postmsg)
   {  Updatetaskattrs(
         AOURL_Contenttype,"text/html",
         AOURL_Jsopen,BOOLVAL(fd->flags&FDVF_JSOPEN),
         AOURL_Data,fd->postmsg,
         AOURL_Datalength,strlen(fd->postmsg),
         TAG_END);
   }
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOURL_Eof,TRUE,
      AOURL_Terminate,TRUE,
      TAG_END);
}

/*------------------------------------------------------------------------*/
/* Channel task. */

/* After headers, apply Cache-Control max-age / must-revalidate (once per response). */
static void Channel_apply_cc_accum(struct Fetchdriver *fd)
{  ULONG exp;
   ULONG nowtoday;
   if(fd->channel_cc_applied)
      return;
   fd->channel_cc_applied=TRUE;
   nowtoday=Today();
   if(fd->channel_cc.saw_positive_max_age && fd->channel_cc.min_max_age>0)
   {  exp=nowtoday+(ULONG)fd->channel_cc.min_max_age;
      Updatetaskattrs(AOURL_Expires,exp,TAG_END);
   }
   else if(fd->channel_cc.must_revalidate && !fd->channel_got_expires && fd->serverdate)
   {  Updatetaskattrs(AOURL_Expires,nowtoday,TAG_END);
   }
}

/* Process headers (shouldn't this be shared with http.c?) */
static void Channelheader(struct Fetchdriver *fd,UBYTE *header)
{  Updatetaskattrs(
      AOURL_Header,header,
      TAG_END);
   if(STRNIEQUAL(header,"Date:",5))
   {  fd->serverdate=Scandate(header+5);
      Updatetaskattrs(
         AOURL_Serverdate,fd->serverdate,
         TAG_END);
   }
   else if(STRNIEQUAL(header,"Last-Modified:",14))
   {  ULONG date=Scandate(header+14);
      Updatetaskattrs(
         AOURL_Lastmodified,date,
         TAG_END);
   }
   else if(STRNIEQUAL(header,"Expires:",8))
   {  ULONG expires=Scandate(header+8);
      fd->channel_got_expires=TRUE;
      Updatetaskattrs(
         AOURL_Expires,expires,
         TAG_END);
   }
   else if(STRNIEQUAL(header,"Pragma:",7))
   {  UBYTE *p;
      for(p=header+7;*p && isspace(*p);p++);
      if(STRNIEQUAL(p,"No-Cache",8))
      {  Updatetaskattrs(
            AOURL_Nocache,TRUE,
            TAG_END);
      }
   }
   else if(STRNIEQUAL(header,"Cache-Control:",14))
   {  UBYTE *p;
      UBYTE *term;
      UBYTE termsave;
      struct Http_cc_accum line;
      p=header+14;
      while(*p && (*p==' ' || *p=='\t')) p++;
      term=p;
      while(*term && *term!='\r' && *term!='\n') term++;
      termsave=*term;
      *term='\0';
      Http_cc_parse(p,&line);
      Http_cc_merge(&fd->channel_cc,&line);
      if(line.forbid_disk)
      {  Updatetaskattrs(
            AOURL_Nocache,TRUE,
            TAG_END);
      }
      *term=termsave;
   }
   else if(STRNIEQUAL(header,"Content-Length:",15))
   {  long i=0;
      sscanf(header+15," %ld",&i);
      Updatetaskattrs(
         AOURL_Contentlength,i,
         TAG_END);
   }
   else if(STRNIEQUAL(header,"Content-Type:",13))
   {  if(!prefs.ignoremime)
      {  UBYTE *p,*q;
         for(p=header+13;*p && isspace(*p);p++);
         q=strchr(p,';');
         if(q) *q='\0';
         if(strlen(p)>31) p[31]='\0';
         Updatetaskattrs(
            AOURL_Contenttype,p,
            TAG_END);
      }
   }
   else if(STRNIEQUAL(header,"Set-Cookie:",11))
   {  if(prefs.cookies)
      {  Storecookie(fd->name,header+11,fd->serverdate);
      }
   }
   else if(STRNIEQUAL(header,"Refresh:",8))
   {  Updatetaskattrs(
         AOURL_Clientpull,header+8,
         TAG_END);
   }
}

/* The actual subtask */
void Channeltask(struct Fetchdriver *fd)
{  struct Taskmsg *msg;
   struct Amset *ams;
   struct TagItem *tstate,*tag;
   BOOL done=FALSE;
   Http_cc_reset_accum(&fd->channel_cc);
   fd->channel_got_expires=FALSE;
   fd->channel_cc_applied=FALSE;
   while(!done)
   {  Waittask(0);
      while(!done && (msg=Gettaskmsg()))
      {  if(msg->amsg && msg->amsg->method==AOM_SET)
         {  ams=(struct Amset *)msg->amsg;
            if(tstate=ams->tags)
            {  while(!done && (tag=NextTagItem(&tstate)))
               {  switch(tag->ti_Tag)
                  {  case AOTSK_Stop:
                        if(tag->ti_Data) done=TRUE;
                        break;
                     case AOFCC_Header:
                        Channelheader(fd,(UBYTE *)tag->ti_Data);
                        break;
                     case AOFCC_Data:
                        Channel_apply_cc_accum(fd);
                        Updatetaskattrs(
                           AOURL_Data,tag->ti_Data,
                           AOURL_Datalength,strlen((UBYTE *)tag->ti_Data),
                           TAG_END);
                        break;
                     case AOFCC_Newline:
                        if(tag->ti_Data)
                        {  Updatetaskattrs(
                              AOURL_Data,"\n",
                              AOURL_Datalength,1,
                              TAG_END);
                        }
                        break;
                     case AOFCC_Close:
                        if(tag->ti_Data) done=TRUE;
                        break;
                  }
               }
            }
         }
         Replytaskmsg(msg);
      }
      
   }
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOURL_Eof,TRUE,
      AOURL_Terminate,TRUE,
      TAG_END);
}

/*------------------------------------------------------------------------*/

/* Find out if this host is in the no-cache or no-proxy list */
static BOOL Isinlist(LIST(Nocache) *list,UBYTE *name)
{  UBYTE *p,*q;
   struct Nocache *nc;
   BOOL result=FALSE,scheme;
   short l,plen;
#ifndef LOCALONLY
   p=strstr(name,"://");
   if(p) p+=3;
   else p=name;
   if((*p=='/' || STRNIEQUAL(p,"localhost",9)) && !prefs.cachelocalhost) result=TRUE;
   else
   {  q=strchr(p,'/');
      plen=strlen(p);
      for(nc=list->first;!result && nc->next;nc=nc->next)
      {  if(nc->pattern)
         {  scheme=BOOLVAL(strstr(nc->name,"://"));
            result=MatchPatternNoCase(nc->pattern,scheme?name:p);
         }
         else
         {  if(strchr(nc->name,'/'))
            {  l=strlen(nc->name);
               result=STRNIEQUAL(nc->name,p,l);
            }
            else
            {  /* no-name is domain only - match full domain*/
               if(q) l=q-p;
               else l=plen;
               if(l==strlen(nc->name)) result=STRNIEQUAL(nc->name,p,l);
            }
         }
      }
   }
#else
   result=TRUE;
#endif
   return result;
}

/* Start the driver subtask */
static BOOL Dostartdriver(struct Fetch *fch)
{  if(fch->task=Anewobject(AOTP_TASK,
      AOTSK_Entry,fch->driverfun,
      AOTSK_Name,"AWeb retrieve",
      AOTSK_Userdata,fch->fd,
      AOBJ_Target,fch,
      TAG_END))
   {  Asetattrs(fch->task,AOTSK_Start,TRUE,TAG_END);
      if(Agetattr(fch->task,AOTSK_Started))
      {  fch->flags|=FCHF_RUNNING|FCHF_USESLOT;
         if(fch->netstat) Chgnetstat(fch->netstat,NWS_STARTED,0,0);
         else fch->netstat=Addnetstat(fch,(UBYTE *)Agetattr(fch->url,AOURL_Url),
            NWS_STARTED,BOOLVAL(fch->flags&FCHF_NETSLOT));
         if(fch->flags&FCHF_LOCALSLOT) nrlocal++;
         else if(fch->flags&FCHF_NETSLOT) nrnet++;
         {  extern BOOL httpdebug;
            if(httpdebug)
            {  printf("[FETCH] Dostartdriver: STARTED task=%p url=%s netslot=%ld localslot=%ld nrnet=%ld/%ld nrlocal=%ld/%ld\n",
                  fch->task,
                  (char *)Agetattr(fch->url,AOURL_Url),
                  (long)BOOLVAL(fch->flags&FCHF_NETSLOT),
                  (long)BOOLVAL(fch->flags&FCHF_LOCALSLOT),
                  (long)nrnet,(long)prefs.maxconnect,
                  (long)nrlocal,(long)prefs.maxdiskread);
            }
         }
      }
      else
      {  Adisposeobject(fch->task);
         fch->task=NULL;
      }
   }
   return BOOLVAL(fch->task);
}

/* Check if any requests in this list matches this Waitrequest.
 * Returns TRUE if no match. */
static BOOL Matchwaitrequest(struct Waitrequest *wr,LIST(Fetch) *list)
{  BOOL ready=TRUE;
   struct Fetch *fch;
   for(fch=list->first;ready && fch->next;fch=fch->next)
   {  if(wr->url)
      {  if(wr->url==fch->url) ready=FALSE;
      }
      else if(!wr->windowkey || wr->windowkey==fch->windowkey)
      {  if(fch->flags&FCHF_IMAGE)
         {  if(wr->img || !wr->doc) ready=FALSE;
         }
         else
         {  if(wr->doc || !wr->img) ready=FALSE;
         }
      }
   }
   return ready;
}

/* Check outstanding wait requests */
static void Checkwaitrequests(void)
{  struct Waitrequest *wr,*wrnext;
   BOOL ready;
   for(wr=waitrequests.first;wr->next;wr=wrnext)
   {  wrnext=wr->next;
      ready=Matchwaitrequest(wr,&running)
         && Matchwaitrequest(wr,&netqueue)
         && Matchwaitrequest(wr,&localqueue)
         && Matchwaitrequest(wr,&channels);
      if(ready)
      {  REMOVE(wr);
         if(wr->cmd) Replyarexxcmd(wr->cmd);
         FREE(wr);
      }
   }
}

/* Put fetch in a queue, non-image fetches at higher priority. */
static void Addtoqueue(LIST(Fetch) *queue,struct Fetch *fch)
{  struct Fetch *fa;
   if(fch->flags&FCHF_IMAGE)
   {  ADDTAIL(queue,fch);
   }
   else
   {  for(fa=queue->first;fa->next;fa=fa->next)
      {  if(fa->flags&FCHF_IMAGE) break;
      }
      INSERT(queue,fch,fa->prev);
   }
}

/* Check the queues as a result of this fetch terminating */
static void Checkqueues(struct Fetch *tfch)
{  struct Fetch *fch;
   if(tfch && (tfch->flags&FCHF_USESLOT))
   {  if(tfch->flags&FCHF_NETSLOT) nrnet--;
      if(tfch->flags&FCHF_LOCALSLOT) nrlocal--;
      tfch->flags&=~FCHF_USESLOT;
   }
   while(nrnet<prefs.maxconnect && (fch=REMHEAD(&netqueue)))
   {  ADDTAIL(&running,fch);
      fch->flags&=~FCHF_QUEUED;
      {  extern BOOL httpdebug;
         if(httpdebug)
         {  printf("[FETCH] Checkqueues: DEQUEUE-NET url=%s nrnet=%ld/%ld\n",
               fch->name? (char *)fch->name : "(null)",
               (long)nrnet,(long)prefs.maxconnect);
         }
      }
      if(!Dostartdriver(fch))
      {  Asrcupdatetags(fch->url,fch,
            AOURL_Terminate,TRUE,
            TAG_END);
         Adisposeobject(fch);
      }
   }
   while(nrlocal<prefs.maxdiskread && (fch=REMHEAD(&localqueue)))
   {  ADDTAIL(&running,fch);
      fch->flags&=~FCHF_QUEUED;
      {  extern BOOL httpdebug;
         if(httpdebug)
         {  printf("[FETCH] Checkqueues: DEQUEUE-LOCAL url=%s nrlocal=%ld/%ld\n",
               fch->name? (char *)fch->name : "(null)",
               (long)nrlocal,(long)prefs.maxdiskread);
         }
      }
      if(!Dostartdriver(fch))
      {  Asrcupdatetags(fch->url,fch,
            AOURL_Terminate,TRUE,
            TAG_END);
         Adisposeobject(fch);
      }
   }
}

/* Fill in the proxy */
static void Setproxy(struct Fetchdriver *fd,UBYTE *proxy)
{  UBYTE *p;
   if(STRNIEQUAL(proxy,"https://",8))
   {  p=proxy+8;
      fd->flags|=FDVF_SSL;
   }
   else p=proxy+7;
   fd->proxy=Dupstr(p,-1);
}

static int Hex(char p)
{  if(p>='0' && p<='9') return p-'0';
   if(p>='A' && p<='F') return p-'A'+10;
   if(p>='a' && p<='f') return p-'a'+10;
   return 0;
}

/* Unescape the URL */
static UBYTE *Unescape(UBYTE *url)
{  UBYTE *p,*q;
   for(p=url,q=url;*p;p++,q++)
   {  if(*p=='%' && p[1] && p[2])
      {  *q=16*Hex(p[1])+Hex(p[2]);
         p+=2;
      }
      else if(p!=q)
      {  *q=*p;
      }
   }
   *q='\0';
   return url;
}

/* Find out what driver function to start. In case of external program,
 * do that here and don't fill in a function. */
static void Driverfunction(struct Fetch *fch)
{  /* Check for view-source: URL scheme - strip prefix and set flags */
   if(fch->name && STRNIEQUAL(fch->name,"VIEW-SOURCE:",12))
   {  UBYTE *newurl;
      /* Strip "view-source:" prefix from URL */
      newurl=Dupstr(fch->name+12,-1);
      if(newurl)
      {  FREE(fch->name);
         fch->name=newurl;
         fch->flags|=FCHF_VIEWSOURCE;
         /* Set view-source flag in URL object */
         Asetattrs(fch->url,AOURL_Viewsource,TRUE,TAG_END);
      }
   }
   if(fch->fdbase)
   {  CloseLibrary(fch->fdbase);
      fch->fdbase=NULL;
   }
#ifndef LOCALONLY
   if(fch->flags&FCHF_CACHE)
   {  fch->driverfun=Localfiletask;
      fch->fd->name=fch->name;
      fch->fd->flags|=FDVF_CACHERELOAD;
      fch->flags|=FCHF_LOCALSLOT;
   }
   else if(fch->flags&FCHF_CHANNEL)
   {  fch->driverfun=Channeltask;
      fch->fd->name=fch->name;
      fch->channelid=++channelid;
   }
   else if(STRNIEQUAL(fch->name,"HTTP://",7))
   {  /* Use original bsdsocket-based implementation */
      fch->driverfun=Httptask;
      fch->fd->name=fch->name;
      if(prefs.httpproxy && !(fch->flags&FCHF_NOPROXY))
      {  Setproxy(fch->fd,prefs.httpproxy);
      }
      fch->flags|=FCHF_NETSLOT;
   }
   else if(STRNIEQUAL(fch->name,"HTTPS://",8))
   {  /* Use original bsdsocket-based implementation */
      fch->driverfun=Httptask;
      fch->fd->name=fch->name;
      fch->fd->flags|=FDVF_SSL;
      if(prefs.httpproxy && !(fch->flags&FCHF_NOPROXY))
      {  Setproxy(fch->fd,prefs.httpproxy);
      }
      fch->flags|=FCHF_NETSLOT;
   }
   else if(STRNIEQUAL(fch->name,"FTPS://",7))
   {  /* FTPS (FTP over TLS) - use SSL */
      if(prefs.ftpproxy && !(fch->flags&FCHF_NOPROXY))
      {  fch->driverfun=Httptask;
         fch->fd->name=fch->name;
         Setproxy(fch->fd,prefs.ftpproxy);
         fch->flags|=FCHF_NETSLOT;
      }
      else if(fch->fdbase=Openaweblib("AWeb:aweblib/ftp.aweblib"))
      {  fch->driverfun=AWEBLIBENTRY(fch->fdbase,0);
         fch->fd->name=Unescape(fch->name+7);
         fch->fd->flags|=FDVF_SSL;
         fch->flags|=FCHF_NETSLOT;
      }
      else
      {  fch->driverfun=Errorschemetask;
         fch->fd->name=fch->name;
         fch->fd->validate=MSG_EPART_NOAWEBLIB;
      }
   }
   else if(STRNIEQUAL(fch->name,"FTP://",6))
   {  if(prefs.ftpproxy && !(fch->flags&FCHF_NOPROXY))
      {  fch->driverfun=Httptask;
         fch->fd->name=fch->name;
         Setproxy(fch->fd,prefs.ftpproxy);
         fch->flags|=FCHF_NETSLOT;
      }
      else if(fch->fdbase=Openaweblib("AWeb:aweblib/ftp.aweblib"))
      {  fch->driverfun=AWEBLIBENTRY(fch->fdbase,0);
         fch->fd->name=Unescape(fch->name+6);
         fch->flags|=FCHF_NETSLOT;
      }
      else
      {  fch->driverfun=Errorschemetask;
         fch->fd->name=fch->name;
         fch->fd->validate=MSG_EPART_NOAWEBLIB;
      }
   }
   else if(STRNIEQUAL(fch->name,"GOPHER://",9))
   {  if(fch->fdbase=Openaweblib("AWeb:aweblib/gopher.aweblib"))
      {  fch->driverfun=AWEBLIBENTRY(fch->fdbase,0);
         fch->fd->name=Unescape(fch->name+9);
         fch->flags|=FCHF_NETSLOT;
      }
      else
      {  fch->driverfun=Errorschemetask;
         fch->fd->name=fch->name;
         fch->fd->validate=MSG_EPART_NOAWEBLIB;
      }
   }
   else if(STRNIEQUAL(fch->name,"GEMINI://",9))
   {  if(fch->fdbase=Openaweblib("AWeb:aweblib/gemini.aweblib"))
      {  fch->driverfun=AWEBLIBENTRY(fch->fdbase,0);
         fch->fd->name=Unescape(fch->name+9);
         fch->fd->flags|=FDVF_SSL;
         fch->flags|=FCHF_NETSLOT;
      }
      else
      {  fch->driverfun=Errorschemetask;
         fch->fd->name=fch->name;
         fch->fd->validate=MSG_EPART_NOAWEBLIB;
      }
   }
   else if(STRNIEQUAL(fch->name,"SPARTAN://",10))
   {  if(fch->fdbase=Openaweblib("AWeb:aweblib/gemini.aweblib"))
      {  fch->driverfun=AWEBLIBENTRY(fch->fdbase,0);
         fch->fd->name=Unescape(fch->name+10);
         /* Spartan does NOT use SSL - do not set FDVF_SSL flag */
         fch->flags|=FCHF_NETSLOT;
      }
      else
      {  fch->driverfun=Errorschemetask;
         fch->fd->name=fch->name;
         fch->fd->validate=MSG_EPART_NOAWEBLIB;
      }
   }
   /* Legacy protocols removed - Telnet support discontinued */
   else if(STRNIEQUAL(fch->name,"TELNET:",7))
   {  fch->driverfun=Errorschemetask;
      fch->fd->name=fch->name;
      fch->fd->validate=MSG_EPART_ADDRSCHEME;
   }
   else if(STRNIEQUAL(fch->name,"MAILTO:",7))
   {  if(prefs.extmailer && prefs.mailtocmd)
      {  Spawn(FALSE,prefs.mailtocmd,
            prefs.mailtoargs?prefs.mailtoargs:NULLSTRING,
            "en",
            Unescape(fch->name+7),Agetattr(Aweb(),AOAPP_Screenname));
      }
      else if(fch->fdbase=Openaweblib("AWeb:aweblib/mail.aweblib"))
      {  fch->driverfun=AWEBLIBENTRY(fch->fdbase,0);
         fch->fd->name=fch->name;
         Asetattrs(fch->url,
            AOURL_Cacheable,FALSE,
            AOURL_Volatile,TRUE,
            TAG_END);
      }
      else
      {  fch->driverfun=Errorschemetask;
         fch->fd->name=fch->name;
         fch->fd->validate=MSG_EPART_NOPROGRAM;
      }
   }
   else if(STRNIEQUAL(fch->name,"NEWS:",5) || STRNIEQUAL(fch->name,"NNTP://",7))
   {  if(fch->fdbase=Openaweblib("AWeb:aweblib/news.aweblib"))
      {  fch->driverfun=AWEBLIBENTRY(fch->fdbase,0);
         fch->fd->name=fch->name;
         fch->flags|=FCHF_NETSLOT;
      }
      else
      {  fch->driverfun=Errorschemetask;
         fch->fd->name=fch->name;
         fch->fd->validate=MSG_EPART_NOAWEBLIB;
      }
   }
   else if(STRNIEQUAL(fch->name,"ABOUT:",6))
   {  if(fch->fdbase=Openaweblib("AWeb:aweblib/about.aweblib"))
      {  fch->driverfun=AWEBLIBENTRY(fch->fdbase,0);
         fch->fd->name=fch->name;
         Asetattrs(fch->url,AOURL_Cacheable,FALSE,TAG_END);
      }
      else
      {  fch->driverfun=Errorschemetask;
         fch->fd->name=fch->name;
         fch->fd->validate=MSG_EPART_NOAWEBLIB;
      }
   }
   else
#endif /* !LOCALONLY */
   /* file:///... and file://localhost/... are local. Host-only file://localhost
    * (no slash after host) is valid file-root and must not fall through: the
    * FILE:/// eight-char probe fails at name[7] ('l' vs '/'). */
   if(STRNIEQUAL(fch->name,"FILE://localhost/",17)
        || STRNIEQUAL(fch->name,"FILE:///",8)
        || ((fch->name && strlen((const char *)fch->name)>=16)
            && STRNIEQUAL(fch->name,"FILE://LOCALHOST",16)
            && (!fch->name[16] || fch->name[16]=='/')))
   {  UBYTE *upath;
      UBYTE *slash;
      fch->driverfun=Localfiletask;
      /* After "file://" (7), path is "/Volume:..." or "localhost/..."; strchr must not run past missing '/'. */
      upath=Unescape(fch->name+7);
      slash=strchr(upath,'/');
      if(slash) fch->fd->name=slash+1;
      else if(STRNIEQUAL(upath,"LOCALHOST",9) && !upath[9])
         fch->fd->name=upath+9;
      else fch->fd->name=upath;
      fch->flags|=FCHF_LOCALSLOT;
      if(!prefs.cachelocalhost)
      {  Asetattrs(fch->url,AOURL_Cacheable,FALSE,TAG_END);
      }
   }
   else if(STRNIEQUAL(fch->name,"JAVASCRIPT:",11))
   {  struct Jcontext *jc;
      struct Jvar *jv;
      UBYTE *result;
      if(prefs.dojs && Openjslib())
      {  Runjavascript(fch->jframe,Unescape(fch->name+11),NULL);
         jc=(struct Jcontext *)Agetattr(Aweb(),AOAPP_Jcontext);
         if(jv=Jgetreturnvalue(jc))
         {  result=Jtostring(jc,jv);
            if(fch->postmsg) FREE(fch->postmsg);
            fch->postmsg=Dupstr(result,-1);
            fch->fd->postmsg=fch->postmsg;
            fch->fd->validate=Today();
            fch->driverfun=Javascripttask;
            Asetattrs(fch->url,AOURL_Cacheable,FALSE,TAG_END);
         }
         else
         {  /* No defined result from script, ensure it's not cached */
            Asetattrs(fch->url,AOURL_Cacheable,FALSE,TAG_END);
         }
      }
   }
   else if(STRNIEQUAL(fch->name,"X-AWEB:MAIL/",12))
   {  if(fch->fdbase=Openaweblib("AWeb:aweblib/mail.aweblib"))
      {  fch->driverfun=AWEBLIBENTRY(fch->fdbase,0);
         fch->fd->name=fch->name;
         Asetattrs(fch->url,
            AOURL_Cacheable,FALSE,
            AOURL_Volatile,TRUE,
            TAG_END);
      }
   }
   else if(STRNIEQUAL(fch->name,"X-AWEB:NEWS",11))
   {  if(fch->fdbase=Openaweblib("AWeb:aweblib/news.aweblib"))
      {  fch->driverfun=AWEBLIBENTRY(fch->fdbase,0);
         fch->fd->name=fch->name;
         Asetattrs(fch->url,
            AOURL_Cacheable,FALSE,
            AOURL_Volatile,TRUE,
            TAG_END);
      }
   }
   else if(STRNIEQUAL(fch->name,"X-AWEB:",7))
   {  fch->driverfun=Xawebtask;
      fch->fd->name=fch->name+7;
      Asetattrs(fch->url,AOURL_Cacheable,FALSE,TAG_END);
   }
   else if(STRNIEQUAL(fch->name,"X-JSGENERATED:",14))
   {  if(fch->postmsg) FREE(fch->postmsg);
      if(!Getjsgeneratedtext(fch->name,&fch->postmsg))
      {  fch->fd->flags|=FDVF_JSOPEN;
      }
      if(fch->postmsg)
      {  fch->fd->postmsg=fch->postmsg;
         fch->driverfun=Jsgeneratedtask;
         Asetattrs(fch->url,AOURL_Cacheable,FALSE,TAG_END);
      }
   }
   else if(STRNIEQUAL(fch->name,"X-NIL:",6))
   {  /* Do nothing */
   }
   else if(STRNIEQUAL(fch->name,"CID:",4))
   {  fch->driverfun=Cidurltask;
      fch->fd->name=fch->name+4;  /* Skip "cid:" prefix */
      Asetattrs(fch->url,
         AOURL_Cacheable,FALSE,
         AOURL_Volatile,TRUE,
         TAG_END);
   }
   else if(STRNIEQUAL(fch->name,"DATA:",5))
   {  fch->driverfun=Dataurltask;
      fch->fd->name=fch->name+5;  /* Skip "data:" prefix */
      Asetattrs(fch->url,
         AOURL_Cacheable,FALSE,
         AOURL_Volatile,TRUE,
         TAG_END);
   }
   else
#ifdef LOCALONLY
   {  /* Do nothing */
   }
#else
   {  fch->driverfun=Errorschemetask;
      fch->fd->name=fch->name;
      fch->fd->validate=MSG_EPART_ADDRSCHEME;
   }
#endif
}

/*------------------------------------------------------------------------*/

/* Dispose a Fetchdriver */
static void Disposefd(struct Fetchdriver *fd)
{  if(fd)
   {  /* referer and postmsg are shared with Fetch - don't free these here. */
      if(fd->proxy) FREE(fd->proxy);
      if(fd->block) FREE(fd->block);
      if(fd->etag) FREE(fd->etag);
      FREE(fd);
   }
}

/* Create the fetch driver process */
static BOOL Startdriver(struct Fetch *fch)
{  BOOL result=FALSE;
   extern BOOL httpdebug;
   fch->driverfun=NULL;
   if(!(fch->fd=ALLOCSTRUCT(Fetchdriver,1,MEMF_PUBLIC|MEMF_CLEAR))) return FALSE;
   fch->fd->fetch=fch;
   if(!(fch->fd->block=ALLOCTYPE(UBYTE,INPUTBLOCKSIZE,MEMF_PUBLIC))) return FALSE;
   fch->fd->blocksize=INPUTBLOCKSIZE;
   fch->fd->validate=fch->validate;
   fch->fd->etag=fch->etag?Dupstr(fch->etag,-1):NULL;
   if(fch->flags&FCHF_NOCACHE)
   {  fch->fd->flags|=FDVF_NOCACHE;
   }
   if(fch->flags&FCHF_COMMANDS)
   {  fch->fd->flags|=FDVF_COMMANDS;
   }
   if(fch->flags&FCHF_FORMWARN)
   {  fch->fd->flags|=FDVF_FORMWARN;
   }
   fch->fd->postmsg=fch->postmsg;
   fch->fd->multipart=fch->mpd;
   fch->total=0;
   fch->sofar=0;
   fch->fd->prefs=&prefs;
   fch->fd->prefssema=&prefssema;
   /* Include referer only if it is not localhost, and if not browsing anonymously.
    * But: if target is x-aweb:, mailto:, or cid:, always include referer. */
   fch->fd->referer=fch->referer;
   ObtainSemaphore(&prefssema);
   if(fch->fd->referer
   && !STRNIEQUAL(fch->name,"X-AWEB:",7)
   && !STRNIEQUAL(fch->name,"MAILTO:",7)
   && !STRNIEQUAL(fch->name,"CID:",4))
   {  if(STRNIEQUAL(fch->fd->referer,"FILE://",7)
      || STRNIEQUAL(fch->fd->referer,"X-AWEB:",7)) fch->fd->referer=NULL;
      if(!prefs.referer) fch->fd->referer=NULL;
   }
   /* Don't proxy if limited proxy usage and it's a POST, or
    * if it is in the noproxy list. */
   if(prefs.limitproxy)
   {  if(fch->postmsg || fch->mpd)
      {  fch->flags|=FCHF_NOPROXY;
      }
   }
   if(Isinlist(&prefs.noproxy,fch->name))
   {  fch->flags|=FCHF_NOPROXY;
   }
   if(Isinlist(&prefs.nocache,fch->name))
   {  Asetattrs(fch->url,AOURL_Cacheable,FALSE,TAG_END);
   }
   Driverfunction(fch);
   ReleaseSemaphore(&prefssema);
   if(httpdebug)
   {  printf("[FETCH] Startdriver: url=%s flags=0x%08lX driverfun=%p netslot=%ld localslot=%ld nrnet=%ld/%ld nrlocal=%ld/%ld\n",
         fch->name? (char *)fch->name : "(null)",
         (ULONG)fch->flags,
         fch->driverfun,
         (long)BOOLVAL(fch->flags&FCHF_NETSLOT),
         (long)BOOLVAL(fch->flags&FCHF_LOCALSLOT),
         (long)nrnet,(long)prefs.maxconnect,
         (long)nrlocal,(long)prefs.maxdiskread);
   }
   if(fch->driverfun)
   {  if((fch->flags&FCHF_LOCALSLOT) && nrlocal>=prefs.maxdiskread)
      {  REMOVE(fch);
         Addtoqueue(&localqueue,fch);
         fch->flags|=FCHF_QUEUED;
         if(fch->netstat) Chgnetstat(fch->netstat,NWS_QUEUED,0,0);
         else fch->netstat=Addnetstat(fch,(UBYTE *)Agetattr(fch->url,AOURL_Url),NWS_QUEUED,FALSE);
         if(httpdebug)
         {  printf("[FETCH] Startdriver: QUEUED-LOCAL url=%s nrlocal=%ld/%ld\n",
               fch->name? (char *)fch->name : "(null)",
               (long)nrlocal,(long)prefs.maxdiskread);
         }
         result=TRUE;
      }
      else if((fch->flags&FCHF_NETSLOT) && nrnet>=prefs.maxconnect)
      {  REMOVE(fch);
         Addtoqueue(&netqueue,fch);
         fch->flags|=FCHF_QUEUED;
         if(fch->netstat) Chgnetstat(fch->netstat,NWS_QUEUED,0,0);
         else fch->netstat=Addnetstat(fch,(UBYTE *)Agetattr(fch->url,AOURL_Url),NWS_QUEUED,TRUE);
         if(httpdebug)
         {  printf("[FETCH] Startdriver: QUEUED-NET url=%s nrnet=%ld/%ld\n",
               fch->name? (char *)fch->name : "(null)",
               (long)nrnet,(long)prefs.maxconnect);
         }
         result=TRUE;
      }
      else
      {  if(fch->flags&FCHF_CHANNEL)
         {  REMOVE(fch);
            ADDTAIL(&channels,fch);
         }
         result=Dostartdriver(fch);
      }
   }
   return result;
}

/*------------------------------------------------------------------------*/

/* Return from 'incomplete' requester */
static void Incompletereturn(short code,struct Fetch *fch)
{  switch(code)
   {  case 0:  /* cancel */
         Asrcupdatetags(fch->url,fch,
            AOURL_Error,TRUE,
            AOURL_Eof,TRUE,
            AOURL_Terminate,TRUE,
            TAG_END);
         fch->flags|=FCHF_DISPOSED;
         break;
      case 1:  /* ok */
         Asrcupdatetags(fch->url,fch,
            AOURL_Eof,TRUE,
            AOURL_Terminate,TRUE,
            TAG_END);
         fch->flags|=FCHF_DISPOSED;
         break;
      case 2:  /* retry */
         if(Startdriver(fch))
         {  Asrcupdatetags(fch->url,fch,
               AOURL_Reload,TRUE,
               TAG_END);
         }
         else
         {  Asrcupdatetags(fch->url,fch,
               AOURL_Eof,TRUE,
               AOURL_Terminate,TRUE,
               TAG_END);
            fch->flags|=FCHF_DISPOSED;
         }
         break;
   }
   fch->flags&=~FCHF_INCOMPLETE;
   if(fch->flags&FCHF_DISPOSED)
   {  Adisposeobject(fch);
   }
}

/* Open 'incomplete' requester */
static BOOL Incompleterequest(struct Fetch *fch)
{  UBYTE *p=AWEBSTR(MSG_FILE_INCTEXT);
   UBYTE *buf,*url;
   long len;
   BOOL ok=FALSE;
   url=(UBYTE *)Agetattr(fch->url,AOURL_Url);
   len=strlen(p)+strlen(url)+80;
   if(buf=ALLOCTYPE(UBYTE,len,0))
   {  Lprintf(buf,p,url,fch->total,fch->sofar);
      if(haiku)
      {
         strncpy(buf, HAIKU22, len - 1);
         buf[len - 1] = '\0';
      }
      ok=Asyncrequestcc(AWEBSTR(MSG_REQUEST_TITLE),buf,AWEBSTR(MSG_FILE_INCBUTTONS2),
         Incompletereturn,fch,url);
      if(ok)
      {  fch->flags&=~FCHF_RUNNING;
         fch->flags|=FCHF_INCOMPLETE;
      }
      else
      {  FREE(buf);
      }
   }
   return ok;
}

/*------------------------------------------------------------------------*/

/* Process output from driver */
static long Updatefetch(struct Fetch *fch,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   BOOL forward=TRUE,dispose=FALSE,retryget=FALSE,statusset=FALSE;
   ULONG netstat=0;
   UBYTE buf[40];
   ULONG statustag=TAG_IGNORE;
   if(tstate)
   {  while(tag=NextTagItem(&tstate))
      {  switch(tag->ti_Tag)
         {  case AOURL_Contentlength:
               fch->total=tag->ti_Data;
               break;
            case AOURL_Datalength:
               fch->sofar+=tag->ti_Data;
               if(!netstat) netstat=NWS_READ;
               break;
            case AOURL_Data:
               /* Stop driver if not modified */
               if(fch->flags&FCHF_NOTMODIFIED)
               {  Asetattrs(fch->task,
                     AOTSK_Stop,TRUE,
                     AOTSK_Async,TRUE,
                     TAG_END);
                  forward=FALSE;
               }
               break;
            case AOURL_Terminate:
               /* Forward and dispose only if no error after redirected POST */
               if(fch->flags&FCHF_POSTNOGOOD)
               {  forward=FALSE;
                  retryget=TRUE;
               }
               else if(!(fch->flags&(FCHF_CANCELLED|FCHF_NOTMODIFIED|FCHF_DISPOSED))
               && fch->sofar<fch->total)
               {  /* Don't forward, open 'incomplete' requester. */
                  if(Incompleterequest(fch))
                  {  forward=FALSE;
                  }
                  else
                  {  dispose=TRUE;
                  }
               }
               else
               {  dispose=TRUE;
               }
               Adisposeobject(fch->task);
               fch->task=NULL;
               Checkqueues(fch);
               break;
            case AOURL_Status:
               strncpy(fch->statusbuf,(UBYTE *)tag->ti_Data,STATUSBUFSIZE-1);
               tag->ti_Data=(ULONG)fch->statusbuf;
               statusset=TRUE;
               break;
            case AOURL_Netstatus:
               netstat=tag->ti_Data;
               forward=FALSE;
               break;
            case AOURL_Notmodified:
               if(tag->ti_Data) fch->flags|=FCHF_NOTMODIFIED;
               else fch->flags&=~FCHF_NOTMODIFIED;
               forward=FALSE;
               break;
            case AOURL_Serverdate:
               fch->serverdate=tag->ti_Data;
               forward=FALSE;
               break;
            case AOURL_Expires:
               /* translate to local time: */
               if(fch->serverdate)
               {  tag->ti_Data=tag->ti_Data-fch->serverdate+Today();
               }
               break;
            case AOURL_Arexxcommand:
               Sendarexxcmd(fch->windowkey,(UBYTE *)tag->ti_Data);
               break;
            case AOURL_Postnogood:
               if(tag->ti_Data)
               {  Asetattrs(fch->task,
                     AOTSK_Stop,TRUE,
                     AOTSK_Async,TRUE,
                     TAG_END);
                  fch->flags|=FCHF_POSTNOGOOD;
                  forward=FALSE;
               }
               else fch->flags&=~FCHF_POSTNOGOOD;
               break;
            case AOURL_Reload:
               fch->total=0;
               fch->sofar=0;
               break;
            case AOURL_Movedto:
            case AOURL_Tempmovedto:
            case AOURL_Seeother:
               fch->total=0;  /* Prevent "incomplete file" requester */
               break;
            case AOURL_Gohistory:
               Gohistory(Findwindow(fch->windowkey),(long)tag->ti_Data);
               break;
         }
      }
   }
   /* If we are already disposed, or we should retry, don't forward. */
   if(fch->flags&(FCHF_DISPOSED|FCHF_POSTNOGOOD))
   {  forward=FALSE;
   }
   /* Forward this message. Build status text */
   if(forward && fch->sofar && !statusset)
   {  strcpy(buf,AWEBSTR(MSG_AWEB_BYTESREAD));
      if(fch->total)
      {  strcat(buf,": %d/%d");
         sprintf(fch->statusbuf,buf,fch->sofar,fch->total);
      }
      else
      {  strcat(buf,": %d");
         sprintf(fch->statusbuf,buf,fch->sofar);
      }
      statustag=AOURL_Status;
   }
   if(forward)
   {  Asrcupdatetags(fch->url,fch,
         statustag,fch->statusbuf,
         AOURL_Contentlength,fch->total,
         AOURL_Datatotal,fch->sofar,
         ams->tags?TAG_MORE:TAG_END,ams->tags);
   }
   if(netstat && fch->netstat)
   {  Chgnetstat(fch->netstat,netstat,fch->sofar,fch->total);
   }
   /* Try again with no post message */
   if(retryget)
   {  fch->flags&=~(FCHF_RUNNING|FCHF_POSTNOGOOD);
      Disposefd(fch->fd);
      fch->fd=NULL;
      if(fch->postmsg)
      {  FREE(fch->postmsg);
         fch->postmsg=NULL;
      }
      if(fch->mpd)
      {  Freemultipartdata(fch->mpd);
         fch->mpd=NULL;
      }
      if(!Startdriver(fch))
      {  Asrcupdatetags(fch->url,fch,
            AOURL_Terminate,TRUE,
            TAG_END);
         Checkqueues(fch);
         Adisposeobject(fch);
      }
   }
   /* Dispose ourselves if our time has come */
   if(dispose)
   {  fch->flags&=~FCHF_RUNNING;
      Adisposeobject(fch);
   }
   return 0;
}

static long Setfetch(struct Fetch *fch,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   BOOL cancel=FALSE,cancelerr=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFCH_Url:
            fch->url=(void *)tag->ti_Data;
            break;
         case AOFCH_Name:
            fch->name=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFCH_Ifmodifiedsince:
            fch->validate=tag->ti_Data;
            break;
         case AOFCH_Etag:
            if(fch->etag) FREE(fch->etag);
            fch->etag=tag->ti_Data?Dupstr((UBYTE *)tag->ti_Data,-1):NULL;
            break;
         case AOFCH_Nocache:
            if(tag->ti_Data) fch->flags|=FCHF_NOCACHE;
            else fch->flags&=~FCHF_NOCACHE;
            break;
         case AOFCH_Cache:
            if(tag->ti_Data) fch->flags|=FCHF_CACHE;
            else fch->flags&=~FCHF_CACHE;
            break;
         case AOFCH_Cancel:
            if(tag->ti_Data) cancel=cancelerr=TRUE;
            break;
         case AOFCH_Cancellocal:
            if(tag->ti_Data && (fch->flags&FCHF_LOCALSLOT)) cancel=TRUE;
            break;
         case AOFCH_Postmsg:
            if(fch->postmsg) FREE(fch->postmsg);
            fch->postmsg=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFCH_Multipartdata:
            if(fch->mpd) Freemultipartdata(fch->mpd);
            fch->mpd=(struct Multipartdata *)tag->ti_Data;
            break;
         case AOFCH_Referer:
            if(fch->referer) FREE(fch->referer);
            fch->referer=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFCH_Windowkey:
            fch->windowkey=tag->ti_Data;
            break;
         case AOFCH_Noproxy:
            if(tag->ti_Data) fch->flags|=FCHF_NOPROXY;
            else fch->flags&=~FCHF_NOPROXY;
            break;
         case AOFCH_Loadflags:
            fch->loadflags=tag->ti_Data;
            break;
         case AOFCH_Imagefetch:
            SETFLAG(fch->flags,FCHF_IMAGE,tag->ti_Data);
            break;
         case AOFCH_Commands:
            SETFLAG(fch->flags,FCHF_COMMANDS,tag->ti_Data);
            break;
         case AOFCH_Jframe:
            fch->jframe=(void *)tag->ti_Data;
            break;
         case AOFCH_Formwarn:
            SETFLAG(fch->flags,FCHF_FORMWARN,tag->ti_Data);
            break;
         case AOFCH_Channel:
            SETFLAG(fch->flags,FCHF_CHANNEL,tag->ti_Data);
            break;
      }
   }
   if(cancel)
   {  fch->flags|=FCHF_CANCELLED;
      if(fch->flags&FCHF_RUNNING)
      {  if(cancelerr)
         {  Asrcupdatetags(fch->url,fch,
               AOURL_Error,TRUE,
               TAG_END);
         }
         Asetattrs(fch->task,
            AOTSK_Stop,TRUE,
            AOTSK_Async,TRUE,
            TAG_END);
         /* Will be disposed when task returns */
      }
      else if(fch->flags&FCHF_QUEUED)
      {  Asrcupdatetags(fch->url,fch,
            AOURL_Terminate,TRUE,
            TAG_END);
         Adisposeobject(fch);
      }
   }
   return 0;
}

static void Disposefetch(struct Fetch *fch)
{  if(fch->flags&FCHF_RUNNING)
   {  Asetattrs(fch->task,
         AOTSK_Stop,TRUE,
         AOTSK_Async,TRUE,
         TAG_END);
      fch->flags|=FCHF_DISPOSED;
   }
   else if(fch->flags&FCHF_INCOMPLETE)
   {  fch->flags|=FCHF_DISPOSED;
   }
   else
   {  REMOVE(fch);
      if(fch->netstat) Chgnetstat(fch->netstat,NWS_END,0,0);
      if(fch->fd) Disposefd(fch->fd);
      if(fch->fdbase) CloseLibrary(fch->fdbase);
      if(fch->name) FREE(fch->name);
      if(fch->postmsg) FREE(fch->postmsg);
      if(fch->mpd) Freemultipartdata(fch->mpd);
      if(fch->referer) FREE(fch->referer);
      if(fch->etag) FREE(fch->etag);
      Amethodas(AOTP_OBJECT,fch,AOM_DISPOSE);
      Checkwaitrequests();
   }
}

static struct Fetch *Newfetch(struct Amset *ams)
{  struct Fetch *fch;
   if(fch=Allocobject(AOTP_FETCH,sizeof(struct Fetch),ams))
   {  ADDTAIL(&running,fch);
      Setfetch(fch,ams);
      if(!fch->name || !Startdriver(fch))
      {  Disposefetch(fch);
         fch=NULL;
      }
   }
   return fch;
}

static long Getfetch(struct Fetch *fch,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFCH_Url:
            PUTATTR(tag,fch->url);
            break;
         case AOFCH_Postmsg:
            PUTATTR(tag,fch->postmsg);
            break;
         case AOFCH_Multipartdata:
            PUTATTR(tag,fch->mpd);
            break;
         case AOFCH_Referer:
            PUTATTR(tag,fch->referer);
            break;
         case AOFCH_Windowkey:
            PUTATTR(tag,fch->windowkey);
            break;
         case AOFCH_Loadflags:
            PUTATTR(tag,fch->loadflags);
            break;
         case AOFCH_Channelid:
            PUTATTR(tag,fch->channelid);
            break;
         case AOFCH_Etag:
            PUTATTR(tag,fch->etag);
            break;
      }
   }
   return 0;
}

static long Dispatch(struct Fetch *fch,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newfetch((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setfetch(fch,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatefetch(fch,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getfetch(fch,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposefetch(fch);
         break;
      case AOM_DEINSTALL:
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

/* Cancel all fetches that have the specified referer URL string */
void Cancelfetchesbyreferer(UBYTE *refererurl)
{  struct Fetch *fch,*next;
   if(!refererurl) return;
   /* Check all fetch lists: running, netqueue, localqueue, channels */
   for(fch=running.first;fch->next;fch=next)
   {  next=fch->next;
      if(fch->referer && STRIEQUAL(fch->referer,refererurl))
      {  Asetattrs(fch,AOFCH_Cancel,TRUE,TAG_END);
      }
   }
   for(fch=netqueue.first;fch->next;fch=next)
   {  next=fch->next;
      if(fch->referer && STRIEQUAL(fch->referer,refererurl))
      {  Asetattrs(fch,AOFCH_Cancel,TRUE,TAG_END);
      }
   }
   for(fch=localqueue.first;fch->next;fch=next)
   {  next=fch->next;
      if(fch->referer && STRIEQUAL(fch->referer,refererurl))
      {  Asetattrs(fch,AOFCH_Cancel,TRUE,TAG_END);
      }
   }
   for(fch=channels.first;fch->next;fch=next)
   {  next=fch->next;
      if(fch->referer && STRIEQUAL(fch->referer,refererurl))
      {  Asetattrs(fch,AOFCH_Cancel,TRUE,TAG_END);
      }
   }
}

/* Cancel all fetches for a specific window - like CancelAll but for one window */
void Cancelwindowfetches(ULONG windowkey)
{  struct Fetch *fch,*next;
   if(!windowkey) return;
   /* Cancel all fetches in all lists for this window */
   for(fch=running.first;fch->next;fch=next)
   {  next=fch->next;
      if(fch->windowkey==windowkey && !(fch->flags&FCHF_CANCELLED))
      {  Asetattrs(fch,AOFCH_Cancel,TRUE,TAG_END);
      }
   }
   for(fch=netqueue.first;fch->next;fch=next)
   {  next=fch->next;
      if(fch->windowkey==windowkey && !(fch->flags&FCHF_CANCELLED))
      {  Asetattrs(fch,AOFCH_Cancel,TRUE,TAG_END);
      }
   }
   for(fch=localqueue.first;fch->next;fch=next)
   {  next=fch->next;
      if(fch->windowkey==windowkey && !(fch->flags&FCHF_CANCELLED))
      {  Asetattrs(fch,AOFCH_Cancel,TRUE,TAG_END);
      }
   }
   for(fch=channels.first;fch->next;fch=next)
   {  next=fch->next;
      if(fch->windowkey==windowkey && !(fch->flags&FCHF_CANCELLED))
      {  Asetattrs(fch,AOFCH_Cancel,TRUE,TAG_END);
      }
   }
}

BOOL Installfetch(void)
{  NEWLIST(&netqueue);
   NEWLIST(&localqueue);
   NEWLIST(&running);
   NEWLIST(&channels);
   NEWLIST(&waitrequests);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_FETCH,Dispatch)) return FALSE;
   return TRUE;
}

BOOL Transferring(void)
{  return (BOOL)(!ISEMPTY(&running) || !ISEMPTY(&netqueue) || !ISEMPTY(&localqueue)
      || !ISEMPTY(&channels));
}

/* Check if there are active network transfers for a specific window */
BOOL Windowtransferring(ULONG windowkey)
{  struct Fetch *fch;
   void *url;
   if(!windowkey) return FALSE;
   /* Check running fetches - these are actively transferring */
   for(fch=running.first;fch->next;fch=fch->next)
   {  if(fch->windowkey==windowkey && (fch->flags&FCHF_RUNNING) && !(fch->flags&FCHF_CANCELLED) && !(fch->flags&FCHF_DISPOSED))
      {  /* Verify this is a network transfer, not just a local/cache operation */
         url=(void *)Agetattr(fch,AOFCH_Url);
         if(url && Agetattr(url,AOURL_Input))
         {  return TRUE;
         }
      }
   }
   /* Check network queue - these are queued for network transfer */
   for(fch=netqueue.first;fch->next;fch=fch->next)
   {  if(fch->windowkey==windowkey && !(fch->flags&FCHF_CANCELLED) && !(fch->flags&FCHF_DISPOSED))
      {  return TRUE;
      }
   }
   /* Check channels - active channel transfers */
   for(fch=channels.first;fch->next;fch=fch->next)
   {  if(fch->windowkey==windowkey && (fch->flags&FCHF_RUNNING) && !(fch->flags&FCHF_CANCELLED) && !(fch->flags&FCHF_DISPOSED))
      {  return TRUE;
      }
   }
   /* Don't check localqueue - those are cache/local file operations, not network transfers */
   return FALSE;
}

void Windowfetchstats(ULONG windowkey,long *netrunning,long *netqueued,
   long *localqueued,long *imgrunning,long *imgqueued)
{  struct Fetch *fch;
   long nrun=0,nq=0,lq=0,irun=0,iq=0;
   if(!windowkey)
   {  if(netrunning) *netrunning=0;
      if(netqueued) *netqueued=0;
      if(localqueued) *localqueued=0;
      if(imgrunning) *imgrunning=0;
      if(imgqueued) *imgqueued=0;
      return;
   }
   for(fch=running.first;fch->next;fch=fch->next)
   {  if(fch->windowkey==windowkey
      && (fch->flags&FCHF_RUNNING)
      && !(fch->flags&FCHF_CANCELLED) && !(fch->flags&FCHF_DISPOSED))
      {  if(fch->flags&FCHF_NETSLOT) nrun++;
         if(fch->flags&FCHF_IMAGE) irun++;
      }
   }
   for(fch=netqueue.first;fch->next;fch=fch->next)
   {  if(fch->windowkey==windowkey
      && !(fch->flags&FCHF_CANCELLED) && !(fch->flags&FCHF_DISPOSED))
      {  nq++;
         if(fch->flags&FCHF_IMAGE) iq++;
      }
   }
   for(fch=localqueue.first;fch->next;fch=fch->next)
   {  if(fch->windowkey==windowkey
      && !(fch->flags&FCHF_CANCELLED) && !(fch->flags&FCHF_DISPOSED))
      {  lq++;
         if(fch->flags&FCHF_IMAGE) iq++;
      }
   }
   for(fch=channels.first;fch->next;fch=fch->next)
   {  if(fch->windowkey==windowkey
      && (fch->flags&FCHF_RUNNING)
      && !(fch->flags&FCHF_CANCELLED) && !(fch->flags&FCHF_DISPOSED))
      {  if(fch->flags&FCHF_NETSLOT) nrun++;
         if(fch->flags&FCHF_IMAGE) irun++;
      }
   }
   if(netrunning) *netrunning=nrun;
   if(netqueued) *netqueued=nq;
   if(localqueued) *localqueued=lq;
   if(imgrunning) *imgrunning=irun;
   if(imgqueued) *imgqueued=iq;
}

/* Global scheduler: active slot counts (nrnet/nrlocal) plus fetches waiting in each queue
 * (same lists Checkqueues drains); not the prefs maxconnect/maxdiskread caps.
 */
void Fetchslotfills(long *netactive,long *netqueued,long *localactive,long *localqueued)
{  struct Fetch *fch;
   long nq;
   long lq;

   if(netactive) *netactive=nrnet;
   if(localactive) *localactive=nrlocal;
   if(netqueued)
   {  nq=0;
      for(fch=netqueue.first;fch->next;fch=fch->next)
      {  if(!(fch->flags&FCHF_CANCELLED) && !(fch->flags&FCHF_DISPOSED)) nq++;
      }
      *netqueued=nq;
   }
   if(localqueued)
   {  lq=0;
      for(fch=localqueue.first;fch->next;fch=fch->next)
      {  if(!(fch->flags&FCHF_CANCELLED) && !(fch->flags&FCHF_DISPOSED)) lq++;
      }
      *localqueued=lq;
   }
}

void Addwaitrequest(struct Arexxcmd *ac,ULONG windowkey,BOOL doc,BOOL img,void *url)
{  struct Waitrequest *wr;
   if(wr=ALLOCSTRUCT(Waitrequest,1,MEMF_CLEAR))
   {  wr->cmd=ac;
      wr->windowkey=windowkey;
      wr->doc=doc;
      wr->img=img;
      wr->url=url;
      ADDTAIL(&waitrequests,wr);
      Checkwaitrequests();
   }
   else
   {  Replyarexxcmd(ac);
   }
}

/* Check if a URL has any active fetches (in any queue) */
BOOL Isurlloading(void *url)
{  struct Fetch *fch;
   if(!url) return FALSE;
   for(fch=running.first;fch->next;fch=fch->next)
   {  if(fch->url==url) return TRUE;
   }
   for(fch=netqueue.first;fch->next;fch=fch->next)
   {  if(fch->url==url) return TRUE;
   }
   for(fch=localqueue.first;fch->next;fch=fch->next)
   {  if(fch->url==url) return TRUE;
   }
   for(fch=channels.first;fch->next;fch=fch->next)
   {  if(fch->url==url) return TRUE;
   }
   return FALSE;
}

static void Channelreplied(void *task,struct Taskmsg *msg)
{  struct Amset *ams;
   struct TagItem *tstate,*tag;
   if(msg && msg->amsg && msg->amsg->method==AOM_SET)
   {  ams=(struct Amset *)msg->amsg;
      if(ams->tags)
      {  if(tstate=ams->tags)
         {  while(tag=NextTagItem(&tstate))
            {  switch(tag->ti_Tag)
               {  case AOFCC_Arexxcmd:
                     if(tag->ti_Data) Replyarexxcmd((struct Arexxcmd *)tag->ti_Data);
                     break;
                  case AOFCC_Header:
                  case AOFCC_Data:
                     FREE((void *)tag->ti_Data);
                     break;
               }
            }
         }
      }
   }
}

BOOL Channelfetch(struct Arexxcmd *ac,long id,...)
{  BOOL ok=FALSE;
   struct Fetch *fch;
   for(fch=channels.first;fch->next;fch=fch->next)
   {  if(fch->channelid==id) break;
   }
   if(fch && fch->next && (fch->flags&FCHF_RUNNING) && !(fch->flags&FCHF_DISPOSED))
   {  ok=TRUE;
      Asetattrsasync(fch->task,
         AOTSK_Replied,Channelreplied,
         AOFCC_Arexxcmd,ac,
         TAG_MORE,VARARG(id));
   }
   return ok;
}
