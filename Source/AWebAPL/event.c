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

/* event.c aweb event handler and window display module */

#include "aweb.h"
#include "frame.h"
#include "winhis.h"
#include "url.h"
#include "window.h"
#include "winprivate.h"
#include "application.h"
#include "filereq.h"
#include "source.h"
#include "copy.h"
#include "cache.h"
#include "fetch.h"
#include "copydriver.h"
#include "css.h"
#include "docprivate.h"
#include "linkprivate.h"
#include "frprivate.h"
#include <intuition/intuition.h>
#include <intuition/imageclass.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <libraries/gadtools.h>
#include <dos/dosextens.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <gadgets/layout.h>
#include <gadgets/chooser.h>
#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/keymap.h>
#include <proto/layout.h>
#include <proto/chooser.h>
#include <proto/gadtools.h>

/* Setwindowdisplay() codes */
#define WINDISP_NEWDOC  1
#define WINDISP_UPDATE  2
#define WINDISP_REUSE   3
#define WINDISP_REDISP  4
#define WINDISP_RDFROM  5

UBYTE activeport[32];

static struct TagItem openlocalmap[]=
{  AOFRQ_Filename,AOWIN_Openlocal,
   AOFRQ_Pattern,AOWIN_Openlocalpattern,
   TAG_END
};
static struct TagItem startarexxmap[]=
{  AOFRQ_Filename,AOWIN_Startarexx,
   TAG_END
};
static struct TagItem otherhotlistmap[]=
{  AOFRQ_Filename,AOWIN_Otherhotlist,
   TAG_END
};
static struct TagItem loadsettingsmap[]=
{  AOFRQ_Filename,AOWIN_Loadsettings,
   TAG_END
};
static struct TagItem savesettingsasmap[]=
{  AOFRQ_Filename,AOWIN_Savesettingsas,
   TAG_END
};

static UBYTE lastopenpath[STRINGBUFSIZE];
static UBYTE lastopenpattern[STRINGBUFSIZE]="#?.(html|htm|shtml|phtml|asp)";
static UBYTE lastarexxpath[STRINGBUFSIZE];
static UBYTE lasthotlistpath[STRINGBUFSIZE];

/* IDCMP messages that, by default, deactivate objects */
#define INACTIVATEIDCMP \
   (IDCMP_SIZEVERIFY | IDCMP_MOUSEBUTTONS | IDCMP_GADGETDOWN | \
   IDCMP_GADGETUP | IDCMP_MENUPICK | IDCMP_CLOSEWINDOW | IDCMP_MENUVERIFY | \
   IDCMP_INACTIVEWINDOW | IDCMP_MENUHELP | IDCMP_IDCMPUPDATE | IDCMP_CHANGEWINDOW | \
   IDCMP_GADGETHELP)

struct Openreq
{  ULONG key;
   UBYTE *string;
};

/*-----------------------------------------------------------------------*/

/* also called in frame.c */
BOOL Windowresized(struct Awindow *win)
{  struct Window *w=win->window;
   if(w && !(win->flags&WINF_RESIZED))
   {  long iw=Agetattr(win->frame,AOFRM_Innerwidth);
      long ih=Agetattr(win->frame,AOFRM_Innerheight);
      long nw=Getvalue(win->spacegad,GA_Width);
      long nh=Getvalue(win->spacegad,GA_Height);
      if(iw!=nw || ih!=nh)
      {  win->flags|=WINF_RESIZED;
      }
   }
   return BOOLVAL(win->flags&WINF_RESIZED);
}

void Refreshwindow(struct Awindow *win)
{  /* Race condition fix: Defensive checks before accessing window/frame.
    * IDCMP_REFRESHWINDOW may arrive immediately after window opens before
    * all structures are fully initialized. */
   if(!win || !win->window || !win->frame) return;
   LockLayerInfo(win->window->WLayer->LayerInfo);
   LockLayer(0,win->window->WLayer);
   BeginRefresh(win->window);
   win->flags|=WINF_REFRESHING;
   Arender(win->frame,NULL,0,0,AMRMAX,AMRMAX,AMRF_CLEAR,NULL);
   win->flags&=~WINF_REFRESHING;
   EndRefresh(win->window,TRUE);
   UnlockLayer(win->window->WLayer);
   UnlockLayerInfo(win->window->WLayer->LayerInfo);
}

static void Resizewindow(struct Awindow *win)
{  win->flags&=~WINF_RESIZED;
   Busypointer(TRUE);
   Asetattrs(win->frame,
      AOBJ_Width,win->window->Width,
      AOBJ_Height,win->window->Height,
      TAG_END);
   Busypointer(FALSE);
}

/* Set statusgad to URL */
static void Setstatusgad(struct Awindow *win,UBYTE *url)
{  UBYTE *p,*q,*q1;
   long l1,l2;
   if(url && *url)
   {  for(p=url,q=NULL,q1=NULL;*p && *p!='?' && *p!='#';p++)
      {  if(*p=='/') { q1=q;q=p; }
      }
      if(q && (!q[1] || q[1]=='?' || q[1]=='#')) q=q1;
      if(q)
      {  l1=q-url;
         l2=p-q;
      }
      else
      {  l1=0;
         l2=p-url;
      }
      if(win->statushptext) FREE(win->statushptext);
      win->statushptext=Dupstr(url,-1);
      if(win->statusgad)
      {  Setgadgetattrs(win->statusgad,win->window,NULL,
            STATGA_HPText,url,
            STATGA_HPLen1,l1,
            STATGA_HPLen2,l2,
            TAG_END);
      }
   }
   else
   {  if(win->statushptext) FREE(win->statushptext);
      win->statushptext=NULL;
      if(win->statusgad)
      {  Setgadgetattrs(win->statusgad,win->window,NULL,
            STATGA_HPText,NULL,
            TAG_END);
      }
   }
}

/* Clear the hittext and statusgad */
static void Clearhittext(struct Awindow *win)
{  if(win->hittext)
   {  Setstatusgad(win,NULL);
      FREE(win->hittext);
      win->hittext=NULL;
   }
}

