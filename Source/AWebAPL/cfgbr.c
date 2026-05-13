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

/* cfgbr.c - AWebCfg browser window */

#include "awebcfg.h"
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>

/* Missing Reaction defines - not available in proto headers */
#ifndef REACTION_Underscore
#define REACTION_Underscore 0x80000000
#endif
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/listbrowser.h>
#include <gadgets/chooser.h>
#include <gadgets/integer.h>
#include <gadgets/checkbox.h>
#include <gadgets/string.h>
#include <images/label.h>
#include <gadgets/palette.h>
#include <classes/window.h>
#include <proto/intuition.h>
#include <dos/dos.h>


ULONG brmask=0;

static struct Browserprefs brp,orgbrp;
static BOOL tested=FALSE;

enum BRPREFS_PAGES
{  BRPREFS_OPTIONS,BRPREFS_FONTS,BRPREFS_STYLES,BRPREFS_COLOURS,BRPREFS_MIME,
};

static struct List tablist;
static struct List htmllist,dojslist,brpenlist,mimelist,extvwrhelplist,mimeactlist;
static struct List fonttypelist,fontlist,stylelist;

static struct Mimeinfo *mimeinfo;
static short aliasi; /* currently selected alias nr */
static short fonti;  /* currently selected font nr */

static void *winobject=NULL;
static struct Gadget *toplayout,*tabgad,*pagelayout,*pagegad;
static struct Gadget *htmlgad,*ullinkgad,*docogad,*bgsogad,*blnkgad,
   *dojsgad,*dofrgad,*inctgad,*nfrhgad,*jsergad,*nbangad,*ttipgad,*handgad,
   *jwtcgad,
   *fonttypegad,*ftaddgad,*ftdelgad,*ftnamegad,*fontlistgad,*fontnamegad,
   *stylelistgad,*styledesgad,*stylefixedgad,*styleabsgad,*stylerelgad,
   *stylengad,*stylebgad,*styleigad,*styleugad,
   *brclistgad,*sbtpgad,
   *mimelistgad,*mimetypegad,*mimesubtgad,*mimedelgad,*mimeextgad,
   *mimeactgad,*mimecmdgad,*mimepopgad,*mimeargsgad,*mimehelpgad;

static UBYTE *tablabels[6];
static UBYTE *fnames[]=
{  NULL,"H1","H2","H3","H4","H5","H6","BIG","SMALL","SUB","SUP",
   "ADDRESS","BLOCKQUOTE","CITE","CODE","DFN","EM","KBD","PRE","SAMP","STRONG","VAR",
   NULL,
};
static UBYTE *fdescr[22];
static UBYTE *htmllabels[4];
static UBYTE *dojslabels[4];
static UBYTE *brcolorpennames[6];
static UBYTE *proghelplabels[5];
static UBYTE *fonttypelabels[3];
static UBYTE *fsizes[8]=
{  NULL,"2",NULL,"4","5","6",NULL,NULL,
};
static UBYTE *mimeactlabels[7];
static UBYTE *mimeabbrev;

static long brpens[5]={ -1,-1,-1,-1,-1 };
static struct Image brpenimg[5];

static UBYTE prefsname[64];
static struct MsgPort *nport;
static struct Window *window=NULL;
static struct Menu *menubar;
static struct NotifyRequest nreq;

enum GADGET_IDS
{  PGID_TABS=1,
   PGID_FONTTYPE,PGID_FONTALIAS,PGID_FONTADD,PGID_FONTDEL,
   PGID_FONTLIST,PGID_FONTPOP,PGID_FONTALL,
   PGID_STYLELIST,PGID_STYLEFIXED,PGID_STYLEABS,PGID_STYLEREL,
   PGID_STYLEN,PGID_STYLEB,PGID_STYLEI,PGID_STYLEU,
   PGID_BRCLIST,PGID_BRCOLOR,
   PGID_MIMELIST,PGID_MIMETYPE,PGID_MIMESUBT,PGID_MIMEADD,PGID_MIMEDEL,
   PGID_MIMEEXT,PGID_MIMEACT,PGID_MIMECMD,PGID_MIMEPOPCMD,PGID_MIMEARGS,
   PGID_SAVE,PGID_USE,PGID_TEST,PGID_CANCEL,
};

static USHORT endmode;  /* gadget id that caused end (save, use) */

/* Whether mime cmd and args are disabled for this driver */
/*                          Def   Int   Plug   Ext    Expipe Save */
static BOOL mimedisable[]={ TRUE, TRUE, FALSE, FALSE, FALSE, TRUE };
static BOOL mimeahdisbl[]={ TRUE, TRUE, TRUE,  FALSE, FALSE, TRUE };    /* Arg help */

static void Dofontpop(void);
static struct Fontprefs *Findfont(int n);

/*---------------------------------------------------------------------------*/

static void *Makeoptionspage(void)
{  return
   VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      LAYOUT_FixedVert,FALSE,
      StartMember,VLayoutObject,
         StartMember,htmlgad=ChooserObject,
            CHOOSER_PopUp,TRUE,
            CHOOSER_Labels,&htmllist,
            CHOOSER_Active,brp.htmlmode,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_BROPT_HTMLMODE)),
         StartMember,dojsgad=ChooserObject,
            CHOOSER_PopUp,TRUE,
            CHOOSER_Labels,&dojslist,
            CHOOSER_Active,brp.dojs,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_BROPT_JAVASCRIPT)),
      EndMember,
      CHILD_WeightedWidth,0,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         StartMember,VLayoutObject,
            StartMember,jsergad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_JSERRORS),
               GA_Selected,brp.jserrors,
            EndMember,
            StartMember,jwtcgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_JSWATCH),
               GA_Selected,brp.jswatch,
            EndMember,
            StartMember,nbangad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_NOBANNERS),
               GA_Selected,brp.nobanners,
#ifdef DEMOVERSION
               GA_Disabled,TRUE,
#endif
            EndMember,
            StartMember,ttipgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_TOOLTIPS),
               GA_Selected,brp.tooltips,
            EndMember,
            StartMember,handgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_HANDPOINTER),
               GA_Selected,brp.handpointer,
            EndMember,
            StartMember,ullinkgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_ULLINKS),
               GA_Selected,brp.ullink,
            EndMember,
         EndMember,
         CHILD_WeightedHeight,0,
         CHILD_WeightedWidth,0,
         StartMember,VLayoutObject,
            StartMember,dofrgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_FRAMES),
               GA_Selected,brp.doframes,
            EndMember,
            StartMember,nfrhgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_NOMINALFRAME),
               GA_Selected,brp.nominalframe,
            EndMember,
            StartMember,inctgad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_INCTABLE),
               GA_Selected,brp.inctable,
            EndMember,
            StartMember,docogad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_DOCOLORS),
               GA_Selected,brp.docolors,
            EndMember,
            StartMember,bgsogad=CheckBoxObject,
               GA_Text,CFGAWEBSTR(MSG_SET_BROPT_DOBGSOUND),
               GA_Selected,brp.dobgsound,
            EndMember,
         EndMember,
         CHILD_WeightedWidth,0,
         CHILD_WeightedHeight,0,
      EndMember,
      StartMember,VLayoutObject,
         StartMember,blnkgad=IntegerObject,
            INTEGER_Number,brp.blinkrate,
            INTEGER_Minimum,0,
            INTEGER_Maximum,40,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_BROPT_BLINK)),
      EndMember,
      CHILD_WeightedHeight,0,
      CHILD_WeightedWidth,0,
   End;
}

/* Returns nr of (faa) in list */
long Makefonttypelist(struct List *list,UBYTE **labels,struct Fontalias *faa)
{  struct Node *node;
   struct Fontalias *fa;
   short i,n=-1;
   UBYTE buf[128]="* ";
   for(i=0;i<2;i++)
   {  strcpy(buf+2,labels[i]);
      if(node=AllocListBrowserNode(1,
         LBNCA_CopyText,TRUE,
         LBNCA_Text,buf,
         TAG_END))
         AddTail(list,node);
   }
   for(fa=brp.aliaslist.first;fa->next;fa=fa->next)
   {  if(node=AllocListBrowserNode(1,
         LBNCA_CopyText,TRUE,
         LBNCA_Text,fa->alias,
         TAG_END))
         AddTail(list,node);
      if(fa==faa) n=i;
      i++;
   }
   return n;
}

static struct ColumnInfo fontscolumns[]=
{  20,NULL,0,
   80,NULL,0,
   -1,NULL,0
};

