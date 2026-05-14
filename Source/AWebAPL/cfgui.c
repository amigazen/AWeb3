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

/* cfgui.c - AWebCfg user interface window */

#include "awebcfg.h"
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <images/bevel.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <classes/window.h>
#include <proto/intuition.h>

ULONG uimask=0;

static struct Guiprefs uip,orguip;
static BOOL tested=FALSE;

enum UIPREFS_PAGES
{  UIPREFS_OPTIONS,UIPREFS_MENU,UIPREFS_BUTTONS,UIPREFS_POPUP,UIPREFS_KEYBD,
   UIPREFS_NAVS,
};

static struct List tablist,
   menulist,menutypelist,menuhelplist,
   butlist,buthelplist,
   puptypelist,puplist,pupihelplist,puplhelplist,pupfhelplist,
   kbdlist,kbdhelplist,
   navlist,navhelplist;

static struct Menuentry *menuentry;
static long menuselected;
static BOOL menuvalid;
static struct Userbutton *userbutton;
static short popuptype;
static struct Popupitem *popupitem;
static USHORT kbdkey;
static short navnr;

static UBYTE *tablabels[7];
static UBYTE *buthelplabels[6];
static UBYTE *puptypelabels[4];
static UBYTE *pupihelplabels[2];
static UBYTE *puplhelplabels[4];
static UBYTE *pupfhelplabels[3];
static UBYTE *kbdhelplabels[6];
static UBYTE *menutypelabels[5];
static UBYTE *menucolumnlabels[5];
static UBYTE *menuhelplabels[6];
static UBYTE *navlabels[10];
static UBYTE *navhelplabels[6];

static void *winobject=NULL;
static struct Gadget *toplayout,*tabgad,*pagelayout,*pagegad;
static struct Gadget *pushiftgad,*pualtgad,*puctrlgad,*pumidbutgad,*purbutgad,
   *ubusgad,*navsgad,
   *menulistgad,*menuvalidgad,*menuupgad,*menudowngad,*menudelgad,*menutypegad,
   *menutitlegad,*menuscutgad,*menucmdgad,*menupopgad,
   *butlistgad,*butsortgad,*butupgad,*butdowngad,*butdelgad,
   *butlabelgad,*butcmdgad,*butpopgad,
   *puptypegad,*puplistgad,*pupsortgad,*pupupgad,*pupdowngad,*pupdelgad,
   *puptitlegad,*pupcmdgad,*puppopgad,*pupinmgad,*pupnomgad,
   *kbdlistgad,*kbdkeygad,*kbdcmdgad,*kbdpopgad,
   *navlistgad,*navbutgad,*navcmdgad;

static UBYTE prefsname[64];
static struct MsgPort *nport;
static struct Window *window=NULL;
static struct Menu *menubar;
static struct NotifyRequest nreq;

static BOOL control=FALSE;
static struct Hook idcmphook;

enum GADGET_IDS
{  PGID_TABS=1,
   PGID_MENULIST,PGID_MENUUP,PGID_MENUDOWN,PGID_MENUADD,PGID_MENUDEL,
   PGID_MENUTYPE,PGID_MENUTITLE,PGID_MENUSCUT,PGID_MENUCMD,
   PGID_BUTLIST,PGID_BUTSORT,PGID_BUTUP,PGID_BUTDOWN,PGID_BUTADD,PGID_BUTDEL,
   PGID_BUTLABEL,PGID_BUTCMD,
   PGID_PUPTYPE,PGID_PUPLIST,PGID_PUPSORT,PGID_PUPUP,PGID_PUPDOWN,PGID_PUPADD,PGID_PUPDEL,
   PGID_PUPTITLE,PGID_PUPCMD,PGID_PUPINM,PGID_PUPNOM,
   PGID_KBDLIST,PGID_KBDCMD,
   PGID_NAVLIST,PGID_NAVCMD,
   PGID_SAVE,PGID_USE,PGID_TEST,PGID_CANCEL,
};

static USHORT endmode;  /* gadget id that caused end (save, use) */

static BOOL menuhastitle[4]={ TRUE,TRUE,TRUE,FALSE };
static BOOL menuhasdetails[4]={ FALSE,TRUE,TRUE,FALSE };

/*---------------------------------------------------------------------------*/

static void Validatemenus(void)
{  struct Menuentry *me;
   BOOL hasmenu=FALSE,hasitem=FALSE;
   menuvalid=TRUE;
   for(me=uip.menus.first;menuvalid && me->next;me=me->next)
   {  switch(me->type)
      {  case AMENU_MENU:
            hasmenu=TRUE;
            hasitem=FALSE;
            break;
         case AMENU_ITEM:
            menuvalid=hasmenu;
            hasitem=TRUE;
            break;
         case AMENU_SUB:
            menuvalid=hasitem;
            break;
         case AMENU_SEPARATOR:
            menuvalid=hasmenu || hasitem;
            break;
      }
   }
}

/*---------------------------------------------------------------------------*/

static void *Makeoptionspage(void)
{  return
   VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      LAYOUT_FixedVert,FALSE,
      StartMember,HLayoutObject,
         StartMember,VLayoutObject,
            StartMember,navsgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_GUIOPT_SHOWNAV),
               GA_Selected,uip.shownav,
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,ubusgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_GUIOPT_SHOWBUTTONS),
               GA_Selected,uip.showbuttons,
            EndMember,
            CHILD_WeightedWidth,0,
         EndMember,
         CHILD_WeightedHeight,0,
         StartMember,VLayoutObject,
            LAYOUT_BevelStyle,BVS_GROUP,
            LAYOUT_Label,CFGAWEBSTR(MSG_SET_BROPT_POPUPLABEL),
            LAYOUT_SpaceOuter,TRUE,
            StartMember,pushiftgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_POPUPSHIFT),
               GA_Selected,(uip.popupkey&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT))!=0,
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,pualtgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_POPUPALT),
               GA_Selected,(uip.popupkey&(IEQUALIFIER_LALT|IEQUALIFIER_RALT))!=0,
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,puctrlgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_POPUPCONTROL),
               GA_Selected,(uip.popupkey&IEQUALIFIER_CONTROL)!=0,
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,purbutgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_POPUPRBUTTON),
               GA_Selected,(uip.popupkey&IEQUALIFIER_RBUTTON)!=0,
            EndMember,
            CHILD_WeightedWidth,0,
            StartMember,pumidbutgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_POPUPMIDBUTTON),
               GA_Selected,(uip.popupkey&IEQUALIFIER_MIDBUTTON)!=0,
            EndMember,
            CHILD_WeightedWidth,0,
         EndMember,
      EndMember,
   End;
}

static struct ColumnInfo menucolumns[]=
{  0,NULL,0,
   0,NULL,0,
   0,NULL,0,
   0,NULL,0,
   0,NULL,0,
   -1,NULL,0
};

void Makemenulist(struct List *list)
{  struct Menuentry *me;
   struct Node *node;
   UBYTE *sep="----------";
   UBYTE *col0;
   BOOL pens,scut,hassub;
   for(me=uip.menus.first;me->next;me=me->next)
   {  pens=me->type==AMENU_MENU;
      scut=me->scut[0]!=0;
      hassub=me->type==AMENU_ITEM && me->next->next && me->next->type==AMENU_SUB;
      if(me->type==AMENU_SEPARATOR && me->next->next && me->next->type==AMENU_SUB)
      {  col0=menucolumnlabels[AMENU_SUB];
      }
      else
      {  col0=menucolumnlabels[me->type];
      }
      if(node=AllocListBrowserNode(5,
         LBNA_Flags,LBFLG_CUSTOMPENS,
         LBNA_Column,0,
            LBNCA_Text,col0,
            LBNCA_Justification,LCJ_RIGHT,
            pens?LBNCA_FGPen:TAG_IGNORE,drawinfo->dri_Pens[BARBLOCKPEN],
            pens?LBNCA_BGPen:TAG_IGNORE,drawinfo->dri_Pens[BARDETAILPEN],
         LBNA_Column,1,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,me->type==AMENU_SEPARATOR?sep:(me->title?me->title:NULLSTRING),
            LBNCA_FGPen,drawinfo->dri_Pens[pens?BARBLOCKPEN:BARDETAILPEN],
            LBNCA_BGPen,drawinfo->dri_Pens[pens?BARDETAILPEN:BARBLOCKPEN],
         LBNA_Column,2,
            (scut && !hassub)?LBNCA_Image:TAG_IGNORE,amigaimg,
            (scut && !hassub)?TAG_IGNORE:LBNCA_Text,"",
            LBNCA_FGPen,drawinfo->dri_Pens[pens?BARBLOCKPEN:BARDETAILPEN],
            LBNCA_BGPen,drawinfo->dri_Pens[pens?BARDETAILPEN:BARBLOCKPEN],
         LBNA_Column,3,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,hassub?(UBYTE *)"�":me->scut,
            LBNCA_FGPen,drawinfo->dri_Pens[pens?BARBLOCKPEN:BARDETAILPEN],
            LBNCA_BGPen,drawinfo->dri_Pens[pens?BARDETAILPEN:BARBLOCKPEN],
         LBNA_Column,4,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,me->cmd?me->cmd:NULLSTRING,
            pens?LBNCA_FGPen:TAG_IGNORE,drawinfo->dri_Pens[BARBLOCKPEN],
            pens?LBNCA_BGPen:TAG_IGNORE,drawinfo->dri_Pens[BARDETAILPEN],
         TAG_END))
         AddTail(list,node);
   }
}

