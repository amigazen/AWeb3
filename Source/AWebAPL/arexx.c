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

/* arexx.c - AWeb arexx interface */

#include "aweb.h"
#include "arexx.h"
#include "simplerexx.h"
#include "application.h"
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/arexx.h>

/* ARexx command templates */

struct Arexxtemplate
{  ULONG command;          /* internal command id */
   UBYTE *keyword;         /* ARexx keyword */
   UBYTE *templ;           /* command template */
};

static struct Arexxtemplate templates[]=
{  ARX_ACTIVATEWINDOW,  "ACTIVATEWINDOW",    "",
   ARX_ADDHOTLIST,      "ADDHOTLIST",        "URL,TITLE,TARGET/K,GROUP/K",
   ARX_ALLOWCMD,        "ALLOWCMD",          "",
   ARX_BACKGROUND,      "BACKGROUND",        "ON/S,OFF/S",
   ARX_BGSOUND,         "BGSOUND",           "ON/S,OFF/S",
   ARX_CANCEL,          "CANCEL",            "LOADID/N,ALL/S",
   ARX_CHANCLOSE,       "CHANCLOSE",         "CHANNEL/A",
   ARX_CHANDATA,        "CHANDATA",          "CHANNEL/A,DATA/A,NL=NEWLINE/S",
   ARX_CHANHEADER,      "CHANHEADER",        "CHANNEL/A,HEADER/A",
   ARX_CHANOPEN,        "CHANOPEN",          "URL/A",
   ARX_CLEARSELECTION,  "CLEARSELECTION",    "",
   ARX_CLOSE,           "CLOSE",             "FORCE/S",
   ARX_COPYBLOCK,       "COPYBLOCK",         "",
   ARX_COPYURL,         "COPYURL",           "TARGET/K",
   ARX_DELETECACHE,     "DELETECACHE",       "IMAGES/S,DOCUMENTS/S,FORCE/S,PAT=PATTERN/K",
   ARX_DRAGGING,        "DRAGGING",          "ON/S,OFF/S",
   ARX_EDITSOURCE,      "EDITSOURCE",        "URL/A",
   ARX_FIXCACHE,        "FIXCACHE",          "FORCE/S",
   ARX_FLUSHCACHE,      "FLUSHCACHE",        "IMAGES/S,DOCUMENTS/S,ALL/S,URL/K",
   ARX_FOCUS,           "FOCUS",             "TARGET/K",
   ARX_GET,             "GET",               "ITEM/A,TARGET/K,VAR/K,STEM/K,PAT=PATTERN/K,ALL/S",
   ARX_GETCFG,          "GETCFG",            "ITEM,VAR/K,STEM/K",
   ARX_GETHISTORY,      "GETHISTORY",        "WINDOW/N,MAINLINE/S,STEM/AK",
   ARX_GO,              "GO",                "N/N,BACK/S,FWD=FORWARD/S,HOME/S",
   ARX_HOTLIST,         "HOTLIST",           "SET/K,SAVE/S,RESTORE/S",
#ifdef BETAKEYFILE
   ARX_HTTPDEBUG,       "HTTPDEBUG",         "ON/S",
#endif
   ARX_HTTPGET,         "HTTPGET",           "URL/A,STEM/AK,HEADERS/S,TIMEOUT/K/N",
   ARX_ICONIFY,         "ICONIFY",           "HIDE/S,SHOW/S",
   ARX_IMAGELOADING,    "IMAGELOADING",      "OFF/S,MAPS/S,ALL/S",
   ARX_INFO,            "INFO",              "TARGET/K",
   ARX_JAVASCRIPT,      "JAVASCRIPT",        "SOURCE,FILE/K,TARGET/K,VAR/K",
   ARX_JSBREAK,         "JSBREAK",           "",
   ARX_JSDEBUG,         "JSDEBUG",           "ON/S,OFF/S",
   ARX_JSONGET,         "JSONGET",           "DATA/A,PATH/A,VAR/K,STEM/K",
   ARX_LOAD,            "LOAD",              "URL/A,SAVEAS/K,RELOAD/S,APPEND/S,"
                                                "SAVEREQ/S,NOICON/S,POST/K",
   ARX_LOADIMAGES,      "LOADIMAGES",        "TARGET/K,MAPS/S,RESTRICT/S",
   ARX_LOADSETTINGS,    "LOADSETTINGS",      "PATH,REQUEST/S",
   ARX_MIMETYPE,        "MIMETYPE",          "NAME/A",
#ifndef DEMOVERSION
   ARX_NEW,             "NEW",               "URL/A,NAME/K,RELOAD/S,POST/K,SMART/S",
#endif
   ARX_OPEN,            "OPEN",              "URL/A,TARGET/K,RELOAD/S,POST/K,SMART/S",
   ARX_OPENREQ,         "OPENREQ",           "FILE/S,PAT=PATTERN/K",
   ARX_PLAYBGSOUND,     "PLAYBGSOUND",       "",
   ARX_PLUGIN,          "PLUGIN",            "PLUGIN/A,COMMAND/A",
   ARX_PRINT,           "PRINT",             "SCALE/N/K,CENTER/S,NOFF=NOFORMFEED/S,"
                                                "NOBG=NOBACKGROUND/S,WAIT/S,REQUEST/S,DEBUG/S",
   ARX_QUIT,            "QUIT",              "FORCE/S",
   ARX_RELOAD,          "RELOAD",            "TARGET/K,IMAGES/S",
   ARX_REQUEST,         "REQUEST",           "TITLE/A,BODY/A,GADGETS/A,NOWAIT/S",
   ARX_REQUESTFILE,     "REQUESTFILE",       "TITLE/A,FILE/K,PATTERN/K,SAVEMODE/S,DIRSONLY/S",
   ARX_REQUESTSTRING,   "REQUESTSTRING",     "TITLE/A,BODY/A,GADGETS/A,DEFAULT",
   ARX_RESETFRAME,      "RESETFRAME",        "TARGET/K",
#ifndef NOAREXXPORTS
   ARX_RUN,             "RUN",               "NAME/A/F",
#endif
   ARX_SAVEAS,          "SAVEAS",            "NAME,APPEND/S,TARGET/K,NOICON/S",
   ARX_SAVEAUTHORIZE,   "SAVEAUTHORIZE",     "",
   ARX_SAVEIFF,         "SAVEIFF",           "NAME,NOICON/S,WAIT/S",
   ARX_SAVESETTINGS,    "SAVESETTINGS",      "PATH,REQUEST/S",
   ARX_SCREENTOBACK,    "SCREENTOBACK",      "",
   ARX_SCREENTOFRONT,   "SCREENTOFRONT",     "",
   ARX_SCROLL,          "SCROLL",            "N/N,UP/S,DOWN/S,LEFT/S,RIGHT/S,PAGE/S,FAR/S,"
                                                "TARGET/K",
   ARX_SEARCH,          "SEARCH",            "TARGET/K",
   ARX_SETCFG,          "SETCFG",            "ITEM,VALUE,STEM/K,ADD/S",
   ARX_SETCLIP,         "SETCLIP",           "VALUE/A/F",
   ARX_SETCOOKIES,      "SETCOOKIES",        "STEM/A/K,ADD/S",
   ARX_SNAPSHOT,        "SNAPSHOT",          "",
   ARX_STATUSFIELD,     "STATUSFIELD",       "SET/A",
   ARX_SUBWINDOW,       "SUBWINDOW",         "TYPE/A,OPEN/S,CLOSE/S",
   ARX_SYSTEM,          "SYSTEM",            "COMMAND/A,ARGUMENTS/F",
   ARX_URLENCODE,       "URLENCODE",         "STRING/A,VAR/K",
   ARX_URLFIELD,        "URLFIELD",          "ACTIVATE/S,POS/N/K,SET/K,PASTE/S",
   ARX_USEPROXY,        "USEPROXY",          "ENABLE/S,DISABLE/S",
   ARX_VIEWSOURCE,      "VIEWSOURCE",        "URL/A",
   ARX_WAIT,            "WAIT",              "URL,DOC=DOCUMENT/S,IMG=IMAGES/S,ALL/S",
   ARX_WINDOW,          "WINDOW",            "RECT/K,ACTIVATE/S,TOFRONT/S,TOBACK/S,ZIP/S,"
                                                "NEXT/K/N",
   ARX_WINDOWTOBACK,    "WINDOWTOBACK",      "",
   ARX_WINDOWTOFRONT,   "WINDOWTOFRONT",     "",
};
#define NRTEMPLATES (sizeof(templates)/sizeof(struct Arexxtemplate))

