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

/* window.c - AWeb window object */

#include "aweb.h"
#include "window.h"
#include "url.h"
#include "frame.h"
#include "application.h"
#include "winhis.h"
#include "winprivate.h"
#include "jslib.h"
#include "fetch.h"
#include <intuition/intuition.h>
#include <intuition/imageclass.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <intuition/intuitionbase.h>
#include <workbench/workbench.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <reaction/reaction_class.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/speedbar.h>
#include <gadgets/chooser.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/layout.h>
#include <gadgets/space.h>
#include <images/label.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/utility.h>
#include <intuition/pointerclass.h>
#include <proto/layers.h>
#include <proto/wb.h>
#include <proto/bitmap.h>
#include <proto/window.h>
#include <proto/label.h>
#include <proto/speedbar.h>
#include <proto/chooser.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/layout.h>
#include <proto/space.h>
#include <proto/timer.h>

LIST(Awindow) windows;

static short nextx=-1,nexty;
static UBYTE buf2[MAXSTRBUFCHARS],buf3[MAXSTRBUFCHARS];
static UBYTE screentitlebuf[256];  /* Global buffer for screen title */
static struct Awindow *lastscreentitlewin=NULL;  /* Track which window's title is displayed */
static long windowkey=0;

struct Awindow *activewindow;

static ULONG idcmpflags=IDCMP_CHANGEWINDOW|IDCMP_INTUITICKS|IDCMP_REFRESHWINDOW|
   IDCMP_CLOSEWINDOW|IDCMP_MOUSEBUTTONS|
   IDCMP_MOUSEMOVE|IDCMP_GADGETUP|IDCMP_MENUPICK|IDCMP_IDCMPUPDATE|
   IDCMP_RAWKEY|IDCMP_SIZEVERIFY|
   IDCMP_GADGETDOWN|IDCMP_INACTIVEWINDOW|IDCMP_ACTIVEWINDOW;

#ifdef LOCALONLY
UBYTE *urlpops[]=
{  "file:///",
   NULL,
};
#else
UBYTE *urlpops[]=
{  "http://www.",
   "http://",
   "https://",
   "ftp://ftp.",
   "mailto:",
   "file://localhost/",
   NULL,
};
#endif

/*------------------------------------------------------------------------*/
/* Default images */

#define DEFAULTWIDTH    16
#define DEFAULTHEIGHT   14

static UWORD backdata[]=
{  0x0000,0x0000,0x0000,0x0200,0x0200,0x0201,0x0001,0x8001,
   0x43ff,0x2200,0x1200,0x0e00,0x0000,0x0000,
   0x0000,0x0000,0x0e00,0x1000,0x2000,0x41fe,0x8000,0x0000,
   0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
};
static UWORD fwddata[]=
{  0x0000,0x0000,0x0000,0x0008,0x0004,0x0002,0x0001,0x0001,
   0x7fc2,0x0004,0x0008,0x0030,0x0000,0x0000,
   0x0000,0x0000,0x0070,0x0040,0x0040,0xffc0,0x8000,0x8000,
   0x8000,0x0040,0x0040,0x0040,0x0000,0x0000,
};
static UWORD homedata[]=
{  0x0000,0x0000,0x0004,0x0064,0x001c,0x0006,0x0e72,0x0842,
   0x0842,0x0802,0x0802,0x0802,0x3ffe,0x0000,
   0x0000,0x0000,0x0198,0x0610,0x1800,0x6000,0x4108,0x4108,
   0x4138,0x4100,0x4100,0x4700,0x4000,0x0000,
};
static UWORD hotdata[]=
{  0x0000,0x0000,0x1004,0x13fc,0x1001,0x13ff,0x1080,0x1380,
   0x7004,0x03fc,0x1001,0x73ff,0x0000,0x0000,
   0x0000,0x0000,0xe7f8,0x8400,0x87fe,0x8400,0x8700,0x8400,
   0x87f8,0x0400,0xe7fe,0x8400,0x0000,0x0000,
};
static UWORD canceldata[]=
{  0x0000,0x0000,0x0002,0x2004,0x1008,0x0810,0x0420,0x0000,
   0x0000,0x0180,0x0240,0x3c3e,0x0000,0x0000,
   0x0000,0x0000,0x7c3c,0x0240,0x0180,0x0000,0x0000,0x0420,
   0x0810,0x1008,0x2004,0x4000,0x0000,0x0000,
};
static UWORD nwsdata[]=
{  0x0000,0x0000,0x0000,0x0000,0x0040,0x0042,0x0042,0x0606,
   0x1818,0x2060,0x0180,0x0000,0x0000,0x0000,
   0x0000,0x0000,0x0000,0x01c0,0x0606,0x1818,0x6020,0x4000,
   0x4200,0x4200,0x0200,0x0000,0x0000,0x0000,
};
static UWORD rlddata[]=
{  0x0000,0x0000,0x0004,0x07e2,0x0802,0x080e,0x0800,0x0808,
   0x0004,0x0002,0x1fe4,0x0008,0x0010,0x0000,
   0x0000,0x1ff8,0x2000,0x4000,0x4010,0x4010,0x4030,0x4020,
   0x47e0,0x2000,0x0000,0x0020,0x0020,0x0000,
};
static UWORD imgdata[]=
{  0x0000,0x0000,0x0200,0x0100,0x0202,0x1c02,0x0002,0x0002,
   0x011e,0x0100,0x0080,0x1e80,0x03c0,0x0000,
   0x0000,0x1c00,0x2000,0x4000,0x203c,0x0020,0x0020,0x0220,
   0x0420,0x0800,0x1000,0x2000,0x0000,0x0000,
};
static UWORD addhotdata[]=
{  0x0000,0x0000,0x0204,0x027c,0x0201,0x023f,0x0060,0x0060,
   0x73c4,0x027c,0x0201,0x0e7f,0x0000,0x0000,
   0x0000,0x0000,0x1cf8,0x1080,0x10fe,0xf1c0,0x8000,0x8000,
   0x8038,0x1080,0x10fe,0x1080,0x0000,0x0000,
};
static UWORD searchdata[]=
{  0x0000,0x0000,0x0e00,0x3180,0x2020,0x4020,0x4020,0x4000,
   0x2020,0x2060,0x0099,0x1f06,0x0000,0x0000,
   0x0000,0x1f00,0x2080,0x4040,0x8080,0x8040,0x8040,0x8060,
   0x8098,0x5186,0x2e00,0x0000,0x0000,0x0000,
};
/* #ifndef DEMOVERSION */
static UWORD unsecuredata[]=
{  0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
   0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
   0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
   0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
};
static UWORD securedata[]=
{  0x0000,0x0000,0x01e0,0x0210,0x0210,0x0000,0x0008,0x01c8,
   0x01c8,0x0088,0x01c8,0x0008,0x07f8,0x0000,
   0x0000,0x01c0,0x0200,0x0420,0x0420,0x0ff8,0x0800,0x0800,
   0x0800,0x0800,0x0800,0x0800,0x0800,0x0000,
};
/* #endif DEMOVERSION */

/*------------------------------------------------------------------------*/

/* Find the NewMenu item */
static struct NewMenu *Findmenudata(struct NewMenu *menus,UBYTE *cmd)
{  struct NewMenu *nm=menus;
   struct Menuentry *me;
   for(me=prefs.menus.first;me->next;me=me->next,nm++)
   {  if(STRIEQUAL(me->cmd,cmd)) return nm;
   }
   return NULL;
}

/* Check or uncheck one NewMenu item */
static void Checkmenu(struct NewMenu *nm,BOOL check)
{  if(check) nm->nm_Flags|=CHECKED;
   else nm->nm_Flags&=~CHECKED;
}

/* Check or uncheck NewMenu items */
static void Setmenus(struct Awindow *win,struct NewMenu *nmenus)
{  struct NewMenu *nm;
   if(nm=Findmenudata(nmenus,"@LOADIMGOFF"))
      Checkmenu(nm,prefs.loadimg==LOADIMG_OFF);
   if(nm=Findmenudata(nmenus,"@LOADIMGMAPS"))
      Checkmenu(nm,prefs.loadimg==LOADIMG_MAPS);
   if(nm=Findmenudata(nmenus,"@LOADIMGALL"))
      Checkmenu(nm,prefs.loadimg==LOADIMG_ALL);
   if(nm=Findmenudata(nmenus,"@DRAGGING"))
      Checkmenu(nm,BOOLVAL(win->flags&WINF_CLIPDRAG));
   if(nm=Findmenudata(nmenus,"@NOPROXY"))
      Checkmenu(nm,BOOLVAL(win->flags&WINF_NOPROXY));
   if(nm=Findmenudata(nmenus,"@BGIMAGES"))
      Checkmenu(nm,prefs.docolors);
   if(nm=Findmenudata(nmenus,"@BGSOUND"))
      Checkmenu(nm,prefs.dobgsound);
}

/* Build screen title with version, portname, and available memory */
static UBYTE *Makescreentitle(struct Awindow *win)
{  ULONG availmem;
   
   /* Guard against NULL win - must not dereference when intuition refreshes title */
   if(!win) return NULL;
   /* Use global buffer - ensure it's null-terminated */
   screentitlebuf[0] = '\0';
   /* Start with version */
   sprintf(screentitlebuf, "AWeb %s", awebversion ? awebversion : "");
   
   /* Add portname */
   if(win->portname && win->portname[0])
   {  strcat(screentitlebuf, " - ");
      strcat(screentitlebuf, win->portname);
   }
   else
   {  /* Use window key as fallback */
      UBYTE keybuf[32];
      sprintf(keybuf, " - (AWEB.%ld)", win->key);
      strcat(screentitlebuf, keybuf);
   }
   
   /* Add available memory */
   availmem = AvailMem(MEMF_TOTAL);
   if(availmem >= 1048576)
   {  /* Show in MB */
      UBYTE membuf[32];
      sprintf(membuf, "   %ldMB free", availmem / 1048576);
      strcat(screentitlebuf, membuf);
   }
   else if(availmem >= 1024)
   {  /* Show in KB */
      UBYTE membuf[32];
      sprintf(membuf, "   %ldKB free", availmem / 1024);
      strcat(screentitlebuf, membuf);
   }
   else
   {  /* Show in bytes */
      UBYTE membuf[32];
      sprintf(membuf, "   %ld bytes free", availmem);
      strcat(screentitlebuf, membuf);
   }
   return screentitlebuf;
}