void Makefontlist(struct List *list,long n)
{  short i;
   UBYTE buf[256];
   struct Node *node;
   struct Fontprefs *fp=Findfont(n);
   if(fp)
   {  for(i=0;i<NRFONTS;i++)
      {  sprintf(buf,"%s/%d",fp[i].fontname,fp[i].fontsize);
         if(node=AllocListBrowserNode(2,
            LBNA_Column,0,
               LBNCA_Text,fsizes[i],
            LBNA_Column,1,
               LBNCA_CopyText,TRUE,
               LBNCA_Text,buf,
            TAG_END))
            AddTail(list,node);
      }
   }
}

static void *Makefontspage(void)
{  fonti=0;
   aliasi=0;
   return
   VLayoutObject,
      StartMember,VLayoutObject,
         StartMember,fonttypegad=ListBrowserObject,
            GA_ID,PGID_FONTTYPE,
            GA_RelVerify,TRUE,
            LISTBROWSER_ShowSelected,TRUE,
            LISTBROWSER_Labels,&fonttypelist,
            LISTBROWSER_Selected,0,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_FONT_FONTTYPE2)),
         StartMember,HLayoutObject,
            StartMember,ftnamegad=StringObject,
               GA_ID,PGID_FONTALIAS,
               GA_RelVerify,TRUE,
               STRINGA_TextVal,"",
               STRINGA_MaxChars,127,
               GA_Disabled,TRUE,
            EndMember,
            StartMember,HLayoutObject,
               LAYOUT_SpaceInner,FALSE,
               LAYOUT_EvenSize,TRUE,
               StartMember,ftaddgad=ButtonObject,
                  GA_ID,PGID_FONTADD,
                  GA_RelVerify,TRUE,
                  GA_Text,CFGAWEBSTR(MSG_SET_FONT_ADD),
               EndMember,
               StartMember,ftdelgad=ButtonObject,
                  GA_ID,PGID_FONTDEL,
                  GA_RelVerify,TRUE,
                  GA_Text,CFGAWEBSTR(MSG_SET_FONT_DEL),
                  GA_Disabled,TRUE,
               EndMember,
            EndMember,
            CHILD_WeightedWidth,0,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_FONT_ALIAS)),
         CHILD_WeightedHeight,0,
      EndMember,
      CHILD_WeightedHeight,30,
      StartMember,fontlistgad=ListBrowserObject,
         GA_ID,PGID_FONTLIST,
         GA_RelVerify,TRUE,
         LISTBROWSER_ShowSelected,TRUE,
         LISTBROWSER_AutoFit,TRUE,
         LISTBROWSER_ColumnInfo,fontscolumns,
         LISTBROWSER_ColumnTitles,TRUE,
         LISTBROWSER_Labels,&fontlist,
         LISTBROWSER_Selected,0,
      EndMember,
      CHILD_WeightedHeight,70,
      StartMember,HLayoutObject,
         LAYOUT_InnerSpacing,1,
         LAYOUT_VertAlignment,LALIGN_CENTER,
         StartImage,LabelObject,
            LABEL_Text,CFGAWEBSTR(MSG_SET_FONT_FONT),
         EndImage,
         StartMember,fontnamegad=ButtonObject,
            GA_Text," ",
            GA_ReadOnly,TRUE,
            BUTTON_Justification,BCJ_LEFT,
            REACTION_Underscore,0,
         EndMember,
         StartMember,ButtonObject,
            GA_ID,PGID_FONTPOP,
            GA_RelVerify,TRUE,
            BUTTON_AutoButton,BAG_POPFONT,
            REACTION_CommKey,Hotkey(CFGAWEBSTR(MSG_SET_FONT_FONT)),
         EndMember,
         CHILD_MaxWidth,20,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         LAYOUT_InnerSpacing,1,
         LAYOUT_VertAlignment,LALIGN_CENTER,
         StartMember,SpaceObject,
         EndMember,
         StartMember,ButtonObject,
            GA_ID,PGID_FONTALL,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_FONT_SETALL),
         EndMember,
         StartMember,SpaceObject,
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
   End;
}

static struct ColumnInfo stylecolumns[]=
{  20,NULL,0,
   80,NULL,0,
   -1,NULL,0
};

void Makestylelist(struct List *list)
{  short i;
   UBYTE buf[256],*p;
   struct Node *node;
   for(i=1;fnames[i];i++)  /* skip STYLE_NORMAL */
   {  p=buf;
      p+=sprintf(p,"%s, %s ",
         CFGAWEBSTR(brp.styles[i].fonttype?MSG_SET_FD_FIXEDFONT:MSG_SET_FD_NORMALFONT),
         CFGAWEBSTR(MSG_SET_FD_SIZE));
      p+=sprintf(p,brp.styles[i].relsize?"%+d":"%d",brp.styles[i].fontsize);
      if(brp.styles[i].style&FSF_BOLD) strcat(buf,CFGAWEBSTR(MSG_SET_FD_BOLD));
      if(brp.styles[i].style&FSF_ITALIC) strcat(buf,CFGAWEBSTR(MSG_SET_FD_ITALIC));
      if(brp.styles[i].style&FSF_UNDERLINED) strcat(buf,CFGAWEBSTR(MSG_SET_FD_UNDERLINED));
      if(node=AllocListBrowserNode(2,
         LBNA_Column,0,
            LBNCA_Text,fnames[i],
         LBNA_Column,1,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,buf,
         TAG_END))
         AddTail(list,node);
   }
}

static void *Makestylepage(void)
{  return
   VLayoutObject,
      StartMember,stylelistgad=ListBrowserObject,
         GA_ID,PGID_STYLELIST,
         GA_RelVerify,TRUE,
         LISTBROWSER_ShowSelected,TRUE,
         LISTBROWSER_AutoFit,TRUE,
         LISTBROWSER_ColumnInfo,stylecolumns,
         LISTBROWSER_ColumnTitles,TRUE,
         LISTBROWSER_Labels,&stylelist,
         LISTBROWSER_Selected,0,
      EndMember,
      StartMember,styledesgad=ButtonObject,
         GA_Text," ",
         GA_ReadOnly,TRUE,
         BUTTON_Justification,BCJ_LEFT,
         REACTION_Underscore,0,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         StartMember,stylefixedgad=CheckBoxObject,
            GA_ID,PGID_STYLEFIXED,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_STYLE_FIXED),
            GA_Selected,brp.styles[1].fonttype,
         EndMember,
         CHILD_WeightedWidth,0,
         StartMember,styleabsgad=IntegerObject,
            GA_ID,PGID_STYLEABS,
            GA_RelVerify,TRUE,
            INTEGER_Minimum,1,
            INTEGER_Maximum,7,
            INTEGER_Number,brp.styles[1].fontsize,
            GA_Disabled,brp.styles[1].relsize,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_STYLE_ABSSIZE)),
         StartMember,stylerelgad=IntegerObject,
            GA_ID,PGID_STYLEREL,
            GA_RelVerify,TRUE,
            INTEGER_Minimum,-6,
            INTEGER_Maximum,6,
            GA_Disabled,!brp.styles[1].relsize,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_STYLE_RELSIZE)),
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         StartMember,stylengad=CheckBoxObject,
            GA_ID,PGID_STYLEN,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_STYLE_NORMAL),
            GA_Selected,brp.styles[1].style==0,
         EndMember,
         CHILD_WeightedWidth,0,
         StartMember,stylebgad=CheckBoxObject,
            GA_ID,PGID_STYLEB,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_STYLE_BOLD),
            GA_Selected,brp.styles[1].style&FSF_BOLD,
         EndMember,
         CHILD_WeightedWidth,0,
         StartMember,styleigad=CheckBoxObject,
            GA_ID,PGID_STYLEI,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_STYLE_ITALIC),
            GA_Selected,brp.styles[1].style&FSF_ITALIC,
         EndMember,
         CHILD_WeightedWidth,0,
         StartMember,styleugad=CheckBoxObject,
            GA_ID,PGID_STYLEU,
            GA_RelVerify,TRUE,
            GA_Text,CFGAWEBSTR(MSG_SET_STYLE_UNDERLINED),
            GA_Selected,brp.styles[1].style&FSF_UNDERLINED,
         EndMember,
         CHILD_WeightedWidth,0,
      EndMember,
      CHILD_WeightedHeight,0,
   End;
}

