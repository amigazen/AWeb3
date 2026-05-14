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

/* cfgpr.c - AWebCfg program window */

#include "awebcfg.h"
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>

/* Missing Reaction defines - not available in proto headers */
#ifndef REACTION_Underscore
#define REACTION_Underscore 0x80000000
#endif
#include <images/bevel.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <classes/window.h>
#include <proto/intuition.h>




ULONG prmask=0;

static struct Programprefs pgp,orgpgp;
static BOOL tested=FALSE;

enum PGPREFS_PAGES
{  PGPREFS_SCREEN,PGPREFS_PALETTE,
   PGPREFS_OPTIONS,PGPREFS_PROGRAMS,PGPREFS_AREX,
};

static struct List tablist;
static struct List scrlist,scrplist,
   col4list,scrpenlist,
   progtypelist,proghelplist,imgvhelplist,conshelplist;

static UBYTE *tablabels[5];
static UBYTE *screenlabels[4];
static UBYTE *scrplabels[4];
static UBYTE *col4labels[3];
static UBYTE *scrcolorpennames[10];
static UBYTE *progtypelabels[6]={ NULL,NULL,NULL,NULL,NULL,NULL };
static UBYTE *proghelplabels[3];
static UBYTE *imgvhelplabels[4];
static UBYTE *conshelplabels[3];
static struct Image scrpenimg[10];

static void *winobject=NULL;
static struct Gadget *toplayout,*tabgad,*pagelayout,*pagegad;
static struct Gadget *scrtypegad,*scrpagelayout,*scrpage,*scrngad,*scrmgad,
   *scrpgad,*col4gad,*penlistgad,*penpalgad,*scrpalgad,*poppalgad,*savegad,
   *tempgad,*ovlpgad,*cmdsgad,*draggad,
   *hlacgad,*hlrqgad,*whacgad,*savigad,*aahlgad,*aawhgad,*aansgad,*cenrgad,
   *progtypegad,*progcmdgad,*progargsgad,*proghelpgad,*consgad;

static UBYTE prefsname[64];
static struct MsgPort *nport;
static struct Window *window=NULL;
static struct Menu *menubar;
static struct NotifyRequest nreq;
static UBYTE *screenmodename;

enum GADGET_IDS
{  PGID_TABS=1,
   PGID_SCRTYPE,PGID_SCRPOP,PGID_SCRPENLIST,PGID_SCRPENPAL,PGID_SCRPOPCOLOR,
   PGID_FOURCOLORS,
   PGID_PRGPOPSAVE,PGID_POPTEMP,
   PGID_PROGTYPE,PGID_PROGCMD,PGID_PROGPOPC,PGID_PROGARGS,
   PGID_SAVE,PGID_USE,PGID_TEST,PGID_CANCEL,
};

static USHORT endmode;  /* gadget id that caused end (save, use) */

static ULONG progmsgid[5]=
{  MSG_SET_REQTITLE_EDITOR,
   MSG_SET_REQTITLE_HTMLVIEWER,
   MSG_SET_REQTITLE_IMGVIEWER,
   MSG_SET_REQTITLE_STARTUPSCRIPT,
   MSG_SET_REQTITLE_SHUTDOWNSCRIPT,
};

/*---------------------------------------------------------------------------*/

static void *Makescreenpage(void)
{  void *object=VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      LAYOUT_FixedVert,FALSE,
      StartMember,scrtypegad=ChooserObject,
         GA_ID,PGID_SCRTYPE,
         GA_RelVerify,TRUE,
         CHOOSER_PopUp,TRUE,
         CHOOSER_Labels,&scrlist,
         CHOOSER_Active,pgp.screentype,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_SCREEN_OPENON)),
      StartMember,scrpagelayout=HLayoutObject,
         StartMember,scrpage=PageObject,
            PAGE_Add,VLayoutObject,
            End,
            PAGE_Add,VLayoutObject,
               StartMember,scrngad=StringObject,
                  GA_TabCycle,TRUE,
                  STRINGA_TextVal,pgp.screenname,
                  STRINGA_MaxChars,64,
               EndMember,
               MemberLabel(CFGAWEBSTR(MSG_SET_SCREEN_NAME)),
            End,
            PAGE_Add,VLayoutObject,
               StartMember,HLayoutObject,
                  LAYOUT_InnerSpacing,1,
                  LAYOUT_VertAlignment,LALIGN_CENTER,
                  StartImage,LabelObject,
                     LABEL_Text,CFGAWEBSTR(MSG_SET_SCREEN_MODE),
                  EndImage,
                  StartMember,scrmgad=ButtonObject,
                     GA_Text," ",
                     GA_ReadOnly,TRUE,
                     BUTTON_Justification,BCJ_LEFT,
                     REACTION_Underscore,0,
                  EndMember,
                  CHILD_NominalSize,TRUE,
                  StartMember,ButtonObject,
                     GA_ID,PGID_SCRPOP,
                     GA_RelVerify,TRUE,
                     BUTTON_AutoButton,BAG_POPSCREEN,
                     REACTION_CommKey,Hotkey(CFGAWEBSTR(MSG_SET_SCREEN_MODE)),
                  EndMember,
                  CHILD_MaxWidth,20,
               EndMember,
               StartMember,scrpgad=ChooserObject,
                  CHOOSER_PopUp,TRUE,
                  CHOOSER_Labels,&scrplist,
                  CHOOSER_Active,pgp.loadpalette,
                  GA_Disabled,pgp.screendepth>8,
               EndMember,
               MemberLabel(CFGAWEBSTR(MSG_SET_SCREEN_PALETTE)),
               CHILD_WeightedWidth,0,
            End,
         EndMember,
      EndMember,
   End;
   if(scrpage) SetAttrs(scrpage,PAGE_Current,pgp.screentype,TAG_END);
   screenmodename=Prefsscreenmodename(pgp.screenmode,
      pgp.screenwidth,pgp.screenheight,pgp.screendepth);
   return object;
}

