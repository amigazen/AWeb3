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

/* cookie.c - AWeb cookie manager */

#include "aweb.h"
#include "cache.h"
#include "asyncio.h"
#include "application.h"
#include "arexx.h"
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <reaction/reaction_class.h>
#include <classes/window.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <images/label.h>
#include <intuition/intuition.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/button.h>
#include <proto/layout.h>
#include <proto/window.h>
#include <proto/label.h>
#include <proto/utility.h>

#define IOBUFSIZE 8192

#ifndef LOCALONLY

struct Usenode
{  NODE(Usenode);
};

struct Cookie
{  NODE(Cookie);
   struct Usenode use;
   UBYTE *name;
   UBYTE *value;
   ULONG expires;
   UBYTE *domain;
   UBYTE *path;
   UBYTE *comment;
   short pathlen,domainlen;
   short version;
   UBYTE flags;
   long size;
};

#define COOF_DOMAIN  0x0001   /* Domain was given in original set */
#define COOF_PATH    0x0002   /* Path was given in original set */
#define COOF_ACCEPT  0x0004   /* Accept changes to this cookie */
#define COOF_SECURE  0x0008   /* Only send back over secure line */

/* Get cookie address from Usenode */
#define COOKIE(u) ((struct Cookie *)((ULONG)u-8))

/* List contains cookies sorted on pathlen, descending */
static LIST(Cookie) cookies;

static LIST(Usenode) usenodes;
static long cookiesize=0;

static struct SignalSemaphore cookiesema;

enum COOKIEGADGET_IDS
{  CGID_ONCE=1,CGID_ACCEPT,CGID_NEVER,CGID_CANCEL,
};

enum COOKIE_ACTIONS
{  COOKIE_REJECT=0,COOKIE_ONCE=1,COOKIE_ACCEPT=2,
};

/*----------------------------------------------------------------------*/

/* Build matching label and value strings, values do wordwrap. */
static void Wraptext(struct Buffer *lbuf,UBYTE *label,struct Buffer *vbuf,UBYTE *text)
{  UBYTE *p;
   long start,line,lastsp;
   if(lbuf->length)
   {  Addtobuffer(lbuf,"\n",1);
      Addtobuffer(vbuf,"\n",1);
   }
   Addtobuffer(lbuf,label,strlen(label));
   Addtobuffer(lbuf," ",1);
   start=vbuf->length;
   line=vbuf->length;
   lastsp=-1;
   for(p=text;*p;p++)
   {  if(isspace(*p)) lastsp=vbuf->length;
      Addtobuffer(vbuf,p,1);
      if(*p=='\n')
      {  line=vbuf->length;
         lastsp=-1;
         Addtobuffer(lbuf,"\n ",2);
      }
      else if(vbuf->length-line>50)
      {  if(lastsp>=0)
         {  vbuf->buffer[lastsp]='\n';
            line=lastsp+1;
            lastsp=-1;
         }
         else
         {  Addtobuffer(vbuf,"\n",1);
            line=vbuf->length;
            lastsp=-1;
         }
         Addtobuffer(lbuf,"\n ",2);
      }
   }
}

