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

/* defprefs.c - AWeb default prefs */

#define NOCFGLOCALE
#include "awebcfg.h"
#include "awebprefs.h"
#include "keyfile.h"
#include "ezlists.h"
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/icon.h>
#include <proto/locale.h>
#include <proto/colorwheel.h>

#define CATCOMP_NUMBERS
#include "locale.h"

/* Initial empty LIST() */
#define EMPTYLIST(str,list) { (struct str *)&list.tail,NULL,(struct str *)&list.first }

#ifdef DEMOVERSION
#define NDTRUE          FALSE
#define DEFMAXCONNECT   2
#else
#define NDTRUE          TRUE
#define DEFMAXCONNECT   8
#endif

struct Prefs defprefs=
{  {  /* browser prefs */
      {  {  /* font [0] normal */
            {  "CGTimes.font",     11,NULL },
            {  "CGTimes.font",     13,NULL },
            {  "CGTimes.font",     15,NULL },
            {  "CGTimes.font",     18,NULL },
            {  "CGTimes.font",     24,NULL },
            {  "CGTimes.font",     30,NULL },
            {  "CGTimes.font",     36,NULL },
         },
         {  /* font [1] fixed */
            {  "LetterGothic.font",   11,NULL },
            {  "LetterGothic.font",   13,NULL },
            {  "LetterGothic.font",   15,NULL },
            {  "LetterGothic.font",   18,NULL },
            {  "LetterGothic.font",   24,NULL },
            {  "LetterGothic.font",   30,NULL },
            {  "LetterGothic.font",   36,NULL },
         },
      },
      EMPTYLIST(Fontalias,defprefs.aliaslist), /* font alias */
      {  /* styles */
         {  0,3,FALSE,0 },                   /* NORMAL */
         {  0,7,FALSE,0 },                   /* H1 */
         {  0,6,FALSE,0 },                   /* H2 */
         {  0,5,FALSE,0 },                   /* H3 */
         {  0,4,FALSE,0 },                   /* H4 */
         {  0,3,FALSE,0 },                   /* H5 */
         {  0,2,FALSE,0 },                   /* H6 */
         {  0,3,TRUE,0 },                    /* BIG */
         {  0,-3,TRUE,0 },                   /* SMALL */
         {  0,-2,TRUE,0 },                   /* SUB */
         {  0,-2,TRUE,0 },                   /* SUP */
         {  0,0,TRUE,FSF_ITALIC },           /* ADDRESS */
         {  0,0,TRUE,FSF_ITALIC },           /* BLOCKQUOTE */
         {  0,0,TRUE,FSF_ITALIC },           /* CITE */
         {  1,0,TRUE,0 },                    /* CODE */
         {  0,0,TRUE,FSF_ITALIC },           /* DFN */
         {  0,0,TRUE,FSF_ITALIC },           /* EM */
         {  1,0,TRUE,0 },                    /* KBD */
         {  1,0,TRUE,0 },                    /* PRE */
         {  1,0,TRUE,0 },                    /* SAMP */
         {  0,1,TRUE,FSF_BOLD },             /* STRONG */
         {  1,0,TRUE,FSF_ITALIC },           /* VAR */
      },
      { 0,0,0xffffffff },                    /* new link */
      { 0,0,0x99999999 },                    /* old link */
      { 0xaaaaaaaa,0,0 },                    /* selected link */
      { 0xffffffff,0xffffffff,0xffffffff },  /* background */
      { 0,0,0 },                             /* text */
      FALSE,                                 /* screenpens */
      HTML_TOLERANT,                         /* html mode */
      FALSE,                                 /* underline links */
      TRUE,                                  /* do colours */
      TRUE,                                  /* do bg sound */
      5,                                     /* blink rate */
      DOJS_11,                               /* do javascript */
      TRUE,                                  /* do frames */
      TRUE,                                  /* incremental table display */
      FALSE,                                 /* nominal frame height */
      FALSE,                                 /* show JS errors */
      FALSE,                                 /* suppress banner windows */
      TRUE,                                  /* tooltips */
      TRUE,                                  /* hand pointer */
      TRUE,                                  /* JS watch */
      EMPTYLIST(Mimeinfo,defprefs.mimelist), /* mime */
   },
   {  /* program prefs */
      "RAM:",                                /* save path */
      "T:",                                  /* temp path */
      32,                                    /* overlap */
      FALSE,                                 /* commands */
      TRUE,                                  /* hl auto close */
      FALSE,                                 /* hl requester */
      TRUE,                                  /* wh auto close */
      NDTRUE,                                /* save icons */
      NDTRUE,                                /* allow clip dragging */
      FALSE,                                 /* auto open hotlist */
      FALSE,                                 /* auto open winhis */
      FALSE,                                 /* auto open netstat */
      TRUE,                                 /* center requesters */
      "C:Ed",                                /* editor cmd */
      "\"%f\"",                              /* editor args */
      "SYS:Utilities/MultiView",             /* viewer cmd */
      "\"%f\" pubscreen \"%n\"",             /* viewer args */
      "",                                    /* image viewer cmd */
      "",                                    /* image viewer args */
      "CON:////%t/AUTO/CLOSE/WAIT/SCREEN %n",/* console name */
      "",                                    /* startup script */
      "",                                    /* shutdown script */
      SCRTYPE_DEFAULT,                       /* screen type */
      "Workbench",                           /* screen name */
      PAL_MONITOR_ID|HIRES_KEY,              /* screen mode */
      8,                                     /* screen depth */
      640,256,                               /* screen width, height */
      LOADPAL_COLORS,                        /* load palette */
      {  /* screen palette */
         0xaaaaaaaa,0xaaaaaaaa,0xaaaaaaaa, 0x00000000,0x00000000,0x00000000,
         0xffffffff,0xffffffff,0xffffffff, 0x66666666,0x88888888,0xbbbbbbbb,
         0xeeeeeeee,0x44444444,0x44444444, 0x55555555,0xdddddddd,0x55555555,
         0x00000000,0x44444444,0xdddddddd, 0xeeeeeeee,0x99999999,0x00000000,
      },
      {  /* screen pens */
         1,2,1,2,1,3,1,0,2,1,2,1,-1,
      },
      FALSE,                                 /* four colour mode */
   },
   {  /* gui prefs */
      IEQUALIFIER_LALT|IEQUALIFIER_RALT|IEQUALIFIER_CONTROL|IEQUALIFIER_RBUTTON,
                                             /* popup key */
      TRUE,                                  /* show buttons */
      TRUE,                                  /* show navigation controls */
      EMPTYLIST(Menuentry,defprefs.menus),   /* menus */
      EMPTYLIST(Userbutton,defprefs.buttons),/* user buttons */
      {  EMPTYLIST(Popupitem,defprefs.popupmenu[PUPT_IMAGE]),  /* popup menu items */
         EMPTYLIST(Popupitem,defprefs.popupmenu[PUPT_LINK]),   /* popup menu items */
         EMPTYLIST(Popupitem,defprefs.popupmenu[PUPT_FRAME]),  /* popup menu items */
      },
      EMPTYLIST(Userkey,defprefs.keys),      /* user keys */
      {  {NULL},{NULL},{NULL},{NULL},{NULL}, /* navigation buttons */
         {NULL},{NULL},{NULL},{NULL},{NULL}
      },
   },
   {  /* network prefs */
      LOADIMG_ALL,                           /* load images */
      DEFMAXCONNECT,                         /* max connections */
      2,                                     /* max disk read */
#ifdef NETDEMO
      "file:///AWeb:docs/full.html#full",
#elif defined(LOCALONLY)
      "file:///",     /* home url */
#else
      "about:home",     /* home url */
#endif
      "index.html",                          /* local index */
      "http://www.boingsearch.com/?q=%s",   /* search url */
      "",                                    /* spoof ID */
      FALSE,                                 /* ignore mime */
      TRUE,                                  /* start home page */
      NDTRUE,                                /* auto search */
      FALSE,                                 /* continuous animation */
      FALSE,                                 /* restrict images to same host */
      NULL,NULL,                             /* telnet cmd,arg */
      NULL,NULL,                             /* starttcp cmd,arg */
      NULL,NULL,                             /* endtcp cmd,arg */
      NULL,                                  /* http proxy */
      NULL,                                  /* ftp proxy */
      NULL,                                  /* gopher proxy */
      NULL,                                  /* telnet proxy */
      FALSE,                                 /* limit proxy */
      FALSE,                                 /* passive FTP */
      EMPTYLIST(Noproxy,defprefs.noproxy),   /* no proxy sites */
      "AWeb:Cache",                          /* cache path */
      1024,10240,                            /* memsize, disksize */
      100,0,                                 /* min free chip,fast */
      CAVERIFY_ONCE,                         /* verification mode */
      TRUE,                                  /* fast response */
      EMPTYLIST(Nocache,defprefs.nocache),   /* no cache sites */
      TRUE,                                  /* referer */
      FALSE,                                 /* form warn */
      TRUE,                                  /* spam block */
      TRUE,                                  /* ftp email address */
      COOKIES_QUIET,                           /* use cookies */
      TRUE,                                  /* RFC 2109 cookies */
      EMPTYLIST(Nocookie,defprefs.nocookie), /* no cookie domains */
      "",                                    /* email address */
      "",                                    /* reply address */
      "",                                    /* full name */
      "",                                    /* organization */
      "",                                    /* smtp host */
      "",                                    /* nntp host */
      "",                                    /* signature file */
      "",                                    /* news auth username */
      "",                                    /* news auth password */
      "On %d you wrote about %s:",           /* mail quote header */
      "On %d, %n wrote about %s:",           /* news quote header */
      FALSE,                                 /* external mailer */
      NULL,NULL,                             /* mailto cmd,arg */
      FALSE,                                 /* external newsreader */
      NULL,NULL,                             /* news cmd,arg */
      0,                                     /* max articles news */
      TRUE,                                  /* framed news */
      TRUE,                                  /* sorted news */
      FALSE,                                 /* long headers news */
      FALSE,                                 /* proportional news */
      FALSE,                                 /* news by article number */
      
      FALSE,            /* cachelocalhost */
   },
   {  /* window prefs */
      0,0,9999,9999,                         /* browser window */
      0,0,100,100,                           /* alternate size */
      9999,50,200,100,                       /* netstat window */
      0,0,0,0,                               /* hotlist viewer */
      0,0,0,0,                               /* hotlist manager */
      0,0,0,0,                               /* history window */
      0,0,0,0,                               /* cachebrowser window */
      0,0,0,0,                               /* authorization edit window */
      0,0,0,0,                               /* info window */
      0,0,0,0,                               /* open file requesters */
      0,0,0,0,                               /* save file requesters */
   }
};

#ifdef LOCALONLY
UBYTE configname[32]="AWebView";
#else
UBYTE configname[32]="";
#endif

enum SAVEFORMATS
{  SVF_SHORT=1,SVF_LONG,SVF_COLOR,
   SVF_STRING,SVF_NLSTR,
   SVF_FONT,SVF_ALIAS,SVF_STYLE,
   SVF_SCRM,SVF_PALL,SVF_DPEN,
   SVF_WINDOW,SVF_MIME,SVF_MENU,SVF_NOCACH,SVF_NOPROX,SVF_NOCOOK,
   SVF_BUTTON,SVF_POPUP,SVF_KEY,SVF_NAV
};

struct Saveformat
{  UBYTE *label;
   short format;
   short savepass;
   short offset;
};

/* savepass field determines in which pass this item is saved.
 * It is read in either pass. Separate passes may use different
 * file handles. */

#define SEPARATOR    0x7f

#define OFFSET(s,e) (long)(&((struct s *)NULL)->e)