/* Build screen title with status message (for modern layout) */
static UBYTE *Makestatusscreentitle(struct Awindow *win, UBYTE *status)
{  if(!win || !status || !*status) return NULL;
   
   /* Use global buffer - ensure it's null-terminated and copy status */
   strncpy(screentitlebuf, status, sizeof(screentitlebuf) - 1);
   screentitlebuf[sizeof(screentitlebuf) - 1] = '\0';
   return screentitlebuf;
}

/* Set a new window title */
static void Settitle(struct Awindow *win,UBYTE *title)
{  UBYTE *newtitle;
   if(newtitle=Windowtitle(win,(UBYTE *)Agetattr(win->frame,AOFRM_Name),title))
   {  
      /* Update window title, preserve screen title if it exists */
      if(win->window)
      {  /* COMMENTED OUT: Screen title feature temporarily disabled */
         /* if(win->screentitle && win->screentitle[0])
         {  Keep existing screen title when updating window title
            SetWindowTitles(win->window,newtitle,win->screentitle);
         }
         else
         {  No screen title yet, just update window title
            SetWindowTitles(win->window,newtitle,(UBYTE *)~0);
         } */
         SetWindowTitles(win->window,newtitle,(UBYTE *)~0);
      }
      if(win->wintitle) FREE(win->wintitle);
      win->wintitle=newtitle;
   }
}

/* Enable or disable the history gadgets and menu items */
static void Sethisgadgets(struct Awindow *win)
{  void *prev,*next;
   if(win && win->window)
   {  prev=(void *)Agetattr(win->hiswhis,AOWHS_Previous);
      next=(void *)Agetattr(win->hiswhis,AOWHS_Next);
      if(win->backgad)
      {  Setgadgetattrs(win->backgad,win->window,NULL,
            GA_Disabled,!prev,
            TAG_END);
      }
      if(win->fwdgad)
      {  Setgadgetattrs(win->fwdgad,win->window,NULL,
            GA_Disabled,!next,
            TAG_END);
      }
   }
}

/* Enable or disable the cancel gadget and menu item */
static void Setcancel(struct Awindow *win,BOOL onoff)
{  if(win->window )
   {  if(win->cancelgad)
      {  Setgadgetattrs(win->cancelgad,win->window,NULL,
            GA_Disabled,!onoff,
            GA_Selected,FALSE,
            TAG_END);
      }
   }
}

/* Update reload button image based on input state (for modern layout dual-function button) */
static void Setreloadbutton(struct Awindow *win,BOOL isinput)
{  if(win->window && win->rldgad && win->layoutstyle==1)
   {  if(isinput)
      {  /* Show cancel image when transfer is in progress */
         if(win->cancelimg)
         {  Setgadgetattrs(win->rldgad,win->window,NULL,
               GA_Image,win->cancelimg,
               TAG_END);
         }
      }
      else
      {  /* Show reload image when no transfer */
         if(win->rldimg)
         {  Setgadgetattrs(win->rldgad,win->window,NULL,
               GA_Image,win->rldimg,
               TAG_END);
         }
      }
   }
}

/* Enable or disable the cancel gadget depending on if fetches are going on */
static void Setwincancel(struct Awindow *win)
{  BOOL transferring;
   ULONG windowkey;
   /* Check for active network transfers for this window - this is the correct test */
   windowkey=Agetattr(win,AOWIN_Key);
   transferring=Windowtransferring(windowkey);
   if(transferring && !(win->flags&WINF_INPUT))
   {  win->flags|=WINF_INPUT;
      Setcancel(win,TRUE);
      Setreloadbutton(win,TRUE);
   }
   else if(!transferring && (win->flags&WINF_INPUT))
   {  /* Only clear input flag when there are no active network transfers */
      win->flags&=~WINF_INPUT;
      Setcancel(win,FALSE);
      Setreloadbutton(win,FALSE);
   }
}

/* Set a custom pointer */
void Setawinpointer(struct Awindow *win,USHORT ptrtype)
{  void *ptr;
   struct IntuitionBase *ibase;
   ULONG version;
   /* Only set the pointer if it changes */
   if(win->window && ptrtype!=win->ptrtype)
   {  /* Check if we have Intuition 47+ with system pointer types */
      ibase=IntuitionBase;
      if(ibase && (version=ibase->lib_Version)>=47)
      {  /* Use system pointer types for Intuition 47+ */
         if(ptrtype==APTR_HAND)
         {  SetWindowPointer(win->window,WA_PointerType,POINTERTYPE_LINK,TAG_END);
         }
         else if(ptrtype==APTR_DEFAULT)
         {  SetWindowPointer(win->window,WA_PointerType,POINTERTYPE_NORMAL,TAG_END);
         }
         else if(ptrtype==APTR_TEXT)
         {  SetWindowPointer(win->window,WA_PointerType,POINTERTYPE_TEXT,TAG_END);
         }
         else if(ptrtype==APTR_NOTALLOWED)
         {  SetWindowPointer(win->window,WA_PointerType,POINTERTYPE_NOTALLOWED,TAG_END);
         }
         else if(ptrtype==APTR_CONTEXTMENU)
         {  SetWindowPointer(win->window,WA_PointerType,POINTERTYPE_CONTEXTMENU,TAG_END);
         }
         else
         {  /* Fall back to custom pointers for resize pointers */
            ptr=Apppointer(Aweb(),ptrtype);
            SetWindowPointer(win->window,WA_Pointer,ptr,TAG_END);
         }
      }
      else
      {  /* Use custom pointers for older Intuition versions */
         ptr=Apppointer(Aweb(),ptrtype);
         SetWindowPointer(win->window,WA_Pointer,ptr,TAG_END);
      }
      win->ptrtype=ptrtype;
   }
}

/* Make menu bar and set it */
static void Newwindowmenus(struct Awindow *win,struct NewMenu *menus)
{  struct DrawInfo *drinfo=NULL;
   void *vinfo=NULL;
   if(menus)
   {  Agetattrs(Aweb(),
         AOAPP_Drawinfo,&drinfo,
         AOAPP_Visualinfo,&vinfo,
         TAG_END);
      Setmenus(win,menus);
      if((win->menu=CreateMenus(menus,
         GTMN_FrontPen,drinfo->dri_Pens[BARDETAILPEN],
         TAG_END))
      && LayoutMenus(win->menu,vinfo,TAG_END)
      && win->window)
      {  SetMenuStrip(win->window,win->menu);
      }
   }
}

/* Remove menu bar and free it */
static void Freewindowmenus(struct Awindow *win)
{  if(win->window)
   {  ClearMenuStrip(win->window);
   }
   if(win->menu)
   {  FreeMenus(win->menu);
      win->menu=NULL;
   }
}

/* Update the slider. Scale attributes if necessary. If top==~0, use existing
 * top value. */
static void Setslider(struct Awindow *win,struct Scrollgad *gs,
   ULONG total,ULONG vis,ULONG top)
{  short shift=0;
   if(total!=gs->total || vis!=gs->vis || top!=~0)
   {  gs->total=total;
      gs->vis=vis;
      while(total>0xffff)
      {  total>>=1;
         shift++;
      }
      vis>>=shift;
      if(top==~0 && shift!=gs->shift)
      {  /* We must scale the existing top value too */
         GetAttr(PGA_Top,gs->gad,&top);
         top<<=gs->shift;
      }
      gs->shift=shift;
      if(top!=~0) top>>=shift;
      Setgadgetattrs(gs->gad,win->window,NULL,
         PGA_Total,total,
         PGA_Visible,vis,
         top!=~0?PGA_Top:TAG_IGNORE,top,
         TAG_END);
   }
}

/* Speedbar sets its minimum size too large if too many buttons are there, but
 * needs at least 1 button to get its height right.
 * Actually it needs 2 because with 1 button the height isn't calculated correctly...
 */

/* Add a speedbar node for this button */
static void Addspeedbutton(struct Awindow *win,struct DrawInfo *dri,
   struct Userbutton *ub,short n)
{  struct Node *node;
   void *label;
   label=LabelObject,
         LABEL_DrawInfo,dri,
         LABEL_Text,Dupstr(ub->label,-1),
            /* will dangle in the memory pool after disposal of the label...
             * must copy it because new buttons will trash the label text otherwise */
      End;
   if(label)
   {  SetAttrs(label,IA_Width,Getvalue(label,IA_Width)+4,TAG_END);
      node=AllocSpeedButtonNode(n,
         SBNA_Image,label,
         SBNA_Spacing,0,
         SBNA_Enabled,BOOLVAL(n>=0),
         TAG_END);
      if(node)
      {  AddTail(&win->userbutlist,node);
      }
   }
}

/* Create the user button row with 2 bogus buttons */
static void *Makebuttonrow(struct Awindow *win,struct DrawInfo *dri)
{  void *buttonrow=NULL;
   win->ubutgad=NULL;
   if(!ISEMPTY(&prefs.buttons) && prefs.showbuttons && (win->flags&WINF_BUTTONS))
   {  Addspeedbutton(win,dri,prefs.buttons.first,-1);
      Addspeedbutton(win,dri,prefs.buttons.first,-1);
      buttonrow=HLayoutObject,
         LAYOUT_SpaceOuter,TRUE,
         LAYOUT_SpaceInner,FALSE,
         LAYOUT_TopSpacing,0,
         StartMember,win->ubutgad=SpeedBarObject,
            GA_ID,GID_UBUTTON,
            GA_RelVerify,TRUE,
            SPEEDBAR_EvenSize,TRUE,
            SPEEDBAR_Buttons,&win->userbutlist,
            SPEEDBAR_BevelStyle,BVS_NONE,
            REACTION_SpecialPens,&win->capens,
         EndMember,
      End;
   }
   return buttonrow;
}

