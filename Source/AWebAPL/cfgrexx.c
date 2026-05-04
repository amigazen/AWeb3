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

/* cfgrexx.c - AWeb ARexx GETCFG and SETCFG AWebLib module */

#include "aweblib.h"
#include "arexx.h"
#include <exec/resident.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/graphics.h>

static void *AwebPluginBase;
/* Library base pointers are provided by proto headers:
   DOSBase - from proto/dos.h
   UtilityBase - from proto/utility.h  
   GfxBase - from proto/graphics.h
*/

struct Iteminfo
{  UBYTE *name;
   UBYTE *sub;
   USHORT type;
   long offset;
   long max;
};

enum ITEM_TYPES
{  IT_BOOL=1,IT_NBOOL,IT_SHORT,IT_LONG,IT_NSTRING,IT_STRING,IT_XLONG,
   IT_RGB,IT_CMDARGS,IT_PENS,IT_FONT,IT_STYLE,IT_POPUP,
   
   IT_UNNAMEDGROUP,
   IT_MENU,IT_BUTTON,IT_PUPMENU,IT_KEY,IT_ALIAS,IT_MIME,IT_NOCACHE,IT_NOPROXY,IT_NOCOOKIE,
};

#define OFFSET(s,e) (long)(&((struct s *)NULL)->e)