static struct Saveformat browsersave[]=
{  "FON1",SVF_FONT,  0,OFFSET(Browserprefs,font[0][0]),
   "FON2",SVF_FONT,  0,OFFSET(Browserprefs,font[0][1]),
   "FON3",SVF_FONT,  0,OFFSET(Browserprefs,font[0][2]),
   "FON4",SVF_FONT,  0,OFFSET(Browserprefs,font[0][3]),
   "FON5",SVF_FONT,  0,OFFSET(Browserprefs,font[0][4]),
   "FON6",SVF_FONT,  0,OFFSET(Browserprefs,font[0][5]),
   "FON7",SVF_FONT,  0,OFFSET(Browserprefs,font[0][6]),
   "FOF1",SVF_FONT,  0,OFFSET(Browserprefs,font[1][0]),
   "FOF2",SVF_FONT,  0,OFFSET(Browserprefs,font[1][1]),
   "FOF3",SVF_FONT,  0,OFFSET(Browserprefs,font[1][2]),
   "FOF4",SVF_FONT,  0,OFFSET(Browserprefs,font[1][3]),
   "FOF5",SVF_FONT,  0,OFFSET(Browserprefs,font[1][4]),
   "FOF6",SVF_FONT,  0,OFFSET(Browserprefs,font[1][5]),
   "FOF7",SVF_FONT,  0,OFFSET(Browserprefs,font[1][6]),
   "FONA",SVF_ALIAS, 0,OFFSET(Browserprefs,aliaslist),
   /* no entry for STYLE_NORMAL - must never change */
   "FSH1",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_H1]),
   "FSH2",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_H2]),
   "FSH3",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_H3]),
   "FSH4",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_H4]),
   "FSH5",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_H5]),
   "FSH6",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_H6]),
   "FSBI",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_BIG]),
   "FSSM",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_SMALL]),
   "FSSB",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_SUB]),
   "FSSP",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_SUP]),
   "FSAD",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_ADDRESS]),
   "FSBQ",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_BLOCKQUOTE]),
   "FSCI",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_CITE]),
   "FSCO",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_CODE]),
   "FSDF",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_DFN]),
   "FSEM",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_EM]),
   "FSKB",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_KBD]),
   "FSPR",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_PRE]),
   "FSSA",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_SAMP]),
   "FSST",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_STRONG]),
   "FSVA",SVF_STYLE, 0,OFFSET(Browserprefs,styles[STYLE_VAR]),
   "CONL",SVF_COLOR, 0,OFFSET(Browserprefs,newlink),
   "COOL",SVF_COLOR, 0,OFFSET(Browserprefs,oldlink),
   "COSL",SVF_COLOR, 0,OFFSET(Browserprefs,selectlink),
   "COBG",SVF_COLOR, 0,OFFSET(Browserprefs,background),
   "COTX",SVF_COLOR, 0,OFFSET(Browserprefs,text),
   "SBTP",SVF_SHORT, 0,OFFSET(Browserprefs,screenpens),
   "HTML",SVF_SHORT, 0,OFFSET(Browserprefs,htmlmode),
   "ULLK",SVF_SHORT, 0,OFFSET(Browserprefs,ullink),
   "DOCO",SVF_SHORT, 0,OFFSET(Browserprefs,docolors),
   "BGSO",SVF_SHORT, 0,OFFSET(Browserprefs,dobgsound),
   "BLNK",SVF_SHORT, 0,OFFSET(Browserprefs,blinkrate),
   "DOJS",SVF_SHORT, 0,OFFSET(Browserprefs,dojs),
   "DOFR",SVF_SHORT, 0,OFFSET(Browserprefs,doframes),
   "INCT",SVF_SHORT, 0,OFFSET(Browserprefs,inctable),
   "NFRH",SVF_SHORT, 0,OFFSET(Browserprefs,nominalframe),
   "JSER",SVF_SHORT, 0,OFFSET(Browserprefs,jserrors),
   "NBAN",SVF_SHORT, 0,OFFSET(Browserprefs,nobanners),
   "TTIP",SVF_SHORT, 0,OFFSET(Browserprefs,tooltips),
   "HAND",SVF_SHORT, 0,OFFSET(Browserprefs,handpointer),
   "JWTC",SVF_SHORT, 0,OFFSET(Browserprefs,jswatch),
   "MIMD",SVF_MIME,  0,
   NULL,0,0,
};

static struct Saveformat programsave[]=
{  "SAVE",SVF_STRING,0,OFFSET(Programprefs,savepath),
   "TEMP",SVF_STRING,0,OFFSET(Programprefs,temppath),
   "OVLP",SVF_LONG,  0,OFFSET(Programprefs,overlap),
   "CMDS",SVF_SHORT, 0,OFFSET(Programprefs,commands),
   "HLAC",SVF_SHORT, 0,OFFSET(Programprefs,hlautoclose),
   "HLRQ",SVF_SHORT, 0,OFFSET(Programprefs,hlrequester),
   "AAHL",SVF_SHORT, 0,OFFSET(Programprefs,aahotlist),
   "WHAC",SVF_SHORT, 0,OFFSET(Programprefs,whautoclose),
#ifndef DEMOVERSION
   "SAVI",SVF_SHORT, 0,OFFSET(Programprefs,saveicons),
   "DRAG",SVF_SHORT, 0,OFFSET(Programprefs,clipdrag),
#endif
   "AAWH",SVF_SHORT, 0,OFFSET(Programprefs,aawinhis),
   "AANS",SVF_SHORT, 0,OFFSET(Programprefs,aanetstat),
   "CENR",SVF_SHORT, 0,OFFSET(Programprefs,centerreq),
   "SCRT",SVF_SHORT, 0,OFFSET(Programprefs,screentype),
   "VIWC",SVF_STRING,0,OFFSET(Programprefs,viewcmd),
   "VIWA",SVF_STRING,0,OFFSET(Programprefs,viewargs),
   "EDTC",SVF_STRING,0,OFFSET(Programprefs,editcmd),
   "EDTA",SVF_STRING,0,OFFSET(Programprefs,editargs),
   "IMVC",SVF_STRING,0,OFFSET(Programprefs,imgvcmd),
   "IMVA",SVF_STRING,0,OFFSET(Programprefs,imgvargs),
   "CONS",SVF_STRING,0,OFFSET(Programprefs,console),
   "STUP",SVF_STRING,0,OFFSET(Programprefs,startupscript),
   "SHDN",SVF_STRING,0,OFFSET(Programprefs,shutdownscript),
   "SCRM",SVF_SCRM,  0,0,
   "SCRN",SVF_STRING,0,OFFSET(Programprefs,screenname),
   "SCRP",SVF_SHORT, 0,OFFSET(Programprefs,loadpalette),
   "PALL",SVF_PALL,  0,0,
   "DPEN",SVF_DPEN,  0,0,
/*
   "4COL",SVF_SHORT, 0,OFFSET(Programprefs,fourcolors),
*/
   NULL,0,0,
};

static struct Saveformat guisave[]=
{  "PUKY",SVF_SHORT, 0,OFFSET(Guiprefs,popupkey),
   "UBUS",SVF_SHORT, 0,OFFSET(Guiprefs,showbuttons),
   "NAVS",SVF_SHORT, 0,OFFSET(Guiprefs,shownav),
#ifndef DEMOVERSION
   "MENU",SVF_MENU,  0,OFFSET(Guiprefs,menus),
#endif
   "UBUT",SVF_BUTTON,0,OFFSET(Guiprefs,buttons),
   "PUPI",SVF_POPUP, 0,OFFSET(Guiprefs,popupmenu[PUPT_IMAGE]),
   "PUPL",SVF_POPUP, 0,OFFSET(Guiprefs,popupmenu[PUPT_LINK]),
   "PUPF",SVF_POPUP, 0,OFFSET(Guiprefs,popupmenu[PUPT_FRAME]),
#ifndef DEMOVERSION
   "UKEY",SVF_KEY,   0,OFFSET(Guiprefs,keys),
   "NAVB",SVF_NAV,   0,OFFSET(Guiprefs,navs),
#endif
   NULL,0,0,
};

static struct Saveformat networksave[]=
{  "LIMG",SVF_SHORT, 0,OFFSET(Networkprefs,loadimg),
   "PARI",SVF_LONG,  0,OFFSET(Networkprefs,maxconnect),
   "MAXD",SVF_LONG,  0,OFFSET(Networkprefs,maxdiskread),
   "HOME",SVF_STRING,0,OFFSET(Networkprefs,homeurl),
   "LOIX",SVF_STRING,0,OFFSET(Networkprefs,localindex),
#ifndef DEMOVERSION
   "SRCH",SVF_STRING,0,OFFSET(Networkprefs,searchurl),
   "SPOF",SVF_STRING,0,OFFSET(Networkprefs,spoofid),
#endif
   "IGSM",SVF_SHORT, 0,OFFSET(Networkprefs,ignoremime),
   "STHP",SVF_SHORT, 0,OFFSET(Networkprefs,starthomepage),
#ifndef DEMOVERSION
   "ASRH",SVF_SHORT, 0,OFFSET(Networkprefs,autosearch),
#endif
   "CANI",SVF_SHORT, 0,OFFSET(Networkprefs,contanim),
#ifndef DEMOVERSION
   "RSTI",SVF_SHORT, 0,OFFSET(Networkprefs,restrictimages),
   "TLNC",SVF_NLSTR, 0,OFFSET(Networkprefs,telnetcmd),
   "TLNA",SVF_NLSTR, 0,OFFSET(Networkprefs,telnetargs),
   "TCPC",SVF_NLSTR, 0,OFFSET(Networkprefs,starttcpcmd),
   "TCPA",SVF_NLSTR, 0,OFFSET(Networkprefs,starttcpargs),
   "TCEC",SVF_NLSTR, 0,OFFSET(Networkprefs,endtcpcmd),
   "TCEA",SVF_NLSTR, 0,OFFSET(Networkprefs,endtcpargs),
#endif
   "HTPX",SVF_NLSTR, 0,OFFSET(Networkprefs,httpproxy),
   "FTPX",SVF_NLSTR, 0,OFFSET(Networkprefs,ftpproxy),
   "GOPX",SVF_NLSTR, 0,OFFSET(Networkprefs,gopherproxy),
   "TEPX",SVF_NLSTR, 0,OFFSET(Networkprefs,telnetproxy),
   "LMPX",SVF_SHORT, 0,OFFSET(Networkprefs,limitproxy),
   "PFTP",SVF_SHORT, 0,OFFSET(Networkprefs,passiveftp),
#ifndef DEMOVERSION
   "NOPR",SVF_NOPROX,0,OFFSET(Networkprefs,noproxy),
#endif
   "CAPA",SVF_STRING,0,OFFSET(Networkprefs,cachepath),
   "CAME",SVF_LONG,  0,OFFSET(Networkprefs,camemsize),
   "CADI",SVF_LONG,  0,OFFSET(Networkprefs,cadisksize),
   "FREC",SVF_LONG,  0,OFFSET(Networkprefs,minfreechip),
   "FREF",SVF_LONG,  0,OFFSET(Networkprefs,minfreefast),
   "CAVE",SVF_SHORT, 0,OFFSET(Networkprefs,caverify),
   "CAFR",SVF_SHORT, 0,OFFSET(Networkprefs,fastresponse),
#ifndef DEMOVERSION
   "NOCA",SVF_NOCACH,0,OFFSET(Networkprefs,nocache),
#endif
   "REFE",SVF_SHORT, 0,OFFSET(Networkprefs,referer),
#ifndef DEMOVERSION
   "FOWA",SVF_SHORT, 0,OFFSET(Networkprefs,formwarn),
#endif
   "SPAM",SVF_SHORT, 0,OFFSET(Networkprefs,spamblock),
   "FTPE",SVF_SHORT, 0,OFFSET(Networkprefs,ftpemailaddr),
   "COOK",SVF_SHORT, 0,OFFSET(Networkprefs,cookies),
   "RFCC",SVF_SHORT, 0,OFFSET(Networkprefs,rfc2109),
#ifndef DEMOVERSION
   "NOCO",SVF_NOCOOK,1,OFFSET(Networkprefs,nocookie),
#endif
   "MNAD",SVF_STRING,0,OFFSET(Networkprefs,emailaddr),
   "MNRE",SVF_STRING,0,OFFSET(Networkprefs,replyaddr),
   "MNFN",SVF_STRING,0,OFFSET(Networkprefs,fullname),
   "MNOR",SVF_STRING,0,OFFSET(Networkprefs,organization),
   "MNSH",SVF_STRING,0,OFFSET(Networkprefs,smtphost),
   "MNNH",SVF_STRING,0,OFFSET(Networkprefs,nntphost),
   "MNSI",SVF_STRING,0,OFFSET(Networkprefs,sigfile),
   "MNAU",SVF_STRING,0,OFFSET(Networkprefs,newsauthuser),
   "MNAP",SVF_STRING,0,OFFSET(Networkprefs,newsauthpass),
   "MNMQ",SVF_STRING,0,OFFSET(Networkprefs,mailquotehdr),
   "MNNQ",SVF_STRING,0,OFFSET(Networkprefs,newsquotehdr),
   "MNEM",SVF_SHORT, 0,OFFSET(Networkprefs,extmailer),
   "MLTC",SVF_NLSTR, 0,OFFSET(Networkprefs,mailtocmd),
   "MLTA",SVF_NLSTR, 0,OFFSET(Networkprefs,mailtoargs),
   "MNEN",SVF_SHORT, 0,OFFSET(Networkprefs,extnewsreader),
   "NWSC",SVF_NLSTR, 0,OFFSET(Networkprefs,newscmd),
   "NWSA",SVF_NLSTR, 0,OFFSET(Networkprefs,newsargs),
   "MNMA",SVF_LONG,  0,OFFSET(Networkprefs,maxartnews),
   "MNFR",SVF_SHORT, 0,OFFSET(Networkprefs,framednews),
   "MNSO",SVF_SHORT, 0,OFFSET(Networkprefs,sortednews),
   "MNLO",SVF_SHORT, 0,OFFSET(Networkprefs,longhdrnews),
   "MNPR",SVF_SHORT, 0,OFFSET(Networkprefs,propnews),
   "MNNN",SVF_SHORT, 0,OFFSET(Networkprefs,newsbynum),
#ifdef DEVELOPER
   "CALH",SVF_SHORT, 0,OFFSET(Networkprefs,cachelocalhost),
#endif
   NULL,0,0,
};