/* Create the the real user buttons */
static void Completebuttonrow(struct Awindow *win,struct DrawInfo *dri)
{  struct Userbutton *ub;
   short n;
   if(win->ubutgad)
   {  Setgadgetattrs(win->ubutgad,NULL,NULL,SPEEDBAR_Buttons,~0,TAG_END);
      for(ub=prefs.buttons.first,n=0;ub->next;ub=ub->next,n++)
      {  Addspeedbutton(win,dri,ub,n);
      }
      Setgadgetattrs(win->ubutgad,NULL,NULL,SPEEDBAR_Buttons,&win->userbutlist,TAG_END);
   }
}

static void Rebuildbuttonrow(struct Awindow *win,struct DrawInfo *dri)
{  struct Node *node;
   void *label;
   struct Userbutton *ub;
   short n;
   if(win->ubutgad)
   {  Setgadgetattrs(win->ubutgad,win->window,NULL,SPEEDBAR_Buttons,~0,TAG_END);
      while(node=RemHead(&win->userbutlist))
      {  GetSpeedButtonNodeAttrs(node,SBNA_Image,&label,TAG_END);
         DisposeObject(label);
         FreeSpeedButtonNode(node);
      }
      Addspeedbutton(win,dri,prefs.buttons.first,-1);
      Addspeedbutton(win,dri,prefs.buttons.first,-1);
      for(ub=prefs.buttons.first,n=0;ub->next;ub=ub->next,n++)
      {  Addspeedbutton(win,dri,ub,n);
      }
      Setgadgetattrs(win->ubutgad,win->window,NULL,SPEEDBAR_Buttons,&win->userbutlist,TAG_END);
   }
}