/* Check if mouse pointer is over hittable area */
static void Checklink(struct Awindow *win,long x,long y,USHORT hitflags)
{  struct Amhresult amhr={0};
   long result;
   UBYTE *text;
   void *frame;
   struct Document *doc;
   void *hoveredElement;
   void *lp;
   struct Aobject *ao;
   short objtype;
   struct Link *lnk;
   struct Component *comp;
   short hoverType;
   
   if(win->frame)
   {  result=Ahittest(win->frame,NULL,x,y,hitflags,win->hitobject,&amhr);
      win->nextfocus=amhr.focus;
      if(amhr.object != win->hitobject)
      {  short hitType;
         hitType = -1;
         if(amhr.object)
         {  ao = (struct Aobject *)amhr.object;
            hitType = ao->objecttype;
         }
      }
      
      /* Track hover state for CSS :hover pseudo-class */
      frame = win->frame;
      if(frame)
      {  struct Frame *fr = (struct Frame *)frame;
         if(fr->copy)
         {  ao = (struct Aobject *)fr->copy;
            if(ao->objecttype == AOTP_COPYDRIVER)
            {  doc = (struct Document *)fr->copy;
               hoveredElement = NULL;
               
               /* Map hit object to a layout node for CSS :hover on ancestors (menus).
                * Hittestlink sets object=AOTP_LINK; use the link's flow body like Goactivelink.
                * Images/fields expose AOBJ_Layoutparent to the containing AOTP_BODY. */
               if(amhr.object)
               {  ao = (struct Aobject *)amhr.object;
                  objtype = ao->objecttype;
                  if(objtype == AOTP_BODY)
                  {  hoveredElement = amhr.object;
                  }
                  else if(objtype == AOTP_ELEMENT)
                  {  hoveredElement = amhr.object;
                  }
                  else if(objtype == AOTP_LINK)
                  {  lnk = (struct Link *)amhr.object;
                     comp = lnk->components.first;
                     if(comp && comp->object)
                     {  hoveredElement = (void *)Agetattr((void *)comp->object, AOBJ_Layoutparent);
                     }
                  }
                  else
                  {  lp = (void *)Agetattr(amhr.object, AOBJ_Layoutparent);
                     if(lp)
                     {  ao = (struct Aobject *)lp;
                        if(ao->objecttype == AOTP_BODY)
                        {  hoveredElement = lp;
                        }
                     }
                  }
               }
               
               /* Update hover state and re-apply CSS if changed */
               if(doc->hoveredElement != hoveredElement)
               {  doc->hoveredElement = hoveredElement;
                  hoverType=-1;
                  if(hoveredElement)
                  {  ao=(struct Aobject *)hoveredElement;
                     hoverType=ao->objecttype;
                  }
                  /* Re-apply CSS to all elements to update :hover styles */
                  if(doc->cssstylesheet)
                  {  ReapplyCSSToAllElements(doc);
                  }
               }
            }
         }
      }
      
      switch(result&AMHR_RESULTMASK)
      {  case AMHR_NEWHIT:
            if(win->hittext) FREE(win->hittext);
            if(amhr.jonmouse!=win->jonmouse)
            {  if(win->jonmouse) Ajonmouse(win->jonmouse,AMJE_ONMOUSEOUT);
            }
            win->hittext=amhr.text;
            Setstatusgad(win,win->hittext);
            if(amhr.jonmouse!=win->jonmouse)
            {  if(amhr.jonmouse) Ajonmouse(amhr.jonmouse,AMJE_ONMOUSEOVER);
            }
            win->hitobject=amhr.object;
            win->jonmouse=amhr.jonmouse;
            SETFLAG(win->window->Flags,WFLG_RMBTRAP,
               (prefs.gui.popupkey&IEQUALIFIER_RBUTTON) && (result&AMHR_POPUP));
            /* Set context menu pointer if popup is available and popup key is active */
            if((result&AMHR_POPUP) && (hitflags&AMHF_POPUP) && !amhr.ptrtype)
            {  Setawinpointer(win,APTR_CONTEXTMENU);
            }
            else
            {  Setawinpointer(win,amhr.ptrtype);
            }
            Tooltip(amhr.tooltip,x+win->window->LeftEdge,y+win->window->TopEdge);
            break;
         case AMHR_OLDHIT:
            SETFLAG(win->window->Flags,WFLG_RMBTRAP,
               (prefs.gui.popupkey&IEQUALIFIER_RBUTTON) && (result&AMHR_POPUP));
            Tooltipmove(x+win->window->LeftEdge,y+win->window->TopEdge);
            break;
         default:
            if((result&AMHR_RESULTMASK)==AMHR_STATUS)
            {  text=(UBYTE *)Agetattr(amhr.object,AOBJ_Statustext);
               if(!(text && win->hittext && STREQUAL(text,win->hittext)))
               {  if(win->hittext) FREE(win->hittext);
                  if(text) win->hittext=Dupstr(text,-1);
                  else win->hittext=NULL;
                  Setstatusgad(win,win->hittext);
               }
            }
            else Clearhittext(win);
            if(win->jonmouse) Ajonmouse(win->jonmouse,AMJE_ONMOUSEOUT);
            win->hitobject=NULL;
            win->jonmouse=NULL;
            SETFLAG(win->window->Flags,WFLG_RMBTRAP,
               (prefs.gui.popupkey&IEQUALIFIER_RBUTTON) && (result&AMHR_POPUP));
            Setawinpointer(win,APTR_DEFAULT);
            Tooltip(amhr.tooltip,x+win->window->LeftEdge,y+win->window->TopEdge);
            break;
      }
      if(amhr.tooltip) FREE(amhr.tooltip);
   }
}

static void Activateobject(struct Awindow *win,void *object,
   struct IntuiMessage *msg,USHORT hitflags)
{  long result;
   result=Agoactive(object,msg,hitflags);
   switch(result)
   {  case AMR_ACTIVE:
         win->activeobject=object;
         break;
      case AMR_CHANGED:
         Agoinactive(object);
         Clearhittext(win);
         break;
   }
}


/* Left mouse button pressed. Check if mouse is over hittable area, and if so,
 * activate the object */
static void Activatelink(struct Awindow *win,struct IntuiMessage *msg,USHORT hitflags)
{  Checklink(win,msg->MouseX,msg->MouseY,hitflags);
   if(win->hitobject)
   {  Activateobject(win,win->hitobject,msg,hitflags);
      win->hitobject=NULL;
   }
}

/* Pass the message to the active object. Return TRUE if message should be re-used. */
static BOOL Handlelink(struct Awindow *win,struct IntuiMessage *msg,USHORT hitflags)
{  BOOL reuse=FALSE;
   struct Amiresult amir={0};
   long result;
   void *active;
   if(active=win->activeobject)
   {  result=Ahandleinput(win->activeobject,msg,hitflags,&amir);
      if(result==AMR_NOCARE)
      {  if(msg->Class&INACTIVATEIDCMP)
         {  result=AMR_REUSE;
         }
         else
         {  result=AMR_ACTIVE;
            reuse=TRUE;
         }
      }
      switch(result)
      {  case AMR_ACTIVE:
            if(amir.text)
            {  if(win->hittext) FREE(win->hittext);
               win->hittext=amir.text;
               Setstatusgad(win,win->hittext);
            }
            break;
         case AMR_REUSE:
            reuse=TRUE;
            /* fall through: */
         case AMR_NOREUSE:
            /* Note that with JavaScript, this might have activated another object. */
            if(win->activeobject==active)
            {  if(win->cmd&CMD_DEFER)
               {  Ahandleinput(active,NULL,AMHF_DEFER,NULL);
                  win->cmd&=~CMD_DEFER;
               }
               /* First clear out activeobject or JS onBlur() may cause
                * terrible loops */
               win->activeobject=NULL;
               Agoinactive(active);
               Clearhittext(win);
               if(amir.newobject)
               {  Activateobject(win,amir.newobject,msg,hitflags);
               }
            }
            break;
         case AMR_DEFER:
            win->cmd|=CMD_DEFER;
            break;
      }
      if(!reuse && result!=AMR_ACTIVE) win->flags&=~WINF_DRAGSTART;
   }
   else reuse=TRUE;
   return reuse;
}

