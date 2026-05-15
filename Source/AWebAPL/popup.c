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

/* popup.c - AWeb popup menu object */

#include "aweb.h"
#include "popup.h"
#include "window.h"
#include "application.h"
#include <intuition/intuition.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <images/bevel.h>
#include <classes/window.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/bevel.h>
#include <proto/window.h>

/*------------------------------------------------------------------------*/

#define NRMENUITEMS  18

struct Popup
{  struct Aobject object;
   void *target;                    /* Current target for which labels are added */
   void *owner;                     /* Owner target that created the popup */
   short left,top;                  /* Our coordinates */
   void *win;                       /* The WINDOW object attached to. */
   void *cawin;                     /* The Reaction Window object. */
void *layout;                    /* The Reaction layout object. */
   LIST(Label) labels;              /* All the labels. */
   struct Label *selected;          /* Selected label, action still to perform. */
   struct Window *window;           /* The Intuition Window. */
   struct DrawInfo *drinfo;
   struct Hook idcmphook;
   USHORT flags;
};

#define PUPF_DONE          0x0001   /* Menu should close. */
#define PUPF_VALID         0x0002   /* Menu has items. */
#define PUPF_CLOSING       0x0004   /* Closing, don't dispose. */
#define PUPF_SEPARATOR     0x0008   /* Add separator before next item */

struct Label                        /* Also kept in gadget's UserData */
{  NODE(Label);
   void *target;                    /* Target object to update */
   UBYTE *title;                    /* Title string to display */
   UBYTE *command;                  /* Command to execute when selected */
};

static LIST(Popup) popups;

/*------------------------------------------------------------------------*/

static void Freelabel(struct Label *l)
{  if(l)
   {  if(l->title) FREE(l->title);
      if(l->command) FREE(l->command);
      FREE(l);
   }
}

/* Add a separator if there are already items in the menu */
static void Addseparator(struct Popup *pup)
{  if(pup->flags&PUPF_VALID)  /* Already items in menu - add separator */
   {  SetAttrs(pup->layout,
         StartImage,BevelObject,
            BEVEL_Style,BVS_SBAR_VERT,
         EndImage,
      End;
   }
}

/* Add a button */
static void Addbutton(struct Popup *pup,UBYTE *title,UBYTE *command)
{  struct Label *l;
   void *button;
   long bpen=-1,dpen=-1;
   if(pup->drinfo->dri_NumPens>BARBLOCKPEN)
   {  bpen=pup->drinfo->dri_Pens[BARBLOCKPEN];
      dpen=pup->drinfo->dri_Pens[BARDETAILPEN];
   }
   if((l=ALLOCSTRUCT(Label,1,MEMF_PUBLIC|MEMF_CLEAR))
   && (l->title=Dupstr(title,-1))
   && (l->command=Dupstr(command,-1)))
   {  l->target=pup->target;
      button=ButtonObject,
         GA_ID,1,
         GA_RelVerify,TRUE,
         GA_Text,l->title,
         GA_UserData,l,
         BUTTON_BevelStyle,BVS_NONE,
         BUTTON_TextPen,dpen,
         BUTTON_BackgroundPen,bpen,
         BUTTON_FillTextPen,bpen,
         BUTTON_FillPen,dpen,
      End;
      if(button)
      {  SetAttrs(pup->layout,
            StartMember,button,
         End;
         pup->flags|=PUPF_VALID;
         return;
      }
   }
   if(l) Freelabel(l);
}

static long __saveds __asm Idcmphook(register __a0 struct Hook *hook,
   register __a1 struct IntuiMessage *msg)
{  struct Popup *pup=hook->h_Data;
   struct Gadget *gad;
   switch(msg->Class)
   {  case IDCMP_MOUSEBUTTONS:
         if(msg->Code==MENUDOWN)
         {  pup->flags|=PUPF_DONE;
            Signal(FindTask(NULL),1<<pup->window->UserPort->mp_SigBit);
         }
         break;
      case IDCMP_IDCMPUPDATE:
         if(gad=(struct Gadget *)
            GetTagData(LAYOUT_RelAddress,NULL,(struct TagItem *)msg->IAddress))
         {  pup->selected=(struct Label *)gad->UserData;
         }
         break;
   }
   return 0;
}

static void Processpopup(void)
{  struct Popup *pup,*next;
   ULONG result;
   for(pup=popups.first;pup->next;pup=next)
   {  next=pup->next;
      if(!(pup->flags&PUPF_DONE))
      {  while((result=RA_HandleInput(pup->cawin,NULL))!=WMHI_LASTMSG)
         {  switch(result&WMHI_CLASSMASK)
            {  case WMHI_GADGETUP:
                  pup->flags|=PUPF_DONE;
                  break;
               case WMHI_INACTIVE:
                  pup->flags|=PUPF_DONE;
                  break;
            }
         }
      }
      if(pup->flags&PUPF_DONE)
      {  if(pup->selected)
         {  /* This could dispose us, prevent from disappearing under our own ass */
            pup->flags|=PUPF_CLOSING;
            Aupdateattrs(pup->selected->target,NULL,
               AOPUP_Command,pup->selected->command,
               TAG_END);
            pup->flags&=~PUPF_CLOSING;
            if(pup->selected->target==pup->owner)
            {  /* Don't notify owner twice */
               pup->owner=NULL;
            }
            pup->selected=NULL;
         }
         Adisposeobject(pup);
      }
   }
}

/*------------------------------------------------------------------------*/

static long Setpopup(struct Popup *pup,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *title=NULL,*command=NULL;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOPUP_Target:
            if(tag->ti_Data)
            {  pup->target=(void *)tag->ti_Data;
               pup->flags|=PUPF_SEPARATOR;
               Asetattrs(pup->target,AOPUP_Inquire,pup,TAG_END);
               if(!pup->owner) pup->owner=(void *)tag->ti_Data;
            }
            break;
         case AOPUP_Left:
            pup->left=tag->ti_Data-4;
            break;
         case AOPUP_Top:
            pup->top=tag->ti_Data-4;
            break;
         case AOPUP_Window:
            if(pup->win) Aremchild(pup->win,pup,AOREL_WIN_POPUP);
            pup->win=(void *)tag->ti_Data;
            if(pup->win) Aaddchild(pup->win,pup,AOREL_WIN_POPUP);
            break;
         case AOPUP_Title:
            title=(UBYTE *)tag->ti_Data;
            break;
         case AOPUP_Command:
            command=(UBYTE *)tag->ti_Data;
            break;
         case AOAPP_Screenvalid:
            if(!tag->ti_Data)
            {  Adisposeobject(pup);
               return 0;
            }
            break;
      }
   }
   if(title && command)
   {  if(pup->flags&PUPF_SEPARATOR)
      {  Addseparator(pup);
         pup->flags&=~PUPF_SEPARATOR;
      }
      Addbutton(pup,title,command);
   }
   return 0;
}