/* Ask before setting a cookie. */
static short Askcookie(UBYTE *domain,UBYTE *path,UBYTE *name,UBYTE *value,
   UBYTE *expires,UBYTE *maxage,UBYTE *comment,BOOL secure)
{  struct Buffer lbuf={0},vbuf={0};
   void *winobj;
   struct Window *window;
   UBYTE *screenname;
   struct Screen *screen;
   ULONG mask,got,result;
   BOOL done=FALSE;
   short ok=COOKIE_REJECT;
   Wraptext(&lbuf,AWEBSTR(MSG_COOKIE_NAME),&vbuf,name);
   Wraptext(&lbuf,AWEBSTR(MSG_COOKIE_VALUE),&vbuf,value);
   Wraptext(&lbuf,AWEBSTR(MSG_COOKIE_DOMAIN),&vbuf,domain);
   Wraptext(&lbuf,AWEBSTR(MSG_COOKIE_PATH),&vbuf,path);
   if(comment) Wraptext(&lbuf,AWEBSTR(MSG_COOKIE_COMMENT),&vbuf,comment);
   if(maxage) Wraptext(&lbuf,AWEBSTR(MSG_COOKIE_MAXAGE),&vbuf,maxage);
   if(expires) Wraptext(&lbuf,AWEBSTR(MSG_COOKIE_EXPIRES),&vbuf,expires);
   Addtobuffer(&lbuf,"",1);
   Addtobuffer(&vbuf,"",1);
   screenname=(UBYTE *)Agetattr(Aweb(),AOAPP_Screenname);
   screen=LockPubScreen(screenname);
   if(screen)
   {  winobj=WindowObject,
         WA_Title,AWEBSTR(MSG_COOKIE_TITLE),
         WA_AutoAdjust,TRUE,
         WA_CloseGadget,TRUE,
         WA_DepthGadget,TRUE,
         WA_DragBar,TRUE,
         WA_Activate,Awebactive(),
         WA_PubScreen,screen,
         WINDOW_Position,WPOS_CENTERSCREEN,
         WINDOW_Layout,VLayoutObject,
            LAYOUT_SpaceOuter,TRUE,
            StartImage,LabelObject,
               LABEL_SoftStyle,FSF_BOLD,
               LABEL_Text,AWEBSTR(MSG_COOKIE_WARNING),
            EndImage,
            CHILD_WeightedHeight,0,
            StartMember,HLayoutObject,
               LAYOUT_BevelStyle,BVS_GROUP,
               LAYOUT_SpaceOuter,TRUE,
               LAYOUT_ShrinkWrap,TRUE,
               StartImage,LabelObject,
                  LABEL_Justification,LJ_RIGHT,
                  LABEL_Text,lbuf.buffer,
                  LABEL_Underscore,0,
               EndImage,
               StartImage,LabelObject,
                  LABEL_Text,vbuf.buffer,
                  LABEL_Underscore,0,
               EndImage,
            EndMember,
            StartImage,LabelObject,
               LABEL_Text,AWEBSTR(secure?MSG_COOKIE_SECURE:MSG_COOKIE_UNSECURE),
               LABEL_Underscore,0,
            EndImage,
            CHILD_WeightedHeight,0,
            StartMember,HLayoutObject,
               LAYOUT_EvenSize,TRUE,
               StartMember,ButtonObject,
                  GA_Text,AWEBSTR(MSG_COOKIE_ONCE),
                  GA_ID,CGID_ONCE,
                  GA_RelVerify,TRUE,
               EndMember,
               CHILD_WeightedWidth,0,
               StartMember,ButtonObject,
                  GA_Text,AWEBSTR(MSG_COOKIE_ACCEPT),
                  GA_ID,CGID_ACCEPT,
                  GA_RelVerify,TRUE,
#ifdef DEMOVERSION
                  GA_Disabled,TRUE,
#endif
               EndMember,
               CHILD_WeightedWidth,0,
               StartMember,ButtonObject,
                  GA_Text,AWEBSTR(MSG_COOKIE_NEVER),
                  GA_ID,CGID_NEVER,
                  GA_RelVerify,TRUE,
#ifdef DEMOVERSION
                  GA_Disabled,TRUE,
#endif
               EndMember,
               CHILD_WeightedWidth,0,
               StartMember,ButtonObject,
                  GA_Text,AWEBSTR(MSG_COOKIE_CANCEL),
                  GA_ID,CGID_CANCEL,
                  GA_RelVerify,TRUE,
               EndMember,
               CHILD_WeightedWidth,0,
            EndMember,
            CHILD_WeightedHeight,0,
         EndMember,
      EndWindow;
      if(winobj)
      {  if(window=RA_OpenWindow(winobj))
         {  GetAttr(WINDOW_SigMask,winobj,&mask);
            while(!done)
            {  got=Wait(mask|SIGBREAKF_CTRL_C);
               if(got&SIGBREAKF_CTRL_C) break;
               while((result=RA_HandleInput(winobj,NULL))!=WMHI_LASTMSG)
               {  switch(result&WMHI_CLASSMASK)
                  {  case WMHI_CLOSEWINDOW:
                        done=TRUE;
                        break;
                     case WMHI_GADGETUP:
                        switch(result&WMHI_GADGETMASK)
                        {  case CGID_ONCE:
                              ok=COOKIE_ONCE;
                              done=TRUE;
                              break;
#ifndef DEMOVERSION
                           case CGID_ACCEPT:
                              ok=COOKIE_ACCEPT;
                              done=TRUE;
                              break;
#endif
                           case CGID_CANCEL:
                              done=TRUE;
                              break;
#ifndef DEMOVERSION
                           case CGID_NEVER:
                              Addtonocookie(domain);
                              done=TRUE;
                              break;
#endif
                        }
                        break;
                     case WMHI_RAWKEY:
                        switch(result&WMHI_GADGETMASK)
                        {  case 0x43:  /* Num Enter */
                           case 0x44:  /* Enter */
                              ok=COOKIE_ONCE;
                              done=TRUE;
                              break;
                           case 0x45:  /* ESC */
                              done=TRUE;
                              break;
                        }
                        break;
                  }
               }
            }
         }
         DisposeObject(winobj);
      }
   }
   Freebuffer(&lbuf);
   Freebuffer(&vbuf);
   if(screen) UnlockPubScreen(NULL,screen);
   return ok;
}