static struct Saveformat windowsave[]=
{  "WIND",SVF_WINDOW,0,OFFSET(Windowprefs,winx),
   "WINA",SVF_WINDOW,0,OFFSET(Windowprefs,wiax),
   "NWSD",SVF_WINDOW,0,OFFSET(Windowprefs,nwsx),
   "HOTD",SVF_WINDOW,0,OFFSET(Windowprefs,hotx),
   "HOMD",SVF_WINDOW,0,OFFSET(Windowprefs,homx),
   "WHID",SVF_WINDOW,0,OFFSET(Windowprefs,whisx),
   "CBRD",SVF_WINDOW,0,OFFSET(Windowprefs,cabrx),
   "AUTD",SVF_WINDOW,0,OFFSET(Windowprefs,autx),
#ifndef DEMOVERSION
   "INFD",SVF_WINDOW,0,OFFSET(Windowprefs,infx),
#endif
   "OPEN",SVF_WINDOW,0,OFFSET(Windowprefs,openx),
   "SAVE",SVF_WINDOW,0,OFFSET(Windowprefs,savex),
   NULL,0,0,
};

static struct Saveformat settingssave[]=
{  "BRWW",SVF_WINDOW,0,OFFSET(Settingsprefs,brwx),
   "PRGW",SVF_WINDOW,0,OFFSET(Settingsprefs,prgx),
   "GUIW",SVF_WINDOW,0,OFFSET(Settingsprefs,guix),
   "NETW",SVF_WINDOW,0,OFFSET(Settingsprefs,netx),
   NULL,0,0,
};

static UBYTE *mdrivername[]=
{  "NONE","INT","PLI","EXT","EXP","SAV",NULL,
};

static UBYTE iobuf[512],parambuf[256],param2buf[8];

/*---------------------------------------------------------------------------*/

/* Load prefs from file (fh) into prefs (prf) after (svf) */
static void Loadprefs(struct Saveformat *svf,long fh,void *prf)
{  struct Saveformat *s;
   UBYTE *p;
   void *item;
   long i;
   BOOL menus=FALSE,buttons=FALSE,popup=FALSE,keys=FALSE,alias=FALSE,mime=FALSE,
      nocache=FALSE,noproxy=FALSE,nocookie=FALSE,imagemime=FALSE;
   while(FGets(fh,iobuf,512))
   {  p=iobuf+strlen(iobuf)-1;
      if(*p=='\n') *p='\0';
      for(s=svf;s->format;s++)
      {  if(STRNEQUAL(iobuf,s->label,4)) break;
      }
      item=(char *)prf+s->offset;
      switch(s->format)
      {  case SVF_SHORT:
            sscanf(iobuf+4," %hd",item);
            break;
         case SVF_LONG:
            sscanf(iobuf+4," %ld",item);
            break;
         case SVF_COLOR:
            {  struct Colorprefs *cp=(struct Colorprefs *)item;
               sscanf(iobuf+4," %lx %lx %lx",
                  &cp->red,&cp->green,&cp->blue);
            }
            break;
         case SVF_STRING:
            {  UBYTE **ptr=(UBYTE **)item;
               if(p=Dupstr(iobuf+5,strlen(iobuf+5)))
               {  if(*ptr) FREE(*ptr);
                  *ptr=p;
               }
            }
            break;
         case SVF_NLSTR:
            {  UBYTE **ptr=(UBYTE **)item;
               if(*ptr) FREE(*ptr);
               *ptr=Dupstr(iobuf+5,strlen(iobuf+5));
            }
            break;
         case SVF_FONT:
            {  struct Fontprefs *fp=(struct Fontprefs *)item;
               UBYTE *q,*r;
               for(q=iobuf+4;isspace(*q);q++);
               p=q;
               while(r=strchr(p+1,' ')) p=r;
               sscanf(p," %hd",&fp->fontsize);
               while(isspace(*p)) p--;
               if(p=Dupstr(q,p-q+1))
               {  if(fp->fontname) FREE(fp->fontname);
                  fp->fontname=p;
               }
            }
            break;
         case SVF_ALIAS:
            {  struct Browserprefs *bp=(struct Browserprefs *)prf;
               struct Fontalias *fa;
               UBYTE *aname,*pfn,*fname,*pfsize;
               long fn=0,fsize=8;
               if(!alias)
               {  while(fa=REMHEAD(&bp->aliaslist)) Freefontalias(fa);
               }
               alias=TRUE;
               for(p=iobuf+4;isspace(*p);p++);
               aname=p;
               if(p=strchr(p+1,SEPARATOR))
               {  *p='\0';
                  pfn=p+1;
                  if(p=strchr(pfn,SEPARATOR))
                  {  *p='\0';
                     fname=p+1;
                     if(p=strchr(fname,SEPARATOR))
                     {  *p='\0';
                        pfsize=p+1;
                        sscanf(pfn,"%ld",&fn);
                        sscanf(pfsize,"%ld",&fsize);
                        if(fn>0 && fn<=NRFONTS)
                        {  fn--;
                           if(fa=Addfontalias(&bp->aliaslist,aname))
                           {  fa->fp[fn].fontname=Dupstr(fname,-1);
                              fa->fp[fn].fontsize=fsize;
                           }
                        }
                     }
                  }
               }
            }
            break;
         case SVF_STYLE:
            {  struct Styleprefs *sp=(struct Styleprefs *)item;
               sscanf(iobuf+4," %hd %hd %s",&sp->fonttype,&sp->fontsize,param2buf);
               sp->style=0;
               for(p=param2buf;*p;p++)
               {  if(*p=='B') sp->style|=FSF_BOLD;
                  else if(*p=='I') sp->style|=FSF_ITALIC;
                  else if(*p=='U') sp->style|=FSF_UNDERLINED;
               }
            }
            break;
         case SVF_SCRM:
            {  struct Programprefs *pp=(struct Programprefs *)prf;
               sscanf(iobuf+4," %lx %hd %hd %hd",&pp->screenmode,
                  &pp->screendepth,
                  &pp->screenwidth,&pp->screenheight);
            }
            break;
         case SVF_PALL:
            {  struct Programprefs *pp=(struct Programprefs *)prf;
               sscanf(iobuf+4," %ld",&i);
               sscanf(iobuf+6," %lx %lx %lx",&pp->scrpalette[3*i],
                  &pp->scrpalette[3*i+1],&pp->scrpalette[3*i+2]);
            }
            break;
         case SVF_DPEN:
            {  struct Programprefs *pp=(struct Programprefs *)prf;
               sscanf(iobuf+4,
                  " %hd %hd %hd %hd %hd %hd %hd %hd %hd %hd %hd %hd",
                  &pp->scrdrawpens[0],&pp->scrdrawpens[1],
                  &pp->scrdrawpens[2],&pp->scrdrawpens[3],
                  &pp->scrdrawpens[4],&pp->scrdrawpens[5],
                  &pp->scrdrawpens[6],&pp->scrdrawpens[7],
                  &pp->scrdrawpens[8],&pp->scrdrawpens[9],
                  &pp->scrdrawpens[10],&pp->scrdrawpens[11]);
            }
            break;
         case SVF_WINDOW:
            {  short *ptr=(short *)item;
               sscanf(iobuf+4," %hd %hd %hd %hd",
                  &ptr[0],&ptr[1],&ptr[2],&ptr[3]);
            }
            break;
         case SVF_MIME:
            {  struct Browserprefs *bp=(struct Browserprefs *)prf;
               UBYTE *type,*subt,*exts,*drv,*cmd,*args;
               USHORT driver;
               struct Mimeinfo *mi;
               if(!mime)
               {  while(mi=REMHEAD(&bp->mimelist)) Freemimeinfo(mi);
               }
               mime=TRUE;
               type=iobuf+5;
               if(p=strchr(type,'/'))
               {  *p='\0';
                  subt=p+1;
                  if(p=strchr(subt,';'))
                  {  *p='\0';
                     exts=p+1;
                     if(p=strchr(exts,';'))
                     {  *p='\0';
                        drv=p+1;
                        if(p=strchr(drv,';'))
                        {  *p='\0';
                           cmd=p+1;
                           if(p=strchr(cmd,';'))
                           {  *p='\0';
                              args=p+1;
                              for(driver=0;mdrivername[driver];driver++)
                              {  if(STRNIEQUAL(mdrivername[driver],drv,3)) break;
                              }
                              if(!mdrivername[driver]) driver=0;
                              Addmimeinfo(&bp->mimelist,type,subt,exts,driver,cmd,args);
                           }
                        }
                     }
                  }
               }
            }
            break;
         case SVF_BUTTON:
            {  struct Guiprefs *gp=(struct Guiprefs *)prf;
               UBYTE *label,*cmd;
               struct Userbutton *ub;
               if(!buttons)
               {  while(ub=REMHEAD(&gp->buttons)) Freeuserbutton(ub);
               }
               buttons=TRUE;
               label=iobuf+5;
               if(p=strchr(label,SEPARATOR))
               {  *p='\0';
                  cmd=p+1;
                  Adduserbutton(&gp->buttons,label,cmd);
               }
            }
            break;
         case SVF_POPUP:
            {  struct Guiprefs *gp=(struct Guiprefs *)prf;
               LIST(Popupitem) *list=item;
               UBYTE *flagsp,*title,*cmd;
               struct Popupitem *pi;
               short i;
               USHORT flags;
               if(!popup)
               {  for(i=0;i<NRPOPUPMENUS;i++)
                  {  while(pi=REMHEAD(&gp->popupmenu[i])) Freepopupitem(pi);
                  }
               }
               popup=TRUE;
               flagsp=iobuf+5;
               if(p=strchr(flagsp,SEPARATOR))
               {  *p='\0';
                  sscanf(flagsp,"%hd",&flags);
                  title=p+1;
                  if(p=strchr(title,SEPARATOR))
                  {  *p='\0';
                     cmd=p+1;
                     Addpopupitem(list,flags,title,cmd);
                  }
               }
            }
            break;
#ifndef DEMOVERSION
         case SVF_MENU:
            {  struct Guiprefs *gp=(struct Guiprefs *)prf;
               UBYTE *cmd=NULL,*title=NULL;
               USHORT type;
               UBYTE scut='\0';
               struct Menuentry *me;
               if(!menus)
               {  while(me=REMHEAD(&gp->menus)) Freemenuentry(me);
               }
               menus=TRUE;
               p=iobuf+5;
               switch(*p)
               {  case 'M':type=AMENU_MENU;break;
                  case 'I':type=AMENU_ITEM;break;
                  case 'S':type=AMENU_SUB;break;
                  default: type=AMENU_SEPARATOR;
               }
               p++;
               if(p[0]=='/')
               {  scut=p[1];
                  p+=2;
               }
               if(*p==';')
               {  p++;
                  cmd=p;
                  if(p=strchr(cmd,';'))
                  {  *p='\0';
                     title=p+1;
                  }
               }
               Addmenuentry(&gp->menus,type,title,scut,cmd);
            }
            break;
         case SVF_NAV:
            {  struct Guiprefs *gp=(struct Guiprefs *)prf;
               long n=atoi(iobuf+5);
               p=strchr(iobuf+5,' ');
               if(n>=0 && n<NRNAVBUTTONS && p)
               {  while(*p==' ') p++;
                  if(p=Dupstr(p,-1))
                  {  if(gp->navs[n].cmd) FREE(gp->navs[n].cmd);
                     gp->navs[n].cmd=p;
                  }
               }
            }
            break;
         case SVF_KEY:
            {  struct Guiprefs *gp=(struct Guiprefs *)prf;
               UBYTE *keyp,*cmd;
               USHORT key;
               struct Userkey *uk;
               if(!keys)
               {  while(uk=REMHEAD(&gp->keys)) Freeuserkey(uk);
               }
               keys=TRUE;
               keyp=iobuf+5;
               if(p=strchr(keyp,' '))
               {  *p='\0';
                  sscanf(keyp,"%hx",&key);
                  cmd=p+1;
                  Adduserkey(&gp->keys,key,cmd);
               }
            }
            break;
         case SVF_NOCACH:
            {  struct Networkprefs *np=(struct Networkprefs *)prf;
               struct Nocache *nc;
               if(!nocache)
               {  while(nc=REMHEAD(&np->nocache)) Freenocache(nc);
               }
               nocache=TRUE;
               Addnocache(&np->nocache,iobuf+5);
            }
            break;
         case SVF_NOPROX:
            {  struct Networkprefs *np=(struct Networkprefs *)prf;
               struct Noproxy *nc;
               if(!noproxy)
               {  while(nc=REMHEAD(&np->noproxy)) Freenoproxy(nc);
               }
               noproxy=TRUE;
               Addnoproxy(&np->noproxy,iobuf+5);
            }
            break;
         case SVF_NOCOOK:
            {  struct Networkprefs *np=(struct Networkprefs *)prf;
               struct Nocookie *nc;
               if(!nocookie)
               {  while(nc=REMHEAD(&np->nocookie)) Freenocookie(nc);
               }
               nocookie=TRUE;
               Addnocookie(&np->nocookie,iobuf+5);
            }
            break;
         default:
            if(STRNIEQUAL(iobuf,"MIME",4) && svf==browsersave)   /* Auto convert to MIMD */
            {  struct Browserprefs *bp=(struct Browserprefs *)prf;
               UBYTE *type,*subt,*exts,*cmd,*args;
               USHORT driver;
               struct Mimeinfo *mi;
               if(!mime)
               {  imagemime=TRUE;
                  while(mi=REMHEAD(&bp->mimelist)) Freemimeinfo(mi);
               }
               mime=TRUE;
               type=iobuf+5;
               if(p=strchr(type,'/'))
               {  *p='\0';
                  subt=p+1;
                  if(p=strchr(subt,';'))
                  {  *p='\0';
                     exts=p+1;
                     if(p=strchr(exts,';'))
                     {  *p='\0';
                        cmd=p+1;
                        if(p=strchr(cmd,';'))
                        {  *p='\0';
                           args=p+1;
                           if(STRIEQUAL(type,"TEXT")
                           && ((STRIEQUAL(subt,"PLAIN") || STRIEQUAL(subt,"HTML"))))
                           {  driver=MDRIVER_INTERNAL;
                              *cmd=*args='\0';
                           }
                           else if(STRIEQUAL(type,"IMAGE"))
                           {  driver=MDRIVER_INTERNAL;
                              *cmd=*args='\0';
                              if(STRIEQUAL(subt,"*")) imagemime=FALSE;
                           }
                           else if(*cmd && *args)
                           {  driver=MDRIVER_EXTERNAL;
                           }
                           else
                           {  driver=MDRIVER_NONE;
                           }
                           Addmimeinfo(&bp->mimelist,type,subt,exts,driver,cmd,args);
                        }
                     }
                  }
               }
            }
            else if(STRNIEQUAL(iobuf,"AREX",4) && svf==guisave) /* Auto convert to MENU */
            {  struct Guiprefs *gp=(struct Guiprefs *)prf;
               UBYTE *cmdp,*macro,*title;
               short cmd=0;
               UBYTE scut='\0';
               UBYTE *buf;
               cmdp=iobuf+5;
               if(p=strchr(cmdp,';'))
               {  *p='\0';
                  sscanf(cmdp,"%hd",&cmd);
                  if(cmd==10) scut='0';
                  else if(cmd) scut='0'+cmd;
                  macro=p+1;
                  if(p=strchr(macro,';'))
                  {  *p='\0';
                     title=p+1;
                     if(macro[0]=='"')
                     {  p=macro+strlen(macro)-1;
                        if(*p=='"') *p='\0';
                        macro++;
                        Addmenuentry(&gp->menus,AMENU_ITEM,title,scut,macro);
                     }
                     else if(buf=ALLOCTYPE(UBYTE,strlen(macro)+8,0))
                     {  strcpy(buf,"RUN ");
                        strcat(buf,macro);
                        Addmenuentry(&gp->menus,AMENU_ITEM,title,scut,buf);
                        FREE(buf);
                     }
                  }
               }
            }
            else if(STRNIEQUAL(iobuf,"_ADD",4))
            {  p=iobuf+5;
               if(STRNIEQUAL(p,"MIMD",4)) mime=TRUE;
               else if(STRNIEQUAL(p,"UBUT",4)) buttons=TRUE;
               else if(STRNIEQUAL(p,"PUPX",4)) popup=TRUE;
               else if(STRNIEQUAL(p,"UKEY",4)) keys=TRUE;
            }
#endif
      }
   }
   if(imagemime)
   {  Addmimeinfo(&((struct Browserprefs *)prf)->mimelist,"image","*",
         "",MDRIVER_INTERNAL,"","");
   }
}