#ifndef NOAREXXPORTS
static struct Iteminfo iteminfo[]=
{
   /* Offsets relative to bundled struct Prefs from awebprefs.h */
   "ALLOWCMD",          NULL,    IT_BOOL,    OFFSET(Prefs,program.commands),0,
   "ANONYMOUS",         NULL,    IT_NBOOL,   OFFSET(Prefs,network.referer),0,
#ifndef DEMOVERSION
   "AUTOSEARCH",        NULL,    IT_BOOL,    OFFSET(Prefs,network.autosearch),0,
   "AUTOSEARCHURL",     NULL,    IT_STRING,  OFFSET(Prefs,network.searchurl),0,
#endif
   "BACKGROUND",        NULL,    IT_BOOL,    OFFSET(Prefs,browser.docolors),0,
   "BGSOUND",           NULL,    IT_BOOL,    OFFSET(Prefs,browser.dobgsound),0,
   "BLINK",             NULL,    IT_SHORT,   OFFSET(Prefs,browser.blinkrate),40,
   "BUTTON",            NULL,    IT_BUTTON,  OFFSET(Prefs,gui.buttons),0,
   "CACHEDISK",         NULL,    IT_LONG,    OFFSET(Prefs,network.cadisksize),0,
   "CACHEMEMORY",       NULL,    IT_LONG,    OFFSET(Prefs,network.camemsize),0,
   "CACHEPATH",         NULL,    IT_STRING,  OFFSET(Prefs,network.cachepath),0,
   "CENTERREQS",        NULL,    IT_BOOL,    OFFSET(Prefs,program.centerreq),0,
   "COLOUR",            "BACKGROUND",IT_RGB, OFFSET(Prefs,browser.background),0,
   "COLOUR",            "TEXT",  IT_RGB,     OFFSET(Prefs,browser.text),0,
   "COLOUR",            "LINK",  IT_RGB,     OFFSET(Prefs,browser.newlink),0,
   "COLOUR",            "VLINK", IT_RGB,     OFFSET(Prefs,browser.oldlink),0,
   "COLOUR",            "ALINK", IT_RGB,     OFFSET(Prefs,browser.selectlink),0,
   "CONSOLE",           NULL,    IT_STRING,  OFFSET(Prefs,program.console),0,
   "CONTANIM",          NULL,    IT_BOOL,    OFFSET(Prefs,network.contanim),0,
   "COOKIES",           NULL,    IT_SHORT,   OFFSET(Prefs,network.cookies),2,
#ifndef DEMOVERSION
   "DRAGGING",          NULL,    IT_BOOL,    OFFSET(Prefs,program.clipdrag),0,
#endif
   "EDITOR",            NULL,    IT_CMDARGS, OFFSET(Prefs,program.editcmd),0,
#ifndef DEMOVERSION
   "ENDTCP",            NULL,    IT_CMDARGS, OFFSET(Prefs,network.endtcpcmd),0,
#endif
   "FASTRESPONSE",      NULL,    IT_BOOL,    OFFSET(Prefs,network.fastresponse),0,
   "FONT",              "N1",    IT_FONT,    OFFSET(Prefs,browser.font[0][0]),0,
   "FONT",              "N2",    IT_FONT,    OFFSET(Prefs,browser.font[0][1]),0,
   "FONT",              "N3",    IT_FONT,    OFFSET(Prefs,browser.font[0][2]),0,
   "FONT",              "N4",    IT_FONT,    OFFSET(Prefs,browser.font[0][3]),0,
   "FONT",              "N5",    IT_FONT,    OFFSET(Prefs,browser.font[0][4]),0,
   "FONT",              "N6",    IT_FONT,    OFFSET(Prefs,browser.font[0][5]),0,
   "FONT",              "N7",    IT_FONT,    OFFSET(Prefs,browser.font[0][6]),0,
   "FONT",              "F1",    IT_FONT,    OFFSET(Prefs,browser.font[1][0]),0,
   "FONT",              "F2",    IT_FONT,    OFFSET(Prefs,browser.font[1][1]),0,
   "FONT",              "F3",    IT_FONT,    OFFSET(Prefs,browser.font[1][2]),0,
   "FONT",              "F4",    IT_FONT,    OFFSET(Prefs,browser.font[1][3]),0,
   "FONT",              "F5",    IT_FONT,    OFFSET(Prefs,browser.font[1][4]),0,
   "FONT",              "F6",    IT_FONT,    OFFSET(Prefs,browser.font[1][5]),0,
   "FONT",              "F7",    IT_FONT,    OFFSET(Prefs,browser.font[1][6]),0,
   "FONTALIAS",         NULL,    IT_ALIAS,   OFFSET(Prefs,browser.aliaslist),0,
#ifndef DEMOVERSION
   "FORMWARNING",       NULL,    IT_BOOL,    OFFSET(Prefs,network.formwarn),0,
#endif
   "FRAMES",            NULL,    IT_BOOL,    OFFSET(Prefs,browser.doframes),0,
   "FTPEMAILADDR",      NULL,    IT_BOOL,    OFFSET(Prefs,network.ftpemailaddr),0,
   "FTPPROXY",          NULL,    IT_NSTRING, OFFSET(Prefs,network.ftpproxy),0,
   "GOPHERPROXY",       NULL,    IT_NSTRING, OFFSET(Prefs,network.gopherproxy),0,
   "HANDPOINTER",       NULL,    IT_BOOL,    OFFSET(Prefs,browser.handpointer),0,
   "HISTORYCLOSE",      NULL,    IT_BOOL,    OFFSET(Prefs,program.whautoclose),0,
   "HISTORYOPEN",       NULL,    IT_BOOL,    OFFSET(Prefs,program.aawinhis),0,
   "HOMEURL",           NULL,    IT_STRING,  OFFSET(Prefs,network.homeurl),0,
   "HOTLISTCLOSE",      NULL,    IT_BOOL,    OFFSET(Prefs,program.hlautoclose),0,
   "HOTLISTOPEN",       NULL,    IT_BOOL,    OFFSET(Prefs,program.aahotlist),0,
   "HOTLISTREQUESTER",  NULL,    IT_BOOL,    OFFSET(Prefs,program.hlrequester),0,
   "HTMLMODE",          NULL,    IT_SHORT,   OFFSET(Prefs,browser.htmlmode),2,
   "HTMLVIEWER",        NULL,    IT_CMDARGS, OFFSET(Prefs,program.viewcmd),0,
   "HTTPPROXY",         NULL,    IT_NSTRING, OFFSET(Prefs,network.httpproxy),0,
#ifndef DEMOVERSION
   "ICONS",             NULL,    IT_BOOL,    OFFSET(Prefs,program.saveicons),0,
#endif
   "IGNOREMIME",        NULL,    IT_BOOL,    OFFSET(Prefs,network.ignoremime),0,
   "IMAGES",            NULL,    IT_SHORT,   OFFSET(Prefs,network.loadimg),2,
   "IMAGEVIEWER",       NULL,    IT_CMDARGS, OFFSET(Prefs,program.imgvcmd),0,
   "INCREMENTALTABLE",  NULL,    IT_BOOL,    OFFSET(Prefs,browser.inctable),0,
   "INDEXSUFFIX",       NULL,    IT_STRING,  OFFSET(Prefs,network.localindex),0,
   "JAVASCRIPT",        NULL,    IT_SHORT,   OFFSET(Prefs,browser.dojs),2,
   "JSERRORS",          NULL,    IT_BOOL,    OFFSET(Prefs,browser.jserrors),0,
   "KEEPFREECHIP",      NULL,    IT_LONG,    OFFSET(Prefs,network.minfreechip),0,
   "KEEPFREEFAST",      NULL,    IT_LONG,    OFFSET(Prefs,network.minfreefast),0,
#ifndef DEMOVERSION
   "KEY",               NULL,    IT_KEY,     OFFSET(Prefs,gui.keys),0,
#endif
   "LIMITEDPROXY",      NULL,    IT_BOOL,    OFFSET(Prefs,network.limitproxy),0,
   "MAILNEWS",          "EMAIL",       IT_STRING,  OFFSET(Prefs,network.emailaddr),0,
   "MAILNEWS",          "REALNAME",    IT_STRING,  OFFSET(Prefs,network.fullname),0,
   "MAILNEWS",          "REPLYTO",     IT_STRING,  OFFSET(Prefs,network.replyaddr),0,
   "MAILNEWS",          "ORGANIZATION",IT_STRING,  OFFSET(Prefs,network.organization),0,
   "MAILNEWS",          "SIGNATURE",   IT_STRING,  OFFSET(Prefs,network.sigfile),0,
   "MAILNEWS",          "SMTPHOST",    IT_STRING,  OFFSET(Prefs,network.smtphost),0,
   "MAILNEWS",          "MAILQUOTEHDR",IT_STRING,  OFFSET(Prefs,network.mailquotehdr),0,
   "MAILNEWS",          "EXTMAILER",   IT_BOOL,    OFFSET(Prefs,network.extmailer),0,
   "MAILNEWS",          "NNTPHOST",    IT_STRING,  OFFSET(Prefs,network.nntphost),0,
   "MAILNEWS",          "NEWSQUOTEHDR",IT_STRING,  OFFSET(Prefs,network.newsquotehdr),0,
   "MAILNEWS",          "USERNAME",    IT_STRING,  OFFSET(Prefs,network.newsauthuser),0,
   "MAILNEWS",          "PASSWORD",    IT_STRING,  OFFSET(Prefs,network.newsauthpass),0,
   "MAILNEWS",          "EXTNEWSREADER",IT_BOOL,   OFFSET(Prefs,network.extnewsreader),0,
   "MAILNEWS",          "MAXARTICLES", IT_LONG,    OFFSET(Prefs,network.maxartnews),0,
   "MAILNEWS",          "SORTARTICLES",IT_BOOL,    OFFSET(Prefs,network.sortednews),0,
   "MAILNEWS",          "SPLITSCREEN", IT_BOOL,    OFFSET(Prefs,network.framednews),0,
   "MAILNEWS",          "LONGHEADERS", IT_BOOL,    OFFSET(Prefs,network.longhdrnews),0,
   "MAILNEWS",          "PROPFONT",    IT_BOOL,    OFFSET(Prefs,network.propnews),0,
   "MAILNEWS",          "BYNUMBER",    IT_BOOL,    OFFSET(Prefs,network.newsbynum),0,
   "MAILTO",            NULL,    IT_CMDARGS, OFFSET(Prefs,network.mailtocmd),0,
   "MAXLOCAL",          NULL,    IT_LONG,    OFFSET(Prefs,network.maxdiskread),0,
   "MAXNETWORK",        NULL,    IT_LONG,    OFFSET(Prefs,network.maxconnect),0,
#ifndef DEMOVERSION
   "MENU",              NULL,    IT_MENU,    OFFSET(Prefs,gui.menus),0,
#endif
   "MIME",              NULL,    IT_MIME,    OFFSET(Prefs,browser.mimelist),0,
#ifndef DEMOVERSION
   "NAVBUTTON",         "BACK",     IT_STRING,OFFSET(Prefs,gui.navs[0]),0,
   "NAVBUTTON",         "FORWARD",  IT_STRING,OFFSET(Prefs,gui.navs[1]),0,
   "NAVBUTTON",         "HOME",     IT_STRING,OFFSET(Prefs,gui.navs[2]),0,
   "NAVBUTTON",         "ADDHOT",   IT_STRING,OFFSET(Prefs,gui.navs[3]),0,
   "NAVBUTTON",         "HOTLIST",  IT_STRING,OFFSET(Prefs,gui.navs[4]),0,
   "NAVBUTTON",         "CANCEL",   IT_STRING,OFFSET(Prefs,gui.navs[5]),0,
   "NAVBUTTON",         "NETSTATUS",IT_STRING,OFFSET(Prefs,gui.navs[6]),0,
   "NAVBUTTON",         "SEARCH",   IT_STRING,OFFSET(Prefs,gui.navs[7]),0,
   "NAVBUTTON",         "RELOAD",   IT_STRING,OFFSET(Prefs,gui.navs[8]),0,
   "NAVBUTTON",         "LOADIMGS", IT_STRING,OFFSET(Prefs,gui.navs[9]),0,
#endif
   "NAVIGATION",        NULL,    IT_BOOL,    OFFSET(Prefs,gui.shownav),0,
   "NETSTATUSOPEN",     NULL,    IT_BOOL,    OFFSET(Prefs,program.aanetstat),0,
   "NEWS",              NULL,    IT_CMDARGS, OFFSET(Prefs,network.newscmd),0,
#ifndef DEMOVERSION
   "NOCACHE",           NULL,    IT_NOCACHE, OFFSET(Prefs,network.nocache),0,
   "NOCOOKIE",          NULL,    IT_NOCOOKIE,OFFSET(Prefs,network.nocookie),0,
#endif
   "NOMINALFRAME",      NULL,    IT_BOOL,    OFFSET(Prefs,browser.nominalframe),0,
#ifndef DEMOVERSION
   "NOPROXY",           NULL,    IT_NOPROXY, OFFSET(Prefs,network.noproxy),0,
#endif
   "OVERLAP",           NULL,    IT_LONG,    OFFSET(Prefs,program.overlap),0,
   "PALETTE",           "0",     IT_RGB,     OFFSET(Prefs,program.scrpalette[0]),0,
   "PALETTE",           "1",     IT_RGB,     OFFSET(Prefs,program.scrpalette[3]),0,
   "PALETTE",           "2",     IT_RGB,     OFFSET(Prefs,program.scrpalette[6]),0,
   "PALETTE",           "3",     IT_RGB,     OFFSET(Prefs,program.scrpalette[9]),0,
   "PALETTE",           "4",     IT_RGB,     OFFSET(Prefs,program.scrpalette[12]),0,
   "PALETTE",           "5",     IT_RGB,     OFFSET(Prefs,program.scrpalette[15]),0,
   "PALETTE",           "6",     IT_RGB,     OFFSET(Prefs,program.scrpalette[18]),0,
   "PALETTE",           "7",     IT_RGB,     OFFSET(Prefs,program.scrpalette[21]),0,
   "PASSIVEFTP",        NULL,    IT_BOOL,    OFFSET(Prefs,network.passiveftp),0,
   "POPUP",             NULL,    IT_POPUP,   OFFSET(Prefs,gui.popupkey),0,
   "POPUPMENU",         NULL,    IT_PUPMENU, OFFSET(Prefs,gui.popupmenu),0,
   "RESTRICTIMAGES",    NULL,    IT_BOOL,    OFFSET(Prefs,network.restrictimages),0,
   "RFCCOOKIES",        NULL,    IT_BOOL,    OFFSET(Prefs,network.rfc2109),0,
   "SAVEPATH",          NULL,    IT_STRING,  OFFSET(Prefs,program.savepath),0,
   "SCREEN",            "TYPE",  IT_SHORT,   OFFSET(Prefs,program.screentype),2,
   "SCREEN",            "NAME",  IT_STRING,  OFFSET(Prefs,program.screenname),0,
   "SCREEN",            "MODE",  IT_XLONG,   OFFSET(Prefs,program.screenmode),0,
   "SCREEN",            "WIDTH", IT_SHORT,   OFFSET(Prefs,program.screenwidth),0,
   "SCREEN",            "HEIGHT",IT_SHORT,   OFFSET(Prefs,program.screenheight),0,
   "SCREEN",            "DEPTH", IT_SHORT,   OFFSET(Prefs,program.screendepth),0,
   "SCREEN",            "PALETTE",IT_SHORT,  OFFSET(Prefs,program.loadpalette),0,
   "SCREENPENS",        NULL,    IT_PENS,    OFFSET(Prefs,program.scrdrawpens[0]),0,
   "SHOWBUTTONS",       NULL,    IT_BOOL,    OFFSET(Prefs,gui.showbuttons),0,
   "SHUTDOWN",          NULL,    IT_STRING,  OFFSET(Prefs,program.shutdownscript),0,
   "SPAMBLOCK",         NULL,    IT_BOOL,    OFFSET(Prefs,network.spamblock),0,
#ifndef DEMOVERSION
   "SPOOFAS",           NULL,    IT_STRING,  OFFSET(Prefs,network.spoofid),0,
#endif
   "STARTHOME",         NULL,    IT_BOOL,    OFFSET(Prefs,network.starthomepage),0,
#ifndef DEMOVERSION
   "STARTTCP",          NULL,    IT_CMDARGS, OFFSET(Prefs,network.starttcpcmd),0,
#endif
   "STARTUP",           NULL,    IT_STRING,  OFFSET(Prefs,program.startupscript),0,
   "STYLE",             "H1",    IT_STYLE,   OFFSET(Prefs,browser.styles[1]),0,
   "STYLE",             "H2",    IT_STYLE,   OFFSET(Prefs,browser.styles[2]),0,
   "STYLE",             "H3",    IT_STYLE,   OFFSET(Prefs,browser.styles[3]),0,
   "STYLE",             "H4",    IT_STYLE,   OFFSET(Prefs,browser.styles[4]),0,
   "STYLE",             "H5",    IT_STYLE,   OFFSET(Prefs,browser.styles[5]),0,
   "STYLE",             "H6",    IT_STYLE,   OFFSET(Prefs,browser.styles[6]),0,
   "STYLE",             "BIG",   IT_STYLE,   OFFSET(Prefs,browser.styles[7]),0,
   "STYLE",             "SMALL", IT_STYLE,   OFFSET(Prefs,browser.styles[8]),0,
   "STYLE",             "SUB",   IT_STYLE,   OFFSET(Prefs,browser.styles[9]),0,
   "STYLE",             "SUP",   IT_STYLE,   OFFSET(Prefs,browser.styles[10]),0,
   "STYLE",             "ADDRESS",IT_STYLE,  OFFSET(Prefs,browser.styles[11]),0,
   "STYLE",             "BLOCKQUOTE",IT_STYLE,OFFSET(Prefs,browser.styles[12]),0,
   "STYLE",             "CITE",  IT_STYLE,   OFFSET(Prefs,browser.styles[13]),0,
   "STYLE",             "CODE",  IT_STYLE,   OFFSET(Prefs,browser.styles[14]),0,
   "STYLE",             "DFN",   IT_STYLE,   OFFSET(Prefs,browser.styles[15]),0,
   "STYLE",             "EM",    IT_STYLE,   OFFSET(Prefs,browser.styles[16]),0,
   "STYLE",             "KBD",   IT_STYLE,   OFFSET(Prefs,browser.styles[17]),0,
   "STYLE",             "PRE",   IT_STYLE,   OFFSET(Prefs,browser.styles[18]),0,
   "STYLE",             "SAMP",  IT_STYLE,   OFFSET(Prefs,browser.styles[19]),0,
   "STYLE",             "STRONG",IT_STYLE,   OFFSET(Prefs,browser.styles[20]),0,
   "STYLE",             "VAR",   IT_STYLE,   OFFSET(Prefs,browser.styles[21]),0,
   "SUPPRESSBANNERS",   NULL,    IT_BOOL,    OFFSET(Prefs,browser.nobanners),0,
#ifndef DEMOVERSION
   "TELNET",            NULL,    IT_CMDARGS, OFFSET(Prefs,network.telnetcmd),0,
#endif
   "TELNETPROXY",       NULL,    IT_NSTRING, OFFSET(Prefs,network.telnetproxy),0,
   "TEMPPATH",          NULL,    IT_STRING,  OFFSET(Prefs,program.temppath),0,
   "TOOLTIPS",          NULL,    IT_BOOL,    OFFSET(Prefs,browser.tooltips),0,
   "ULLINKS",           NULL,    IT_BOOL,    OFFSET(Prefs,browser.ullink),0,
   "VERIFICATION",      NULL,    IT_SHORT,   OFFSET(Prefs,network.caverify),2,
   "WATCHJS",           NULL,    IT_BOOL,    OFFSET(Prefs,browser.jswatch),0,
   NULL,
};

