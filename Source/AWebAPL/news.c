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

/* news.c - AWeb newsreader */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "aweblib.h"
#include "tcperr.h"
#include "fetchdriver.h"
#include "task.h"
#include "application.h"
#include "awebtcp.h"
#include <exec/resident.h>
#include <libraries/asl.h>
#include <graphics/displayinfo.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <proto/exec.h>
#include <proto/asl.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/socket.h>

/* This library supports the following URLs:

   news:                         Main news page with subscripted list
   news:group                    Show (opt framed) article list
   news:article@id               Show article
   x-aweb:news/2?formparms       subscribe: Add group to subscribed, moveto "news:"
                                 read: moveto "news:group"
   x-aweb:news/3?group           Show post new article form
   x-aweb:news/4/NEWS?group      Subscribe group, show subscribed list
   x-aweb:news/5/NEWS?group      Unsubscribe group, show subscribed list
   x-aweb:news/6/group           Show article list only for use within frame
   x-aweb:news/FOLLOWUP?art@id   Show post article form w quoted article
   x-aweb:news/REPLY?art@id      Show mail form w quoted article
   x-aweb:news/POST [formparms]  (Form parameteres in post message)
                                 no cancel: post article, show "posted"
                                 cancel: Go history -1
   x-aweb:news/a                 Go history -2
   x-aweb:news/b/NEWS?group+nr   Catch up group, show subscribed list
   x-aweb:news/search?formparams Search for group name;
                                 download active list first if not valid
   x-aweb:news/c/group           Subscribe group, no output
   x-aweb:news/d/article@id      Save article

 * NNTP type URLs act the same but specify a host (with optional :port) to get
   the information from instead of the default host.
   
   nntp://host/group             Show (opt framed) article list
   news://host/group             Idem
   nntp://host/group/artnr       Show article
   news://host/group/artnr       Idem
   x-aweb:news/3?host/group      Show post new article form
   x-aweb:news/6/host/group      Show article list only for use within frame
   x-aweb:news/FOLLOWUP?host/group/artnr  Show post article form w quoted article
   x-aweb:news/REPLY?host/group/artnr  Show mail form w quoted article
   x-aweb:news/POST?host [formparms]  (Form parameters in post message)
                                 no cancel: post article, show "posted"
                                 cancel: Go history -1
   x-aweb:nntp/d/host/group/artnr  Save article

URL formats are chosen so that those who could block, result in sensible
names in the network status window.

*/

#ifndef LOCALONLY

struct Library *AwebTcpBase;
void *AwebPluginBase;

struct Newsinfo
{  struct Fetchdriver *fd;
   struct Library *socketbase;
   struct hostent *hent;
   long sock;
   USHORT flags;
   UBYTE *buffer;    /* response buffer */
   long length;      /* nr of characters in buffer */
   long linelen;     /* nr of characters in first line */
   long size;        /* size of buffer */
};

#define NIF_HASSOCKET   0x0001

struct Group         /* A subscribed group */
{  NODE(Group);
   UBYTE *name;
   long lastread;    /* Number of last read article */
   long lastfetched; /* Number of last fetched article */
};

/* Subscribed groups list.
 * If this is unitilialized, subscribed haven't beed read yet. */
static LIST(Group) groups;

static struct SignalSemaphore groupsema;

/*-----------------------------------------------------------------------*/
/* AWebLib module startup */

__asm __saveds struct Library *Initlib(
   register __a6 struct ExecBase *sysbase,
   register __a0 struct SegList *seglist,
   register __d0 struct Library *libbase);

__asm __saveds struct Library *Openlib(
   register __a6 struct Library *libbase);

__asm __saveds struct SegList *Closelib(
   register __a6 struct Library *libbase);

__asm __saveds struct SegList *Expungelib(
   register __a6 struct Library *libbase);

__asm __saveds ULONG Extfunclib(void);

__asm __saveds void Fetchdrivertask(
   register __a0 struct Fetchdriver *fd);

/* Function declarations for project dependent hook functions */
static ULONG Initaweblib(struct Library *libbase);
static void Expungeaweblib(struct Library *libbase);

struct Library *NewsBase;

static APTR libseglist;

LONG __saveds __asm Libstart(void)
{  return -1;
}

static APTR functable[]=
{  Openlib,
   Closelib,
   Expungelib,
   Extfunclib,
   Fetchdrivertask,
   (APTR)-1
};

/* Init table used in library initialization. */
static ULONG inittab[]=
{  sizeof(struct Library),
   (ULONG) functable,
   0,
   (ULONG) Initlib
};

static char __aligned libname[]="news.aweblib";
static char __aligned libid[]="news.aweblib " AWEBLIBVSTRING " " __AMIGADATE__;

/* The ROM tag */
struct Resident __aligned romtag=
{  RTC_MATCHWORD,
   &romtag,
   &romtag+1,
   RTF_AUTOINIT,
   AWEBLIBVERSION,
   NT_LIBRARY,
   0,
   libname,
   libid,
   inittab
};

__asm __saveds struct Library *Initlib(
   register __a6 struct ExecBase *sysbase,
   register __a0 struct SegList *seglist,
   register __d0 struct Library *libbase)
{  SysBase=sysbase;
   NewsBase=libbase;
   libbase->lib_Revision=AWEBLIBREVISION;
   libseglist=seglist;
   if(!Initaweblib(libbase))
   {  Expungeaweblib(libbase);
      libbase=NULL;
   }
   return libbase;
}

__asm __saveds struct Library *Openlib(
   register __a6 struct Library *libbase)
{  libbase->lib_OpenCnt++;
   libbase->lib_Flags&=~LIBF_DELEXP;
   if(libbase->lib_OpenCnt==1)
   {  AwebPluginBase=OpenLibrary("awebplugin.library",0);
   }
#ifndef DEMOVERSION
   if(!Fullversion())
   {  Closelib(libbase);
      return NULL;
   }
#endif
   return libbase;
}

__asm __saveds struct SegList *Closelib(
   register __a6 struct Library *libbase)
{  libbase->lib_OpenCnt--;
   if(libbase->lib_OpenCnt==0)
   {  if(AwebPluginBase)
      {  CloseLibrary(AwebPluginBase);
         AwebPluginBase=NULL;
      }
      if(libbase->lib_Flags&LIBF_DELEXP)
      {  return Expungelib(libbase);
      }
   }
   return NULL;
}

__asm __saveds struct SegList *Expungelib(
   register __a6 struct Library *libbase)
{  if(libbase->lib_OpenCnt==0)
   {  ULONG size=libbase->lib_NegSize+libbase->lib_PosSize;
      UBYTE *ptr=(UBYTE *)libbase-libbase->lib_NegSize;
      Remove((struct Node *)libbase);
      Expungeaweblib(libbase);
      FreeMem(ptr,size);
      return libseglist;
   }
   libbase->lib_Flags|=LIBF_DELEXP;
   return NULL;
}

__asm __saveds ULONG Extfunclib(void)
{  return 0;
}

/*-----------------------------------------------------------------------*/

#ifndef DEMOVERSION

static void Subscribe(struct Fetchdriver *fd,UBYTE *name,BOOL showlist);

#endif

/*-----------------------------------------------------------------------*/

#define SUBSCRIBEDNAME "AWeb:AWeb.News"

/* Allocate a group. Note AllocVec is used because the list may be cleaned
 * up after AwebPluginBase is closed. */
static struct Group *Allocgroup(UBYTE *name,long l)
{  struct Group *g;
   if(l<=0) l=strlen(name);
   if(g=(struct Group *)AllocVec(sizeof(struct Group)+l+1,MEMF_PUBLIC|MEMF_CLEAR))
   {  g->name=(UBYTE *)(g+1);
      strncpy(g->name,name,l);
   }
   return g;
}

/* Dispose a group */
static void Freegroup(struct Group *g)
{  if(g)
   {  FreeVec(g);
   }
}

/* Make sure subscribed are read */
static void Readsubscribed(struct Newsinfo *ni)
{  
#ifndef DEMOVERSION
   long file;
   long len,left;
   UBYTE *p,*q,*eol,*end;
   struct Group *g;
   ObtainSemaphore(&groupsema);
   if(!groups.first)
   {  /* Not yet initialized */
      NEWLIST(&groups);
      if(file=Open(SUBSCRIBEDNAME,MODE_OLDFILE))
      {  left=0;
         for(;;)
         {  len=Read(file,ni->fd->block+left,ni->fd->blocksize-left);
            if(len<=0) break;
            p=ni->fd->block;
            end=p+len;
            for(;;)
            {  /* q points to after group name; eol poins to end of line */
               for(q=p;q<end && *q!=' ' && *q!='\n';q++);
               for(eol=q;eol<end && *eol!='\n';eol++);
               if(eol>=end) break;
               if(q>p && (g=Allocgroup(p,q-p)))
               {  g->lastread=0;
                  g->lastfetched=0;
                  sscanf(q," %ld %ld",&g->lastread,&g->lastfetched);
                  ADDTAIL(&groups,g);
               }
               p=eol+1;
            }
            /* If something was left, move it to the beginning of the block */
            left=len-(p-ni->fd->block);
            if(left>0)
            {  memmove(ni->fd->block,p,left);
            }
         }
         Close(file);
      }
   }
   ReleaseSemaphore(&groupsema);
#endif
}

/* Write out subscribed */
static void Writesubscribed(struct Newsinfo *ni)
{  
#ifndef DEMOVERSION
   long file;
   long l,len;
   struct Group *g;
   ObtainSemaphore(&groupsema);
   if(groups.first)
   {  if(file=Open(SUBSCRIBEDNAME,MODE_NEWFILE))
      {  len=0;
         for(g=groups.first;g->next;g=g->next)
         {  l=strlen(g->name);
            if(len+l+16>ni->fd->blocksize)
            {  Write(file,ni->fd->block,len);
               len=0;
            }
            len+=sprintf(ni->fd->block+len,"%s %d %d\n",g->name,g->lastread,g->lastfetched);
         }
         if(len)
         {  Write(file,ni->fd->block,len);
         }
         Close(file);
      }
   }
   ReleaseSemaphore(&groupsema);
#endif
}

/* Find a group. Obtain semaphore yourself first. */
static struct Group *Findgroup(struct Newsinfo *ni,UBYTE *name)
{  
#ifndef DEMOVERSION
   struct Group *g;
   Readsubscribed(ni);
   for(g=groups.first;g->next;g=g->next)
   {  if(STRIEQUAL(g->name,name)) return g;
   }
#endif
   return NULL;
}

/*-----------------------------------------------------------------------*/

#define ACTIVENAME "AWeb:AWeb.Active"

/* See if active file is present and valid.
 * Returns the file handle if it is valid, NULL otherwise.
 *
 * Active file must contain
 * Newsgroups at YYMMDD HHMMSS news.server *** etc
 * as its first line.
 */
static long Validactive(struct Fetchdriver *fd)
{  long file=NULL,len;
   BOOL valid=FALSE;
   UBYTE *p,*q;
   if(file=Open(ACTIVENAME,MODE_OLDFILE))
   {  len=Read(file,fd->block,256);
      fd->block[len]='\0';
      if(len>28)
      {  p=fd->block+28;   /* Skip "Newsgroups at YYMMDD HHMMSS " */
         if(q=strchr(p,' '))
         {  *q='\0';
            ObtainSemaphore(fd->prefssema);
            if(STRIEQUAL(p,fd->prefs->nntphost))
            {  valid=TRUE;
            }
            ReleaseSemaphore(fd->prefssema);
         }
      }
      if(!valid)
      {  Close(file);
         file=NULL;
      }
   }
   return file;
}

/*-----------------------------------------------------------------------*/

/* Convert hexadecimal */
static UBYTE Hextodec(UBYTE c)
{  UBYTE ch=0;
   if(c>='0' && c<='9') ch=c-'0';
   else
   {  c=toupper(c);
      if(c>='A' && c<='F') ch=c+10-'A';
   }
   return ch;
}