static void Saveprefsfmt(struct Saveformat *svf,long fh,void *prf,short pass)
{  struct Saveformat *s;
   void *item;
   UBYTE *p;
   long i;
   strcpy(iobuf,"** Do not modify this file by hand! **\n");
   Write(fh,iobuf,strlen(iobuf));
   for(s=svf;s->format;s++)
   {  if(s->savepass==pass)
      {  *iobuf='\0';
         item=(char *)prf+s->offset;
         switch(s->format)
         {  case SVF_SHORT:
               sprintf(iobuf,"%s %d\n",s->label,*(short *)item);
               break;
            case SVF_LONG:
               sprintf(iobuf,"%s %d\n",s->label,*(long *)item);
               break;
            case SVF_COLOR:
               {  struct Colorprefs *cp=(struct Colorprefs *)item;
                  sprintf(iobuf,"%s %08X %08X %08X\n",s->label,
                     cp->red,cp->green,cp->blue);
               }
               break;
            case SVF_STRING:
               sprintf(iobuf,"%s %s\n",s->label,*(UBYTE **)item);
               break;
            case SVF_NLSTR:
               p=*(UBYTE **)item;
               if(p) sprintf(iobuf,"%s %s\n",s->label,p);
               break;
            case SVF_FONT:
               {  struct Fontprefs *fp=(struct Fontprefs *)item;
                  sprintf(iobuf,"%s %s %d\n",s->label,fp->fontname,fp->fontsize);
               }
               break;
            case SVF_ALIAS:
               {  struct Browserprefs *bp=(struct Browserprefs *)prf;
                  struct Fontalias *fa;
                  if(ISEMPTY(&bp->aliaslist))
                  {  /* Force defaults to be cleared from list on subsequent load */
                     strcpy(iobuf,"FONA -!-\n");
                  }
                  else
                  {  for(fa=bp->aliaslist.first;fa->next;fa=fa->next)
                     {  if(fa->alias && *fa->alias)
                        {  for(i=0;i<NRFONTS;i++)
                           {  sprintf(iobuf,"FONA %s%c%d%c%s%c%d\n",
                                 fa->alias,SEPARATOR,i+1,SEPARATOR,fa->fp[i].fontname,
                                 SEPARATOR,fa->fp[i].fontsize);
                              Write(fh,iobuf,strlen(iobuf));
                           }
                        }
                     }
                     *iobuf='\0';
                  }
               }
               break;
            case SVF_STYLE:
               {  struct Styleprefs *sp=(struct Styleprefs *)item;
                  *param2buf='\0';
                  if(sp->style&FSF_BOLD) strcat(param2buf,"B");
                  if(sp->style&FSF_ITALIC) strcat(param2buf,"I");
                  if(sp->style&FSF_UNDERLINED) strcat(param2buf,"U");
                  sprintf(iobuf,"%s %d %d .%s\n",s->label,sp->fonttype,sp->fontsize,param2buf);
               }
               break;
            case SVF_SCRM:
               {  struct Programprefs *pp=(struct Programprefs *)prf;
                  sprintf(iobuf,"SCRM %08x %d %d %d\n",pp->screenmode,
                     pp->screendepth,pp->screenwidth,pp->screenheight);
               }
               break;
            case SVF_PALL:
               {  struct Programprefs *pp=(struct Programprefs *)prf;
                  for(i=0;i<8;i++)
                  {  sprintf(iobuf,"PALL %d %08X %08X %08X\n",i,
                        pp->scrpalette[3*i],pp->scrpalette[3*i+1],
                        pp->scrpalette[3*i+2]);
                     Write(fh,iobuf,strlen(iobuf));
                  }
                  *iobuf='\0';
               }
               break;
            case SVF_DPEN:
               {  struct Programprefs *pp=(struct Programprefs *)prf;
                  p=iobuf+sprintf(iobuf,"DPEN");
                  for(i=0;i<12;i++) p+=sprintf(p," %d",pp->scrdrawpens[i]);
                  sprintf(p,"\n");
               }
               break;
            case SVF_WINDOW:
               {  short *ptr=(short *)item;
                  sprintf(iobuf,"%s %d %d %d %d\n",s->label,
                     ptr[0],ptr[1],ptr[2],ptr[3]);
               }
               break;
            case SVF_MIME:
               {  struct Browserprefs *bp=(struct Browserprefs *)prf;
                  struct Mimeinfo *mi;
                  for(mi=bp->mimelist.first;mi->next;mi=mi->next)
                  {  sprintf(iobuf,"MIMD %s/%s;%s;%s;%s;%s\n",mi->type,mi->subtype,
                        mi->extensions?mi->extensions:NULLSTRING,
                        mdrivername[mi->driver],
                        mi->cmd?mi->cmd:NULLSTRING,mi->args?mi->args:NULLSTRING);
                     Write(fh,iobuf,strlen(iobuf));
                  }
                  *iobuf='\0';
               }
               break;
            case SVF_BUTTON:
               {  struct Guiprefs *gp=(struct Guiprefs *)prf;
                  struct Userbutton *ub;
                  for(ub=gp->buttons.first;ub->next;ub=ub->next)
                  {  sprintf(iobuf,"UBUT %s%c%s\n",ub->label,SEPARATOR,ub->cmd);
                     Write(fh,iobuf,strlen(iobuf));
                  }
                  *iobuf='\0';
               }
               break;
            case SVF_POPUP:
               {  LIST(Popupitem) *list=item;
                  struct Popupitem *pi;
                  for(pi=list->first;pi->next;pi=pi->next)
                  {  sprintf(iobuf,"%s %d%c%s%c%s\n",s->label,
                        pi->flags,SEPARATOR,pi->title,SEPARATOR,pi->cmd);
                     Write(fh,iobuf,strlen(iobuf));
                  }
                  *iobuf='\0';
               }
               break;
#ifndef DEMOVERSION
            case SVF_MENU:
               {  struct Guiprefs *gp=(struct Guiprefs *)prf;
                  struct Menuentry *me;
                  for(me=gp->menus.first;me->next;me=me->next)
                  {  p=iobuf+sprintf(iobuf,"MENU %c","MIS-"[me->type]);
                     if(me->scut[0])
                     {  *p++='/';
                        *p++=me->scut[0];
                     }
                     sprintf(p,";%s;%s\n",me->cmd?me->cmd:NULLSTRING,
                        me->title?me->title:NULLSTRING);
                     Write(fh,iobuf,strlen(iobuf));
                  }
                  *iobuf='\0';
               }
               break;
            case SVF_NAV:
               {  struct Guiprefs *gp=(struct Guiprefs *)prf;
                  int n;
                  for(n=0;n<NRNAVBUTTONS;n++)
                  {  sprintf(iobuf,"NAVB %d %s\n",n,gp->navs[n].cmd);
                     Write(fh,iobuf,strlen(iobuf));
                  }
                  *iobuf='\0';
               }
               break;
            case SVF_KEY:
               {  struct Guiprefs *gp=(struct Guiprefs *)prf;
                  struct Userkey *uk;
                  for(uk=gp->keys.first;uk->next;uk=uk->next)
                  {  sprintf(iobuf,"UKEY %04x %s\n",uk->key,uk->cmd);
                     Write(fh,iobuf,strlen(iobuf));
                  }
                  *iobuf='\0';
               }
               break;
            case SVF_NOCACH:
            case SVF_NOPROX:
            case SVF_NOCOOK:
               {  LIST(Nocache) *list=item;
                  struct Nocache *nc;
                  for(nc=list->first;nc->next;nc=nc->next)
                  {  sprintf(iobuf,"%s %s\n",s->label,nc->name);
                     Write(fh,iobuf,strlen(iobuf));
                  }
                  *iobuf='\0';
               }
               break;
#endif
         }
         if(*iobuf) Write(fh,iobuf,strlen(iobuf));
      }
   }
}

static void Makedir(UBYTE *dir)
{  long lock=Lock(dir,SHARED_LOCK);
   if(!lock) lock=CreateDir(dir);
   if(lock) UnLock(lock);
}

static long Openname(BOOL saved,UBYTE *name,UBYTE *file,long mode)
{  if(name) strcpy(iobuf,name);
   else
   {  sprintf(iobuf,"%s:" DEFAULTCFG "%c%s",
         saved?"ENVARC":"ENV",
         *configname?'/':'\0',
         *configname?configname:(UBYTE *)"");
      if(mode==MODE_NEWFILE)
      {  UBYTE *p;
         /* Create ENV:DEFAULTCFG first, then optional subdir. */
         p=strchr(iobuf,'/');
         if(p)
         {  *p='\0';
            Makedir(iobuf);
            *p='/';
         }
         Makedir(iobuf);
      }
      if(file) strcat(iobuf,file);
   }
   return Open(iobuf,mode);
}

/* Copy optional strings (may be NULL) */
static BOOL Copyoptstring(UBYTE *from,UBYTE **to)
{  BOOL ok=TRUE;
   if(*to)
   {  FREE(*to);
      *to=NULL;
   }
   if(from)
   {  if(!(*to=Dupstr(from,-1))) ok=FALSE;
   }
   return ok;
}

static void Adddeffontalias(UBYTE *alias,short size,UBYTE *font,short fsize)
{  struct Fontalias *fa;
   if(fa=Addfontalias(&defprefs.aliaslist,alias))
   {  fa->fp[size].fontname=Dupstr(font,-1);
      fa->fp[size].fontsize=fsize;
   }
}

static void Adddefmenu(USHORT type,ULONG msg,UBYTE *cmd)
{  char *title = NULL;
   UBYTE scut='\0';

   if(msg)
   {
       title = Getmainstr(msg);
   }

   if(!title) title="";
   if(title[0] && title[1]=='/')
   {  scut=title[0];
      title+=2;
   }
   Addmenuentry(&defprefs.menus,type,title,scut,cmd?cmd:NULLSTRING);
}