static void *Makemenupage(void)
{  BOOL hasitem=FALSE;
   menuselected=-1;
   menuentry=uip.menus.first;
   if(menuentry->next)
   {  hasitem=TRUE;
      menuselected=0;
   }
   Validatemenus();
   return
   VLayoutObject,
      StartMember,menulistgad=ListBrowserObject,
         GA_ID,PGID_MENULIST,
         GA_RelVerify,TRUE,
         LISTBROWSER_ShowSelected,TRUE,
         LISTBROWSER_AutoFit,TRUE,
         LISTBROWSER_ColumnInfo,menucolumns,
         LISTBROWSER_Separators,FALSE,
         LISTBROWSER_Labels,&menulist,
         LISTBROWSER_Selected,0,
      EndMember,
      StartMember,HLayoutObject,
         StartMember,menuvalidgad=ButtonObject,
            GA_ReadOnly,TRUE,
            GA_Text,menuvalid?NULLSTRING:CFGAWEBSTR(MSG_SET_MENUS_INVALID),
            BUTTON_DomainString,CFGAWEBSTR(MSG_SET_MENUS_INVALID),
            BUTTON_TextPen,drawinfo->dri_Pens[BARDETAILPEN],
            BUTTON_BackgroundPen,drawinfo->dri_Pens[menuvalid?BACKGROUNDPEN:BARBLOCKPEN],
            BUTTON_SoftStyle,FSF_BOLD,
         EndMember,
         StartMember,menuupgad=ButtonObject,
            GA_ID,PGID_MENUUP,
            GA_RelVerify,TRUE,
            BUTTON_AutoButton,BAG_UPARROW,
            GA_Disabled,TRUE,
         EndMember,
         CHILD_WeightedWidth,0,
         StartMember,menudowngad=ButtonObject,
            GA_ID,PGID_MENUDOWN,
            GA_RelVerify,TRUE,
            BUTTON_AutoButton,BAG_DNARROW,
            GA_Disabled,!hasitem,
         EndMember,
         CHILD_WeightedWidth,0,
         StartMember,ButtonObject,
            GA_ID,PGID_MENUADD,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_AREXX_ADD),
         EndMember,
         StartMember,menudelgad=ButtonObject,
            GA_ID,PGID_MENUDEL,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_AREXX_DEL),
            GA_Disabled,!hasitem,
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         StartMember,menutypegad=ChooserObject,
            GA_ID,PGID_MENUTYPE,
            GA_RelVerify,TRUE,
            CHOOSER_PopUp,TRUE,
            CHOOSER_Labels,&menutypelist,
            CHOOSER_Active,hasitem?menuentry->type:0,
            GA_Disabled,!hasitem,
         EndMember,
         CHILD_WeightedWidth,0,
         MemberLabel(CFGAWEBSTR(MSG_SET_MENUS_TYPE)),
         StartMember,menutitlegad=StringObject,
            GA_ID,PGID_MENUTITLE,
            GA_TabCycle,TRUE,
            GA_RelVerify,TRUE,
            STRINGA_TextVal,hasitem?menuentry->title:NULLSTRING,
            STRINGA_MaxChars,30,
            GA_Disabled,!(hasitem && menuhastitle[menuentry->type]),
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_AREXX_TITLE)),
         StartMember,menuscutgad=StringObject,
            GA_ID,PGID_MENUSCUT,
            GA_TabCycle,TRUE,
            GA_RelVerify,TRUE,
            STRINGA_TextVal,hasitem?menuentry->scut:NULLSTRING,
            STRINGA_MaxChars,3,
            GA_Disabled,!(hasitem && menuhasdetails[menuentry->type]),
         EndMember,
         CHILD_WeightedWidth,0,
         CHILD_Label,LabelObject,
            LABEL_Image,amigaimg,
         LabelEnd,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,menucmdgad=StringObject,
            GA_TabCycle,TRUE,
            GA_ID,PGID_MENUCMD,
            GA_RelVerify,TRUE,
            STRINGA_TextVal,hasitem?menuentry->cmd:NULLSTRING,
            STRINGA_MaxChars,127,
            GA_Disabled,!(hasitem && menuhasdetails[menuentry->type]),
         EndMember,
         StartMember,menupopgad=ChooserObject,
            CHOOSER_DropDown,TRUE,
            CHOOSER_Labels,&menuhelplist,
            CHOOSER_AutoFit,TRUE,
            GA_Disabled,!(hasitem && menuhasdetails[menuentry->type]),
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      MemberLabel(CFGAWEBSTR(MSG_SET_MENUS_COMMAND)),
#ifdef DEMOVERSION
      GA_Disabled,TRUE,
#endif
   End;
}
      
static struct ColumnInfo butcolumns[]=
{  0,NULL,0,
   0,NULL,0,
   -1,NULL,0
};

void Makebutlist(struct List *list)
{  struct Userbutton *ub;
   struct Node *node;
   for(ub=uip.buttons.first;ub->next;ub=ub->next)
   {  if(node=AllocListBrowserNode(3,
         LBNA_Column,0,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,ub->label,
         LBNA_Column,1,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,ub->cmd,
         TAG_END))
         AddTail(list,node);
   }
}

static void *Makebuttonspage(void)
{  BOOL hasitem=FALSE;
   userbutton=uip.buttons.first;
   if(userbutton->next) hasitem=TRUE;
   return
   VLayoutObject,
      StartMember,butlistgad=ListBrowserObject,
         GA_ID,PGID_BUTLIST,
         GA_RelVerify,TRUE,
         LISTBROWSER_ShowSelected,TRUE,
         LISTBROWSER_AutoFit,TRUE,
         LISTBROWSER_ColumnInfo,butcolumns,
         LISTBROWSER_Labels,&butlist,
         LISTBROWSER_Selected,0,
      EndMember,
      StartMember,HLayoutObject,
         StartMember,butsortgad=ButtonObject,
            GA_ID,PGID_BUTSORT,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_BUTTON_SORT),
            GA_Disabled,!hasitem,
         EndMember,
         StartMember,butupgad=ButtonObject,
            GA_ID,PGID_BUTUP,
            GA_RelVerify,TRUE,
            BUTTON_AutoButton,BAG_UPARROW,
            GA_Disabled,TRUE,
         EndMember,
         CHILD_WeightedWidth,0,
         StartMember,butdowngad=ButtonObject,
            GA_ID,PGID_BUTDOWN,
            GA_RelVerify,TRUE,
            BUTTON_AutoButton,BAG_DNARROW,
            GA_Disabled,!(hasitem && userbutton->next->next),
         EndMember,
         CHILD_WeightedWidth,0,
         StartMember,ButtonObject,
            GA_ID,PGID_BUTADD,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_BUTTON_ADD),
         EndMember,
         StartMember,butdelgad=ButtonObject,
            GA_ID,PGID_BUTDEL,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_BUTTON_DEL),
            GA_Disabled,!hasitem,
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,butlabelgad=StringObject,
         GA_ID,PGID_BUTLABEL,
         GA_TabCycle,TRUE,
         GA_RelVerify,TRUE,
         STRINGA_TextVal,hasitem?userbutton->label:NULLSTRING,
         STRINGA_MaxChars,30,
         GA_Disabled,!hasitem,
      EndMember,
      CHILD_WeightedHeight,0,
      MemberLabel(CFGAWEBSTR(MSG_SET_BUTTON_LABEL)),
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,butcmdgad=StringObject,
            GA_TabCycle,TRUE,
            GA_ID,PGID_BUTCMD,
            GA_RelVerify,TRUE,
            STRINGA_TextVal,hasitem?userbutton->cmd:NULLSTRING,
            STRINGA_MaxChars,127,
            GA_Disabled,!hasitem,
         EndMember,
         StartMember,butpopgad=ChooserObject,
            CHOOSER_DropDown,TRUE,
            CHOOSER_Labels,&buthelplist,
            CHOOSER_AutoFit,TRUE,
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      MemberLabel(CFGAWEBSTR(MSG_SET_BUTTON_CMD)),
   End;
}

static struct ColumnInfo pupcolumns[]=
{  0,NULL,0,
   0,NULL,0,
   -1,NULL,0
};

void Makepuplist(struct List *list,short type)
{  struct Popupitem *pi;
   struct Node *node;
   for(pi=uip.popupmenu[type].first;pi->next;pi=pi->next)
   {  if(node=AllocListBrowserNode(3,
         LBNA_Column,0,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,pi->title,
         LBNA_Column,1,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,pi->cmd,
         TAG_END))
         AddTail(list,node);
   }
}

static void *Makepopuppage(void)
{  BOOL hasitem=FALSE;
   popuptype=PUPT_IMAGE;
   popupitem=uip.popupmenu[popuptype].first;
   if(popupitem->next) hasitem=TRUE;
   return
   VLayoutObject,
      StartMember,HLayoutObject,
         StartMember,puptypegad=ChooserObject,
            GA_ID,PGID_PUPTYPE,
            GA_RelVerify,TRUE,
            CHOOSER_Labels,&puptypelist,
            CHOOSER_Active,popuptype,
            CHOOSER_PopUp,TRUE,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_POPUP_TYPE)),
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,puplistgad=ListBrowserObject,
         GA_ID,PGID_PUPLIST,
         GA_RelVerify,TRUE,
         LISTBROWSER_ShowSelected,TRUE,
         LISTBROWSER_AutoFit,TRUE,
         LISTBROWSER_ColumnInfo,pupcolumns,
         LISTBROWSER_Labels,&puplist,
         LISTBROWSER_Selected,0,
      EndMember,
      StartMember,HLayoutObject,
         StartMember,pupsortgad=ButtonObject,
            GA_ID,PGID_PUPSORT,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_POPUP_SORT),
            GA_Disabled,!hasitem,
         EndMember,
         StartMember,pupupgad=ButtonObject,
            GA_ID,PGID_PUPUP,
            GA_RelVerify,TRUE,
            BUTTON_AutoButton,BAG_UPARROW,
            GA_Disabled,TRUE,
         EndMember,
         CHILD_WeightedWidth,0,
         StartMember,pupdowngad=ButtonObject,
            GA_ID,PGID_PUPDOWN,
            GA_RelVerify,TRUE,
            BUTTON_AutoButton,BAG_DNARROW,
            GA_Disabled,!(hasitem && popupitem->next->next),
         EndMember,
         CHILD_WeightedWidth,0,
         StartMember,ButtonObject,
            GA_ID,PGID_PUPADD,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_POPUP_ADD),
         EndMember,
         StartMember,pupdelgad=ButtonObject,
            GA_ID,PGID_PUPDEL,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_POPUP_DEL),
            GA_Disabled,!hasitem,
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,puptitlegad=StringObject,
         GA_ID,PGID_PUPTITLE,
         GA_TabCycle,TRUE,
         GA_RelVerify,TRUE,
         STRINGA_TextVal,hasitem?popupitem->title:NULLSTRING,
         STRINGA_MaxChars,30,
         GA_Disabled,!hasitem,
      EndMember,
      CHILD_WeightedHeight,0,
      MemberLabel(CFGAWEBSTR(MSG_SET_POPUP_TITLE)),
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,pupcmdgad=StringObject,
            GA_TabCycle,TRUE,
            GA_ID,PGID_PUPCMD,
            GA_RelVerify,TRUE,
            STRINGA_TextVal,hasitem?popupitem->cmd:NULLSTRING,
            STRINGA_MaxChars,127,
            GA_Disabled,!hasitem,
         EndMember,
         StartMember,puppopgad=ChooserObject,
            CHOOSER_DropDown,TRUE,
            CHOOSER_Labels,&pupihelplist,
            CHOOSER_AutoFit,TRUE,
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      MemberLabel(CFGAWEBSTR(MSG_SET_POPUP_CMD)),
      StartMember,HLayoutObject,
         StartMember,HLayoutObject,
            StartMember,pupinmgad=CheckBoxObject,
               GA_ID,PGID_PUPINM,
               GA_RelVerify,TRUE,
               GA_Text,CFGAWEBSTR(MSG_SET_POPUP_INMEM),
               GA_Selected,hasitem?BOOLVAL(popupitem->flags&PUPF_INMEM):FALSE,
               GA_Disabled,!hasitem,
            EndMember,
            CHILD_WeightedWidth,0,
            MemberLabel(CFGAWEBSTR(MSG_SET_POPUP_SHOWIF)),
         EndMember,
         StartMember,pupnomgad=CheckBoxObject,
            GA_ID,PGID_PUPNOM,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_POPUP_NOTINMEM),
            GA_Selected,hasitem?BOOLVAL(popupitem->flags&PUPF_NOTINMEM):FALSE,
            GA_Disabled,!hasitem,
         EndMember,
         CHILD_WeightedWidth,0,
      EndMember,
      CHILD_WeightedHeight,0,
      CHILD_WeightedWidth,0,
   End;
}