/*----------------------------------------------------------------------*/

static void Deletecookie(struct Cookie *ck)
{  if(ck)
   {  if(ck->use.next) REMOVE(&ck->use);
      if(ck->name) FREE(ck->name);
      if(ck->value) FREE(ck->value);
      if(ck->domain) FREE(ck->domain);
      if(ck->path) FREE(ck->path);
      if(ck->comment) FREE(ck->comment);
      cookiesize-=ck->size;
      FREE(ck);
   }
}

/* Remember this cookie. Delete any old cookies with same keys */
static void Addcookie(struct Cookie *nck)
{  struct Cookie *ck,*nextck;
   ObtainSemaphore(&cookiesema);
   for(ck=cookies.first;ck->next;ck=nextck)
   {  nextck=ck->next;
      if(STRIEQUAL(ck->domain,nck->domain)
      && STREQUAL(ck->path,nck->path)
      && STRIEQUAL(ck->name,nck->name))
      {  REMOVE(ck);
         Deletecookie(ck);
      }
   }
   if(nck->expires && nck->expires<=Today())
   {  Deletecookie(nck);
   }
   else
   {  nck->pathlen=strlen(nck->path);
      nck->domainlen=strlen(nck->domain);
      nck->size=nck->pathlen+nck->domainlen+strlen(nck->name)+strlen(nck->value);
      if(nck->comment) nck->size+=strlen(nck->comment);
      for(ck=cookies.first;ck->next;ck=ck->next)
      {  if(ck->pathlen<nck->pathlen) break;
      }
      INSERT(&cookies,nck,ck->prev);
      ADDTAIL(&usenodes,&nck->use);
      cookiesize+=ck->size;
   }
   ReleaseSemaphore(&cookiesema);
}

/* Check if this is a known cookie */
static short Knowncookie(UBYTE *domain,UBYTE *path,UBYTE *name)
{  struct Cookie *ck;
   short known=COOKIE_REJECT;
#ifndef DEMOVERSION
   ObtainSemaphore(&cookiesema);
   for(ck=cookies.first;ck->next && !known;ck=ck->next)
   {  if((ck->flags&COOF_ACCEPT)
      && STRIEQUAL(ck->domain,domain)
      && STREQUAL(ck->path,path)
      && STRIEQUAL(ck->name,name))
      {  known=COOKIE_ACCEPT;
      }
   }
   ReleaseSemaphore(&cookiesema);
#endif
   return known;
}

/* Create domain name and path from from URL */
static UBYTE *Breakup(UBYTE *url,UBYTE **domain,UBYTE **path)
{  UBYTE *p,*q,*dom,*dome,*pat,*pate,*buf=NULL;
   p=url;
   if(STRNIEQUAL(p,"FILE:///",8))
   {  dom="localhost";
      dome=dom+strlen(dom);
      p=url+7;
   }
   else
   {  if(STRNIEQUAL(p,"HTTP://",7)) p+=7;
      else if(STRNIEQUAL(p,"HTTPS://",8)) p+=8;
      else if(STRNIEQUAL(p,"FILE://",7)) p+=7;
      dom=p;
      while(*p && *p!=':' && *p!='/' && *p!='?' && *p!=';') p++;
      dome=p;
   }
   if(*p=='/')
   {  pat=p;
      p++;
      q=NULL;
      while(*p && *p!='?' && *p!=';')
      {  if(*p=='/') q=p;
         p++;
      }
      if(q) pate=q;
      else pate=pat+1;
   }
   else
   {  pat="/";
      pate=p+1;
   }
   if(buf=ALLOCTYPE(UBYTE,(dome-dom+1)+(pate-pat+1),MEMF_CLEAR|MEMF_PUBLIC))
   {  *domain=buf;
      strncpy(*domain,dom,dome-dom);
      *path=buf+(dome-dom+1);
      strncpy(*path,pat,pate-pat);
   }
   return buf;
}