#define STDOUTNAME "CON:////AWeb ARexx/AUTO/CLOSE/WAIT"

static short commonsigbit=-1;

struct Arexxport
{  NODE(Arexxport);
   AREXXCONTEXT context;
   ULONG windowkey;
   ULONG mask;
   long portnr;
};

static LIST(Arexxport) ports;

/*-----------------------------------------------------------------------*/

#ifndef NOAREXXPORTS
static struct Arexxport *Newarexxport(LONG windowkey)
{  struct Arexxport *ap=ALLOCSTRUCT(Arexxport,1,MEMF_CLEAR);
   if(ap)
   {  ADDTAIL(&ports,ap);
#ifdef LOCALONLY
      ap->context=InitARexx("AWebView","awebrx");
#else
      ap->context=InitARexx("AWeb","awebrx");
#endif
      ap->windowkey=windowkey;
      ap->mask=ARexxSignal(ap->context);
   }
   return ap;
}

static void Freearexxport(struct Arexxport *ap)
{  FreeARexx(ap->context);
   FREE(ap);
}

static struct Arexxport *Findarexxport(ULONG windowkey)
{  struct Arexxport *ap;
   for(ap=ports.first;ap->next;ap=ap->next)
   {  if(ap->windowkey==windowkey) return ap;
   }
   return NULL;
}
#endif /* !NOAREXXPORTS */