static UBYTE buffer[1024];
#endif /* !NOAREXXPORTS */

/* add boolean indices */
#define ADDMENUS     0
#define ADDMIME      1
#define ADDNOPROXY   2
#define ADDNOCACHE   3
#define ADDBUTTON    4
#define ADDPUPMENU   5
#define ADDNOCOOKIE  6
#define ADDKEY       7
#define ADDALIAS     8

#define NRADDIND     9

/* Rawkey codes of keys allowed to use for userkey */
static UBYTE nonmapkeys[]=
{  0x0f,0x1d,0x1e,0x1f,0x2d,0x2e,0x2f,0x3c,0x3d,0x3e,0x3f,0x40,0x41,
   0x42,0x43,0x44,0x45,0x56,0x4a,0x4c,0x4d,0x4e,0x4f,0x50,0x51,0x52,
   0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5f,
};

static UBYTE *menutypes="MIS-";

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

__asm __saveds void Getcfg(
   register __a0 struct Arexxcmd *ac,
   register __a1 struct Prefs *prefs);
__asm __saveds void Setcfg(
   register __a0 struct Arexxcmd *ac,
   register __a1 struct Prefs *prefs);

/* Function declarations for project dependent hook functions */
static ULONG Initaweblib(struct Library *libbase);
static void Expungeaweblib(struct Library *libbase);