/* Remove URL escape from block. Return new length (always <= old length) */
static long Unescape(UBYTE *block,long len)
{  UBYTE *start,*p,*end;
   long newlen=len;
   start=block;
   end=block+len;
   while(start<end)
   {  for(p=start;p<end && *p!='%';p++)
      {  if(*p=='+') *p=' ';
      }
      if(p<end)
      {  /* Now *p=='%' */
         if(p<end-2)
         {  *p=(Hextodec(p[1])<<4)+Hextodec(p[2]);
            memmove(p+1,p+3,end-p-3);
            newlen-=2;
         }
      }
      start=p+1;
   }
   return newlen;
}

/* Append this block with dangerous characters HTML escaped. */
static void Addblock(struct Buffer *buf,UBYTE *block,long len)
{  UBYTE *start,*p,*end,*esc;
   if(len<0) len=strlen(block);
   start=block;
   end=block+len;
   while(start<end)
   {  for(p=start;p<end && *p!='<' && *p!='&' && *p!='\'';p++);
      Addtobuffer(buf,start,p-start);
      if(p<end)
      {  switch(*p)
         {  case '<':esc="&lt;";break;
            case '&':esc="&amp;";break;
            case '\'':esc="&#39;";break;
            default: esc="";break;
         }
         Addtobuffer(buf,esc,strlen(esc));
         p++;  /* Skip over dangerous character */
      }
      start=p;
   }
}

/* Append this HTML block (no escapes) */
static void Addbhtml(struct Buffer *buf,UBYTE *block,long len)
{  if(len<0) len=strlen(block);
   Addtobuffer(buf,block,len);
}

/* Format and add to buffer */
static void Bprintf(struct Buffer *buf,UBYTE *scratch,UBYTE *fmt,...)
{  va_list args;
   long len;
   va_start(args,fmt);
   len=vsprintf(scratch,fmt,args);
   va_end(args);
   Addtobuffer(buf,scratch,len);
}

/* Add signature to buffer */
static void Addsignature(struct Buffer *buf,struct Fetchdriver *fd)
{  long file,len;
   BOOL first=TRUE;
   ObtainSemaphore(fd->prefssema);
   if(fd->prefs->sigfile)
   {  Addbhtml(buf,"&#10;\n--\n",-1);
      if(file=Open(fd->prefs->sigfile,MODE_OLDFILE))
      {  while(len=Read(file,fd->block,fd->blocksize))
         {  if(first && len>3 && STRNEQUAL(fd->block,"--\n",3))
            {  Addblock(buf,fd->block+3,len-3);
            }
            else
            {  Addblock(buf,fd->block,len);
            }
            first=FALSE;
         }
         Close(file);
      }
   }
   ReleaseSemaphore(fd->prefssema);
}

/* See if post form was submitted from RETURN button */
static BOOL Findreturn(UBYTE *formdata)
{  UBYTE *p;
   p=formdata;
   while(p=strchr(p,'&'))
   {  if(STRNIEQUAL(p,"&RETURN=",8)) return TRUE;
      p++;
   }
   return FALSE;
}

/* Create random word ".cvccvc\0". Buffer must be 8 bytes long. */
static void Spamblock(UBYTE *buffer,UBYTE *seedstring)
{  static long seed=0;
   static UBYTE consonants[]="bcdfghjklmnpqrstvwxz";
   static UBYTE vowels[]="aeiouy";
   UBYTE *p;
   ObtainSemaphore(&groupsema);
   if(!seed)
   {  for(p=seedstring;*p;p++) seed+=*p;
      srand(seed);
   }
   buffer[0]=consonants[(rand()>>7)%20];
   buffer[1]=vowels[(rand()>>7)%6];
   buffer[2]=consonants[(rand()>>7)%20];
   buffer[3]=consonants[(rand()>>7)%20];
   buffer[4]=vowels[(rand()>>7)%6];
   buffer[5]=consonants[(rand()>>7)%20];
   buffer[6]='.';
   buffer[7]='\0';
   ReleaseSemaphore(&groupsema);
}

/* Add spamblocked address to buffer */
static void Addaddress(struct Buffer *buf,UBYTE *address,UBYTE *spamblock)
{  UBYTE *p;
   if(p=strchr(address,'@'))
   {  p++;
      Addtobuffer(buf,address,p-address);
      address=p;
      Addtobuffer(buf,spamblock,strlen(spamblock));
   }
   Addtobuffer(buf,address,strlen(address));
   Addtobuffer(buf,".invalid",8);
}

/*-----------------------------------------------------------------------*/

/* Return the next line of response, or NULL in case of error.
 * If no response in buffer, read it. */
static UBYTE *Nextline(struct Newsinfo *ni)
{  UBYTE *p,*end;
   long r;
   if(!ni->buffer)
   {  if(!(ni->buffer=ALLOCTYPE(UBYTE,1024,MEMF_CLEAR))) return NULL;
      ni->size=1024;
   }
   /* Delete current first line */
   if(ni->linelen)
   {  memmove(ni->buffer,ni->buffer+ni->linelen,ni->length-ni->linelen);
      ni->length-=ni->linelen;
      ni->linelen=0;
   }
   /* Find end of first line. If no EOL yet, read more data. */
   for(;;)
   {  end=ni->buffer+ni->length;
      for(p=ni->buffer;p<end && *p!='\n';p++);
      if(p<end)
      {  *p='\0';
         ni->linelen=p-ni->buffer+1;
         if(ni->linelen>0 && ni->buffer[ni->linelen-2]=='\r')
         {  ni->buffer[ni->linelen-2]='\0';
         }
         break;
      }
      /* If buffer overflow (line longer than buffer), enlarge buffer. */
      if(ni->length>=ni->size-1)
      {  UBYTE *newbuffer;
         if(!(newbuffer=ALLOCTYPE(UBYTE,ni->size+1024,MEMF_CLEAR))) return NULL;
         memcpy(newbuffer,ni->buffer,ni->size);
         FREE(ni->buffer);
         ni->buffer=newbuffer;
         ni->size+=1024;
      }
      r=a_recv(ni->sock,ni->buffer+ni->length,ni->size-ni->length-1,0,ni->socketbase);
      if(r<=0) return NULL;
      ni->length+=r;
   }
   return ni->buffer;
}

/* Get a response code */
static long Getresponse(struct Newsinfo *ni)
{  long r;
   UBYTE *p;
   if(!(p=Nextline(ni))) return 999;
   r=atoi(p);
   return r;
}

/* Send a command, return the response. */
static long Sendcommand(struct Newsinfo *ni,UBYTE *fmt,...)
{  va_list args;
   UBYTE cmd[256];
   long len;
   va_start(args,fmt);
   vsprintf(cmd,fmt,args);
   va_end(args);
   strcat(cmd,"\r\n");
   len=strlen(cmd);
   if(a_send(ni->sock,cmd,len,0,ni->socketbase)!=len) return 999;
   return Getresponse(ni);
}

/* Open TCP, make connection and all. Don't give error messages if optional==TRUE.
 * (host) is NULL or contains "host[:port][/data]" */
static BOOL Opennews(struct Newsinfo *ni,BOOL optional,UBYTE *host)
{  BOOL ok=FALSE;
   UBYTE *hostname,*user=NULL,*pass=NULL,*p;
   long port=119;
   AwebTcpBase=Opentcp(&ni->socketbase,ni->fd,!optional);
   if(ni->socketbase)
   {  ObtainSemaphore(ni->fd->prefssema);
      if(host)
      {  hostname=Dupstr(host,-1);
         if(p=strchr(hostname,'/')) *p='\0';
         if(p=strchr(hostname,':'))
         {  *p='\0';
            port=atoi(p+1);
         }
      }
      else
      {  hostname=Dupstr(ni->fd->prefs->nntphost,-1);
      }
      if(*ni->fd->prefs->newsauthuser)
      {  user=Dupstr(ni->fd->prefs->newsauthuser,-1);
         pass=Dupstr(ni->fd->prefs->newsauthpass,-1);
      }
      ReleaseSemaphore(ni->fd->prefssema);
      if(hostname && *hostname)
      {  Updatetaskattrs(AOURL_Netstatus,NWS_LOOKUP,TAG_END);
         Tcpmessage(ni->fd,TCPMSG_LOOKUP,hostname);
         ni->hent=Lookup(hostname,ni->socketbase);
         if(ni->hent)
         {  if((ni->sock=a_socket(ni->hent->h_addrtype,SOCK_STREAM,0,ni->socketbase))>=0)
            {  ni->flags|=NIF_HASSOCKET;
               Updatetaskattrs(AOURL_Netstatus,NWS_CONNECT,TAG_END);
               Tcpmessage(ni->fd,TCPMSG_CONNECT,"NNTP",ni->hent->h_name);
               if(!a_connect(ni->sock,ni->hent,port,ni->socketbase)
               && Getresponse(ni)<400)
               {  if(user)
                  {  if(Sendcommand(ni,"AUTHINFO USER %s",user)<400
                     && Sendcommand(ni,"AUTHINFO PASS %s",pass)<400)
                     {  ok=TRUE;
                     }
                     else
                     {  if(!optional) Tcperror(ni->fd,TCPERR_NOLOGIN,hostname,user);
                     }
                  }
                  else
                  {  ok=TRUE;
                  }
               }
               else
               {  if(!optional) Tcperror(ni->fd,TCPERR_NOCONNECT,ni->hent->h_name);
               }
            }
         }
         else
         {  if(!optional) Tcperror(ni->fd,TCPERR_NOHOST,hostname);
         }
         FREE(hostname);
      }
      else
      {  if(!optional) Tcperror(ni->fd,TCPERR_NOHOST,"");
      }
      if(user) FREE(user);
      if(pass) FREE(pass);
   }
   else
   {  if(!optional) Tcperror(ni->fd,TCPERR_NOLIB);
   }
   return ok;
}

/* Disconnect, close, free Newsinfo buffer */
static void Closenews(struct Newsinfo *ni)
{  if(ni->socketbase)
   {  if(ni->flags&NIF_HASSOCKET)
      {  a_close(ni->sock,ni->socketbase);
      }
      a_cleanup(ni->socketbase);
      CloseLibrary(ni->socketbase);
   }
   if(ni->buffer) FREE(ni->buffer);
}

/*-----------------------------------------------------------------------*/
/* news:                                                                 */
/*-----------------------------------------------------------------------*/
/* Build main news page */

#ifndef DEMOVERSION

/* Add one line with group name and details */
static void Addsubscribedline(struct Newsinfo *ni,struct Group *g)
{  long l,len,first=1,last=g->lastread;
   if((ni->flags&NIF_HASSOCKET)
   && Sendcommand(ni,"GROUP %s",g->name)<400)
   {  /* ni->buffer contains "211 nr first last name..." */
      sscanf(ni->buffer+3," %*d %ld %ld",&first,&last);
      if(last<g->lastread) last=g->lastread;
   }
   l=strlen(g->name);
   len=sprintf(ni->fd->block," <A HREF='news:%s'>%s</A>%*s",
      g->name,g->name,MAX(1,32-l),"");
   len+=sprintf(ni->fd->block+len,AWEBSTR(MSG_NEWS_NEW_ARTICLES),
      last-MAX(first-1,g->lastread));
   len+=sprintf(ni->fd->block+len,"  (<A HREF='x-aweb:news/B/news?%s&%d'>%s</A>)  "
      "(<A HREF='x-aweb:news/5/news?%s'>%s</A>)\n",
      g->name,last,AWEBSTR(MSG_NEWS_CATCH_UP),
      g->name,AWEBSTR(MSG_NEWS_UNSUBSCRIBE));
   Updatetaskattrs(
      AOURL_Data,ni->fd->block,
      AOURL_Datalength,len,
      TAG_END);
}

#endif