/* Programmatically set a new active object */
void Setactiveobject(struct Awindow *win,void *ao)
{  if(win->activeobject)
   {  if(win->cmd&CMD_DEFER)
      {  Ahandleinput(win->activeobject,NULL,AMHF_DEFER,NULL);
         win->cmd&=~CMD_DEFER;
      }
      Agoinactive(win->activeobject);
      win->activeobject=NULL;
      Clearhittext(win);
   }
   if(ao)
   {  Activateobject(win,ao,NULL,0);
   }
}

/* Clear dragged selection */
void Dragclear(struct Awindow *win)
{  if(win->dragstartobject)
   {  Adragrender(win->frame,NULL,NULL,0,NULL,0,AMDS_BEFORE);
      win->dragstartobject=NULL;
      win->dragstartobjpos=0;
      win->dragendobject=NULL;
      win->dragendobjpos=0;
   }
}

/* Start or update drag selection. Return TRUE if message must be processed. */
static BOOL Dragselect(struct Awindow *win,struct IntuiMessage *msg)
{  struct Amdresult amdr={0};
   long result;
   BOOL process=TRUE;
   if(win->activeobject && Agetattr(win->activeobject,AOBJ_Clipdrag))
   {  Agoinactive(win->activeobject);
      win->activeobject=NULL;
      Clearhittext(win);
   }
   if(!win->activeobject)
   {  result=Adragtest(win->frame,NULL,msg->MouseX,msg->MouseY,&amdr);
      switch(result)
      {  case AMDR_HIT:
            process=FALSE;
            win->dragendobject=amdr.object;
            win->dragendobjpos=amdr.objpos;
            if(!win->dragstartobject)
            {  win->dragstartobject=win->dragendobject;
               win->dragstartobjpos=win->dragendobjpos;
            }
            Adragrender(win->frame,NULL,win->dragstartobject,win->dragstartobjpos,
               win->dragendobject,win->dragendobjpos,AMDS_BEFORE);
            break;
      }
   }
   return process;
}

/* Start a new input for a frame in this window */
static void Inputwindocref(void *win,void *url,UBYTE *fragment,UBYTE *frameid,
   BOOL noreferer,BOOL reload)
{  struct Awindow *awin=win;
   struct Winhis *whis;
   extern BOOL httpdebug;
   UBYTE *urlstr;
   if(awin && url)
   {
      if(httpdebug)
      {  urlstr=(UBYTE *)Agetattr(url,AOURL_Url);
         printf("[NAV] Inputwindocref: Creating history/docref, URL=%s fragment=%s frameid=%s reload=%ld noreferer=%ld\n",
            urlstr? (char *)urlstr : "NULL",
            fragment? (char *)fragment : "NULL",
            frameid? (char *)frameid : "NULL",
            (long)reload,(long)noreferer);
      }
      if(whis=Anewobject(AOTP_WINHIS,
         AOWHS_Copyfrom,awin->whis,
         AOWHS_Key,awin->key,
         AOWHS_Windownr,awin->windownr,
         AOWHS_Frameid,frameid,
         AOWHS_Url,url,
         AOWHS_Fragment,fragment,
         AOWHS_Clearbelow,reload,
         TAG_END))
      {  
/*
         Removebatch(win);
*/
         if(httpdebug)
         {  printf("[NAV] Inputwindocref: WINHIS created=%p, applying to frame=%p\n",
               whis, awin->frame);
         }
         Asetattrs(awin->frame,
            AOBJ_Winhis,whis,
            AOFRM_Noreferer,noreferer,
            TAG_END);
         if(httpdebug)
         {  printf("[NAV] Inputwindocref: Frame updated with new WINHIS\n");
         }
      }
      else if(httpdebug)
      {  printf("[NAV] Inputwindocref: FAILED to create WINHIS\n");
      }
   }
}

void Followurlname(struct Awindow *win,UBYTE *name,UBYTE *id)
{  void *url;
   UBYTE *fixedname;
   UBYTE *frag;
   extern BOOL httpdebug;
   if(httpdebug)
   {  printf("[NAV] Followurlname: name=%s id=%s\n",
         name? (char *)name : "NULL",
         id? (char *)id : "NULL");
   }
   if(fixedname=Fixurlname(name))
   {  frag=Fragmentpart(fixedname);
      if(httpdebug)
      {  printf("[NAV] Followurlname: fixed=%s frag=%s\n",
            fixedname? (char *)fixedname : "NULL",
            frag? (char *)frag : "NULL");
      }
      url=Findurl(NULL,fixedname,0);
      Inputwindocref(win,url,frag,id,TRUE,FALSE);
      FREE(fixedname);
   }
}

/* Use the url string as search argument if autosearch enabled */
static void Followurlsmart(struct Awindow *win,UBYTE *urlname,UBYTE *id)
{  
#ifdef LOCALONLY
   /* In LOCALONLY builds, all URLs are treated as file paths */
   /* Fixurlname will add file:/// prefix if needed */
   Followurlname(win,urlname,id);
#else
#ifndef DEMOVERSION
   if(prefs.network.autosearch && *prefs.network.searchurl && *urlname
   && !strchr(urlname,':') && !strchr(urlname,'.') && !strchr(urlname,'/'))
   {  struct Buffer argbuf={0};
      UBYTE *urlbuf;
      long len;
      void *url,*referer;
      ULONG loadflags=0;
      Urlencode(&argbuf,urlname,strlen(urlname));
      len=Pformatlength(prefs.network.searchurl,"s",&argbuf.buffer)+4;
      if(urlbuf=ALLOCTYPE(UBYTE,len,0))
      {  Pformat(urlbuf,prefs.network.searchurl,"s",&argbuf.buffer,FALSE);
         /* Start the load already with referer to allow arexx scripts */
         url=Findurl(NULL,urlbuf,0);
         referer=Findurl(NULL,"x-aweb:",0);
         if(win->flags&WINF_NOPROXY) loadflags|=AUMLF_NOPROXY;
         Auload(url,loadflags,referer,NULL,win->frame);
         Followurlname(win,urlbuf,id);
         FREE(urlbuf);
      }
      Freebuffer(&argbuf);
   }
   else
#endif
   {  Followurlname(win,urlname,id);
   }
#endif
}