/*---------------------------------------------------------------------------*/

BOOL Initdefprefs(void)
{  struct Screen *screen;
   struct DrawInfo *dri;
   UBYTE *alias,*font;
   if(screen=LockPubScreen(NULL))
   {  defprefs.screenmode=GetVPModeID(&screen->ViewPort);
      defprefs.screenwidth=screen->Width;
      defprefs.screenheight=screen->Height;
      if(dri=GetScreenDrawInfo(screen))
      {  defprefs.screendepth=dri->dri_Depth;
         FreeScreenDrawInfo(screen,dri);
      }
      UnlockPubScreen(NULL,screen);
   }
   
   /* Serif font aliases - map to CGTimes.font (scalable) */
   alias="Times New Roman,Times,serif";
   font="CGTimes.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   alias="Georgia";
   font="CGTimes.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   alias="Palatino";
   font="CGTimes.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   alias="Garamond";
   font="CGTimes.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   alias="Book Antiqua";
   font="CGTimes.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   /* Sans-serif font aliases - map to CGTriumvirate.font (scalable) */
   alias="Arial,Helvetica,sans-serif";
   font="CGTriumvirate.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   alias="Verdana";
   font="CGTriumvirate.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   alias="Trebuchet MS";
   font="CGTriumvirate.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   alias="Tahoma";
   font="CGTriumvirate.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   alias="Lucida Sans Unicode";
   font="CGTriumvirate.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   alias="Comic Sans MS";
   font="CGTriumvirate.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   alias="Impact";
   font="CGTriumvirate.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   /* Monospace font aliases - map to LetterGothic.font (scalable) */
   alias="Courier New,Courier,monospace";
   font="LetterGothic.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   alias="Lucida Console";
   font="LetterGothic.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   alias="Consolas";
   font="LetterGothic.font";
   Adddeffontalias(alias,0,font,9);
   Adddeffontalias(alias,1,font,11);
   Adddeffontalias(alias,2,font,13);
   Adddeffontalias(alias,3,font,15);
   Adddeffontalias(alias,4,font,18);
   Adddeffontalias(alias,5,font,24);
   Adddeffontalias(alias,6,font,36);
   
   if(!Addmimeinfo(&defprefs.mimelist,
      "text","html","html htm shtml phtml php asp",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "text","plain","txt",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "text","css","css",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "text","javascript","js",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "text","csv","csv",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "text","xml","xml",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "application","rss+xml","rss",
#ifdef OSVERSION
      MDRIVER_PLUGIN,"AWeb:awebplugin/awebrss.awebplugin",""
#else
      MDRIVER_INTERNAL,"",""
#endif
      )) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "application","atom+xml","atom",
#ifdef OSVERSION
      MDRIVER_PLUGIN,"AWeb:awebplugin/awebrss.awebplugin",""
#else
      MDRIVER_INTERNAL,"",""
#endif
      )) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "message","rfc822","eml",
#ifdef OSVERSION
      MDRIVER_PLUGIN,"AWeb:awebplugin/awebeml.awebplugin",""
#else
      MDRIVER_INTERNAL,"",""
#endif
      )) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "application","x-lha","lha",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "application","zip","zip",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "application","x-tar","tar",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "application","gzip","gz",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "application","pdf","pdf",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "application","x-shockwave-flash","swf",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "text","x-aguide","guide",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "text","*","",MDRIVER_EXTERNAL,
      "SYS:Utilities/MultiView","%f pubscreen %n")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "image","gif","gif",
#ifdef OSVERSION
      MDRIVER_PLUGIN,"AWeb:awebplugin/awebgif.awebplugin",""
#else
      MDRIVER_INTERNAL,"",""
#endif
      )) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "image","jpeg","jpg jpeg jpe jfif",
#ifdef OSVERSION
      MDRIVER_PLUGIN,"AWeb:awebplugin/awebjfif.awebplugin",""
#else
      MDRIVER_INTERNAL,"",""
#endif
      )) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "image","x-iff","iff ilbm ham ham8",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "image","png","png",
#ifdef OSVERSION
      MDRIVER_PLUGIN,"AWeb:awebplugin/awebpng.awebplugin",""
#else
      MDRIVER_INTERNAL,"",""
#endif
      )) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "image","xbitmap","xbm",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "image","bmp","bmp",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "image","tiff","tif tiff",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "image","vnd.microsoft.icon","ico",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "image","x-icon","ico",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "image","svg+xml","svg",
#ifdef OSVERSION
      MDRIVER_PLUGIN,"AWeb:awebplugin/awebsvg.awebplugin",""
#else
      MDRIVER_INTERNAL,"",""
#endif
      )) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "image","*","",MDRIVER_INTERNAL,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "video","mpeg","mpg mpeg",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "video","x-msvideo","avi",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "video","quicktime","qt",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "video","x-anim","ani anim",MDRIVER_EXTERNAL,
      "SYS:Utilities/MultiView","%f screen")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "audio","basic","au snd",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "audio","mpeg","mp3 mpga",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "audio","midi","mid midi",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "audio","wav","wav",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "audio","x-8svx","8svx svx",MDRIVER_EXTERNAL,
      "SYS:Utilities/MultiView","%f pubscreen %n")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "audio","x-mod","mod",MDRIVER_NONE,"","")) return FALSE;
   if(!Addmimeinfo(&defprefs.mimelist,
      "audio","*","",MDRIVER_INTERNAL,"","")) return FALSE;

   Adddefmenu(AMENU_MENU,MSG_PROJECT_MENU,NULL);
#ifndef DEMOVERSION
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_NEWWINDOW,"NEW %u");
#endif
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_CLOSEWINDOW,"CLOSE");
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
#ifndef LOCALONLY
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_OPENURL,"OPENREQ");
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_OPENWWW,"URLFIELD SET \"http://www.\" ACTIVATE");
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_OPENNEWS,"OPEN \"news:\"");
#endif
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_OPENLOCAL,"OPENREQ FILE");
#ifndef DEMOVERSION
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_OPENSEARCH,"OPEN \"file:///AWeb:extras/search.html\"");
#endif
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_SEARCH,"SEARCH TARGET %i");
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_SOURCE,"VIEWSOURCE %u");
#ifndef DEMOVERSION
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_EDIT,"EDITSOURCE %u");
#endif
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_SAVEHTML,"SAVEAS");
#ifndef NETDEMO
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_SAVEIFF,"SAVEIFF");
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_PRINT,"PRINT REQUEST");
#endif
#ifndef DEMOVERSION
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_INFO,"INFO");
#endif
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_ABOUT,"SUBWINDOW ABOUT OPEN");
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_ICONIFY,"ICONIFY HIDE");
   Adddefmenu(AMENU_ITEM,MSG_PROJECT_QUIT,"QUIT FORCE");

   Adddefmenu(AMENU_MENU,MSG_CONTROL_MENU,NULL);
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_LOADNOW,NULL);
    Adddefmenu(AMENU_SUB,MSG_CONTROL_LOADNOWALL,"LOADIMAGES");
    Adddefmenu(AMENU_SUB,MSG_CONTROL_LOADNOWMAPS,"LOADIMAGES MAPS");
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_PLAYBGSOUND,"PLAYBGSOUND");
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_RESET,"RESETFRAME");
#ifndef DEMOVERSION
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_COPYURL,"COPYURL");
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_PASTEURL,"URLFIELD PASTE ACTIVATE");
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_COPYBLOCK,"COPYBLOCK");
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_DRAGGING,"@DRAGGING");
#endif
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_NETWORKSTATUS,"SUBWINDOW NETSTATUS OPEN");
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_RLOAD,NULL);
    Adddefmenu(AMENU_SUB,MSG_CONTROL_RLOADDOC,"RELOAD");
    Adddefmenu(AMENU_SUB,MSG_CONTROL_RLOADIMGS,"RELOAD IMAGES");
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_CANCEL,"CANCEL");
#ifndef DEMOVERSION
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_NEXTWINDOW,"WINDOW TOFRONT ACTIVATE NEXT 1");
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_PREVWINDOW,"WINDOW TOFRONT ACTIVATE NEXT -1");
#endif
#ifndef LOCALONLY
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_NOPROXY,"@NOPROXY");
#endif
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_BREAKJS,"@BREAKJS");
#ifndef DEMOVERSION
   Adddefmenu(AMENU_ITEM,MSG_CONTROL_DEBUGJS,"@DEBUGJS");
#endif

   Adddefmenu(AMENU_MENU,MSG_CACHE_MENU,NULL);
   Adddefmenu(AMENU_ITEM,MSG_CACHE_BROWSER,"SUBWINDOW CACHEBROWSER OPEN");
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_CACHE_FLUSH,NULL);
    Adddefmenu(AMENU_SUB,MSG_CACHE_FLSHIMGSND,"FLUSHCACHE IMAGES");
    Adddefmenu(AMENU_SUB,MSG_CACHE_FLSHIMGS,"FLUSHCACHE IMAGES ALL");
    Adddefmenu(AMENU_SUB,MSG_CACHE_FLSHDOCSND,"FLUSHCACHE DOCUMENTS");
#ifndef LOCALONLY
   Adddefmenu(AMENU_ITEM,MSG_CACHE_DELETE,NULL);
    Adddefmenu(AMENU_SUB,MSG_CACHE_DELIMGS,"DELETECACHE IMAGES");
    Adddefmenu(AMENU_SUB,MSG_CACHE_DELDOCS,"DELETECACHE DOCUMENTS");
    Adddefmenu(AMENU_SUB,MSG_CACHE_CLEAR,"DELETECACHE DOCUMENTS IMAGES");
   Adddefmenu(AMENU_ITEM,MSG_CACHE_FIX,"FIXCACHE");
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_CACHE_SAVEAUTH,"SAVEAUTHORIZE");
   Adddefmenu(AMENU_ITEM,MSG_CACHE_EDITAUTH,"SUBWINDOW AUTHORIZE OPEN");
#endif

   Adddefmenu(AMENU_MENU,MSG_NAVIGATE_MENU,NULL);
   Adddefmenu(AMENU_ITEM,MSG_NAVIGATE_BACK,"GO BACK");
   Adddefmenu(AMENU_ITEM,MSG_NAVIGATE_FWD,"GO FORWARD");
   Adddefmenu(AMENU_ITEM,MSG_NAVIGATE_HOME,"GO HOME");
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_NAVIGATE_HISTORY,"SUBWINDOW HISTORY OPEN");

   Adddefmenu(AMENU_MENU,MSG_HOTLIST_MENU,NULL);
   Adddefmenu(AMENU_ITEM,MSG_HOTLIST_ADDHOT,"ADDHOTLIST %u %t");
   Adddefmenu(AMENU_ITEM,MSG_HOTLIST_SHOWHOT,"OPEN \"x-aweb:hotlist\"");
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_HOTLIST_VIEW,"SUBWINDOW HOTVIEWER OPEN");
   Adddefmenu(AMENU_ITEM,MSG_HOTLIST_MGR,"SUBWINDOW HOTMANAGER OPEN");
#ifndef DEMOVERSION
   Adddefmenu(AMENU_ITEM,MSG_HOTLIST_SAVE,"HOTLIST SAVE");
   Adddefmenu(AMENU_ITEM,MSG_HOTLIST_RESTORE,"HOTLIST RESTORE");
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_HOTLIST_AMHOTRX,"OPEN \"x-aweb:amhotlist.rexx\"");
   Adddefmenu(AMENU_ITEM,MSG_HOTLIST_AMHOT20,"OPEN \"x-aweb:amhotlist.20\"");
   Adddefmenu(AMENU_ITEM,MSG_HOTLIST_IBHOT,"@OTHERHOT");
#endif

   Adddefmenu(AMENU_MENU,MSG_SETTINGS_MENU,NULL);
   Adddefmenu(AMENU_ITEM,MSG_SETTINGS_LOADIMG,NULL);
    Adddefmenu(AMENU_SUB,MSG_SETTINGS_LOADIMGALL,"@LOADIMGALL");
    Adddefmenu(AMENU_SUB,MSG_SETTINGS_LOADIMGMAPS,"@LOADIMGMAPS");
    Adddefmenu(AMENU_SUB,MSG_SETTINGS_LOADIMGOFF,"@LOADIMGOFF");
   Adddefmenu(AMENU_ITEM,MSG_SETTINGS_BGIMAGES,"@BGIMAGES");
   Adddefmenu(AMENU_ITEM,MSG_SETTINGS_BGSOUND,"@BGSOUND");
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_SETTINGS_BROWSER,"SYSTEM AWebCfg BROWSER CONFIG %c PUBSCREEN %n");
   Adddefmenu(AMENU_ITEM,MSG_SETTINGS_PROGRAM,"SYSTEM AWebCfg PROGRAM CONFIG %c PUBSCREEN %n");
   Adddefmenu(AMENU_ITEM,MSG_SETTINGS_GUI,"SYSTEM AWebCfg GUI CONFIG %c PUBSCREEN %n");
   Adddefmenu(AMENU_ITEM,MSG_SETTINGS_NETWORK,"SYSTEM AWebCfg NETWORK CONFIG %c PUBSCREEN %n");