static void Buildmain(struct Fetchdriver *fd)
{  struct Newsinfo ni={0};
   struct Buffer buf={0};
   struct Group *g;
   long file;
   ni.fd=fd;
   if(Obtaintasksemaphore(&groupsema))
   {  Readsubscribed(&ni);
      Bprintf(&buf,fd->block,"<HTML>\n<HEAD>\n<TITLE>News:</TITLE>\n</HEAD>\n");
      Bprintf(&buf,fd->block,"<BODY>\n<H1>%s</H1>\n",AWEBSTR(MSG_NEWS_NEWS_HEADER));
#ifndef DEMOVERSION
      Bprintf(&buf,fd->block,"%s:<BR>\n<PRE>\n",AWEBSTR(MSG_NEWS_SUBSCRIBED));
#else
      Bprintf(&buf,fd->block,"The full commercial version of AWeb allows you to "
         "subscribe to newsgroups\n"
         "and remember the last article you read in those groups.\n");
#endif
      Updatetaskattrs(
         AOURL_Contenttype,"text/html",
         AOURL_Data,buf.buffer,
         AOURL_Datalength,buf.length,
         TAG_END);
      buf.length=0;
#ifndef DEMOVERSION
      Opennews(&ni,TRUE,NULL);
      for(g=groups.first;g->next;g=g->next)
      {  Addsubscribedline(&ni,g);
      }
      Bprintf(&buf,fd->block,"</PRE>\n");
#endif
      ReleaseSemaphore(&groupsema);
      Bprintf(&buf,fd->block,"<HR>\n<BR>\n<FORM ACTION='x-aweb:news/2'>\n%s:<BR>\n",
         AWEBSTR(MSG_NEWS_OTHER_GROUP));
      Bprintf(&buf,fd->block,"<INPUT NAME='group' SIZE=30>\n");
#ifndef DEMOVERSION
      Bprintf(&buf,fd->block,"<INPUT TYPE=SUBMIT NAME='subscribe' VALUE='%s'>\n",
         AWEBSTR(MSG_NEWS_SUBSCRIBE));
#endif
      Bprintf(&buf,fd->block,"<INPUT TYPE=SUBMIT NAME='read' VALUE='%s'>\n",
         AWEBSTR(MSG_NEWS_READ));
      Bprintf(&buf,fd->block,"<BR>\n</FORM>\n<BR>\n");
      Bprintf(&buf,fd->block,"<FORM ACTION='x-aweb:news/search'>\n%s:<BR>\n",
         AWEBSTR(MSG_NEWS_SEARCH_FOR_GROUP));
      Bprintf(&buf,fd->block,"<INPUT NAME='search' SIZE=30>\n");
      Bprintf(&buf,fd->block,"<INPUT TYPE=SUBMIT VALUE='%s'>\n",AWEBSTR(MSG_NEWS_SEARCH));
      Bprintf(&buf,fd->block,"<BR>\n</FORM>\n");
      if(file=Validactive(fd))
      {  Close(file);
      }
      else
      {  Bprintf(&buf,fd->block,AWEBSTR(MSG_NEWS_LONG_DOWNLOAD));
      }
      Bprintf(&buf,fd->block,"</BODY>\n</HTML>\n");
      Updatetaskattrs(
         AOURL_Data,buf.buffer,
         AOURL_Datalength,buf.length,
         TAG_END);
   }
   Closenews(&ni);
   Freebuffer(&buf);
}

/*-----------------------------------------------------------------------*/
/* news:group                                                            */
/* nntp://host/group                                                     */
/*-----------------------------------------------------------------------*/
/* Read news group article index */

struct Artindex
{  long nr;
   UBYTE *id;
   UBYTE *subject;
   UBYTE *sortject;  /* Subject to sort on (excluding "Re:") */
   UBYTE *from;
   UBYTE *lines;
};

/* Clear an Artindex array */
static void Clearaiarray(struct Artindex *aindex,long nr)
{  long i;
   struct Artindex *ai;
   for(i=0,ai=aindex;i<nr;i++,ai++)
   {  ai->nr=0;
      if(ai->id) { FREE(ai->id);ai->id=NULL; }
      if(ai->subject) { FREE(ai->subject);ai->subject=NULL; }
      ai->sortject=NULL;
      if(ai->from) { FREE(ai->from);ai->from=NULL; }
      if(ai->lines) { FREE(ai->lines);ai->lines=NULL; }
   }
}

/* Build entries from article index. Send every 512 characters to main. */
static void Buildartindex(struct Newsinfo *ni,struct Buffer *buf,struct Artindex *aindex,
   long nr,UBYTE *nntp)
{  struct Artindex *ai;
   long n;
   UBYTE nrbuf[16];
   for(n=0;n<nr;n++)
   {  ai=&aindex[n];
      if(ai->id && *ai->id)
      {  if(nntp)
         {  Addbhtml(buf,"<A HREF='nntp://",-1);
            Addblock(buf,nntp,-1);
            Addblock(buf,"/",1);
            sprintf(nrbuf,"%d",ai->nr);
            Addblock(buf,nrbuf,-1);
         }
         else
         {  Addbhtml(buf,"<A HREF='news:",-1);
            Addblock(buf,ai->id,-1);
         }
         Addbhtml(buf,"'>",-1);
         Addblock(buf,(ai->subject && *ai->subject)?ai->subject:AWEBSTR(MSG_NEWS_NO_SUBJECT),-1);
         Addbhtml(buf,"</A>, ",-1);
         Addblock(buf,ai->from?ai->from:NULLSTRING,-1);
         if(ai->lines && *ai->lines)
         {  Addbhtml(buf," ",-1);
            Bprintf(buf,ni->fd->block,AWEBSTR(MSG_NEWS_LINES),
               ai->lines?ai->lines:NULLSTRING);
         }
         Addbhtml(buf,"<BR>\n",-1);
      }
      if(buf->length>512)
      {  Updatetaskattrs(
            AOURL_Data,buf->buffer,
            AOURL_Datalength,buf->length,
            TAG_END);
         buf->length=0;
      }
   }
}

/* Read XOVER headers into article array for the article numbers starting
 * from (from), with a maximum of (nrarts) articles, but not beyond (lastavail).
 * If (buf) is given, re-use array entry [0] and print to buffer directly.
 * Otherwise keep headers in array and issue progress messages.
 * Returns last article nr read. */
static long Readaiheaders(struct Newsinfo *ni,struct Buffer *buf,
   struct Artindex *aindex,UBYTE *name,long from,long nrarts,long lastavail,long *pnrread,
   UBYTE *nntp)
{  UBYTE *line,*p,*q,*r;
   struct Artindex *ai;
   long nr,lastnr=0,nextmsg,delta,nrread=0,to;
   delta=nrarts/10;
   if(delta<10) delta=10;
   nextmsg=delta;
   while(nrread<nrarts && from<=lastavail)
   {  to=from+(nrarts-nrread)-1;
      if(to>lastavail) to=lastavail;
      if(Sendcommand(ni,"XOVER %d-%d",from,to)<400)
      {  while(line=Nextline(ni))
         {  if(line[0]=='.' && !line[1]) break;
            /* Line contains:
             * nr TAB subject TAB from TAB date TAB msgid TAB ref TAB bytes TAB lines [TAB ...]
             */
            nr=atoi(line);
            if(nr>=from && nr<=to)
            {  if(buf)
               {  ai=aindex;
                  Clearaiarray(ai,1);
               }
               else
               {  ai=&aindex[nr-from];
               }
               ai->nr=nr;
               if(!(p=strchr(line,'\t'))) continue;
               p++;
               if(!(q=strchr(p,'\t'))) continue;      /* p..q = subject */
               ai->subject=Dupstr(p,q-p);
               if(STRNIEQUAL(ai->subject,"RE:",3))
               {  for(r=ai->subject+3;*r && *r==' ';r++);
                  ai->sortject=r;
               }
               else ai->sortject=ai->subject;
               p=q+1;
               if(!(q=strchr(p,'\t'))) continue;      /* p..q = from */
               if((r=strchr(p,'<')) && r<q)    /* Real Name <user@domain> */
               {  ai->from=Dupstr(p,r-p-1);
               }
               else
               {  if((r=strchr(p,'(')) && r<q) /* user@domain (Real Name) */
                  {  p=r+1;
                     r=strchr(p,')');
                     if(!r || r>q) r=q;
                  }
                  else r=q;
                  ai->from=Dupstr(p,r-p);
               }
               p=q+1;
               if(!(q=strchr(p,'\t'))) continue;      /* p..q = date */
               p=q+1;
               if(!(q=strchr(p,'\t'))) continue;      /* p..q = msgid */
               if(*p=='<') p++;
               r=q-1;
               if(*r=='>') r--;
               ai->id=Dupstr(p,r-p+1);
               p=q+1;
               if(!(q=strchr(p,'\t'))) continue;      /* p..q = ref */
               p=q+1;
               if(!(q=strchr(p,'\t'))) continue;      /* p..q = bytes */
               p=q+1;
               if(!(q=strchr(p,'\t'))) q=p+strlen(p); /* p..q = lines */
               ai->lines=Dupstr(p,q-p);
               if(buf)
               {  Buildartindex(ni,buf,aindex,1,nntp);
               }
               else if(nr>=from+nextmsg)
               {  Tcpmessage(ni->fd,TCPMSG_NEWSSCAN,name,nextmsg);
                  nextmsg+=delta;
               }
               lastnr=nr;
               nrread++;
            }
         }
         from=to+1;
      }
      else break;
   }
   if(pnrread) *pnrread=nrread;
   return lastnr;
}

/* Sort compare function */
static int Artindexcompare(struct Artindex *ai1,struct Artindex *ai2)
{  int c=0;
   if(ai1->sortject && ai2->sortject)
   {  c=stricmp(ai1->sortject,ai2->sortject);
      if(!c) c=ai1->nr-ai2->nr;
   }
   return c;
}