/* See if domain matches orgdomain, i.e.:
 * if domain is given, it must start with a period and contain a period.
 * orgdomain must match domain from the first period (*).
 * And orgdomain must not contain only numbers and periods (then it would be
 * an IP address and domain must not be given).
 * (*) If RFC2109 is off, a simple right-hand match is performed. */
static BOOL Matchdomain(UBYTE *domain,UBYTE *orgdomain)
{  UBYTE *p;
   BOOL ip=TRUE,match=FALSE;
   long l1,l2;
   if(domain && orgdomain)
   {  for(p=orgdomain;*p;p++)
      {  if(*p!='.' && !isdigit(*p))
         {  ip=FALSE;
            break;
         }
      }
      if(!ip)
      {  if(prefs.network.rfc2109)
         {  if(*domain=='.' && strchr(domain+1,'.'))
            {  p=strchr(orgdomain,'.');
               if(p && STRIEQUAL(p,domain)) match=TRUE;
            }
         }
         else
         {  l1=strlen(domain);
            l2=strlen(orgdomain);
            if(l2>=l1 && STRIEQUAL(orgdomain+l2-l1,domain)) match=TRUE;
            if((l2==l1-1) && (*domain=='.') && (STRIEQUAL(orgdomain,domain+1))) match=TRUE;
         }
      }
   }
   else
   {  match=TRUE;
   }
   return match;
}

/* See if cookie matches domain to send.
 * Both must be equal, or ckdomain must match domain from (domain)'s first period.
 * If RFC2109 is off, a simple right-hand match is used. */
static BOOL Matchdomainsend(UBYTE *ckdomain,UBYTE *domain)
{  UBYTE *p;
   BOOL match=FALSE;
   long l1,l2;
   if(STRIEQUAL(ckdomain,domain))
   {  match=TRUE;
   }
   else
   {        if(prefs.network.rfc2109)
      {  p=strchr(domain,'.');
         if(p && STRIEQUAL(ckdomain,p)) match=TRUE;
      }
      else
      {  if((*ckdomain=='.') && (STRIEQUAL(ckdomain+1,domain)))
         {  match=TRUE;
         }
         else
         {  l1=strlen(ckdomain);
            l2=strlen(domain);
            if(l2>l1 && STRIEQUAL(domain+l2-l1,ckdomain)) match=TRUE;
         }
      }
   }
   return match;
}

/* See if path matches orgpath */
static BOOL Matchpath(UBYTE *path,UBYTE *orgpath)
{  long len,orglen;
   BOOL match=FALSE;
   if(path && orgpath)
   {  len=strlen(path);
      orglen=strlen(path);
      if(STRNEQUAL(path,orgpath,len))
      {  if(orglen==len) match=TRUE;
         else if(orgpath[len]=='/') match=TRUE;
      }
   }
   else
   {  match=TRUE;
   }
   return match;
}

/* See if domain matches any nocookie domain */
static BOOL Nocookie(UBYTE *domain)
{  struct Nocookie *nc;
   BOOL result=FALSE;
   ObtainSemaphore(&prefssema);
   for(nc=prefs.network.nocookie.first;!result && nc->next;nc=nc->next)
   {  if(nc->pattern)
      {  result=MatchPatternNoCase(nc->pattern,domain);
      }
      else
      {  result=STRIEQUAL(nc->name,domain);
      }
   }
   ReleaseSemaphore(&prefssema);
   return result;
}

static void Writestring(void *fh,UBYTE *string)
{  WriteAsync(fh,string,strlen(string));
}