static void Makescrpenlist(struct List *list)
{  short i;
   struct Node *node;
   UWORD *onoffs=pgp.scrdrawpens+2;
   for(i=0;scrcolorpennames[i];i++)
   {  scrpenimg[i].Width=32;
      scrpenimg[i].Height=8;
      scrpenimg[i].PlaneOnOff=onoffs[i];
      if(pgp.fourcolors) scrpenimg[i].PlaneOnOff&=0x03;
      if(node=AllocListBrowserNode(2,
         LBNA_Column,0,
            LBNCA_Image,&scrpenimg[i],
            LBNCA_Justification,LCJ_CENTER,
         LBNA_Column,1,
            LBNCA_Text,scrcolorpennames[i],
         TAG_END))
         AddTail(list,node);
   }
}

static struct ColumnInfo scrcolorcolumns[]=
{  0,NULL,0,
   0,NULL,0,
   -1,NULL,0
};

static void *Makepalettepage(void)
{  BOOL ownscreen=Ownscreen();
   void *object=
   VLayoutObject,
      StartMember,VLayoutObject,
         LAYOUT_SpaceOuter,TRUE,
         LAYOUT_BevelStyle,BVS_GROUP,
         LAYOUT_Label,CFGAWEBSTR(MSG_SET_PALL_PENLABEL),
/* palette gadget bug makes this unusable
         StartMember,VLayoutObject,
            StartMember,col4gad=ChooserObject,
               GA_ID,PGID_FOURCOLORS,
               GA_RelVerify,TRUE,
               CHOOSER_PopUp,TRUE,
               CHOOSER_Labels,&col4list,
               CHOOSER_Active,pgp.fourcolors,
               GA_Disabled,!ownscreen,
            EndMember,
            MemberLabel(CFGAWEBSTR(MSG_SET_PALL_NRCOLOURS)),
            CHILD_WeightedWidth,0,
         EndMember,
         CHILD_WeightedHeight,0,
*/
         StartMember,penlistgad=ListBrowserObject,
            LISTBROWSER_ShowSelected,TRUE,
            LISTBROWSER_AutoFit,TRUE,
            LISTBROWSER_ColumnInfo,scrcolorcolumns,
            LISTBROWSER_Labels,&scrpenlist,
            LISTBROWSER_Separators,FALSE,
            LISTBROWSER_Selected,0,
            GA_ID,PGID_SCRPENLIST,
            GA_RelVerify,TRUE,
            GA_Disabled,!ownscreen,
         EndMember,
         StartMember,penpalgad=PaletteObject,
            PALETTE_NumColors,pgp.fourcolors?4:8,
            PALETTE_Colour,pgp.scrdrawpens[2], /* offset 2 */
            GA_ID,PGID_SCRPENPAL,
            GA_RelVerify,TRUE,
            GA_Disabled,!ownscreen,
         EndMember,
         CHILD_MinHeight,16,
         CHILD_WeightedHeight,0,
         MemberLabel(CFGAWEBSTR(MSG_SET_PALL_PEN)),
      EndMember,
      StartMember,HLayoutObject,
         LAYOUT_SpaceOuter,TRUE,
         LAYOUT_BevelStyle,BVS_GROUP,
         LAYOUT_Label,CFGAWEBSTR(MSG_SET_PALL_PALLETTELABEL),
         StartMember,scrpalgad=PaletteObject,
            PALETTE_NumColors,pgp.fourcolors?4:8,
            GA_Disabled,!ownscreen,
         EndMember,
         CHILD_MinHeight,16,
         MemberLabel(CFGAWEBSTR(MSG_SET_PALL_PALLPEN)),
         StartMember,poppalgad=ButtonObject,
            GA_Text,CFGAWEBSTR(MSG_SET_PALL_CHGCOLOUR),
            GA_ID,PGID_SCRPOPCOLOR,
            GA_RelVerify,TRUE,
            GA_Disabled,!ownscreen,
         EndMember,
         CHILD_WeightedWidth,0,
      EndMember,
      CHILD_WeightedHeight,0,
   End;
   SetGadgetAttrs(penlistgad,NULL,NULL,GA_Disabled,!Ownscreen(),TAG_END);
   return object;
}

static void *Makeoptionspage(void)
{  return
   VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      LAYOUT_FixedVert,FALSE,
      StartMember,VLayoutObject,
         StartMember,HLayoutObject,
            LAYOUT_SpaceInner,FALSE,
            StartMember,savegad=StringObject,
               GA_TabCycle,TRUE,
               STRINGA_TextVal,pgp.savepath,
               STRINGA_MaxChars,127,
            EndMember,
            StartMember,ButtonObject,
               BUTTON_AutoButton,BAG_POPDRAWER,
               GA_ID,PGID_PRGPOPSAVE,
               GA_RelVerify,TRUE,
            EndMember,
            CHILD_MaxWidth,20,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_PROGRAM_SAVEPATH)),
         StartMember,HLayoutObject,
            LAYOUT_SpaceInner,FALSE,
            StartMember,tempgad=StringObject,
               GA_TabCycle,TRUE,
               STRINGA_TextVal,pgp.temppath,
               STRINGA_MaxChars,127,
            EndMember,
            StartMember,ButtonObject,
               BUTTON_AutoButton,BAG_POPDRAWER,
               GA_ID,PGID_POPTEMP,
               GA_RelVerify,TRUE,
            EndMember,
            CHILD_MaxWidth,20,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_CACHE_TEMPPATH)),
      EndMember,
      StartMember,VLayoutObject,
         StartMember,ovlpgad=IntegerObject,
            GA_TabCycle,TRUE,
            INTEGER_Minimum,0,
            INTEGER_Maximum,200,
            INTEGER_Number,pgp.overlap,
         EndMember,
         CHILD_MaxWidth,120,
         MemberLabel(CFGAWEBSTR(MSG_SET_PROGRAM_OVERLAP)),
      EndMember,
      StartMember,HLayoutObject,
         StartMember,VLayoutObject,
            StartMember,cmdsgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_PROGRAM_ALLOW),
               GA_Selected,pgp.commands,