/* Actually read the group (article index) */
static void Readgroupindex(struct Fetchdriver *fd,UBYTE *name,BOOL framed)
{  struct Newsinfo ni={0};
   struct Buffer buf={0};
   struct Artindex *aindex;
   long maxarts,start=1,lastfetched=0,gstart,gend,nrarts;
   struct Group *g;
   BOOL sortednews,subscribe=TRUE;
   UBYTE *host=NULL;
   if(strchr(name,'/'))  /* NNTP: host/group */
   {  host=Dupstr(name,-1);
      name=strchr(host,'/')+1;
      subscribe=FALSE;
   }
   ni.fd=fd;
   Bprintf(&buf,fd->block,"<HTML>\n<HEAD>\n<TITLE>News:%s</TITLE>\n",name);
   if(framed)
   {  Bprintf(&buf,fd->block,"<BASE TARGET='NewsArticle'>\n");
   }
   Bprintf(&buf,fd->block,"</HEAD>\n<BODY>\n<H1>News:%s\n</H1>\n",name);
   Updatetaskattrs(
      AOURL_Contenttype,"text/html",
      AOURL_Data,buf.buffer,
      AOURL_Datalength,buf.length,
      TAG_END);
   buf.length=0;
   if(Opennews(&ni,FALSE,host))
   {  ObtainSemaphore(&groupsema);
      if(g=Findgroup(&ni,name)) start=g->lastread+1;
      ReleaseSemaphore(&groupsema);
      ObtainSemaphore(fd->prefssema);
      maxarts=fd->prefs->maxartnews;
      sortednews=fd->prefs->sortednews;
      if(fd->prefs->newsbynum && !host)
      {  if(host=ALLOCTYPE(UBYTE,strlen(fd->prefs->nntphost)+strlen(name)+2,0))
         {  strcpy(host,fd->prefs->nntphost);
            strcat(host,"/");
            strcat(host,name);
            subscribe=TRUE;
         }
      }
      ReleaseSemaphore(fd->prefssema);
      Updatetaskattrs(AOURL_Netstatus,NWS_NEWSGROUP,TAG_END);
      Tcpmessage(ni.fd,TCPMSG_NEWSGROUP,name);
      if(Sendcommand(&ni,"GROUP %s",name)<400)
      {  gstart=start;gend=start;
         sscanf(ni.buffer+3," %*d %ld %ld",&gstart,&gend);
         if(gstart>start)
         {  start=gstart;
         }
         if(!maxarts) maxarts=gend-gstart+1;
         Bprintf(&buf,fd->block,"(<A HREF='x-aweb:news/3?%s' TARGET='_top'>%s</A>)\n",
            host?host:name,AWEBSTR(MSG_NEWS_POST_NEW_ARTICLE));
         if(subscribe)
         {
#ifndef DEMOVERSION
            Bprintf(&buf,fd->block,"(<A HREF='x-aweb:news/B/news?%s' TARGET='_top'>%s</A>)\n",
               name,AWEBSTR(MSG_NEWS_CATCH_UP));
            if(g)
            {  /* Group is in subscribed list */
               Bprintf(&buf,fd->block,"(<A HREF='x-aweb:news/5/news?%s' TARGET='_top'>%s</A>)\n",
                  name,AWEBSTR(MSG_NEWS_UNSUBSCRIBE));
            }
            else
            {  Bprintf(&buf,fd->block,"(<A HREF='x-aweb:news/4/news?%s' TARGET='_top'>%s</A>)\n",
                  name,AWEBSTR(MSG_NEWS_SUBSCRIBE));
            }
#endif
            Bprintf(&buf,fd->block,"(<A HREF='news:' TARGET='_top'>%s</A>)\n",
               AWEBSTR(MSG_NEWS_GROUP_LIST));
         }
         Bprintf(&buf,fd->block,"<BR><BR>\n<NOBR>\n");
         
         if(sortednews)
         {  /* Sorted list. Fetch, sort, build. */
            Updatetaskattrs(
               AOURL_Data,buf.buffer,
               AOURL_Datalength,buf.length,
               TAG_END);
            buf.length=0;
            Updatetaskattrs(AOURL_Netstatus,NWS_NEWSGROUP,TAG_END);
            Tcpmessage(ni.fd,TCPMSG_NEWSGROUP,name);
            if(aindex=ALLOCSTRUCT(Artindex,maxarts,MEMF_CLEAR))
            {  lastfetched=Readaiheaders(&ni,NULL,aindex,name,start,maxarts,gend,&nrarts,host);
               Tcpmessage(ni.fd,TCPMSG_NEWSSORT,name);
               if(lastfetched>=start)
               {  qsort(aindex,nrarts,sizeof(struct Artindex),Artindexcompare);
               }
               Buildartindex(&ni,&buf,aindex,nrarts,host);
               Clearaiarray(aindex,maxarts);
               FREE(aindex);
            }
         }
         else
         {  /* Unsorted list. Fetch and build on the fly. */
            if(aindex=ALLOCSTRUCT(Artindex,1,MEMF_CLEAR))
            {  lastfetched=Readaiheaders(&ni,&buf,aindex,name,start,maxarts,gend,NULL,host);
               Clearaiarray(aindex,1);
               FREE(aindex);
            }
         }

         ObtainSemaphore(&groupsema);
         if(g=Findgroup(&ni,name))
         {  g->lastfetched=lastfetched;
            Writesubscribed(&ni);
         }
         ReleaseSemaphore(&groupsema);
         Bprintf(&buf,fd->block,"</NOBR>\n<BR>\n");
         Bprintf(&buf,fd->block,"(<A HREF='x-aweb:news/3?%s' TARGET='_top'>%s</A>)\n",
            host?host:name,AWEBSTR(MSG_NEWS_POST_NEW_ARTICLE));
         if(subscribe)
         {
#ifndef DEMOVERSION
            Bprintf(&buf,fd->block,"(<A HREF='x-aweb:news/B/news?%s' TARGET='_top'>%s</A>)\n",
               name,AWEBSTR(MSG_NEWS_CATCH_UP));
            if(g)
            {  /* Group is in subscribed list */
               Bprintf(&buf,fd->block,"(<A HREF='x-aweb:news/5/news?%s' TARGET='_top'>%s</A>)\n",
                  name,AWEBSTR(MSG_NEWS_UNSUBSCRIBE));
            }
            else
            {  Bprintf(&buf,fd->block,"(<A HREF='x-aweb:news/4/news?%s' TARGET='_top'>%s</A>)\n",
                  name,AWEBSTR(MSG_NEWS_SUBSCRIBE));
            }
#endif
            Bprintf(&buf,fd->block,"(<A HREF='news:' TARGET='_top'>%s</A>)\n",
               AWEBSTR(MSG_NEWS_GROUP_LIST));
         }
      }
      else
      {  Bprintf(&buf,fd->block,AWEBSTR(MSG_NEWS_ERROR_NO_GROUP),name);
      }
   }
   Bprintf(&buf,fd->block,"</BODY>\n</HTML>\n");
   Updatetaskattrs(
      AOURL_Data,buf.buffer,
      AOURL_Datalength,buf.length,
      TAG_END);
   Closenews(&ni);
   Freebuffer(&buf);
   if(host) FREE(host);
}

static void Readgroup(struct Fetchdriver *fd,UBYTE *name)
{  BOOL framed;
   ObtainSemaphore(fd->prefssema);
   framed=fd->prefs->framednews && fd->prefs->doframes;
   ReleaseSemaphore(fd->prefssema);
   if(framed)
   {  struct Buffer buf={0};
      Bprintf(&buf,fd->block,"<HTML>\n<HEAD>\n<TITLE>News:%s</TITLE>\n</HEAD>\n",name);
      Bprintf(&buf,fd->block,"<FRAMESET ROWS='40%,60%'>\n");
      Bprintf(&buf,fd->block,"  <FRAME SRC='x-aweb:news/6/%s' NAME='NewsIndex'>\n",name);
      Bprintf(&buf,fd->block,"  <FRAME NAME='NewsArticle'>\n");
      Bprintf(&buf,fd->block,"</FRAMESET>\n<NOFRAMES>\n<BODY>\n");
      Bprintf(&buf,fd->block,AWEBSTR(MSG_NEWS_USE_FRAMES));
      Bprintf(&buf,fd->block,"</BODY>\n</NOFRAMES>\n</HTML>\n");
      Updatetaskattrs(
         AOURL_Contenttype,"text/html",
         AOURL_Data,buf.buffer,
         AOURL_Datalength,buf.length,
         TAG_END);
      Freebuffer(&buf);
   }
   else
   {  Readgroupindex(fd,name,FALSE);
   }
}

/*-----------------------------------------------------------------------*/
/* news:article@id                                                       */
/* nntp://host/group/artnr                                               */
/*-----------------------------------------------------------------------*/
/* Read article */

/* Add an article line. Convert any URL to hyperlink. */
static void Addartline(struct Buffer *buf,UBYTE *line)
{  UBYTE *p,*q;
   for(p=line;*p;p++)
   {  if(STRNIEQUAL(p,"HTTP://",7)
      || STRNIEQUAL(p,"FTP://",6))
      {  /* Terminate with space but remove trailing common interpunction */
         if(!(q=strchr(p,' '))) q=p+strlen(p);
         for(q--;q>=p && strchr(".!?,;:'\"()<>[]{}",*q);q--);
         q++;
         Addblock(buf,line,p-line);
         Addbhtml(buf,"<A HREF='",-1);
         Addblock(buf,p,q-p);
         Addbhtml(buf,"' TARGET='_blank'>",-1);
         Addblock(buf,p,q-p);
         Addbhtml(buf,"</A>",-1);
         line=q;
         p=q;
      }
   }
   Addblock(buf,line,-1);
}

/* (id) might also be "host/group/artnr" */
static void Readarticle(struct Fetchdriver *fd,UBYTE *id)
{  struct Newsinfo ni={0};
   struct Buffer buf={0};
   BOOL longhdr,propfont,headers,include,ok=FALSE,nofup=FALSE;
   UBYTE *line,*p,*host=NULL,*group=NULL,*artnr=NULL;
   long result;
   if(!strchr(id,'@'))     /* NNTP: host/group/artnr */
   {  host=Dupstr(id,-1);
      if(p=strchr(host,'/'))
      {  *p='\0';
         group=p+1;
         if(p=strchr(group,'/'))
         {  *p='\0';
            artnr=p+1;
         }
      }
   }
   ni.fd=fd;
   ObtainSemaphore(fd->prefssema);
   longhdr=fd->prefs->longhdrnews;
   propfont=fd->prefs->propnews;
   ReleaseSemaphore(fd->prefssema);
   Updatetaskattrs(
      AOURL_Contenttype,"text/html",
      TAG_END);
   if(Opennews(&ni,FALSE,host))
   {  Bprintf(&buf,fd->block,"<HTML>\n<HEAD>\n<TITLE>News:%s</TITLE>\n</HEAD>\n",id);
      Bprintf(&buf,fd->block,"</HEAD>\n<BODY>\n");
      Bprintf(&buf,fd->block,propfont?"<NOBR>\n":"<PRE>\n");
      if(artnr)
      {  if(Sendcommand(&ni,"GROUP %s",group)<400)
         {  result=Sendcommand(&ni,"ARTICLE %s",artnr);
         }
      }
      else
      {  result=Sendcommand(&ni,"ARTICLE <%s>",id);
      }
      if(result<400)
      {  headers=TRUE;
         include=FALSE;
         while(line=Nextline(&ni))
         {  if(line[0]=='.')
            {  if(!line[1]) break;
               else line++;
            }
            if(headers)
            {  if(*line==' ' || *line=='\t')
               {  /* Header continuation line */
                  if(include)
                  {  Addblock(&buf,line,-1);
                  }
               }
               else
               {  /* New header line */
                  if(p=strchr(line,':'))
                  {  include=longhdr
                        || STRNIEQUAL(line,"FROM:",5)
                        || STRNIEQUAL(line,"SUBJECT:",8)
                        || STRNIEQUAL(line,"DATE:",5);
                     if(STRNIEQUAL(line,"FOLLOWUP-TO:",12))
                     {  for(p++;*p && *p==' ';p++);
                        if(STRNIEQUAL(p,"POSTER",6)) nofup=TRUE;
                     }
                  }
                  else
                  {  /* Blank line */
                     include=TRUE;
                     headers=FALSE;
                  }
               }
               if(include)
               {  Addblock(&buf,line,-1);
                  Addbhtml(&buf,propfont?"<BR>\n":"\n",-1);
               }
            }
            else
            {  if(*line=='>') Addbhtml(&buf,"<I>",-1);
               Addartline(&buf,line);
               if(*line=='>') Addbhtml(&buf,"</I>",-1);
               Addbhtml(&buf,propfont?"<BR>\n":"\n",-1);
            }
            if(buf.length>512)
            {  Updatetaskattrs(
                  AOURL_Data,buf.buffer,
                  AOURL_Datalength,buf.length,
                  TAG_END);
               buf.length=0;
            }
            ok=TRUE;
         }
      }
      else
      {  Bprintf(&buf,fd->block,AWEBSTR(MSG_NEWS_ERROR_NO_ARTICLE),id);
         Bprintf(&buf,fd->block,"<BR>\n%s\n",ni.buffer);
      }
      Bprintf(&buf,fd->block,propfont?"</NOBR>\n<BR>\n":"</PRE>\n<HR>\n");
      if(ok)
      {  if(!nofup)
         {  Bprintf(&buf,fd->block,"(<A HREF='x-aweb:news/followup?%s' TARGET='_top'>%s</A>)\n",
               id,AWEBSTR(MSG_NEWS_FOLLOW_UP));
         }
         Bprintf(&buf,fd->block,"(<A HREF='x-aweb:news/reply?%s' TARGET='_top'>%s</A>)\n",
            id,AWEBSTR(MSG_NEWS_REPLY));
         Addbhtml(&buf,"(<A HREF='x-aweb:news/D/",-1);
         Addblock(&buf,id,-1);
         Bprintf(&buf,fd->block,"'>%s</A>)\n",AWEBSTR(MSG_NEWS_SAVE));
      }
      Bprintf(&buf,fd->block,"</BODY>\n</HTML>\n");
   }
   Updatetaskattrs(
      AOURL_Data,buf.buffer,
      AOURL_Datalength,buf.length,
      TAG_END);
   Closenews(&ni);
   Freebuffer(&buf);
   if(host) FREE(host);
}

/*-----------------------------------------------------------------------*/
/* x-aweb:news/2?group=group&subscribe=xxxx                              */
/* x-aweb:news/2?group=group&read=xxxx                                   */
/*-----------------------------------------------------------------------*/
/* (subscribe=xxxx) subscribe to group or (read=xxxx) read group */

