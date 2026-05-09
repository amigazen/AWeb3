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

/* framejs.c - AWeb frame JS interface */

#include "aweb.h"
#include "frame.h"
#include "application.h"
#include "window.h"
#include "copy.h"
#include "url.h"
#include "timer.h"
#include "winhis.h"
#include "frprivate.h"
#include "jslib.h"
#include "xhrjs.h"
#include <time.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

static struct Buffer *jgenbuf=NULL;
static BOOL jgenclosed=FALSE;
static UBYTE jgenurl[256];
static long jgennr=0;
static BOOL nobanners=FALSE;

static ULONG maptimeout[]={ AOTIM_Ready,AOFRM_Timertoready,TAG_END };

/*-----------------------------------------------------------------------*/

/* Timeout stuff */

static void Disposetimeout(struct Timeout *to)
{  if(to)
   {  if(to->timer) Adisposeobject(to->timer);
      if(to->script) FREE(to->script);
      FREE(to);
   }
}

void Triggertimeout(struct Frame *fr,void *timer)
{  struct Timeout *to;
   /* Only run the timeout script if there is still a document object */
   if(fr->jdscope)
   {  for(to=fr->timeouts.first;to->next;to=to->next)
      {  if(to->timer==timer)
         {  REMOVE(to);
            if(to->nobanners)
            {  Runjsnobanners(fr,to->script,&fr->jobject);
            }
            else
            {  Runjavascript(fr,to->script,&fr->jobject);
            }
            Disposetimeout(to);
            break;
         }
      }
   }
}

void Cleartimeouts(struct Frame *fr)
{  struct Timeout *to;
   while(to=REMHEAD(&fr->timeouts)) Disposetimeout(to);
}

/*-----------------------------------------------------------------------*/