/* returns TRUE if done with this command */
static BOOL Parsecommand(ULONG windowkey,UBYTE *cmd,struct Arexxcmd *ac)
{  UBYTE *buf=Dupstr(cmd,strlen(cmd)+2);
   UBYTE *p,*q;
   BOOL done=TRUE;
   long a=0,b=NRTEMPLATES-1,m,c;
   struct Arexxtemplate *tp=NULL;
   ac->errorlevel=RXERR_FATAL;
   if(buf)
   {  for(p=buf;*p==' ';p++);
      for(q=p;*q && *q!=' ';q++);
      if(*q) *q++='\0';
      while(a<=b)
      {  m=(a+b)/2;
         c=stricmp(templates[m].keyword,p);
         if(c==0)
         {  tp=&templates[m];
            break;
         }
         if(c<0) a=m+1;
         else b=m-1;
      }
      if(tp)
      {  struct RDArgs *rda,*rdb;
         short i;
         if(rda=AllocDosObject(DOS_RDARGS,TAG_END))
         {  strcat(q,"\n");
            rda->CS_Buffer=q;
            rda->CS_Length=strlen(q);
            rda->RDA_Flags=RDAF_NOPROMPT;
            for(i=0;i<10;i++) ac->parameter[i]=0;
            ac->result=NULL;
            ac->errorlevel=0;
            ac->varname=NULL;
            if(rdb=ReadArgs(tp->templ,(long *)ac->parameter,rda))
            {  ac->command=tp->command;
               ac->windowkey=windowkey;
               done=Doarexxcmd(ac);
               FreeArgs(rdb);
            }
            else ac->errorlevel=RXERR_INVARGS;
            FreeDosObject(DOS_RDARGS,rda);
         }
      }
      else ac->errorlevel=RXERR_INVCMD;
      FREE(buf);
   }
   return done;
}