#ifdef DEMOVERSION
               GA_Disabled,TRUE,
#endif
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,hlacgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_PROGRAM_HOTCLOSE),
               GA_Selected,pgp.hlautoclose,
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,hlrqgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_PROGRAM_HOTREQ),
               GA_Selected,pgp.hlrequester,
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,whacgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_PROGRAM_WHISCLOSE),
               GA_Selected,pgp.whautoclose,
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,draggad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_PROGRAM_DRAGGING),
               GA_Selected,pgp.clipdrag,
#ifdef DEMOVERSION
               GA_Disabled,TRUE,
#endif
            EndMember,
            CHILD_WeightedWidth,0,
         EndMember,
         StartMember,VLayoutObject,
            StartMember,savigad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_PROGRAM_SAVEICONS),
               GA_Selected,pgp.saveicons,
#ifdef DEMOVERSION
               GA_Disabled,TRUE,
#endif
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,aahlgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_PROGRAM_AAHOTLIST),
               GA_Selected,pgp.aahotlist,
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,aawhgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_PROGRAM_AAWINHIS),
               GA_Selected,pgp.aawinhis,
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,aansgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_PROGRAM_AANETSTAT),
               GA_Selected,pgp.aanetstat,
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,cenrgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_PROGRAM_CENTERREQS),
               GA_Selected,pgp.centerreq,
            EndMember,
            CHILD_WeightedWidth,0,
         EndMember,
      EndMember,
   End;
}

static void *Makeprogramspage(void)
{  return
   VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      LAYOUT_FixedVert,FALSE,
      StartMember,progtypegad=ChooserObject,
         CHOOSER_PopUp,TRUE,
         CHOOSER_Labels,&progtypelist,
         CHOOSER_Active,0,
         GA_ID,PGID_PROGTYPE,
         GA_RelVerify,TRUE,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_PRPROGRAMS_TYPE)),
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,progcmdgad=StringObject,
            GA_TabCycle,TRUE,
            GA_ID,PGID_PROGCMD,
            GA_RelVerify,TRUE,
            STRINGA_TextVal,pgp.editcmd,
            STRINGA_MaxChars,127,
         EndMember,
         StartMember,ButtonObject,
            BUTTON_AutoButton,BAG_POPFILE,
            GA_ID,PGID_PROGPOPC,
            GA_RelVerify,TRUE,
         EndMember,
         CHILD_MaxWidth,20,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_EDITOR_CMD)),
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,progargsgad=StringObject,
            GA_TabCycle,TRUE,
            GA_ID,PGID_PROGARGS,
            GA_RelVerify,TRUE,
            STRINGA_TextVal,pgp.editargs,
            STRINGA_MaxChars,127,
         EndMember,
         StartMember,proghelpgad=ChooserObject,
            CHOOSER_DropDown,TRUE,
            CHOOSER_Labels,&proghelplist,
            CHOOSER_AutoFit,TRUE,
         EndMember,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_EDITOR_ARGS)),
      StartMember,VLayoutObject,
         LAYOUT_BevelStyle,BVS_SBAR_VERT,
      EndMember,
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,consgad=StringObject,
            GA_TabCycle,TRUE,
            STRINGA_TextVal,pgp.console,
            STRINGA_MaxChars,127,
         EndMember,
         StartMember,ChooserObject,
            CHOOSER_DropDown,TRUE,
            CHOOSER_Labels,&conshelplist,
            CHOOSER_AutoFit,TRUE,
         EndMember,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_CONSOLE)),
   End;
}

/*---------------------------------------------------------------------------*/

static void Doscrpop(void)
{  struct ScreenModeRequester *sm;
   UBYTE *p;
   if(sm=AllocAslRequestTags(ASL_ScreenModeRequest,
      ASLSM_Window,window,
      ASLSM_TitleText,CFGAWEBSTR(MSG_SET_REQTITLE_SCREEN),
      ASLSM_InitialHeight,Reqheight(window->WScreen),
      ASLSM_InitialDisplayID,pgp.screenmode,
      ASLSM_InitialDisplayDepth,pgp.screendepth,
      ASLSM_InitialDisplayWidth,pgp.screenwidth,
      ASLSM_InitialDisplayHeight,pgp.screenheight,
      ASLSM_DoDepth,TRUE,
      ASLSM_DoWidth,TRUE,
      ASLSM_DoHeight,TRUE,
      ASLSM_MinWidth,640,
      ASLSM_MinHeight,200,
      ASLSM_MinDepth,3,
      TAG_END))
   {  SetGadgetAttrs(toplayout,window,NULL,LAYOUT_DeferLayout,FALSE,TAG_END);
      SetAttrs(winobject,WA_BusyPointer,TRUE,GA_ReadOnly,TRUE,TAG_END);
      if(AslRequest(sm,NULL))
      {  pgp.screenmode=sm->sm_DisplayID;
         pgp.screendepth=sm->sm_DisplayDepth;
         pgp.screenwidth=sm->sm_DisplayWidth;
         pgp.screenheight=sm->sm_DisplayHeight;
         if(p=Prefsscreenmodename(pgp.screenmode,
            pgp.screenwidth,pgp.screenheight,pgp.screendepth))
         {  if(screenmodename) FREE(screenmodename);
            screenmodename=p;
            Setgadgetattrs(scrmgad,window,NULL,
               GA_Text,screenmodename,TAG_END);
            Setgadgetattrs(scrpgad,window,NULL,
               GA_Disabled,pgp.screendepth>8,
               TAG_END);
         }
      }
      SetAttrs(winobject,WA_BusyPointer,FALSE,GA_ReadOnly,FALSE,TAG_END);
      SetGadgetAttrs(toplayout,window,NULL,LAYOUT_DeferLayout,TRUE,TAG_END);
      FreeAslRequest(sm);
   }
}