/* Save cookies with expiry date */
static void Savecookies(void)
{  struct Usenode *u;
   struct Cookie *ck;
   void *fh;
   UBYTE name[256],datebuf[32];
   ObtainSemaphore(&cookiesema);
   strcpy(name,Cachename());
   if(AddPart(name,"AWCK",256))
   {  if(fh=OpenAsync(name,MODE_WRITE,IOBUFSIZE))
      {  for(u=usenodes.first;u->next;u=u->next)
         {  ck=COOKIE(u);
            if(ck->expires && !Nocookie(ck->domain))
            {  Writestring(fh,ck->name);
               Writestring(fh,"=");
               Writestring(fh,ck->value);
               Writestring(fh,"; ");
               if(!(ck->flags&COOF_DOMAIN)) Writestring(fh,"*");
               Writestring(fh,"DOMAIN=\"");
               Writestring(fh,ck->domain);
               Writestring(fh,"\"; ");
               if(!(ck->flags&COOF_PATH)) Writestring(fh,"*");
               Writestring(fh,"PATH=\"");
               Writestring(fh,ck->path);
               Writestring(fh,"\"; ");
               if(ck->flags&COOF_SECURE)
               {  Writestring(fh,"SECURE; ");
               }
               if(ck->comment)
               {  Writestring(fh,"COMMENT=\"");
                  Writestring(fh,ck->comment);
                  Writestring(fh,"\"; ");
               }
#ifndef DEMOVERSION
               if(ck->flags&COOF_ACCEPT)
               {  Writestring(fh,"*ACCEPT; ");
               }
#endif
               if(ck->version)
               {  Writestring(fh,"VERSION=\"");
                  sprintf(datebuf,"%d",ck->version);
                  Writestring(fh,datebuf);
                  Writestring(fh,"\"; ");
               }
               Writestring(fh,"EXPIRES=\"");
               Makedate(ck->expires,datebuf);
               Writestring(fh,datebuf);
               Writestring(fh,"\"\n");
            }
         }
         CloseAsync(fh);
      }
   }
   ReleaseSemaphore(&cookiesema);
}

/* Parse cookie and store. If orgdomain==NULL, don't validate. */
static void Parsecookie(UBYTE *cookiespec,UBYTE *orgdomain,UBYTE *orgpath,ULONG serverdate)
{  UBYTE *p,*name=NULL,*value=NULL,*expires=NULL,*domain=NULL,*path=NULL,
      *maxage=NULL,*comment=NULL,*version=NULL;
   UBYTE *kwd,*val;
   BOOL secure=FALSE,hasvalue;
   USHORT flags=0;
   struct Cookie *ck;
   short ok=0;
   p=cookiespec;
   while(*p && isspace(*p)) p++;
   if(*p)
   {  /* First comes name=value; */
      name=p;
      while(*p && !isspace(*p) && *p!=',' && *p!=';' && *p!='=') p++;
      if(isspace(*p))
      {  *p++='\0';
         while(isspace(*p)) p++;
      }
      if(*p=='=')
      {  *p++='\0';
         value=p;
         while(isspace(*p)) p++;
         if(*p=='"')
         {  p++;
            while(*p && *p!='"') p++;
            if(*p) p++;
         }
         else
         {  while(*p && !isspace(*p) && *p!=',' && *p!=';') p++;
         }
         if(*p) *p++='\0';
      }
   }
   /* Then come the other fields */
   while(*p)
   {  while(*p && (isspace(*p) || *p==',' || *p==';')) p++;
      kwd=p;
      while(*p && !isspace(*p) && *p!=',' && *p!=';' && *p!='=') p++;
      if(isspace(*p))
      {  *p++='\0';
         while(isspace(*p)) p++;
      }
      hasvalue=(*p=='=');
      if(*p=='=' || *p==',' || *p==';')
      {  *p++='\0';
      }
      if(hasvalue)
      {  while(isspace(*p)) p++;
         if(*p=='"')
         {  val=++p;
            while(*p && *p!='"') p++;
            if(*p=='"')
            {  *p++='\0';
            }
         }
         else
         {  val=p;
            if(STRIEQUAL(kwd,"EXPIRES"))
            {  while(*p && *p!=';') p++;
            }
            else
            {  while(*p && *p!=',' && *p!=';') p++;
            }
            if(*p) *p++='\0';
         }
      }
      else
      {  val="";
      }
      if(STRIEQUAL(kwd,"DOMAIN"))
      {  domain=val;
         flags|=COOF_DOMAIN;
      }
      else if(STRIEQUAL(kwd,"PATH"))
      {  path=val;
         flags|=COOF_PATH;
      }
      else if(STRIEQUAL(kwd,"COMMENT")) comment=val;
      else if(STRIEQUAL(kwd,"MAX-AGE")) maxage=val;
      else if(STRIEQUAL(kwd,"VERSION")) version=val;
      else if(STRIEQUAL(kwd,"EXPIRES")) expires=val;
      else if(STRIEQUAL(kwd,"SECURE")) secure=TRUE;
      else if(STRIEQUAL(kwd,"*DOMAIN") && !orgdomain) domain=val;
      else if(STRIEQUAL(kwd,"*PATH") && !orgdomain) path=val;
#ifndef DEMOVERSION
      else if(STRIEQUAL(kwd,"*ACCEPT") && !orgdomain) flags|=COOF_ACCEPT;
#endif
   }
   if(name && value)
   {  /* Domain must not be given (will be orgdomain) or must match */
      if(orgdomain)
      {  if(!domain) domain=orgdomain;
         else if(!Matchdomain(domain,orgdomain)) domain=NULL;
      }
      /* Path must match, if not given then use orgpath */
      if(!path) path=orgpath;
      else if(prefs.network.rfc2109 && !Matchpath(path,orgpath)) path=NULL;
      /* Domain must not match any "no-cookie" domains */
      if(domain && Nocookie(domain)) domain=NULL;
      if(domain && path)
      {  if(!orgdomain
         || prefs.network.cookies==COOKIES_QUIET
         || (ok=Knowncookie(domain,path,name))
         || (ok=Askcookie(domain,path,name,value,expires,maxage,comment,secure)))
         {  if(ck=ALLOCSTRUCT(Cookie,1,MEMF_PUBLIC|MEMF_CLEAR))
            {  if((ck->name=Dupstr(name,-1))
               && (ck->value=Dupstr(value,-1))
               && (ck->domain=Dupstr(domain,-1))
               && (ck->path=Dupstr(path,-1)))
               {  if(secure) flags|=COOF_SECURE;
                  if(comment) ck->comment=Dupstr(comment,-1);
                  if(maxage)
                  {  ck->expires=atol(maxage)+Today();
                  }
                  else if(expires)
                  {  ck->expires=Scandate(expires);
                     if(serverdate) ck->expires=ck->expires-serverdate+Today();
                  }
                  if(version) ck->version=atol(version);
                  if(ok==COOKIE_ACCEPT) flags|=COOF_ACCEPT;
                  ck->flags=flags;
                  Addcookie(ck);
                  if(orgdomain && ck->expires)
                  {  Savecookies();
                  }
               }
            }
         }
      }
   }
}

