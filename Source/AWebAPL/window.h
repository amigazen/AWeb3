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

/* window.h - AWeb window object */

#ifndef AWEB_WINDOW_H
#define AWEB_WINDOW_H

#include "object.h"

/*--- window tags ---*/

#define AOWIN_Dummy        AOBJ_DUMMYTAG(AOTP_WINDOW)

#define AOWIN_Activeurl    (AOWIN_Dummy+1)
   /* (void *) The active url object with respect to status updates. */

#define AOWIN_Status       (AOWIN_Dummy+2)
   /* (UBYTE *) Status text or NULL. */

#define AOWIN_Read         (AOWIN_Dummy+3)
#define AOWIN_Total        (AOWIN_Dummy+4)
   /* (long) Progress bar status, only used if AOWIN_Status is also given. */

#define AOWIN_Goinactive   (AOWIN_Dummy+6)   /* SET */
   /* (void *) Forget about this object if it is active.
    * If no object is supplied, inactivate any active object. */

#define AOWIN_Window       (AOWIN_Dummy+7)
   /* (struct Window *) GET Intuition window for this object */

#define AOWIN_Rastport     (AOWIN_Dummy+8)
   /* (struct RastPort *) GET RastPort for this object */

#define AOWIN_Innerleft    (AOWIN_Dummy+9)   /* GET */
#define AOWIN_Innertop     (AOWIN_Dummy+10)  /* GET */
#define AOWIN_Innerwidth   (AOWIN_Dummy+11)  /* GET,NEW */
#define AOWIN_Innerheight  (AOWIN_Dummy+12)  /* GET,NEW */
   /* (long) Inner browser window dimensions */

#define AOWIN_Resized      (AOWIN_Dummy+13)
   /* (BOOL) Window is being resized. */

#define AOWIN_Busy         (AOWIN_Dummy+14)
   /* (BOOL) Window is busy */

#define AOWIN_Title        (AOWIN_Dummy+15)
   /* (UBYTE *) SET new window title */

#define AOWIN_Vslidertotal (AOWIN_Dummy+16)
#define AOWIN_Vslidervisible (AOWIN_Dummy+17)
#define AOWIN_Vslidertop   (AOWIN_Dummy+18)
#define AOWIN_Hslidertotal (AOWIN_Dummy+19)
#define AOWIN_Hslidervisible (AOWIN_Dummy+20)
#define AOWIN_Hslidertop   (AOWIN_Dummy+21)
   /* (long) Set the scrollers */

#define AOWIN_Currenturl   (AOWIN_Dummy+22)
   /* (UBYTE *) SET the value of the URL gadget */

#define AOWIN_Windownr     (AOWIN_Dummy+23)
   /* (long) Window number as the user knows it */

#define AOWIN_Key          (AOWIN_Dummy+24)
   /* (ULONG) Unique id of window object. */

#define AOWIN_Noproxy      (AOWIN_Dummy+25)  /* NEW,GET */
   /* (BOOL) Get or initialize the no proxy menu item status. */

#define AOWIN_Name         (AOWIN_Dummy+26)  /* NEW,GET */
   /* (UBYTE *) Name of the top frame in this window. */

#define AOWIN_Portname     (AOWIN_Dummy+27)  /* GET */
   /* (UBYTE *) Window ARexx port name */

#define AOWIN_Bgsound      (AOWIN_Dummy+28)  /* SET */
   /* (BOOL) Enable or disable the "Play background sound" menu item */

#define AOWIN_Box          (AOWIN_Dummy+29)  /* GET */
#define AOWIN_Zoombox      (AOWIN_Dummy+30)  /* GET */
   /* (struct IBox *) Get the window's dimensions */

#define AOWIN_Activate     (AOWIN_Dummy+31)  /* SET */
   /* Activate the window, but don't force it to front. */