/*
static void Docol4(void)
{  pgp.fourcolors=Getvalue(col4gad,CHOOSER_Active);
   Setgadgetattrs(penlistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&scrpenlist);
   Makescrpenlist(&scrpenlist);
   Setgadgetattrs(penlistgad,window,NULL,LISTBROWSER_Labels,&scrpenlist,TAG_END);
   Setgadgetattrs(penpalgad,window,NULL,PALETTE_NumColours,(pgp.fourcolors?4:8),TAG_END);
   Setgadgetattrs(scrpalgad,window,NULL,PALETTE_NumColours,(pgp.fourcolors?4:8),TAG_END);
}
*/

static void Doscrpenlist(WORD select)
{  short pen=pgp.scrdrawpens[select+2];
   Setgadgetattrs(penpalgad,window,NULL,
      PALETTE_Colour,pen,
      GA_Disabled,select==5,  /* backgroundpen */
      TAG_END);
}

static void Doscrpenpal(WORD pen)
{  short select=Getvalue(penlistgad,LISTBROWSER_Selected);
   Setgadgetattrs(penlistgad,window,NULL,
      LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&scrpenlist);
   pgp.scrdrawpens[select+2]=pen;
   Makescrpenlist(&scrpenlist);
   Setgadgetattrs(penlistgad,window,NULL,
      LISTBROWSER_Labels,&scrpenlist,TAG_END);
}

static void Doscrpopcolor(void)
{  long pen=Getvalue(scrpalgad,PALETTE_Colour);
   if(Popcolor(winobject,window,toplayout,NULL,pen))
   {  GetRGB32(window->WScreen->ViewPort.ColorMap,pen,1,
         &pgp.scrpalette[3*pen]);
   }
}

static void Doprgpopsave(void)
{  Popdrawer(winobject,window,toplayout,CFGAWEBSTR(MSG_SET_REQTITLE_SAVEPATH),savegad);
}

/* handle temp file popup */
static void Doprgpoptemp(void)
{  Popdrawer(winobject,window,toplayout,CFGAWEBSTR(MSG_SET_REQTITLE_TEMPPATH),tempgad);
}

/* return pointer to ext program string for code */
static UBYTE **Prprgptr(short code)
{  switch(code)
   {  case 0:  return &pgp.editcmd;
      case 1:  return &pgp.viewcmd;
      case 2:  return &pgp.imgvcmd;
      case 3:  return &pgp.startupscript;
      case 4:  return &pgp.shutdownscript;
   }
   return NULL;
}

/* handle program type select */
static void Doprprgtp(short code)
{  UBYTE **p=Prprgptr(code);
   Setgadgetattrs(progcmdgad,window,NULL,
      STRINGA_TextVal,p[0]?p[0]:NULLSTRING,
      STRINGA_DispPos,0,
      TAG_END);
   if(code<3)
   {  Setgadgetattrs(progargsgad,window,NULL,
         STRINGA_TextVal,p[1]?p[1]:NULLSTRING,
         STRINGA_DispPos,0,
         GA_Disabled,FALSE,
         TAG_END);
      Setgadgetattrs(proghelpgad,window,NULL,
         CHOOSER_Labels,(code==2)?&imgvhelplist:&proghelplist,
         GA_Disabled,FALSE,
         TAG_END);
   }
   else
   {  Setgadgetattrs(progargsgad,window,NULL,
         STRINGA_TextVal,"",
         GA_Disabled,TRUE,
         TAG_END);
      Setgadgetattrs(proghelpgad,window,NULL,
         GA_Disabled,TRUE,
         TAG_END);
   }
}

/* handle program command entered */
static void Doprprgcmd(void)
{  short code=Getvalue(progtypegad,CHOOSER_Active);
   UBYTE **p=Prprgptr(code);
   Getstringvalue(p,progcmdgad);
}

/* handle program command popup */
static void Doprprgpop(void)
{  short code=Getvalue(progtypegad,CHOOSER_Active);
   Popfile(winobject,window,toplayout,CFGAWEBSTR(progmsgid[code]),progcmdgad);
   Doprprgcmd();
}

/* handle program args entered */
static void Doprprgargs(void)
{  short code=Getvalue(progtypegad,CHOOSER_Active);
   if(code<3)
   {  UBYTE **p=Prprgptr(code);
      Getstringvalue(p+1,progargsgad);
   }
}

static void Docursor(short n)
{  long page=Getvalue(pagegad,PAGE_Current);
   long newsel;
   switch(page)
   {  case PGPREFS_PALETTE:
         newsel=Moveselected(window,penlistgad,&scrpenlist,n);
         if(newsel>=0) Doscrpenlist(newsel);
         break;
   }
}

/* get data from gadgets */
static void Copydata(void)
{  Getstringvalue(&pgp.savepath,savegad);
   Getstringvalue(&pgp.temppath,tempgad);
   pgp.overlap=Getvalue(ovlpgad,INTEGER_Number);
   pgp.commands=Getselected(cmdsgad);
   pgp.hlautoclose=Getselected(hlacgad);
   pgp.hlrequester=Getselected(hlrqgad);
   pgp.whautoclose=Getselected(whacgad);
   pgp.clipdrag=Getselected(draggad);
   pgp.saveicons=Getselected(savigad);
   pgp.aahotlist=Getselected(aahlgad);
   pgp.aawinhis=Getselected(aawhgad);
   pgp.aanetstat=Getselected(aansgad);
   pgp.centerreq=Getselected(cenrgad);
   Getstringvalue(&pgp.console,consgad);
   pgp.screentype=Getvalue(scrtypegad,CHOOSER_Active);
   Getstringvalue(&pgp.screenname,scrngad);
   pgp.loadpalette=Getvalue(scrpgad,CHOOSER_Active);
}