#ifndef NEED35
   if(!has35)
   {  Adddefmenu(AMENU_ITEM,MSG_SETTINGS_CLASSACT,"SYSTEM SYS:Prefs/ReAction PUBSCREEN %n");
   }
#endif
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_SETTINGS_SAVEALL,"SAVESETTINGS");
   Adddefmenu(AMENU_ITEM,MSG_SETTINGS_SAVEAS,"SAVESETTINGS REQUEST");
   Adddefmenu(AMENU_ITEM,MSG_SETTINGS_LOAD,"LOADSETTINGS REQUEST");
   Adddefmenu(AMENU_ITEM,MSG_SETTINGS_SNAPSHOT,"SNAPSHOT");

   Adddefmenu(AMENU_MENU,MSG_HELP_MENU,NULL);
   Adddefmenu(AMENU_ITEM,MSG_HELP_HELP,"OPEN file:///AWeb:docs/aweb.html");
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);
   Adddefmenu(AMENU_ITEM,MSG_HELP_AWEBHOME,"OPEN about:home");

   Adddefmenu(AMENU_MENU,MSG_AREXX_MENU,NULL);
   Adddefmenu(AMENU_ITEM,MSG_AREXX_AREXX,"@AREXX");
   Adddefmenu(AMENU_SEPARATOR,NULL,NULL);


#ifndef LOCALONLY
   if(!Adduserbutton(&defprefs.buttons,"AWeb",
      "OPEN http://amigazen.com/aweb/")) return FALSE;
   if(!Adduserbutton(&defprefs.buttons,"Amiga.com",
      "OPEN https://www.amiga.com/")) return FALSE;
   if(!Adduserbutton(&defprefs.buttons,"Aminet",
      "OPEN http://www.aminet.net/")) return FALSE;
   if(!Adduserbutton(&defprefs.buttons,"Amiga-News",
      "OPEN http://www.amiga-news.de/en/")) return FALSE;
   if(!Adduserbutton(&defprefs.buttons,"AmigaWorld",
      "OPEN https://amigaworld.net/")) return FALSE;
#else
   /* LOCALONLY: No network links in predefined buttons */
#endif
#ifdef OSVERSION
   if(!Adduserbutton(&defprefs.buttons,"Modes",
      "RUN AWeb:plugins/awebmodes.awebrx")) return FALSE;
   if(!Adduserbutton(&defprefs.buttons,"DZone",
      "RUN AWeb:plugins/dropzone.awebrx")) return FALSE;
   if(!Adduserbutton(&defprefs.buttons,"HTTX",
      "RUN AWeb:plugins/httx/httxplugincp.awebrx")) return FALSE;
   if(!Adduserbutton(&defprefs.buttons,"ToolBar",
      "RUN AWeb:plugins/toolbar.awebrx")) return FALSE;
#else
#ifdef DEMOVERSION
   if(!Adduserbutton(&defprefs.buttons,"HTTX",
      "REQUEST HTTX \"This HTML to ASCII/ANSI converter*Nis included in the "
      "full version.*N *NComplete with ARexx interface.\" _Ok")) return FALSE;
   if(!Adduserbutton(&defprefs.buttons,"Full version",
      "OPEN file:///AWeb:docs/full.html")) return FALSE;
#endif
#endif
#ifndef LOCALONLY
   /*if(!Adduserbutton(&defprefs.buttons,Getmainstr(MSG_USERBUTTON_CACHE),
      "SUBWINDOW CACHEBROWSER OPEN")) return FALSE;*/
   if(!Adduserbutton(&defprefs.buttons,"Protoweb",
      "RUN AWeb:Docs/protoweb.awebrx")) return FALSE;
   /*
   if(!Adduserbutton(&defprefs.buttons,"News:",
      "OPEN news:")) return FALSE;
   */
#endif
/*
   if(!Adduserbutton(&defprefs.buttons,Getmainstr(MSG_USERBUTTON_CLOCK),
      "SYSTEM SYS:Utilities/Clock pubscreen %n digital format 2 top 0 left 9999")) return FALSE;
*/
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_IMAGE],
      PUPF_INMEM,Getmainstr(MSG_POPUP_FLUSHIMAGE),"FLUSHCACHE URL %u")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_IMAGE],
      PUPF_NOTINMEM,Getmainstr(MSG_POPUP_LOADIMAGE),"LOAD %u")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_IMAGE],
      PUPF_INMEM,Getmainstr(MSG_POPUP_RELOADIMAGE),"LOAD %u RELOAD")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_IMAGE],
      PUPF_NOTINMEM,Getmainstr(MSG_POPUP_DOWNLOADIMAGE),"LOAD %u SAVEREQ")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_IMAGE],
      PUPF_INMEM,Getmainstr(MSG_POPUP_SAVEIMAGE),"LOAD %u SAVEREQ")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_IMAGE],
      PUPF_INMEM|PUPF_NOTINMEM,Getmainstr(MSG_POPUP_SHOWIMAGE),"OPEN %u")) return FALSE;

   if(!Addpopupitem(&defprefs.popupmenu[PUPT_LINK],
      PUPF_INMEM|PUPF_NOTINMEM,Getmainstr(MSG_POPUP_OPENLINK),"OPEN %u TARGET %i")) return FALSE;
#ifndef DEMOVERSION
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_LINK],
      PUPF_INMEM|PUPF_NOTINMEM,Getmainstr(MSG_POPUP_OPENLINKNW),"NEW %u")) return FALSE;
#endif
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_LINK],
      PUPF_NOTINMEM,Getmainstr(MSG_POPUP_LOADLINK),"LOAD %u")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_LINK],
      PUPF_NOTINMEM,Getmainstr(MSG_POPUP_DOWNLOADLINK),"LOAD %u SAVEREQ")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_LINK],
      PUPF_INMEM,Getmainstr(MSG_POPUP_SAVELINK),"LOAD %u SAVEREQ")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_LINK],
      PUPF_INMEM|PUPF_NOTINMEM,Getmainstr(MSG_POPUP_ADDLINK),"ADDHOTLIST %u %t")) return FALSE;

   if(!Addpopupitem(&defprefs.popupmenu[PUPT_FRAME],
      0,Getmainstr(MSG_POPUP_RELOADFRAME),"RELOAD TARGET %i")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_FRAME],
      0,Getmainstr(MSG_POPUP_SAVEFRAME),"SAVEAS TARGET %i")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_FRAME],
      0,Getmainstr(MSG_POPUP_VIEWFRAME),"VIEWSOURCE %u")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_FRAME],
      0,Getmainstr(MSG_POPUP_SHOWFRAME),"OPEN %u")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_FRAME],
      0,Getmainstr(MSG_POPUP_SEARCHFRAME),"SEARCH TARGET %i")) return FALSE;
   if(!Addpopupitem(&defprefs.popupmenu[PUPT_FRAME],
      0,Getmainstr(MSG_POPUP_INFOFRAME),"INFO TARGET %i")) return FALSE;

   Adduserkey(&defprefs.keys,0x45,"CANCEL");
   Adduserkey(&defprefs.keys,0x41,"SCROLL PAGE UP TARGET %i");
   Adduserkey(&defprefs.keys,0x40,"SCROLL PAGE DOWN TARGET %i");
   Adduserkey(&defprefs.keys,0x4c,"SCROLL 8 UP TARGET %i");
   Adduserkey(&defprefs.keys,0x4d,"SCROLL 8 DOWN TARGET %i");
   Adduserkey(&defprefs.keys,0x4e,"SCROLL 8 RIGHT TARGET %i");
   Adduserkey(&defprefs.keys,0x4f,"SCROLL 8 LEFT TARGET %i");
   Adduserkey(&defprefs.keys,0x4c|UKEY_SHIFT,"SCROLL PAGE UP TARGET %i");
   Adduserkey(&defprefs.keys,0x4d|UKEY_SHIFT,"SCROLL PAGE DOWN TARGET %i");
   Adduserkey(&defprefs.keys,0x4e|UKEY_SHIFT,"SCROLL PAGE RIGHT TARGET %i");
   Adduserkey(&defprefs.keys,0x4f|UKEY_SHIFT,"SCROLL PAGE LEFT TARGET %i");
   Adduserkey(&defprefs.keys,0x4c|UKEY_ALT,"SCROLL FAR UP TARGET %i");
   Adduserkey(&defprefs.keys,0x4d|UKEY_ALT,"SCROLL FAR DOWN TARGET %i");
   Adduserkey(&defprefs.keys,0x4e|UKEY_ALT,"GO FORWARD");
   Adduserkey(&defprefs.keys,0x4f|UKEY_ALT,"GO BACK");
   Adduserkey(&defprefs.keys,0x3d,"SCROLL FAR UP TARGET %i");
   Adduserkey(&defprefs.keys,0x3e,"SCROLL 8 UP TARGET %i");
   Adduserkey(&defprefs.keys,0x3f,"SCROLL PAGE UP TARGET %i");
   Adduserkey(&defprefs.keys,0x2d,"SCROLL 8 LEFT TARGET %i");
   Adduserkey(&defprefs.keys,0x2f,"SCROLL 8 RIGHT TARGET %i");
   Adduserkey(&defprefs.keys,0x1d,"SCROLL FAR DOWN TARGET %i");
   Adduserkey(&defprefs.keys,0x1e,"SCROLL 8 DOWN TARGET %i");
   Adduserkey(&defprefs.keys,0x1f,"SCROLL PAGE DOWN TARGET %i");
   Adduserkey(&defprefs.keys,0x5f,"OPEN \"file:///AWeb:docs/aweb.html\"");
   Adduserkey(&defprefs.keys,0x7a,"SCROLL 80 UP TARGET %i");
   Adduserkey(&defprefs.keys,0x7b,"SCROLL 80 DOWN TARGET %i");
   Adduserkey(&defprefs.keys,0x7a|UKEY_SHIFT,"SCROLL PAGE UP TARGET %i");
   Adduserkey(&defprefs.keys,0x7b|UKEY_SHIFT,"SCROLL PAGE DOWN TARGET %i");

   if(!(defprefs.navs[0].cmd=Dupstr("GO BACK",-1))) return FALSE;
   if(!(defprefs.navs[1].cmd=Dupstr("GO FORWARD",-1))) return FALSE;
   if(!(defprefs.navs[2].cmd=Dupstr("GO HOME",-1))) return FALSE;
   if(!(defprefs.navs[3].cmd=Dupstr("ADDHOTLIST %u %t",-1))) return FALSE;
   if(!(defprefs.navs[4].cmd=Dupstr("HOTLIST",-1))) return FALSE;
   if(!(defprefs.navs[5].cmd=Dupstr("CANCEL",-1))) return FALSE;
   if(!(defprefs.navs[6].cmd=Dupstr("SUBWINDOW NETSTATUS OPEN",-1))) return FALSE;
   if(!(defprefs.navs[7].cmd=Dupstr("SEARCH TARGET %i",-1))) return FALSE;
   if(!(defprefs.navs[8].cmd=Dupstr("RELOAD",-1))) return FALSE;
   if(!(defprefs.navs[9].cmd=Dupstr("LOADIMAGES",-1))) return FALSE;

   if(AvailMem(MEMF_CHIP)>300*1024) defprefs.minfreechip=300;
   if(AvailMem(MEMF_FAST)>500*1024) defprefs.minfreefast=500;

   return TRUE;
}

void Freedefprefs(void)
{  void *p;
   short i;
   while(p=REMHEAD(&defprefs.mimelist)) Freemimeinfo(p);
   while(p=REMHEAD(&defprefs.buttons)) Freeuserbutton(p);
   for(i=0;i<NRPOPUPMENUS;i++)
   {  while(p=REMHEAD(&defprefs.popupmenu[i])) Freepopupitem(p);
   }
}

/* Free a menu entry */
void Freemenuentry(struct Menuentry *me)
{  if(me)
   {  if(me->title) FREE(me->title);
      if(me->cmd) FREE(me->cmd);
      FREE(me);
   }
}

/* Add a new menu entry to the list */
struct Menuentry *Addmenuentry(void *list,USHORT type,UBYTE *title,UBYTE scut,UBYTE *cmd)
{  struct Menuentry *me=ALLOCSTRUCT(Menuentry,1,MEMF_PUBLIC|MEMF_CLEAR);
   if(me
   && (me->title=Dupstr(title,-1))
   && (me->cmd=Dupstr(cmd,-1)))
   {  me->type=type;
      me->scut[0]=scut;
      ADDTAIL(list,me);
      return me;
   }
   else Freemenuentry(me);
   return NULL;
}

/* Free a Userbutton */
void Freeuserbutton(struct Userbutton *ub)
{  if(ub)
   {  if(ub->label) FREE(ub->label);
      if(ub->cmd) FREE(ub->cmd);
      FREE(ub);
   }
}

/* Add a new Userbutton to the list */
struct Userbutton *Adduserbutton(void *list,UBYTE *label,UBYTE *cmd)
{  struct Userbutton *ub=ALLOCSTRUCT(Userbutton,1,MEMF_PUBLIC|MEMF_CLEAR);
   if(ub
   && (ub->label=Dupstr(label,-1))
   && (ub->cmd=Dupstr(cmd,-1)))
   {  ADDTAIL(list,ub);
   }
   else
   {  Freeuserbutton(ub);
      ub=NULL;
   }
   return ub;
}