/* Read a string into buffer. Return length (ex 0-byte) or -1 if error */
static long Readstring(void *fh,UBYTE *buffer,long max)
{  long n=0,ch;
   while(n<max)
   {  ch=ReadCharAsync(fh);
      if(ch<0) return -1;
      if(ch=='\n') ch='\0';
      *buffer++=ch;
      if(!ch) break;
      n++;
   }
   return n;
}

/* Read cookies */
static void Readcookies(void)
{  void *fh;
   UBYTE name[256];
   UBYTE *buffer;
   if(buffer=ALLOCTYPE(UBYTE,INPUTBLOCKSIZE,0))
   {  strcpy(name,Cachename());
      if(AddPart(name,"AWCK",256))
      {  if(fh=OpenAsync(name,MODE_READ,IOBUFSIZE))
         {  if(Readstring(fh,buffer,INPUTBLOCKSIZE)<0) goto err;
            /* If buffer starts with 'AWCK' it's old format - ignore. */
            if(STRNEQUAL(buffer,"AWCK",4)) goto err;
            for(;;)
            {  Parsecookie(buffer,NULL,NULL,0);
               if(Readstring(fh,buffer,INPUTBLOCKSIZE)<0) break;
            }
err:
            CloseAsync(fh);
         }
      }
      FREE(buffer);
   }
}

#endif /* LOCALONLY */

/*----------------------------------------------------------------------*/

BOOL Initcookie(void)
{  
#ifndef LOCALONLY
   NEWLIST(&cookies);
   NEWLIST(&usenodes);
   InitSemaphore(&cookiesema);
   Readcookies();
#endif
   return TRUE;
}

void Freecookie(void)
{  
#ifndef LOCALONLY
   void *p;
   if(cookies.first)
   {  Savecookies();
      while(p=REMHEAD(&cookies)) Deletecookie(p);
   }
#endif
}

/* cookiespec is line from server starting after "Set-Cookie:", may be altered */
void Storecookie(UBYTE *originator,UBYTE *cookiespec,ULONG serverdate)
{  
#ifndef LOCALONLY
   UBYTE *orgdup,*orgdomain,*orgpath;
   if(!(orgdup=Breakup(originator,&orgdomain,&orgpath))) return;
   Parsecookie(cookiespec,orgdomain,orgpath,serverdate);
   FREE(orgdup);
#endif
}