struct Library *ArexxBase;

static APTR libseglist;

struct ExecBase *SysBase;

LONG __saveds __asm Libstart(void)
{  return -1;
}

static APTR functable[]=
{  Openlib,
   Closelib,
   Expungelib,
   Extfunclib,
   Getcfg,
   Setcfg,
   (APTR)-1
};

/* Init table used in library initialization. */
static ULONG inittab[]=
{  sizeof(struct Library),
   (ULONG) functable,
   0,
   (ULONG) Initlib
};

static char __aligned libname[]="arexx.aweblib";
static char __aligned libid[]="arexx.aweblib " AWEBLIBVSTRING " " __AMIGADATE__;

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
   ArexxBase=libbase;
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

static ULONG Initaweblib(struct Library *libbase)
{  
#ifdef NOAREXXPORTS
   return FALSE;
#else
   if(!(DOSBase=(struct DosLibrary *)OpenLibrary("dos.library",39))) return FALSE;
   if(!(UtilityBase=(struct Library *)OpenLibrary("utility.library",39))) return FALSE;
   if(!(GfxBase=(struct GfxBase *)OpenLibrary("graphics.library",39))) return FALSE;
   return TRUE;
#endif
}

static void Expungeaweblib(struct Library *libbase)
{
#ifndef NOAREXXPORTS
   if(GfxBase) CloseLibrary(GfxBase);
   if(UtilityBase) CloseLibrary(UtilityBase);
   if(DOSBase) CloseLibrary(DOSBase);
#endif
}

/*-----------------------------------------------------------------------*/

#ifndef NOAREXXPORTS
/* Output this item in the buffer */
static void Printitem(struct Prefs *prefs,struct Iteminfo *ii)
{  void *itemp=(char *)prefs+ii->offset;
   switch(ii->type)
   {  case IT_BOOL:
         sprintf(buffer,"%d",BOOLVAL(*(BOOL *)itemp));
         break;
      case IT_NBOOL:
         sprintf(buffer,"%d",!*(BOOL *)itemp);
         break;
      case IT_SHORT:
         sprintf(buffer,"%d",*(short *)itemp);
         break;
      case IT_LONG:
         sprintf(buffer,"%d",*(long *)itemp);
         break;
      case IT_STRING:
      case IT_NSTRING:
         {  UBYTE *p=*(UBYTE **)itemp;
            strncpy(buffer,p?p:NULLSTRING,511);
         }
         break;
      case IT_XLONG:
         sprintf(buffer,"%08x",*(ULONG *)itemp);
         break;
      case IT_RGB:
         {  ULONG *p=itemp;
            sprintf(buffer,"%02x%02x%02x",p[0]>>24,p[1]>>24,p[2]>>24);
         }
         break;
      case IT_CMDARGS:
         {  UBYTE **p=itemp;
            if(p[0] && *p[0])
            {  sprintf(buffer,"%s;%s",p[0],p[1]?p[1]:NULLSTRING);
            }
            else
            {  *buffer='\0';
            }
         }
         break;
      case IT_PENS:
         {  short i;
            WORD *wp=itemp;
            UBYTE *p=buffer;
            for(i=2;i<11;i++)
            {  p+=sprintf(p," %d",wp[i]);
            }
         }
         break;
      case IT_FONT:
         {  struct Fontprefs *fp=itemp;
            sprintf(buffer,"%s/%d",fp->fontname,fp->fontsize);
         }
         break;
      case IT_STYLE:
         {  struct Styleprefs *sp=itemp;
            sprintf(buffer,sp->relsize?"%c;%+d;":"%c;%d;","NF"[sp->fonttype],sp->fontsize);
            if(sp->style&FSF_BOLD) strcat(buffer,"B");
            if(sp->style&FSF_ITALIC) strcat(buffer,"I");
            if(sp->style&FSF_UNDERLINED) strcat(buffer,"U");
         }
         break;
      case IT_POPUP:
         {  USHORT mask=*(USHORT *)itemp;
            UBYTE *p=buffer;
            if(mask&(IEQUALIFIER_LALT|IEQUALIFIER_RALT)) *p++='A';
            if(mask&(IEQUALIFIER_CONTROL)) *p++='C';
            if(mask&(IEQUALIFIER_MIDBUTTON)) *p++='M';
            if(mask&(IEQUALIFIER_RBUTTON)) *p++='R';
            if(mask&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)) *p++='S';
            *p++='\0';
         }
         break;
      default:
         *buffer='\0';
   }
}