/* set gadgets to changed data */
static void Setdata(void)
{  long page=Getvalue(pagegad,PAGE_Current);
   long n;
   UBYTE **p;
   struct Window *win;
   win=(page==PGPREFS_SCREEN)?window:NULL;
   Setgadgetattrs(scrtypegad,win,NULL,CHOOSER_Active,pgp.screentype,TAG_END);
   Setgadgetattrs(scrpage,win,NULL,PAGE_Current,pgp.screentype,TAG_END);
   if(win) RethinkLayout(scrpagelayout,win,NULL,TRUE);
   Setgadgetattrs(scrngad,pgp.screentype==SCRTYPE_NAMED?win:NULL,NULL,
      STRINGA_TextVal,pgp.screenname,TAG_END);
   if(screenmodename) FREE(screenmodename);
   screenmodename=Prefsscreenmodename(pgp.screenmode,
      pgp.screenwidth,pgp.screenheight,pgp.screendepth);
   Setgadgetattrs(scrmgad,pgp.screentype==SCRTYPE_OWN?win:NULL,NULL,
      GA_Text,screenmodename,TAG_END);
   Setgadgetattrs(scrpgad,pgp.screentype==SCRTYPE_OWN?win:NULL,NULL,
      CHOOSER_Active,pgp.loadpalette,
      GA_Disabled,pgp.screendepth>8,
      TAG_END);
   win=(page==PGPREFS_PALETTE)?window:NULL;
   Setgadgetattrs(penlistgad,win,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&scrpenlist);
   Makescrpenlist(&scrpenlist);
   Setgadgetattrs(penlistgad,win,NULL,
      LISTBROWSER_Labels,&scrpenlist,
      GA_Disabled,!Ownscreen(),
      TAG_END);
   Setgadgetattrs(penpalgad,win,NULL,
      PALETTE_Colour,pgp.scrdrawpens[2],
      GA_Disabled,!Ownscreen(),
      TAG_END);
   Setgadgetattrs(scrpalgad,win,NULL,
      PALETTE_Colour,0,
      GA_Disabled,!Ownscreen(),
      TAG_END);
   Setgadgetattrs(poppalgad,win,NULL,GA_Disabled,!Ownscreen(),TAG_END);
   win=(page==PGPREFS_OPTIONS)?window:NULL;
   Setgadgetattrs(savegad,win,NULL,
      STRINGA_TextVal,pgp.savepath,
      STRINGA_DispPos,0,
      TAG_END);
   Setgadgetattrs(tempgad,win,NULL,
      STRINGA_TextVal,pgp.temppath,
      STRINGA_DispPos,0,
      TAG_END);
   Setgadgetattrs(ovlpgad,win,NULL,INTEGER_Number,pgp.overlap,TAG_END);
   Setgadgetattrs(cmdsgad,win,NULL,GA_Selected,pgp.commands,TAG_END);
   Setgadgetattrs(hlacgad,win,NULL,GA_Selected,pgp.hlautoclose,TAG_END);
   Setgadgetattrs(hlrqgad,win,NULL,GA_Selected,pgp.hlrequester,TAG_END);
   Setgadgetattrs(whacgad,win,NULL,GA_Selected,pgp.whautoclose,TAG_END);
   Setgadgetattrs(draggad,win,NULL,GA_Selected,pgp.clipdrag,TAG_END);
   Setgadgetattrs(savigad,win,NULL,GA_Selected,pgp.saveicons,TAG_END);
   Setgadgetattrs(aahlgad,win,NULL,GA_Selected,pgp.aahotlist,TAG_END);
   Setgadgetattrs(aawhgad,win,NULL,GA_Selected,pgp.aawinhis,TAG_END);
   Setgadgetattrs(aansgad,win,NULL,GA_Selected,pgp.aanetstat,TAG_END);
   Setgadgetattrs(cenrgad,win,NULL,GA_Selected,pgp.centerreq,TAG_END);
   win=(page==PGPREFS_PROGRAMS)?window:NULL;
   n=Getvalue(progtypegad,CHOOSER_Active);
   p=Prprgptr(n);
   Setgadgetattrs(progcmdgad,win,NULL,STRINGA_TextVal,p[0]?p[0]:NULLSTRING,TAG_END);
   if(n<3)
   {  Setgadgetattrs(progargsgad,win,NULL,STRINGA_TextVal,p[1]?p[1]:NULLSTRING,TAG_END);
   }
   Setgadgetattrs(consgad,win,NULL,
      STRINGA_TextVal,pgp.console,
      STRINGA_DispPos,0,
      TAG_END);
}

/*---------------------------------------------------------------------------*/

static void Localize(void)
{  tablabels[0]=CFGAWEBSTR(MSG_SET_CTPR_SCREEN);
   tablabels[1]=CFGAWEBSTR(MSG_SET_CTPR_PALETTE);
   tablabels[2]=CFGAWEBSTR(MSG_SET_CTPR_OPTIONS);
   tablabels[3]=CFGAWEBSTR(MSG_SET_CTPR_PROGRAMS);
   tablabels[4]=NULL;
   progtypelabels[0]=CFGAWEBSTR(MSG_SET_EDITOR_LABEL);
   progtypelabels[1]=CFGAWEBSTR(MSG_SET_VIEWER_LABEL);
   progtypelabels[2]=CFGAWEBSTR(MSG_SET_IMGVIEWER_LABEL);
   progtypelabels[3]=CFGAWEBSTR(MSG_SET_STARTUP_LABEL);
   progtypelabels[4]=CFGAWEBSTR(MSG_SET_SHUTDOWN_LABEL);
   progtypelabels[5]=NULL;
   proghelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_F);
   proghelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_N);
   proghelplabels[2]=NULL;
   imgvhelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_F);
   imgvhelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_N);
   imgvhelplabels[2]=CFGAWEBSTR(MSG_SET_HLP_M);
   imgvhelplabels[3]=NULL;
   conshelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_T);
   conshelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_N);
   conshelplabels[2]=NULL;
   screenlabels[0]=CFGAWEBSTR(MSG_SET_SCRT_DEFAULT);
   screenlabels[1]=CFGAWEBSTR(MSG_SET_SCRT_NAMED);
   screenlabels[2]=CFGAWEBSTR(MSG_SET_SCRT_OWN);
   screenlabels[3]=NULL;
   scrplabels[0]=CFGAWEBSTR(MSG_SET_LPAL_OFF);
   scrplabels[1]=CFGAWEBSTR(MSG_SET_LPAL_COLOURS);
   scrplabels[2]=CFGAWEBSTR(MSG_SET_LPAL_GRAY);
   scrplabels[3]=NULL;