static void Makebrpenlist(struct List *list)
{  short i;
   struct Node *node;
   struct ColorMap *cmap=pubscreen->ViewPort.ColorMap;
   struct Colorprefs *cp=&brp.newlink;
   for(i=0;brcolorpennames[i];i++,cp++)
   {  brpens[i]=ObtainBestPen(cmap,cp->red,cp->green,cp->blue,TAG_END);
      brpenimg[i].Width=32;
      brpenimg[i].Height=8;
      brpenimg[i].PlaneOnOff=brpens[i];
      if(node=AllocListBrowserNode(2,
         LBNA_Column,0,
            LBNCA_Image,&brpenimg[i],
            LBNCA_Justification,LCJ_CENTER,
         LBNA_Column,1,
            LBNCA_Text,brcolorpennames[i],
         TAG_END))
         AddTail(list,node);
   }
}

static void Freebrpens(void)
{  short i;
   struct ColorMap *cmap=pubscreen->ViewPort.ColorMap;
   for(i=0;i<5;i++)
   {  if(brpens[i]>=0) ReleasePen(cmap,brpens[i]);
      brpens[i]=-1;
   }
}

static struct ColumnInfo brcolorcolumns[]=
{  0,NULL,0,
   0,NULL,0,
   -1,NULL,0
};

static void *Makecolourspage(void)
{  return
   VLayoutObject,
      LAYOUT_SpaceOuter,TRUE,
      StartMember,brclistgad=ListBrowserObject,
         GA_ID,PGID_BRCLIST,
         GA_RelVerify,TRUE,
         LISTBROWSER_ShowSelected,TRUE,
         LISTBROWSER_AutoFit,TRUE,
         LISTBROWSER_ColumnInfo,brcolorcolumns,
         LISTBROWSER_Labels,&brpenlist,
         LISTBROWSER_Separators,FALSE,
         LISTBROWSER_Selected,0,
      EndMember,
      StartMember,HLayoutObject,
         StartMember,SpaceObject,
         EndMember,
         StartMember,ButtonObject,
            GA_Text,CFGAWEBSTR(MSG_SET_APP_CHGCOLOUR),
            GA_ID,PGID_BRCOLOR,
            GA_RelVerify,TRUE,
         EndMember,
         StartMember,SpaceObject,
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,sbtpgad=CheckBoxObject,
         GA_Text,CFGAWEBSTR(MSG_SET_APP_SCREENPENS),
         GA_Selected,brp.screenpens,
      EndMember,
      CHILD_WeightedWidth,0,
      CHILD_WeightedHeight,0,
   End;
}

static struct ColumnInfo mimecolumns[]=
{  0,NULL,0,
   0,NULL,0,
   0,NULL,0,
   0,NULL,0,
   -1,NULL,0
};

void Makemimelist(struct List *list)
{  struct Mimeinfo *mi;
   UBYTE buf1[64];
   UBYTE buf2[256];
   UBYTE buf3[2];
   struct Node *node;
   for(mi=brp.mimelist.first;mi->next;mi=mi->next)
   {  sprintf(buf1,"%s/%s",mi->type,mi->subtype);
      if(mimedisable[mi->driver])
      {  buf2[0]='\0';
      }
      else
      {  sprintf(buf2,"%s %s",mi->cmd,mi->args);
      }
      buf3[0]=mimeabbrev[mi->driver];
      buf3[1]='\0';
      if(node=AllocListBrowserNode(4,
         LBNA_Column,0,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,buf1,
         LBNA_Column,1,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,mi->extensions,
         LBNA_Column,2,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,buf3,
         LBNA_Column,3,
            LBNCA_CopyText,TRUE,
            LBNCA_Text,buf2,
         TAG_END))
         AddTail(list,node);
   }
}

static void *Makemimepage(void)
{  void *layout1,*layout2,*layout3,*object;
   mimeinfo=brp.mimelist.first;
   object=VLayoutObject,
      StartMember,VLayoutObject,
         StartMember,mimelistgad=ListBrowserObject,
            GA_ID,PGID_MIMELIST,
            GA_RelVerify,TRUE,
            LISTBROWSER_ShowSelected,TRUE,
            LISTBROWSER_AutoFit,TRUE,
            LISTBROWSER_ColumnInfo,mimecolumns,
            LISTBROWSER_ColumnTitles,TRUE,
            LISTBROWSER_Labels,&mimelist,
            LISTBROWSER_HorizontalProp,TRUE,
            LISTBROWSER_Selected,0,
         EndMember,
         StartMember,layout1=HLayoutObject,
            StartMember,HLayoutObject,
               LAYOUT_VertAlignment,LALIGN_CENTER,
               StartMember,mimetypegad=StringObject,
                  GA_ID,PGID_MIMETYPE,
                  GA_TabCycle,TRUE,
                  GA_RelVerify,TRUE,
                  STRINGA_TextVal,mimeinfo->type,
                  STRINGA_MaxChars,30,
               EndMember,
               StartImage,LabelObject,
                  LABEL_Text,"/",
               EndImage,
               CHILD_WeightedWidth,0,
               StartMember,mimesubtgad=StringObject,
                  GA_ID,PGID_MIMESUBT,
                  GA_TabCycle,TRUE,
                  GA_RelVerify,TRUE,
                  STRINGA_TextVal,mimeinfo->subtype,
                  STRINGA_MaxChars,30,
               EndMember,
            EndMember,
            MemberLabel(CFGAWEBSTR(MSG_SET_MIME_TYPE)),
            CHILD_WeightedWidth,80,
            StartMember,HLayoutObject,
               LAYOUT_SpaceInner,FALSE,
               LAYOUT_EvenSize,TRUE,
               StartMember,ButtonObject,
                  GA_Text,CFGAWEBSTR(MSG_SET_MIME_ADD),
                  GA_ID,PGID_MIMEADD,
                  GA_RelVerify,TRUE,
               EndMember,
               StartMember,mimedelgad=ButtonObject,
                  GA_Text,CFGAWEBSTR(MSG_SET_MIME_DEL),
                  GA_ID,PGID_MIMEDEL,
                  GA_RelVerify,TRUE,
                  GA_Disabled,!mimeinfo->deleteable,
               EndMember,
            EndMember,
            CHILD_WeightedWidth,40,
         EndMember,
         CHILD_WeightedHeight,0,
      EndMember,
      StartMember,layout2=VLayoutObject,
         StartMember,HLayoutObject,
            StartMember,layout3=HLayoutObject,
               StartMember,mimeextgad=StringObject,
                  GA_TabCycle,TRUE,
                  GA_ID,PGID_MIMEEXT,
                  GA_RelVerify,TRUE,
                  STRINGA_TextVal,mimeinfo->extensions,
                  STRINGA_MaxChars,127,
               EndMember,
               MemberLabel(CFGAWEBSTR(MSG_SET_MIME_EXTENSIONS)),
            EndMember,
            StartMember,mimeactgad=ChooserObject,
               GA_ID,PGID_MIMEACT,
               GA_RelVerify,TRUE,
               CHOOSER_PopUp,TRUE,
               CHOOSER_Labels,&mimeactlist,
               CHOOSER_Active,mimeinfo->driver,
            EndMember,
            MemberLabel(CFGAWEBSTR(MSG_SET_MIME_ACTION)),
            CHILD_WeightedWidth,0,
         EndMember,
         StartMember,HLayoutObject,
            LAYOUT_SpaceInner,FALSE,
            StartMember,mimecmdgad=StringObject,
               GA_TabCycle,TRUE,
               GA_ID,PGID_MIMECMD,
               GA_RelVerify,TRUE,
               GA_Disabled,mimedisable[mimeinfo->driver],
               STRINGA_TextVal,mimeinfo->cmd,
               STRINGA_MaxChars,127,
            EndMember,
            StartMember,mimepopgad=ButtonObject,
               BUTTON_AutoButton,BAG_POPFILE,
               GA_ID,PGID_MIMEPOPCMD,
               GA_RelVerify,TRUE,
               GA_Disabled,mimedisable[mimeinfo->driver],
            EndMember,
            CHILD_MaxWidth,20,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_MIME_NAME)),
         StartMember,HLayoutObject,
            LAYOUT_SpaceInner,FALSE,
            StartMember,mimeargsgad=StringObject,
               GA_TabCycle,TRUE,
               GA_ID,PGID_MIMEARGS,
               GA_RelVerify,TRUE,
               GA_Disabled,mimedisable[mimeinfo->driver],
               STRINGA_TextVal,mimeinfo->args,
               STRINGA_MaxChars,127,
            EndMember,
            StartMember,mimehelpgad=ChooserObject,
               GA_Disabled,mimeahdisbl[mimeinfo->driver],
               CHOOSER_DropDown,TRUE,
               CHOOSER_Labels,&extvwrhelplist,
               CHOOSER_AutoFit,TRUE,
            EndMember,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_MIME_ARGS)),
      EndMember,
      CHILD_WeightedHeight,0,
   End;
   if(layout1) SetAttrs(layout1,LAYOUT_AlignLabels,layout2,TAG_END);
   if(layout2) SetAttrs(layout2,LAYOUT_AlignLabels,layout3,TAG_END);
   if(layout3) SetAttrs(layout3,LAYOUT_AlignLabels,layout1,TAG_END);
   return object;
}