static void Subscribeorread(struct Fetchdriver *fd,UBYTE *args)
{  UBYTE *group,*p;
   if(STRNIEQUAL(args,"GROUP=",6))
   {  group=args+6;
      if(p=strchr(group,'&'))
      {  *p++='\0';
         Unescape(group,p-group);
#ifndef DEMOVERSION
         if(STRNIEQUAL(p,"SUBSCRIBE=",10))
         {  Subscribe(fd,group,FALSE);
            Updatetaskattrs(
               AOURL_Movedto,"news:",
               TAG_END);
         }
         else
#endif
         if(STRNIEQUAL(p,"READ=",5))
         {  sprintf(fd->block,"news:%s",group);
            Updatetaskattrs(
               AOURL_Movedto,fd->block,
               TAG_END);
         }
      }
   }
}

/*-----------------------------------------------------------------------*/
/* x-aweb:news/3?group                                                   */
/* x-aweb:news/3?host/group                                              */
/*-----------------------------------------------------------------------*/
/* Post new article to group: build post form */

static void Postnewarticle(struct Fetchdriver *fd,UBYTE *name)
{  struct Buffer buf={0};
   UBYTE *nntp=NULL,*p;
   if(p=strchr(name,'/'))
   {  nntp=Dupstr(name,-1);
      p=strchr(nntp,'/');
      *p='\0';
      name=p+1;
   }
   Updatetaskattrs(
      AOURL_Contenttype,"text/html",
      TAG_END);
   Bprintf(&buf,fd->block,"<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n</HEAD>\n"
      "<BODY>\n<H1>%s</H1>\n"
      "<FORM ACTION='x-aweb:news/post%s%s' METHOD=POST>\n"
      "<TABLE>\n<TR>\n<TD ALIGN='right'>%s\n"
      "<TD><INPUT NAME='subject' SIZE=60>\n"
      "<TR>\n<TD ALIGN='right'>%s\n",
      AWEBSTR(MSG_NEWS_POST_NEW_ARTICLE),
      AWEBSTR(MSG_NEWS_POST_NEW_ARTICLE),
      nntp?(UBYTE *)"?":NULLSTRING,
      nntp?nntp:NULLSTRING,
      AWEBSTR(MSG_NEWS_SUBJECT),
      AWEBSTR(MSG_NEWS_NEWSGROUPS));
   Bprintf(&buf,fd->block,"<TD><INPUT NAME='newsgroups' VALUE='%s' SIZE=60><BR>\n",name);
   Bprintf(&buf,fd->block,"</TABLE>\n"
      "%s (<INPUT TYPE=CHECKBOX NAME='extra' VALUE='Y'> %s)<BR>\n"
      "<TEXTAREA NAME='data' COLS=70 ROWS=20>\n",
      AWEBSTR(MSG_NEWS_ARTICLE_BODY),
      AWEBSTR(MSG_NEWS_EXTRA_HEADERS));
   Addsignature(&buf,fd);
   Bprintf(&buf,fd->block,"</TEXTAREA><BR><BR>\n<INPUT TYPE=SUBMIT VALUE='%s'> "
      "<INPUT TYPE=RESET VALUE='%s'> "
      "<INPUT TYPE=SUBMIT NAME='Return' VALUE='%s'>\n",
      AWEBSTR(MSG_NEWS_POST_ARTICLE),
      AWEBSTR(MSG_NEWS_RESET),
      AWEBSTR(MSG_NEWS_RETURN));
   Updatetaskattrs(
      AOURL_Data,buf.buffer,
      AOURL_Datalength,buf.length,
      TAG_END);
   Freebuffer(&buf);
   if(nntp) FREE(nntp);
}
   
/*-----------------------------------------------------------------------*/
/* x-aweb:news/4/news?group                                              */
/*-----------------------------------------------------------------------*/
/* Subscribe to group */

#ifndef DEMOVERSION

static void Subscribe(struct Fetchdriver *fd,UBYTE *name,BOOL showlist)
{  struct Newsinfo ni={0};
   struct Group *g,*ga;
   BOOL found=FALSE;
   ni.fd=fd;
   if(*name && Obtaintasksemaphore(&groupsema))
   {  Readsubscribed(&ni);
      ga=NULL;
      for(g=groups.first;g->next;g=g->next)
      {  if(g->name && STRIEQUAL(g->name,name))
         {  found=TRUE;
            break;
         }
         if(!ga && g->name && stricmp(g->name,name)>0)
         {  ga=g->prev;
         }
      }
      if(!found)
      {  if(!ga) ga=groups.last;
         if(g=Allocgroup(name,-1))
         {  INSERT(&groups,g,ga);
            Writesubscribed(&ni);
         }
      }
      if(showlist)
      {  Buildmain(fd);
      }
      ReleaseSemaphore(&groupsema);
   }
}

#endif /* DEMOVERSION */

/*-----------------------------------------------------------------------*/
/* x-aweb:news/5/news?group                                              */
/*-----------------------------------------------------------------------*/
/* Unsubscribe from group */

#ifndef DEMOVERSION

static void Unsubscribe(struct Fetchdriver *fd,UBYTE *name)
{  struct Newsinfo ni={0};
   struct Group *g;
   BOOL found=FALSE;
   ni.fd=fd;
   if(*name && Obtaintasksemaphore(&groupsema))
   {  Readsubscribed(&ni);
      for(g=groups.first;g->next;g=g->next)
      {  if(g->name && STRIEQUAL(g->name,name))
         {  REMOVE(g);
            Freegroup(g);
            found=TRUE;
            break;
         }
      }
      if(found)
      {  Writesubscribed(&ni);
      }
      Buildmain(fd);
      ReleaseSemaphore(&groupsema);
   }
}

#endif /* DEMOVERSION */

/*-----------------------------------------------------------------------*/
/* x-aweb:news/followup?article@id                                       */
/* x-aweb:news/followup?host/group/artnr                                 */
/*-----------------------------------------------------------------------*/
/* Followup to article: build post form */

static void Followuptoarticle(struct Fetchdriver *fd,UBYTE *id)
{  struct Newsinfo ni={0};
   struct Buffer buf={0};
   UBYTE *line,*p,*address,*realname;
   UBYTE *from=NULL,*destgroups=NULL,*newsgroups=NULL,*subject=NULL,
      *references=NULL,*date=NULL;
   BOOL headers,ok=FALSE;
   UBYTE *host=NULL,*group=NULL,*artnr=NULL;
   long result;
   if(!strchr(id,'@'))     /* NNTP: host/group/artnr */
   {  host=Dupstr(id,-1);
      if(p=strchr(host,'/'))
      {  *p='\0';
         group=p+1;
         if(p=strchr(group,'/'))
         {  *p='\0';
            artnr=p+1;
         }
      }
   }
   ni.fd=fd;
   Updatetaskattrs(
      AOURL_Contenttype,"text/html",
      TAG_END);
   if(Opennews(&ni,FALSE,host))
   {  Bprintf(&buf,fd->block,"<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n</HEAD>\n",
         AWEBSTR(MSG_NEWS_FOLLOWUP_TITLE));
      Bprintf(&buf,fd->block,"</HEAD>\n<BODY>\n");
      if(artnr)
      {  if(Sendcommand(&ni,"GROUP %s",group)<400)
         {  result=Sendcommand(&ni,"ARTICLE %s",artnr);
         }
      }
      else
      {  result=Sendcommand(&ni,"ARTICLE <%s>",id);
      }
      if(result<400)
      {  headers=TRUE;
         while(line=Nextline(&ni))
         {  if(line[0]=='.')
            {  if(!line[1]) break;
               else line++;
            }
            if(headers)
            {  if(*line==' ' || *line=='\t')
               {  /* Header continuation line */
               }
               else
               {  /* New header line */
                  if(p=strchr(line,':'))
                  {  for(p++;*p && *p==' ';p++);
                     if(STRNIEQUAL(line,"NEWSGROUPS:",11))
                     {  if(!newsgroups) newsgroups=Dupstr(p,-1);
                        if(!destgroups) destgroups=Dupstr(p,-1);
                     }
                     else if(STRNIEQUAL(line,"FOLLOWUP-TO:",12))
                     {  if(destgroups) FREE(destgroups);
                        destgroups=Dupstr(p,-1);
                     }
                     if(!subject && STRNIEQUAL(line,"SUBJECT:",8))
                     {  subject=Dupstr(p,-1);
                     }
                     if(!references && STRNIEQUAL(line,"REFERENCES:",11))
                     {  references=Dupstr(p,-1);
                     }
                     if(!from && STRNIEQUAL(line,"FROM:",5))
                     {  from=Dupstr(p,-1);
                     }
                     if(!date && STRNIEQUAL(line,"DATE:",5))
                     {  date=Dupstr(p,-1);
                     }
                  }
                  else
                  {  /* Blank line, end of headers */
                     headers=FALSE;
                     if(destgroups)
                     {  Bprintf(&buf,fd->block,"<BODY>\n<H1>%s ",AWEBSTR(MSG_NEWS_FOLLOWUP_TO_GROUPS));
                        Addblock(&buf,destgroups,-1);
                        Bprintf(&buf,fd->block,"</H1>\n"
                           "<FORM ACTION='x-aweb:news/post%s%s' METHOD=POST>\n"
                           "<TABLE><TR>\n<TD ALIGN='right'>%s\n"
                           "<TD><INPUT NAME='subject' VALUE='Re: ",
                           host?(UBYTE *)"?":NULLSTRING,
                           host?host:NULLSTRING,
                           AWEBSTR(MSG_NEWS_SUBJECT));
                        if(subject)
                        {  p=subject;
                           if(STRNIEQUAL(p,"Re: ",4)) p+=4;
                           Addblock(&buf,p,-1);
                        }
                        else
                        {  Addblock(&buf,AWEBSTR(MSG_NEWS_NO_SUBJECT),-1);
                        }
                        Bprintf(&buf,fd->block,"' SIZE=60>\n"
                           "<TR>\n<TD ALIGN='right'>%s\n"
                           "<TD><INPUT NAME='newsgroups' VALUE='",
                           AWEBSTR(MSG_NEWS_NEWSGROUPS));
                        Addblock(&buf,destgroups,-1);
                        Addbhtml(&buf,"' SIZE=60>\n"
                           "</TABLE>\n"
                           "<INPUT TYPE=HIDDEN NAME='references' VALUE='",-1);
                        if(references)
                        {  Addblock(&buf,references,-1);
                           Addbhtml(&buf," ",-1);
                        }
                        Bprintf(&buf,fd->block,"&lt;%s&gt;",id);
                        Bprintf(&buf,fd->block,"'>\n%s "
                           "(<INPUT TYPE=CHECKBOX NAME='extra' VALUE='Y'> %s) "
                           "(%s)<BR>\n"
                           "<TEXTAREA NAME='data' COLS=70 ROWS=20>\n",
                           AWEBSTR(MSG_NEWS_ARTICLE_BODY),
                           AWEBSTR(MSG_NEWS_EXTRA_HEADERS),
                           AWEBSTR(MSG_NEWS_PLEASE_DELETE));
                        if(from)
                        {  if(p=strchr(from,'<'))        /* real Name <user@domain> */
                           {  realname=from;
                              p[-1]='\0';
                              address=p+1;
                              if(p=strchr(address,'>')) *p='\0';
                           }
                           else if(p=strchr(from,'('))   /* user@domain (Real Name) */
                           {  address=from;
                              p[-1]='\0';
                              realname=p+1;
                              if(p=strchr(realname,')')) *p='\0';
                           }
                           else
                           {  address=realname=from;
                           }
                        }
                        else
                        {  address=NULL;
                           realname=NULL;
                        }
                        ObtainSemaphore(fd->prefssema);
                        if(p=Pprintf(fd->prefs->newsquotehdr,"degins",
                           date?date:(UBYTE *)"(no date)",
                           address?address:(UBYTE *)"(no address)",
                           newsgroups?newsgroups:(UBYTE *)"(no newsgroups)",
                           id,
                           realname?realname:(UBYTE *)"(no name)",
                           subject?subject:(UBYTE *)"(no subject)"))
                        {  Addblock(&buf,p,-1);
                           Addbhtml(&buf,"\n",-1);
                           FREE(p);
                        }
                        ReleaseSemaphore(fd->prefssema);
                        ok=TRUE;
                     }
                  }
               }
            }
            else
            {  /* Quote article text */
               if(ok)
               {  Addblock(&buf,"> ",-1);
                  Addblock(&buf,line,-1);
                  Addbhtml(&buf,"\n",-1);
               }
            }
            if(buf.length>512)
            {  Updatetaskattrs(
                  AOURL_Data,buf.buffer,
                  AOURL_Datalength,buf.length,
                  TAG_END);
               buf.length=0;
            }
         }
      }
      else
      {  Bprintf(&buf,fd->block,AWEBSTR(MSG_NEWS_ERROR_NO_ARTICLE),id);
         Bprintf(&buf,fd->block,"<BR>\n%s\n",ni.buffer);
      }
      if(date) FREE(date);
      if(from) FREE(from);
      if(newsgroups) FREE(newsgroups);
      if(destgroups) FREE(destgroups);
      if(subject) FREE(subject);
      if(references) FREE(references);
      if(ok)
      {  Addsignature(&buf,fd);
         Bprintf(&buf,fd->block,"</TEXTAREA>\n<BR><BR>\n<INPUT TYPE=SUBMIT VALUE='%s'> "
            "<INPUT TYPE=RESET VALUE='%s'> "
            "<INPUT TYPE=SUBMIT NAME='Return' VALUE='%s'>\n"
            "</FORM>\n",
            AWEBSTR(MSG_NEWS_POST_ARTICLE),
            AWEBSTR(MSG_NEWS_RESET),
            AWEBSTR(MSG_NEWS_RETURN));
      }
      Bprintf(&buf,fd->block,"</BODY>\n</HTML>\n");
   }
   Updatetaskattrs(
      AOURL_Data,buf.buffer,
      AOURL_Datalength,buf.length,
      TAG_END);
   Closenews(&ni);
   Freebuffer(&buf);
   if(host) FREE(host);
}