/* Build classic layout with URL bar, status bar, and navigation buttons */
static void *BuildClassicLayout(struct Awindow *win,struct DrawInfo *dri,UBYTE *urlname)
{  return HLayoutObject,
      LAYOUT_SpaceOuter,TRUE,
      StartMember,VLayoutObject,
         StartMember,HLayoutObject,
            LAYOUT_SpaceInner,FALSE,
            StartMember,win->urlgad=StringObject,
               GA_ID,GID_URL,
               GA_RelVerify,TRUE,
               GA_Immediate,TRUE,
               STRINGA_MaxChars,MAXSTRBUFCHARS,
               STRINGA_Buffer,win->urlbuf,
               STRINGA_UndoBuffer,buf2,
               STRINGA_WorkBuffer,buf3,
               STRINGA_TextVal,urlname?urlname:NULLSTRING,
               REACTION_SpecialPens,&win->capens,
            EndMember,
            StartMember,win->urlpopgad=ChooserObject,
               GA_ID,GID_URLPOP,
               GA_RelVerify,TRUE,
               CHOOSER_DropDown,TRUE,
               CHOOSER_Labels,&win->urlpoplist,
               CHOOSER_AutoFit,TRUE,
               REACTION_SpecialPens,&win->capens,
            EndMember,
         EndMember,
         StartMember,HLayoutObject,
            LAYOUT_SpaceInner,FALSE,
            StartMember,win->statusgad=NewObject(Stagadclass(),NULL,
               STATGA_SpecialPens,&win->capens,
               GA_Text,win->statustext,
            EndMember,
/* #ifndef DEMOVERSION */
            StartMember,win->securegad=ButtonObject,
               GA_Image,win->unsecureimg,
               GA_ReadOnly,TRUE,
               REACTION_SpecialPens,&win->capens,
            EndMember,
            CHILD_WeightedWidth,0,
/* #endif */
         EndMember,
      EndMember,
      StartMember,win->ledgad=Animgadget(Aweb(),&win->capens),
      CHILD_WeightedHeight,0,
      StartMember,VLayoutObject,
         LAYOUT_InnerSpacing,1,
         StartMember,HLayoutObject,
            LAYOUT_InnerSpacing,1,
            StartMember,win->backgad=ButtonObject,
               GA_Image,win->backimg,
               GA_Disabled,TRUE,
               GA_ID,GID_NAV+0,
               GA_RelVerify,TRUE,
               GA_Immediate,TRUE,
               REACTION_SpecialPens,&win->capens,
            EndMember,
            StartMember,win->fwdgad=ButtonObject,
               GA_Image,win->fwdimg,
               GA_Disabled,TRUE,
               GA_ID,GID_NAV+1,
               GA_RelVerify,TRUE,
               GA_Immediate,TRUE,
               REACTION_SpecialPens,&win->capens,
            EndMember,
            StartMember,win->homegad=ButtonObject,
               GA_Image,win->homeimg,
               GA_ID,GID_NAV+2,
               GA_RelVerify,TRUE,
               GA_Immediate,TRUE,
               REACTION_SpecialPens,&win->capens,
            EndMember,
            StartMember,win->addhotgad=ButtonObject,
               GA_Image,win->addhotimg,
               GA_ID,GID_NAV+3,
               GA_RelVerify,TRUE,
               GA_Immediate,TRUE,
               REACTION_SpecialPens,&win->capens,
            EndMember,
            StartMember,win->hotgad=ButtonObject,
               GA_Image,win->hotimg,
               GA_ID,GID_NAV+4,
               GA_RelVerify,TRUE,
               GA_Immediate,TRUE,
               REACTION_SpecialPens,&win->capens,
            EndMember,
         EndMember,
         StartMember,HLayoutObject,
            LAYOUT_InnerSpacing,1,
            StartMember,win->cancelgad=ButtonObject,
               GA_Image,win->cancelimg,
               GA_Disabled,!(win->flags&WINF_INPUT),
               GA_ID,GID_NAV+5,
               GA_RelVerify,TRUE,
               GA_Immediate,TRUE,
               REACTION_SpecialPens,&win->capens,
            EndMember,
            StartMember,win->nwsgad=ButtonObject,
               GA_Image,win->nwsimg,
               GA_ID,GID_NAV+6,
               GA_RelVerify,TRUE,
               GA_Immediate,TRUE,
               REACTION_SpecialPens,&win->capens,
            EndMember,
            StartMember,win->searchgad=ButtonObject,
               GA_Image,win->searchimg,
               GA_ID,GID_NAV+7,
               GA_RelVerify,TRUE,
               GA_Immediate,TRUE,
               REACTION_SpecialPens,&win->capens,
            EndMember,
            StartMember,win->rldgad=ButtonObject,
               GA_Image,win->rldimg,
               GA_ID,GID_NAV+8,
               GA_RelVerify,TRUE,
               GA_Immediate,TRUE,
               REACTION_SpecialPens,&win->capens,
            EndMember,
            StartMember,win->imggad=ButtonObject,
               GA_Image,win->imgimg,
               GA_ID,GID_NAV+9,
               GA_RelVerify,TRUE,
               GA_Immediate,TRUE,
               REACTION_SpecialPens,&win->capens,
            EndMember,
         EndMember,
      EndMember,
      CHILD_WeightedWidth,0,
   EndMember;
}

/* Build modern Chrome-like layout: back, forward, reload, securegad, urlgad, addhot */
static void *BuildModernLayout(struct Awindow *win,struct DrawInfo *dri,UBYTE *urlname)
{  void *layout;
   /* Validate required images exist before building layout */
   if(!win->backimg || !win->fwdimg || !win->rldimg || !win->addhotimg)
   {  return NULL;
   }
   if(!win->unsecureimg)
   {  return NULL;
   }
   layout=HLayoutObject,
      LAYOUT_SpaceOuter,TRUE,
      LAYOUT_InnerSpacing,4,
      LAYOUT_TopSpacing,4,
      LAYOUT_BottomSpacing,4,
      LAYOUT_LeftSpacing,4,
      LAYOUT_RightSpacing,4,
      StartMember,win->backgad=ButtonObject,
         GA_Image,win->backimg,
         GA_Disabled,TRUE,
         GA_ID,GID_NAV+0,
         GA_RelVerify,TRUE,
         GA_Immediate,TRUE,
         BUTTON_BevelStyle,BVS_NONE,
         REACTION_SpecialPens,&win->capens,
      EndMember,
      CHILD_WeightedWidth,0,
      StartMember,win->fwdgad=ButtonObject,
         GA_Image,win->fwdimg,
         GA_Disabled,TRUE,
         GA_ID,GID_NAV+1,
         GA_RelVerify,TRUE,
         GA_Immediate,TRUE,
         BUTTON_BevelStyle,BVS_NONE,
         REACTION_SpecialPens,&win->capens,
      EndMember,
      CHILD_WeightedWidth,0,
      StartMember,win->rldgad=ButtonObject,
         GA_Image,win->rldimg,
         GA_ID,GID_NAV+8,
         GA_RelVerify,TRUE,
         GA_Immediate,TRUE,
         BUTTON_BevelStyle,BVS_NONE,
         REACTION_SpecialPens,&win->capens,
      EndMember,
      CHILD_WeightedWidth,0,
      StartMember,win->urlgad=StringObject,
         GA_ID,GID_URL,
         GA_RelVerify,TRUE,
         GA_Immediate,TRUE,
         STRINGA_MaxChars,MAXSTRBUFCHARS,
         STRINGA_Buffer,win->urlbuf,
         STRINGA_UndoBuffer,buf2,
         STRINGA_WorkBuffer,buf3,
         STRINGA_TextVal,urlname?urlname:NULLSTRING,
         REACTION_SpecialPens,&win->capens,
      EndMember,
      CHILD_WeightedWidth,1,
/* #ifndef DEMOVERSION */
      StartMember,win->securegad=ButtonObject,
         GA_Image,win->unsecureimg,
         GA_ReadOnly,TRUE,
         BUTTON_BevelStyle,BVS_NONE,
         REACTION_SpecialPens,&win->capens,
      EndMember,
      CHILD_WeightedWidth,0,
/*#endif DEMOVERSION */
      StartMember,win->addhotgad=ButtonObject,
         GA_Image,win->addhotimg,
         GA_ID,GID_NAV+3,
         GA_RelVerify,TRUE,
         GA_Immediate,TRUE,
         BUTTON_BevelStyle,BVS_NONE,
         REACTION_SpecialPens,&win->capens,
      EndMember,
      CHILD_WeightedWidth,0,
   EndMember;
   if(!layout) return NULL;
   return layout;
}

/* Open intuition window */
static BOOL Openwindow(struct Awindow *win)
{  void *url;
   UBYTE *urlname;
   struct Screen *screen=NULL;
   struct ColorMap *colormap=NULL;
   struct DrawInfo *drinfo=NULL;
   struct TextFont *font=NULL;
   struct MsgPort *windowport=NULL;
   struct MsgPort *appwindowport;
   struct NewMenu *menus=NULL;
   struct LayoutLimits limits={0};
   struct Node *node;
   void *buttonrow;
   ULONG bgrgb[3];
   short i;
   /* trace removed */
   url=(void *)Agetattr(win->frame,AOFRM_Url);
   urlname=(UBYTE *)Agetattr(url,AOURL_Url);
   if(ISEMPTY(&win->urlpoplist))
   {  for(i=0;urlpops[i];i++)
      {  if(node=AllocChooserNode(CNA_Text,urlpops[i],TAG_END))
         {  AddTail(&win->urlpoplist,node);
         }
      }
   }
   Agetattrs(Aweb(),
      AOAPP_Screen,&screen,
      AOAPP_Colormap,&colormap,
      AOAPP_Drawinfo,&drinfo,
      AOAPP_Screenfont,&font,
      AOAPP_Windowport,&windowport,
      AOAPP_Menus,&menus,
      TAG_END);
   Asetattrs(Aweb(),
      AOAPP_Processtype,AOTP_WINDOW,
      AOAPP_Processfun,Processwindow,
      TAG_END);
   if(!win->box.Width)
   {  short calc_width,calc_height;
      short screen_width,screen_height;
      short max_width,max_height;
      struct Screen screendata;
      
      /* Calculate window size from preferences */
      calc_width=prefs.winw;
      calc_height=prefs.winh;
      
      /* If preferences indicate maximum size (9999), calculate sensible default */
      if(calc_width==9999 || calc_height==9999)
      {  /* Get screen dimensions using GetScreenData() instead of accessing private members */
         if(GetScreenData(&screendata,sizeof(struct Screen),CUSTOMSCREEN,screen))
         {  screen_width=screendata.Width;
            screen_height=screendata.Height;
         }
         else
         {  /* Fallback: use screen pointer if GetScreenData fails */
            screen_width=screen->Width;
            screen_height=screen->Height;
         }
         
         /* For small screens (640x512 or smaller), use existing logic (let Intuition clamp) */
         if(screen_width<=640 && screen_height<=512)
         {  /* Keep 9999, Intuition will clamp to screen size */
            if(calc_width==9999) calc_width=9999;
            if(calc_height==9999) calc_height=9999;
         }
         /* For larger screens (1024x768 or above), use 75% of screen size */
         else if(screen_width>=1024 && screen_height>=768)
         {  if(calc_width==9999) calc_width=(screen_width*75)/100;
            if(calc_height==9999) calc_height=(screen_height*75)/100;
         }
         /* For medium screens, use 90% up to 800x600 maximum */
         else
         {  if(calc_width==9999)
            {  calc_width=(screen_width*90)/100;
               max_width=800;
               if(calc_width>max_width) calc_width=max_width;
            }
            if(calc_height==9999)
            {  calc_height=(screen_height*90)/100;
               max_height=600;
               if(calc_height>max_height) calc_height=max_height;
            }
         }
      }
      else
      {  /* Still need screen dimensions for position calculation */
         if(GetScreenData(&screendata,sizeof(struct Screen),CUSTOMSCREEN,screen))
         {  screen_width=screendata.Width;
            screen_height=screendata.Height;
         }
         else
         {  screen_width=screen->Width;
            screen_height=screen->Height;
         }
      }
      
      if(nextx<0)
      {  nextx=prefs.winx;
         nexty=prefs.winy;
      }
      win->box.Left=nextx;
      win->box.Top=nexty;
      nextx+=20;
      nexty+=8;
      if(nextx+calc_width>screen_width) nextx=0;
      if(nexty+calc_height>screen_height) nexty=0;
      win->box.Width=calc_width;
      win->box.Height=calc_height;
      if(win->newwidth || win->newheight)
      {  struct Awindow *awin;
         for(awin=windows.first;awin && awin->next;awin=awin->next)
         {  if(awin->window)
            {  if(win->newwidth)
               {  win->box.Width=awin->window->Width-awin->spacegad->Width+win->newwidth;
                  if(win->box.Width<awin->window->MinWidth)
                  {  win->box.Width=awin->window->MinWidth;
                  }
               }
               if(win->newheight)
               {  win->box.Height=awin->window->Height-awin->spacegad->Height+win->newheight;
                  if(win->box.Height<awin->window->MinHeight)
                  {  win->box.Height=awin->window->MinHeight;
                  }
               }
            }
            break;
         }
      }
      win->zoombox.Left=prefs.wiax;
      win->zoombox.Top=prefs.wiay;
      win->zoombox.Width=prefs.wiaw;
      win->zoombox.Height=prefs.wiah;
   }

   GetRGB32(colormap,drinfo->dri_Pens[BACKGROUNDPEN],1,bgrgb);
   win->capens.sp_LightPen=ObtainBestPen(colormap,
      bgrgb[0]+0x22222222,bgrgb[1]+0x22222222,bgrgb[2]+0x22222222,
      OBP_FailIfBad,FALSE,
      OBP_Precision,PRECISION_EXACT,
      TAG_END);
   win->capens.sp_DarkPen=ObtainBestPen(colormap,
      bgrgb[0]-0x22222222,bgrgb[1]-0x22222222,bgrgb[2]-0x22222222,
      OBP_FailIfBad,FALSE,
      OBP_Precision,PRECISION_EXACT,
      TAG_END);

   /* Check if intuition.library v46+ is available for iconify gadget support */
   if(IntuitionBase && IntuitionBase->lib_Version>=46)
   {  if(!(win->window=OpenWindowTags(NULL,
         WA_Left,win->box.Left,
         WA_Top,win->box.Top,
         WA_Width,win->box.Width,
         WA_Height,win->box.Height,
         WA_PubScreen,screen,
         WA_SizeGadget,TRUE,
         WA_DragBar,TRUE,
         WA_DepthGadget,TRUE,
         WA_CloseGadget,TRUE,
         WA_IconifyGadget,TRUE,
         WA_Activate,TRUE,
         WA_SimpleRefresh,TRUE,
         WA_SizeBRight,TRUE,
         WA_SizeBBottom,TRUE,
         WA_NewLookMenus,TRUE,
         WA_AutoAdjust,TRUE,
         WA_ReportMouse,TRUE,
         WA_MaxWidth,-1,
         WA_MaxHeight,-1,
         WA_Zoom,&win->zoombox,
         TAG_END))) return FALSE;
   }
   else
   {  if(!(win->window=OpenWindowTags(NULL,
         WA_Left,win->box.Left,
         WA_Top,win->box.Top,
         WA_Width,win->box.Width,
         WA_Height,win->box.Height,
         WA_PubScreen,screen,
         WA_SizeGadget,TRUE,
         WA_DragBar,TRUE,
         WA_DepthGadget,TRUE,
         WA_CloseGadget,TRUE,
         WA_Activate,TRUE,
         WA_SimpleRefresh,TRUE,
         WA_SizeBRight,TRUE,
         WA_SizeBBottom,TRUE,
         WA_NewLookMenus,TRUE,
         WA_AutoAdjust,TRUE,
         WA_ReportMouse,TRUE,
         WA_MaxWidth,-1,
         WA_MaxHeight,-1,
         WA_Zoom,&win->zoombox,
         TAG_END))) return FALSE;
   }
   Settitle(win,AWEBSTR(MSG_AWEB_NODOCTITLE));
   Setactiveport(win->portname);
   activewindow=win;
   /* Update screen title when window is first opened */
   /* Updatescreentitle(win); */  /* COMMENTED OUT: Screen title feature temporarily disabled */
   win->window->UserData=(BYTE *)win;
   win->window->UserPort=windowport;
   /* Add IDCMP_EXTENDEDMOUSE if intuition.library v47+ supports mousewheel */
   if(IntuitionBase && IntuitionBase->lib_Version>=47)
   {  ModifyIDCMP(win->window,idcmpflags|IDCMP_EXTENDEDMOUSE);
   }
   else
   {  ModifyIDCMP(win->window,idcmpflags);
   }
   Newwindowmenus(win,menus);
   if(!(win->downimg=(struct Image *)NewObject(NULL,"sysiclass",
      SYSIA_DrawInfo,drinfo,
      SYSIA_Which,DOWNIMAGE,
      TAG_END))) return FALSE;
   if(!(win->upimg=(struct Image *)NewObject(NULL,"sysiclass",
      SYSIA_DrawInfo,drinfo,
      SYSIA_Which,UPIMAGE,
      TAG_END))) return FALSE;
   if(!(win->rightimg=(struct Image *)NewObject(NULL,"sysiclass",
      SYSIA_DrawInfo,drinfo,
      SYSIA_Which,RIGHTIMAGE,
      TAG_END))) return FALSE;
   if(!(win->leftimg=(struct Image *)NewObject(NULL,"sysiclass",
      SYSIA_DrawInfo,drinfo,
      SYSIA_Which,LEFTIMAGE,
      TAG_END))) return FALSE;

   if(prefs.shownav && (win->flags&WINF_NAVS))
   {  if(!(win->backimg=Buttonimage(Aweb(),BUTF_BACK,backdata,DEFAULTWIDTH,DEFAULTHEIGHT))) return FALSE;
      if(!(win->fwdimg=Buttonimage(Aweb(),BUTF_FORWARD,fwddata,DEFAULTWIDTH,DEFAULTHEIGHT))) return FALSE;
      if(!(win->homeimg=Buttonimage(Aweb(),BUTF_HOME,homedata,DEFAULTWIDTH,DEFAULTHEIGHT))) return FALSE;
      if(!(win->addhotimg=Buttonimage(Aweb(),BUTF_ADDHOTLIST,addhotdata,DEFAULTWIDTH,DEFAULTHEIGHT))) return FALSE;
      if(!(win->hotimg=Buttonimage(Aweb(),BUTF_HOTLIST,hotdata,DEFAULTWIDTH,DEFAULTHEIGHT))) return FALSE;
      if(!(win->cancelimg=Buttonimage(Aweb(),BUTF_CANCEL,canceldata,DEFAULTWIDTH,DEFAULTHEIGHT))) return FALSE;
      if(!(win->nwsimg=Buttonimage(Aweb(),BUTF_NETSTATUS,nwsdata,DEFAULTWIDTH,DEFAULTHEIGHT))) return FALSE;
      if(!(win->searchimg=Buttonimage(Aweb(),BUTF_SEARCH,searchdata,DEFAULTWIDTH,DEFAULTHEIGHT))) return FALSE;
      if(!(win->rldimg=Buttonimage(Aweb(),BUTF_RELOAD,rlddata,DEFAULTWIDTH,DEFAULTHEIGHT))) return FALSE;
      if(!(win->imgimg=Buttonimage(Aweb(),BUTF_LOADIMAGES,imgdata,DEFAULTWIDTH,DEFAULTHEIGHT))) return FALSE;
/*#ifndef DEMOVERSION */
      if(!(win->unsecureimg=Buttonimage(Aweb(),BUTF_UNSECURE,unsecuredata,DEFAULTWIDTH,DEFAULTHEIGHT))) return FALSE;
      if(!(win->secureimg=Buttonimage(Aweb(),BUTF_SECURE,securedata,DEFAULTWIDTH,DEFAULTHEIGHT))) return FALSE;
/* #endif DEMOVERSION */
   }

   if(!(win->downarrow=(struct Gadget *)NewObject(NULL,"buttongclass",
      GA_RelRight,-win->window->BorderRight+1,
      GA_RelBottom,-win->window->BorderBottom-win->downimg->Height+1,
      GA_Image,win->downimg,
      GA_ID,GID_DOWN,
      ICA_TARGET,ICTARGET_IDCMP,
      TAG_END))) return FALSE;
   if(!(win->uparrow=(struct Gadget *)NewObject(NULL,"buttongclass",
      GA_RelRight,-win->window->BorderRight+1,
      GA_RelBottom,-win->window->BorderBottom-
         win->downimg->Height-win->upimg->Height+1,
      GA_Image,win->upimg,
      GA_ID,GID_UP,
      ICA_TARGET,ICTARGET_IDCMP,
      GA_Previous,win->downarrow,
      TAG_END))) return FALSE;
   if(!(win->vslider.gad=(struct Gadget *)NewObject(NULL,"propgclass",
      GA_RelRight,-win->window->BorderRight+5,
      GA_Width,win->window->BorderRight-8,
      GA_Top,win->window->BorderTop+2,
      GA_RelHeight,-win->window->BorderTop-win->window->BorderBottom-
         win->downimg->Height-win->upimg->Height-4,
      GA_ID,GID_VSLIDER,
      PGA_Freedom,FREEVERT,
      PGA_Total,1,
      PGA_Visible,1,
      PGA_NewLook,TRUE,
      PGA_Borderless,TRUE,
      ICA_TARGET,ICTARGET_IDCMP,
      GA_Previous,win->uparrow,
      TAG_END))) return FALSE;
   if(!(win->rightarrow=(struct Gadget *)NewObject(NULL,"buttongclass",
      GA_RelRight,-win->window->BorderRight-win->rightimg->Width+1,
      GA_RelBottom,-win->window->BorderBottom+1,
      GA_Image,win->rightimg,
      GA_ID,GID_RIGHT,
      ICA_TARGET,ICTARGET_IDCMP,
      GA_Previous,win->vslider.gad,
      TAG_END))) return FALSE;
   if(!(win->leftarrow=(struct Gadget *)NewObject(NULL,"buttongclass",
      GA_RelRight,-win->window->BorderRight-
         win->rightimg->Width-win->leftimg->Width+1,
      GA_RelBottom,-win->window->BorderBottom+1,
      GA_Image,win->leftimg,
      GA_ID,GID_LEFT,
      ICA_TARGET,ICTARGET_IDCMP,
      GA_Previous,win->rightarrow,
      TAG_END))) return FALSE;
   if(!(win->hslider.gad=(struct Gadget *)NewObject(NULL,"propgclass",
      GA_Left,win->window->BorderLeft,
      GA_RelBottom,-win->window->BorderBottom+3,
      GA_RelWidth,-win->window->BorderLeft-win->window->BorderRight-
         win->rightimg->Width-win->leftimg->Width-2,
      GA_Height,win->window->BorderBottom-4,
      GA_ID,GID_HSLIDER,
      PGA_Freedom,FREEHORIZ,
      PGA_Total,1,
      PGA_Visible,1,
      PGA_NewLook,TRUE,
      PGA_Borderless,TRUE,
      ICA_TARGET,ICTARGET_IDCMP,
      GA_Previous,win->leftarrow,
      TAG_END))) return FALSE;

   buttonrow=Makebuttonrow(win,drinfo);

   win->layoutgad=VLayoutObject,
      GA_ID,GID_LAYOUT,
      LAYOUT_SpaceInner,FALSE,
      TAG_END);
   if(!win->layoutgad) return FALSE;
   if(prefs.shownav && (win->flags&WINF_NAVS))
   {  void *navlayout;
      if(win->layoutstyle==1)
      {  
         /* Initialize unused gadgets to NULL for modern layout before building */
         win->urlpopgad=NULL;
         win->statusgad=NULL;
         win->ledgad=NULL;
         win->homegad=NULL;
         win->hotgad=NULL;
         win->cancelgad=NULL;
         win->nwsgad=NULL;
         win->searchgad=NULL;
         win->imggad=NULL;
         navlayout=BuildModernLayout(win,drinfo,urlname);
         if(navlayout)
         {  
            SetAttrs(win->layoutgad,
               StartMember,navlayout,
               CHILD_WeightedHeight,0,
               TAG_END);
         }
         else
         { 
            win->backgad=NULL;
            win->fwdgad=NULL;
            win->rldgad=NULL;
            win->homegad=NULL;
            win->cancelgad=NULL;
            win->hotgad=NULL;
            win->imggad=NULL;
            win->nwsgad=NULL;
            win->urlgad=NULL;
            win->urlpopgad=NULL;
            win->statusgad=NULL;
            win->searchgad=NULL;
            win->ledgad=NULL;
            win->addhotgad=NULL;
            win->securegad=NULL;
            return FALSE;
         }
      }
      else
      { 
         navlayout=BuildClassicLayout(win,drinfo,urlname);
         if(navlayout)
         { 
            SetAttrs(win->layoutgad,
               StartMember,navlayout,
               CHILD_WeightedHeight,0,
               TAG_END);
         }
         else
         {  
            win->backgad=NULL;
            win->fwdgad=NULL;
            win->rldgad=NULL;
            win->homegad=NULL;
            win->cancelgad=NULL;
            win->hotgad=NULL;
            win->imggad=NULL;
            win->nwsgad=NULL;
            win->urlgad=NULL;
            win->urlpopgad=NULL;
            win->statusgad=NULL;
            win->searchgad=NULL;
            win->ledgad=NULL;
            win->addhotgad=NULL;
            win->securegad=NULL;
            return FALSE;
         }
      }
   }
   else
   {
      win->backgad=NULL;
      win->fwdgad=NULL;
      win->rldgad=NULL;
      win->homegad=NULL;
      win->cancelgad=NULL;
      win->hotgad=NULL;
      win->imggad=NULL;
      win->nwsgad=NULL;
      win->urlgad=NULL;
      win->urlpopgad=NULL;
      win->statusgad=NULL;
      win->searchgad=NULL;
      win->ledgad=NULL;
      win->addhotgad=NULL;
      win->securegad=NULL;
   }
   if(buttonrow)
   { 
      if(!(prefs.shownav && (win->flags&WINF_NAVS)))
      { 
         SetAttrs(win->layoutgad,
            StartMember,SpaceObject,
            EndMember,
            CHILD_MinHeight,2,
            CHILD_MaxHeight,2,
            TAG_END);
      }
      SetAttrs(win->layoutgad,
         StartMember,buttonrow,
         CHILD_WeightedHeight,0,
         TAG_END);
   }
   SetAttrs(win->layoutgad,
      StartMember,VLayoutObject,
         LAYOUT_BevelStyle,(buttonrow || (prefs.shownav && (win->flags&WINF_NAVS)))?
            BVS_SBAR_VERT:BVS_NONE,
         LAYOUT_SpaceOuter,TRUE,
         StartMember,win->spacegad=SpaceObject,
         EndMember,
         CHILD_MinHeight,40,
         CHILD_MinWidth,80,
      EndMember,
      TAG_END);
   if(win->layoutgad)
   {  LayoutLimits(win->layoutgad,&limits,NULL,screen);
      WindowLimits(win->window,
         limits.MinWidth+win->window->BorderLeft+win->window->BorderRight,
         limits.MinHeight+win->window->BorderTop+win->window->BorderBottom,
         0,0);
      SetGadgetAttrs(win->layoutgad,NULL,NULL,
         GA_Top,win->window->BorderTop,
         GA_Left,win->window->BorderLeft,
         GA_RelWidth,-win->window->BorderLeft-win->window->BorderRight,
         GA_RelHeight,-win->window->BorderTop-win->window->BorderBottom,
         ICA_TARGET,ICTARGET_IDCMP,
         TAG_END);
   }
   /* Verify that spacegad width matches window inner width for viewport accuracy.
    * The spacegad represents the inner content area and should match the window
    * inner width exactly. This ensures the viewport width calculation is correct. */
   if(win->spacegad)
   {  long expected_width,actual_width;
      expected_width=win->window->Width-win->window->BorderLeft-win->window->BorderRight;
      actual_width=win->spacegad->Width;
      /* If there's a mismatch, it may be due to layout constraints or UI elements.
       * The spacegad should fill the available space, so this check helps ensure
       * viewport width accuracy. */
      if(actual_width!=expected_width && expected_width>0)
      {  /* Spacegad width may differ due to layout constraints (button bars, etc.)
           * This is expected and the actual spacegad width should be used as the
           * viewport width. */
      }
   }
   if(buttonrow)
   {  Completebuttonrow(win,drinfo);
   }
   AddGList(win->window,win->downarrow,-1,-1,NULL);
   if(win->layoutgad)
   {  ((struct ExtGadget *)win->layoutgad)->MoreFlags&=~GMORE_SCROLLRASTER;
      AddGList(win->window,win->layoutgad,-1,-1,NULL);
   }
   RefreshGList(win->downarrow,win->window,NULL,-1);
   Asetattrs(win->frame,
      AOBJ_Width,win->window->Width,
      AOBJ_Height,win->window->Height,
      AOBJ_Window,win,
      TAG_END);
   SetABPenDrMd(win->window->RPort,1,0,JAM1);
   Arender(win->frame,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
   Adragrender(win->frame,NULL,NULL,0,NULL,0,AMDS_BEFORE);
   Sethisgadgets(win);
   /* Initialize reload button state for modern layout - start in reload mode */
   if(win->layoutstyle==1)
   {  win->flags&=~WINF_INPUT;  /* Ensure input flag is clear initially */
      Setreloadbutton(win,FALSE);  /* Start in reload mode, not cancel */
   }
   if(win->flags&WINF_ZOOMED)
   {
      ZipWindow(win->window);
   }
   Addanimgad(win->window,win->ledgad);
   if(appwindowport=(struct MsgPort *)Agetattr(Aweb(),AOAPP_Appwindowport))
   {
      win->appwindow=AddAppWindow(win->key,0,win->window,appwindowport,TAG_END);
   }
   /* trace removed */
   return TRUE;
}

/* Close the intuition window */
static void Closewindow(struct Awindow *win)
{  struct Node *node;
   struct ColorMap *colormap=(struct ColorMap *)Agetattr(Aweb(),AOAPP_Colormap);
   void *label;
   /* Clear screen title cache so we never use a pointer to a closed window */
   if(lastscreentitlewin == win) lastscreentitlewin = NULL;
   Remanimgad(win->ledgad);
   if(win->frame)
   {  Asetattrs(win->frame,AOBJ_Window,NULL,TAG_END);
   }
   if(win->popup)
   {  Adisposeobject(win->popup);
      win->popup=NULL;
   }
   Freewindowmenus(win);
   if(win->appwindow)
   {  RemoveAppWindow(win->appwindow);
      win->appwindow=NULL;
   }
   if(win->window)
   {  Safeclosewindow(win->window);
      win->window=NULL;
   }
   if(win->capens.sp_LightPen!=-1)
   {  ReleasePen(colormap,win->capens.sp_LightPen);
      win->capens.sp_LightPen=-1;
   }
   if(win->capens.sp_DarkPen!=-1)
   {  ReleasePen(colormap,win->capens.sp_DarkPen);
      win->capens.sp_DarkPen=-1;
   }
   while(node=RemHead(&win->userbutlist))
   {  GetSpeedButtonNodeAttrs(node,SBNA_Image,&label,TAG_END);
      DisposeObject(label);
      FreeSpeedButtonNode(node);
   }
   if(win->downarrow) DisposeObject(win->downarrow);win->downarrow=NULL;
   if(win->uparrow) DisposeObject(win->uparrow);win->uparrow=NULL;
   if(win->leftarrow) DisposeObject(win->leftarrow);win->leftarrow=NULL;
   if(win->rightarrow) DisposeObject(win->rightarrow);win->rightarrow=NULL;
   if(win->vslider.gad) DisposeObject(win->vslider.gad);win->vslider.gad=NULL;
   if(win->hslider.gad) DisposeObject(win->hslider.gad);win->hslider.gad=NULL;
   if(win->layoutgad) DisposeObject(win->layoutgad);win->layoutgad=NULL;
   if(win->downimg) DisposeObject(win->downimg);win->downimg=NULL;
   if(win->upimg) DisposeObject(win->upimg);win->upimg=NULL;
   if(win->rightimg) DisposeObject(win->rightimg);win->rightimg=NULL;
   if(win->leftimg) DisposeObject(win->leftimg);win->leftimg=NULL;
   if(win->backimg) DisposeObject(win->backimg);win->backimg=NULL;
   if(win->fwdimg) DisposeObject(win->fwdimg);win->fwdimg=NULL;
   if(win->rldimg) DisposeObject(win->rldimg);win->rldimg=NULL;
   if(win->homeimg) DisposeObject(win->homeimg);win->homeimg=NULL;
   if(win->cancelimg) DisposeObject(win->cancelimg);win->cancelimg=NULL;
   if(win->hotimg) DisposeObject(win->hotimg);win->hotimg=NULL;
   if(win->imgimg) DisposeObject(win->imgimg);win->imgimg=NULL;
   if(win->nwsimg) DisposeObject(win->nwsimg);win->nwsimg=NULL;
   if(win->addhotimg) DisposeObject(win->addhotimg);win->addhotimg=NULL;
   if(win->unsecureimg) DisposeObject(win->unsecureimg);win->unsecureimg=NULL;
   if(win->secureimg) DisposeObject(win->secureimg);win->secureimg=NULL;
   if(win->wintitle) FREE(win->wintitle);win->wintitle=NULL;
}

/* Process the App Message */
static void Processappmessage(struct Awindow *win,struct AppMessage *msg)
{  short i;
   UBYTE buffer[264]="file:///";
   UBYTE *filename=buffer+8;
   for(i=0;i<msg->am_NumArgs;i++)
   {  if(NameFromLock(msg->am_ArgList[i].wa_Lock,filename,256)
      && AddPart(filename,msg->am_ArgList[i].wa_Name,256))
      {  Followurlname(win,buffer,0);
      }
   }
}

/*------------------------------------------------------------------------*/

static long Setwindow(struct Awindow *win,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *status=(UBYTE *)~0,*hpstatus=(UBYTE *)~0;
   long total=0,read=0;
   ULONG htotal=win->hslider.total,hvis=win->hslider.vis,htop=~0;
   ULONG vtotal=win->vslider.total,vvis=win->vslider.vis,vtop=~0;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOAPP_Screenvalid:
            if(tag->ti_Data && !win->window)
            {  Openwindow(win);
            }
            else
            {  Closewindow(win);
            }
            break;
         case AOAPP_Menus:
            if(tag->ti_Data)
            {  if(win->window) Newwindowmenus(win,(struct NewMenu *)tag->ti_Data);
            }
            else
            {  Freewindowmenus(win);
            }
            break;
         case AOWIN_Title:
            if(tag->ti_Data) Settitle(win,(UBYTE *)tag->ti_Data);
            break;
         case AOWIN_Vslidertotal:
            vtotal=tag->ti_Data;
            break;
         case AOWIN_Vslidervisible:
            vvis=tag->ti_Data;
            break;
         case AOWIN_Vslidertop:
            vtop=tag->ti_Data;
            break;
         case AOWIN_Hslidertotal:
            htotal=tag->ti_Data;
            break;
         case AOWIN_Hslidervisible:
            hvis=tag->ti_Data;
            break;
         case AOWIN_Hslidertop:
            htop=tag->ti_Data;
            break;
         case AOWIN_Activeurl:
            if(win->activeurl)
            {  Aremchild(win->activeurl,win,AOREL_URL_WINDOW);
            }
            win->activeurl=(void *)tag->ti_Data;
            if(win->activeurl)
            {  Aaddchild(win->activeurl,win,AOREL_URL_WINDOW);
            }
            status=NULL;
            total=read=0;
            break;
         case AOWIN_Status:
            status=(UBYTE *)tag->ti_Data;
            break;
         case AOWIN_Read:
            read=tag->ti_Data;
            break;
         case AOWIN_Total:
            total=tag->ti_Data;
            break;
         case AOWIN_Hpstatus:
            hpstatus=(UBYTE *)tag->ti_Data;
            break;
         case AOWIN_Goinactive:
            if((!tag->ti_Data && win->activeobject)
            || (win->activeobject==(void *)tag->ti_Data))
            {  if(!tag->ti_Data)
               {  Agoinactive(win->activeobject);
               }
               win->activeobject=NULL;
               if(win->hittext)
               {  if(win->statushptext) FREE(win->statushptext);
                  win->statushptext=NULL;
                  if(win->window && win->statusgad)
                  {  Setgadgetattrs(win->statusgad,win->window,NULL,
                        STATGA_HPText,NULL,
                        TAG_END);
                  }
                  FREE(win->hittext);
                  win->hittext=NULL;
               }
               if(win->window)
               {  win->window->Flags&=~WFLG_RMBTRAP;
               }
            }
            if(!tag->ti_Data || win->hitobject==(void *)tag->ti_Data)
            {  win->hitobject=NULL;
            }
            if(!tag->ti_Data || win->jonmouse==(void *)tag->ti_Data)
            {  win->jonmouse=NULL;
            }
            break;
         case AOWIN_Currenturl:
            if(win->urlgad)
            {  Setgadgetattrs(win->urlgad,win->window,NULL,
                  STRINGA_TextVal,tag->ti_Data?(UBYTE *)tag->ti_Data:NULLSTRING,
                  STRINGA_DispPos,0,
                  TAG_END);
            }
            break;
         case AOWIN_Noproxy:
            if(!win->window) SETFLAG(win->flags,WINF_NOPROXY,tag->ti_Data);
            break;
         case AOBJ_Winhis:
            win->whis=(void *)tag->ti_Data;
            win->hiswhis=(void *)tag->ti_Data;
            Sethisgadgets(win);
            Setwhiswindow(win->hiswhis);
            win->dragstartobject=NULL;
            win->dragstartobjpos=0;
            win->dragendobject=NULL;
            win->dragendobjpos=0;
            Setsecure(win);
            Setwincancel(win);
            break;
         case AOWIN_Activate:
            if(tag->ti_Data && win->window)
            {  ActivateWindow(win->window);
            }
            break;
         case AOWIN_Nofocus:
            if(win->focus==(void *)tag->ti_Data) win->focus=NULL;
            if(win->nextfocus==(void *)tag->ti_Data) win->nextfocus=NULL;
            break;
         case AOWIN_Focus:
            if(tag->ti_Data && win->focus!=(void *)tag->ti_Data)
            {  Asetattrs(win->focus,AOFRM_Focus,FALSE,TAG_END);
               win->focus=(void *)tag->ti_Data;
               Setsecure(win);
               Asetattrs(win->focus,AOFRM_Focus,TRUE,TAG_END);
            }
            break;
         case AOWIN_Keepselection:
            SETFLAG(win->flags,WINF_KEEPDRAG,tag->ti_Data);
            break;
         case AOWIN_Clearselection:
            if(tag->ti_Data)
            {  Dragclear(win);
            }
            break;
         case AOWIN_Refresh:
            if(tag->ti_Data)
            {  Refreshwindow(win);
            }
            break;
         case AOWIN_Rmbtrap:
            if(win->window)
            {  SETFLAG(win->window->Flags,WFLG_RMBTRAP,tag->ti_Data);
            }
            break;
         case AOWIN_Activeobject:
            Setactiveobject(win,(void *)tag->ti_Data);
            break;
         case AOWIN_Innerwidth:
            win->newwidth=tag->ti_Data;
            break;
         case AOWIN_Innerheight:
            win->newheight=tag->ti_Data;
            break;
         case AOBJ_Pointertype:
            Setawinpointer(win,tag->ti_Data);
            break;
         case AOWIN_Hiswinhis:
            if(tag->ti_Data)
            {  Followhis(win,(void *)tag->ti_Data);
            }
            break;
         case AOWIN_Appmessage:
            Processappmessage(win,(struct AppMessage *)tag->ti_Data);
            break;
         case AOWIN_Navigation:
            SETFLAG(win->flags,WINF_NAVS,tag->ti_Data);
            break;
         case AOWIN_Buttonbar:
            SETFLAG(win->flags,WINF_BUTTONS,tag->ti_Data);
            break;
         case AOWIN_LayoutStyle:
            win->layoutstyle=tag->ti_Data;
            break;
      }
   }
   Setslider(win,&win->vslider,vtotal,vvis,vtop);
   Setslider(win,&win->hslider,htotal,hvis,htop);
   if(status!=(UBYTE *)~0)
   {  if(win->statustext) FREE(win->statustext);
      win->statustext=Dupstr(status,-1);
      if(win->window && win->statusgad)
      {  Setgadgetattrs(win->statusgad,win->window,NULL,
            GA_Text,status,
            PGA_Total,total,
            PGA_Visible,read,
            TAG_END);
      }
      /* For modern layout, show status in screen title briefly */
      /* COMMENTED OUT: Screen title feature temporarily disabled */
      /* if(win->layoutstyle == 1 && win->window)
      {  if(status && *status)
         {  Show status message in screen title
            struct timeval tv;
            UBYTE *statustitle;
            GetSysTime(&tv);
            win->statustime = tv.tv_secs;
            statustitle = Makestatusscreentitle(win, status);
            if(statustitle)
            {  SetWindowTitles(win->window,(UBYTE *)~0,statustitle);
               Store copy for comparison
               if(win->screentitle) FREE(win->screentitle);
               win->screentitle = Dupstr(statustitle, -1);
            }
         }
         else
         {  Status cleared, restore normal screen title immediately
            win->statustime = 0;
            Updatescreentitle(win);
         }
      } */
      /* Screen title is updated when window becomes active, not on every status change
       * to avoid corruption from frequent updates */
   }
   if(hpstatus!=(UBYTE *)~0)
   {  if(win->statushptext) FREE(win->statushptext);
      win->statushptext=Dupstr(hpstatus,-1);
      if(win->window && win->statusgad)
      {  Setgadgetattrs(win->statusgad,win->window,NULL,
            STATGA_HPText,hpstatus,
            TAG_END);
      }
   }
   return 0;
}