/* Use the gadget string as new url */
static void Followurlgadget(struct Awindow *win)
{  Followurlsmart(win,win->urlbuf,0);
}

void Followhis(struct Awindow *win,void *whis)
{  
/*
   Removebatch(win);
*/
   if(whis)
   {  win->hiswhis=whis;
      Asetattrs(win,AOWIN_Status,NULL,TAG_END);
      Asetattrs(whis,AOWHS_History,TRUE,TAG_END);
      Asetattrs(win->frame,AOBJ_Winhis,whis,TAG_END);
   }
}

/* Process a userbutton */
static void Douserbutton(struct Awindow *win,short nr)
{  struct Userbutton *ub;
   void *url;
   UBYTE *id,*title;
   for(ub=prefs.gui.buttons.first;ub->next && nr;ub=ub->next,nr--);
   if(ub->next)
   {  url=(void *)Agetattr(win->whis,AOWHS_Url);
      if(id=Rexxframeid(win->focus?win->focus:win->frame))
      {  title=(UBYTE *)Agetattr(win->frame,AOFRM_Title);
         Execarexxcmd(win->key,ub->cmd,"unict",
            (UBYTE *)Agetattr(url,AOURL_Url),
            (UBYTE *)Agetattr(Aweb(),AOAPP_Screenname),
            id,
            (UBYTE *)Agetattr(Aweb(),AOAPP_Configname),
            title?title:NULLSTRING);
         FREE(id);
      }
   }
}

/* Process a key */
static void Douserkey(struct Awindow *win,USHORT key)
{  struct Userkey *uk;
   void *url;
   UBYTE *id,*title;
   uk=Finduserkey(&prefs.gui.keys,key);
   if(uk)
   {  url=(void *)Agetattr(win->whis,AOWHS_Url);
      if(id=Rexxframeid(win->focus?win->focus:win->frame))
      {  title=(UBYTE *)Agetattr(win->frame,AOFRM_Title);
         Execarexxcmd(win->key,uk->cmd,"unict",
            (UBYTE *)Agetattr(url,AOURL_Url),
            (UBYTE *)Agetattr(Aweb(),AOAPP_Screenname),
            id,
            (UBYTE *)Agetattr(Aweb(),AOAPP_Configname),
            title?title:NULLSTRING);
         FREE(id);
      }
   }
}

/* Process a navigation button */
static void Donavbutton(struct Awindow *win,short nr)
{  void *url;
   UBYTE *id,*title;
   UBYTE *cmd;
   ULONG windowkey;
   BOOL transferring;
   /* For reload button (nr==8) in modern layout, check if we should cancel instead.
    * This function is ONLY called when the user explicitly clicks a navigation button,
    * so cancellation only happens on user interaction, never automatically. */
   if(nr==8 && win->layoutstyle==1)
   {  /* Check for active network transfers - this is the correct test */
      windowkey=Agetattr(win,AOWIN_Key);
      transferring=Windowtransferring(windowkey);
      if(transferring)
      {  /* Reload button acts as cancel when network transfer is in progress - user clicked button */
         cmd="CANCEL";
      }
      else if(prefs.gui.navs[nr].cmd)
      {  cmd=prefs.gui.navs[nr].cmd;
      }
      else
      {  return;
      }
   }
   else if(prefs.gui.navs[nr].cmd)
   {  cmd=prefs.gui.navs[nr].cmd;
   }
   else
   {  return;
   }
   if(cmd)
   {  url=(void *)Agetattr(win->whis,AOWHS_Url);
      if(id=Rexxframeid(win->focus?win->focus:win->frame))
      {  title=(UBYTE *)Agetattr(win->frame,AOFRM_Title);
         Execarexxcmd(win->key,cmd,"unict",
            (UBYTE *)Agetattr(url,AOURL_Url),
            (UBYTE *)Agetattr(Aweb(),AOAPP_Screenname),
            id,
            (UBYTE *)Agetattr(Aweb(),AOAPP_Configname),
            title?title:NULLSTRING);
         FREE(id);
      }
   }
}

/* Scroll this frame according to CMD flags */
static void Scrollframe(struct Awindow *win,void *focus,long top,long left)
{  if(win->cmd&CMD_LINEUP) top-=8;
   if(win->cmd&CMD_LINEDOWN) top+=8;
   if(win->cmd&CMD_LEFT) left-=8;
   if(win->cmd&CMD_RIGHT) left+=8;
   if(win->cmd&CMD_PAGEUP)
   {  long h=Agetattr(focus,AOFRM_Innerheight);
      if(h>prefs.program.overlap) top-=h-prefs.program.overlap;
      else top-=h;
   }
   if(win->cmd&CMD_PAGEDOWN)
   {  long h=Agetattr(focus,AOFRM_Innerheight);
      if(h>prefs.program.overlap) top+=h-prefs.program.overlap;
      else top+=h;
   }
   if(win->cmd&CMD_PAGELEFT)
   {  long w=Agetattr(focus,AOFRM_Innerwidth);
      if(w>prefs.program.overlap) left-=w-prefs.program.overlap;
      else left-=w;
   }
   if(win->cmd&CMD_PAGERIGHT)
   {  long w=Agetattr(focus,AOFRM_Innerwidth);
      if(w>prefs.program.overlap) left+=w-prefs.program.overlap;
      else left+=w;
   }
   if(win->cmd&CMD_HOME) top=0;
   if(win->cmd&CMD_END) top=0x3fffffff;
   Asetattrs(focus,
      AOFRM_Leftpos,left,
      AOFRM_Toppos,top,
      AOFRM_Setscroller,win->cmd&CMD_SETSCROLL,
      TAG_END);
}

/*-----------------------------------------------------------------------*/

/* Save the path */
static void Savelastpath(UBYTE *save,UBYTE *path)
{  UBYTE *end=FilePart(path);
   memset(save,0,STRINGBUFSIZE);
   strncpy(save,path,MIN(end-path,STRINGBUFSIZE-1));
}