/*-----------------------------------------------------------------------*/
/* x-aweb:news/reply?article@id                                          */
/* x-aweb:news/reply?host/group/artnr                                    */
/*-----------------------------------------------------------------------*/
/* E-mail reply to article: build mail form */

static void Replytoarticle(struct Fetchdriver *fd,UBYTE *id)
{  struct Newsinfo ni={0};
   struct Buffer buf={0};
   UBYTE *line,*p,*address,*realname;
   UBYTE *from=NULL,*to=NULL,*subject=NULL,*date=NULL,*newsgroups=NULL;
   BOOL headers,ok=FALSE;
   UBYTE *host=NULL,*group=NULL,*artnr=NULL,*artid=NULL;
   long result;
   if(!strchr(id,'@'))     /* NNTP: host/group/artnr */
   {  host=Dupstr(id,-1);
      if(p=strchr(host,'/'))
      {  *p='\0';
         group=p+1;
         if(p=strchr(group,'/'))
         {  *p='\0';
            artnr=p+1;
         }
      }
   }
   ni.fd=fd;
   Updatetaskattrs(
      AOURL_Contenttype,"text/html",
      TAG_END);
   if(Opennews(&ni,FALSE,host))
   {  Bprintf(&buf,fd->block,"<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n</HEAD>\n",
         AWEBSTR(MSG_NEWS_REPLY_TITLE));
      Bprintf(&buf,fd->block,"</HEAD>\n<BODY>\n");
      if(artnr)
      {  if(Sendcommand(&ni,"GROUP %s",group)<400)
         {  result=Sendcommand(&ni,"ARTICLE %s",artnr);
         }
      }
      else
      {  result=Sendcommand(&ni,"ARTICLE <%s>",id);
      }
      if(result<400)
      {  headers=TRUE;
         while(line=Nextline(&ni))
         {  if(line[0]=='.')
            {  if(!line[1]) break;
               else line++;
            }
            if(headers)
            {  if(*line==' ' || *line=='\t')
               {  /* Header continuation line */
               }
               else
               {  /* New header line */
                  if(p=strchr(line,':'))
                  {  for(p++;*p && *p==' ';p++);
                     if(STRNIEQUAL(line,"FROM:",5))
                     {  if(!from) from=Dupstr(p,-1);
                        if(!to) to=Dupstr(p,-1);
                     }
                     else if(STRNIEQUAL(line,"REPLY-TO:",9))
                     {  if(to) FREE(to);
                        to=Dupstr(p,-1);
                     }
                     if(!subject && STRNIEQUAL(line,"SUBJECT:",8))
                     {  subject=Dupstr(p,-1);
                     }
                     if(!date && STRNIEQUAL(line,"DATE:",5))
                     {  date=Dupstr(p,-1);
                     }
                     if(!newsgroups && STRNIEQUAL(line,"NEWSGROUPS:",11))
                     {  newsgroups=Dupstr(p,-1);
                     }
                     if(host && STRNIEQUAL(line,"MESSAGE-ID:",11))
                     {  if(*p=='<') p++;
                        artid=Dupstr(p+1,-1);
                        if(p=strchr(artid,'>')) *p='\0';
                     }
                  }
                  else
                  {  /* Blank line, end of headers */
                     headers=FALSE;
                     if(from)
                     {  Bprintf(&buf,fd->block,"<BODY>\n<H1>%s ",
                           AWEBSTR(MSG_NEWS_REPLY_TO_ADDRESS));
                        Addblock(&buf,to,-1);
                        Bprintf(&buf,fd->block,"</H1>\n<FORM ACTION='x-aweb:mail/1/mail' METHOD=POST>\n"
                           "<TABLE>\n<TR>\n<TD ALIGN='right'>%s\n"
                           "<TD><INPUT NAME='to' VALUE='",
                           AWEBSTR(MSG_MAIL_TO));
                        Addblock(&buf,to,-1);
                        Bprintf(&buf,fd->block,"' SIZE=60>\n"
                           "<TR>\n<TD ALIGN='right'>%s\n"
                           "<TD><INPUT NAME='subject' VALUE='Re: ",
                           AWEBSTR(MSG_MAIL_SUBJECT));
                        if(subject)
                        {  p=subject;
                           if(STRNIEQUAL(p,"Re: ",4)) p+=4;
                           Addblock(&buf,p,-1);
                        }
                        else
                        {  Addblock(&buf,"(no subject)",-1);
                        }
                        Addbhtml(&buf,"' SIZE=60>\n"
                           "</TABLE>\n"
                           "<INPUT TYPE=HIDDEN NAME='inreplyto' VALUE='",-1);
                        Addblock(&buf,artid?artid:id,-1);
                        Bprintf(&buf,fd->block,"'>\n"
                           "%s (<INPUT TYPE=CHECKBOX NAME='extra' VALUE='Y'> %s)<BR>\n"
                           "<TEXTAREA NAME='data' COLS=70 ROWS=20>\n",
                           AWEBSTR(MSG_MAIL_MESSAGE_BODY),
                           AWEBSTR(MSG_MAIL_EXTRA_HEADERS));
                        if(from)
                        {  if(p=strchr(from,'<'))        /* real Name <user@domain> */
                           {  realname=from;
                              p[-1]='\0';
                              address=p+1;
                              if(p=strchr(address,'>')) *p='\0';
                           }
                           else if(p=strchr(from,'('))   /* user@domain (Real Name) */
                           {  address=from;
                              p[-1]='\0';
                              realname=p+1;
                              if(p=strchr(address,')')) *p='\0';
                           }
                           else
                           {  address=realname=from;
                           }
                        }
                        else
                        {  address=NULL;
                           realname=NULL;
                        }
                        ObtainSemaphore(fd->prefssema);
                        if(p=Pprintf(fd->prefs->mailquotehdr,"degins",
                           date?date:(UBYTE *)"(no date)",
                           address?address:(UBYTE *)"(no address)",
                           newsgroups?newsgroups:(UBYTE *)"(no newsgroups)",
                           id,
                           realname?realname:(UBYTE *)"(no name)",
                           subject?subject:(UBYTE *)"(no subject)"))
                        {  Addblock(&buf,p,-1);
                           Addbhtml(&buf,"\n",-1);
                           FREE(p);
                        }
                        ReleaseSemaphore(fd->prefssema);
                        ok=TRUE;
                     }
                  }
               }
            }
            else
            {  /* Quote article text */
               if(ok)
               {  Addblock(&buf,"> ",-1);
                  Addblock(&buf,line,-1);
                  Addbhtml(&buf,"\n",-1);
               }
            }
            if(buf.length>512)
            {  Updatetaskattrs(
                  AOURL_Data,buf.buffer,
                  AOURL_Datalength,buf.length,
                  TAG_END);
               buf.length=0;
            }
         }
      }
      else
      {  Bprintf(&buf,fd->block,AWEBSTR(MSG_NEWS_ERROR_NO_ARTICLE),id);
         Bprintf(&buf,fd->block,"<BR>\n%s\n",ni.buffer);
      }
      if(from) FREE(from);
      if(to) FREE(to);
      if(subject) FREE(subject);
      if(date) FREE(date);
      if(newsgroups) FREE(newsgroups);
      if(artid) FREE(artid);
      if(ok)
      {  Addsignature(&buf,fd);
         Bprintf(&buf,fd->block,"</TEXTAREA>\n<BR><BR>\n<INPUT TYPE=SUBMIT VALUE='%s'> "
            "<INPUT TYPE=RESET VALUE='%s'> "
            "<INPUT TYPE=SUBMIT NAME='Return' VALUE='%s'>\n"
            "</FORM>\n",
            AWEBSTR(MSG_MAIL_SEND),
            AWEBSTR(MSG_MAIL_RESET),
            AWEBSTR(MSG_MAIL_RETURN));
      }
      Bprintf(&buf,fd->block,"</BODY>\n</HTML>\n");
   }
   Updatetaskattrs(
      AOURL_Data,buf.buffer,
      AOURL_Datalength,buf.length,
      TAG_END);
   Closenews(&ni);
   Freebuffer(&buf);
   if(host) FREE(host);
}

/*-----------------------------------------------------------------------*/
/* x-aweb:news/post (formfields)                                         */
/* x-aweb:news/post?host (formfields)                                    */
/*-----------------------------------------------------------------------*/
/* Post article (Return button already checked).
 * Form fields are expected in order:
 * subject, newsgroups, [references,] [extra=Y,] data */

/* Build article in buffer */
static BOOL Buildarticle(struct Fetchdriver *fd,struct Buffer *buf,UBYTE *formdata)
{  UBYTE *p,*q,*start;
   UBYTE spamblock[8];
   UBYTE *spammsg="** To reply in e-mail, remove \"%s\" and \".invalid\" from address **\r\n\r\n";
   BOOL extraheaders=FALSE;
   p=formdata;
   ObtainSemaphore(fd->prefssema);
   Bprintf(buf,fd->block,"From: ");
   if(fd->prefs->spamblock)
   {  Spamblock(spamblock,formdata);
      Addaddress(buf,fd->prefs->emailaddr,spamblock);
   }
   else
   {  Bprintf(buf,fd->block,"%s",fd->prefs->emailaddr);
   }
   Bprintf(buf,fd->block," (%s)\r\n",fd->prefs->fullname);
   if(!STRNIEQUAL(p,"SUBJECT=",8)) goto badform;
   p+=8;
   if(!(q=strchr(p,'&'))) goto badform;
   *q='\0';
   p[Unescape(p,q-p)]='\0';
   Bprintf(buf,fd->block,"Subject: %s\r\n",p);
   p=q+1;
   if(!STRNIEQUAL(p,"NEWSGROUPS=",11)) goto badform;
   p+=11;
   if(!(q=strchr(p,'&'))) goto badform;
   *q='\0';
   p[Unescape(p,q-p)]='\0';
   Bprintf(buf,fd->block,"Newsgroups: %s\r\n",p);
   p=q+1;
   if(*fd->prefs->organization)
   {  Bprintf(buf,fd->block,"Organization: %s\r\n",fd->prefs->organization);
   }
   if(*fd->prefs->replyaddr)
   {  Bprintf(buf,fd->block,"Reply-To: ");
      if(fd->prefs->spamblock)
      {  Addaddress(buf,fd->prefs->replyaddr,spamblock);
      }
      else
      {  Bprintf(buf,fd->block,"%s",fd->prefs->replyaddr);
      }
      Addtobuffer(buf,"\r\n",2);
   }
   if(STRNIEQUAL(p,"REFERENCES=",11))
   {  p+=11;
      if(!(q=strchr(p,'&'))) goto badform;
      *q='\0';
      p[Unescape(p,q-p)]='\0';
      Bprintf(buf,fd->block,"References: %s\r\n",p);
      p=q+1;
   }
   Bprintf(buf,fd->block,"X-Newsreader: Amiga-AWeb/%s\r\n",Awebversion());
   if(STRNIEQUAL(p,"EXTRA=Y&",8))
   {  p+=8;
      extraheaders=TRUE;
   }
   else
   {  Bprintf(buf,fd->block,"\r\n");
      if(fd->prefs->spamblock)
      {  Bprintf(buf,fd->block,spammsg,spamblock);
      }
   }
   if(!STRNIEQUAL(p,"DATA=",5)) goto badform;
   p+=5;
   ReleaseSemaphore(fd->prefssema);
   p[Unescape(p,strlen(p))]='\0';
   start=p;
   for(;;)
   {  if(*p=='.')
      {  Addtobuffer(buf,start,p-start);
         Addtobuffer(buf,".",1);
         start=p;
      }
      if(!(q=strchr(p,'\r'))) break;
      q+=2; /* Skip over \r\n */
      p=q;
      if(extraheaders && fd->prefs->spamblock && p[0]=='\r' && p[1]=='\n')
      {  /* Insert spamblock msg after extra headers (and after empty line) */
         p+=2;
         Addtobuffer(buf,start,p-start);
         start=p;
         Bprintf(buf,fd->block,spammsg,spamblock);
         extraheaders=FALSE;
      }
   }
   Addtobuffer(buf,start,strlen(start));
   p=buf->buffer+buf->length-2;
   if(!(p[0]=='\r' && p[1]=='\n'))
   {  Addtobuffer(buf,"\r\n",2);
   }
   return TRUE;

/* form fields were bad */
badform:
   ReleaseSemaphore(fd->prefssema);
   return FALSE;
}