/* Print all items of unnamed group */
static void Printgroup(struct Arexxcmd *ac,UBYTE *stem,short *stemnrp,
   struct Prefs *prefs,struct Iteminfo *ii)
{  void *itemp=(char *)prefs+ii->offset;
   switch(ii->type)
   {  case IT_MENU:
         {  struct Menuentry *me=itemp;   /* list head */
            for(me=me->next;me->next;me=me->next)
            {  sprintf(buffer,"%c\n%s\n%s\n%s",
                  menutypes[me->type],me->title,me->scut,me->cmd);
               (*stemnrp)++;
               Setstemvar(ac,stem,*stemnrp,"ITEM",ii->name);
               Setstemvar(ac,stem,*stemnrp,"VALUE",buffer);
            }
         }
         break;
      case IT_BUTTON:
         {  struct Userbutton *ub=itemp;  /* list head */
            for(ub=ub->next;ub->next;ub=ub->next)
            {  sprintf(buffer,"%s\n%s",ub->label,ub->cmd);
               (*stemnrp)++;
               Setstemvar(ac,stem,*stemnrp,"ITEM",ii->name);
               Setstemvar(ac,stem,*stemnrp,"VALUE",buffer);
            }
         }
         break;
      case IT_PUPMENU:
         {  LIST(Popupitem) *pmenu=itemp;  /* First of 3 lists */
            struct Popupitem *pi;
            UBYTE ident[]="ILF";
            short i;
            for(i=0;i<NRPOPUPMENUS;i++)
            {  for(pi=pmenu[i].first;pi->next;pi=pi->next)
               {  sprintf(buffer,"%c\n%d\n%s\n%s",ident[i],
                     ((pi->flags&PUPF_INMEM)?1:0)+((pi->flags&PUPF_NOTINMEM)?2:0),
                     pi->title,pi->cmd);
                  (*stemnrp)++;
                  Setstemvar(ac,stem,*stemnrp,"ITEM",ii->name);
                  Setstemvar(ac,stem,*stemnrp,"VALUE",buffer);
               }
            }
         }
         break;
      case IT_KEY:
         {  struct Userkey *uk=itemp;      /* list head */
            UBYTE *p;
            for(uk=uk->next;uk->next;uk=uk->next)
            {  *buffer='\0';
               if(uk->key&UKEY_SHIFT) strcat(buffer,"S");
               if(uk->key&UKEY_ALT) strcat(buffer,"A");
               if(*buffer) strcat(buffer,"-");
               p=buffer+strlen(buffer);
               if(uk->key&UKEY_ASCII)
               {  p+=sprintf(p,"%c",uk->key&UKEY_MASK);
               }
               else
               {  p+=sprintf(p,"$%02X",uk->key&UKEY_MASK);
               }
               sprintf(p,"\n%s",uk->cmd);
               (*stemnrp)++;
               Setstemvar(ac,stem,*stemnrp,"ITEM",ii->name);
               Setstemvar(ac,stem,*stemnrp,"VALUE",buffer);
            }
         }
         break;
      case IT_ALIAS:
         {  struct Fontalias *fa=itemp;   /* list head */
            short i;
            UBYTE *p;
            for(fa=fa->next;fa->next;fa=fa->next)
            {  p=buffer;
               p+=sprintf(buffer,"%s",fa->alias);
               for(i=0;i<NRFONTS;i++)
               {  p+=sprintf(p,"\n%s/%d",fa->fp[i].fontname,fa->fp[i].fontsize);
               }
               (*stemnrp)++;
               Setstemvar(ac,stem,*stemnrp,"ITEM",ii->name);
               Setstemvar(ac,stem,*stemnrp,"VALUE",buffer);
            }
         }
         break;
      case IT_MIME:
         {  struct Mimeinfo *mi=itemp; /* list head */
            for(mi=mi->next;mi->next;mi=mi->next)
            {  sprintf(buffer,"%s/%s;%s;%c;%s;%s",
                  mi->type,mi->subtype,
                  mi->extensions?mi->extensions:NULLSTRING,
                  "DIAEPS"[mi->driver],
                  mi->cmd?mi->cmd:NULLSTRING,
                  mi->args?mi->args:NULLSTRING);
               (*stemnrp)++;
               Setstemvar(ac,stem,*stemnrp,"ITEM",ii->name);
               Setstemvar(ac,stem,*stemnrp,"VALUE",buffer);
            }
         }
         break;
      case IT_NOPROXY:
      case IT_NOCACHE:
      case IT_NOCOOKIE:
         {  struct Nocache *nc=itemp;  /* list head */
            for(nc=nc->next;nc->next;nc=nc->next)
            {  (*stemnrp)++;
               Setstemvar(ac,stem,*stemnrp,"ITEM",ii->name);
               Setstemvar(ac,stem,*stemnrp,"VALUE",nc->name);
            }
         }
         break;
   }
}

/* Find this item */
static struct Iteminfo *Finditem(UBYTE *item,UBYTE *sub)
{  struct Iteminfo *ii;
   for(ii=iteminfo;ii->name;ii++)
   {  if(STRIEQUAL(ii->name,item)
      && ((!sub && !ii->sub) || (sub && ii->sub && STRIEQUAL(ii->sub,sub))))
      {  return ii;
      }
   }
   return NULL;
}

