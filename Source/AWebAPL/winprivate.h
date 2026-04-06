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

/* winprivate.h - window, event private data */

#include <reaction/reaction_author.h>
#include <reaction/reaction_macros.h>
#include <reaction/reaction_class.h>

#define MAXSTRBUFCHARS  256

struct Scrollgad
{  struct Gadget *gad;
   ULONG total,vis;           /* Unscaled attributes */
   short shift;               /* Shift (scaling) factor */
};

struct Awindow
{  struct Aobject object;
   void *frame;               /* Window's top frame. */
   ULONG key;                 /* ID for external refs. */
   long windownr;             /* Use for communication with user. */
   void *whis;                /* Currently displayed documents. */
   void *hiswhis;             /* Current winhis as far as history is concerned.
                               * This is used to compute new whis from when going
                               * back or forward. Since frame might not set our
                               * winhis if nothing changes, whis may not properly
                               * reflect the history cursor. */
   ULONG flags;
   struct SpecialPens capens; /* Obtained pens for CA. */
   struct Window *window;     /* Our Intuition window. */
   struct Menu *menu;         /* Our menu bar. */
   struct Image *leftimg,*rightimg,*upimg,*downimg,*backimg,*fwdimg,
      *rldimg,*homeimg,*cancelimg,*hotimg,*imgimg,*nwsimg,*addhotimg,
      *searchimg,*unsecureimg,*secureimg;  /* Our GUI images. */
   struct Scrollgad hslider,vslider;
   struct Gadget *leftarrow,*rightarrow,*uparrow,
      *downarrow,*backgad,*fwdgad,*rldgad,*homegad,*cancelgad,*hotgad,
      *imggad,*nwsgad,*urlgad,*urlpopgad,*statusgad,*searchgad,
      *ledgad,*addhotgad,*ubutgad,*securegad;  /* Our GUI gadgets. */
   struct Gadget *layoutgad,*spacegad;
   UBYTE urlbuf[MAXSTRBUFCHARS]; /* URL gadget buffer. */
   UBYTE *wintitle;           /* Our window title. */
   void *activeobject;        /* Current active object. */
   void *hitobject;           /* current hit object. */
   UBYTE *hittext;            /* Current status text for hitobject. */
   void *focus;               /* Frame that has scroll focus or NULL */
   void *nextfocus;           /* Frame that wants focus on mouse click */
   void *jonmouse;            /* Current object wanting AOM_JONMOUSE */
   void *dragstartobject;     /* Starting object for drag */
   ULONG dragstartobjpos;     /* Starting position for drag */
   void *dragendobject;       /* Ending object for drag */
   ULONG dragendobjpos;       /* Ending position for drag */
   ULONG cmd;                 /* Commands emerging from events. */
   UBYTE *portname;           /* ARexx port name. */
   void *activeurl;           /* URL whose input we monitor. */
   void *popup;               /* The current popup object. */
   struct IBox box,zoombox;   /* Cached window dimensions. */
   struct List urlpoplist;    /* Chooser list */
   struct List userbutlist;   /* User button speedbar list */
   short newwidth,newheight;  /* Inner dimensions set with OM_NEW */
   short newleft,newtop;      /* Outer position set with OM_NEW */
   USHORT ptrtype;            /* Current pointer type */
   struct AppWindow *appwindow; /* The AppWindow for this window or NULL */
   UBYTE *statustext;         /* Copy of status text */
   UBYTE *statushptext;       /* Copy of status HP text */
   UBYTE *screentitle;        /* Last screen title set */
   short layoutstyle;         /* Layout style: 0=classic, 1=modern */
   ULONG statustime;          /* Timestamp when status was shown in screen title (0 = not showing) */
};