enum POSTARTICLE_RETURNS
{  PAR_FAIL,PAR_NOCONNECT,PAR_Ok,
};

/* Show post error requester. Return TRUE if retry requested. */
static BOOL Postretry(struct Fetchdriver *fd,struct Buffer *buf,short err)
{  struct FileRequester *afr;
   struct Screen *scr;
   long result,file,height;
   struct DimensionInfo dim={0};
   if(err==PAR_NOCONNECT)
   {  ObtainSemaphore(fd->prefssema);
      if(*fd->prefs->nntphost)
      {  sprintf(fd->block,AWEBSTR(MSG_NEWS_NOCONNECT),fd->prefs->nntphost);
      }
      else
      {  strcpy(fd->block,AWEBSTR(MSG_NEWS_NONNTPHOST));
      }
      ReleaseSemaphore(fd->prefssema);
   }
   else
   {  strcpy(fd->block,AWEBSTR(MSG_NEWS_POSTFAILED));
   }
   result=Syncrequest(AWEBSTR(MSG_NEWS_TITLE),fd->block,AWEBSTR(MSG_NEWS_BUTTONS));
   if(result==2) /* Save */
   {  if(scr=(struct Screen *)Agetattr(Aweb(),AOAPP_Screen))
      {  if(GetDisplayInfoData(NULL,(UBYTE *)&dim,sizeof(dim),DTAG_DIMS,
            GetVPModeID(&scr->ViewPort)))
         {  height=(dim.Nominal.MaxY-dim.Nominal.MinY+1)*4/5;
         }
         else
         {  height=scr->Height*4/5;
         }
         if(afr=AllocAslRequestTags(ASL_FileRequest,
            ASLFR_Screen,scr,
            ASLFR_TitleText,AWEBSTR(MSG_NEWS_SAVETITLE),
            ASLFR_InitialHeight,height,
            ASLFR_InitialDrawer,"RAM:",
            ASLFR_InitialFile,"Article",
            ASLFR_RejectIcons,TRUE,
            ASLFR_DoSaveMode,TRUE,
            TAG_END))
         {  if(AslRequest(afr,NULL))
            {  strcpy(fd->block,afr->fr_Drawer);
               AddPart(fd->block,afr->fr_File,fd->blocksize);
               if(file=Open(fd->block,MODE_OLDFILE))
               {  Seek(file,0,OFFSET_END);
               }
               else
               {  file=Open(fd->block,MODE_NEWFILE);
               }
               if(file)
               {  Write(file,buf->buffer,buf->length);
                  Close(file);
               }
            }
            FreeAslRequest(afr);
         }
      }
      result=0;
   }
   return (BOOL)(result==1);
}

/* Connect and transfer the article */
static short Sendarticle(struct Fetchdriver *fd,struct Buffer *buf,UBYTE *host)
{  struct Newsinfo ni={0};
   short retval=PAR_FAIL;
   ni.fd=fd;
   if(Opennews(&ni,TRUE,host))
   {  Updatetaskattrs(AOURL_Netstatus,NWS_WAIT,TAG_END);
      Tcpmessage(fd,TCPMSG_NEWSPOST);
      if(Sendcommand(&ni,"POST")<400)
      {  if(a_send(ni.sock,buf->buffer,buf->length,0,ni.socketbase)==buf->length
         && a_send(ni.sock,".\r\n",3,0,ni.socketbase)==3
         && Getresponse(&ni)<400)
         {  retval=PAR_Ok;
         }
      }
   }
   else retval=PAR_NOCONNECT;
   Closenews(&ni);
   return retval;
}

/* Build result page */
static void Buildresult(struct Fetchdriver *fd)
{  sprintf(fd->block,
      "<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n</HEAD>\n"
      "<BODY>\n%s<p>\n"
      "<FORM ACTION='x-aweb:news/A'>\n<INPUT TYPE=SUBMIT VALUE='%s'>\n</FORM>\n"
      "</BODY>\n</HTML>\n",
      AWEBSTR(MSG_NEWS_ARTICLE_POSTED_TITLE),
      AWEBSTR(MSG_NEWS_ARTICLE_POSTED),
      AWEBSTR(MSG_NEWS_RETURN));
   Updatetaskattrs(
      AOURL_Contenttype,"text/html",
      AOURL_Data,fd->block,
      AOURL_Datalength,strlen(fd->block),
      TAG_END);
}

static void Postarticle(struct Fetchdriver *fd,UBYTE *formdata,UBYTE *host)
{  struct Buffer buf={0};
   short r;
   if(Buildarticle(fd,&buf,formdata))
   {  for(;;)
      {  r=Sendarticle(fd,&buf,host);
         if(r==PAR_Ok) break;
         if(!Postretry(fd,&buf,r)) break;
      }
      if(r==PAR_Ok)
      {  Buildresult(fd);
      }
   }
   Freebuffer(&buf);
}

/*-----------------------------------------------------------------------*/
/* x-aweb:news/B/news?formfields                                         */
/*-----------------------------------------------------------------------*/
/* Catch up. Expects form fields group[&article#] */

#ifndef DEMOVERSION

static void Catchup(struct Fetchdriver *fd,UBYTE *formfields)
{  struct Newsinfo ni={0};
   struct Group *g;
   UBYTE *p;
   long lastfetched=0;
   ni.fd=fd;
   if(p=strchr(formfields,'&')) *p++='\0';
   ObtainSemaphore(&groupsema);
   if(g=Findgroup(&ni,formfields))
   {  if(p) lastfetched=atoi(p);
      else lastfetched=g->lastfetched;
      if(lastfetched>g->lastread)
      {  g->lastread=lastfetched;
         Writesubscribed(&ni);
      }
   }
   ReleaseSemaphore(&groupsema);
   Buildmain(fd);
}

#endif /* DEMOVERSION */

/*-----------------------------------------------------------------------*/
/* x-aweb:news/search?formfields                                         */
/*-----------------------------------------------------------------------*/
/* Search active newsgroups list. Expects form field search= */

/* Obtain the current date in YYMMDD HHMMSS format */
static void Readdate(struct Newsinfo *ni,UBYTE *buffer)
{  if(ni && Sendcommand(ni,"DATE")<400)
   {  strncpy(buffer,ni->buffer+6,6); /* Response is "111 YYYYMMDDhhmmss" */
      buffer[6]=' ';
      strncpy(buffer+7,ni->buffer+12,6);
      buffer[13]='\0';
   }
   else
   {  ULONG today=Today();
      struct DateStamp ds;
      ds.ds_Days=today/86400;
      ds.ds_Minute=(today/60)%1440;
      ds.ds_Tick=(today%60)*TICKS_PER_SECOND;
      Lprintdate(buffer,"%y%m%d% H%M%S",&ds);
   }
}

/* Append groups to file */
static BOOL Appendactive(struct Newsinfo *ni,long file)
{  long len=0; /* Filled up of fd->block so far */
   UBYTE *line,*p;
   BOOL ok=FALSE;
   for(;;)
   {  if(!(line=Nextline(ni))) break;  /* Disconnected */
      if(line[0]=='.' && !line[1])
      {  ok=TRUE;
         break;
      }
      if(p=strchr(line,' ')) *p='\0';
      else p=line+strlen(line);
      if(len+(p-line)+1>ni->fd->blocksize)
      {  Write(file,ni->fd->block,len);
         /* Pass data length only so status is updated */
         Updatetaskattrs(
            AOURL_Datalength,len,
            TAG_END);
         len=0;
      }
      strcpy(ni->fd->block+len,line);
      len+=(p-line);
      ni->fd->block[len++]='\n';
   }
   if(len) Write(file,ni->fd->block,len);
   return ok;
}

/* Download active list. Create multipart response to browser. */
static void Downloadactive(struct Fetchdriver *fd)
{  struct Newsinfo ni={0};
   long file;
   UBYTE datebuf[16];
   BOOL ok=FALSE;
   ni.fd=fd;
   if(Opennews(&ni,FALSE,NULL))
   {  sprintf(fd->block,"<HTML><HEAD><TITLE>%s</TITLE></HEAD>\n"
        "<BODY>%s\n"
        "</BODY></HTML>",
        AWEBSTR(MSG_NEWS_DOWNLOAD_TITLE),
        AWEBSTR(MSG_NEWS_DOWNLOAD_BODY));
      Updatetaskattrs(
         AOURL_Contenttype,"text/html",
         AOURL_Data,fd->block,
         AOURL_Datalength,strlen(fd->block),
         TAG_END);
      Updatetaskattrs(
         AOURL_Eof,TRUE,
         TAG_END);
      if(file=Open(ACTIVENAME,MODE_NEWFILE))
      {  Readdate(&ni,datebuf);
         ObtainSemaphore(fd->prefssema);
         sprintf(fd->block,"Newsgroups at %s %s *** DO NOT EDIT THIS LINE ***\n",
            datebuf,fd->prefs->nntphost);
         ReleaseSemaphore(fd->prefssema);
         Write(file,fd->block,strlen(fd->block));
         if(Sendcommand(&ni,"LIST ACTIVE")<400)
         {  ok=Appendactive(&ni,file);
         }
         Close(file);
         if(!ok)
         {  DeleteFile(ACTIVENAME);
         }
      }
      Updatetaskattrs(
         AOURL_Reload,TRUE,
         TAG_END);
   }
   Closenews(&ni);
}

static int Searchcompare(UBYTE **p1,UBYTE **p2)
{  int c=0;
   if(*p1 && *p2)
   {  c=stricmp(*p1,*p2);
   }
   return c;
}