/* Expand byte to long */
static ULONG Expandrgb(UBYTE c)
{  ULONG rgb=(c<<8)|c;
   return (rgb<<16)|rgb;
}

/* Set a new item value. Returns TRUE if ok. */
static BOOL Setitem(struct Prefs *prefs,UBYTE *itemname,UBYTE *value,BOOL *add)
{  UBYTE item[64],*sub;
   BOOL ok=FALSE;
   struct Iteminfo *ii;
   void *itemp;
   long n=0;
   if(itemname)
   {  strncpy(item,itemname,63);
      item[63]='\0';
      if(sub=strchr(item,'.'))
      {  *sub++='\0';
      }
      if(ii=Finditem(item,sub))
      {  itemp=(char *)prefs+ii->offset;
         switch(ii->type)
         {  case IT_BOOL:
               ok=sscanf(value," %ld",&n);
               *(BOOL *)itemp=BOOLVAL(n);
               break;
            case IT_NBOOL:
               ok=sscanf(value," %ld",&n);
               *(BOOL *)itemp=!n;
               break;
            case IT_SHORT:
               ok=sscanf(value," %ld",&n);
               if(n<0) n=0;
               if(ii->max && n>ii->max) n=ii->max;
               *(short *)itemp=n;
               break;
            case IT_LONG:
               ok=sscanf(value," %ld",&n);
               if(n<0) n=0;
               *(long *)itemp=n;
               break;
            case IT_STRING:
               {  UBYTE **p=itemp;
                  UBYTE *v=Dupstr(value,-1);
                  if(v)
                  {  if(*p) FREE(*p);
                     *p=v;
                     ok=TRUE;
                  }
               }
               break;
            case IT_NSTRING:
               {  UBYTE **p=itemp;
                  if(*p) FREE(*p);
                  if(*value)
                  {  *p=Dupstr(value,-1);
                  }
                  else
                  {  *p=NULL;
                  }
                  ok=TRUE;
               }
               break;
            case IT_XLONG:
               ok=sscanf(value," %lx",&n);
               *(ULONG *)itemp=n;
               break;
            case IT_RGB:
               {  ULONG *p=itemp;
                  if(sscanf(value," %lx",&n) && n>=0 && n<=0x00ffffff)
                  {  p[0]=Expandrgb((n>>16)&0xff);
                     p[1]=Expandrgb((n>>8)&0xff);
                     p[2]=Expandrgb(n&0xff);
                     ok=TRUE;
                  }
               }
               break;
            case IT_CMDARGS:
               {  UBYTE *args=strchr(value,';');
                  UBYTE **p=itemp;
                  UBYTE *p0,*p1;
                  p0=Dupstr(value,args?args-value:-1);
                  if(args) p1=Dupstr(args+1,-1);
                  else p1=Dupstr("",-1);
                  if(p0 && p1)
                  {  if(p[0]) FREE(p[0]);
                     p[0]=p0;
                     if(p[1]) FREE(p[1]);
                     p[1]=p1;
                  }
                  else
                  {  if(p0) FREE(p0);
                     if(p1) FREE(p1);
                  }
                  ok=TRUE;
               }
               break;
            case IT_PENS:
               {  WORD *wp=itemp;
                  short i;
                  for(i=2;i<13;i++) wp[i]=-1;
                  sscanf(value," %hd %hd %hd %hd %hd %hd %hd %hd %hd",
                     &wp[2],&wp[3],&wp[4],&wp[5],&wp[6],&wp[7],&wp[8],&wp[9],&wp[10]);
                  wp[5]=0;
                  ok=TRUE;
               }
               break;
            case IT_FONT:
               {  struct Fontprefs *fp=itemp;
                  UBYTE *sizep=strchr(value,'/');
                  if(sizep)
                  {  if(fp->fontname) FREE(fp->fontname);
                     fp->fontname=Dupstr(value,sizep-value);
                     ok=sscanf(sizep+1,"%hd",&fp->fontsize);
                  }
               }
               break;
            case IT_STYLE:
               {  struct Styleprefs *sp=itemp;
                  UBYTE *p=value;
                  ok=TRUE;
                  while(isspace(*p)) p++;
                  sp->fonttype=BOOLVAL(toupper(*p)=='F');
                  if(p=strchr(value,';'))
                  {  sscanf(p,"%hd",&sp->fontsize);
                     if(sp->relsize)
                     {  if(sp->fontsize<-6) sp->fontsize=-6;
                        if(sp->fontsize>6) sp->fontsize=6;
                     }
                     else
                     {  if(sp->fontsize<1) sp->fontsize=1;
                        if(sp->fontsize>7) sp->fontsize=7;
                     }
                     sp->style=0;
                     for(;*p;p++)
                     {  if(toupper(*p)=='B') sp->style|=FSF_BOLD;
                        if(toupper(*p)=='I') sp->style|=FSF_ITALIC;
                        if(toupper(*p)=='U') sp->style|=FSF_UNDERLINED;
                     }
                     ok=TRUE;
                  }
               }
               break;
            case IT_POPUP:
               {  USHORT mask=0;
                  UBYTE *p;
                  for(p=value;*p;p++)
                  {  switch(toupper(*p))
                     {  case 'A':mask|=IEQUALIFIER_LALT|IEQUALIFIER_RALT;break;
                        case 'C':mask|=IEQUALIFIER_CONTROL;break;
                        case 'M':mask|=IEQUALIFIER_MIDBUTTON;break;
                        case 'R':mask|=IEQUALIFIER_RBUTTON;break;
                        case 'S':mask|=IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT;break;
                     }
                  }
                  *(USHORT *)itemp=mask;
                  ok=TRUE;
               }
               break;
            case IT_MENU:
               {  LIST(Menuentry) *list=itemp;
                  struct Menuentry *me;
                  UBYTE *p,*q,*title,*cmd;
                  UBYTE scut;
                  USHORT type;
                  if(p=Dupstr(value,-1))
                  {  if(!add[ADDMENUS])
                     {  while(me=REMHEAD(list))
                        {  Freemenuentry(me);
                        }
                        add[ADDMENUS]=TRUE;
                     }
                     if(q=strchr(menutypes,*p))
                     {  type=q-menutypes;
                        if(title=strchr(p,'\n'))
                        {  title++;
                           if(q=strchr(title,'\n'))
                           {  *q++='\0';
                              if(cmd=strchr(q,'\n'))
                              {  *cmd++='\0';
                                 scut=*q;
                                 Addmenuentry(list,type,title,scut,cmd);
                                 ok=TRUE;
                              }
                           }
                        }
                     }
                     FREE(p);
                  }
               }
               break;
            case IT_BUTTON:
               {  LIST(Userbutton) *list=itemp;
                  struct Userbutton *ub;
                  UBYTE *label,*cmd;
                  if(label=Dupstr(value,-1))
                  {  if(!add[ADDBUTTON])
                     {  while(ub=REMHEAD(list))
                        {  Freeuserbutton(ub);
                        }
                        add[ADDBUTTON]=TRUE;
                     }
                     if(*label)
                     {  if(cmd=strchr(label,'\n'))
                        {  *cmd++='\0';
                           Adduserbutton(list,label,cmd);
                           ok=TRUE;
                        }
                     }
                     FREE(label);
                  }
               }
               break;
            case IT_PUPMENU:
               {  LIST(Popupitem) *pmenu=itemp;
                  struct Popupitem *pi;
                  short i;
                  UBYTE *p,*title,*cmd;
                  USHORT flags,mem;
                  if(p=Dupstr(value,-1))
                  {  if(!add[ADDPUPMENU])
                     {  for(i=0;i<NRPOPUPMENUS;i++)
                        {  while(pi=REMHEAD(&pmenu[i]))
                           {  Freepopupitem(pi);
                           }
                        }
                        add[ADDPUPMENU]=TRUE;
                     }
                     if(*p)
                     {  switch(toupper(*p))
                        {  case 'I':i=0;break;
                           case 'L':i=1;break;
                           case 'F':i=2;break;
                           default:i=-1;
                        }
                        if(i>=0)
                        {  if(title=strchr(p,'\n'))
                           {  title++;
                              sscanf(title,"%hd",&mem);
                              flags=0;
                              if(mem&1) flags|=PUPF_INMEM;
                              if(mem&2) flags|=PUPF_NOTINMEM;
                              if(i<2)
                              {  if(!flags) flags=PUPF_INMEM|PUPF_NOTINMEM;
                              }
                              else
                              {  flags=0;
                              }
                              if(title=strchr(title,'\n'))
                              {  title++;
                                 if(cmd=strchr(title,'\n'))
                                 {  *cmd++='\0';
                                    Addpopupitem(&pmenu[i],flags,title,cmd);
                                    ok=TRUE;
                                 }
                              }
                           }
                        }
                     }
                     FREE(p);
                  }
               }
               break;
            case IT_KEY:
               {  LIST(Userkey) *list=itemp;
                  struct Userkey *uk;
                  UBYTE *p;
                  USHORT key;
                  ULONG hex;
                  short i;
                  if(!add[ADDKEY])
                  {  while(uk=REMHEAD(list))
                     {  Freeuserkey(uk);
                     }
                     add[ADDKEY]=TRUE;
                  }
                  p=value;
                  key=0;
                  if(toupper(*p)=='S')
                  {  key|=UKEY_SHIFT;
                     p++;
                  }
                  else if(toupper(*p)=='A')
                  {  key|=UKEY_ALT;
                     p++;
                  }
                  if(*p=='-') p++;
                  if(*p=='$')
                  {  p++;
                     sscanf(p,"%lx",&hex);
                     for(i=0;i<sizeof(nonmapkeys);i++)
                     {  if(nonmapkeys[i]==hex)
                        {  key|=hex;
                           break;
                        }
                     }
                  }
                  else
                  {  if(key==UKEY_ALT)
                     {  hex=toupper(*p);
                        if(hex>='A' && hex<='Z')
                        {  key|=UKEY_ASCII|hex;
                        }
                     }
                  }
                  if(key&UKEY_MASK)
                  {  if(p=strchr(p,'\n'))
                     {  p++;
                        if(*p)
                        {  Adduserkey(list,key,p);
                           ok=TRUE;
                        }
                     }
                  }
               }
               break;
            case IT_ALIAS:
               {  LIST(Fontalias) *list=itemp;
                  UBYTE *alias,*p;
                  UBYTE *fname[NRFONTS];
                  short fsize[NRFONTS];
                  short i;
                  struct Fontalias *fa;
                  if(alias=Dupstr(value,-1))
                  {  if(!add[ADDALIAS])
                     {  while(fa=REMHEAD(list)) Freefontalias(fa);
                     }
                     add[ADDALIAS]=TRUE;
                     if(*alias)
                     {  p=alias;
                        for(i=0;i<NRFONTS;i++)
                        {  if(p=strchr(p,'\n'))
                           {  *p++='\0';
                              fname[i]=p;
                              if(p=strchr(p,'/'))
                              {  *p++='\0';
                                 sscanf(p,"%hd",&fsize[i]);
                              }
                              else break;
                           }
                           else break;
                        }
                        if(i>=NRFONTS)
                        {  if(fa=Addfontalias(list,alias))
                           {  for(i=0;i<NRFONTS;i++)
                              {  if(fa->fp[i].fontname) FREE(fa->fp[i].fontname);
                                 fa->fp[i].fontname=Dupstr(fname[i],-1);
                                 fa->fp[i].fontsize=fsize[i];
                                 if(fa->fp[i].font)
                                 {  CloseFont(fa->fp[i].font);
                                    fa->fp[i].font=NULL;
                                 }
                              }
                              ok=TRUE;
                           }
                        }
                     }
                     FREE(alias);
                  }
               }
               break;
            case IT_MIME:
               {  LIST(Mimeinfo) *list=itemp;
                  struct Mimeinfo *mi,*next;
                  UBYTE *type,*sub,*ext,*action,*name,*args;
                  UBYTE *actions="DIAEPS";
                  short act;
                  if(type=Dupstr(value,-1))
                  {  if(!add[ADDMIME])
                     {  for(mi=list->first;mi->next;mi=next)
                        {  next=mi->next;
                           if(mi->deleteable)
                           {  REMOVE(mi);
                              Freemimeinfo(mi);
                           }
                        }
                        add[ADDMIME]=TRUE;
                     }
                     if(*type)
                     {  if(sub=strchr(type,'/'))
                        {  *sub++='\0';
                           if(ext=strchr(sub,';'))
                           {  *ext++='\0';
                              if(action=strchr(ext,';'))
                              {  *action++='\0';
                                 if(name=strchr(action,';'))
                                 {  *name++='\0';
                                    if(args=strchr(name,';'))
                                    {  *args++='\0';
                                    }
                                 }
                                 else
                                 {  args=NULL;
                                 }
                                 action=strchr(actions,toupper(*action));
                                 if(action) act=action-actions;
                                 else act=0;
                                 if(*type && *sub)
                                 {  for(mi=list->first;mi->next;mi=mi->next)
                                    {  if(STRIEQUAL(mi->type,type) && STRIEQUAL(mi->subtype,sub))
                                       {  REMOVE(mi);
                                          Freemimeinfo(mi);
                                          break;
                                       }
                                    }
                                    Addmimeinfo(list,type,sub,ext,act,
                                       name?name:NULLSTRING,args?args:NULLSTRING);
                                    ok=TRUE;
                                 }
                              }
                           }
                        }
                     }
                     else
                     {  ok=TRUE;
                     }
                     FREE(type);
                  }
               }
               break;
            case IT_NOPROXY:
               {  LIST(Nocache) *list=itemp;
                  struct Nocache *nc;
                  if(!add[ADDNOPROXY])
                  {  while(nc=REMHEAD(list))
                     {  Freenocache(nc);
                     }
                     add[ADDNOPROXY]=TRUE;
                  }
                  if(*value)
                  {  Addnocache(list,value);
                  }
                  ok=TRUE;
               }
               break;
            case IT_NOCACHE:
               {  LIST(Nocache) *list=itemp;
                  struct Nocache *nc;
                  if(!add[ADDNOCACHE])
                  {  while(nc=REMHEAD(list))
                     {  Freenocache(nc);
                     }
                     add[ADDNOCACHE]=TRUE;
                  }
                  if(*value)
                  {  Addnocache(list,value);
                  }
                  ok=TRUE;
               }
               break;
            case IT_NOCOOKIE:
               {  LIST(Nocache) *list=itemp;
                  struct Nocache *nc;
                  if(!add[ADDNOCOOKIE])
                  {  while(nc=REMHEAD(list))
                     {  Freenocache(nc);
                     }
                     add[ADDNOCOOKIE]=TRUE;
                  }
                  if(*value)
                  {  Addnocache(list,value);
                  }
                  ok=TRUE;
               }
               break;
         }
      }
   }
   return ok;
}