static struct ColumnInfo kbdcolumns[]=
{  0,NULL,0,
   0,NULL,0,
   -1,NULL,0
};

static void Addkbdnode(struct List *list,UBYTE *label,USHORT key)
{  struct Node *node;
   struct Userkey *uk=Finduserkey(&uip.keys,key);
   if(node=AllocListBrowserNode(3,
      LBNA_UserData,key,
      LBNA_Column,0,
         LBNCA_CopyText,TRUE,
         LBNCA_Text,label,
      LBNA_Column,1,
         LBNCA_CopyText,TRUE,
         LBNCA_Text,uk?uk->cmd:NULLSTRING,
      TAG_END))
   {  AddTail(list,node);
   }
}

struct Keydef
{  USHORT key;
   UBYTE *name;
};

static struct Keydef keydef[39]=
{  { 0x45,"Esc", },
   { 0x50,"F1" },
   { 0x51,"F2" },
   { 0x52,"F3" },
   { 0x53,"F4" },
   { 0x54,"F5" },
   { 0x55,"F6" },
   { 0x56,"F7" },
   { 0x57,"F8" },
   { 0x58,"F9" },
   { 0x59,"F10" },
   { 0x41,NULL }, /* [11] BS */
   { 0x46,"Del" },
   { 0x5f,"Help" },
   { 0x42,NULL }, /* [14] Tab */
   { 0x44,NULL }, /* [15] Enter */
   { 0x40,NULL }, /* [16] Space */
   { 0x4c,NULL }, /* [17] Up */
   { 0x4f,NULL }, /* [18] Left */
   { 0x4e,NULL }, /* [19] Right */
   { 0x4d,NULL }, /* [20] Down */
   { 0x5a,"(" },  /* [21-38] Num xxx */
   { 0x5b,")" },
   { 0x5c,"/" },
   { 0x5d,"*" },
   { 0x4a,"-" },
   { 0x5e,"+" },
   { 0x43,NULL }, /* [27] Num Enter */
   { 0x3c,"." },
   { 0x0f,"0" },
   { 0x1d,"1" },
   { 0x1e,"2" },
   { 0x1f,"3" },
   { 0x2d,"4" },
   { 0x2e,"5" },
   { 0x2f,"6" },
   { 0x3d,"7" },
   { 0x3e,"8" },
   { 0x3f,"9" },
};
static struct Keydef wheeldef[2]=
{  { 0x7a,NULL }, /* [0] Wheel up */
   { 0x7b,NULL }, /* [1] Wheel down */
};

void Makekbdlist(struct List *list)
{  UBYTE *buf;
   UBYTE *shift=CFGAWEBSTR(MSG_SET_KEYS_SHIFT);
   UBYTE *num=CFGAWEBSTR(MSG_SET_KEYS_NUM);
   short i,l,lshift,lnum,lkey=0;
   lshift=strlen(shift);
   lnum=strlen(num);
   if(!keydef[11].name)
   {  keydef[11].name=CFGAWEBSTR(MSG_SET_KEYS_BS);
      keydef[14].name=CFGAWEBSTR(MSG_SET_KEYS_TAB);
      keydef[15].name=CFGAWEBSTR(MSG_SET_KEYS_ENTER);
      keydef[16].name=CFGAWEBSTR(MSG_SET_KEYS_SPACE);
      keydef[17].name=CFGAWEBSTR(MSG_SET_KEYS_UP);
      keydef[18].name=CFGAWEBSTR(MSG_SET_KEYS_LEFT);
      keydef[19].name=CFGAWEBSTR(MSG_SET_KEYS_RIGHT);
      keydef[20].name=CFGAWEBSTR(MSG_SET_KEYS_DOWN);
      keydef[27].name=CFGAWEBSTR(MSG_SET_KEYS_ENTER);
      wheeldef[0].name=CFGAWEBSTR(MSG_SET_KEYS_WHEELUP);
      wheeldef[1].name=CFGAWEBSTR(MSG_SET_KEYS_WHEELDOWN);
   }
   for(i=0;i<39;i++)
   {  l=strlen(keydef[i].name);
      if(l>lkey) lkey=l;
   }
   if(buf=ALLOCTYPE(UBYTE,lshift+lkey+lnum+16,0))
   {  for(i=0;i<39;i++)
      {  if(i<21) Addkbdnode(list,keydef[i].name,keydef[i].key);
         else
         {  sprintf(buf,"%s %s",num,keydef[i].name);
            Addkbdnode(list,buf,keydef[i].key);
         }
      }
      for(i=0;i<39;i++)
      {  if(i<21) sprintf(buf,"%s %s",shift,keydef[i].name);
         else sprintf(buf,"%s %s %s",shift,num,keydef[i].name);
         Addkbdnode(list,buf,keydef[i].key|UKEY_SHIFT);
      }
      for(i=0;i<39;i++)
      {  if(i<21) sprintf(buf,"Alt %s",keydef[i].name);
         else sprintf(buf,"Alt %s %s",num,keydef[i].name);
         Addkbdnode(list,buf,keydef[i].key|UKEY_ALT);
      }
      for(i='A';i<='Z';i++)
      {  sprintf(buf,"Alt %c",i);
         Addkbdnode(list,buf,i|UKEY_ALT|UKEY_ASCII);
      }
      for(i=0;i<2;i++)
      {  Addkbdnode(list,wheeldef[i].name,wheeldef[i].key);
      }
      for(i=0;i<2;i++)
      {  sprintf(buf,"%s %s",shift,wheeldef[i].name);
         Addkbdnode(list,buf,wheeldef[i].key|UKEY_SHIFT);
      }
      FREE(buf);
   }
}

static void *Makekeybdpage(void)
{  struct Userkey *uk=Finduserkey(&uip.keys,keydef[0].key);
   kbdkey=keydef[0].key;
   return
   VLayoutObject,
      StartMember,kbdlistgad=ListBrowserObject,
         GA_RelVerify,TRUE,
         GA_ID,PGID_KBDLIST,
         LISTBROWSER_ShowSelected,TRUE,
         LISTBROWSER_AutoFit,TRUE,
         LISTBROWSER_ColumnInfo,kbdcolumns,
         LISTBROWSER_Labels,&kbdlist,
         LISTBROWSER_Selected,0,
      EndMember,
      StartMember,kbdkeygad=ButtonObject,
         GA_Text,keydef[0].name,
         GA_ReadOnly,TRUE,
         BUTTON_Justification,BCJ_LEFT,
      EndMember,
      CHILD_WeightedHeight,0,
      MemberLabel(CFGAWEBSTR(MSG_SET_KEYS_KEY)),
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,kbdcmdgad=StringObject,
            GA_RelVerify,TRUE,
            GA_ID,PGID_KBDCMD,
            STRINGA_TextVal,uk?uk->cmd:NULLSTRING,
            STRINGA_MaxChars,127,
         EndMember,
         StartMember,kbdpopgad=ChooserObject,
            CHOOSER_DropDown,TRUE,
            CHOOSER_Labels,&kbdhelplist,
            CHOOSER_AutoFit,TRUE,
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      MemberLabel(CFGAWEBSTR(MSG_SET_KEYS_COMMAND)),
#ifdef DEMOVERSION
      GA_Disabled,TRUE,
#endif
   End;
}

static struct ColumnInfo navcolumns[]=
{  0,NULL,0,
   0,NULL,0,
   -1,NULL,0
};

void Makenavlist(struct List *list)
{  short i;
   struct Node *node;
   for(i=0;i<NRNAVBUTTONS;i++)
   {  if(node=AllocListBrowserNode(2,
         LBNA_Column,0,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,navlabels[i],
         LBNA_Column,1,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,uip.navs[i].cmd,
         TAG_END))
         AddTail(list,node);
   }
}

static void *Makenavspage(void)
{  navnr=0;
   return
   VLayoutObject,
      StartMember,navlistgad=ListBrowserObject,
         GA_ID,PGID_NAVLIST,
         GA_RelVerify,TRUE,
         LISTBROWSER_ShowSelected,TRUE,
         LISTBROWSER_AutoFit,TRUE,
         LISTBROWSER_ColumnInfo,navcolumns,
         LISTBROWSER_Labels,&navlist,
         LISTBROWSER_Selected,0,
      EndMember,
      StartMember,navbutgad=ButtonObject,
         GA_Text,navlabels[0],
         GA_ReadOnly,TRUE,
         BUTTON_Justification,BCJ_LEFT,
      EndMember,
      CHILD_WeightedHeight,0,
      MemberLabel(CFGAWEBSTR(MSG_SET_NAVS_BUTTON)),
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,navcmdgad=StringObject,
            GA_RelVerify,TRUE,
            GA_ID,PGID_NAVCMD,
            STRINGA_TextVal,uip.navs[0].cmd,
            STRINGA_MaxChars,127,
         EndMember,
         StartMember,kbdpopgad=ChooserObject,
            CHOOSER_DropDown,TRUE,
            CHOOSER_Labels,&navhelplist,
            CHOOSER_AutoFit,TRUE,
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      MemberLabel(CFGAWEBSTR(MSG_SET_NAVS_COMMAND)),
#ifdef DEMOVERSION
      GA_Disabled,TRUE,
#endif
   End;
}