#define AOWIN_Borderright  (AOWIN_Dummy+32)  /* GET */
#define AOWIN_Borderbottom (AOWIN_Dummy+33)  /* GET */
   /* (long) Size of window border, for adjusting scroller size */

#define AOWIN_Commands     (AOWIN_Dummy+34)  /* GET */
   /* (BOOL) Shell/ARexx commands allowed. */

#define AOWIN_Nofocus      (AOWIN_Dummy+35)  /* SET */
   /* (void *) FRAME object that no longer wants scroll focus. */

#define AOWIN_Focus        (AOWIN_Dummy+36)  /* SET */
   /* (void *) FRAME object that wants scroll focus. */

#define AOWIN_Keepselection (AOWIN_Dummy+37) /* SET */
   /* (BOOL) Do not clear drag selection on this mouse click. */

#define AOWIN_Clearselection (AOWIN_Dummy+38) /* SET */
   /* (BOOL) Clear drag selection now. */

#define AOWIN_Refresh      (AOWIN_Dummy+39)  /* SET */
   /* (BOOL) Start refresh sequence */

#define AOWIN_Specialpens  (AOWIN_Dummy+40)  /* GET */
   /* (void *) The ClassAct special pens for this window. */

#define AOWIN_Rmbtrap      (AOWIN_Dummy+41)  /* SET */
   /* (BOOL) Set or clear RMB trap */

#define AOWIN_Activeobject (AOWIN_Dummy+42)  /* SET */
   /* (void *) The object that wants to become active, or NULL if the
    * current active object should be inactivated.
    * Any active object will be sent an AM_GOINACTIVE, and the new
    * object an AM_GOACTIVE with no IntuiMessage. */

#define AOWIN_Hpstatus     (AOWIN_Dummy+43)  /* SET,GET */
   /* (UBYTE *) High-priority status text or NULL. */

#define AOWIN_Refreshing   (AOWIN_Dummy+44)  /* GET */
   /* (BOOL) If BeginRefresh() was done on Intuition window */

#define AOWIN_Animon       (AOWIN_Dummy+45)  /* GET */
   /* (BOOL) If the transfer animation is running */

#define AOWIN_Jsdebug      (AOWIN_Dummy+46)  /* GET */
   /* (BOOL) If JS debug menu item is selected */

#define AOWIN_Hiswinhis    (AOWIN_Dummy+47)  /* SET,GET */
   /* (void *) Get the current winhis object for history.
    * When SET, go to this winhis as history move. */

#define AOWIN_Appmessage   (AOWIN_Dummy+48)  /* SET */
   /* (struct AppMessage *) An AppMessage for this window. */

#define AOWIN_Navigation   (AOWIN_Dummy+49)  /* NEW */
   /* (BOOL) If window should want navigation gadgets */

#define AOWIN_Buttonbar    (AOWIN_Dummy+50)  /* NEW */
   /* (BOOL) If window should want userbutton bar */

#define AOWIN_LayoutStyle   (AOWIN_Dummy+51)  /* NEW */
   /* (short) Layout style: 0=classic, 1=modern */

#define AOWIN_Left         (AOWIN_Dummy+52)  /* NEW */
#define AOWIN_Top          (AOWIN_Dummy+53)  /* NEW */
   /* (long) Initial outer window position. */

/*--- window relationships ---*/

#define AOREL_WIN_POPUP       1
   /* Popup object belonging to this window. */

/*--- window functions ---*/

extern void *Firstwindow(void);
   /* Return the first window object. */

extern void *Findwindow(ULONG key);
   /* Find the window object with this key. */

extern void *Findwindownr(long nr);
   /* Find the window object with this windownr. */

extern void Setanimgad(BOOL onoff);
   /* Set animation gadget on and advance, or off. For all windows. */

extern UBYTE *Windowtitle(void *window,UBYTE *name,UBYTE *title);
   /* Construct a dynamic string containg a window title, using this window name. */

extern void *Activewindow(void);
   /* Find the active window object */

#endif