#endif /* !NOAREXXPORTS */

/*-----------------------------------------------------------------------*/

__asm __saveds void Getcfg(register __a0 struct Arexxcmd *ac,
   register __a1 struct Prefs *prefs)
{  
#ifndef NOAREXXPORTS
   UBYTE *sub,*stem,*p;
   UBYTE item[64],outbuf[64];
   struct Iteminfo *ii;
   short stemnr=0;
   if(ac->parameter[1] && (ac->varname=Dupstr((UBYTE *)ac->parameter[1],-1)))
   {  for(p=ac->varname;*p;p++) *p=toupper(*p);
   }
   stem=(UBYTE *)ac->parameter[2];
   if(ac->parameter[0])
   {  strncpy(item,(UBYTE *)ac->parameter[0],63);
      item[63]='\0';
      sub=strchr(item,'.');
      if(sub)
      {  *sub++='\0';
      }
   }
   else
   {  *item='\0';
      sub=NULL;
   }
   ac->errorlevel=RXERR_INVARGS;
   for(ii=iteminfo;ii->name;ii++)
   {  if(!*item || STRIEQUAL(ii->name,item))
      {  if(*item && (!ii->sub || (sub && STRIEQUAL(ii->sub,sub)))
         && ii->type<IT_UNNAMEDGROUP)
         {  /* Single item */
            Printitem(prefs,ii);
            ac->result=Dupstr(buffer,-1);
            ac->errorlevel=RXERR_OK;
         }
         else if((!*item || (ii->sub && !sub)) && ii->type<IT_UNNAMEDGROUP)
         {  /* Multiple items */
            if(!(ac->flags&ARXCF_ALLOWSTEM)) break;
            if(!stem) break;
            Printitem(prefs,ii);
            if(ii->sub)
            {  sprintf(outbuf,"%s.%s",ii->name,ii->sub);
            }
            else
            {  strcpy(outbuf,ii->name);
            }
            stemnr++;
            Setstemvar(ac,stem,stemnr,"ITEM",outbuf);
            Setstemvar(ac,stem,stemnr,"VALUE",buffer);
            ac->errorlevel=RXERR_OK;
         }
         else if(ii->type>IT_UNNAMEDGROUP)
         {  /* Unnamed group */
            if(!(ac->flags&ARXCF_ALLOWSTEM)) break;
            if(!stem) break;
            Printgroup(ac,stem,&stemnr,prefs,ii);
            ac->errorlevel=RXERR_OK;
         }
      }
   }
   if((ac->flags&ARXCF_ALLOWSTEM) && stem && !ac->errorlevel)
   {  sprintf(outbuf,"%d",stemnr);
      Setstemvar(ac,stem,0,NULL,outbuf);
   }
#endif
}

