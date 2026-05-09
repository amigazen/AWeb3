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

/* application.h - AWeb application context object */

#ifndef AWEB_APPLICATION_H
#define AWEB_APPLICATION_H

#include "object.h"

/*--- application tags ---*/

#define AOAPP_Dummy        AOBJ_DUMMYTAG(AOTP_APPLICATION)

/* All attributes are GET only! */

#define AOAPP_Programname  (AOAPP_Dummy+1)
   /* (UBYTE *) Fully qualified program name. */

#define AOAPP_Screen       (AOAPP_Dummy+2)
   /* (struct Screen *) Screen AWeb is running on. */

#define AOAPP_Screenname   (AOAPP_Dummy+3)
   /* (UBYTE *) Public screen name. */

#define AOAPP_Colormap     (AOAPP_Dummy+4)
   /* (struct ColorMap *) The screen's ColorMap. */

#define AOAPP_Drawinfo     (AOAPP_Dummy+5)
   /* (struct DrawInfo *) The screen's DrawInfo. */

#define AOAPP_Screenfont   (AOAPP_Dummy+6)
   /* (struct TextFont *) The screen's font. */

#define AOAPP_Systemfont   (AOAPP_Dummy+7)
   /* (struct TextFont *) The system font. */

#define AOAPP_Screenwidth  (AOAPP_Dummy+8)
#define AOAPP_Screenheight (AOAPP_Dummy+9)
#define AOAPP_Screendepth  (AOAPP_Dummy+10)
   /* (long) The screen's dimensions. */

#define AOAPP_Configname   (AOAPP_Dummy+11)
   /* (UBYTE *) The current CONFIG name. */

#define AOAPP_Screenvalid  (AOAPP_Dummy+12)
   /* (BOOL) If TRUE, the screen and related attributes can safely be used.
    * If FALSE, we are in the middle of a screen change, don't use
    * the attributes.
    * If you AOM_ADDCHILD yourself in the AOREL_APP_USE_SCREEN relationship,
    * you will be sent AOM_SET messages with this attribute to notify
    * you in case of screen changes. When receiving FALSE, you must release
    * all screen related resources, and re-obtain them after receiving TRUE. */

#define AOAPP_Menus        (AOAPP_Dummy+13)
   /* (struct NewMenu *) Basic menu structure.
    * Childs in the AOREL_APP_USE_MENUS will be sent AOM_SET messages
    * with this attribute set to NULL or to a new menu structure.
    * (PREFS ONLY) When menu changes, this attribute it SET with a dummy value. */

#define AOAPP_Visualinfo   (AOAPP_Dummy+14)
   /* (void *) Gadtools Visualinfo for the screen. */

#define AOAPP_Browsebgpen  (AOAPP_Dummy+15)
#define AOAPP_Textpen      (AOAPP_Dummy+16)
#define AOAPP_Linkpen      (AOAPP_Dummy+17)
#define AOAPP_Vlinkpen     (AOAPP_Dummy+18)
#define AOAPP_Alinkpen     (AOAPP_Dummy+19)
   /* (long) General default pen colours */

#define AOAPP_Windowport   (AOAPP_Dummy+20)
   /* (struct MsgPort *) The shared IDCMP port */

#define AOAPP_Tofront      (AOAPP_Dummy+21)
   /* (BOOL) When SET, brings the screen to front or to back. */

#define AOAPP_Newprefs     (AOAPP_Dummy+22)
   /* (ULONG) Preferences have changed. */

#define AOAPP_Savepath     (AOAPP_Dummy+23)
   /* (UBYTE *) GET the latest save path, or SET to path+file name, or SET to NULL to reset */

#define AOAPP_Deficon      (AOAPP_Dummy+24)
#define AOAPP_Defmapicon   (AOAPP_Dummy+25)
#define AOAPP_Deferricon   (AOAPP_Dummy+26)
   /* (void *) ClassAct bitmap.image objects for the normal, image map
    * and error unloaded icons. */

#define AOAPP_Browsersettings (AOAPP_Dummy+27)
   /* (BOOL) Browser settings have changed. SET to childs in AOREL_APP_USE_BROWSER
    * relationship. */

#define AOAPP_Browserpens  (AOAPP_Dummy+28)
   /* (BOOL) (PREFS ONLY) Browser default pens have changed. */

#define AOAPP_Whitepen     (AOAPP_Dummy+29)
#define AOAPP_Blackpen     (AOAPP_Dummy+30)
   /* (long) General black&white pens */

#define AOAPP_Semaphore    (AOAPP_Dummy+31)
   /* (struct SignalSemaphore *) Semaphore to single thread heavy subtasks. */

#define AOAPP_Pwfont       (AOAPP_Dummy+32)
   /* (struct TextFont *) Password font with invisible characters */