/* Build a Cookie: header string terminated by CRLF */
UBYTE *Findcookies(UBYTE *url,BOOL secure)
{  UBYTE *header=NULL;
#ifndef LOCALONLY
   struct Buffer buf={0};
   UBYTE versionbuf[16];
   struct Cookie *ck;
   UBYTE *urldup,*domain=NULL,*path=NULL;
   BOOL first=TRUE;
   urldup=Breakup(url,&domain,&path);
   if(!urldup) return NULL;
   ObtainSemaphore(&cookiesema);
   for(ck=cookies.first;ck->next;ck=ck->next)
   {  if(Matchdomainsend(ck->domain,domain) && Matchpath(ck->path,path)
      && !Nocookie(ck->domain)
      && (secure || !(ck->flags&COOF_SECURE))
      && (!ck->expires || ck->expires>=Today()))
      {  if(first)
         {  Addtobuffer(&buf,"Cookie: ",8);
            if(ck->version)
            {  sprintf(versionbuf,"%d",ck->version);
               Addtobuffer(&buf,"$Version=\"",10);
               Addtobuffer(&buf,versionbuf,strlen(versionbuf));
               Addtobuffer(&buf,"\"; ",3);
            }
         }
         else
         {  Addtobuffer(&buf,"; ",2);
         }
         Addtobuffer(&buf,ck->name,strlen(ck->name));
         Addtobuffer(&buf,"=",1);
         Addtobuffer(&buf,ck->value,strlen(ck->value));
         ObtainSemaphore(&prefssema);
         if(prefs.network.rfc2109)
         {  if(ck->flags&COOF_PATH)
            {  Addtobuffer(&buf,"; $Path=\"",9);
               Addtobuffer(&buf,ck->path,strlen(ck->path));
               Addtobuffer(&buf,"\"",1);
            }
            if(ck->flags&COOF_DOMAIN)
            {  Addtobuffer(&buf,"; $Domain=\"",11);
               Addtobuffer(&buf,ck->domain,strlen(ck->domain));
               Addtobuffer(&buf,"\"",1);
            }
         }
         ReleaseSemaphore(&prefssema);
         REMOVE(&ck->use);
         ADDTAIL(&usenodes,&ck->use);
         first=FALSE;
      }
   }
   ReleaseSemaphore(&cookiesema);
   if(buf.buffer)
   {  Addtobuffer(&buf,"\r\n",3);
      header=Dupstr(buf.buffer,-1);
      Freebuffer(&buf);
   }
   FREE(urldup);
#endif
   return header;
}

/* Build the JS cookie string */
UBYTE *Getjcookies(UBYTE *url)
{  UBYTE *cstring=NULL;
#ifndef LOCALONLY
   struct Buffer buf={0};
   struct Cookie *ck;
   UBYTE *urldup,*domain=NULL,*path=NULL;
   BOOL first=TRUE;
   urldup=Breakup(url,&domain,&path);
   if(!urldup) return NULL;
   ObtainSemaphore(&cookiesema);
   for(ck=cookies.first;ck->next;ck=ck->next)
   {  if(Matchdomainsend(ck->domain,domain) && Matchpath(ck->path,path)
      && !Nocookie(ck->domain)
      && (!ck->expires || ck->expires>=Today()))
      {  if(!first)
         {  Addtobuffer(&buf,"; ",2);
         }
         Addtobuffer(&buf,ck->name,strlen(ck->name));
         Addtobuffer(&buf,"=",1);
         Addtobuffer(&buf,ck->value,strlen(ck->value));
         first=FALSE;
      }
   }
   ReleaseSemaphore(&cookiesema);
   if(buf.buffer)
   {  Addtobuffer(&buf,"",1);
      cstring=Dupstr(buf.buffer,-1);
      Freebuffer(&buf);
   }
   FREE(urldup);
#endif
   return cstring;
}

void Flushcookies(long max)
{  
#ifndef LOCALONLY
   struct Usenode *u,*next;
   struct Cookie *ck;
   for(u=usenodes.first;u->next && cookiesize>max;u=next)
   {  next=u->next;
      ck=COOKIE(u);
      REMOVE(ck);
      Deletecookie(ck);
   }
#endif
}