__asm __saveds void Setcfg(register __a0 struct Arexxcmd *ac,
   register __a1 struct Prefs *prefs)
{  
#ifndef NOAREXXPORTS
   struct Prefs newprefs={0};
   UBYTE *stem=(UBYTE *)ac->parameter[2];
   UBYTE *item,*value;
   long i,j,max=0;
   BOOL add[NRADDIND];
   Copyprefs(prefs,&newprefs);
   /* Clear font pointers to avoid bogus close */
   for(i=0;i<2;i++)
   {  for(j=0;j<NRFONTS;j++)
      {  newprefs.browser.font[i][j].font=NULL;
      }
   }
   for(i=0;i<NRADDIND;i++) add[i]=ac->parameter[3];
   if((ac->flags&ARXCF_ALLOWSTEM) && stem)
   {  if(value=Getstemvar(ac,stem,0,NULL))
      {  if(sscanf(value,"%ld",&max) && max>0)
         {  for(i=0;!ac->errorlevel && i<max;i++)
            {  if((item=Getstemvar(ac,stem,i+1,"ITEM"))
               && (value=Getstemvar(ac,stem,i+1,"VALUE")))
               {  if(!Setitem(&newprefs,item,value,add))
                  {  ac->errorlevel=RXERR_INVARGS;
                  }
               }
               else ac->errorlevel=RXERR_FATAL;
            }
         }
         else ac->errorlevel=RXERR_INVARGS;
      }
      else ac->errorlevel=RXERR_FATAL;
   }
   else if(ac->parameter[0] && ac->parameter[1])
   {  if(!Setitem(&newprefs,(UBYTE *)ac->parameter[0],(UBYTE *)ac->parameter[1],add))
      {  ac->errorlevel=RXERR_INVARGS;
      }
   }
   else ac->errorlevel=RXERR_INVARGS;
   if(!ac->errorlevel)
   {  Saveprefs(&newprefs);
   }
   Disposeprefs(&newprefs);
#endif
}