#define AOAPP_Overlapsetting (AOAPP_Dummy+33)
   /* (BOOL) Overlap setting has changed. SET to childs in
    * AOREL_APP_USE_OVERLAP relationship. */

#define AOAPP_Blink        (AOAPP_Dummy+34)
   /* (BOOL) Blink phase is on or off. SET to childs in AOREL_APP_WANT_BLINK
    * relationship.
    * Prefs SET this if blink rate changes. */

#define AOAPP_Processtype  (AOAPP_Dummy+35)  /* SET */
   /* (short) Object type for which the next AOAPP_Processfun is valid */

#define AOAPP_Processfun   (AOAPP_Dummy+36)  /* SET */
   /* (void (*)(void)) Function to call when a message for AOAPP_Processtype
    * object (in window UserData) arrives */

#define AOAPP_Jcontext     (AOAPP_Dummy+37)  /* GET */
   /* (struct Jcontext *) Get the JS context. Create one if none exists.
    * Returns NULL if JS is disabled. */

#define AOAPP_Jnavigator   (AOAPP_Dummy+38)  /* GET */
   /* (struct Jobject *) JS navigator object */

#define AOAPP_Animgadsetting (AOAPP_Dummy+39) /* SET */
   /* (BOOL) Animgad (continuous) setting has changed. */

#define AOAPP_Appwindowport (AOAPP_Dummy+40) /* GET */
   /* (struct MsgPort *) The Workbench AppWindow message port, or NULL. 
    * When registering an AppWindow, the ID must be set to the window key. */

#define AOAPP_Iconify      (AOAPP_Dummy+41)  /* SET */
   /* (BOOL) (Un)iconify the application. */

#define AOAPP_Iconified    (AOAPP_Dummy+42)  /* GET */
   /* (BOOL) If application is iconified. */

#define AOAPP_Jscreen      (AOAPP_Dummy+43)  /* GET */
   /* (struct Jobject *) JS screen object */

#define AOAPP_Tooltippen   (AOAPP_Dummy+44)  /* GET */
   /* (long) Pen number for that pale yellow tooltip background */

/* MARQUEE broadcast tag disabled (see application.c / body.c)
#define AOAPP_Marquee       (AOAPP_Dummy+45)
*/


#define AOAPP_    (AOAPP_Dummy+)
#define AOAPP_    (AOAPP_Dummy+)


/* Preference change flags */
#define PREFSF_SCREEN      0x00000001  /* New screen mode or size */
#define PREFSF_BUTTONS     0x00000002  /* New user button set */
#define PREFSF_SHOWBUTTONS 0x00000004  /* Show or hide buttonset */

/*--- Application relations ---*/

#define AOREL_APP_DUMMY          AOREL_DUMMYTAG(AOTP_APPLICATION)

#define AOREL_APP_USE_SCREEN     (AOREL_APP_DUMMY+1)
   /* Child object uses screen resources */

#define AOREL_APP_USE_MENUS      (AOREL_APP_DUMMY+2)
   /* Child object uses menus */

#define AOREL_APP_USE_BROWSER    (AOREL_APP_DUMMY+3)
   /* Child object uses browser settings like HTML mode, browser pens,
    * cycle-to-list */

#define AOREL_APP_USE_OVERLAP    (AOREL_APP_DUMMY+4)
   /* Child uses overlap setting */

#define AOREL_APP_WANT_BLINK     (AOREL_APP_DUMMY+5)
   /* Child wants blink events */

/* MARQUEE relation disabled
#define AOREL_APP_WANT_MARQUEE   (AOREL_APP_DUMMY+6)
*/


/*--- Application functions ---*/

extern struct Image *Buttonimage(void *ap,short type,UWORD *data,long width,long height);
   /* Return an image for this type, fallback on this image data. */

extern struct Gadget *Animgadget(void *ap,void *capens);
   /* Return an animation gadget */

extern struct IntuiMessage *Getimessage(struct MsgPort *port,short type);
   /* Get an IntuiMessage from this window port for this object type */

extern void *Apppointer(void *app,USHORT ptrtype);
   /* Get a custom pointer object. See object.h for pointer types. */

extern void Addanimgad(struct Window *w,struct Gadget *g);
   /* Add an animation gadget */

extern void Remanimgad(struct Gadget *g);
   /* Remove an animation gadget */

extern BOOL Setanimgads(BOOL onoff);
   /* Set animation gadgets on or off. Returns previous on/off value.
    * If non-continuous, animations proceed one step if set to on. */

extern USHORT Menunumfromcmd(UBYTE *cmd);
   /* Find the menu number for this command */

extern struct Menuentry *Menuentryfromkey(UBYTE key);
   /* Find the menuentry for this shortcut */

extern struct Menuentry *Menuentryfromnum(USHORT menunum);
   /* Find the menuentry for this menu num */

#endif