/*
   col4labels[0]=CFGAWEBSTR(MSG_SET_4COL_8);
   col4labels[1]=CFGAWEBSTR(MSG_SET_4COL_4);
   col4labels[2]=NULL;
*/
   scrcolorpennames[0]=CFGAWEBSTR(MSG_SET_SCRPEN_TEXT);
   scrcolorpennames[1]=CFGAWEBSTR(MSG_SET_SCRPEN_SHINE);
   scrcolorpennames[2]=CFGAWEBSTR(MSG_SET_SCRPEN_SHADOW);
   scrcolorpennames[3]=CFGAWEBSTR(MSG_SET_SCRPEN_FILL);
   scrcolorpennames[4]=CFGAWEBSTR(MSG_SET_SCRPEN_FILLTEXT);
   scrcolorpennames[5]=CFGAWEBSTR(MSG_SET_SCRPEN_BACKGROUND);
   scrcolorpennames[6]=CFGAWEBSTR(MSG_SET_SCRPEN_HIGHLIGHTTEXT);
   scrcolorpennames[7]=CFGAWEBSTR(MSG_SET_SCRPEN_BARDETAIL);
   scrcolorpennames[8]=CFGAWEBSTR(MSG_SET_SCRPEN_BARBLOCK);
   scrcolorpennames[9]=NULL;
}