static void Disposepopup(struct Popup *pup)
{  struct Label *l;
   if(pup->flags&PUPF_CLOSING)
   {  /* The only one that can dispose us is our owner, as a result
       * of updating the target that is not the owner (like target=
       * frame, disposing the copy that is our owner).
       * Prevent updating our owner later, since it doesn't expect
       * to hear from us any more now or is gone itself. */
       pup->owner=NULL;
   }
   else
   {  if(pup->owner)
      {  Aupdateattrs(pup->owner,NULL,
            AOPUP_Command,NULL,
            TAG_END);
      }
      if(pup->win) Aremchild(pup->win,pup,AOREL_WIN_POPUP);
      if(pup->cawin)
      {  DisposeObject(pup->cawin);
      }
      while(l=REMHEAD(&pup->labels)) Freelabel(l);
      Aremchild(Aweb(),pup,AOREL_APP_USE_SCREEN);
      REMOVE(pup);
      Amethodas(AOTP_OBJECT,pup,AOM_DISPOSE);
   }
}

static struct Popup *Newpopup(struct Amset *ams)
{  struct Popup *pup;
   struct Screen *screen;
   struct MsgPort *port;
   if(pup=Allocobject(AOTP_POPUP,sizeof(struct Popup),ams))
   {  NEWLIST(&pup->labels);
      ADDTAIL(&popups,pup);
      Aaddchild(Aweb(),pup,AOREL_APP_USE_SCREEN);
      if(Agetattr(Aweb(),AOAPP_Screenvalid))
      {  pup->idcmphook.h_Entry=(HOOKFUNC)Idcmphook;
         pup->idcmphook.h_Data=pup;
         Agetattrs(Aweb(),
            AOAPP_Screen,&screen,
            AOAPP_Drawinfo,&pup->drinfo,
            AOAPP_Windowport,&port,
            TAG_END);
         Asetattrs(Aweb(),
            AOAPP_Processtype,AOTP_POPUP,
            AOAPP_Processfun,Processpopup,
            TAG_END);
         pup->cawin=WindowObject,
            WA_Borderless,TRUE,
            WA_Activate,FALSE,
            WA_RMBTrap,TRUE,
            WA_IDCMP,IDCMP_MOUSEBUTTONS|IDCMP_IDCMPUPDATE,
            WA_PubScreen,screen,
            WINDOW_SharedPort,port,
            WINDOW_UserData,pup,
            WINDOW_IDCMPHook,&pup->idcmphook,
            WINDOW_IDCMPHookBits,IDCMP_MOUSEBUTTONS|IDCMP_IDCMPUPDATE,
            WINDOW_Layout,pup->layout=VLayoutObject,
               LAYOUT_SpaceOuter,FALSE,
               LAYOUT_SpaceInner,FALSE,
               LAYOUT_BevelStyle,BVS_BOX,
            End,
         End;
      }
      if(pup->cawin && pup->layout)
      {  Setpopup(pup,ams);
         if(pup->win && pup->cawin)
         {  struct Window *window=(struct Window *)Agetattr(pup->win,AOWIN_Window);
            if(window)
            {  pup->left+=window->LeftEdge;
               pup->top+=window->TopEdge;
            }
            SetAttrs(pup->cawin,
               WA_Left,pup->left,
               WA_Top,pup->top,
               TAG_END);
         }
      }
      if(pup->flags&PUPF_VALID)
      {  struct Window *parent;
         pup->window=RA_OpenWindow(pup->cawin);
         /* Keep the browser window active; the popup must not steal input. */
         if(pup->window && pup->win)
         {  parent=(struct Window *)Agetattr(pup->win,AOWIN_Window);
            if(parent && parent!=pup->window) ActivateWindow(parent);
         }
      }
      if(!pup->window)
      {  Disposepopup(pup);
         pup=NULL;
      }
   }
   return pup;
}

static long Dispatch(struct Popup *pup,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newpopup((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setpopup(pup,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposepopup(pup);
         break;
      case AOM_DEINSTALL:
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

/* Dismiss open popup menus without selecting an item (e.g. click in parent). */
void Closeopenpopups(void)
{  struct Popup *pup;
   for(pup=popups.first;pup->next;pup=pup->next)
   {  pup->flags|=PUPF_DONE;
   }
   Processpopup();
}

BOOL Installpopup(void)
{  NEWLIST(&popups);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_POPUP,Dispatch)) return FALSE;
   return TRUE;
}