static BOOL Propertystatus(struct Varhookdata *vd)
{  BOOL result=FALSE;
   UBYTE *status;
   struct Frame *fr=vd->hookdata;
   if(fr)
   {  switch(vd->code)
      {  case VHC_SET:
            status=Jtostring(vd->jc,vd->value);
            Asetattrs(fr->win,
               AOWIN_Hpstatus,status,
               AOWIN_Status,status,
               TAG_END);
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,(UBYTE *)Agetattr(fr->win,AOWIN_Status));
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertydefaultstatus(struct Varhookdata *vd)
{  BOOL result=FALSE;
   UBYTE *status;
   struct Frame *fr=vd->hookdata;
   if(fr)
   {  switch(vd->code)
      {  case VHC_SET:
            status=Jtostring(vd->jc,vd->value);
            if(fr->defstatus) FREE(fr->defstatus);
            fr->defstatus=Dupstr(status,-1);
            /* Make sure new text is used */
            if(fr->hittype==FHIT_DEFSTATUS) fr->hittype=0;
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,fr->defstatus);
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertyname(struct Varhookdata *vd)
{  BOOL result=FALSE;
   UBYTE *name;
   struct Frame *fr=vd->hookdata;
   if(fr)
   {  switch(vd->code)
      {  case VHC_SET:
            name=Jtostring(vd->jc,vd->value);
            Asetattrs(fr,AOFRM_Name,name,TAG_END);
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,fr->name?fr->name->name:NULLSTRING);
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertylength(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Frame *fr=vd->hookdata;
   struct Jvar *jv;
   struct Jobject *jo;
   long n;
   if(fr)
   {  switch(vd->code)
      {  case VHC_SET:
            result=TRUE;
            break;
         case VHC_GET:
            if((jv=Jproperty(vd->jc,fr->jobject,"frames"))
            && (jo=Jtoobject(vd->jc,jv))
            && (jv=Jproperty(vd->jc,jo,"length")))
            {  n=Jtonumber(vd->jc,jv);
               Jasgnumber(vd->jc,vd->value,n);
            }
            result=TRUE;
            break;
      }
   }
   return result;
}

static void Methodalert(struct Jcontext *jc)
{  struct Jvar *jv;
   UBYTE *prompt,*text,*head="JavaScript alert:\n";
   long l;
   if(jv=Jfargument(jc,0))
   {  prompt=Jtostring(jc,jv);
      if(prompt)
      {  l=strlen(prompt)+strlen(head)+1;
         if(text=ALLOCTYPE(UBYTE,l,0))
         {  strcpy(text,head);
            strcat(text,prompt);
            Syncrequest("AWeb JavaScript alert",text,"_Ok",0);
            FREE(text);
            Refreshevents();
         }
      }
   }
}

static void Methodconfirm(struct Jcontext *jc)
{  struct Jvar *jv;
   UBYTE *prompt,*text,*head="JavaScript confirm:\n";
   long l;
   BOOL result=FALSE;
   if(jv=Jfargument(jc,0))
   {  prompt=Jtostring(jc,jv);
      if(prompt)
      {  l=strlen(prompt)+strlen(head)+1;
         if(text=ALLOCTYPE(UBYTE,l,0))
         {  strcpy(text,head);
            strcat(text,prompt);
            result=BOOLVAL(Syncrequest("AWeb JavaScript confirm",text,"_Ok|_Cancel",0));
            FREE(text);
            Refreshevents();
         }
      }
   }
   Jasgboolean(jc,NULL,result);
}

static void Methodprompt(struct Jcontext *jc)
{  struct Jvar *jv;
   UBYTE *prompt="",*defstr="undefined",*retstr;
   if(jv=Jfargument(jc,0))
   {  prompt=Jtostring(jc,jv);
   }
   if(jv=Jfargument(jc,1))
   {  defstr=Jtostring(jc,jv);
   }
   retstr=Promptrequest(prompt,defstr);
   Jasgstring(jc,NULL,retstr);
   if(retstr) FREE(retstr);
   Refreshevents();
}

static void Methodsettimeout(struct Jcontext *jc)
{  struct Jvar *jv;
   UBYTE *script=NULL;
   ULONG timeout=0;
   struct Timeout *to=NULL;
   struct Frame *fr=Jointernal(Jthis(jc));
   if(fr)
   {  if(jv=Jfargument(jc,0))
      {  script=Jtostring(jc,jv);
      }
      if(jv=Jfargument(jc,1))
      {  timeout=Jtonumber(jc,jv);
         if(timeout<=0) timeout=1;
      }
      if(script && timeout)
      {  if(to=ALLOCSTRUCT(Timeout,1,0))
         {  to->script=Dupstr(script,-1);
            to->timer=Anewobject(AOTP_TIMER,
               AOBJ_Target,fr,
               AOBJ_Map,maptimeout,
               AOTIM_Waitseconds,timeout/1000,
               AOTIM_Waitmicros,(timeout%1000)*1000,
               TAG_END);
            to->nobanners=nobanners;
            ADDTAIL(&fr->timeouts,to);
         }
      }
   }
   Jasgnumber(jc,NULL,(ULONG)to);
}

static void Methodcleartimeout(struct Jcontext *jc)
{  struct Jvar *jv;
   struct Timeout *to,*toc;
   struct Frame *fr=Jointernal(Jthis(jc));
   if(fr)
   {  if(jv=Jfargument(jc,0))
      {  toc=(struct Timeout *)Jtonumber(jc,jv);
         for(to=fr->timeouts.first;to->next;to=to->next)
         {  if(to==toc)
            {  REMOVE(to);
               Disposetimeout(to);
               break;
            }
         }
      }
   }
}

static void Methodopen(struct Jcontext *jc)
{  struct Jvar *jv;
   UBYTE *urlname="",*name="",*spec=NULL,*basename,*frag;
   struct Frame *fr=Jointernal(Jthis(jc));
   struct Frame *nfr;
   struct Url *url;
   struct Jobject *njo;
   if(fr)
   {  if(jv=Jfargument(jc,0))
      {  urlname=Jtostring(jc,jv);
      }
      if(jv=Jfargument(jc,1))
      {  name=Jtostring(jc,jv);
      }
      if(jv=Jfargument(jc,2))
      {  spec=Jtostring(jc,jv);
      }
      if(name && (nfr=Targetframeoptnew(fr,name,FALSE)))
      {  if(*urlname) basename=Getjscurrenturlname(jc);
         else basename="";
         url=Findurl(basename,urlname,0);
         frag=Fragmentpart(urlname);
         Inputwindoc(nfr->win,url,frag,nfr->id);
         Jasgobject(jc,NULL,nfr->jobject);
      }
      else if(nobanners)
      {  // Don't show errors resulting from this function failing
         Jerrors(jc,prefs.browser.jserrors,JERRORS_OFF,prefs.browser.jswatch);
      }
      else if(njo=Jopenwindow(jc,fr->jobject,urlname,name,spec,fr->win))
      {  Jasgobject(jc,NULL,njo);
      }
   }
}

static void Methodclose(struct Jcontext *jc)
{  struct Jvar *jv;
   struct Frame *fr=Jointernal(Jthis(jc));
   if(fr && (fr->flags&FRMF_TOPFRAME))
   {  Jclosewindow(fr->win);
      if(jv=Jproperty(jc,fr->jobject,"closed"))
      {  Jasgboolean(jc,jv,TRUE);
      }
   }
}

static void Methodfocus(struct Jcontext *jc)
{  struct Frame *fr=Jointernal(Jthis(jc));
   if(fr)
   {  Asetattrs(fr->win,AOWIN_Focus,fr,TAG_END);
   }
}

static void Methodblur(struct Jcontext *jc)
{  struct Frame *fr=Jointernal(Jthis(jc));
   if(fr)
   {  Asetattrs(fr->win,AOWIN_Nofocus,fr,TAG_END);
   }
}

static void Methodscroll(struct Jcontext *jc)
{  struct Jvar *jv;
   long x=-1,y=-1;
   struct Frame *fr=Jointernal(Jthis(jc));
   if(fr)
   {  if(jv=Jfargument(jc,0))
      {  x=Jtonumber(jc,jv);
      }
      if(jv=Jfargument(jc,1))
      {  y=Jtonumber(jc,jv);
      }
      /* Make sure all pending updates are displayed */
      Asetattrs(fr,
         AOFRM_Updatecopy,TRUE,
         TAG_END);
      Asetattrs(fr,
         (x>=0)?AOFRM_Leftpos:TAG_IGNORE,x,
         (y>=0)?AOFRM_Toppos:TAG_IGNORE,y,
         AOFRM_Setscroller,TRUE,
         TAG_END);
   }
}

/*-----------------------------------------------------------------------*/

/* Queue an Inputwindoc(). Calling Inputwindoc() during JS processing may cause
 * loops. */
static void Queueinputwindoc(struct Frame *fr,void *url,UBYTE *fragment,UBYTE *id)
{  struct Queuedinputwindoc *qi;
   if(qi=ALLOCSTRUCT(Queuedinputwindoc,1,MEMF_CLEAR))
   {  qi->url=url;
      if(fragment) qi->fragment=Dupstr(fragment,-1);
      if(id) qi->id=Dupstr(id,-1);
      Queuesetmsgdata(fr,FQID_INPUTWINDOC,(ULONG)qi);
   }
}

/* Methods and properties of the location object */

static BOOL Commonlocproperty(struct Varhookdata *vd,USHORT which)
{  BOOL result=FALSE;
   struct Frame *fr=vd->hookdata;
   UBYTE *part,*buf,*fragment=NULL;
   long length;
   void *url=(void *)Agetattr(fr,AOFRM_Url);
   if(fr)
   {  switch(vd->code)
      {  case VHC_SET:
            part=Jtostring(vd->jc,vd->value);
            if(url=Repjspart(url,which,part))
            {  Agetattrs(fr->whis?fr->whis:fr->inputwhis,
                  AOWHS_Frameid,fr->id,
                  AOWHS_Fragment,&fragment,
                  TAG_END);
               /* Queue the Inputwindoc() call. Doing it here may cause terrible
                * loops because other frame may not recognize that the url
                * in the new Winhis for it is the same as the one it is already
                * inputting. Causing to parse the source again, and run the JS
                * script that sets _this_ attribute again, and again... */
               Queueinputwindoc(fr,url,fragment,NULL);
            }
            result=TRUE;
            break;
         case VHC_GET:
            Getjspart(url,which,&part,&length);
            if(buf=Dupstr(part,length))
            {  Jasgstring(vd->jc,vd->value,buf);
               FREE(buf);
            }
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertylochash(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Frame *fr=vd->hookdata;
   UBYTE *fragment,*hash;
   void *url;
   if(fr)
   {  switch(vd->code)
      {  case VHC_SET:
            hash=Jtostring(vd->jc,vd->value);
            if(hash && *hash=='#') hash++;
            Agetattrs(fr->whis?fr->whis:fr->inputwhis,
               AOWHS_Frameid,fr->id,
               AOWHS_Url,&url,
               TAG_END);
               /* Queue the Inputwindoc() call. Doing it here may cause terrible
                * loops because other frame may not recognize that the url
                * in the new Winhis for it is the same as the one it is already
                * inputting. Causing to parse the source again, and run the JS
                * script that sets _this_ attribute again, and again... */
               Queueinputwindoc(fr,url,hash,NULL);
            result=TRUE;
            break;
         case VHC_GET:
            Agetattrs(fr->whis?fr->whis:fr->inputwhis,
               AOWHS_Frameid,fr->id,
               AOWHS_Fragment,&fragment,
               TAG_END);
            if(fragment)
            {  if(hash=ALLOCTYPE(UBYTE,strlen(fragment)+2,0))
               {  strcpy(hash,"#");
                  strcat(hash,fragment);
                  Jasgstring(vd->jc,vd->value,hash);
                  FREE(hash);
               }
            }
            else
            {  Jasgstring(vd->jc,vd->value,"");
            }
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertylochost(struct Varhookdata *vd)
{  return Commonlocproperty(vd,UJP_HOST);
}

static BOOL Propertylochostname(struct Varhookdata *vd)
{  return Commonlocproperty(vd,UJP_HOSTNAME);
}

static BOOL Propertylochref(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Frame *fr=vd->hookdata;
   UBYTE *href,*urlname,*fragment;
   long l1,l2;
   void *url;
   if(fr)
   {  switch(vd->code)
      {  case VHC_SET:
            href=Jtostring(vd->jc,vd->value);
            if(href)
            {  fragment=Fragmentpart(href);
               if(url=Findurl(Getjscurrenturlname(vd->jc),href,0))
               {  /* Queue the Inputwindoc() call. Doing it here may cause terrible
                   * loops because other frame may not recognize that the url
                   * in the new Winhis for it is the same as the one it is already
                   * inputting. Causing to parse the source again, and run the JS
                   * script that sets _this_ attribute again, and again... */
                  Queueinputwindoc(fr,url,fragment,NULL);
               }
            }
            result=TRUE;
            break;
         case VHC_GET:
            Agetattrs(fr->whis?fr->whis:fr->inputwhis,
               AOWHS_Frameid,fr->id,
               AOWHS_Url,&url,
               AOWHS_Fragment,&fragment,
               TAG_END);
            urlname=(UBYTE *)Agetattr(url,AOURL_Url);
            if(urlname)
            {  if(STRNIEQUAL(urlname,"x-jsgenerated:",14))
               {  for(urlname+=14;*urlname && *urlname!='/';urlname++);
                  if(*urlname) urlname++;
               }
               l1=l2=strlen(urlname);
               if(fragment) l2+=strlen(fragment)+1;
               if(href=ALLOCTYPE(UBYTE,l2+1,0))
               {  strcpy(href,urlname);
                  if(fragment)
                  {  href[l1]='#';
                     strcpy(href+l1+1,fragment);
                  }
                  Jasgstring(vd->jc,vd->value,href);
                  FREE(href);
               }
            }
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertylocpathname(struct Varhookdata *vd)
{  return Commonlocproperty(vd,UJP_PATHNAME);
}

static BOOL Propertylocport(struct Varhookdata *vd)
{  return Commonlocproperty(vd,UJP_PORT);
}

static BOOL Propertylocprotocol(struct Varhookdata *vd)
{  return Commonlocproperty(vd,UJP_PROTOCOL);
}

static BOOL Propertylocsearch(struct Varhookdata *vd)
{  return Commonlocproperty(vd,UJP_SEARCH);
}

static void Methodlocreload(struct Jcontext *jc)
{  struct Jvar *jv;
   struct Frame *fr=Jointernal(Jthis(jc));
   if(fr)
   {  if((jv=Jfargument(jc,0)) && Jtoboolean(jc,jv))
      {  /* True reload */
         Asetattrs(fr,AOFRM_Reload,TRUE,TAG_END);
      }
      else
      {  /* Reparse only.... *****/
      }
   }
}

static void Methodlocreplace(struct Jcontext *jc)
{  struct Jvar *jv;
   UBYTE *urlname,*fragment=NULL;
   void *url=NULL,*whis;
   struct Frame *fr=Jointernal(Jthis(jc));
   if(fr)
   {  if(jv=Jfargument(jc,0))
      {  urlname=Jtostring(jc,jv);
         fragment=Fragmentpart(urlname);
         if(url=Findurl(Getjscurrenturlname(jc),urlname,0))
         {  Asetattrs(fr->whis?fr->whis:fr->inputwhis,
               AOWHS_Frameid,fr->id,
               AOWHS_Url,url,
               AOWHS_Fragment,fragment,
               TAG_END);
            /* Clear out our winhis pointer, or else frame won't detect
             * that a new URL was requested (it gets the current URL
             * from winhis, but that has changed already) */
            whis=fr->whis;
            fr->whis=NULL;
            fr->inputwhis=NULL;
            Asetattrs(fr,AOBJ_Winhis,whis,TAG_END);
            Changedlayout();
         }
      }
   }
}

static void Methodloctostring(struct Jcontext *jc)
{  struct Frame *fr=Jointernal(Jthis(jc));
   void *url=NULL;
   UBYTE *fragment=NULL,*urlname,*href;
   long l1,l2;
   if(fr)
   {  Agetattrs(fr->whis?fr->whis:fr->inputwhis,
         AOWHS_Frameid,fr->id,
         AOWHS_Url,&url,
         AOWHS_Fragment,&fragment,
         TAG_END);
      urlname=(UBYTE *)Agetattr(url,AOURL_Url);
      if(urlname)
      {  l1=l2=strlen(urlname);
         if(fragment) l2+=strlen(fragment)+1;
         if(href=ALLOCTYPE(UBYTE,l2+1,0))
         {  strcpy(href,urlname);
            if(fragment)
            {  href[l1]='#';
               strcpy(href+l1+1,fragment);
            }
            Jasgstring(jc,NULL,href);
            FREE(href);
         }
      }
   }
}

static BOOL Propertylocation(struct Varhookdata *vd)
{  BOOL result=FALSE;
   switch(vd->code)
   {  case VHC_SET:
         /* Set location.href */
         result=Propertylochref(vd);
         break;
      case VHC_GET:
         /* Do default GET of object */
         result=FALSE;
         break;
   }
   return result;
}

/*-----------------------------------------------------------------------*/

/* Methods and properties of the history object */

static void Historygodelta(struct Frame *fr,long delta)
{  void *whis;
   whis=(void *)Agetattr(fr->win,AOWIN_Hiswinhis);
   while(whis && delta>0)
   {  whis=(void *)Agetattr(whis,AOWHS_Next);
      delta--;
   }
   while(whis && delta<0)
   {  whis=(void *)Agetattr(whis,AOWHS_Previous);
      delta++;
   }
   if(whis)
   {  Queuesetmsgdata(fr,FQID_HISTORY,(ULONG)whis);
   }
}

/* Do a case-insensitive string search in a string */
static UBYTE *Strstri(UBYTE *str,UBYTE *sub)
{  long l=strlen(sub);
   UBYTE *end=str+strlen(str)-l;
   for(;str<=end;str++)
   {  if(STRNIEQUAL(str,sub,l)) return str;
   }
   return NULL;
}

/* Go back or forward in history to the nearest URL that contains this string */
static void Historygostring(struct Frame *fr,UBYTE *str)
{  void *whis,*cwhis,*whis1,*url;
   UBYTE *urlname;
   long d,d1;
   /* Get the winhis to start with (the current one ) */
   cwhis=(void *)Agetattr(fr->win,AOWIN_Hiswinhis);
   /* Search back until a matching URL found. Count the number of steps. */
   whis=cwhis;
   d1=0;
   whis1=NULL;
   for(d=0;;d++)
   {  whis=(void *)Agetattr(whis,AOWHS_Previous);
      if(!whis) break;
      Agetattrs(whis,
         AOWHS_Frameid,fr->id,
         AOWHS_Url,&url,
         TAG_END);
      urlname=(UBYTE *)Agetattr(url,AOURL_Url);
      if(urlname && Strstri(urlname,str))
      {  /* Found one */
         d1=d;
         whis1=whis;
         break;
      }
   }
   /* Search forward until a matching URL found, or d1 steps are taken. */
   whis=cwhis;
   for(d=0;d<d1;d++)
   {  whis=(void *)Agetattr(whis,AOWHS_Next);
      if(!whis) break;
      Agetattrs(whis,
         AOWHS_Frameid,fr->id,
         AOWHS_Url,&url,
         TAG_END);
      urlname=(UBYTE *)Agetattr(url,AOURL_Url);
      if(urlname && Strstri(urlname,str))
      {  /* Found one */
         whis1=whis;
         break;
      }
   }
   /* If whis1 is not NULL, it's a matching winhis. */
   if(whis1)
   {  Queuesetmsgdata(fr,FQID_INPUTWINDOC,(ULONG)whis);
   }
}

static void Methodhisgo(struct Jcontext *jc)
{  struct Jvar *jv;
   struct Frame *fr=Jointernal(Jthis(jc));
   if(fr)
   {  if(jv=Jfargument(jc,0))
      {  if(Jisnumber(jv))
         {  Historygodelta(fr,Jtonumber(jc,jv));
         }
         else
         {  Historygostring(fr,Jtostring(jc,jv));
         }
      }
   }
}

static void Methodhisback(struct Jcontext *jc)
{  struct Frame *fr=Jointernal(Jthis(jc));
   Historygodelta(fr,-1);
}

static void Methodhisforward(struct Jcontext *jc)
{  struct Frame *fr=Jointernal(Jthis(jc));
   Historygodelta(fr,1);
}

struct Historynode
{  NODE(Historynode);
   UBYTE *urlname;
};

static struct Historynode *Newhistorynode(void *url)
{  struct Historynode *hn;
   if(hn=ALLOCSTRUCT(Historynode,1,0))
   {  hn->urlname=Dupstr((UBYTE *)Agetattr(url,AOURL_Url),-1);
   }
   return hn;
}

static struct Jobject *Makehistory(struct Frame *fr,struct Jcontext *jc)
{  struct Jobject *his;
   struct Jvar *jv;
   void *cwhis=NULL,*whis,*url;
   struct Historynode *hn,*hnc=NULL,*hnp=NULL,*hnn=NULL;
   LIST(Historynode) list;
   UBYTE pname[16];
   long length;
   NEWLIST(&list);
   /* First build a list of all history entries in sequence. */
   /* Add the current url */
   Agetattrs(fr->whis,
      AOWHS_Frameid,fr->id,
      AOWHS_Leadingwinhis,&cwhis,
      TAG_END);
   if(cwhis)
   {  Agetattrs(cwhis,
         AOWHS_Frameid,fr->id,
         AOWHS_Url,&url,
         TAG_END);
      if(hnc=Newhistorynode(url)) ADDTAIL(&list,hnc);
   }
   /* Walk back in history. */
   whis=cwhis;
   for(;;)
   {  Agetattrs(whis,
         AOWHS_Frameid,fr->id,
         AOWHS_Previnframe,&whis,
         TAG_END);
      if(!whis) break;
      Agetattrs(whis,
         AOWHS_Frameid,fr->id,
         AOWHS_Url,&url,
         TAG_END);
      if(hn=Newhistorynode(url)) ADDHEAD(&list,hn);
      if(!hnp) hnp=hn;
   }
   /* Walk forward in history */
   whis=cwhis;
   for(;;)
   {  Agetattrs(whis,
         AOWHS_Frameid,fr->id,
         AOWHS_Nextinframe,&whis,
         TAG_END);
      if(!whis) break;
      Agetattrs(whis,
         AOWHS_Frameid,fr->id,
         AOWHS_Url,&url,
         TAG_END);
      if(hn=Newhistorynode(url)) ADDTAIL(&list,hn);
      if(!hnn) hnn=hn;
   }
   /* Now create the history object */
   if(his=Newjobject(jc))
   {  Setjobject(his,NULL,fr,NULL);
      if(jv=Jproperty(jc,his,"current"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgstring(jc,jv,hnc?hnc->urlname:NULLSTRING);
      }
      if(jv=Jproperty(jc,his,"previous"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgstring(jc,jv,hnp?hnp->urlname:NULLSTRING);
      }
      if(jv=Jproperty(jc,his,"next"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgstring(jc,jv,hnn?hnn->urlname:NULLSTRING);
      }
   }
   length=0;
   while(hn=REMHEAD(&list))
   {  sprintf(pname,"%d",length++);
      if(his && (jv=Jproperty(jc,his,pname)))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgstring(jc,jv,hn->urlname);
      }
      FREE(hn->urlname);
      FREE(hn);
   }
   if(jv=Jproperty(jc,his,"length"))
   {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
      Jasgnumber(jc,jv,length);
   }
   Addjfunction(jc,his,"back",Methodhisback,NULL);
   Addjfunction(jc,his,"forward",Methodhisforward,NULL);
   Addjfunction(jc,his,"go",Methodhisgo,"deltaOrLocation",NULL);
   return his;
}

/*-----------------------------------------------------------------------*/

static BOOL Feedback(struct Jcontext *jc)
{  BOOL run=TRUE;
   struct Frame *fr=(struct Frame *)Jgetuserdata(jc);
   Setanimgads(TRUE);
   Refreshevents();
   if(fr) run=!Agetattr(fr->win,AOBJ_Jscancel);
   return run;
}

/*-----------------------------------------------------------------------*/

long Jsetupframe(struct Frame *fr,struct Amjsetup *amj)
{  struct Jvar *jv;
   struct Jobject *loc,*his,*frames;
   UBYTE buf[16];
   long i,length;
   BOOL add;
   if(prefs.browser.dojs && Openjslib())
   {  Jallowgc(amj->jc,FALSE);
      if(!fr->jobject)
      {  fr->jobject=Newjobject(amj->jc);
         fr->flags&=~(FRMF_JSIMAGECTR|FRMF_JSOPTIONCTR);
         if(fr->jobject)
         {  Jkeepobject(fr->jobject,TRUE);
            if(jv=Jproperty(amj->jc,fr->jobject,"opener"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgobject(amj->jc,jv,NULL);
            }
            if(jv=Jproperty(amj->jc,fr->jobject,"closed"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgboolean(amj->jc,jv,FALSE);
            }
         }
      }
      if(fr->jobject)
      {  Setjobject(fr->jobject,NULL,fr,NULL);
         if(amj->parentframe && fr->name
         && (jv=Jproperty(amj->jc,amj->parentframe,fr->name->name)))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgobject(amj->jc,jv,fr->jobject);
         }
         if(amj->parentframe && (frames=Jfindarray(amj->jc,amj->parentframe,"frames")))
         {  if(fr->name
            && (jv=Jproperty(amj->jc,frames,fr->name->name)))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgobject(amj->jc,jv,fr->jobject);
            }
            /* Only add if we are not yet in the array */
            add=TRUE;
            if(jv=Jproperty(amj->jc,frames,"length"))
            {  length=Jtonumber(amj->jc,jv);
               for(i=0;i<length;i++)
               {  sprintf(buf,"%d",i);
                  if(!(jv=Jproperty(amj->jc,frames,buf))) break;
                  if(Jtoobject(amj->jc,jv)==fr->jobject)
                  {  add=FALSE;
                     break;
                  }
               }
            }
            if(add)
            {  if(jv=Jnewarrayelt(amj->jc,frames))
               {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                  Jasgobject(amj->jc,jv,fr->jobject);
               }
            }
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"defaultStatus"))
         {  Setjproperty(jv,Propertydefaultstatus,fr);
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"frames"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            if(fr->jframes || (fr->jframes=Newjarray(amj->jc)))
            {  Jasgobject(amj->jc,jv,fr->jframes);
               Jsetobjasfunc(fr->jframes,TRUE);
            }
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"length"))
         {  Setjproperty(jv,Propertylength,fr);
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"name"))
         {  Setjproperty(jv,Propertyname,fr);
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"onerror"))
         {  /* There are no run time JS error requesters in AWeb */
            Jasgobject(amj->jc,jv,NULL);
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"parent"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgobject(amj->jc,jv,amj->parentframe?amj->parentframe:fr->jobject);
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"screen"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgobject(amj->jc,jv,(struct Jobject *)Agetattr(Aweb(),AOAPP_Jscreen));
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"self"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgobject(amj->jc,jv,fr->jobject);
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"status"))
         {  Setjproperty(jv,Propertystatus,fr);
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"top"))
         {  if(fr->flags&FRMF_TOPFRAME)
            {  Jasgobject(amj->jc,jv,fr->jobject);
            }
            else if(amj->parent)
            {  struct Jvar *jptop=Jproperty(amj->jc,amj->parentframe,"top");
               Jasgobject(amj->jc,jv,Jtoobject(amj->jc,jptop));
            }
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"window"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgobject(amj->jc,jv,fr->jobject);
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"location"))
         {  Setjproperty(jv,Propertylocation,fr);
            if(loc=Jtoobject(amj->jc,jv))
            {  Clearjobject(loc,NULL);
            }
            if(loc=Newjobject(amj->jc))
            {  Setjobject(loc,NULL,fr,NULL);
               Jasgobject(amj->jc,jv,loc);
               if(jv=Jproperty(amj->jc,loc,"hash"))
               {  Setjproperty(jv,Propertylochash,fr);
                  Jpprotect(jv,fr->jprotect);
               }
               if(jv=Jproperty(amj->jc,loc,"host"))
               {  Setjproperty(jv,Propertylochost,fr);
                  Jpprotect(jv,fr->jprotect);
               }
               if(jv=Jproperty(amj->jc,loc,"hostname"))
               {  Setjproperty(jv,Propertylochostname,fr);
                  Jpprotect(jv,fr->jprotect);
               }
               if(jv=Jproperty(amj->jc,loc,"href"))
               {  Setjproperty(jv,Propertylochref,fr);
                  Jpprotect(jv,fr->jprotect);
               }
               if(jv=Jproperty(amj->jc,loc,"pathname"))
               {  Setjproperty(jv,Propertylocpathname,fr);
                  Jpprotect(jv,fr->jprotect);
               }
               if(jv=Jproperty(amj->jc,loc,"port"))
               {  Setjproperty(jv,Propertylocport,fr);
                  Jpprotect(jv,fr->jprotect);
               }
               if(jv=Jproperty(amj->jc,loc,"protocol"))
               {  Setjproperty(jv,Propertylocprotocol,fr);
                  Jpprotect(jv,fr->jprotect);
               }
               if(jv=Jproperty(amj->jc,loc,"search"))
               {  Setjproperty(jv,Propertylocsearch,fr);
                  Jpprotect(jv,fr->jprotect);
               }
               Addjfunction(amj->jc,loc,"reload",Methodlocreload,"trueReload",NULL);
               Addjfunction(amj->jc,loc,"replace",Methodlocreplace,"URL",NULL);
               Addjfunction(amj->jc,loc,"toString",Methodloctostring,NULL);
               Freejobject(loc);
            }
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"history"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            if(his=Jtoobject(amj->jc,jv))
            {  Clearjobject(his,NULL);
            }
            Jpprotect(jv,fr->jprotect);
            if(his=Makehistory(fr,amj->jc))
            {  Jasgobject(amj->jc,jv,his);
               Freejobject(his);
            }
         }
         Addjfunction(amj->jc,fr->jobject,"alert",Methodalert,"message",NULL);
         Addjfunction(amj->jc,fr->jobject,"blur",Methodblur,NULL);
         Addjfunction(amj->jc,fr->jobject,"clearTimeout",Methodcleartimeout,"timeoutID",NULL);
         Addjfunction(amj->jc,fr->jobject,"close",Methodclose,NULL);
         Addjfunction(amj->jc,fr->jobject,"confirm",Methodconfirm,"message",NULL);
         Addjfunction(amj->jc,fr->jobject,"focus",Methodfocus,NULL);
         Addjfunction(amj->jc,fr->jobject,"open",Methodopen,"URL","name","spec",NULL);
         Addjfunction(amj->jc,fr->jobject,"prompt",Methodprompt,"message","inputDefault",NULL);
         Addjfunction(amj->jc,fr->jobject,"scroll",Methodscroll,"x_coordinate","y_coordinate",NULL);
         Addjfunction(amj->jc,fr->jobject,"setTimeout",Methodsettimeout,"expression","msec",NULL);
         Jaddeventhandler(amj->jc,fr->jobject,"onfocus",fr->onfocus);
         Jaddeventhandler(amj->jc,fr->jobject,"onblur",fr->onblur);
         if(!(fr->flags&FRMF_JSIMAGECTR))
         {  Addimageconstructor(amj->jc,fr->jobject);
            fr->flags|=FRMF_JSIMAGECTR;
         }
         if(!(fr->flags&FRMF_JSOPTIONCTR))
         {  Addoptionconstructor(amj->jc,fr->jobject);
            fr->flags|=FRMF_JSOPTIONCTR;
         }
         if(!(fr->flags&FRMF_JSXHRCTR))
         {  Addxhrconstructor(amj->jc,fr->jobject);
            fr->flags|=FRMF_JSXHRCTR;
         }
         if(jv=Jproperty(amj->jc,fr->jobject,"navigator"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgobject(amj->jc,jv,(struct Jobject *)Agetattr(Aweb(),AOAPP_Jnavigator));
         }
         Ajsetup(fr->copy,amj->jc,fr->jobject,fr->jobject);
      }
      Jallowgc(amj->jc,TRUE);
   }
   return 0;
}

void Freejframe(struct Frame *fr)
{  struct Timeout *to;
   struct Jcontext *jc;
   struct Jvar *jv;
   if(fr->jobject)
   {  jc=(struct Jcontext *)Agetattr(Aweb(),AOAPP_Jcontext);
      if(jc && (jv=Jproperty(jc,fr->jobject,"closed")))
      {  Jasgboolean(jc,jv,TRUE);
      }
      Disposejobject(fr->jobject);
      fr->jobject=NULL;
   }
   if(fr->jframes)
   {  Disposejobject(fr->jframes);
      fr->jframes=NULL;
   }
   fr->jdscope=NULL;
   while(to=REMHEAD(&fr->timeouts)) Disposetimeout(to);
}

void Clearjframe(struct Frame *fr)
{  struct Timeout *to;
   static UBYTE *except[]={ "closed", "opener", NULL };
   if(fr->jobject)
   {  Clearjobject(fr->jobject,except);
      fr->flags&=~(FRMF_JSIMAGECTR|FRMF_JSOPTIONCTR|FRMF_JSXHRCTR);
   }
   if(fr->jframes)
   {  Disposejobject(fr->jframes);
      fr->jframes=NULL;
   }
   fr->jdscope=NULL;
   while(to=REMHEAD(&fr->timeouts)) Disposetimeout(to);
}

/* Start the load of a JS generated document. Fetch will call Getjsgenerated()
 * to obtain details that are set here (and earlier). Note this will all
 * run synchroneously and single-threaded so static variables for these
 * details will work. */
void Loadjgenerated(struct Frame *fr)
{  void *url;
   UBYTE *urlname,*p;
   if(fr->jgenerated && fr->win)
   {  sprintf(jgenurl,"x-jsgenerated:%d/",++jgennr);
      if(urlname=(UBYTE *)Agetattr((void *)Agetattr(fr,AOFRM_Url),AOURL_Url))
      {  if(STRNEQUAL(urlname,"x-jsgenerated:",14))
         {  if(p=strchr(urlname,'/'))
            {  urlname=p+1;
            }
         }
         strncat(jgenurl,urlname,sizeof(jgenurl)-strlen(jgenurl)-1);
      }
      jgenbuf=fr->jgenerated;
      jgenclosed=!(fr->flags&FRMF_JSOPEN);
      if(url=Findurl(NULL,jgenurl,0))
      {  Inputwindoc(fr->win,url,NULL,fr->id);
      }
   }
   fr->jgenerated=NULL;
   fr->flags&=~FRMF_JSOPEN;
}

/*-----------------------------------------------------------------------*/

BOOL Runjavascriptwith(struct Frame *fr,UBYTE *script,struct Jobject **jthisp,
   struct Aobject *with)
{  struct Jcontext *jc;
   BOOL result=TRUE,animon;
   struct Jobject *jthis;
   UBYTE *p;
   struct Jobject *jgscope[4];
   short i;
   long jerr;
   extern BOOL httpdebug;
   UBYTE jsdbg[160];
   UBYTE jshead[64];
   long slen;
   long k;
   BPTR jsfh;
   LONG jsn;
   if(fr && script && prefs.browser.dojs && Openjslib())
   {  fr=Bodyframe(fr);
      if(!fr)
      {  return FALSE;
      }
      /* Get jc first, then call Ajsetup - the reentrancy guard in Jsetupapplication
       * will prevent deadlocks from recursive calls */
      jc=(struct Jcontext *)Agetattr(Aweb(),AOAPP_Jcontext);
      if(httpdebug)
      {  printf("[JS] Runjavascriptwith: enter fr=%p bodyfr=%p jc=%p script=%p len=%ld win=%p jobj=%p jdscope=%p with=%p\n",
            fr, fr, jc, script, (long)strlen((char *)script),
            (fr ? fr->win : NULL),
            (fr ? fr->jobject : NULL),
            (fr ? fr->jdscope : NULL),
            with);
      }
      /* Extra crash-localization: show a short preview of the script.
       * Don't dereference jc here (framejs.c doesn't know struct Jcontext layout). */
      if(httpdebug && jc && script)
      {
         slen=(long)strlen((char *)script);
         if(slen < 0) slen=0;
         k=0;
         while(k < (long)sizeof(jshead)-1 && k < slen)
         {
            UBYTE c=script[k];
            if(c=='\r' || c=='\n' || c=='\t') c=' ';
            if(c<32) c='.';
            jshead[k]=c;
            k++;
         }
         jshead[k]='\0';
         sprintf((char *)jsdbg,"[JS] Runjavascriptwith: slen=%ld head=\"%s\"\n",
            (long)slen,(char *)jshead);
         jsfh=Output();
         if(jsfh)
         {
            jsn=(LONG)strlen((char *)jsdbg);
            if(jsn>0) Write(jsfh,(APTR)jsdbg,jsn);
         }
      }
      if(httpdebug)
      {
         strcpy((char *)jsdbg,"[JS] Runjavascriptwith: stage=before_Ajsetup\n");
         jsfh=Output();
         if(jsfh)
         {
            jsn=(LONG)strlen((char *)jsdbg);
            if(jsn>0) Write(jsfh,(APTR)jsdbg,jsn);
         }
      }
      /* Avoid global application-wide Ajsetup() here: during inline script execution (while parsing),
       * walking all windows/frames can hit half-built objects. Set up only the current frame/copy. */
      if(fr && fr->copy && jc && fr->jobject)
      {
         Ajsetup(fr->copy,jc,fr->jobject,fr->jobject);
      }
      else
      {
         Ajsetup(Aweb(),NULL,NULL,NULL);
      }
      if(httpdebug)
      {
         strcpy((char *)jsdbg,"[JS] Runjavascriptwith: stage=after_Ajsetup\n");
         jsfh=Output();
         if(jsfh)
         {
            jsn=(LONG)strlen((char *)jsdbg);
            if(jsn>0) Write(jsfh,(APTR)jsdbg,jsn);
         }
      }
      if(!jc) jc=(struct Jcontext *)Agetattr(Aweb(),AOAPP_Jcontext);
      if(jc) Jsetfeedback(jc,Feedback);
      if(jthisp && *jthisp) jthis=*jthisp;
      else if(fr && fr->jobject) jthis=fr->jobject;
      else
      {  return FALSE;
      }
      if(httpdebug)
      {
         strcpy((char *)jsdbg,"[JS] Runjavascriptwith: stage=after_jthis_select\n");
         jsfh=Output();
         if(jsfh)
         {
            jsn=(LONG)strlen((char *)jsdbg);
            if(jsn>0) Write(jsfh,(APTR)jsdbg,jsn);
         }
      }
      /* If (jthis) is our own object, only use us as global scope, not the
       * document. */
      if(!fr || !fr->jobject)
      {  return FALSE;
      }
      jgscope[0]=fr->jobject;
      i=1;
      if(jthis!=fr->jobject && fr->jdscope)
      {  jgscope[i++]=fr->jdscope;
      }
      if(with)
      {  jgscope[i++]=(struct Jobject *)Agetattr(with,AOBJ_Jobject);
      }
      jgscope[i]=NULL;
      if(jc && jthis)
      {  if(script)
         {  long len=strlen(script);
            for(p=script;p<script+len;p++)
            {  if(*p==0xa0) *p=' ';
            }
         }
         /* for compatabilty purposes ignore a leading "javascript:" */
         if(script && prefs.browser.htmlmode!=HTML_STRICT)
         {  UBYTE *p;
            p=strchr(script,':');
            if(p && STRNIEQUAL(script,"javascript:",p+1-script))
            {  script=p+1;
            }
         }
         animon=Setanimgads(TRUE);
         jerr=(prefs.browser.jserrors?JERRORS_ON:JERRORS_OFF);
         if(prefs.browser.dojs==DOJS_ALL) jerr=JERRORS_CONTINUE;
         Jerrors(jc,prefs.browser.jserrors,jerr,prefs.browser.jswatch);
#ifndef DEMOVERSION
         Jdebug(jc,Agetattr(fr->win,AOWIN_Jsdebug));
#endif
         if(httpdebug)
         {  printf("[JS] Runjavascriptwith: before Runjprogram fr=%p jc=%p fscope=%p jthis=%p prot=%lu\n",
               fr, jc, (fr ? fr->jobject : NULL), jthis, (unsigned long)fr->jprotect);
         }
         result=Runjprogram(jc,fr->jobject,script,jthis,jgscope,fr->jprotect,(ULONG)fr);
         if(httpdebug)
         {  printf("[JS] Runjavascriptwith: after Runjprogram fr=%p jc=%p result=%ld\n",
               fr, jc, (long)result);
         }
         if(httpdebug)
         {  printf("[JS] Runjavascriptwith: before GC fr=%p jc=%p AWebJSBase=%p\n",
               fr, jc, AWebJSBase);
         }
         /* Original AWeb 3.4 code only GC'd when timer seconds advanced; many short scripts never
          * collected and awebjs allocations showed up as leaks. Fix by collecting after each run. */
         if(AWebJSBase) Jgarbagecollect(jc);
         if(httpdebug)
         {  printf("[JS] Runjavascriptwith: after GC fr=%p jc=%p\n",
               fr, jc);
         }
         if(!animon) Setanimgads(FALSE);
         if(httpdebug)
         {  printf("[JS] Runjavascriptwith: exit fr=%p jc=%p result=%ld\n",
               fr, jc, (long)result);
         }
      }
   }
   return result;
}

BOOL Runjavascript(struct Frame *fr,UBYTE *script,struct Jobject **jthisp)
{  return Runjavascriptwith(fr,script,jthisp,NULL);
}

BOOL Runjsnobanners(struct Frame *fr,UBYTE *script,struct Jobject **jthisp)
{  BOOL result;
   BOOL oldnobanners=nobanners;
   nobanners=prefs.browser.nobanners;
   result=Runjavascript(fr,script,jthisp);
   nobanners=oldnobanners;
   return result;
}

void *Getjsframe(struct Jcontext *jc)
{  return (void *)Jgetuserdata(jc);
}

void *Getjsdocument(struct Jcontext *jc)
{  struct Frame *fr=(struct Frame *)Jgetuserdata(jc);
   return (void *)Agetattr(fr->copy,AOCPY_Driver);
}

BOOL Getjsgeneratedtext(UBYTE *urlname,UBYTE **bufferp)
{  UBYTE *text=NULL;
   BOOL closed=jgenclosed;
   if(STREQUAL(urlname,jgenurl) && jgenbuf)
   {  text=Dupstr(jgenbuf->buffer?jgenbuf->buffer:NULLSTRING,jgenbuf->length);
      jgenbuf=NULL;
      jgenclosed=FALSE;
      *jgenurl='\0';
   }
   if(bufferp) *bufferp=text;
   return closed;
}

UBYTE *Getjscurrenturlname(struct Jcontext *jc)
{  struct Frame *fr=(struct Frame *)Jgetuserdata(jc);
   void *url;
   UBYTE *urlname="";
   if(fr)
   {  url=(void *)Agetattr(fr,AOFRM_Url);
      if(url) urlname=(UBYTE *)Agetattr(url,AOURL_Finalurl);
   }
   return urlname;
}

void Jprotframe(struct Frame *fr,ULONG jprotect)
{  struct Jvar *jv;
   struct Jobject *jo;
   struct Jcontext *jc=(struct Jcontext *)Agetattr(Aweb(),AOAPP_Jcontext);
   fr->jprotect=jprotect;
   if(Getjsframe(jc)==fr)
   {  /* We are running the current script; change the context protection too. */
      Jcprotect(jc,jprotect);
   }
   if(jv=Jproperty(jc,fr->jobject,"location"))
   {  if(jo=Jtoobject(jc,jv))
      {  if(jv=Jproperty(jc,jo,"hash")) Jpprotect(jv,jprotect);
         if(jv=Jproperty(jc,jo,"host")) Jpprotect(jv,jprotect);
         if(jv=Jproperty(jc,jo,"hostname")) Jpprotect(jv,jprotect);
         if(jv=Jproperty(jc,jo,"href")) Jpprotect(jv,jprotect);
         if(jv=Jproperty(jc,jo,"pathname")) Jpprotect(jv,jprotect);
         if(jv=Jproperty(jc,jo,"port")) Jpprotect(jv,jprotect);
         if(jv=Jproperty(jc,jo,"protocol")) Jpprotect(jv,jprotect);
         if(jv=Jproperty(jc,jo,"search")) Jpprotect(jv,jprotect);
      }
   }
   if(jv=Jproperty(jc,fr->jobject,"history"))
   {  Jpprotect(jv,jprotect);
   }
}