#define WINF_NOPROXY       0x0001   /* Don't use proxies. */
#define WINF_CLIPDRAG      0x0002   /* Clipboard dragging allowed */
#define WINF_RESIZED       0x0004   /* Resizing, don't render until NEWSIZE received. */
#define WINF_INPUT         0x0008   /* This window is inputting. */
#define WINF_ZOOMED        0x0010   /* Window is in zoomed state. */
#define WINF_DRAGSTART     0x0020   /* A new drag may start */
#define WINF_KEEPDRAG      0x0040   /* Do not clear drag for this mouse click */
#define WINF_REFRESHING    0x0080   /* BeginRefresh() was done. */
#define WINF_JSCLOSEABLE   0x0100   /* Is closeable by JS close() */
#define WINF_ANIMON        0x0200   /* Ledgad is on */
#define WINF_NAVS          0x0400   /* Window wants navigation gadgets */
#define WINF_BUTTONS       0x0800   /* Window wants userbuttons */
#define WINF_USERPOS       0x1000   /* Window position set explicitly */

enum GADGET_IDS
{  GID_HSLIDER=1,GID_VSLIDER,GID_LEFT,GID_RIGHT,GID_UP,GID_DOWN,
   GID_URL,GID_LAYOUT,GID_URLPOP,GID_UBUTTON,
   /* must be last: */
   GID_NAV=100,
};

/* window event commands */
#define CMD_LINEUP      0x00000001
#define CMD_LINEDOWN    0x00000002
#define CMD_PAGEUP      0x00000004
#define CMD_PAGEDOWN    0x00000008
#define CMD_HOME        0x00000010
#define CMD_END         0x00000020
#define CMD_LEFT        0x00000040
#define CMD_RIGHT       0x00000080
#define CMD_MOVED       0x00000100  /* Keyboard move */
#define CMD_CHECKLINK   0x00000200
#define CMD_SCROLLED    0x00000400  /* Scroller move */
#define CMD_CLOSE       0x00000800
#define CMD_CLOSEFORCE  0x00001000
#define CMD_SETSCROLL   0x00002000  /* Set scroller after move */
#define CMD_PAGELEFT    0x00010000
#define CMD_PAGERIGHT   0x00020000
#define CMD_DEFER       0x00040000  /* Active object deferred */

/* window update tags */

#define AOWIN_Openlocal    (AOWIN_Dummy+101)
   /* (UBYTE *) File name to open */

#define AOWIN_Startarexx   (AOWIN_Dummy+102)
   /* (UBYTE *) Arexx macro to start */

#define AOWIN_Otherhotlist (AOWIN_Dummy+103)
   /* (UBYTE *) Name of other hotlist to open */

#define AOWIN_Loadsettings (AOWIN_Dummy+104)
   /* (UBYTE *) Settings directory to load */

#define AOWIN_Savesettingsas (AOWIN_Dummy+105)
   /* (UBYTE *) Settings directory to save into */

#define AOWIN_Openlocalpattern (AOWIN_Dummy+106)
   /* (UBYTE *) Pattern on exit */

#define AOWIN_    (AOWIN_Dummy+)
#define AOWIN_    (AOWIN_Dummy+)

/* window.c */

extern LIST(Awindow) windows;
extern UBYTE *urlpops[];
extern struct Awindow *activewindow;
extern struct Awindow *Nextwindow(struct Awindow *win,long d);
extern void Setsecure(struct Awindow *win);
extern void Setawinpointer(struct Awindow *win,USHORT ptrtype);

/* event.c */

extern UBYTE activeport[32];
extern void Refreshwindow(struct Awindow *win);
extern void Setactiveport(UBYTE *portname);
extern long Updatewindow(struct Awindow *win,struct Amset *ams);
extern void Followurlname(struct Awindow *win,UBYTE *name,UBYTE *id);
extern void Followhis(struct Awindow *win,void *whis);
extern void Dragclear(struct Awindow *win);
extern void Setactiveobject(struct Awindow *win,void *ao);
extern BOOL Cancelevent(struct Awindow *win);
extern void Savesettingsreq(struct Awindow *win);
extern void Loadsettingsreq(struct Awindow *win);