/* Free a Popupitem */
void Freepopupitem(struct Popupitem *pi)
{  if(pi)
   {  if(pi->title) FREE(pi->title);
      if(pi->cmd) FREE(pi->cmd);
      FREE(pi);
   }
}

/* Add a new Popupitem to the list */
struct Popupitem *Addpopupitem(void *list,USHORT flags,UBYTE *title,UBYTE *cmd)
{  struct Popupitem *pi=ALLOCSTRUCT(Popupitem,1,MEMF_PUBLIC|MEMF_CLEAR);
   if(pi
   && (pi->title=Dupstr(title,-1))
   && (pi->cmd=Dupstr(cmd,-1)))
   {  pi->flags=flags;
      ADDTAIL(list,pi);
   }
   else
   {  Freepopupitem(pi);
      pi=NULL;
   }
   return pi;
}

/* Free a Userkey */
void Freeuserkey(struct Userkey *uk)
{  if(uk)
   {  if(uk->cmd) FREE(uk->cmd);
      FREE(uk);
   }
}

/* Add a new Userkey to the list */
struct Userkey *Adduserkey(void *list,USHORT key,UBYTE *cmd)
{  struct Userkey *uk=ALLOCSTRUCT(Userkey,1,MEMF_PUBLIC|MEMF_CLEAR);
   if(uk
   && (uk->cmd=Dupstr(cmd,-1)))
   {  uk->key=key;
      ADDTAIL(list,uk);
   }
   else
   {  Freeuserkey(uk);
      uk=NULL;
   }
   return uk;
}

/* Find a Userkey */
struct Userkey *Finduserkey(void *list,USHORT key)
{  LIST(Userkey) *ulist=(LIST(Userkey) *)list;
   struct Userkey *uk;
   for(uk=ulist->first;uk->next;uk=uk->next)
   {  if(uk->key==key) return uk;
   }
   return NULL;
}

/* Free a font alias */
void Freefontalias(struct Fontalias *fa)
{  short i;
   if(fa)
   {  if(fa->alias) FREE(fa->alias);
      for(i=0;i<NRFONTS;i++)
      {  if(fa->fp[i].fontname) FREE(fa->fp[i].fontname);
         if(fa->fp[i].font) CloseFont(fa->fp[i].font);
      }
      FREE(fa);
   }
}

/* Add a font alias, or return existing for this alias */
struct Fontalias *Addfontalias(LIST(Fontalias) *list,UBYTE *alias)
{  struct Fontalias *fa,*fb;
   for(fa=list->first;fa->next;fa=fa->next)
   {  if(STRIEQUAL(fa->alias,alias)) return fa;
   }
   if((fa=ALLOCSTRUCT(Fontalias,1,MEMF_PUBLIC|MEMF_CLEAR))
   && (fa->alias=Dupstr(alias,-1))
   )
   {  for(fb=list->first;fb->next;fb=fb->next)
      {  if(stricmp(fa->alias,fb->alias)<0) break;
      }
      INSERT(list,fa,fb->prev);
   }
   else
   {  Freefontalias(fa);
      fa=NULL;
   }
   return fa;
}

/* Free a Mimeinfo */
void Freemimeinfo(struct Mimeinfo *mi)
{  if(mi)
   {  if(mi->type) FREE(mi->type);
      if(mi->subtype) FREE(mi->subtype);
      if(mi->extensions) FREE(mi->extensions);
      if(mi->cmd) FREE(mi->cmd);
      if(mi->args) FREE(mi->args);
      FREE(mi);
   }
}

/* Add a new Mimeinfo to the list */
struct Mimeinfo *Addmimeinfo(LIST(Mimeinfo) *list,UBYTE *type,UBYTE *subtype,UBYTE *extensions,
   USHORT driver,UBYTE *cmd,UBYTE *args)
{  struct Mimeinfo *mi=ALLOCSTRUCT(Mimeinfo,1,MEMF_PUBLIC|MEMF_CLEAR);
   struct Mimeinfo *mj,*mk;
   int c;
   if(mi
   && (!type || (mi->type=Dupstr(type,-1)))
   && (!subtype || (mi->subtype=Dupstr(subtype,-1)))
   && (!extensions || (mi->extensions=Dupstr(extensions,-1)))
   && (!cmd || (mi->cmd=Dupstr(cmd,-1)))
   && (!args || (mi->args=Dupstr(args,-1)))
   )
   {  mi->driver=driver;
      if(type && subtype && STRIEQUAL(type,"text") &&
         (STRIEQUAL(subtype,"html") || STRIEQUAL(subtype,"plain")))
         mi->deleteable=FALSE;
      else mi->deleteable=TRUE;
      /* If new mimeinfo is empty, add to tail of the list
       * Else insert at sorted place, where subtype * comes last */
      if(!mi->type[0]) ADDTAIL(list,mi);
      else
      {  for(mj=list->first;mj->next;mj=mj->next)
         {  c=stricmp(mi->type,mj->type);
            if(c<0) break;
            if(c==0)
            {  if(STRIEQUAL(mi->subtype,mj->subtype))
               {  mk=mj->next;
                  REMOVE(mj);
                  Freemimeinfo(mj);
                  mj=mk;
                  break;
               }
               if(mj->subtype[0]=='*') break;
               if(mi->subtype[0]!='*' && stricmp(mi->subtype,mj->subtype)<0) break;
            }
         }
         INSERT(list,mi,mj->prev);
      }
      return mi;
   }
   Freemimeinfo(mi);
   return NULL;
}

void Freenocache(struct Nocache *nc)
{  if(nc)
   {  if(nc->name) FREE(nc->name);
      if(nc->pattern) FREE(nc->pattern);
      FREE(nc);
   }
}

struct Nocache *Addnocache(LIST(Nocache) *list,UBYTE *name)
{  struct Nocache *nc=ALLOCSTRUCT(Nocache,1,MEMF_PUBLIC|MEMF_CLEAR);
   struct Nocache *na;
   if(nc
   && (nc->name=Dupstr(name,-1)))
   {  for(na=list->first;na->next;na=na->next)
      {  if(stricmp(na->name,name)>0) break;
      }
      INSERT(list,nc,na->prev);
   }
   else
   {  Freenocache(nc);
      nc=NULL;
   }
   return nc;
}

#define FREEANDNULL(p) if(p) FREE(p);p=NULL

/* Free all dynamic memory from partial prefs */
void Disposebrowserprefs(struct Browserprefs *p)
{  struct Fontalias *fa;
   struct Mimeinfo *mi;
   short i,j;
   for(j=0;j<2;j++)
   {  for(i=0;i<NRFONTS;i++)
      {  if(p->font[j][i].font) CloseFont(p->font[j][i].font);p->font[j][i].font=NULL;
         FREEANDNULL(p->font[j][i].fontname);
      }
   }
   if(p->aliaslist.first)
   {  while(fa=REMHEAD(&p->aliaslist)) Freefontalias(fa);
   }
   if(p->mimelist.first)
   {  while(mi=REMHEAD(&p->mimelist)) Freemimeinfo(mi);
   }
}

void Disposeprogramprefs(struct Programprefs *p)
{  FREEANDNULL(p->savepath);
   FREEANDNULL(p->temppath);
   FREEANDNULL(p->viewcmd);
   FREEANDNULL(p->viewargs);
   FREEANDNULL(p->editcmd);
   FREEANDNULL(p->editargs);
   FREEANDNULL(p->imgvcmd);
   FREEANDNULL(p->imgvargs);
   FREEANDNULL(p->console);
   FREEANDNULL(p->screenname);
}

void Disposeguiprefs(struct Guiprefs *p)
{  struct Menuentry *me;
   struct Userbutton *ub;
   struct Popupitem *pi;
   struct Userkey *uk;
   short i;
   if(p->menus.first)
   {  while(me=REMHEAD(&p->menus)) Freemenuentry(me);
   }
   if(p->buttons.first)
   {  while(ub=REMHEAD(&p->buttons)) Freeuserbutton(ub);
   }
   for(i=0;i<NRPOPUPMENUS;i++)
   {  if(p->popupmenu[i].first)
      {  while(pi=REMHEAD(&p->popupmenu[i])) Freepopupitem(pi);
      }
   }
   if(p->keys.first)
   {  while(uk=REMHEAD(&p->keys)) Freeuserkey(uk);
   }
   for(i=0;i<NRNAVBUTTONS;i++)
   {  FREEANDNULL(p->navs[i].cmd);
   }
}

void Disposenetworkprefs(struct Networkprefs *p)
{  void *nc;
   FREEANDNULL(p->homeurl);
   FREEANDNULL(p->localindex);
   FREEANDNULL(p->searchurl);
   FREEANDNULL(p->spoofid);
   FREEANDNULL(p->mailtocmd);
   FREEANDNULL(p->mailtoargs);
   FREEANDNULL(p->telnetcmd);
   FREEANDNULL(p->telnetargs);
   FREEANDNULL(p->newscmd);
   FREEANDNULL(p->newsargs);
   FREEANDNULL(p->starttcpcmd);
   FREEANDNULL(p->starttcpargs);
   FREEANDNULL(p->endtcpcmd);
   FREEANDNULL(p->endtcpargs);
   FREEANDNULL(p->httpproxy);
   FREEANDNULL(p->ftpproxy);
   FREEANDNULL(p->gopherproxy);
   FREEANDNULL(p->telnetproxy);
   FREEANDNULL(p->cachepath);
   FREEANDNULL(p->emailaddr);
   FREEANDNULL(p->replyaddr);
   FREEANDNULL(p->fullname);
   FREEANDNULL(p->organization);
   FREEANDNULL(p->smtphost);
   FREEANDNULL(p->nntphost);
   FREEANDNULL(p->sigfile);
   FREEANDNULL(p->newsauthuser);
   FREEANDNULL(p->newsauthpass);
   FREEANDNULL(p->mailquotehdr);
   FREEANDNULL(p->newsquotehdr);
   if(p->nocache.first)
   {  while(nc=REMHEAD(&p->nocache)) Freenocache(nc);
   }
   if(p->noproxy.first)
   {  while(nc=REMHEAD(&p->noproxy)) Freenoproxy(nc);
   }
   if(p->nocookie.first)
   {  while(nc=REMHEAD(&p->nocookie)) Freenocookie(nc);
   }
}

/* Duplicate all items from (from) to (to) */
BOOL Copybrowserprefs(struct Browserprefs *from,struct Browserprefs *to)
{  short i,j;
   struct Fontalias *fa,*fb;
   struct Mimeinfo *mi;
   NEWLIST(&to->aliaslist);
   NEWLIST(&to->mimelist);
   for(j=0;j<2;j++)
   {  for(i=0;i<NRFONTS;i++)
      {  if(!(to->font[j][i].fontname=Dupstr(from->font[j][i].fontname,-1))) return FALSE;
         to->font[j][i].fontsize=from->font[j][i].fontsize;
         to->font[j][i].font=from->font[j][i].font;
      }
   }
   for(fa=from->aliaslist.first;fa->next;fa=fa->next)
   {  fb=Addfontalias(&to->aliaslist,fa->alias);
      for(i=0;i<NRFONTS;i++)
      {  if(!(fb->fp[i].fontname=Dupstr(fa->fp[i].fontname,-1))) return FALSE;
         fb->fp[i].fontsize=fa->fp[i].fontsize;
      }
   }
   memmove(to->styles,from->styles,sizeof(to->styles));
   to->newlink=from->newlink;
   to->oldlink=from->oldlink;
   to->selectlink=from->selectlink;
   to->background=from->background;
   to->text=from->text;
   to->screenpens=from->screenpens;
   to->htmlmode=from->htmlmode;
   to->ullink=from->ullink;
   to->docolors=from->docolors;
   to->dobgsound=from->dobgsound;
   to->blinkrate=from->blinkrate;
   to->dojs=from->dojs;
   to->doframes=from->doframes;
   to->inctable=from->inctable;
   to->nominalframe=from->nominalframe;
   to->jserrors=from->jserrors;
   to->nobanners=from->nobanners;
   to->tooltips=from->tooltips;
   to->handpointer=from->handpointer;
   to->jswatch=from->jswatch;
   for(mi=from->mimelist.first;mi->next;mi=mi->next)
   {  Addmimeinfo(&to->mimelist,mi->type,mi->subtype,mi->extensions,
         mi->driver,mi->cmd,mi->args);
   }
   return TRUE;
}