static void Processarexx(void)
{  struct Arexxport *ap;
   struct RexxMsg *msg;
   struct Arexxcmd *ac;
   for(ap=ports.first;ap->next;ap=ap->next)
   {  while(msg=GetARexxMsg(ap->context))
      {  if(msg!=REXX_RETURN_ERROR)
         {  if(ac=ALLOCSTRUCT(Arexxcmd,1,MEMF_PUBLIC|MEMF_CLEAR))
            {  ac->msg=msg;
               ac->port=ap;
               ac->flags=ARXCF_TRUEREXX|ARXCF_ALLOWGET|ARXCF_ALLOWSTEM;
               if(Parsecommand(ap->windowkey,ARG0(msg),ac))
               {  Replyarexxcmd(ac);
               }
            }
         }
      }
   }
}

static void Processarexxreply(void)
{  GetAWebARexxMsg();
}

/*-----------------------------------------------------------------------*/

BOOL Initarexx(void)
{  ULONG rbit;
   NEWLIST(&ports);
#ifndef NOAREXXPORTS
   if((commonsigbit=AllocSignal(-1))<0) return FALSE;
   Setprocessfun(commonsigbit,Processarexx);
   rbit=InitAWebARexx();
   if(!rbit) return FALSE;
   Setprocessfun(rbit,Processarexxreply);
#endif
   return TRUE;
}

void Freearexx(void)
{  struct Arexxport *ap;
#ifndef NOAREXXPORTS
   if(ports.first)
   {  while(ap=REMHEAD(&ports)) Freearexxport(ap);
   }
   FreeAWebARexx();
   if(commonsigbit>=0)
   {  Setprocessfun(commonsigbit,NULL);
      FreeSignal(commonsigbit);
   }
#endif
}

void Replyarexxcmd(struct Arexxcmd *ac)
{  if(ac->flags&ARXCF_TRUEREXX)
   {  if(ac->varname && ac->result)
      {  long len=strlen(ac->result);
         if(len>65535) len=65535;
         SetRexxVar(ac->msg,ac->varname,ac->result,len);
         ReplyARexxMsg(ac->port->context,ac->msg,NULL,ac->errorlevel);
      }
      else
      {  ReplyARexxMsg(ac->port->context,ac->msg,ac->result,ac->errorlevel);
      }
   }
   if(ac->result)
   {  FREE(ac->result);
      ac->result=NULL;
   }
   if(ac->varname)
   {  FREE(ac->varname);
      ac->varname=NULL;
   }
   FREE(ac);
}

UBYTE *Openarexxport(ULONG windowkey)
{  struct Arexxport *ap;
   UBYTE *portname=NULL,*p;
#ifndef NOAREXXPORTS
   if(ap=Newarexxport(windowkey))
   {  portname=ARexxName(ap->context);
      if(portname && (p=strchr(portname,'.')))
      {  ap->portnr=atoi(p+1);
      }
   }
#endif
   return portname;
}

void Closearexxport(ULONG windowkey)
{  
#ifndef NOAREXXPORTS
   struct Arexxport *ap;
   if(ap=Findarexxport(windowkey))
   {  REMOVE(ap);
      Freearexxport(ap);
   }
#endif
}

short Arexxportnr(ULONG windowkey)
{  struct Arexxport *ap;
   short portnr=0;
#ifndef NOAREXXPORTS
   if(ap=Findarexxport(windowkey))
   {  portnr=GetARexxPortNumber(ap->context);
   }
#endif
   return portnr;
}

void Sendarexxcmd(ULONG windowkey,UBYTE *cmd)
{  
#ifndef NOAREXXPORTS
   struct Arexxport *ap;
   if(cmd && (ap=Findarexxport(windowkey)))
   {  if(ap->context)
      {  SendARexxMsg(ap->context,cmd,FALSE);
      }
   }
#endif
}