long Updatewindow(struct Awindow *win,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *openlocal=NULL;
   UBYTE *openlocalpattern=NULL;
   UBYTE *startarexx=NULL;
   UBYTE *otherhotlist=NULL;
   UBYTE *loadsettings=NULL;
   UBYTE *savesettingsas=NULL;
   UBYTE *buf;
   long len;
   BPTR lock = (BPTR)0;
   BOOL startok = FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOWIN_Openlocal:
            openlocal=(UBYTE *)tag->ti_Data;
            break;
         case AOWIN_Openlocalpattern:
            openlocalpattern=(UBYTE *)tag->ti_Data;
            break;
         case AOWIN_Startarexx:
            startarexx=(UBYTE *)tag->ti_Data;
            break;
         case AOWIN_Otherhotlist:
            otherhotlist=(UBYTE *)tag->ti_Data;
            break;
         case AOWIN_Loadsettings:
            loadsettings=(UBYTE *)tag->ti_Data;
            break;
         case AOWIN_Savesettingsas:
            savesettingsas=(UBYTE *)tag->ti_Data;
            break;
      }
   }
   if(openlocal)
   {  Savelastpath(lastopenpath,openlocal);
      if(openlocalpattern)
      {  strncpy(lastopenpattern,openlocalpattern,STRINGBUFSIZE-1);
      }
      len=strlen(openlocal)+17;
      if(buf=ALLOCTYPE(UBYTE,len+4,0))
      {  strcpy(buf,"file://localhost/");
         strcat(buf,openlocal);
         Followurlname(win,buf,0);
         FREE(buf);
      }
   }
   if(startarexx)
   {  Savelastpath(lastarexxpath,startarexx);
      startok=FALSE;
      lock=Lock(startarexx,SHARED_LOCK);
      if(lock)
      {  UnLock(lock);
         startok=TRUE;
      }
      if(startok)
      {  Sendarexxcmd(win->key,startarexx);
      }
   }
   if(otherhotlist)
   {  Savelastpath(lasthotlistpath,otherhotlist);
      len=strlen(otherhotlist)+17;
      if(buf=ALLOCTYPE(UBYTE,len+4,0))
      {  strcpy(buf,"x-aweb:ibhotlist/");
         strcat(buf,otherhotlist);
         Followurlname(win,buf,0);
         FREE(buf);
      }
   }
   if(loadsettings)
   {  Loadsettings(loadsettings);
   }
   if(savesettingsas)
   {  Savesettingsas(savesettingsas);
   }
   return 0;
}

static void Doopenreq(short code,struct Openreq *or)
{  if(code && or && or->string)
   {  struct Awindow *win=Findwindow(or->key);
      if(win) Followurlsmart(win,or->string,0);
   }
   if(or)
   {  FREE(or->string);
      FREE(or);
   }
}

/*-----------------------------------------------------------------------*/

static void Processmenu(struct Awindow *win,struct Menuentry *me,struct MenuItem *item)
{  void *url;
   UBYTE *id;
   if(me)
   {  if(me->cmd[0]=='@')
      {  if(STRIEQUAL(me->cmd,"@DRAGGING"))
         {  SETFLAG(win->flags,WINF_CLIPDRAG,item->Flags&CHECKED);
         }
         else if(STRIEQUAL(me->cmd,"@NOPROXY"))
         {  SETFLAG(win->flags,WINF_NOPROXY,item->Flags&CHECKED);
         }
         else if(STRIEQUAL(me->cmd,"@DEBUGJS"))
         {  /* No action required */
         }
         else if(STRIEQUAL(me->cmd,"@LOADIMGALL"))
         {  Prefsloadimg(LOADIMG_ALL);
         }
         else if(STRIEQUAL(me->cmd,"@LOADIMGMAPS"))
         {  Prefsloadimg(LOADIMG_MAPS);
         }
         else if(STRIEQUAL(me->cmd,"@LOADIMGOFF"))
         {  Prefsloadimg(LOADIMG_OFF);
         }
         else if(STRIEQUAL(me->cmd,"@BGIMAGES"))
         {  Prefsdocolors(item->Flags&CHECKED);
         }
         else if(STRIEQUAL(me->cmd,"@BGSOUND"))
         {  Prefsdobgsound(item->Flags&CHECKED);
         }
         else if(STRIEQUAL(me->cmd,"@OTHERHOT"))
         {  Anewobject(AOTP_FILEREQ,
               AOFRQ_Title,AWEBSTR(MSG_FILE_HOTLISTTITLE),
               AOFRQ_Filename,lasthotlistpath,
               AOFRQ_Pattern,"",
               AOFRQ_Targetwindow,win->key,
               AOBJ_Map,otherhotlistmap,
               TAG_END);
         }
         else if(STRIEQUAL(me->cmd,"@AREXX"))
         {  Anewobject(AOTP_FILEREQ,
               AOFRQ_Title,AWEBSTR(MSG_FILE_AREXXTITLE),
               AOFRQ_Filename,lastarexxpath,
               AOFRQ_Pattern,"",
               AOFRQ_Targetwindow,win->key,
               AOBJ_Map,startarexxmap,
               TAG_END);
         }
      }
      else
      {  url=(void *)Agetattr(win->whis,AOWHS_Url);
         if(id=Rexxframeid(win->focus?win->focus:win->frame))
         {  Execarexxcmd(win->key,me->cmd,"unict",
               (UBYTE *)Agetattr(url,AOURL_Url),
               (UBYTE *)Agetattr(Aweb(),AOAPP_Screenname),
               id,
               (UBYTE *)Agetattr(Aweb(),AOAPP_Configname),
               (UBYTE *)Agetattr(win->frame,AOFRM_Title));
            FREE(id);
         }
      }
   }
}