static void Disposewindow(struct Awindow *win)
{  void *p;
   Closewindow(win);
   Closearexxport(win->key);
   if(win->statustext) FREE(win->statustext);
   if(win->statushptext) FREE(win->statushptext);
   if(win->screentitle) FREE(win->screentitle);
   if(win->activeurl) Aremchild(win->activeurl,win,AOREL_URL_WINDOW);
   if(win->frame) Adisposeobject(win->frame);
   while(p=RemHead(&win->urlpoplist)) FreeChooserNode(p);
   Aremchild(Aweb(),win,AOREL_APP_USE_SCREEN);
   Aremchild(Aweb(),win,AOREL_APP_USE_MENUS);
   Amethodas(AOTP_OBJECT,win,AOM_DISPOSE);
}

static struct Awindow *Newwindow(struct Amset *ams)
{  struct Awindow *win;
   UBYTE *portname;
   BOOL screenvalid;
/* #ifdef DEMOVERSION
   if(windows.first->next) return NULL;
#endif */
   if(win=Allocobject(AOTP_WINDOW,sizeof(struct Awindow),ams))
   {  NewList(&win->urlpoplist);
      NewList(&win->userbutlist);
      ADDTAIL(&windows,win);
      win->key=++windowkey;
      win->capens.sp_DarkPen=-1;
      win->capens.sp_LightPen=-1;
      win->flags|=WINF_NAVS|WINF_BUTTONS;
      win->layoutstyle=0;  /* 0 = Original layout, 1 = Modern layout */
      win->statustime=0;    /* No status shown initially */
      SETFLAG(win->flags,WINF_CLIPDRAG,prefs.clipdrag);
      if(portname=Openarexxport(win->key))
      {  win->portname=Dupstr(portname,-1);
      }
      if(!(win->windownr=Arexxportnr(win->key))) win->windownr=win->key;
      Setwindow(win,ams);
      Aaddchild(Aweb(),win,AOREL_APP_USE_SCREEN);
      Aaddchild(Aweb(),win,AOREL_APP_USE_MENUS);
      screenvalid=Agetattr(Aweb(),AOAPP_Screenvalid);
      win->frame=Anewobject(AOTP_FRAME,
         AOBJ_Window,screenvalid?win:NULL,
         AOFRM_Topframe,TRUE,
         AOFRM_Name,GetTagData(AOWIN_Name,NULL,ams->tags),
         TAG_END);
      if(screenvalid)
      {  if(!Openwindow(win))
         {  REMOVE(win);
            Disposewindow(win);
            win=NULL;
         }
      }
   }
   return win;
}

