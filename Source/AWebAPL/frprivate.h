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

/* frprivate.h - AWeb frame private */

struct Frame
{  struct Element elt;
   void *frame;
   long x,y,w,h;           /* inner window dimensions relative to object */
   long left,top;          /* left,top scroll positions */
   long docw,doch;         /* document (object) dimensions */
   long layouth;           /* Height to layout against, same as (h) except for dumb frames. */
   void *layoutparent;
   void *copy;             /* document or other object displayed in this frame */
   void *orgurl;           /* original URL for this frame */
   UBYTE *orgfragment;     /* Original fragment for this frame */
   void *hscroll,*vscroll; /* scroller objects */
   void *win;              /* link to window object */
   short bgcolor,textcolor,linkcolor,vlinkcolor,alinkcolor;
                           /* pen numbers. If (-1) use window's defaults. */
   void *bgimage;          /* bg image to use or NULL. */
   struct Aobject *bgalign; /* object to align background image to */
   UBYTE *id;              /* Hierarchical frame id */
   void *whis;             /* Current winhis object */
   void *inputwhis;        /* New winhis we want to obey, but haven't got a doc for yet */
   void *inputcopy;        /* The COPY object we are inputting */
   ULONG flags;
   ULONG inputflags;       /* USEHISPOS, USEFRAGMENT flags for inputcopy */
   UBYTE seqnr;
   UBYTE hittype;          /* Type of hit at last hittest */
   long width,height;      /* Suggested dimensions or after resize */
   long swidth,sheight;    /* Original suggested dimensions */
   ULONG sflags;           /* Original PIXELWIDTH/HEIGHT flags */
   short mwidth,mheight;   /* Margin dimensions */
   short border;           /* Border thickness or 0 */
   UBYTE *defstatus;       /* Status text when nothing else is pointed to */
   struct Framename *name;
   void *search;
   long sizex,sizey,sizew,sizeh; /* Dimensions during resize */
   short sizeside;         /* Which side is resized */
   long hsx,hsy,hsw,hsh;   /* Current dimensions of horizontal scroller */
   long vsx,vsy,vsw,vsh;   /* Current dimensions of vertical scroller */
   void *pulltimer;        /* When it returns... */
   void *pullurl;          /* ...get this URL. */
   void *popup;            /* Popup window open */
   struct Jobject *jobject;/* JS object for this frame */
   struct Jobject *jframes;/* JS frames array object */
   struct Jobject *jdscope;/* JS global data scope from document */
   struct Buffer *jgenerated; /* Buffer containing JS generated text (not ours) */
   LIST(Timeout) timeouts; /* Pending JS timeout events */
   UBYTE *onfocus,*onblur; /* JS scripts (not ours) */
   ULONG jprotect;         /* JS protection key */
   void *qiurl;            /* Queued input url */
   UBYTE *qifragment;      /* Queued input fragment */
   void *info;             /* The info window for this frame */
};

#define FRMF_USEHISPOS     0x00000001  /* Use positions from window history */
#define FRMF_TOPFRAME      0x00000002  /* This is the top frame in the window */
#define FRMF_USEFRAGMENT   0x00000004  /* Find the fragment name */
#define FRMF_CHANGEDCHILD  0x00000008  /* Child was changed in this frame */
#define FRMF_NEWCHILD      0x00000010  /* New child was set in this frame, clear when render. */
#define FRMF_PIXELWIDTH    0x00000020  /* Suggested width is in pixels. */
#define FRMF_PIXELHEIGHT   0x00000040  /* Suggested height is in pixels. */
#define FRMF_SCROLLING     0x00000080  /* Scrollers allowed. */
#define FRMF_RESIZE        0x00000100  /* Resizing is allowed */
#define FRMF_HSCROLL       0x00000200  /* Display h scroller */
#define FRMF_VSCROLL       0x00000400  /* Display v scroller */
#define FRMF_RETRYSCROLL   0x00000800  /* Retry without scrollers on next layout */
#define FRMF_BORDERSET     0x00001000  /* Border was explicitly set */
#define FRMF_DUMBFRAME     0x00002000  /* Don't try to adjust scroll positions */
#define FRMF_NOBACKGROUND  0x00004000  /* Don't use backgrounds */
#define FRMF_INLINE        0x00008000  /* Inline frame, resize possible */
#define FRMF_PULLRELOAD    0x00020000  /* Do a reload on next clientpull */
#define FRMF_FOCUSED       0x00040000  /* Frame has set itself as focus */
#define FRMF_RELOADVERIFY  0x00080000  /* On next new winhis do a forced verify */
#define FRMF_JSIMAGECTR    0x00100000  /* JS object has JS image constructor added */
#define FRMF_JSOPEN        0x00200000  /* JS generated source is still open */
#define FRMF_JSOPTIONCTR   0x00400000  /* JS object has JS option constructor added */
#define FRMF_JSXHRCTR      0x00800000  /* JS object has JS XMLHttpRequest constructor added */
#define FRMF_RESET         0x00800000  /* Next layout must relayout even if dims unchanged */

#define FSIDE_TOP    1
#define FSIDE_BOTTOM 2
#define FSIDE_RIGHT  3
#define FSIDE_LEFT   4

#define FHIT_CORNER     1  /* Corner hit (popup) */
#define FHIT_RESIZE     2  /* Frame resize */
#define FHIT_DEFSTATUS  3  /* Nothing else, use default status */

struct Framename
{  NODE(Framename);
   UBYTE *name;
   struct Frame *frame;
};

struct Timeout
{  NODE(Timeout);
   void *timer;            /* Timer object */
   UBYTE *script;          /* JS script to execute */
   BOOL nobanners;         /* Suppress opening of banner windows when executing */
};

/* Holds data for queued Inputwindoc() */
struct Queuedinputwindoc
{  void *url;              /* URL to input */
   UBYTE *fragment;        /* Fragment to input */
   UBYTE *id;              /* Target frame ID to input in */
};

/* Private tags */
#define AOFRM_Timercpready    (AOFRM_Dummy+128)
#define AOFRM_Timertoready    (AOFRM_Dummy+129)

#define FQID_LOADJGENERATED   1  /* Queueid: load JS generated text */
#define FQID_INPUTWINDOC      2  /* Queueid: do an Inputwindoc() using Queuedinputwindoc data */
#define FQID_HISTORY          3  /* Queueid: set AOWIN_Hiswinhis to queue data */

/* frame.c */

extern struct Frame *Targetframeoptnew(struct Frame *fr,UBYTE *name,BOOL opennew);

/* framejs.c */

extern long Jsetupframe(struct Frame *fr,struct Amjsetup *amj);
extern void Freejframe(struct Frame *fr);
extern void Clearjframe(struct Frame *fr);
extern void Rebindwindowjs(struct Frame *fr);
extern void Loadjgenerated(struct Frame *fr);
extern void Freetimeouts(struct Frame *fr);
extern void Triggertimeout(struct Frame *fr,void *timer);
extern void Jprotframe(struct Frame *fr,ULONG jprotect);
extern void Cleartimeouts(struct Frame *fr);