BOOL Openprogram(void)
{  BOOL ok=FALSE;
   if(window)
   {  WindowToFront(window);
      ActivateWindow(window);
      return FALSE;
   }
   (&tablist)->lh_Head = (struct Node *)&((&tablist)->lh_Tail);
   (&tablist)->lh_Tail = NULL;
   (&tablist)->lh_TailPred = (struct Node *)&((&tablist)->lh_Head);
   (&scrlist)->lh_Head = (struct Node *)&((&scrlist)->lh_Tail);
   (&scrlist)->lh_Tail = NULL;
   (&scrlist)->lh_TailPred = (struct Node *)&((&scrlist)->lh_Head);
   (&scrplist)->lh_Head = (struct Node *)&((&scrplist)->lh_Tail);
   (&scrplist)->lh_Tail = NULL;
   (&scrplist)->lh_TailPred = (struct Node *)&((&scrplist)->lh_Head);
   (&col4list)->lh_Head = (struct Node *)&((&col4list)->lh_Tail);
   (&col4list)->lh_Tail = NULL;
   (&col4list)->lh_TailPred = (struct Node *)&((&col4list)->lh_Head);
   (&scrpenlist)->lh_Head = (struct Node *)&((&scrpenlist)->lh_Tail);
   (&scrpenlist)->lh_Tail = NULL;
   (&scrpenlist)->lh_TailPred = (struct Node *)&((&scrpenlist)->lh_Head);
   (&progtypelist)->lh_Head = (struct Node *)&((&progtypelist)->lh_Tail);
   (&progtypelist)->lh_Tail = NULL;
   (&progtypelist)->lh_TailPred = (struct Node *)&((&progtypelist)->lh_Head);
   (&proghelplist)->lh_Head = (struct Node *)&((&proghelplist)->lh_Tail);
   (&proghelplist)->lh_Tail = NULL;
   (&proghelplist)->lh_TailPred = (struct Node *)&((&proghelplist)->lh_Head);
   (&imgvhelplist)->lh_Head = (struct Node *)&((&imgvhelplist)->lh_Tail);
   (&imgvhelplist)->lh_Tail = NULL;
   (&imgvhelplist)->lh_TailPred = (struct Node *)&((&imgvhelplist)->lh_Head);
   (&conshelplist)->lh_Head = (struct Node *)&((&conshelplist)->lh_Tail);
   (&conshelplist)->lh_Tail = NULL;
   (&conshelplist)->lh_TailPred = (struct Node *)&((&conshelplist)->lh_Head);
   if(!tablabels[0]) Localize();
   if(nport=CreateMsgPort())
   {  strcpy(prefsname,"ENV:" DEFAULTCFG);
      if(*configname) AddPart(prefsname,configname,64);
      AddPart(prefsname,"program",64);
      nreq.nr_Name=prefsname;
      nreq.nr_Port=nport;
      nreq.nr_Flags=NRF_SEND_MESSAGE;
      StartNotify(&nreq);
      Copyprogramprefs(&defprefs.program,&pgp);
      if(!Prefsconfiguseslegacyawebpath(NULL))
      {  Loadprogramprefs(&pgp,FALSE,NULL);
      }
      Copyprogramprefs(&pgp,&orgpgp);
      tested=FALSE;
      Makeclicktablist(&tablist,tablabels);
      Makechooserlist(&scrlist,screenlabels,FALSE);
      Makechooserlist(&scrplist,scrplabels,FALSE);
/*
      Makechooserlist(&col4list,col4labels,FALSE);
*/
      Makescrpenlist(&scrpenlist);
      Makechooserlist(&progtypelist,progtypelabels,FALSE);
      Makechooserlist(&proghelplist,proghelplabels,TRUE);
      Makechooserlist(&imgvhelplist,imgvhelplabels,TRUE);
      Makechooserlist(&conshelplist,conshelplabels,TRUE);
      winobject=WindowObject,
            WA_Title,CFGAWEBSTR(MSG_SET_REQTITLE_PROGRAMWINDOW),
            WA_Left,setprefs.prgx,
            WA_Top,setprefs.prgy,
            WA_InnerWidth,setprefs.prgw,
            WA_InnerHeight,setprefs.prgh,
            WA_AutoAdjust,TRUE,
            WA_CloseGadget,TRUE,
            WA_DragBar,TRUE,
            WA_DepthGadget,TRUE,
            WA_SizeGadget,TRUE,
            WA_Activate,TRUE,
            WA_IDCMP,IDCMP_RAWKEY|IDCMP_MENUPICK,
            WA_SimpleRefresh,TRUE,
            WA_PubScreen,pubscreen,
            setprefs.prgw?TAG_IGNORE:WINDOW_Position,WPOS_CENTERSCREEN,
            WINDOW_Layout,toplayout=VLayoutObject,
               LAYOUT_DeferLayout,TRUE,
               LAYOUT_TopSpacing,2,
               StartMember,tabgad=ClickTabObject,
                  CLICKTAB_Labels,&tablist,
                  CLICKTAB_Current,0,
                  GA_ID,PGID_TABS,
                  GA_RelVerify,TRUE,
               EndMember,
               CHILD_WeightedHeight,0,
               StartMember,pagelayout=HLayoutObject,
                  LAYOUT_SpaceOuter,TRUE,
                  StartMember,pagegad=PageObject,
                     PAGE_Add,Makescreenpage(),
                     PAGE_Add,Makepalettepage(),
                     PAGE_Add,Makeoptionspage(),
                     PAGE_Add,Makeprogramspage(),
                  EndMember,
               EndMember,
               //--------------------------------- Save, Use, Cancel
               StartMember,HLayoutObject,
                  LAYOUT_BevelStyle,BVS_SBAR_VERT,
                  LAYOUT_SpaceOuter,TRUE,
                  StartMember,ButtonObject,
                     GA_Text,CFGAWEBSTR(MSG_SET_SAVE),
                     GA_ID,PGID_SAVE,
                     GA_RelVerify,TRUE,
                  EndMember,
                  StartMember,ButtonObject,
                     GA_Text,CFGAWEBSTR(MSG_SET_USE),
                     GA_ID,PGID_USE,
                     GA_RelVerify,TRUE,
                  EndMember,
                  StartMember,ButtonObject,
                     GA_Text,CFGAWEBSTR(MSG_SET_TEST),
                     GA_ID,PGID_TEST,
                     GA_RelVerify,TRUE,
                  EndMember,
                  StartMember,ButtonObject,
                     GA_Text,CFGAWEBSTR(MSG_SET_CANCEL),
                     GA_ID,PGID_CANCEL,
                     GA_RelVerify,TRUE,
                  EndMember,
               EndMember,
               CHILD_WeightedHeight,0,
            End,
         EndWindow;
      if(winobject)
      {  if(window=(struct Window *)RA_OpenWindow(winobject))
         {  if((menubar=CreateMenus(menubase,
                  GTMN_FrontPen,drawinfo->dri_Pens[BARDETAILPEN],
                  TAG_END))
             && LayoutMenus(menubar,visualinfo,TAG_END))
               SetMenuStrip(window,menubar);
            GetAttr(WINDOW_SigMask,winobject,&prmask);
            prmask|=1<<nport->mp_SigBit;
            SetAttrs(scrmgad,GA_Text,screenmodename,TAG_END);
            if(pgp.screentype==SCRTYPE_OWN)
               RefreshGList(scrmgad,window,NULL,1);
            ok=TRUE;
         }
      }
   }
   if(!ok) Closeprogram();
   return ok;
}