void Setstemvar(struct Arexxcmd *ac,UBYTE *stem,long index,UBYTE *field,UBYTE *value)
{  UBYTE *buf,*p;
   long len;
   if(ac->flags&ARXCF_TRUEREXX)
   {  len=strlen(stem)+10;
      if(field) len+=strlen(field)+1;
      if(buf=ALLOCTYPE(UBYTE,len,0))
      {  p=buf+sprintf(buf,"%s.%d",stem,index);
         if(field)
         {  sprintf(p,".%s",field);
         }
         for(p=buf;*p;p++) *p=toupper(*p);
         len=strlen(value);
         SetRexxVar(ac->msg,buf,value,MIN(len,65535));
         FREE(buf);
      }
   }
}

UBYTE *Getstemvar(struct Arexxcmd *ac,UBYTE *stem,long index,UBYTE *field)
{  UBYTE *buf,*p,*value=NULL;
   long len;
   if(ac->flags&ARXCF_TRUEREXX)
   {  len=strlen(stem)+10;
      if(field) len+=strlen(field)+1;
      if(buf=ALLOCTYPE(UBYTE,len,0))
      {  p=buf+sprintf(buf,"%s.%d",stem,index);
         if(field)
         {  sprintf(p,".%s",field);
         }
         for(p=buf;*p;p++) *p=toupper(*p);
         if(GetRexxVar(ac->msg,buf,&value)) value=NULL;
         FREE(buf);
      }
   }
   return value;
}

void Execarexxcmd(ULONG windowkey,UBYTE *cmd,UBYTE *argspec,...)
{  long len;
   UBYTE *buffer;
   UBYTE **params=VARARG(argspec);
   struct Arexxcmd ac={0};
   len=Pformatlength(cmd,argspec,params)+4;
   if(buffer=ALLOCTYPE(UBYTE,len,0))
   {  Pformat(buffer,cmd,argspec,params,TRUE);
      ac.flags=0;
      Parsecommand(windowkey,buffer,&ac);
      if(ac.result) FREE(ac.result);
      if(ac.varname) FREE(ac.varname);
      FREE(buffer);
   }
}

long Arexxportnumber(ULONG windowkey)
{  long portnr=0;
#ifndef NOAREXXPORTS
   struct Arexxport *ap=Findarexxport(windowkey);
   if(ap) portnr=ap->portnr;
#endif
   return portnr;
}

long Supportarexxcmd(long portnr,UBYTE *cmd,UBYTE *resultbuf,long length)
{  struct Arexxcmd ac={0};
   struct Arexxport *ap;
   ULONG windowkey=0;
   for(ap=ports.first;ap->next;ap=ap->next)
   {  if(ap->portnr==portnr)
      {  windowkey=ap->windowkey;
         break;
      }
   }
   ac.flags=ARXCF_ALLOWGET;
   Parsecommand(windowkey,cmd,&ac);
   if(resultbuf)
   {  if(ac.result)
      {  strncpy(resultbuf,ac.result,length-1);
      }
      else
      {  *resultbuf='\0';
      }
   }
   if(ac.result) FREE(ac.result);
   if(ac.varname) FREE(ac.varname);
   return (long)ac.errorlevel;
}

/*------------------------------------------------------------------------*/
/* Amiga library replacements. Create message port with shared signal bit */
/* Used only in simplerexx.c */

struct MsgPort *Createarexxport(char *name,long pri)
{  struct MsgPort *port;
   if(!(port=ALLOCSTRUCT(MsgPort,1,MEMF_CLEAR|MEMF_PUBLIC))) return NULL;
   port->ln_Name=name;
   port->ln_Pri=pri;
   port->ln_Type=NT_MSGPORT;
   port->mp_Flags=PA_SIGNAL;
   port->mp_SigBit=commonsigbit;
   port->mp_SigTask=FindTask(NULL);
   if(name)
   {  AddPort(port);
   }
   else
   {  NewList(&port->mp_MsgList);
   }
   return port;
}

void Deletearexxport(struct MsgPort *port)
{  if(port->ln_Name) RemPort(port);
   FREE(port);
}