BOOL Copyprogramprefs(struct Programprefs *from,struct Programprefs *to)
{  if(!(to->savepath=Dupstr(from->savepath,-1))) return FALSE;
   if(!(to->temppath=Dupstr(from->temppath,-1))) return FALSE;
   to->overlap=from->overlap;
   to->commands=from->commands;
   to->hlautoclose=from->hlautoclose;
   to->hlrequester=from->hlrequester;
   to->whautoclose=from->whautoclose;
   to->saveicons=from->saveicons;
   to->clipdrag=from->clipdrag;
   to->aahotlist=from->aahotlist;
   to->aawinhis=from->aawinhis;
   to->aanetstat=from->aanetstat;
   to->centerreq=from->centerreq;
   if(!(to->viewcmd=Dupstr(from->viewcmd,-1))) return FALSE;
   if(!(to->viewargs=Dupstr(from->viewargs,-1))) return FALSE;
   if(!(to->editcmd=Dupstr(from->editcmd,-1))) return FALSE;
   if(!(to->editargs=Dupstr(from->editargs,-1))) return FALSE;
   if(!(to->imgvcmd=Dupstr(from->imgvcmd,-1))) return FALSE;
   if(!(to->imgvargs=Dupstr(from->imgvargs,-1))) return FALSE;
   if(!(to->console=Dupstr(from->console,-1))) return FALSE;
   if(!(to->startupscript=Dupstr(from->startupscript,-1))) return FALSE;
   if(!(to->shutdownscript=Dupstr(from->shutdownscript,-1))) return FALSE;
   to->screentype=from->screentype;
   if(!(to->screenname=Dupstr(from->screenname,-1))) return FALSE;
   to->screenmode=from->screenmode;
   to->screendepth=from->screendepth;
   to->screenwidth=from->screenwidth;
   to->screenheight=from->screenheight;
   to->loadpalette=from->loadpalette;
   memmove(to->scrpalette,from->scrpalette,sizeof(from->scrpalette));
   memmove(to->scrdrawpens,from->scrdrawpens,sizeof(from->scrdrawpens));
   to->fourcolors=from->fourcolors;
   return TRUE;
}

BOOL Copyguiprefs(struct Guiprefs *from,struct Guiprefs *to)
{  struct Menuentry *me;
   struct Userbutton *ub;
   struct Popupitem *pi;
   struct Userkey *uk;
   short i;
   NEWLIST(&to->menus);
   NEWLIST(&to->buttons);
   for(i=0;i<NRPOPUPMENUS;i++)
   {  NEWLIST(&to->popupmenu[i]);
   }
   NEWLIST(&to->keys);
   to->popupkey=from->popupkey;
   to->showbuttons=from->showbuttons;
   to->shownav=from->shownav;
   for(me=from->menus.first;me->next;me=me->next)
   {  if(!Addmenuentry(&to->menus,me->type,me->title,me->scut[0],me->cmd)) return FALSE;
   }
   for(ub=from->buttons.first;ub->next;ub=ub->next)
   {  if(!Adduserbutton(&to->buttons,ub->label,ub->cmd)) return FALSE;
   }
   for(i=0;i<NRPOPUPMENUS;i++)
   {  for(pi=from->popupmenu[i].first;pi->next;pi=pi->next)
      {  if(!Addpopupitem(&to->popupmenu[i],pi->flags,pi->title,pi->cmd)) return FALSE;
      }
   }
   for(uk=from->keys.first;uk->next;uk=uk->next)
   {  if(!Adduserkey(&to->keys,uk->key,uk->cmd)) return FALSE;
   }
   for(i=0;i<NRNAVBUTTONS;i++)
   {  if(!(to->navs[i].cmd=Dupstr(from->navs[i].cmd,-1))) return FALSE;
   }
   return TRUE;
}

BOOL Copynetworkprefs(struct Networkprefs *from,struct Networkprefs *to)
{  struct Nocache *nc;
   NEWLIST(&to->nocookie);
   NEWLIST(&to->noproxy);
   NEWLIST(&to->nocache);
   to->loadimg=from->loadimg;
   to->maxconnect=from->maxconnect;
   to->maxdiskread=from->maxdiskread;
   if(!(to->homeurl=Dupstr(from->homeurl,-1))) return FALSE;
   if(!(to->localindex=Dupstr(from->localindex,-1))) return FALSE;
#ifndef DEMOVERSION
   if(!(to->searchurl=Dupstr(from->searchurl,-1))) return FALSE;
   if(!(to->spoofid=Dupstr(from->spoofid,-1))) return FALSE;
#endif
   to->ignoremime=from->ignoremime;
   to->starthomepage=from->starthomepage;
   to->autosearch=from->autosearch;
   to->contanim=from->contanim;
   to->restrictimages=from->restrictimages;
   if(!Copyoptstring(from->mailtocmd,&to->mailtocmd)) return FALSE;
   if(!Copyoptstring(from->mailtoargs,&to->mailtoargs)) return FALSE;
   if(!Copyoptstring(from->telnetcmd,&to->telnetcmd)) return FALSE;
   if(!Copyoptstring(from->telnetargs,&to->telnetargs)) return FALSE;
   if(!Copyoptstring(from->newscmd,&to->newscmd)) return FALSE;
   if(!Copyoptstring(from->newsargs,&to->newsargs)) return FALSE;
   if(!Copyoptstring(from->starttcpcmd,&to->starttcpcmd)) return FALSE;
   if(!Copyoptstring(from->starttcpargs,&to->starttcpargs)) return FALSE;
   if(!Copyoptstring(from->endtcpcmd,&to->endtcpcmd)) return FALSE;
   if(!Copyoptstring(from->endtcpargs,&to->endtcpargs)) return FALSE;
   if(!Copyoptstring(from->httpproxy,&to->httpproxy)) return FALSE;
   if(!Copyoptstring(from->ftpproxy,&to->ftpproxy)) return FALSE;
   if(!Copyoptstring(from->gopherproxy,&to->gopherproxy)) return FALSE;
   if(!Copyoptstring(from->telnetproxy,&to->telnetproxy)) return FALSE;
   to->limitproxy=from->limitproxy;
   to->passiveftp=from->passiveftp;
   if(!(to->cachepath=Dupstr(from->cachepath,-1))) return FALSE;
   to->camemsize=from->camemsize;
   to->cadisksize=from->cadisksize;
   to->minfreechip=from->minfreechip;
   to->minfreefast=from->minfreefast;
   to->caverify=from->caverify;
   to->fastresponse=from->fastresponse;
   to->cachelocalhost=from->cachelocalhost;
   for(nc=from->nocache.first;nc->next;nc=nc->next)
   {  Addnocache(&to->nocache,nc->name);
   }
   for(nc=from->noproxy.first;nc->next;nc=nc->next)
   {  Addnoproxy(&to->noproxy,nc->name);
   }
   to->referer=from->referer;
   to->formwarn=from->formwarn;
   to->spamblock=from->spamblock;
   to->ftpemailaddr=from->ftpemailaddr;
   to->cookies=from->cookies;
   to->rfc2109=from->rfc2109;
   for(nc=from->nocookie.first;nc->next;nc=nc->next)
   {  Addnocookie(&to->nocookie,nc->name);
   }
   if(!(to->emailaddr=Dupstr(from->emailaddr,-1))) return FALSE;
   if(!(to->replyaddr=Dupstr(from->replyaddr,-1))) return FALSE;
   if(!(to->fullname=Dupstr(from->fullname,-1))) return FALSE;
   if(!(to->organization=Dupstr(from->organization,-1))) return FALSE;
   if(!(to->smtphost=Dupstr(from->smtphost,-1))) return FALSE;
   if(!(to->nntphost=Dupstr(from->nntphost,-1))) return FALSE;
   if(!(to->sigfile=Dupstr(from->sigfile,-1))) return FALSE;
   if(!(to->newsauthuser=Dupstr(from->newsauthuser,-1))) return FALSE;
   if(!(to->newsauthpass=Dupstr(from->newsauthpass,-1))) return FALSE;
   if(!(to->mailquotehdr=Dupstr(from->mailquotehdr,-1))) return FALSE;
   if(!(to->newsquotehdr=Dupstr(from->newsquotehdr,-1))) return FALSE;
   to->extmailer=from->extmailer;
   to->extnewsreader=from->extnewsreader;
   to->maxartnews=from->maxartnews;
   to->framednews=from->framednews;
   to->sortednews=from->sortednews;
   to->longhdrnews=from->longhdrnews;
   to->propnews=from->propnews;
   to->newsbynum=from->newsbynum;
   return TRUE;
}

BOOL Copywindowprefs(struct Windowprefs *from,struct Windowprefs *to)
{  *to=*from;
   return TRUE;
}

/* load prefs into memory. Use name or construct from config name */
void Loadbrowserprefs(struct Browserprefs *bp,BOOL saved,UBYTE *name)
{  long fh;
   if(fh=Openname(saved,name,"/browser",MODE_OLDFILE))
   {  Loadprefs(browsersave,fh,bp);
      Close(fh);
   }
}

void Loadprogramprefs(struct Programprefs *pp,BOOL saved,UBYTE *name)
{  long fh;
   if(fh=Openname(saved,name,"/program",MODE_OLDFILE))
   {  Loadprefs(programsave,fh,pp);
      Close(fh);
   }
}

void Loadguiprefs(struct Guiprefs *gp,BOOL saved,UBYTE *name)
{  long fh;
   if(fh=Openname(saved,name,"/gui",MODE_OLDFILE))
   {  Loadprefs(guisave,fh,gp);
      Close(fh);
   }
}

void Loadnetworkprefs(struct Networkprefs *np,BOOL saved,UBYTE *name)
{  long fh;
   if(fh=Openname(saved,name,"/network",MODE_OLDFILE))
   {  Loadprefs(networksave,fh,np);
      Close(fh);
   }
#ifndef DEMOVERSION
   if(fh=Openname(saved,name,"/nocookie",MODE_OLDFILE))
   {  Loadprefs(networksave,fh,np);
      Close(fh);
   }
#endif
#ifdef DEMOVERSION
   if(np->maxconnect>2) np->maxconnect=2;
   if(np->maxdiskread>2) np->maxdiskread=2;
#endif
}

void Loadwindowprefs(struct Windowprefs *wp,BOOL saved,UBYTE *name)
{  long fh;
   if(fh=Openname(saved,name,"/window",MODE_OLDFILE))
   {  Loadprefs(windowsave,fh,wp);
      Close(fh);
   }
}

void Loadsettingsprefs(struct Settingsprefs *sp,BOOL saved,UBYTE *name)
{  long fh;
   if(fh=Openname(saved,name,"/settings",MODE_OLDFILE))
   {  Loadprefs(settingssave,fh,sp);
      Close(fh);
   }
}

/* save prefs. Use name or construct from config name */
void Savebrowserprefs(struct Browserprefs *bp,BOOL saved,UBYTE *name)
{  long fh;
   if(fh=Openname(saved,name,"/browser",MODE_NEWFILE))
   {  Saveprefsfmt(browsersave,fh,bp,0);
      Close(fh);
   }
}

void Saveprogramprefs(struct Programprefs *pp,BOOL saved,UBYTE *name)
{  long fh;
   if(fh=Openname(saved,name,"/program",MODE_NEWFILE))
   {  Saveprefsfmt(programsave,fh,pp,0);
      Close(fh);
   }
}

void Saveguiprefs(struct Guiprefs *gp,BOOL saved,UBYTE *name)
{  long fh;
   if(fh=Openname(saved,name,"/gui",MODE_NEWFILE))
   {  Saveprefsfmt(guisave,fh,gp,0);
      Close(fh);
   }
}

void Savenocookieprefs(struct Networkprefs *np,BOOL saved,UBYTE *name)
{
#ifndef DEMOVERSION
   long fh;
   if(fh=Openname(saved,name,"/nocookie",MODE_NEWFILE))
   {  Saveprefsfmt(networksave,fh,np,1);
      Close(fh);
   }
#endif
}

void Savenetworkprefs(struct Networkprefs *np,BOOL saved,UBYTE *name)
{  long fh;
   if(fh=Openname(saved,name,"/network",MODE_NEWFILE))
   {  Saveprefsfmt(networksave,fh,np,0);
      Close(fh);
   }
}

void Savewindowprefs(struct Windowprefs *wp,BOOL saved,UBYTE *name)
{  long fh;
   if(fh=Openname(saved,name,"/window",MODE_NEWFILE))
   {  Saveprefsfmt(windowsave,fh,wp,0);
      Close(fh);
   }
}

void Savesettingsprefs(struct Settingsprefs *sp,BOOL saved,UBYTE *name)
{  long fh;
   if(fh=Openname(saved,name,"/settings",MODE_NEWFILE))
   {  Saveprefsfmt(settingssave,fh,sp,0);
      Close(fh);
   }
}

void Copyprefs(struct Prefs *from,struct Prefs *to)
{  Copybrowserprefs(&from->browser,&to->browser);
   Copyprogramprefs(&from->program,&to->program);
   Copyguiprefs(&from->gui,&to->gui);
   Copynetworkprefs(&from->network,&to->network);
}

void Saveprefs(struct Prefs *prefs)
{  Savebrowserprefs(&prefs->browser,FALSE,NULL);
   Saveprogramprefs(&prefs->program,FALSE,NULL);
   Saveguiprefs(&prefs->gui,FALSE,NULL);
   Savenetworkprefs(&prefs->network,FALSE,NULL);
   Savenocookieprefs(&prefs->network,FALSE,NULL);
}

void Disposeprefs(struct Prefs *prefs)
{  Disposebrowserprefs(&prefs->browser);
   Disposeprogramprefs(&prefs->program);
   Disposeguiprefs(&prefs->gui);
   Disposenetworkprefs(&prefs->network);
}