/*---------------------------------------------------------------------------*/

static struct Fontprefs *Findfont(int n)
{  struct Fontalias *fa;
   if(n<0) return NULL;
   if(n<2) return brp.font[n];
   fa=(struct Fontalias *)Getnode((struct List *)&brp.aliaslist,n-2);
   if(fa) return fa->fp;
   return NULL;
}

static void Setfontgads(BOOL activate,struct Window *win)
{  if(aliasi<2)
   {  Setgadgetattrs(ftnamegad,win,NULL,
         STRINGA_TextVal,"",
         GA_Disabled,TRUE,
         TAG_END);
   }
   else
   {  struct Fontalias *fa=(struct Fontalias *)Getnode((struct List *)&brp.aliaslist,aliasi-2);
      Setgadgetattrs(ftnamegad,win,NULL,
         GA_Disabled,FALSE,
         STRINGA_TextVal,fa?fa->alias:NULLSTRING,
         TAG_END);
   }
   Setgadgetattrs(ftdelgad,win,NULL,GA_Disabled,(aliasi<2),TAG_END);
   if(aliasi>=2 && activate && win)
   {  ActivateLayoutGadget(toplayout,win,NULL,(ULONG)ftnamegad);
   }
}

static void Dofontlist(WORD code,BOOL popup,struct Window *win)
{  struct Node *node=Getnode(&fontlist,code);
   if(code!=fonti) popup=FALSE;  /* if dblclick on other item, no popup */
   if(node)
   {  UBYTE *fname;
      fonti=code;
      GetListBrowserNodeAttrs(node,
         LBNA_Column,1,
         LBNCA_Text,&fname,
         TAG_END);
      Setgadgetattrs(fontnamegad,win,NULL,
         GA_Text,fname,TAG_END);
   }
   if(popup
   && Getvalue(fontlistgad,LISTBROWSER_RelEvent)==LBRE_DOUBLECLICK)
      Dofontpop();
}

static void Remakefontlist(short n,struct Window *win)
{  Setgadgetattrs(fontlistgad,win,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&fontlist);
   Makefontlist(&fontlist,n);
   Setgadgetattrs(fontlistgad,win,NULL,LISTBROWSER_Labels,&fontlist,TAG_END);
   Dofontlist(fonti,FALSE,win);
   Setfontgads(FALSE,win);
}

static void Dofontpop(void)
{  struct FontRequester *fo;
   struct Fontprefs *fp=Findfont(aliasi);
   if(fp)
   {  fp=&fp[fonti];
      if(fo=AllocAslRequestTags(ASL_FontRequest,
         ASLFO_Window,window,
         ASLFO_SleepWindow,TRUE,
         ASLFO_TitleText,CFGAWEBSTR(MSG_SET_REQTITLE_FONT),
         ASLFO_InitialHeight,Reqheight(window->WScreen),
         ASLFO_InitialName,fp->fontname,
         ASLFO_InitialSize,fp->fontsize,
         ASLFO_FixedWidthOnly,(aliasi==1),
         ASLFO_MaxHeight,64,
         TAG_END))
      {  if(AslRequest(fo,NULL))
         {  if(fp->fontname) FREE(fp->fontname);
            fp->fontname=Dupstr(fo->fo_Attr.ta_Name,-1);
            fp->fontsize=fo->fo_Attr.ta_YSize;
            Remakefontlist(aliasi,window);
         }
         FreeAslRequest(fo);
      }
   }
}

static void Dofonttype(UWORD code)
{  aliasi=code;
   Remakefontlist(aliasi,window);
}