static long Getwindow(struct Awindow *win,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOWIN_Window:
            PUTATTR(tag,win->window);
            break;
         case AOWIN_Rastport:
            PUTATTR(tag,win->window?win->window->RPort:NULL);
            break;
         case AOWIN_Innerleft:
            PUTATTR(tag,win->spacegad->LeftEdge);
            break;
         case AOWIN_Innertop:
            PUTATTR(tag,win->spacegad->TopEdge);
            break;
         case AOWIN_Innerwidth:
            PUTATTR(tag,win->spacegad->Width);
            break;
         case AOWIN_Innerheight:
            PUTATTR(tag,win->spacegad->Height);
            break;
         case AOWIN_Resized:
            PUTATTR(tag,BOOLVAL(win->flags&WINF_RESIZED));
            break;
         case AOWIN_Windownr:
            PUTATTR(tag,win->windownr);
            break;
         case AOWIN_Key:
            PUTATTR(tag,win->key);
            break;
         case AOBJ_Winhis:
            PUTATTR(tag,win->whis);
            break;
         case AOBJ_Frame:
            PUTATTR(tag,win->frame);
            break;
         case AOWIN_Noproxy:
            PUTATTR(tag,BOOLVAL(win->flags&WINF_NOPROXY));
            break;
         case AOWIN_Name:
            PUTATTR(tag,Agetattr(win->frame,AOFRM_Name));
            break;
         case AOWIN_Box:
            PUTATTR(tag,&win->box);
            break;
         case AOWIN_Zoombox:
            PUTATTR(tag,&win->zoombox);
            break;
         case AOWIN_Borderright:
            PUTATTR(tag,win->window?win->window->BorderRight:12);
            break;
         case AOWIN_Borderbottom:
            PUTATTR(tag,win->window?win->window->BorderBottom:12);
            break;
         case AOWIN_Commands:
            PUTATTR(tag,Agetattr(win->whis,AOWHS_Commands));
            break;
         case AOWIN_Specialpens:
            PUTATTR(tag,win->window?&win->capens:NULL);
            break;
         case AOWIN_Status:
            PUTATTR(tag,win->statustext?win->statustext:NULLSTRING);
            break;
         case AOWIN_Hpstatus:
            PUTATTR(tag,win->statushptext?win->statushptext:NULLSTRING);
            break;
         case AOWIN_Refreshing:
            PUTATTR(tag,BOOLVAL(win->flags&WINF_REFRESHING));
            break;
         case AOWIN_Animon:
            PUTATTR(tag,BOOLVAL(win->flags&WINF_ANIMON));
            break;
         case AOBJ_Jscancel:
            PUTATTR(tag,Cancelevent(win));
            break;
         case AOWIN_Jsdebug:
            {  struct MenuItem *mi;
               mi=ItemAddress(win->menu,Menunumfromcmd("@DEBUGJS"));
               PUTATTR(tag,mi?BOOLVAL(mi->Flags&CHECKED):FALSE);
            }
            break;
         case AOWIN_Hiswinhis:
            PUTATTR(tag,win->hiswhis);
            break;
      }
   }
   return 0;
}