void Getrexxcookies(struct Arexxcmd *ac,UBYTE *stem)
{  struct Usenode *u;
   struct Cookie *ck;
   UBYTE buf[32];
   long i=0;
   UBYTE *p;
#ifndef LOCALONLY
   for(u=usenodes.first;u->next;u=u->next)
   {  ck=COOKIE(u);
      i++;
      Setstemvar(ac,stem,i,"NAME",ck->name);
      Setstemvar(ac,stem,i,"VALUE",ck->value);
      if(ck->expires) Makedate(ck->expires,buf);
      else *buf='\0';
      Setstemvar(ac,stem,i,"EXPIRES",buf);
      Setstemvar(ac,stem,i,"DOMAIN",ck->domain);
      Setstemvar(ac,stem,i,"PATH",ck->path);
      Setstemvar(ac,stem,i,"COMMENT",ck->comment?ck->comment:NULLSTRING);
      sprintf(buf,"%d",ck->version);
      Setstemvar(ac,stem,i,"VERSION",buf);
      p=buf;
      if(!(ck->flags&COOF_DOMAIN)) *p++='D';
      if(!(ck->flags&COOF_PATH)) *p++='P';
      if(ck->flags&COOF_SECURE) *p++='S';
      if(ck->flags&COOF_ACCEPT) *p++='A';
      *p='\0';
      Setstemvar(ac,stem,i,"FLAGS",buf);
   }
#endif
   sprintf(buf,"%d",i);
   Setstemvar(ac,stem,0,NULL,buf);
}

void Setrexxcookies(struct Arexxcmd *ac,UBYTE *stem,BOOL add)
{  UBYTE *name,*value,*expires,*domain,*path,*comment,*version,*flags,*max;
   struct Cookie *ck;
   long nmax,i;
   struct Buffer buf={0};
   UBYTE *p;
#ifndef LOCALONLY
   if(!add)
   {  while(ck=REMHEAD(&cookies)) Deletecookie(ck);
   }
   if(max=Getstemvar(ac,stem,0,NULL))
   {  nmax=atoi(max);
      for(i=1;i<=nmax;i++)
      {  name=Getstemvar(ac,stem,i,"NAME");
         value=Getstemvar(ac,stem,i,"VALUE");
         expires=Getstemvar(ac,stem,i,"EXPIRES");
         domain=Getstemvar(ac,stem,i,"DOMAIN");
         path=Getstemvar(ac,stem,i,"PATH");
         comment=Getstemvar(ac,stem,i,"COMMENT");
         version=Getstemvar(ac,stem,i,"VERSION");
         flags=Getstemvar(ac,stem,i,"FLAGS");
         if(!flags) flags="";
         for(p=flags;*p;p++) *p=toupper(*p);
         if(name && value && domain && path)
         {  Addtobuffer(&buf,name,-1);
            Addtobuffer(&buf,"=",1);
            Addtobuffer(&buf,value,-1);
            Addtobuffer(&buf,"; ",-1);
            if(strchr(flags,'D')) Addtobuffer(&buf,"*",-1);
            Addtobuffer(&buf,"DOMAIN=\"",-1);
            Addtobuffer(&buf,domain,-1);
            Addtobuffer(&buf,"\"; ",-1);
            if(strchr(flags,'P')) Addtobuffer(&buf,"*",-1);
            Addtobuffer(&buf,"PATH=\"",-1);
            Addtobuffer(&buf,path,-1);
            Addtobuffer(&buf,"\"; ",-1);
            if(comment)
            {  Addtobuffer(&buf,"COMMENT=\"",-1);
               Addtobuffer(&buf,comment,-1);
               Addtobuffer(&buf,"\"; ",-1);
            }
            if(version)
            {  Addtobuffer(&buf,"VERSION=\"",-1);
               Addtobuffer(&buf,version,-1);
               Addtobuffer(&buf,"\"; ",-1);
            }
            if(strchr(flags,'S'))
            {  Addtobuffer(&buf,"SECURE; ",-1);
            }
            if(strchr(flags,'A'))
            {  Addtobuffer(&buf,"*ACCEPT; ",-1);
            }
            if(expires)
            {  Addtobuffer(&buf,"EXPIRES=\"",-1);
               Addtobuffer(&buf,expires,-1);
               Addtobuffer(&buf,"\"",-1);
            }
            Parsecookie(buf.buffer,NULL,NULL,0);
            Freebuffer(&buf);
         }
         else ac->errorlevel=RXERR_INVARGS;
      }
   }
#endif
}