/*---------------------------------------------------------------------------*/

#ifndef DEMOVERSION

static void Domenulist(WORD code)
{  if(code>=0) menuentry=(struct Menuentry *)Getnode((struct List *)&uip.menus,code);
   else menuentry=NULL;
   menuselected=code;
   Setgadgetattrs(menuvalidgad,window,NULL,
      GA_Text,menuvalid?NULLSTRING:CFGAWEBSTR(MSG_SET_MENUS_INVALID),
      BUTTON_BackgroundPen,drawinfo->dri_Pens[menuvalid?BACKGROUNDPEN:BARBLOCKPEN],
      TAG_END);
   Setgadgetattrs(menuupgad,window,NULL,
      GA_Disabled,!(menuentry && menuentry->prev->prev),
      TAG_END);
   Setgadgetattrs(menudowngad,window,NULL,
      GA_Disabled,!(menuentry && menuentry->next->next),
      TAG_END);
   Setgadgetattrs(menudelgad,window,NULL,
      GA_Disabled,!menuentry,
      TAG_END);
   Setgadgetattrs(menutypegad,window,NULL,
      CHOOSER_Active,menuentry?menuentry->type:0,
      TAG_END);
   Setgadgetattrs(menutitlegad,window,NULL,
      STRINGA_TextVal,menuentry?menuentry->title:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!(menuentry && menuhastitle[menuentry->type]),
      TAG_END);
   Setgadgetattrs(menuscutgad,window,NULL,
      STRINGA_TextVal,menuentry?menuentry->scut:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!(menuentry && menuhasdetails[menuentry->type]),
      TAG_END);
   Setgadgetattrs(menucmdgad,window,NULL,
      STRINGA_TextVal,menuentry?menuentry->cmd:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!(menuentry && menuhasdetails[menuentry->type]),
      TAG_END);
   Setgadgetattrs(menupopgad,window,NULL,
      GA_Disabled,!(menuentry && menuhasdetails[menuentry->type]),
      TAG_END);
}

static void Changemenulist(long select)
{  Setgadgetattrs(menulistgad,window,NULL,
      LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&menulist);
   Makemenulist(&menulist);
   Setgadgetattrs(menulistgad,window,NULL,
         LISTBROWSER_Labels,&menulist,
         LISTBROWSER_AutoFit,TRUE,
         (select>=0)?LISTBROWSER_Selected:TAG_IGNORE,select,
         TAG_END);
   if(select>=0) Setgadgetattrs(menulistgad,window,NULL,
         LISTBROWSER_MakeVisible,select,
         TAG_END);
}

static void Changemenuitem(UBYTE **ptr,struct Gadget *gad)
{  UBYTE *p=(UBYTE *)Getvalue(gad,STRINGA_TextVal);
   if(p && !STREQUAL(p,*ptr) && (p=Dupstr(p,-1)))
   {  if(*ptr) FREE(*ptr);
      *ptr=p;
      Changemenulist(-1);
   }
}

static void Domenuscut(void)
{  UBYTE *p=(UBYTE *)Getvalue(menuscutgad,STRINGA_TextVal);
   UBYTE c=toupper(*p);
   if(c!=menuentry->scut[0])
   {  menuentry->scut[0]=c;
      Setgadgetattrs(menuscutgad,window,NULL,
         STRINGA_TextVal,menuentry->scut,
         TAG_END);
      Changemenulist(-1);
   }
}

static void Domenuadd(void)
{  struct Menuentry *me,*nme;
   me=(struct Menuentry *)Getnode((struct List *)&uip.menus,menuselected);
   if(nme=Addmenuentry(&uip.menus,AMENU_ITEM,"",0,""))
   {  REMOVE(nme);
      INSERT(&uip.menus,nme,me);
      menuselected++;
      Changemenulist(menuselected);
      Validatemenus();
      Domenulist(menuselected);  /* clear gadgets and set current ptrs */
      ActivateLayoutGadget(toplayout,window,NULL,(ULONG)menutitlegad);
   }
}

static void Domenudel(void)
{  if(!menuentry->next->next) menuselected--;
   REMOVE(menuentry);
   Freemenuentry(menuentry);
   Changemenulist(menuselected);
   Validatemenus();
   Domenulist(menuselected);  /* set gadgets and current ptrs */
}

static void Domenumoveup(void)
{  struct Menuentry *me;
   if(menuentry)
   {  me=menuentry->prev->prev;
      if(me)
      {  REMOVE(menuentry);
         INSERT(&uip.menus,menuentry,me);
         menuselected--;
         Changemenulist(menuselected);
         Validatemenus();
         Domenulist(menuselected);
      }
   }
}

static void Domenumovedown(void)
{  struct Menuentry *me;
   if(menuentry)
   {  me=menuentry->next;
      if(me->next)
      {  REMOVE(menuentry);
         INSERT(&uip.menus,menuentry,me);
         menuselected++;
         Changemenulist(menuselected);
         Validatemenus();
         Domenulist(menuselected);
      }
   }
}

#endif /* !DEMOVERSION */

static void Dobutlist(WORD code)
{  if(code>=0) userbutton=(struct Userbutton *)Getnode((struct List *)&uip.buttons,code);
   else userbutton=NULL;
   Setgadgetattrs(butsortgad,window,NULL,
      GA_Disabled,!userbutton,
      TAG_END);
   Setgadgetattrs(butupgad,window,NULL,
      GA_Disabled,!(userbutton && userbutton->prev->prev),
      TAG_END);
   Setgadgetattrs(butdowngad,window,NULL,
      GA_Disabled,!(userbutton && userbutton->next->next),
      TAG_END);
   Setgadgetattrs(butdelgad,window,NULL,
      GA_Disabled,!userbutton,
      TAG_END);
   Setgadgetattrs(butlabelgad,window,NULL,
      STRINGA_TextVal,userbutton?userbutton->label:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!userbutton,
      TAG_END);
   Setgadgetattrs(butcmdgad,window,NULL,
      STRINGA_TextVal,userbutton?userbutton->cmd:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!userbutton,
      TAG_END);
   Setgadgetattrs(butpopgad,window,NULL,
      GA_Disabled,!userbutton,
      TAG_END);
}

static void Changebutlist(long select)
{  Setgadgetattrs(butlistgad,window,NULL,
      LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&butlist);
   Makebutlist(&butlist);
   Setgadgetattrs(butlistgad,window,NULL,
         LISTBROWSER_Labels,&butlist,
         LISTBROWSER_AutoFit,TRUE,
         (select>=0)?LISTBROWSER_Selected:TAG_IGNORE,select,
         TAG_END);
   if(select>=0) Setgadgetattrs(butlistgad,window,NULL,
         LISTBROWSER_MakeVisible,select,
         TAG_END);
}

static void Changeuserbutton(UBYTE **ptr,struct Gadget *gad)
{  UBYTE *p=(UBYTE *)Getvalue(gad,STRINGA_TextVal);
   if(p && !STREQUAL(p,*ptr) && (p=Dupstr(p,-1)))
   {  if(*ptr) FREE(*ptr);
      *ptr=p;
      Changebutlist(-1);
   }
}

/* If user entered URL-type string, convert it to OPEN command */
static void Correctcommand(struct Gadget *gad)
{  UBYTE *p=(UBYTE *)Getvalue(gad,STRINGA_TextVal);
   if(STRNIEQUAL(p,"http:",5)
   || STRNIEQUAL(p,"ftp:",4)
   || STRNIEQUAL(p,"file:",5)
   || STRNIEQUAL(p,"news:",5)
   || STRNIEQUAL(p,"mailto:",7)
   || STRNIEQUAL(p,"gopher:",7)
   || STRNIEQUAL(p,"https:",6))
   {  UBYTE buf[140];
      strcpy(buf,"OPEN ");
      strcat(buf,p); /* gadget string is 127 characters max */
      buf[128]='\0';
      Setgadgetattrs(gad,window,NULL,STRINGA_TextVal,buf,TAG_END);
   }
}

static void Dobutadd(void)
{  struct Userbutton *ub;
   long select;
   if(Adduserbutton(&uip.buttons,"",""))
   {  for(ub=uip.buttons.first,select=-1;ub->next;ub=ub->next,select++);
      Changebutlist(select);
      Dobutlist(select);  /* clear gadgets and set current ptrs */
      ActivateLayoutGadget(toplayout,window,NULL,(ULONG)butlabelgad);
   }
}

static void Dobutdel(void)
{  struct Userbutton *ub;
   long select;
   for(ub=uip.buttons.first,select=0;ub!=userbutton;ub=ub->next,select++);
   if(!userbutton->next->next) select--;
   REMOVE(userbutton);
   Freeuserbutton(userbutton);
   Changebutlist(select);
   Dobutlist(select);  /* set gadgets and current ptrs */
}

static void Dobutmoveup(void)
{  struct Userbutton *ub=userbutton->prev->prev;
   long select;
   if(ub)
   {  REMOVE(userbutton);
      INSERT(&uip.buttons,userbutton,ub);
      for(ub=uip.buttons.first,select=0;ub!=userbutton;ub=ub->next,select++);
      Changebutlist(select);
      Dobutlist(select);
   }
}

static void Dobutmovedown(void)
{  struct Userbutton *ub=userbutton->next;
   long select;
   if(ub->next)
   {  REMOVE(userbutton);
      INSERT(&uip.buttons,userbutton,ub);
      for(ub=uip.buttons.first,select=0;ub!=userbutton;ub=ub->next,select++);
      Changebutlist(select);
      Dobutlist(select);
   }
}

static void Dobutsort(void)
{  struct Userbutton *ub,*uc;
   long select;
   LIST(Userbutton) list;
   list.first = NULL;
   list.tail = NULL;
   list.last = NULL;
   while(ub=REMHEAD(&uip.buttons))
   {  for(uc=list.first;uc->next;uc=uc->next)
      {  if(stricmp(ub->label,uc->label)<0) break;
      }
      INSERT(&list,ub,uc->prev);
   }
   while(ub=REMHEAD(&list)) ADDTAIL(&uip.buttons,ub);
   for(ub=uip.buttons.first,select=0;ub!=userbutton;ub=ub->next,select++);
   Changebutlist(select);
   Dobutlist(select);
}