void Processwindow(void)
{  struct IntuiMessage *msg;
   struct Awindow *win,*nextwin;
   long mousex,mousey;
   UWORD menunum;
   struct MenuItem *item;
   struct Menuentry *me;
   BOOL process;
   USHORT hitflags,key;
   UBYTE *urlpop;
   ULONG gadgetid;
   struct InputEvent ie;
   UBYTE buffer[8];
   struct MsgPort *windowport=(struct MsgPort *)Agetattr(Aweb(),AOAPP_Windowport);
   while(msg=Getimessage(windowport,AOTP_WINDOW))
   {  win=(struct Awindow *)msg->IDCMPWindow->UserData;
      if(msg->Class==IDCMP_MOUSEMOVE && (msg->Qualifier&IEQUALIFIER_LEFTBUTTON)
      && (win->flags&WINF_DRAGSTART) && (win->flags&WINF_CLIPDRAG))
      {  process=Dragselect(win,msg);
      }
      else process=TRUE;
      if(process)
      {  hitflags=0;
         if(msg->Qualifier&prefs.gui.popupkey) hitflags|=AMHF_POPUP;
         else if(msg->Qualifier&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)) hitflags|=AMHF_DOWNLOAD;
         if(msg->Class==IDCMP_MOUSEBUTTONS && (msg->Code&IECODE_UP_PREFIX))
         {  if((prefs.gui.popupkey&IEQUALIFIER_RBUTTON) && msg->Code==MENUUP)
            {  hitflags|=AMHF_POPUPREL;
            }
            else if((prefs.gui.popupkey&IEQUALIFIER_MIDBUTTON) && msg->Code==MIDDLEUP)
            {  hitflags|=AMHF_POPUPREL;
            }
            else if((hitflags&AMHF_POPUP) && msg->Code==SELECTUP)
            {  hitflags|=AMHF_POPUPREL;
            }
         }
         process=Handlelink(win,msg,hitflags);
      }
      if(process)
      {  mousex=msg->MouseX;
         mousey=msg->MouseY;
         win->cmd|=CMD_CHECKLINK;
         switch(msg->Class)
         {  case IDCMP_CLOSEWINDOW:
               /* If intuition.library v46+ and ie_Code is 1, this is from
                * the iconify gadget - call iconify instead of closing */
               if(IntuitionBase && IntuitionBase->lib_Version>=46 && msg->Code==1)
               {  Iconify(TRUE);
               }
               else
               {  win->cmd|=CMD_CLOSE;
               }
               break;
            case IDCMP_IDCMPUPDATE:
               switch(gadgetid=GetTagData(GA_ID,0,(struct TagItem *)msg->IAddress))
               {  case GID_UP:      win->cmd|=CMD_LINEUP|CMD_SCROLLED|CMD_SETSCROLL;break;
                  case GID_DOWN:    win->cmd|=CMD_LINEDOWN|CMD_SCROLLED|CMD_SETSCROLL;break;
                  case GID_LEFT:    win->cmd|=CMD_LEFT|CMD_SCROLLED|CMD_SETSCROLL;break;
                  case GID_RIGHT:   win->cmd|=CMD_RIGHT|CMD_SCROLLED|CMD_SETSCROLL;break;
                  case GID_VSLIDER: win->cmd|=CMD_SCROLLED;break;
                  case GID_HSLIDER: win->cmd|=CMD_SCROLLED;break;
                  case GID_URL:
                     Followurlgadget(win);
                     break;
                  case GID_URLPOP:
                     urlpop=urlpops[Getvalue(win->urlpopgad,CHOOSER_Active)];
                     Setgadgetattrs(win->urlgad,win->window,NULL,
                        STRINGA_TextVal,urlpop,
                        STRINGA_BufferPos,strlen(urlpop),
                        STRINGA_DispPos,0,
                        TAG_END);
                     /* Race condition fix: Check layoutgad is valid before ActivateLayoutGadget,
                      * as it may access ExtGadget MoreFlags internally. */
                     if(win->layoutgad)
                     {  ActivateLayoutGadget(win->layoutgad,win->window,NULL,(ULONG)win->urlgad);
                     }
                     break;
                  case GID_UBUTTON:
                     Douserbutton(win,GetTagData(LAYOUT_RelCode,-1,
                        (struct TagItem *)msg->IAddress));
                     break;
                  default:
                     if(gadgetid>=GID_NAV && gadgetid<GID_NAV+NRNAVBUTTONS)
                     {  Donavbutton(win,gadgetid-GID_NAV);
                     }
                     break;
               }
               break;
            case IDCMP_RAWKEY:
               if(!(msg->Code&IECODE_UP_PREFIX))
               {  if(msg->Qualifier&IEQUALIFIER_RCOMMAND)
                  {  /* Menu shortcut while RMBTRAP flag on */
                     memset(&ie,0,sizeof(ie));
                     ie.ie_Class=IECLASS_RAWKEY;
                     ie.ie_Code=msg->Code;
                     ie.ie_Qualifier=msg->Qualifier&~(IEQUALIFIER_CONTROL);
                     ie.ie_EventAddress=*(APTR *)msg->IAddress;
                     if(MapRawKey(&ie,buffer,8,NULL)==1)
                     {  if(me=Menuentryfromkey(buffer[0]))
                        {  if(item=ItemAddress(win->menu,me->menunum))
                           {  Processmenu(win,me,item);
                           }
                        }
                     }
                  }
                  else if((msg->Qualifier&(IEQUALIFIER_LALT|IEQUALIFIER_RALT))
                  && (msg->Code&0x70)<=0x30 && (msg->Code&0x0f)<=0x0c)
                  {  /* Might be Alt-letter userkey */
                     memset(&ie,0,sizeof(ie));
                     ie.ie_Class=IECLASS_RAWKEY;
                     ie.ie_Code=msg->Code;
                     ie.ie_Qualifier=msg->Qualifier&~(IEQUALIFIER_LALT|IEQUALIFIER_RALT);
                     ie.ie_EventAddress=*(APTR *)msg->IAddress;
                     if(MapRawKey(&ie,buffer,8,NULL)==1)
                     {  buffer[0]=toupper(buffer[0]);
                        if(buffer[0]>='A' && buffer[0]<='Z')
                        {  key=UKEY_ALT|UKEY_ASCII|buffer[0];
                           Douserkey(win,key);
                        }
                     }
                  }
                  else
                  {  /* raw userkey */
                     key=msg->Code;
                     if(msg->Qualifier&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT))
                        key|=UKEY_SHIFT;
                     if(msg->Qualifier&(IEQUALIFIER_LALT|IEQUALIFIER_RALT))
                        key|=UKEY_ALT;
                     Douserkey(win,key);
                  }
               }
               break;
            case IDCMP_EXTENDEDMOUSE:
               /* Mousewheel support requires intuition.library v47+ */
               if(IntuitionBase && IntuitionBase->lib_Version>=47)
               {  if(msg->Code == IMSGCODE_INTUIWHEELDATA)
                  {  struct IntuiWheelData *iwd = (struct IntuiWheelData *)msg->IAddress;
                     key = 0;
                     if(iwd->WheelY > 0)
                     {  key = 0x007b;  /* Wheel down */
                     }
                     else if(iwd->WheelY < 0)
                     {  key = 0x007a;  /* Wheel up */
                     }
                     if(key)
                     {  if(msg->Qualifier&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT))
                           key|=UKEY_SHIFT;
                        if(msg->Qualifier&(IEQUALIFIER_LALT|IEQUALIFIER_RALT))
                           key|=UKEY_ALT;
                        Douserkey(win,key);
                     }
                  }
               }
               break;
            case IDCMP_CHANGEWINDOW:
               if(win->window->Flags&WFLG_ZOOMED)
               {  win->flags|=WINF_ZOOMED;
                  win->zoombox.Left=win->window->LeftEdge;
                  win->zoombox.Top=win->window->TopEdge;
                  win->zoombox.Width=win->window->Width;
                  win->zoombox.Height=win->window->Height;
               }
               else
               {  win->flags&=~WINF_ZOOMED;
                  win->box.Left=win->window->LeftEdge;
                  win->box.Top=win->window->TopEdge;
                  win->box.Width=win->window->Width;
                  win->box.Height=win->window->Height;
               }
               Resizewindow(win);
               break;
            case IDCMP_MOUSEMOVE:
               /* checklink is always done */
               break;
            case IDCMP_MOUSEBUTTONS:
               if(win->nextfocus!=win->focus)
               {  Asetattrs(win->focus,AOFRM_Focus,FALSE,TAG_END);
                  win->focus=win->nextfocus;
                  Setsecure(win);
                  Asetattrs(win->focus,AOFRM_Focus,TRUE,TAG_END);
               }
               win->flags&=~WINF_KEEPDRAG;
               if(msg->Code==SELECTDOWN && win->dragstartobject
               && (msg->Qualifier&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)))
               {  /* Extend drag selection, don't attempt to activate object */
               }
               else
               {  if(msg->Code==SELECTDOWN
                  || ((prefs.gui.popupkey&IEQUALIFIER_RBUTTON) && msg->Code==MENUDOWN)
                  || ((prefs.gui.popupkey&IEQUALIFIER_MIDBUTTON) && msg->Code==MIDDLEDOWN))
                  {  Activatelink(win,msg,hitflags);
                  }
               }
               if(msg->Code==SELECTDOWN && !(win->flags&WINF_KEEPDRAG))
               {  if(!(msg->Qualifier&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)))
                  {  Dragclear(win);
                  }
                  win->flags|=WINF_DRAGSTART;
               }
               else
               {  win->flags&=~WINF_DRAGSTART;
               }
               break;
            case IDCMP_MENUPICK:
               menunum=msg->Code;
               while(menunum!=MENUNULL)
               {  if(!(item=ItemAddress(win->menu,menunum))) break;
                  me=Menuentryfromnum(menunum);
                  if(me) Processmenu(win,me,item);
                  menunum=item->NextSelect;
               }
               break;
            case IDCMP_REFRESHWINDOW:
               Refreshwindow(win);
               break;
            case IDCMP_INTUITICKS:
               /* Refresh screen title periodically even without input.
                * Updatescreentitle() throttles itself to once per second. */
               if(win==activewindow)
               {  Updatescreentitle(win);
               }
               break;
            case IDCMP_ACTIVEWINDOW:
               Setactiveport(win->portname);
               activewindow=win;
               Updatescreentitle(win);
               break;
            case IDCMP_INACTIVEWINDOW:
               Tooltip(NULL,0,0);
               break;
         }
      }
      ReplyMsg(msg);
   }
   for(win=windows.first;win->next;win=nextwin)
   {  BOOL closewin=(win->cmd&CMD_CLOSE);
      BOOL force=(win->cmd&CMD_CLOSEFORCE);
      ULONG left,top;
      nextwin=win->next;
      if(win->cmd&CMD_DEFER)
      {  Ahandleinput(win->activeobject,NULL,AMHF_DEFER,NULL);
      }
      if(win->cmd&CMD_SCROLLED)
      {  Asetattrs(win->focus,AOFRM_Focus,FALSE,TAG_END);
         win->focus=NULL;
         GetAttr(PGA_Top,win->hslider.gad,&left);
         left<<=win->hslider.shift;
         GetAttr(PGA_Top,win->vslider.gad,&top);
         top<<=win->vslider.shift;
         Scrollframe(win,win->frame,top,left);
      }
      else if(win->cmd&CMD_MOVED)
      {  void *focus=win->focus;
         if(!focus) focus=win->frame;
         Agetattrs(focus,
            AOFRM_Toppos,&top,
            AOFRM_Leftpos,&left,
            TAG_END);
         win->cmd|=CMD_SETSCROLL;
         Scrollframe(win,focus,top,left);
      }
      if((win->cmd&CMD_CHECKLINK) && !win->activeobject)
      {  Checklink(win,mousex,mousey,hitflags);
      }
      win->cmd=0;
      if(closewin)
      {  if(((struct Awindow *)windows.first->next)->next) /* >1 window open */
         {  if(STREQUAL(activeport,win->portname)) *activeport='\0';
            REMOVE(win);
            Adisposeobject(win);
         }
         else Quit(force);
      }
   }
}