static long Addchildwindow(struct Awindow *win,struct Amadd *ama)
{  switch(ama->relation)
   {  case AOREL_WIN_POPUP:
         if(win->popup) Adisposeobject(win->popup);
         win->popup=ama->child;
         break;
   }
   return 0;
}

static long Remchildwindow(struct Awindow *win,struct Amadd *ama)
{  switch(ama->relation)
   {  case AOREL_WIN_POPUP:
         if(win->popup==ama->child) win->popup=NULL;
         break;
   }
   return 0;
}

static long Jsetupwindow(struct Awindow *win,struct Amjsetup *amj)
{  AmethodA(win->frame,amj);
   return 0;
}

static void Deinstallwindow(void)
{  void *p;
   while(p=REMHEAD(&windows)) Adisposeobject(p);
}

static long Dispatch(struct Awindow *win,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newwindow((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setwindow(win,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getwindow(win,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatewindow(win,(struct Amset *)amsg);
         break;
      case AOM_ADDCHILD:
         result=Addchildwindow(win,(struct Amadd *)amsg);
         break;
      case AOM_REMCHILD:
         result=Remchildwindow(win,(struct Amadd *)amsg);
         break;
      case AOM_JSETUP:
         result=Jsetupwindow(win,(struct Amjsetup *)amsg);
         break;
      case AOM_DISPOSE:
         Disposewindow(win);
         break;
      case AOM_DEINSTALL:
         Deinstallwindow();
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

/* Get the next or previous window */
struct Awindow *Nextwindow(struct Awindow *win,long d)
{  struct Awindow *nextw=win;
   if(d>0)
   {  for(;;)
      {  nextw=nextw->next;
         if(!nextw->next) nextw=windows.first;
         if(nextw->window) return nextw;
      }
   }
   else
   {  for(;;)
      {  nextw=nextw->prev;
         if(!nextw->prev) nextw=windows.last;
         if(nextw->window) return nextw;
      }
   }
   return win;
}

/* Set the status of the secure gadget */
void Setsecure(struct Awindow *win)
{  
/* #ifndef DEMOVERSION */
   BOOL issecure;
   issecure=BOOLVAL(Agetattr(win->focus?win->focus:win->frame,AOBJ_Secure));
   if(win->securegad)
   {  Setgadgetattrs(win->securegad,win->window,NULL,
         GA_Image,issecure?win->secureimg:win->unsecureimg,
         TAG_END);
   }
/* #endif DEMOVERSION */
}

/*------------------------------------------------------------------------*/

BOOL Installwindow(void)
{  NEWLIST(&windows);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_WINDOW,Dispatch)) return FALSE;
   return TRUE;
}

void *Firstwindow(void)
{  if(windows.first && windows.first->next)
   {  return windows.first;
   }
   else return NULL;
}

void *Findwindow(ULONG key)
{  struct Awindow *win;
   for(win=windows.first;win && win->next;win=win->next)
   {  if(win->key==key) return win;
   }
   return NULL;
}

void *Findwindownr(long nr)
{  struct Awindow *win;
   for(win=windows.first;win && win->next;win=win->next)
   {  if(win->windownr==nr) return win;
   }
   return NULL;
}

void Busypointer(BOOL busy)
{  struct Awindow *win;
   BOOL set=FALSE;
   static short busynest=0;
   struct IntuitionBase *ibase;
   ULONG version;
   void *ptr;
   if(busy && ++busynest==1) set=TRUE;
   if(!busy && --busynest==0) set=TRUE;
   if(set)
   {  ibase=IntuitionBase;
      version=(ibase)?ibase->lib_Version:0;
      for(win=windows.first;win && win->next;win=win->next)
      {  if(win->window)
         {  if(busy)
            {  SetWindowPointer(win->window,
                  WA_BusyPointer,TRUE,
                  WA_PointerDelay,TRUE,
                  TAG_END);
            }
            else
            {  /* Restore pointer using system types if available */
               if(version>=47)
               {  if(win->ptrtype==APTR_HAND)
                  {  SetWindowPointer(win->window,WA_PointerType,POINTERTYPE_LINK,TAG_END);
                  }
                  else if(win->ptrtype==APTR_DEFAULT)
                  {  SetWindowPointer(win->window,WA_PointerType,POINTERTYPE_NORMAL,TAG_END);
                  }
                  else if(win->ptrtype==APTR_TEXT)
                  {  SetWindowPointer(win->window,WA_PointerType,POINTERTYPE_TEXT,TAG_END);
                  }
                  else if(win->ptrtype==APTR_NOTALLOWED)
                  {  SetWindowPointer(win->window,WA_PointerType,POINTERTYPE_NOTALLOWED,TAG_END);
                  }
                  else if(win->ptrtype==APTR_CONTEXTMENU)
                  {  SetWindowPointer(win->window,WA_PointerType,POINTERTYPE_CONTEXTMENU,TAG_END);
                  }
                  else
                  {  ptr=Apppointer(Aweb(),win->ptrtype);
                     SetWindowPointer(win->window,WA_Pointer,ptr,TAG_END);
                  }
               }
               else
               {  ptr=Apppointer(Aweb(),win->ptrtype);
                  SetWindowPointer(win->window,WA_Pointer,ptr,TAG_END);
               }
            }
         }
      }
   }
}

void Setanimgad(BOOL onoff)
{  struct Awindow *win;
   for(win=windows.first;win && win->next;win=win->next)
   {  if(win->ledgad && win->window)
      {  SETFLAG(win->flags,WINF_ANIMON,onoff);
         Setgadgetattrs(win->ledgad,win->window,NULL,
            LEDGGA_Active,onoff,TAG_END);
      }
   }
}

UBYTE *Windowtitle(struct Awindow *win,UBYTE *name,UBYTE *title)
{  UBYTE *newtitle;
   long len=strlen(title)+1;
   /* Window title now shows only the URL/document title, not window ID */
   newtitle=ALLOCTYPE(UBYTE,len,0);
   if(newtitle)
   {  strcpy(newtitle,title);
   }
   return newtitle;
}

/* Update screen title for active window */
/* COMMENTED OUT: Screen title feature temporarily disabled */
void Updatescreentitle(struct Awindow *win)
{  UBYTE *screentitle;
   struct timeval tv;
   ULONG currenttime;
   BOOL showstatus;
   
   /* COMMENTED OUT: Screen title feature temporarily disabled */
   return;
   
   if(win && win->window)
   {  /* Check if we should show status message (modern layout only, within timeout) */
      showstatus = FALSE;
      if(win->layoutstyle == 1 && win->statustime != 0 && win->statustext && win->statustext[0])
      {  GetSysTime(&tv);
         currenttime = tv.tv_secs;
         /* Show status for 3 seconds */
         /* Handle time wraparound: if currenttime < statustime, assume timeout expired */
         if(currenttime >= win->statustime)
         {  if((currenttime - win->statustime) < 3)
            {  showstatus = TRUE;
            }
            else
            {  /* Status timeout expired, clear it */
               win->statustime = 0;
            }
         }
         else
         {  /* Time wraparound or clock change, clear status */
            win->statustime = 0;
         }
      }
      
      if(showstatus)
      {  /* Show status message in screen title */
         screentitle = Makestatusscreentitle(win, win->statustext);
      }
      else
      {  /* Show normal screen title - always generate fresh */
         screentitle = Makescreentitle(win);
      }
      
      if(screentitle && screentitle[0])
      {  /* Always update if this is a different window, or if title changed */
         if(lastscreentitlewin != win || !win->screentitle || strcmp(win->screentitle, screentitle) != 0)
         {  SetWindowTitles(win->window,(UBYTE *)~0,screentitle);
            /* Store copy for comparison */
            if(win->screentitle) FREE(win->screentitle);
            win->screentitle = Dupstr(screentitle, -1);
            lastscreentitlewin = win;  /* Track that this window's title is now displayed */
         }
      }
   }
}

void *Activewindow(void)
{  if(activewindow) return activewindow;
   else return Firstwindow();
}

void Rebuildallbuttons(void)
{  struct Awindow *win;
   struct DrawInfo *dri=(struct DrawInfo *)Agetattr(Aweb(),AOAPP_Drawinfo);
   for(win=windows.first;win && win->next;win=win->next)
   {  Rebuildbuttonrow(win,dri);
   }
}

void Reopenallwindows(void)
{  struct Awindow *win;
   for(win=windows.first;win && win->next;win=win->next)
   {  Closewindow(win);
      Openwindow(win);
   }
}

void Jsetupallwindows(struct Jcontext *jc)
{  struct Awindow *win;
   for(win=windows.first;win && win->next;win=win->next)
   {  Ajsetup(win,jc,NULL,NULL);
   }
}

void Inputallwindows(BOOL input)
{  struct Awindow *win;
   for(win=windows.first;win && win->next;win=win->next)
   {  if(input!=BOOLVAL(win->flags&WINF_INPUT))
      {  Setwincancel(win);
      }
   }
}

static BOOL Parsefeature(UBYTE *p)
{  if(STRNIEQUAL(p,"=0",2)) return FALSE;
   if(STRNIEQUAL(p,"=no",3)) return FALSE;
   if(STRNIEQUAL(p,"=false",6)) return FALSE;
   return TRUE;
}

void *Jopenwindow(struct Jcontext *jc,struct Jobject *opener,
   UBYTE *urlname,UBYTE *name,UBYTE *spec,struct Awindow *oldwin)
{  struct Awindow *win=NULL;
   struct Jobject *jo=NULL;
   struct Jvar *jv;
   void *url;
   UBYTE *frag,*basename;
   UBYTE *p;
   short width=0,height=0;
   BOOL navs=TRUE,buttons=TRUE;
   if(spec && *spec)
   {  navs=FALSE;
      buttons=FALSE;
      for(p=spec;*p;p++)
      {  if(STRNIEQUAL(p,"width=",6))
         {  width=atoi(p+6);
         }
         if(STRNIEQUAL(p,"height=",7))
         {  height=atoi(p+7);
         }
         if(STRNIEQUAL(p,"toolbar",7))
         {  navs|=Parsefeature(p+7);
         }
         if(STRNIEQUAL(p,"location",8))
         {  navs|=Parsefeature(p+8);
         }
         if(STRNIEQUAL(p,"status",6))
         {  navs|=Parsefeature(p+6);
         }
         if(STRNIEQUAL(p,"directories",11))
         {  buttons|=Parsefeature(p+11);
         }
      }
   }
   {  win=Anewobject(AOTP_WINDOW,
         AOWIN_Name,name,
         AOWIN_Innerwidth,width,
         AOWIN_Innerheight,height,
         AOWIN_Noproxy,Agetattr(oldwin,AOWIN_Noproxy),
         AOWIN_Navigation,navs,
         AOWIN_Buttonbar,buttons,
         TAG_END);
      if(win)
      {  win->flags|=WINF_JSCLOSEABLE;
         Ajsetup(win,jc,NULL,NULL);
         jo=(struct Jobject *)Agetattr(win->frame,AOBJ_Jobject);
         if(jv=Jproperty(jc,jo,"opener"))
         {  Jasgobject(jc,jv,opener);
         }
         if(*urlname) basename=Getjscurrenturlname(jc);
         else basename="";
         url=Findurl(basename,urlname,0);
         frag=Fragmentpart(urlname);
         Inputwindoc(win,url,frag,NULL);
         Ajsetup(win,jc,NULL,NULL);
      }
   }
   return jo;
}

void Jclosewindow(struct Awindow *win)
{  if(win && (win->flags&WINF_JSCLOSEABLE))
   {  /*
      if(STREQUAL(activeport,win->portname)) *activeport='\0';
      REMOVE(win);
      Adisposeobject(win);
      */
      win->cmd|=CMD_CLOSE;
   }
}