static void Dopuplist(WORD code)
{  if(code>=0)
   {  popupitem=(struct Popupitem *)
         Getnode((struct List *)&uip.popupmenu[popuptype],code);
   }
   else popupitem=NULL;
   Setgadgetattrs(pupsortgad,window,NULL,
      GA_Disabled,!popupitem,
      TAG_END);
   Setgadgetattrs(pupupgad,window,NULL,
      GA_Disabled,!(popupitem && popupitem->prev->prev),
      TAG_END);
   Setgadgetattrs(pupdowngad,window,NULL,
      GA_Disabled,!(popupitem && popupitem->next->next),
      TAG_END);
   Setgadgetattrs(pupdelgad,window,NULL,
      GA_Disabled,!popupitem,
      TAG_END);
   Setgadgetattrs(puptitlegad,window,NULL,
      STRINGA_TextVal,popupitem?popupitem->title:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!popupitem,
      TAG_END);
   Setgadgetattrs(pupcmdgad,window,NULL,
      STRINGA_TextVal,popupitem?popupitem->cmd:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!popupitem,
      TAG_END);
   Setgadgetattrs(puppopgad,window,NULL,
      GA_Disabled,!popupitem,
      TAG_END);
   Setgadgetattrs(pupinmgad,window,NULL,
      GA_Selected,popupitem?BOOLVAL(popupitem->flags&PUPF_INMEM):FALSE,
      GA_Disabled,!popupitem || popuptype==PUPT_FRAME,
      TAG_END);
   Setgadgetattrs(pupnomgad,window,NULL,
      GA_Selected,popupitem?BOOLVAL(popupitem->flags&PUPF_NOTINMEM):FALSE,
      GA_Disabled,!popupitem || popuptype==PUPT_FRAME,
      TAG_END);
}

static void Changepuplist(long select)
{  Setgadgetattrs(puplistgad,window,NULL,
      LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&puplist);
   Makepuplist(&puplist,popuptype);
   Setgadgetattrs(puplistgad,window,NULL,
         LISTBROWSER_Labels,&puplist,
         LISTBROWSER_AutoFit,TRUE,
         (select>=0)?LISTBROWSER_Selected:TAG_IGNORE,select,
         TAG_END);
   if(select>=0) Setgadgetattrs(puplistgad,window,NULL,
         LISTBROWSER_MakeVisible,select,
         TAG_END);
}

static void Changepopupitem(UBYTE **ptr,struct Gadget *gad)
{  UBYTE *p=(UBYTE *)Getvalue(gad,STRINGA_TextVal);
   if(p && !STREQUAL(p,*ptr) && (p=Dupstr(p,-1)))
   {  if(*ptr) FREE(*ptr);
      *ptr=p;
      Changepuplist(-1);
   }
}

static void Dopuptype(short code)
{  void *helplist;
   popuptype=code;
   Changepuplist(0);
   switch(popuptype)
   {  case PUPT_IMAGE:  helplist=&pupihelplist;break;
      case PUPT_LINK:   helplist=&puplhelplist;break;
      case PUPT_FRAME:  helplist=&pupfhelplist;break;
   }
   SetGadgetAttrs(puppopgad,window,NULL,CHOOSER_Labels,helplist,TAG_END);
   Dopuplist(0);
}

static void Dopupadd(void)
{  struct Popupitem *pi;
   long select;
   if(Addpopupitem(&uip.popupmenu[popuptype],
      (popuptype==PUPT_FRAME)?0:(PUPF_INMEM|PUPF_NOTINMEM),
      "",""))
   {  for(pi=uip.popupmenu[popuptype].first,select=-1;pi->next;pi=pi->next,select++);
      Changepuplist(select);
      Dopuplist(select);  /* clear gadgets and set current ptrs */
      ActivateLayoutGadget(toplayout,window,NULL,(ULONG)puptitlegad);
   }
}

static void Dopupdel(void)
{  struct Popupitem *pi;
   long select;
   for(pi=uip.popupmenu[popuptype].first,select=0;pi!=popupitem;pi=pi->next,select++);
   if(!popupitem->next->next) select--;
   REMOVE(popupitem);
   Freepopupitem(popupitem);
   Changepuplist(select);
   Dopuplist(select);  /* set gadgets and current ptrs */
}

static void Dopupmoveup(void)
{  struct Popupitem *pi=popupitem->prev->prev;
   long select;
   if(pi)
   {  REMOVE(popupitem);
      INSERT(&uip.popupmenu[popuptype],popupitem,pi);
      for(pi=uip.popupmenu[popuptype].first,select=0;pi!=popupitem;pi=pi->next,select++);
      Changepuplist(select);
      Dopuplist(select);
   }
}

static void Dopupmovedown(void)
{  struct Popupitem *pi=popupitem->next;
   long select;
   if(pi->next)
   {  REMOVE(popupitem);
      INSERT(&uip.popupmenu[popuptype],popupitem,pi);
      for(pi=uip.popupmenu[popuptype].first,select=0;pi!=popupitem;pi=pi->next,select++);
      Changepuplist(select);
      Dopuplist(select);
   }
}

static void Dopupsort(void)
{  struct Popupitem *pi,*pj;
   long select;
   LIST(Popupitem) list;
   list.first = NULL;
   list.tail = NULL;
   list.last = NULL;
   while(pi=REMHEAD(&uip.popupmenu[popuptype]))
   {  for(pj=list.first;pj->next;pj=pj->next)
      {  if(stricmp(pi->title,pj->title)<0) break;
      }
      INSERT(&list,pi,pj->prev);
   }
   while(pi=REMHEAD(&list)) ADDTAIL(&uip.popupmenu[popuptype],pi);
   for(pi=uip.popupmenu[popuptype].first,select=0;pi!=popupitem;pi=pi->next,select++);
   Changepuplist(select);
   Dopuplist(select);
}

/* setflag is flag to set if both are unselected */
static void Dopupflag(USHORT setflag)
{  USHORT flags;
   if(popupitem && popuptype!=PUPT_FRAME)
   {  flags=popupitem->flags&~(PUPF_INMEM|PUPF_NOTINMEM);
      if(Getselected(pupinmgad)) flags|=PUPF_INMEM;
      if(Getselected(pupnomgad)) flags|=PUPF_NOTINMEM;
      if(!flags)
      {  flags=setflag;
         Setgadgetattrs((setflag&PUPF_INMEM)?pupinmgad:pupnomgad,window,NULL,
            GA_Selected,TRUE,TAG_END);
      }
      popupitem->flags=flags;
   }
}

#ifndef DEMOVERSION

static void Dokbdlist(WORD code)
{  struct Node *node;
   ULONG key=0;
   struct Userkey *uk=NULL;
   UBYTE *keyname="";
   if(code>=0) node=Getnode(&kbdlist,code);
   else node=NULL;
   if(node)
   {  GetListBrowserNodeAttrs(node,
         LBNA_UserData,&key,
         LBNA_Column,0,
         LBNCA_Text,&keyname,
         TAG_END);
      uk=Finduserkey(&uip.keys,key);
      kbdkey=key;
   }
   else kbdkey=0;
   Setgadgetattrs(kbdkeygad,window,NULL,
      GA_Disabled,!node,
      GA_Text,keyname,
      TAG_END);
   Setgadgetattrs(kbdcmdgad,window,NULL,
      GA_Disabled,!node,
      STRINGA_TextVal,uk?uk->cmd:NULLSTRING,
      TAG_END);
}