void Setloadimg(void)
{  struct Awindow *w;
   USHORT minum;
   struct MenuItem *mi;
   ULONG mid=(ULONG)~0;
   UBYTE *mids[3]={ "@LOADIMGALL","@LOADIMGMAPS","@LOADIMGOFF" };
   short i;
   switch(prefs.network.loadimg)
   {  case LOADIMG_ALL: mid=0;break;
      case LOADIMG_MAPS:mid=1;break;
      case LOADIMG_OFF: mid=2;break;
   }
   for(w=windows.first;w->next;w=w->next)
   {  if(prefs.network.loadimg!=LOADIMG_OFF)
      {  Anotifycload(w->frame,(prefs.network.loadimg==LOADIMG_MAPS)?ACMLF_MAPSONLY:0);
      }
      if(w->window)
      {  ClearMenuStrip(w->window);
         for(i=0;i<3;i++)
         {  minum=Menunumfromcmd(mids[i]);
            if(mi=ItemAddress(w->menu,minum))
            {  if(mid==i) mi->Flags|=CHECKED;
               else mi->Flags&=~CHECKED;
            }
         }
         ResetMenuStrip(w->window,w->menu);
      }
   }
}

void Setdocolors(void)
{  struct Awindow *w;
   USHORT minum;
   struct MenuItem *mi;
   for(w=windows.first;w->next;w=w->next)
   {  if(prefs.browser.docolors)
      {  Anotifycload(w->frame,ACMLF_BACKGROUND);
         Anotifyset(w->frame,AOBJ_Bgchanged,TRUE,TAG_END);
         Arender(w->frame,NULL,0,0,AMRMAX,AMRMAX,AMRF_CLEAR,NULL);
      }
      else
      {  Anotifyset(w->frame,AOBJ_Bgchanged,TRUE,TAG_END);
         Arender(w->frame,NULL,0,0,AMRMAX,AMRMAX,AMRF_CLEAR,NULL);
      }
      if(w->window)
      {  ClearMenuStrip(w->window);
         minum=Menunumfromcmd("@BGIMAGES");
         if(mi=ItemAddress(w->menu,minum))
         {  if(prefs.browser.docolors) mi->Flags|=CHECKED;
            else mi->Flags&=~CHECKED;
         }
         ResetMenuStrip(w->window,w->menu);
      }
   }
}

void Setdobgsound(void)
{  struct Awindow *w;
   USHORT minum;
   struct MenuItem *mi;
   for(w=windows.first;w->next;w=w->next)
   {  Anotifyset(w->frame,AOCDV_Playsound,prefs.browser.dobgsound,TAG_END);
      if(w->window)
      {  ClearMenuStrip(w->window);
         minum=Menunumfromcmd("@BGSOUND");
         if(mi=ItemAddress(w->menu,minum))
         {  if(prefs.browser.dobgsound) mi->Flags|=CHECKED;
            else mi->Flags&=~CHECKED;
         }
         ResetMenuStrip(w->window,w->menu);
      }
   }
}

void Doupdateframes(void)
{  struct Awindow *win;
   Busypointer(TRUE);
   for(win=windows.first;win->next;win=win->next)
   {  Asetattrs(win->frame,AOFRM_Updatecopy,TRUE,TAG_END);
   }
   Busypointer(FALSE);
}

void Redisplayall(void)
{  struct Awindow *win;
   for(win=windows.first;win->next;win=win->next)
   {  Arender(win->frame,NULL,0,0,AMRMAX,AMRMAX,AMRF_CLEAR,NULL);
   }
}