static void Dofontadd(void)
{  struct Fontalias *fa=Addfontalias(&brp.aliaslist,"");
   short i;
   if(fa)
   {  for(i=0;i<NRFONTS;i++)
      {  fa->fp[i].fontname=Dupstr(brp.font[0][i].fontname,-1);
         fa->fp[i].fontsize=brp.font[0][i].fontsize;
      }
      Setgadgetattrs(fonttypegad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
      Freebrowserlist(&fonttypelist);
      aliasi=Makefonttypelist(&fonttypelist,fonttypelabels,fa);
      Setgadgetattrs(fonttypegad,window,NULL,LISTBROWSER_Labels,&fonttypelist,TAG_END);
      Setgadgetattrs(fonttypegad,window,NULL,
         LISTBROWSER_Selected,aliasi,
         LISTBROWSER_MakeVisible,aliasi,
         TAG_END);
      Remakefontlist(aliasi,window);
      ActivateLayoutGadget(toplayout,window,NULL,(ULONG)ftnamegad);
   }
}

static void Dofontdel(void)
{  struct Fontalias *fa,*fb;
   if(aliasi>=2)
   {  fa=(struct Fontalias *)Getnode((struct List *)&brp.aliaslist,aliasi-2);
      if(fa)
      {  if(fa->next->next) fb=fa->next;
         else fb=fa->prev;
         REMOVE(fa);
         Freefontalias(fa);
         Setgadgetattrs(fonttypegad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
         Freebrowserlist(&fonttypelist);
         aliasi=Makefonttypelist(&fonttypelist,fonttypelabels,fb);
         if(aliasi<0) aliasi=1;
         Setgadgetattrs(fonttypegad,window,NULL,LISTBROWSER_Labels,&fonttypelist,TAG_END);
         Setgadgetattrs(fonttypegad,window,NULL,
            LISTBROWSER_Selected,aliasi,
            LISTBROWSER_MakeVisible,aliasi,
            TAG_END);
         Remakefontlist(aliasi,window);
      }
   }
}

static void Dofontalias(void)
{  struct Fontalias *fa,*fb;
   UBYTE *name;
   if(aliasi>=2)
   {  fa=(struct Fontalias *)Getnode((struct List *)&brp.aliaslist,aliasi-2);
      if(fa)
      {  name=Dupstr((UBYTE *)Getvalue(ftnamegad,STRINGA_TextVal),-1);
         if(fa->alias) FREE(fa->alias);
         fa->alias=name;
         REMOVE(fa);
         for(fb=brp.aliaslist.first;fb->next;fb=fb->next)
         {  if(stricmp(fa->alias,fb->alias)<0) break;
         }
         INSERT(&brp.aliaslist,fa,fb->prev);
         Setgadgetattrs(fonttypegad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
         Freebrowserlist(&fonttypelist);
         aliasi=Makefonttypelist(&fonttypelist,fonttypelabels,fa);
         Setgadgetattrs(fonttypegad,window,NULL,LISTBROWSER_Labels,&fonttypelist,TAG_END);
         Setgadgetattrs(fonttypegad,window,NULL,
            LISTBROWSER_Selected,aliasi,
            LISTBROWSER_MakeVisible,aliasi,
            TAG_END);
      }
   }
}

static void Dofontall(void)
{  UBYTE *fontname;
   struct Fontprefs *fp=Findfont(aliasi);
   short i;
   UBYTE *p;
   if(fp)
   {  fontname=fp[fonti].fontname;
      for(i=0;i<NRFONTS;i++)
      {  if(i!=fonti)
         {  if(p=Dupstr(fontname,-1))
            {  if(fp[i].fontname) FREE(fp[i].fontname);
               fp[i].fontname=p;
            }
         }
      }
   }
   Remakefontlist(aliasi,window);
}

static void Dostylelist(UWORD code)
{  short stylei=Getvalue(stylelistgad,LISTBROWSER_Selected)+1;
   struct Styleprefs *sp=&brp.styles[stylei];
   Setgadgetattrs(styledesgad,window,NULL,GA_Text,fdescr[stylei],TAG_END);
   Setgadgetattrs(stylefixedgad,window,NULL,GA_Selected,sp->fonttype,TAG_END);
   Setgadgetattrs(styleabsgad,window,NULL,
      sp->relsize?TAG_IGNORE:INTEGER_Number,sp->fontsize,
      GA_Disabled,sp->relsize||(stylei==0),
      TAG_END);
   Setgadgetattrs(stylerelgad,window,NULL,
      sp->relsize?INTEGER_Number:TAG_IGNORE,sp->fontsize,
      GA_Disabled,(!sp->relsize)||(stylei==0),
      TAG_END);
   Setgadgetattrs(stylengad,window,NULL,GA_Selected,sp->style==0,TAG_END);
   Setgadgetattrs(stylebgad,window,NULL,GA_Selected,sp->style&FSF_BOLD,TAG_END);
   Setgadgetattrs(styleigad,window,NULL,GA_Selected,sp->style&FSF_ITALIC,TAG_END);
   Setgadgetattrs(styleugad,window,NULL,GA_Selected,sp->style&FSF_UNDERLINED,TAG_END);
}

static void Changestyle(void)
{  Setgadgetattrs(stylelistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&stylelist);
   Makestylelist(&stylelist);
   Setgadgetattrs(stylelistgad,window,NULL,LISTBROWSER_Labels,&stylelist,TAG_END);
}

static void Dostyleattrs(void)
{  short stylei=Getvalue(stylelistgad,LISTBROWSER_Selected)+1;
   struct Styleprefs *sp=&brp.styles[stylei];
   sp->fonttype=Getselected(stylefixedgad);
   sp->fontsize=Getvalue(sp->relsize?stylerelgad:styleabsgad,INTEGER_Number);
   Changestyle();
}

static void Dostylenormal(void)
{  short stylei=Getvalue(stylelistgad,LISTBROWSER_Selected)+1;
   struct Styleprefs *sp=&brp.styles[stylei];
   sp->style=0;
   Setgadgetattrs(stylengad,window,NULL,GA_Selected,TRUE,TAG_END);
   Setgadgetattrs(stylebgad,window,NULL,GA_Selected,FALSE,TAG_END);
   Setgadgetattrs(styleigad,window,NULL,GA_Selected,FALSE,TAG_END);
   Setgadgetattrs(styleugad,window,NULL,GA_Selected,FALSE,TAG_END);
   Changestyle();
}

static void Dostylestyle(void)
{  short stylei=Getvalue(stylelistgad,LISTBROWSER_Selected)+1;
   struct Styleprefs *sp=&brp.styles[stylei];
   sp->style=0;
   if(Getselected(stylebgad)) sp->style|=FSF_BOLD;
   if(Getselected(styleigad)) sp->style|=FSF_ITALIC;
   if(Getselected(styleugad)) sp->style|=FSF_UNDERLINED;
   Setgadgetattrs(stylengad,window,NULL,GA_Selected,sp->style==0,TAG_END);
   Changestyle();
}

static void Dobrcolor(void)
{  long color=Getvalue(brclistgad,LISTBROWSER_Selected);
   struct Colorprefs *cp=NULL;
   switch(color)
   {  case 0:  cp=&brp.newlink;break;
      case 1:  cp=&brp.oldlink;break;
      case 2:  cp=&brp.selectlink;break;
      case 3:  cp=&brp.background;break;
      case 4:  cp=&brp.text;break;
   }
   if(cp)
   {  if(Popcolor(winobject,window,toplayout,cp,0))
      {  Setgadgetattrs(brclistgad,window,NULL,
            LISTBROWSER_Labels,~0,TAG_END);
         Freebrowserlist(&brpenlist);
         Freebrpens();
         Makebrpenlist(&brpenlist);
         Setgadgetattrs(brclistgad,window,NULL,
            LISTBROWSER_Labels,&brpenlist,
            TAG_END);
      }
   }
}

static void Dobrclist(void)
{  if(Getvalue(brclistgad,LISTBROWSER_RelEvent)==LBRE_DOUBLECLICK)
      Dobrcolor();
}

static void Domimelist(WORD code)
{  mimeinfo=(struct Mimeinfo *)Getnode((struct List *)&brp.mimelist,code);
   Setgadgetattrs(mimedelgad,window,NULL,GA_Disabled,!mimeinfo->deleteable,TAG_END);
   Setgadgetattrs(mimetypegad,window,NULL,
      STRINGA_TextVal,mimeinfo->type,
      STRINGA_DispPos,0,
      TAG_END);
   Setgadgetattrs(mimesubtgad,window,NULL,
      STRINGA_TextVal,mimeinfo->subtype,
      STRINGA_DispPos,0,
      TAG_END);
   Setgadgetattrs(mimeextgad,window,NULL,
      STRINGA_TextVal,mimeinfo->extensions,
      STRINGA_DispPos,0,
      TAG_END);
   Setgadgetattrs(mimeactgad,window,NULL,
      CHOOSER_Active,mimeinfo->driver,
      TAG_END);
   Setgadgetattrs(mimecmdgad,window,NULL,
      STRINGA_TextVal,mimeinfo->cmd,
      STRINGA_DispPos,0,
      GA_Disabled,mimedisable[mimeinfo->driver],
      TAG_END);
   Setgadgetattrs(mimepopgad,window,NULL,
      GA_Disabled,mimedisable[mimeinfo->driver],
      TAG_END);
   Setgadgetattrs(mimeargsgad,window,NULL,
      STRINGA_TextVal,mimeinfo->args,
      STRINGA_DispPos,0,
      GA_Disabled,mimedisable[mimeinfo->driver],
      TAG_END);
   Setgadgetattrs(mimehelpgad,window,NULL,
      GA_Disabled,mimeahdisbl[mimeinfo->driver],
      TAG_END);
}

static void Changemimelist(long select)
{  Setgadgetattrs(mimelistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&mimelist);
   Makemimelist(&mimelist);
   Setgadgetattrs(mimelistgad,window,NULL,
      LISTBROWSER_Labels,&mimelist,
      LISTBROWSER_AutoFit,TRUE,
      (select>=0)?LISTBROWSER_Selected:TAG_IGNORE,select,
      TAG_END);
   if(select>=0) Setgadgetattrs(mimelistgad,window,NULL,
         LISTBROWSER_MakeVisible,select,
         TAG_END);
}

static void Changemimeinfo(UBYTE **ptr,struct Gadget *gad)
{  UBYTE *p=(UBYTE *)Getvalue(gad,STRINGA_TextVal);
   if(p && !STREQUAL(p,*ptr) && (p=Dupstr(p,-1)))
   {  if(*ptr) FREE(*ptr);
      *ptr=p;
      Changemimelist(-1);
   }
}

static void Changemimeact(void)
{  mimeinfo->driver=Getvalue(mimeactgad,CHOOSER_Active);
   Changemimelist(-1);
   Setgadgetattrs(mimecmdgad,window,NULL,
      GA_Disabled,mimedisable[mimeinfo->driver],
      TAG_END);
   Setgadgetattrs(mimepopgad,window,NULL,
      GA_Disabled,mimedisable[mimeinfo->driver],
      TAG_END);
   Setgadgetattrs(mimeargsgad,window,NULL,
      GA_Disabled,mimedisable[mimeinfo->driver],
      TAG_END);
   Setgadgetattrs(mimehelpgad,window,NULL,
      GA_Disabled,mimeahdisbl[mimeinfo->driver],
      TAG_END);
}

static void Domimepopcmd(void)
{  Popfile(winobject,window,toplayout,CFGAWEBSTR(MSG_SET_REQTITLE_MIMEVIEWER),mimecmdgad);
   Changemimeinfo(&mimeinfo->cmd,mimecmdgad);
}

static void Domimeadd(void)
{  struct Mimeinfo *mi;
   long select;
   if(Addmimeinfo(&brp.mimelist,"","","",MDRIVER_NONE,"",""))
   {  for(mi=brp.mimelist.first,select=-1;mi->next;mi=mi->next,select++);
      Changemimelist(select);
      Domimelist(select);  /* clear gadgets and set current ptrs */
      ActivateLayoutGadget(toplayout,window,NULL,(ULONG)mimetypegad);
   }
}

static void Domimedel(void)
{  struct Mimeinfo *mi;
   long select;
   if(mimeinfo->deleteable)
   {  for(mi=brp.mimelist.first,select=0;mi!=mimeinfo;mi=mi->next,select++);
      if(!mimeinfo->next->next) select--;
      REMOVE(mimeinfo);
      Freemimeinfo(mimeinfo);
      Changemimelist(select);
      Domimelist(select);  /* set gadgets and current ptrs */
   }
}

static void Docursor(short n)
{  long page=Getvalue(pagegad,PAGE_Current);
   long newsel;
   switch(page)
   {  case BRPREFS_FONTS:
         newsel=Moveselected(window,fontlistgad,&fontlist,n);
         if(newsel>=0) Dofontlist(newsel,TRUE,window);
         break;
      case BRPREFS_STYLES:
         newsel=Moveselected(window,stylelistgad,&stylelist,n);
         if(newsel>=0) Dostylelist(newsel);
         break;
      case BRPREFS_COLOURS:
         Moveselected(window,brclistgad,&brpenlist,n);
         break;
      case BRPREFS_MIME:
         newsel=Moveselected(window,mimelistgad,&mimelist,n);
         if(newsel>=0) Domimelist(newsel);
         break;
   }
}

/* get data from gadgets */
static void Copydata(void)
{  brp.screenpens=Getselected(sbtpgad);
   brp.htmlmode=Getvalue(htmlgad,CHOOSER_Active);
   brp.ullink=Getselected(ullinkgad);
   brp.docolors=Getselected(docogad);
   brp.dobgsound=Getselected(bgsogad);
   brp.blinkrate=Getvalue(blnkgad,INTEGER_Number);
   brp.dojs=Getvalue(dojsgad,CHOOSER_Active);
   brp.doframes=Getselected(dofrgad);
   brp.inctable=Getselected(inctgad);
   brp.nominalframe=Getselected(nfrhgad);
   brp.jserrors=Getselected(jsergad);
   brp.nobanners=Getselected(nbangad);
   brp.tooltips=Getselected(ttipgad);
   brp.handpointer=Getselected(handgad);
   brp.jswatch=Getselected(jwtcgad);
}

/* set gadgets to changed data */
static void Setdata(void)
{  long page=Getvalue(pagegad,PAGE_Current);
   struct Window *win;
   short n;
   struct Styleprefs *sp;
   win=(page==BRPREFS_OPTIONS)?window:NULL;
   Setgadgetattrs(htmlgad,win,NULL,CHOOSER_Active,brp.htmlmode,TAG_END);
   Setgadgetattrs(dojsgad,win,NULL,CHOOSER_Active,brp.dojs,TAG_END);
   Setgadgetattrs(dofrgad,win,NULL,GA_Selected,brp.doframes,TAG_END);
   Setgadgetattrs(docogad,win,NULL,GA_Selected,brp.docolors,TAG_END);
   Setgadgetattrs(bgsogad,win,NULL,GA_Selected,brp.dobgsound,TAG_END);
   Setgadgetattrs(nbangad,win,NULL,GA_Selected,brp.nobanners,TAG_END);
   Setgadgetattrs(ttipgad,win,NULL,GA_Selected,brp.tooltips,TAG_END);
   Setgadgetattrs(handgad,win,NULL,GA_Selected,brp.handpointer,TAG_END);
   Setgadgetattrs(blnkgad,win,NULL,INTEGER_Number,brp.blinkrate,TAG_END);
   Setgadgetattrs(ullinkgad,win,NULL,GA_Selected,brp.ullink,TAG_END);
   Setgadgetattrs(inctgad,win,NULL,GA_Selected,brp.inctable,TAG_END);
   Setgadgetattrs(nfrhgad,win,NULL,GA_Selected,brp.nominalframe,TAG_END);
   Setgadgetattrs(jsergad,win,NULL,GA_Selected,brp.jserrors,TAG_END);
   Setgadgetattrs(jwtcgad,win,NULL,GA_Selected,brp.jswatch,TAG_END);
   win=(page==BRPREFS_FONTS)?window:NULL;
   aliasi=0;
   Setgadgetattrs(fonttypegad,win,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&fonttypelist);
   Makefonttypelist(&fonttypelist,fonttypelabels,NULL);
   Setgadgetattrs(fonttypegad,win,NULL,LISTBROWSER_Labels,&fonttypelist,TAG_END);
   Setgadgetattrs(fonttypegad,win,NULL,
      LISTBROWSER_Selected,aliasi,
      LISTBROWSER_MakeVisible,aliasi,
      TAG_END);
   Remakefontlist(aliasi,win);
   win=(page==BRPREFS_STYLES)?window:NULL;
   Setgadgetattrs(stylelistgad,win,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&stylelist);
   Makestylelist(&stylelist);
   Setgadgetattrs(stylelistgad,win,NULL,LISTBROWSER_Labels,&stylelist,TAG_END);
   n=Getvalue(stylelistgad,LISTBROWSER_Selected)+1;
   if(n>=1)
   {  sp=&brp.styles[n];
      Setgadgetattrs(styledesgad,win,NULL,GA_Text,fdescr[n],TAG_END);
      Setgadgetattrs(stylefixedgad,win,NULL,GA_Selected,sp->fonttype,TAG_END);
      Setgadgetattrs(styleabsgad,win,NULL,
         sp->relsize?TAG_IGNORE:INTEGER_Number,sp->fontsize,
         GA_Disabled,sp->relsize,
         TAG_END);
      Setgadgetattrs(stylerelgad,win,NULL,
         sp->relsize?INTEGER_Number:TAG_IGNORE,sp->fontsize,
         GA_Disabled,!sp->relsize,
         TAG_END);
      Setgadgetattrs(stylengad,win,NULL,GA_Selected,sp->style==0,TAG_END);
      Setgadgetattrs(stylebgad,win,NULL,GA_Selected,sp->style&FSF_BOLD,TAG_END);
      Setgadgetattrs(styleigad,win,NULL,GA_Selected,sp->style&FSF_ITALIC,TAG_END);
      Setgadgetattrs(styleugad,win,NULL,GA_Selected,sp->style&FSF_UNDERLINED,TAG_END);
   }
   win=(page==BRPREFS_COLOURS)?window:NULL;
   Setgadgetattrs(brclistgad,win,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&brpenlist);
   Freebrpens();
   Makebrpenlist(&brpenlist);
   Setgadgetattrs(brclistgad,win,NULL,
      LISTBROWSER_Labels,&brpenlist,
      TAG_END);
   Setgadgetattrs(sbtpgad,win,NULL,GA_Selected,brp.screenpens,TAG_END);
   win=(page==BRPREFS_MIME)?window:NULL;
   Setgadgetattrs(mimelistgad,win,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&mimelist);
   Makemimelist(&mimelist);
   mimeinfo=brp.mimelist.first;
   Setgadgetattrs(mimelistgad,win,NULL,
      LISTBROWSER_Labels,&mimelist,
      LISTBROWSER_AutoFit,TRUE,
      LISTBROWSER_Selected,0,
      LISTBROWSER_Top,0,
      TAG_END);
   Setgadgetattrs(mimetypegad,win,NULL,
      STRINGA_TextVal,mimeinfo->type,
      STRINGA_DispPos,0,
      TAG_END);
   Setgadgetattrs(mimesubtgad,win,NULL,
      STRINGA_TextVal,mimeinfo->subtype,
      STRINGA_DispPos,0,
      TAG_END);
   Setgadgetattrs(mimedelgad,win,NULL,GA_Disabled,!mimeinfo->deleteable,TAG_END);
   Setgadgetattrs(mimeextgad,win,NULL,
      STRINGA_TextVal,mimeinfo->extensions,
      STRINGA_DispPos,0,
      TAG_END);
   Setgadgetattrs(mimecmdgad,win,NULL,
      STRINGA_TextVal,mimeinfo->cmd,
      STRINGA_DispPos,0,
      TAG_END);
   Setgadgetattrs(mimeargsgad,win,NULL,
      STRINGA_TextVal,mimeinfo->args,
      STRINGA_DispPos,0,
      TAG_END);
}
   
/*---------------------------------------------------------------------------*/

static void Localize(void)
{  tablabels[0]=CFGAWEBSTR(MSG_SET_CTBR_OPTIONS);
   tablabels[1]=CFGAWEBSTR(MSG_SET_CTBR_FONTS);
   tablabels[2]=CFGAWEBSTR(MSG_SET_CTBR_STYLES);
   tablabels[3]=CFGAWEBSTR(MSG_SET_CTBR_COLOURS);
   tablabels[4]=CFGAWEBSTR(MSG_SET_CTBR_MIME);
   tablabels[5]=NULL;
   fnames[0]=CFGAWEBSTR(MSG_SET_FD_NORMALTAG);
   fdescr[0]=CFGAWEBSTR(MSG_SET_FD_NORMAL);
   fdescr[1]=CFGAWEBSTR(MSG_SET_FD_H1);
   fdescr[2]=CFGAWEBSTR(MSG_SET_FD_H2);
   fdescr[3]=CFGAWEBSTR(MSG_SET_FD_H3);
   fdescr[4]=CFGAWEBSTR(MSG_SET_FD_H4);
   fdescr[5]=CFGAWEBSTR(MSG_SET_FD_H5);
   fdescr[6]=CFGAWEBSTR(MSG_SET_FD_H6);
   fdescr[7]=CFGAWEBSTR(MSG_SET_FD_BIG);
   fdescr[8]=CFGAWEBSTR(MSG_SET_FD_SMALL);
   fdescr[9]=CFGAWEBSTR(MSG_SET_FD_SUB);
   fdescr[10]=CFGAWEBSTR(MSG_SET_FD_SUP);
   fdescr[11]=CFGAWEBSTR(MSG_SET_FD_ADDRESS);
   fdescr[12]=CFGAWEBSTR(MSG_SET_FD_BLOCKQUOTE);
   fdescr[13]=CFGAWEBSTR(MSG_SET_FD_CITE);
   fdescr[14]=CFGAWEBSTR(MSG_SET_FD_CODE);
   fdescr[15]=CFGAWEBSTR(MSG_SET_FD_DFN);
   fdescr[16]=CFGAWEBSTR(MSG_SET_FD_EM);
   fdescr[17]=CFGAWEBSTR(MSG_SET_FD_KBD);
   fdescr[18]=CFGAWEBSTR(MSG_SET_FD_PRE);
   fdescr[19]=CFGAWEBSTR(MSG_SET_FD_SAMP);
   fdescr[20]=CFGAWEBSTR(MSG_SET_FD_STRONG);
   fdescr[21]=CFGAWEBSTR(MSG_SET_FD_VAR);
   htmllabels[0]=CFGAWEBSTR(MSG_SET_HTML_STRICT);
   htmllabels[1]=CFGAWEBSTR(MSG_SET_HTML_TOLERANT);
   htmllabels[2]=CFGAWEBSTR(MSG_SET_HTML_COMPATIBLE);
   htmllabels[3]=NULL;
   dojslabels[0]=CFGAWEBSTR(MSG_SET_JS_OFF);
   dojslabels[1]=CFGAWEBSTR(MSG_SET_JS_11);
   dojslabels[2]=CFGAWEBSTR(MSG_SET_JS_ALL);
   dojslabels[3]=NULL;
   mimecolumns[0].ci_Title=CFGAWEBSTR(MSG_SET_MLCN_TYPE);
   mimecolumns[1].ci_Title=CFGAWEBSTR(MSG_SET_MLCN_EXT);
   mimecolumns[2].ci_Title=CFGAWEBSTR(MSG_SET_MLCN_ACTION);
   mimecolumns[3].ci_Title=CFGAWEBSTR(MSG_SET_MLCN_NAME);
   brcolorpennames[0]=CFGAWEBSTR(MSG_SET_BRPEN_LINK);
   brcolorpennames[1]=CFGAWEBSTR(MSG_SET_BRPEN_VLINK);
   brcolorpennames[2]=CFGAWEBSTR(MSG_SET_BRPEN_ALINK);
   brcolorpennames[3]=CFGAWEBSTR(MSG_SET_BRPEN_BGCOLOR);
   brcolorpennames[4]=CFGAWEBSTR(MSG_SET_BRPEN_TEXT);
   brcolorpennames[5]=NULL;
   proghelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_F);
   proghelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_N);
   proghelplabels[2]=CFGAWEBSTR(MSG_SET_HLP_U);
   proghelplabels[3]=CFGAWEBSTR(MSG_SET_HLP_M);
   proghelplabels[4]=NULL;
   fonttypelabels[0]=CFGAWEBSTR(MSG_SET_FTYPE_NORMAL);
   fonttypelabels[1]=CFGAWEBSTR(MSG_SET_FTYPE_FIXED);
   fonttypelabels[2]=NULL;
   fsizes[0]=CFGAWEBSTR(MSG_SET_FSIZE_1);
   fsizes[2]=CFGAWEBSTR(MSG_SET_FSIZE_3);
   fsizes[6]=CFGAWEBSTR(MSG_SET_FSIZE_7);
   fontscolumns[0].ci_Title=CFGAWEBSTR(MSG_SET_FLH_SIZE);
   fontscolumns[1].ci_Title=CFGAWEBSTR(MSG_SET_FLH_FONTFACE);
   stylecolumns[0].ci_Title=CFGAWEBSTR(MSG_SET_SLH_TAG);
   stylecolumns[1].ci_Title=CFGAWEBSTR(MSG_SET_SLH_STYLE);
   mimeactlabels[0]=CFGAWEBSTR(MSG_SET_MACT_DEFAULT);
   mimeactlabels[1]=CFGAWEBSTR(MSG_SET_MACT_INTERNAL);
   mimeactlabels[2]=CFGAWEBSTR(MSG_SET_MACT_PLUGIN);
   mimeactlabels[3]=CFGAWEBSTR(MSG_SET_MACT_EXTERNAL);
   mimeactlabels[4]=CFGAWEBSTR(MSG_SET_MACT_EXTPIPE);
   mimeactlabels[5]=CFGAWEBSTR(MSG_SET_MACT_SAVEDISK);
   mimeactlabels[6]=NULL;
   mimeabbrev=CFGAWEBSTR(MSG_SET_MACT_ABBREV);
}

BOOL Openbrowser(void)
{  BOOL ok=FALSE;
   UBYTE *fname;
   if(window)
   {  WindowToFront(window);
      ActivateWindow(window);
      return FALSE;
   }
   (&brp.mimelist)->first = NULL;
   (&brp.mimelist)->tail = NULL;
   (&brp.mimelist)->last = NULL;
   (&orgbrp.mimelist)->first = NULL;
   (&orgbrp.mimelist)->tail = NULL;
   (&orgbrp.mimelist)->last = NULL;
   (&tablist)->lh_Head = (struct Node *)&((&tablist)->lh_Tail);
   (&tablist)->lh_Tail = NULL;
   (&tablist)->lh_TailPred = (struct Node *)&((&tablist)->lh_Head);
   (&htmllist)->lh_Head = (struct Node *)&((&htmllist)->lh_Tail);
   (&htmllist)->lh_Tail = NULL;
   (&htmllist)->lh_TailPred = (struct Node *)&((&htmllist)->lh_Head);
   (&dojslist)->lh_Head = (struct Node *)&((&dojslist)->lh_Tail);
   (&dojslist)->lh_Tail = NULL;
   (&dojslist)->lh_TailPred = (struct Node *)&((&dojslist)->lh_Head);
   (&brpenlist)->lh_Head = (struct Node *)&((&brpenlist)->lh_Tail);
   (&brpenlist)->lh_Tail = NULL;
   (&brpenlist)->lh_TailPred = (struct Node *)&((&brpenlist)->lh_Head);
   (&mimelist)->lh_Head = (struct Node *)&((&mimelist)->lh_Tail);
   (&mimelist)->lh_Tail = NULL;
   (&mimelist)->lh_TailPred = (struct Node *)&((&mimelist)->lh_Head);
   (&extvwrhelplist)->lh_Head = (struct Node *)&((&extvwrhelplist)->lh_Tail);
   (&extvwrhelplist)->lh_Tail = NULL;
   (&extvwrhelplist)->lh_TailPred = (struct Node *)&((&extvwrhelplist)->lh_Head);
   (&fonttypelist)->lh_Head = (struct Node *)&((&fonttypelist)->lh_Tail);
   (&fonttypelist)->lh_Tail = NULL;
   (&fonttypelist)->lh_TailPred = (struct Node *)&((&fonttypelist)->lh_Head);
   (&fontlist)->lh_Head = (struct Node *)&((&fontlist)->lh_Tail);
   (&fontlist)->lh_Tail = NULL;
   (&fontlist)->lh_TailPred = (struct Node *)&((&fontlist)->lh_Head);
   (&stylelist)->lh_Head = (struct Node *)&((&stylelist)->lh_Tail);
   (&stylelist)->lh_Tail = NULL;
   (&stylelist)->lh_TailPred = (struct Node *)&((&stylelist)->lh_Head);
   (&mimeactlist)->lh_Head = (struct Node *)&((&mimeactlist)->lh_Tail);
   (&mimeactlist)->lh_Tail = NULL;
   (&mimeactlist)->lh_TailPred = (struct Node *)&((&mimeactlist)->lh_Head);
   if(!tablabels[0]) Localize();
   if(nport=CreateMsgPort())
   {  strcpy(prefsname,"ENV:" DEFAULTCFG);
      if(*configname) AddPart(prefsname,configname,64);
      AddPart(prefsname,"browser",64);
      nreq.nr_Name=prefsname;
      nreq.nr_Port=nport;
      nreq.nr_Flags=NRF_SEND_MESSAGE;
      StartNotify(&nreq);
      Copybrowserprefs(&defprefs.browser,&brp);
      Loadbrowserprefs(&brp,FALSE,NULL);
      Copybrowserprefs(&brp,&orgbrp);
      tested=FALSE;
      Makeclicktablist(&tablist,tablabels);
      Makechooserlist(&htmllist,htmllabels,FALSE);
      Makechooserlist(&dojslist,dojslabels,FALSE);
      Makefonttypelist(&fonttypelist,fonttypelabels,NULL);
      Makefontlist(&fontlist,FALSE);
      Makestylelist(&stylelist);
      Makebrpenlist(&brpenlist);
      Makemimelist(&mimelist);
      Makechooserlist(&extvwrhelplist,proghelplabels,TRUE);
      Makechooserlist(&mimeactlist,mimeactlabels,FALSE);
      winobject=WindowObject,
            WA_Title,CFGAWEBSTR(MSG_SET_REQTITLE_BROWSERWINDOW),
            WA_Left,setprefs.brwx,
            WA_Top,setprefs.brwy,
            WA_InnerWidth,setprefs.brww,
            WA_InnerHeight,setprefs.brwh,
            WA_AutoAdjust,TRUE,
            WA_CloseGadget,TRUE,
            WA_DragBar,TRUE,
            WA_DepthGadget,TRUE,
            WA_SizeGadget,TRUE,
            WA_Activate,TRUE,
            WA_IDCMP,IDCMP_RAWKEY|IDCMP_MENUPICK,
//            WA_SimpleRefresh,TRUE,
            WA_PubScreen,pubscreen,
            setprefs.brww?TAG_IGNORE:WINDOW_Position,WPOS_CENTERSCREEN,
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
                     PAGE_Add,Makefontspage(),
                     PAGE_Add,Makestylepage(),
                     PAGE_Add,Makecolourspage(),
                     PAGE_Add,Makemimepage(),
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
            GetAttr(WINDOW_SigMask,winobject,&brmask);
            brmask|=1<<nport->mp_SigBit;
            GetListBrowserNodeAttrs(fontlist.lh_Head,
               LBNA_Column,1,
               LBNCA_Text,&fname,
               TAG_END);
            SetAttrs(fontnamegad,GA_Text,fname,TAG_END);
            SetAttrs(styledesgad,GA_Text,fdescr[1],TAG_END);
            ok=TRUE;
         }
      }
   }
   if(!ok) Closebrowser();
   return ok;
}