static void Dokbdcmd(void)
{  struct Userkey *uk;
   UBYTE *cmd=(UBYTE *)Getvalue(kbdcmdgad,STRINGA_TextVal);
   struct Node *node;
   ULONG nodekey;
   /* Update the display */
   Setgadgetattrs(kbdlistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
   for(node=kbdlist.lh_Head;node->ln_Succ;node=node->ln_Succ)
   {  GetListBrowserNodeAttrs(node,LBNA_UserData,&nodekey,TAG_END);
      if(nodekey==kbdkey)
      {  SetListBrowserNodeAttrs(node,
            LBNA_Column,1,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,cmd,
            TAG_END);
         break;
      }
   }
   Setgadgetattrs(kbdlistgad,window,NULL,LISTBROWSER_Labels,&kbdlist,TAG_END);
   /* Update the prefs list */
   if(uk=Finduserkey(&uip.keys,kbdkey))
   {  if(*cmd)
      {  if(uk->cmd) FREE(uk->cmd);
         uk->cmd=Dupstr(cmd,-1);
      }
      else
      {  REMOVE(uk);
         Freeuserkey(uk);
      }
   }
   else if(*cmd)
   {  Adduserkey(&uip.keys,kbdkey,cmd);
   }
}

static void Donavlist(WORD code)
{  navnr=code;
   Setgadgetattrs(navbutgad,window,NULL,
      GA_Text,navlabels[code],
      TAG_END);
   Setgadgetattrs(navcmdgad,window,NULL,
      STRINGA_TextVal,uip.navs[code].cmd,
      TAG_END);
}

static void Donavcmd(void)
{  UBYTE *cmd=(UBYTE *)Getvalue(navcmdgad,STRINGA_TextVal);
   struct Node *node;
   long n=0;
   Setgadgetattrs(navlistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
   for(node=navlist.lh_Head;node->ln_Succ;node=node->ln_Succ)
   {  if(n==navnr)
      {  SetListBrowserNodeAttrs(node,
            LBNA_Column,1,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,cmd,
            TAG_END);
         break;
      }
      n++;
   }
   Setgadgetattrs(navlistgad,window,NULL,LISTBROWSER_Labels,&navlist,TAG_END);
   if(uip.navs[navnr].cmd) FREE(uip.navs[navnr].cmd);
   uip.navs[navnr].cmd=Dupstr(cmd,-1);
}

#endif /* !DEMOVERSION */

static void Docursor(short n)
{  long page=Getvalue(pagegad,PAGE_Current);
   long newsel;
   switch(page)
   {  
#ifndef DEMOVERSION
      case UIPREFS_MENU:
         if(control)
         {  if(n<0) Domenumoveup();
            else if(n>0) Domenumovedown();
         }
         else
         {  newsel=Moveselected(window,menulistgad,&menulist,n);
            if(newsel>=0) Domenulist(newsel);
         }
         break;
#endif
      case UIPREFS_BUTTONS:
         if(control)
         {  if(n<0) Dobutmoveup();
            else if(n>0) Dobutmovedown();
         }
         else
         {  newsel=Moveselected(window,butlistgad,&butlist,n);
            if(newsel>=0) Dobutlist(newsel);
         }
         break;
      case UIPREFS_POPUP:
         if(control)
         {  if(n<0) Dopupmoveup();
            else if(n>0) Dopupmovedown();
         }
         else
         {  newsel=Moveselected(window,puplistgad,&puplist,n);
            if(newsel>=0) Dopuplist(newsel);
         }
         break;
#ifndef DEMOVERSION
      case UIPREFS_KEYBD:
         newsel=Moveselected(window,kbdlistgad,&kbdlist,n);
         if(newsel>=0) Dokbdlist(newsel);
         break;
      case UIPREFS_NAVS:
         newsel=Moveselected(window,navlistgad,&navlist,n);
         if(newsel>=0) Donavlist(newsel);
         break;
#endif
   }
}

/* get data from gadgets */
static void Copydata(void)
{  uip.showbuttons=Getselected(ubusgad);
   uip.shownav=Getselected(navsgad);
   uip.popupkey=0;
   if(Getselected(pushiftgad)) uip.popupkey|=IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT;
   if(Getselected(pualtgad)) uip.popupkey|=IEQUALIFIER_LALT|IEQUALIFIER_RALT;
   if(Getselected(puctrlgad)) uip.popupkey|=IEQUALIFIER_CONTROL;
   if(Getselected(pumidbutgad)) uip.popupkey|=IEQUALIFIER_MIDBUTTON;
   if(Getselected(purbutgad)) uip.popupkey|=IEQUALIFIER_RBUTTON;
}

/* set gadgets to changed data */
static void Setdata(void)
{  long page=Getvalue(pagegad,PAGE_Current);
   BOOL hasitem;
   struct Window *win;
   struct Userkey *uk;
   win=(page==UIPREFS_OPTIONS)?window:NULL;
   Setgadgetattrs(ubusgad,win,NULL,GA_Selected,uip.showbuttons,TAG_END);
   Setgadgetattrs(navsgad,win,NULL,GA_Selected,uip.shownav,TAG_END);
   Setgadgetattrs(pushiftgad,win,NULL,
      GA_Selected,(uip.popupkey&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT))!=0,
      TAG_END);
   Setgadgetattrs(pualtgad,win,NULL,
      GA_Selected,(uip.popupkey&(IEQUALIFIER_LALT|IEQUALIFIER_RALT))!=0,
      TAG_END);
   Setgadgetattrs(puctrlgad,win,NULL,
      GA_Selected,(uip.popupkey&IEQUALIFIER_CONTROL)!=0,
      TAG_END);
   Setgadgetattrs(pumidbutgad,win,NULL,
      GA_Selected,(uip.popupkey&IEQUALIFIER_MIDBUTTON)!=0,
      TAG_END);
   Setgadgetattrs(purbutgad,win,NULL,
      GA_Selected,(uip.popupkey&IEQUALIFIER_RBUTTON)!=0,
      TAG_END);
#ifndef DEMOVERSION
   win=(page==UIPREFS_MENU)?window:NULL;
   menuentry=uip.menus.first;
   if(menuentry->next)
   {  hasitem=TRUE;
      menuselected=0;
   }
   else
   {  hasitem=FALSE;
      menuentry=NULL;
      menuselected=-1;
   }
   Validatemenus();
   Setgadgetattrs(menulistgad,win,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&menulist);
   Makemenulist(&menulist);
   Setgadgetattrs(menulistgad,win,NULL,
      LISTBROWSER_Labels,&menulist,
      LISTBROWSER_AutoFit,TRUE,
      LISTBROWSER_Selected,0,
      LISTBROWSER_Top,0,
      TAG_END);
   Setgadgetattrs(menuvalidgad,win,NULL,
      GA_Text,menuvalid?NULLSTRING:CFGAWEBSTR(MSG_SET_MENUS_INVALID),
      BUTTON_BackgroundPen,drawinfo->dri_Pens[menuvalid?BACKGROUNDPEN:BARBLOCKPEN],
      TAG_END);
   Setgadgetattrs(menuupgad,win,NULL,GA_Disabled,TRUE,TAG_END);
   Setgadgetattrs(menudowngad,win,NULL,GA_Disabled,!(hasitem && menuentry->next->next),TAG_END);
   Setgadgetattrs(menudelgad,win,NULL,GA_Disabled,!hasitem,TAG_END);
   Setgadgetattrs(menutypegad,win,NULL,
      CHOOSER_Active,hasitem?menuentry->type:0,
      GA_Disabled,!hasitem,
      TAG_END);
   Setgadgetattrs(menutitlegad,win,NULL,
      STRINGA_TextVal,hasitem?menuentry->title:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!(hasitem && menuhastitle[menuentry->type]),
      TAG_END);
   Setgadgetattrs(menuscutgad,win,NULL,
      STRINGA_TextVal,hasitem?menuentry->scut:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!(hasitem && menuhasdetails[menuentry->type]),
      TAG_END);
   Setgadgetattrs(menucmdgad,win,NULL,
      STRINGA_TextVal,hasitem?menuentry->cmd:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!(hasitem && menuhasdetails[menuentry->type]),
      TAG_END);
   Setgadgetattrs(menupopgad,win,NULL,
      GA_Disabled,(!hasitem && menuhasdetails[menuentry->type]),
      TAG_END);
#endif
   win=(page==UIPREFS_BUTTONS)?window:NULL;
   userbutton=uip.buttons.first;
   if(userbutton->next) hasitem=TRUE;
   else hasitem=FALSE;
   Setgadgetattrs(butlistgad,win,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&butlist);
   Makebutlist(&butlist);
   Setgadgetattrs(butlistgad,win,NULL,
      LISTBROWSER_Labels,&butlist,
      LISTBROWSER_AutoFit,TRUE,
      LISTBROWSER_Selected,0,
      LISTBROWSER_Top,0,
      TAG_END);
   Setgadgetattrs(butsortgad,win,NULL,GA_Disabled,!hasitem,TAG_END);
   Setgadgetattrs(butupgad,win,NULL,GA_Disabled,TRUE,TAG_END);
   Setgadgetattrs(butdowngad,win,NULL,GA_Disabled,!(hasitem && userbutton->next->next),TAG_END);
   Setgadgetattrs(butdelgad,win,NULL,GA_Disabled,!hasitem,TAG_END);
   Setgadgetattrs(butlabelgad,win,NULL,
      STRINGA_TextVal,hasitem?userbutton->label:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!hasitem,
      TAG_END);
   Setgadgetattrs(butcmdgad,win,NULL,
      STRINGA_TextVal,hasitem?userbutton->cmd:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!hasitem,
      TAG_END);
   Setgadgetattrs(butpopgad,win,NULL,GA_Disabled,!hasitem,TAG_END);
   win=(page==UIPREFS_POPUP)?window:NULL;
   popupitem=uip.popupmenu[popuptype].first;
   if(popupitem->next) hasitem=TRUE;
   else hasitem=FALSE;
   Setgadgetattrs(puplistgad,win,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&puplist);
   Makepuplist(&puplist,popuptype);
   Setgadgetattrs(puplistgad,win,NULL,
      LISTBROWSER_Labels,&puplist,
      LISTBROWSER_AutoFit,TRUE,
      LISTBROWSER_Selected,0,
      LISTBROWSER_Top,0,
      TAG_END);
   Setgadgetattrs(pupsortgad,win,NULL,GA_Disabled,!hasitem,TAG_END);
   Setgadgetattrs(pupupgad,win,NULL,GA_Disabled,!(hasitem && popupitem->prev->prev),TAG_END);
   Setgadgetattrs(pupdowngad,win,NULL,GA_Disabled,!(hasitem && popupitem->next->next),TAG_END);
   Setgadgetattrs(pupdelgad,win,NULL,GA_Disabled,!hasitem,TAG_END);
   Setgadgetattrs(puptitlegad,win,NULL,
      STRINGA_TextVal,hasitem?popupitem->title:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!hasitem,
      TAG_END);
   Setgadgetattrs(pupcmdgad,win,NULL,
      STRINGA_TextVal,hasitem?popupitem->cmd:NULLSTRING,
      STRINGA_DispPos,0,
      GA_Disabled,!hasitem,
      TAG_END);
   Setgadgetattrs(puppopgad,win,NULL,GA_Disabled,!hasitem,TAG_END);
   Setgadgetattrs(pupinmgad,win,NULL,
      GA_Selected,hasitem?BOOLVAL(popupitem->flags&PUPF_INMEM):FALSE,
      GA_Disabled,!hasitem || popuptype==PUPT_FRAME,
      TAG_END);
   Setgadgetattrs(pupnomgad,win,NULL,
      GA_Selected,hasitem?BOOLVAL(popupitem->flags&PUPF_NOTINMEM):FALSE,
      GA_Disabled,!hasitem || popuptype==PUPT_FRAME,
      TAG_END);
#ifndef DEMOVERSION
   win=(page==UIPREFS_KEYBD)?window:NULL;
   Setgadgetattrs(kbdlistgad,win,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&kbdlist);
   Makekbdlist(&kbdlist);
   Setgadgetattrs(kbdlistgad,win,NULL,
      LISTBROWSER_Labels,&kbdlist,
      LISTBROWSER_AutoFit,TRUE,
      LISTBROWSER_Selected,0,
      LISTBROWSER_Top,0,
      TAG_END);
   kbdkey=keydef[0].key;
   uk=Finduserkey(&uip.keys,kbdkey);
   Setgadgetattrs(kbdkeygad,win,NULL,GA_Text,keydef[0].name,TAG_END);
   Setgadgetattrs(kbdcmdgad,win,NULL,STRINGA_TextVal,uk?uk->cmd:NULLSTRING,TAG_END);
   win=(page==UIPREFS_NAVS)?window:NULL;
   Setgadgetattrs(navlistgad,win,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&navlist);
   Makenavlist(&navlist);
   Setgadgetattrs(navlistgad,win,NULL,
      LISTBROWSER_Labels,&navlist,
      LISTBROWSER_AutoFit,TRUE,
      LISTBROWSER_Selected,0,
      LISTBROWSER_Top,0,
      TAG_END);
   navnr=0;
   Setgadgetattrs(navbutgad,win,NULL,GA_Text,navlabels[0],TAG_END);
   Setgadgetattrs(navcmdgad,win,NULL,STRINGA_TextVal,uip.navs[0].cmd,TAG_END);
#endif
}

/*---------------------------------------------------------------------------*/

static long __saveds __asm Idcmphook(register __a0 struct Hook *hook,
   register __a1 struct IntuiMessage *msg)
{  switch(msg->Class)
   {  case IDCMP_RAWKEY:
         if(msg->Qualifier&IEQUALIFIER_CONTROL) control=TRUE;
         else control=FALSE;
         break;
   }
   return 0;
}

static void Localize(void)
{  tablabels[0]=CFGAWEBSTR(MSG_SET_CTUI_OPTIONS);
   tablabels[1]=CFGAWEBSTR(MSG_SET_CTUI_MENUS);
   tablabels[2]=CFGAWEBSTR(MSG_SET_CTUI_BUTTONS);
   tablabels[3]=CFGAWEBSTR(MSG_SET_CTUI_POPUP);
   tablabels[4]=CFGAWEBSTR(MSG_SET_CTUI_KEYS);
   tablabels[5]=CFGAWEBSTR(MSG_SET_CTUI_NAV);
   tablabels[6]=NULL;
   buthelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_U2);
   buthelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_N);
   buthelplabels[2]=CFGAWEBSTR(MSG_SET_HLP_I4);
   buthelplabels[3]=CFGAWEBSTR(MSG_SET_HLP_T);
   buthelplabels[4]=CFGAWEBSTR(MSG_SET_HLP_C);
   buthelplabels[5]=NULL;
   puptypelabels[0]=CFGAWEBSTR(MSG_SET_PUPTYPE_IMAGE);
   puptypelabels[1]=CFGAWEBSTR(MSG_SET_PUPTYPE_LINK);
   puptypelabels[2]=CFGAWEBSTR(MSG_SET_PUPTYPE_FRAME);
   puptypelabels[3]=NULL;
   pupihelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_U3);
   pupihelplabels[1]=NULL;
   puplhelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_U3);
   puplhelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_I2);
   puplhelplabels[2]=CFGAWEBSTR(MSG_SET_HLP_T);
   puplhelplabels[3]=NULL;
   pupfhelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_U2);
   pupfhelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_I);
   pupfhelplabels[2]=NULL;
   kbdhelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_U2);
   kbdhelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_N);
   kbdhelplabels[2]=CFGAWEBSTR(MSG_SET_HLP_I4);
   kbdhelplabels[3]=CFGAWEBSTR(MSG_SET_HLP_T);
   kbdhelplabels[4]=CFGAWEBSTR(MSG_SET_HLP_C);
   kbdhelplabels[5]=NULL;
   menucolumnlabels[0]=CFGAWEBSTR(MSG_SET_MENULABEL_MENU);
   menucolumnlabels[1]=CFGAWEBSTR(MSG_SET_MENULABEL_ITEM);
   menucolumnlabels[2]="�";
   menucolumnlabels[3]="";
   menutypelabels[0]=CFGAWEBSTR(MSG_SET_MENUTYPE_MENU);
   menutypelabels[1]=CFGAWEBSTR(MSG_SET_MENUTYPE_ITEM);
   menutypelabels[2]=CFGAWEBSTR(MSG_SET_MENUTYPE_SUB);
   menutypelabels[3]="----";
   menutypelabels[4]=NULL;
   menuhelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_U2);
   menuhelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_N);
   menuhelplabels[2]=CFGAWEBSTR(MSG_SET_HLP_I4);
   menuhelplabels[3]=CFGAWEBSTR(MSG_SET_HLP_T);
   menuhelplabels[4]=CFGAWEBSTR(MSG_SET_HLP_C);
   menuhelplabels[5]=NULL;
   navlabels[0]=CFGAWEBSTR(MSG_SET_NAVS_BACK);
   navlabels[1]=CFGAWEBSTR(MSG_SET_NAVS_FORWARD);
   navlabels[2]=CFGAWEBSTR(MSG_SET_NAVS_HOME);
   navlabels[3]=CFGAWEBSTR(MSG_SET_NAVS_ADDHOT);
   navlabels[4]=CFGAWEBSTR(MSG_SET_NAVS_HOTLIST);
   navlabels[5]=CFGAWEBSTR(MSG_SET_NAVS_CANCEL);
   navlabels[6]=CFGAWEBSTR(MSG_SET_NAVS_NETSTATUS);
   navlabels[7]=CFGAWEBSTR(MSG_SET_NAVS_SEARCH);
   navlabels[8]=CFGAWEBSTR(MSG_SET_NAVS_RELOAD);
   navlabels[9]=CFGAWEBSTR(MSG_SET_NAVS_LOADIMAGES);
   navhelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_U2);
   navhelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_N);
   navhelplabels[2]=CFGAWEBSTR(MSG_SET_HLP_I4);
   navhelplabels[3]=CFGAWEBSTR(MSG_SET_HLP_T);
   navhelplabels[4]=CFGAWEBSTR(MSG_SET_HLP_C);
   navhelplabels[5]=NULL;
}