void Inputwindoc(void *win,void *url,UBYTE *fragment,UBYTE *frameid)
{  Inputwindocref(win,url,fragment,frameid,FALSE,FALSE);
}

void Inputwindocnoref(void *win,void *url,UBYTE *fragment,UBYTE *frameid)
{  Inputwindocref(win,url,fragment,frameid,TRUE,FALSE);
}

void Inputwindocreload(void *win,void *url,UBYTE *fragment,UBYTE *frameid)
{  Inputwindocref(win,url,fragment,frameid,TRUE,TRUE);
}

void Inputwindocsmart(void *win,UBYTE *url,UBYTE *frameid)
{  Followurlsmart(win,url,frameid);
}

void Setactiveport(UBYTE *portname)
{  if(portname) strncpy(activeport,portname,31);
   else *activeport='\0';
}

void Scrolldoc(struct Awindow *win,long newtop)
{  Asetattrs(win->frame,AOFRM_Toppos,newtop,TAG_END);
}

/* Look if there is an Intuitionevent queued
 * - IDCMP_MENUPICK for MID_BREAKJS.
 * - IDCMP_RAWKEY with R_AMIGA and shortcut for the break JS item.
 */
/* Look if there is an Intuitionevent queued
 * - IDCMP_MENUPICK for MID_BREAKJS.
 * - IDCMP_RAWKEY with R_AMIGA and shortcut for the break JS item.
 */
BOOL Cancelevent(struct Awindow *win)
{  BOOL cancel=FALSE;
   struct IntuiMessage *msg;
   struct Menuentry *me;
   struct InputEvent ie;
   UBYTE buffer[8];
   if(win && win->window && win->window->UserPort)
   {  Forbid();
      for(msg=(struct IntuiMessage *)win->window->UserPort->mp_MsgList.lh_Head;
         !cancel && msg->ln_Succ;
         msg=(struct IntuiMessage *)msg->ln_Succ)
      {  switch(msg->Class)
         {  case IDCMP_MENUPICK:
               me=Menuentryfromnum(msg->Code);
               if(me && STRIEQUAL(me->cmd,"@BREAKJS"))
               {  cancel=TRUE;
               }
               break;
            case IDCMP_RAWKEY:
               if(!(msg->Code&IECODE_UP_PREFIX))
               {  if(msg->Qualifier&IEQUALIFIER_RCOMMAND)
                  {  /* Menu shortcut while RMBTRAP flag on */
                     memset(&ie,0,sizeof(ie));
                     ie.ie_Class=IECLASS_RAWKEY;
                     ie.ie_Code=msg->Code;
                     ie.ie_Qualifier=msg->Qualifier&~(IEQUALIFIER_CONTROL);
                     ie.ie_EventAddress=*(APTR *)msg->IAddress;
                     if(MapRawKey(&ie,buffer,8,NULL)==1)
                     {  if(me=Menuentryfromkey(buffer[0]))
                        {  if(STRIEQUAL(me->cmd,"@BREAKJS"))
                           {  cancel=TRUE;
                           }
                        }
                     }
                  }
               }
               break;
         }
      }
      Permit();
   }
   return cancel;
}

/* Scan queued Intuition messages for refresh events for browser windows.
 * If found, remove the message, process and reply. */
void Refreshevents(void)
{  struct IntuiMessage *msg,*nextmsg;
   struct Aobject *aobj;
   struct MsgPort *windowport;
   windowport=(struct MsgPort *)Agetattr(Aweb(),AOAPP_Windowport);
   if(windowport)
   {  Forbid();
      for(msg=(struct IntuiMessage *)windowport->mp_MsgList.lh_Head;
         msg->ln_Succ;msg=nextmsg)
      {  nextmsg=(struct IntuiMessage *)msg->ln_Succ;
         if(msg->Class==IDCMP_REFRESHWINDOW)
         {  aobj=(struct Aobject *)msg->IDCMPWindow->UserData;
            if(aobj && aobj->objecttype==AOTP_WINDOW)
            {  Remove(msg);
               Permit();
               Refreshwindow((struct Awindow *)aobj);
               ReplyMsg(msg);
               Forbid();
            }
         }
      }
      Permit();
   }
}

void Gohistory(struct Awindow *win,long n)
{  void *whis=win->hiswhis,*owhis=NULL;
   if(win)
   {  whis=win->hiswhis;
      if(n>0)
      {  for(;n && whis;n--)
         {  owhis=whis;
            whis=(void *)Agetattr(whis,AOWHS_Next);
         }
      }
      else
      {  for(;n && whis;n++)
         {  owhis=whis;
            whis=(void *)Agetattr(whis,AOWHS_Previous);
         }
      }
      if(!whis) whis=owhis;
      if(whis && whis!=win->hiswhis)
      {  Followhis(win,whis);
      }
   }
}

void Openfilereq(struct Awindow *win,UBYTE *pattern)
{  Anewobject(AOTP_FILEREQ,
      AOFRQ_Title,AWEBSTR(MSG_FILE_LOCALTITLE),
      AOFRQ_Filename,lastopenpath,
      AOFRQ_Pattern,pattern?pattern:lastopenpattern,
      AOFRQ_Targetwindow,win->key,
      AOBJ_Map,openlocalmap,
      TAG_END);
}

void Openurlreq(struct Awindow *win)
{  struct Openreq *or=ALLOCSTRUCT(Openreq,1,MEMF_PUBLIC|MEMF_CLEAR);
   if(or)
   {  if(or->string=ALLOCTYPE(UBYTE,128,MEMF_PUBLIC|MEMF_CLEAR))
      {  or->key=win->key;
         Asyncpromptrequest(AWEBSTR(MSG_OPENURL_TITLE),
            AWEBSTR(MSG_OPENURL_TEXT),AWEBSTR(MSG_OPENURL_BUTTONS),
            Doopenreq,or,or->string);
      }
      else
      {  FREE(or);
      }
   }
}

void Savesettingsreq(struct Awindow *win)
{  Anewobject(AOTP_FILEREQ,
      AOFRQ_Title,AWEBSTR(MSG_FILE_SETTINGSTITLE),
      AOFRQ_Filename,"ENVARC:" DEFAULTCFG "/",
      AOFRQ_Targetwindow,win->key,
      AOFRQ_Dirsonly,TRUE,
      AOFRQ_Savemode,TRUE,
      AOBJ_Map,savesettingsasmap,
      TAG_END);
}

void Loadsettingsreq(struct Awindow *win)
{  Anewobject(AOTP_FILEREQ,
      AOFRQ_Title,AWEBSTR(MSG_FILE_SETTINGSTITLE),
      AOFRQ_Filename,"ENV:" DEFAULTCFG "/",
      AOFRQ_Targetwindow,win->key,
      AOFRQ_Dirsonly,TRUE,
      AOBJ_Map,loadsettingsmap,
      TAG_END);
}