static void Searchactive(struct Fetchdriver *fd,UBYTE *formfields)
{  struct Newsinfo ni={0};
   struct Buffer buf={0};
   UBYTE *name;
   long file,len,namelen,rem;
   UBYTE datebuf[16];
   UBYTE *p,*q,*r;
   UBYTE *pattern=NULL;
   UBYTE **gtab;
   long nfound,gtablen,i,ntotal,l;
   BOOL first,found;
   ni.fd=fd;
   if(STRNIEQUAL(formfields,"search=",7))
   {  name=formfields+7;
      name[Unescape(name,strlen(name))]='\0';
      if(*name)
      {  namelen=strlen(name);
         if(pattern=ALLOCTYPE(UBYTE,2*namelen+4,MEMF_CLEAR))
         {  if(!ParsePatternNoCase(name,pattern,2*namelen+4))
            {  /* Not a pattern */
               FREE(pattern);
               pattern=NULL;
            }
         }
         if(file=Validactive(fd))
         {  /* Active file exists, update it once a day. fd->block contains first line */
            Readdate(NULL,datebuf);
            if(!STRNEQUAL(datebuf,fd->block+14,6))
            {  /* Not yet updated today */
               if(Opennews(&ni,TRUE,NULL))
               {  Seek(file,0,OFFSET_BEGINNING);
                  Read(file,fd->block,256);
                  /* date at fd->block+14 until fd->block+27 */
                  fd->block[27]='\0';
                  if(Sendcommand(&ni,"NEWGROUPS %s",fd->block+14)<400)
                  {  /* Append new groups to active file */
                     Seek(file,0,OFFSET_END);
                     if(Appendactive(&ni,file))
                     {  Seek(file,14,OFFSET_BEGINNING);
                        Readdate(&ni,datebuf);
                        Write(file,datebuf,13);
                     }
                  }
               }
               Closenews(&ni);
            }
         }
         else
         {  /* Build new active file */
            Downloadactive(fd);
            file=Validactive(fd);
         }
         if(file)
         {  /* Read and search the active file */
            Seek(file,0,OFFSET_BEGINNING);
            rem=0;   /* Remainder from last time */
            first=TRUE;
            nfound=0;
            ntotal=0;
            gtab=NULL;
            gtablen=0;
            for(;;)
            {  len=Read(file,fd->block+rem,fd->blocksize-rem-1);
               if(len<=0) break;
               fd->block[rem+len]='\0';
               p=fd->block;
               if(first)
               {  /* Skip first line */
                  if(q=strchr(p,'\n')) p=q+1;
                  first=FALSE;
               }
               while(q=strchr(p,'\n'))
               {  *q='\0';
                  found=FALSE;
                  if(pattern)
                  {  found=MatchPatternNoCase(pattern,p);
                  }
                  else
                  {  for(r=p;r<=q-namelen;r++)
                     {  if(STRNIEQUAL(r,name,namelen))
                        {  found=TRUE;
                           break;
                        }
                     }
                  }
                  if(found)
                  {  if(nfound<200)
                     {  if(nfound>=gtablen)
                        {  UBYTE **newtab;
                           if(newtab=ALLOCTYPE(UBYTE *,gtablen+100,MEMF_CLEAR))
                           {  if(gtab)
                              {  memmove(newtab,gtab,gtablen*sizeof(UBYTE *));
                                 FREE(gtab);
                              }
                              gtab=newtab;
                              gtablen+=100;
                           }
                        }
                        if(nfound<gtablen)
                        {  gtab[nfound++]=Dupstr(p,-1);
                        }
                     }
                     ntotal++;
                  }
                  p=q+1;
                  if(Checktaskbreak()) break;
               }
               rem=fd->block+rem+len-p;
               if(rem>0)
               {  memmove(fd->block,p,rem);
               }
               if(Checktaskbreak()) break;
            }
            Close(file);
            if(gtab)
            {  qsort(gtab,nfound,sizeof(UBYTE *),Searchcompare);
            }
            Bprintf(&buf,fd->block,"<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n</HEAD>\n",
               AWEBSTR(MSG_NEWS_SEARCH_RESULT));
            Bprintf(&buf,fd->block,"<BODY>\n<H1>%s</H1>\n",
               AWEBSTR(MSG_NEWS_SEARCH_RESULT));
            Bprintf(&buf,fd->block,"(<A HREF='news:'>%s</A>)<BR>\n",
               AWEBSTR(MSG_NEWS_GROUP_LIST));
            Bprintf(&buf,fd->block,"<FORM>\n<INPUT NAME='search' SIZE=30 VALUE='");
            Addblock(&buf,name,-1);
            Bprintf(&buf,fd->block,"'>\n<INPUT TYPE=SUBMIT VALUE='%s'>\n</FORM>\n<BR>\n<BR>\n",
               AWEBSTR(MSG_NEWS_SEARCH));
            /* First convert the search argument to esacaped HTML: */
            l=buf.length;
            Addblock(&buf,name,-1);
            p=Dupstr(buf.buffer+l,-1);
            buf.length=l;
            if(nfound<ntotal)
            {  Lprintf(fd->block,AWEBSTR(MSG_NEWS_FIRST_MATCHING),
                  nfound,ntotal,p?p:NULLSTRING);
               Addbhtml(&buf,fd->block,-1);
            }
            else
            {  Lprintf(fd->block,AWEBSTR(MSG_NEWS_FOUND_MATCHING),
                  nfound,p?p:NULLSTRING);
               Addbhtml(&buf,fd->block,-1);
            }
            if(p) FREE(p);
            Bprintf(&buf,fd->block,":\n<BR>\n<BR>\n<PRE>\n");
            for(i=0;i<nfound;i++)
            {  if(gtab[i])
               {  l=strlen(gtab[i]);
                  Bprintf(&buf,fd->block,"<A HREF='news:%s'>%s</A>%*s",
                     gtab[i],gtab[i],MAX(1,50-l),"");
#ifndef DEMOVERSION
                  ObtainSemaphore(&groupsema);
                  if(!Findgroup(&ni,gtab[i]))
                  {  Bprintf(&buf,fd->block,"(<A HREF='x-aweb:news/C/%s'>%s</A>)",
                        gtab[i],AWEBSTR(MSG_NEWS_SUBSCRIBE));
                  }
                  ReleaseSemaphore(&groupsema);
#endif
                  Bprintf(&buf,fd->block,"\n");
                  FREE(gtab[i]);
               }
               if(buf.length>512)
               {  Updatetaskattrs(
                     AOURL_Data,buf.buffer,
                     AOURL_Datalength,buf.length,
                     TAG_END);
                  buf.length=0;
               }
               if(Checktaskbreak()) break;
            }
            Bprintf(&buf,fd->block,"</PRE>\n<BR>\n(<A HREF='news:'>%s</A>)<BR>\n",
               AWEBSTR(MSG_NEWS_GROUP_LIST));
            Bprintf(&buf,fd->block,"</BODY>\n</HTML>\n");
            Updatetaskattrs(
               AOURL_Data,buf.buffer,
               AOURL_Datalength,buf.length,
               TAG_END);
            FREE(gtab);
            Freebuffer(&buf);
         }
         if(pattern) FREE(pattern);
      }
   }
}

/*-----------------------------------------------------------------------*/
/* x-aweb:news/D/article@id                                              */
/* x-aweb:news/D/host/group/artnr                                        */
/*-----------------------------------------------------------------------*/
/* Save article */

#ifndef DEMOVERSION

static void Savearticle(struct Fetchdriver *fd,UBYTE *id)
{  struct Newsinfo ni={0};
   struct Buffer buf={0};
   UBYTE *line;
   UBYTE *p,*host=NULL,*group=NULL,*artnr=NULL;
   long result;
   if(!strchr(id,'@'))     /* NNTP: host/group/artnr */
   {  host=Dupstr(id,-1);
      if(p=strchr(host,'/'))
      {  *p='\0';
         group=p+1;
         if(p=strchr(group,'/'))
         {  *p='\0';
            artnr=p+1;
         }
      }
   }
   ni.fd=fd;
   Updatetaskattrs(
      AOURL_Contenttype,"application/octet-stream",
      TAG_END);
   if(Opennews(&ni,FALSE,host))
   {  if(artnr)
      {  if(Sendcommand(&ni,"GROUP %s",group)<400)
         {  result=Sendcommand(&ni,"ARTICLE %s",artnr);
         }
      }
      else
      {  result=Sendcommand(&ni,"ARTICLE <%s>",id);
      }
      if(result<400)
      {  while(line=Nextline(&ni))
         {  if(line[0]=='.')
            {  if(!line[1]) break;
               else line++;
            }
            Addbhtml(&buf,line,-1);
            Addbhtml(&buf,"\n",1);
            if(buf.length>512)
            {  Updatetaskattrs(
                  AOURL_Data,buf.buffer,
                  AOURL_Datalength,buf.length,
                  TAG_END);
               buf.length=0;
            }
         }
      }
   }
   Updatetaskattrs(
      AOURL_Data,buf.buffer,
      AOURL_Datalength,buf.length,
      TAG_END);
   Closenews(&ni);
   Freebuffer(&buf);
}

#endif /* DEMOVERSION */

/*-----------------------------------------------------------------------*/

__saveds __asm void Fetchdrivertask(register __a0 struct Fetchdriver *fd)
{  if(STRNIEQUAL(fd->name,"nntp://",7) || STRNIEQUAL(fd->name,"news://",7))
   {  UBYTE *p=strchr(fd->name+7,'/');
      if(p)
      {  if(strchr(p+1,'/'))
         {  Readarticle(fd,fd->name+7);
         }
         else
         {  Readgroup(fd,fd->name+7);
         }
      }
   }
   else if(STRNIEQUAL(fd->name,"news:",5))
   {  if(fd->name[5])
      {  if(strchr(fd->name+5,'@'))
         {  Readarticle(fd,fd->name+5);
         }
         else
         {  Readgroup(fd,fd->name+5);
         }
      }
      else
      {  Buildmain(fd);
      }
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:news/2?",14))
   {  Subscribeorread(fd,fd->name+14);
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:news/3?",14))
   {  Postnewarticle(fd,fd->name+14);
   }
#ifndef DEMOVERSION
   else if(STRNIEQUAL(fd->name,"x-aweb:news/4/news?",19))
   {  Subscribe(fd,fd->name+19,TRUE);
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:news/5/news?",19))
   {  Unsubscribe(fd,fd->name+19);
   }
#endif
   else if(STRNIEQUAL(fd->name,"x-aweb:news/6/",14))
   {  Readgroupindex(fd,fd->name+14,TRUE);
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:news/followup?",21))
   {  Followuptoarticle(fd,fd->name+21);
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:news/reply?",18))
   {  Replytoarticle(fd,fd->name+18);
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:news/post",16))
   {  if(Findreturn(fd->postmsg))
      {  Updatetaskattrs(
            AOURL_Gohistory,-1,
            TAG_END);
      }
      else
      {  if(fd->referer &&
            (STRNIEQUAL(fd->referer,"x-aweb:news/followup?",21)
            || STRNIEQUAL(fd->referer,"x-aweb:news/3?",14)))
         {  Postarticle(fd,fd->postmsg,(fd->name[16]=='?')?fd->name+17:NULL);
         }
      }
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:news/A?",14))
   {  Updatetaskattrs(
         AOURL_Gohistory,-2,
         TAG_END);
   }
#ifndef DEMOVERSION
   else if(STRNIEQUAL(fd->name,"x-aweb:news/B/news?",19))
   {  Catchup(fd,fd->name+19);
   }
#endif
   else if(STRNIEQUAL(fd->name,"x-aweb:news/search?",19))
   {  Searchactive(fd,fd->name+19);
   }
#ifndef DEMOVERSION
   else if(STRNIEQUAL(fd->name,"x-aweb:news/C/",14))
   {  Subscribe(fd,fd->name+14,FALSE);
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:news/D/",14))
   {  Savearticle(fd,fd->name+14);
   }
#endif
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOURL_Eof,TRUE,
      AOURL_Terminate,TRUE,
      TAG_END);
}

/*-----------------------------------------------------------------------*/

static ULONG Initaweblib(struct Library *libbase)
{  if(!(DOSBase=(struct DosLibrary *)OpenLibrary("dos.library",39))) return FALSE;
   if(!(AslBase=OpenLibrary("asl.library",OSNEED(39,44)))) return FALSE;
   if(!(GfxBase=(struct GfxBase *)OpenLibrary("graphics.library",39))) return FALSE;
   if(!(UtilityBase=OpenLibrary("utility.library",39))) return FALSE;
   InitSemaphore(&groupsema);
   return TRUE;
}

static void Expungeaweblib(struct Library *libbase)
{  struct Group *g;
   if(UtilityBase) CloseLibrary(UtilityBase);
   if(GfxBase) CloseLibrary(GfxBase);
   if(AslBase) CloseLibrary(AslBase);
   if(DOSBase) CloseLibrary(DOSBase);
   if(groups.first)
   {  while(g=REMHEAD(&groups)) Freegroup(g);
   }
}

#endif /* !LOCALONLY */