BOOL Opengui(void)
{  BOOL ok=FALSE;
   short i;
   if(window)
   {  WindowToFront(window);
      ActivateWindow(window);
      return FALSE;
   }
   uip.menus.first = NULL;
   uip.menus.tail = NULL;
   uip.menus.last = NULL;
   uip.buttons.first = NULL;
   uip.buttons.tail = NULL;
   uip.buttons.last = NULL;
   uip.keys.first = NULL;
   uip.keys.tail = NULL;
   uip.keys.last = NULL;
   orguip.menus.first = NULL;
   orguip.menus.tail = NULL;
   orguip.menus.last = NULL;
   orguip.buttons.first = NULL;
   orguip.buttons.tail = NULL;
   orguip.buttons.last = NULL;
   orguip.keys.first = NULL;
   orguip.keys.tail = NULL;
   orguip.keys.last = NULL;
   for(i=0;i<NRPOPUPMENUS;i++)
   {  uip.popupmenu[i].first = NULL;
      uip.popupmenu[i].tail = NULL;
      uip.popupmenu[i].last = NULL;
      orguip.popupmenu[i].first = NULL;
      orguip.popupmenu[i].tail = NULL;
      orguip.popupmenu[i].last = NULL;
   }
   (&tablist)->lh_Head = (struct Node *)&((&tablist)->lh_Tail);
   (&tablist)->lh_Tail = NULL;
   (&tablist)->lh_TailPred = (struct Node *)&((&tablist)->lh_Head);
   (&menulist)->lh_Head = (struct Node *)&((&menulist)->lh_Tail);
   (&menulist)->lh_Tail = NULL;
   (&menulist)->lh_TailPred = (struct Node *)&((&menulist)->lh_Head);
   (&menutypelist)->lh_Head = (struct Node *)&((&menutypelist)->lh_Tail);
   (&menutypelist)->lh_Tail = NULL;
   (&menutypelist)->lh_TailPred = (struct Node *)&((&menutypelist)->lh_Head);
   (&menuhelplist)->lh_Head = (struct Node *)&((&menuhelplist)->lh_Tail);
   (&menuhelplist)->lh_Tail = NULL;
   (&menuhelplist)->lh_TailPred = (struct Node *)&((&menuhelplist)->lh_Head);
   (&butlist)->lh_Head = (struct Node *)&((&butlist)->lh_Tail);
   (&butlist)->lh_Tail = NULL;
   (&butlist)->lh_TailPred = (struct Node *)&((&butlist)->lh_Head);
   (&buthelplist)->lh_Head = (struct Node *)&((&buthelplist)->lh_Tail);
   (&buthelplist)->lh_Tail = NULL;
   (&buthelplist)->lh_TailPred = (struct Node *)&((&buthelplist)->lh_Head);
   (&puptypelist)->lh_Head = (struct Node *)&((&puptypelist)->lh_Tail);
   (&puptypelist)->lh_Tail = NULL;
   (&puptypelist)->lh_TailPred = (struct Node *)&((&puptypelist)->lh_Head);
   (&puplist)->lh_Head = (struct Node *)&((&puplist)->lh_Tail);
   (&puplist)->lh_Tail = NULL;
   (&puplist)->lh_TailPred = (struct Node *)&((&puplist)->lh_Head);
   (&pupihelplist)->lh_Head = (struct Node *)&((&pupihelplist)->lh_Tail);
   (&pupihelplist)->lh_Tail = NULL;
   (&pupihelplist)->lh_TailPred = (struct Node *)&((&pupihelplist)->lh_Head);
   (&puplhelplist)->lh_Head = (struct Node *)&((&puplhelplist)->lh_Tail);
   (&puplhelplist)->lh_Tail = NULL;
   (&puplhelplist)->lh_TailPred = (struct Node *)&((&puplhelplist)->lh_Head);
   (&pupfhelplist)->lh_Head = (struct Node *)&((&pupfhelplist)->lh_Tail);
   (&pupfhelplist)->lh_Tail = NULL;
   (&pupfhelplist)->lh_TailPred = (struct Node *)&((&pupfhelplist)->lh_Head);
   (&kbdlist)->lh_Head = (struct Node *)&((&kbdlist)->lh_Tail);
   (&kbdlist)->lh_Tail = NULL;
   (&kbdlist)->lh_TailPred = (struct Node *)&((&kbdlist)->lh_Head);
   (&kbdhelplist)->lh_Head = (struct Node *)&((&kbdhelplist)->lh_Tail);
   (&kbdhelplist)->lh_Tail = NULL;
   (&kbdhelplist)->lh_TailPred = (struct Node *)&((&kbdhelplist)->lh_Head);
   (&navlist)->lh_Head = (struct Node *)&((&navlist)->lh_Tail);
   (&navlist)->lh_Tail = NULL;
   (&navlist)->lh_TailPred = (struct Node *)&((&navlist)->lh_Head);
   (&navhelplist)->lh_Head = (struct Node *)&((&navhelplist)->lh_Tail);
   (&navhelplist)->lh_Tail = NULL;
   (&navhelplist)->lh_TailPred = (struct Node *)&((&navhelplist)->lh_Head);
   if(!tablabels[0]) Localize();
   if(nport=CreateMsgPort())
   {  strcpy(prefsname,"ENV:" DEFAULTCFG);
      if(*configname) AddPart(prefsname,configname,64);
      AddPart(prefsname,"gui",64);
      nreq.nr_Name=prefsname;
      nreq.nr_Port=nport;
      nreq.nr_Flags=NRF_SEND_MESSAGE;
      StartNotify(&nreq);
      Copyguiprefs(&defprefs.gui,&uip);
      if(!Prefsconfiguseslegacyawebpath(NULL))
      {  Loadguiprefs(&uip,FALSE,NULL);
      }
      Copyguiprefs(&uip,&orguip);
      tested=FALSE;
      Makeclicktablist(&tablist,tablabels);
      Makechooserlist(&menutypelist,menutypelabels,FALSE);
      Makechooserlist(&menuhelplist,menuhelplabels,TRUE);
      Makemenulist(&menulist);
      Makebutlist(&butlist);
      Makechooserlist(&buthelplist,buthelplabels,TRUE);
      Makechooserlist(&puptypelist,puptypelabels,FALSE);
      popuptype=PUPT_IMAGE;
      Makepuplist(&puplist,popuptype);
      Makechooserlist(&pupihelplist,pupihelplabels,TRUE);
      Makechooserlist(&puplhelplist,puplhelplabels,TRUE);
      Makechooserlist(&pupfhelplist,pupfhelplabels,TRUE);
      Makekbdlist(&kbdlist);
      Makechooserlist(&kbdhelplist,kbdhelplabels,TRUE);
      Makenavlist(&navlist);
      Makechooserlist(&navhelplist,navhelplabels,TRUE);
      idcmphook.h_Entry=(HOOKFUNC)Idcmphook;
      winobject=WindowObject,
            WA_Title,CFGAWEBSTR(MSG_SET_REQTITLE_GUIWINDOW),
            WA_Left,setprefs.guix,
            WA_Top,setprefs.guiy,
            WA_InnerWidth,setprefs.guiw,
            WA_InnerHeight,setprefs.guih,
            WA_AutoAdjust,TRUE,
            WA_CloseGadget,TRUE,
            WA_DragBar,TRUE,
            WA_DepthGadget,TRUE,
            WA_SizeGadget,TRUE,
            WA_Activate,TRUE,
            WA_IDCMP,IDCMP_RAWKEY|IDCMP_MENUPICK,
            WA_SimpleRefresh,TRUE,
            WA_PubScreen,pubscreen,
            setprefs.guiw?TAG_IGNORE:WINDOW_Position,WPOS_CENTERSCREEN,
            WINDOW_IDCMPHook,&idcmphook,
            WINDOW_IDCMPHookBits,IDCMP_RAWKEY,
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
                     PAGE_Add,Makeoptionspage(),
                     PAGE_Add,Makemenupage(),
                     PAGE_Add,Makebuttonspage(),
                     PAGE_Add,Makepopuppage(),
                     PAGE_Add,Makekeybdpage(),
                     PAGE_Add,Makenavspage(),
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
            GetAttr(WINDOW_SigMask,winobject,&uimask);
            uimask|=1<<nport->mp_SigBit;
            ok=TRUE;
         }
      }
   }
   if(!ok) Closegui();
   return ok;
}