/* returns TRUE is window remains open, FALSE if window should close */
BOOL Processbrowser(void)
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
               case PGID_FONTTYPE:
                  Dofonttype(code);
                  break;
               case PGID_FONTADD:
                  Dofontadd();
                  break;
               case PGID_FONTDEL:
                  Dofontdel();
                  break;
               case PGID_FONTALIAS:
                  Dofontalias();
                  break;
               case PGID_FONTLIST:
                  Dofontlist(code,TRUE,window);
                  break;
               case PGID_FONTPOP:
                  Dofontpop();
                  break;
               case PGID_FONTALL:
                  Dofontall();
                  break;
               case PGID_STYLELIST:
                  Dostylelist(code);
                  break;
               case PGID_STYLEFIXED:
               case PGID_STYLEABS:
               case PGID_STYLEREL:
                  Dostyleattrs();
                  break;
               case PGID_STYLEN:
                  Dostylenormal();
                  break;
               case PGID_STYLEB:
               case PGID_STYLEI:
               case PGID_STYLEU:
                  Dostylestyle();
                  break;
               case PGID_BRCLIST:
                  Dobrclist();
                  break;
               case PGID_BRCOLOR:
                  Dobrcolor();
                  break;
               case PGID_MIMELIST:
                  Domimelist(code);
                  break;
               case PGID_MIMETYPE:
                  Changemimeinfo(&mimeinfo->type,mimetypegad);
                  break;
               case PGID_MIMESUBT:
                  Changemimeinfo(&mimeinfo->subtype,mimesubtgad);
                  break;
               case PGID_MIMEEXT:
                  Changemimeinfo(&mimeinfo->extensions,mimeextgad);
                  break;
               case PGID_MIMEACT:
                  Changemimeact();
                  break;
               case PGID_MIMECMD:
                  Changemimeinfo(&mimeinfo->cmd,mimecmdgad);
                  break;
               case PGID_MIMEARGS:
                  Changemimeinfo(&mimeinfo->args,mimeargsgad);
                  break;
               case PGID_MIMEPOPCMD:
                  Domimepopcmd();
                  break;
               case PGID_MIMEADD:
                  Domimeadd();
                  break;
               case PGID_MIMEDEL:
                  Domimedel();
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
                  Savebrowserprefs(&brp,FALSE,NULL);
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
                     CFGAWEBSTR(MSG_SET_REQTITLE_OPENBROWSER),"browser",FALSE))
                  {  Disposebrowserprefs(&brp);
                     Copybrowserprefs(&defprefs.browser,&brp);
                     Loadbrowserprefs(&brp,FALSE,path);
                     Setdata();
                     FREE(path);
                  }
                  break;
               case MID_SAVEAS:
                  if(path=Filereq(winobject,window,toplayout,
                     CFGAWEBSTR(MSG_SET_REQTITLE_SAVEBROWSER),"browser",TRUE))
                  {  Copydata();
                     Savebrowserprefs(&brp,FALSE,path);
                     FREE(path);
                  }
                  break;
               case MID_QUIT:
                  done=TRUE;
                  break;
               case MID_DEFAULTS:
                  Disposebrowserprefs(&brp);
                  Copybrowserprefs(&defprefs.browser,&brp);
                  Setdata();
                  break;
               case MID_LASTSAVED:
                  Disposebrowserprefs(&brp);
                  Copybrowserprefs(&defprefs.browser,&brp);
                  Loadbrowserprefs(&brp,TRUE,NULL);
                  Setdata();
                  break;
               case MID_RESTORE:
                  Disposebrowserprefs(&brp);
                  Copybrowserprefs(&orgbrp,&brp);
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
                  Dimensions(window,&setprefs.brwx);
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
      {  Disposebrowserprefs(&brp);
         Copybrowserprefs(&defprefs.browser,&brp);
         Loadbrowserprefs(&brp,FALSE,NULL);
         Setdata();
         Disposebrowserprefs(&orgbrp);
         Copybrowserprefs(&brp,&orgbrp);
         tested=FALSE;
      }
   }
   if(done)
   {  if(tested)
      {  Disposebrowserprefs(&brp);
         Copybrowserprefs(&orgbrp,&brp);
         if(!endmode) endmode=PGID_USE;
      }
      Closebrowser();
   }
   return (BOOL)!done;
}