/* returns TRUE is window remains open, FALSE if window should close */
BOOL Processprogram(void)
{  BOOL done=FALSE,changed=FALSE;
   WORD code;
   ULONG result;
   struct MenuItem *item;
   long menuid;
   struct Message *msg;
   UBYTE *path;
   while(!done
    && (result=RA_HandleInput(winobject,&code))!=WMHI_LASTMSG)
   {  switch(result&WMHI_CLASSMASK)
      {  case WMHI_CLOSEWINDOW:
            done=TRUE;
            break;
         case WMHI_RAWKEY:
            switch(result&WMHI_GADGETMASK)
            {  case 0x45:  /* esc */
                  done=TRUE;
                  break;
               case 0x4c:  /* up */
                  Docursor(-1);
                  break;
               case 0x4d:  /* down */
                  Docursor(1);
                  break;
            }
            break;
         case WMHI_GADGETUP:
            switch(result&WMHI_GADGETMASK)
            {  case PGID_TABS:
                  Setgadgetattrs(pagegad,window,NULL,
                     PAGE_Current,code,TAG_END);
                  RethinkLayout(pagelayout,window,NULL,TRUE);
                  break;
               case PGID_SCRTYPE:
                  Setgadgetattrs(scrpage,window,NULL,
                     PAGE_Current,code,TAG_END);
                  RethinkLayout(scrpagelayout,window,NULL,TRUE);
                  break;
               case PGID_SCRPOP:
                  Doscrpop();
                  break;
/*
               case PGID_FOURCOLORS:
                  Docol4();
                  break;
*/
               case PGID_SCRPENLIST:
                  Doscrpenlist(Getvalue(penlistgad,LISTBROWSER_Selected));
                  break;
               case PGID_SCRPENPAL:
                  Doscrpenpal(code);
                  break;
               case PGID_SCRPOPCOLOR:
                  Doscrpopcolor();
                  break;
               case PGID_PRGPOPSAVE:
                  Doprgpopsave();
                  break;
               case PGID_POPTEMP:
                  Doprgpoptemp();
                  break;
               case PGID_PROGTYPE:
                  Doprprgtp(code);
                  break;
               case PGID_PROGCMD:
                  Doprprgcmd();
                  break;
               case PGID_PROGPOPC:
                  Doprprgpop();
                  break;
               case PGID_PROGARGS:
                  Doprprgargs();
                  break;
               case PGID_SAVE:
                  Copydata();
                  endmode=PGID_SAVE;
                  tested=FALSE;
                  done=TRUE;
                  break;
               case PGID_USE:
                  Copydata();
                  endmode=PGID_USE;
                  tested=FALSE;
                  done=TRUE;
                  break;
               case PGID_TEST:
                  Copydata();
                  EndNotify(&nreq);
                  Saveprogramprefs(&pgp,FALSE,NULL);
                  StartNotify(&nreq);
                  tested=TRUE;
                  break;
               case PGID_CANCEL:
                  done=TRUE;
                  break;
            }
            break;
         case WMHI_MENUPICK:
            if(!(item=ItemAddress(menubar,code))) break;
            menuid=(long)GTMENUITEM_USERDATA(item);
            switch(menuid)
            {  case MID_OPEN:
                  if(path=Filereq(winobject,window,toplayout,
                     CFGAWEBSTR(MSG_SET_REQTITLE_OPENPROGRAM),"program",FALSE))
                  {  Disposeprogramprefs(&pgp);
                     Copyprogramprefs(&defprefs.program,&pgp);
                     if(!Prefsconfiguseslegacyawebpath(path))
                     {  Loadprogramprefs(&pgp,FALSE,path);
                     }
                     Setdata();
                     FREE(path);
                  }
                  break;
               case MID_SAVEAS:
                  if(path=Filereq(winobject,window,toplayout,
                     CFGAWEBSTR(MSG_SET_REQTITLE_SAVEPROGRAM),"program",TRUE))
                  {  Copydata();
                     Saveprogramprefs(&pgp,FALSE,path);
                     FREE(path);
                  }
                  break;
               case MID_QUIT:
                  done=TRUE;
                  break;
               case MID_DEFAULTS:
                  Disposeprogramprefs(&pgp);
                  Copyprogramprefs(&defprefs.program,&pgp);
                  Setdata();
                  break;
               case MID_LASTSAVED:
                  Disposeprogramprefs(&pgp);
                  Copyprogramprefs(&defprefs.program,&pgp);
                  if(!Prefsconfiguseslegacyawebpath(NULL))
                  {  Loadprogramprefs(&pgp,FALSE,NULL);
                  }
                  Setdata();
                  break;
               case MID_RESTORE:
                  Disposeprogramprefs(&pgp);
                  Copyprogramprefs(&orgpgp,&pgp);
                  Setdata();
                  break;
               case MID_BROWSER:
                  cfgcommand|=CFGCLASS_BROWSER;
                  break;
               case MID_PROGRAM:
                  cfgcommand|=CFGCLASS_PROGRAM;
                  break;
               case MID_GUI:
                  cfgcommand|=CFGCLASS_GUI;
                  break;
               case MID_NETWORK:
                  cfgcommand|=CFGCLASS_NETWORK;
                  break;
               case MID_CLASSACT:
                  cfgcommand|=CFGCLASS_CLASSACT;
                  break;
               case MID_SNAPSHOT:
                  Dimensions(window,&setprefs.prgx);
                  cfgcommand|=CFGCLASS_SNAPSHOT;
                  break;
            }
            break;
      }
   }
   if(!done)
   {  while(msg=GetMsg(nport))
      {  changed=TRUE;
         ReplyMsg(msg);
      }
      if(changed)
      {  Disposeprogramprefs(&pgp);
         Copyprogramprefs(&defprefs.program,&pgp);
         if(!Prefsconfiguseslegacyawebpath(NULL))
         {  Loadprogramprefs(&pgp,FALSE,NULL);
         }
         Setdata();
         Disposeprogramprefs(&orgpgp);
         Copyprogramprefs(&pgp,&orgpgp);
         tested=FALSE;
      }
   }
   if(done)
   {  if(tested)
      {  Disposeprogramprefs(&pgp);
         Copyprogramprefs(&orgpgp,&pgp);
         if(!endmode) endmode=PGID_USE;
      }
      Closeprogram();
   }
   return (BOOL)!done;
}

void Closeprogram(void)
{  prmask=0;
   if(window)
   {  ClearMenuStrip(window);
      Dimensions(window,&setprefs.prgx);
   }
   if(winobject) DisposeObject(winobject);winobject=NULL;window=NULL;
   if(menubar) FreeMenus(menubar);menubar=NULL;
   if(endmode)
   {  if(endmode==PGID_SAVE) Saveprogramprefs(&pgp,TRUE,NULL);
      Saveprogramprefs(&pgp,FALSE,NULL);
   }
   endmode=0;
   Freeclicktablist(&tablist);
   Freechooserlist(&scrlist);
   Freechooserlist(&scrplist);
/*
   Freechooserlist(&col4list);
*/
   Freebrowserlist(&scrpenlist);
   Freechooserlist(&progtypelist);
   Freechooserlist(&proghelplist);
   Freechooserlist(&imgvhelplist);
   Freechooserlist(&conshelplist);
   if(screenmodename) FREE(screenmodename);screenmodename=NULL;
   Disposeprogramprefs(&pgp);
   Disposeprogramprefs(&orgpgp);
   if(nreq.nr_Name) EndNotify(&nreq);
   memset(&nreq,0,sizeof(nreq));
   if(nport) DeleteMsgPort(nport);nport=NULL;
}