/* returns TRUE is window remains open, FALSE if window should close */
BOOL Processgui(void)
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
#ifndef DEMOVERSION
               case PGID_MENULIST:
                  Domenulist(code);
                  break;
               case PGID_MENUADD:
                  Domenuadd();
                  break;
               case PGID_MENUDEL:
                  if(menuentry) Domenudel();
                  break;
               case PGID_MENUUP:
                  if(menuentry) Domenumoveup();
                  break;
               case PGID_MENUDOWN:
                  if(menuentry) Domenumovedown();
                  break;
               case PGID_MENUTYPE:
                  if(menuentry)
                  {  menuentry->type=code;
                     Changemenulist(-1);
                     Validatemenus();
                     Domenulist(menuselected);
                  }
                  break;
               case PGID_MENUTITLE:
                  if(menuentry)
                     Changemenuitem(&menuentry->title,menutitlegad);
                  break;
               case PGID_MENUSCUT:
                  if(menuentry) Domenuscut();
                  break;
               case PGID_MENUCMD:
                     Changemenuitem(&menuentry->cmd,menucmdgad);
                  break;
#endif
               case PGID_BUTLIST:
                  Dobutlist(code);
                  break;
               case PGID_BUTADD:
                  Dobutadd();
                  break;
               case PGID_BUTDEL:
                  if(userbutton) Dobutdel();
                  break;
               case PGID_BUTUP:
                  if(userbutton) Dobutmoveup();
                  break;
               case PGID_BUTDOWN:
                  if(userbutton) Dobutmovedown();
                  break;
               case PGID_BUTSORT:
                  Dobutsort();
                  break;
               case PGID_BUTLABEL:
                  if(userbutton)
                     Changeuserbutton(&userbutton->label,butlabelgad);
                  break;
               case PGID_BUTCMD:
                  if(userbutton)
                  {  Correctcommand(butcmdgad);
                     Changeuserbutton(&userbutton->cmd,butcmdgad);
                  }
                  break;
               case PGID_PUPTYPE:
                  Dopuptype(code);
                  break;
               case PGID_PUPLIST:
                  Dopuplist(code);
                  break;
               case PGID_PUPADD:
                  Dopupadd();
                  break;
               case PGID_PUPDEL:
                  if(popupitem) Dopupdel();
                  break;
               case PGID_PUPUP:
                  if(popupitem) Dopupmoveup();
                  break;
               case PGID_PUPDOWN:
                  if(popupitem) Dopupmovedown();
                  break;
               case PGID_PUPSORT:
                  Dopupsort();
                  break;
               case PGID_PUPTITLE:
                  if(popupitem)
                     Changepopupitem(&popupitem->title,puptitlegad);
                  break;
               case PGID_PUPCMD:
                  if(popupitem)
                     Changepopupitem(&popupitem->cmd,pupcmdgad);
                  break;
               case PGID_PUPINM:
                  Dopupflag(PUPF_NOTINMEM);
                  break;
               case PGID_PUPNOM:
                  Dopupflag(PUPF_INMEM);
                  break;
#ifndef DEMOVERSION
               case PGID_KBDLIST:
                  Dokbdlist(code);
                  break;
               case PGID_KBDCMD:
                  Correctcommand(kbdcmdgad);
                  Dokbdcmd();
                  break;
               case PGID_NAVLIST:
                  Donavlist(code);
                  break;
               case PGID_NAVCMD:
                  Donavcmd();
                  break;
#endif
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
                  Saveguiprefs(&uip,FALSE,NULL);
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
                     CFGAWEBSTR(MSG_SET_REQTITLE_OPENGUI),"gui",FALSE))
                  {  Disposeguiprefs(&uip);
                     Copyguiprefs(&defprefs.gui,&uip);
                     if(!Prefsconfiguseslegacyawebpath(path))
                     {  Loadguiprefs(&uip,FALSE,path);
                     }
                     Setdata();
                     FREE(path);
                  }
                  break;
               case MID_SAVEAS:
                  if(path=Filereq(winobject,window,toplayout,
                     CFGAWEBSTR(MSG_SET_REQTITLE_SAVEGUI),"gui",TRUE))
                  {  Copydata();
                     Saveguiprefs(&uip,FALSE,path);
                     FREE(path);
                  }
                  break;
               case MID_QUIT:
                  done=TRUE;
                  break;
               case MID_DEFAULTS:
                  Disposeguiprefs(&uip);
                  Copyguiprefs(&defprefs.gui,&uip);
                  Setdata();
                  break;
               case MID_LASTSAVED:
                  Disposeguiprefs(&uip);
                  Copyguiprefs(&defprefs.gui,&uip);
                  if(!Prefsconfiguseslegacyawebpath(NULL))
                  {  Loadguiprefs(&uip,FALSE,NULL);
                  }
                  Setdata();
                  break;
               case MID_RESTORE:
                  Disposeguiprefs(&uip);
                  Copyguiprefs(&orguip,&uip);
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
                  cfgcommand+=CFGCLASS_CLASSACT;
                  break;
               case MID_SNAPSHOT:
                  Dimensions(window,&setprefs.guix);
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
      {  Disposeguiprefs(&uip);
         Copyguiprefs(&defprefs.gui,&uip);
         if(!Prefsconfiguseslegacyawebpath(NULL))
         {  Loadguiprefs(&uip,FALSE,NULL);
         }
         Setdata();
         Disposeguiprefs(&orguip);
         Copyguiprefs(&uip,&orguip);
         tested=FALSE;
      }
   }
   if(done)
   {  if(tested)
      {  Disposeguiprefs(&uip);
         Copyguiprefs(&orguip,&uip);
         if(!endmode) endmode=PGID_USE;
      }
      Closegui();
   }
   return (BOOL)!done;
}

void Closegui(void)
{  uimask=0;
   if(window)
   {  ClearMenuStrip(window);
      Dimensions(window,&setprefs.guix);
   }
   if(winobject) DisposeObject(winobject);winobject=NULL;window=NULL;
   if(menubar) FreeMenus(menubar);menubar=NULL;
   if(endmode)
   {  if(endmode==PGID_SAVE) Saveguiprefs(&uip,TRUE,NULL);
      Saveguiprefs(&uip,FALSE,NULL);
   }
   endmode=0;
   Freeclicktablist(&tablist);
   Freebrowserlist(&menulist);
   Freechooserlist(&menutypelist);
   Freechooserlist(&menuhelplist);
   Freebrowserlist(&butlist);
   Freechooserlist(&buthelplist);
   Freechooserlist(&puptypelist);
   Freebrowserlist(&puplist);
   Freechooserlist(&pupihelplist);
   Freechooserlist(&puplhelplist);
   Freechooserlist(&pupfhelplist);
   Freebrowserlist(&kbdlist);
   Freechooserlist(&kbdhelplist);
   Freebrowserlist(&navlist);
   Freechooserlist(&navhelplist);
   Disposeguiprefs(&uip);
   Disposeguiprefs(&orguip);
   if(nreq.nr_Name) EndNotify(&nreq);
   memset(&nreq,0,sizeof(nreq));
   if(nport) DeleteMsgPort(nport);nport=NULL;
}