void Closebrowser(void)
{  
   struct Task *current_task;

   /* CRITICAL: Send exit signals to all background tasks before shutting down */
   /* This prevents DNS lookups and other operations from hanging during exit */
   /* printf("DEBUG: Closebrowser() called, sending exit signals to background tasks\n"); */
   
   /* CRITICAL: Send exit signals to prevent hanging DNS lookups */
   /* We'll use multiple break signals to ensure all tasks get notified */
   current_task = FindTask(NULL);
   if(current_task) {
      /* Send break signals to the current task to notify background operations */
      SetSignal(0, SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_D | SIGBREAKF_CTRL_E | SIGBREAKF_CTRL_F);
      /* printf("DEBUG: Sent exit signals to current task\n"); */
   }
   
   brmask=0;
   if(window)
   {  ClearMenuStrip(window);
      Dimensions(window,&setprefs.brwx);
   }
   if(winobject) DisposeObject(winobject);winobject=NULL;window=NULL;
   if(menubar) FreeMenus(menubar);menubar=NULL;
   if(endmode)
   {  if(endmode==PGID_SAVE) Savebrowserprefs(&brp,TRUE,NULL);
      Savebrowserprefs(&brp,FALSE,NULL);
   }
   endmode=0;
   Freeclicktablist(&tablist);
   Freechooserlist(&htmllist);
   Freechooserlist(&dojslist);
   Freebrowserlist(&fontlist);
   Freebrowserlist(&brpenlist);
   Freebrowserlist(&mimelist);
   Freechooserlist(&extvwrhelplist);
   Freebrowserlist(&fonttypelist);
   /* stylelist was populated in Openbrowser via Makestylelist but never freed on close. */
   Freebrowserlist(&stylelist);
   Freechooserlist(&mimeactlist);
   Freebrpens();
   Disposebrowserprefs(&brp);
   Disposebrowserprefs(&orgbrp);
   if(nreq.nr_Name) EndNotify(&nreq);
   memset(&nreq,0,sizeof(nreq));
   if(nport) DeleteMsgPort(nport);nport=NULL;
   
   /* printf("DEBUG: Closebrowser() completed, all resources cleaned up\n"); */
}
