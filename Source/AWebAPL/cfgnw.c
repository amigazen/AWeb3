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

/* cfgnw.c - AWebCfg network window */

#include "awebcfg.h"
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <reaction/reaction_author.h>
#include <intuition/intuition.h>
#include <images/bevel.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/listbrowser.h>
#include <classes/window.h>
#include <proto/intuition.h>
#include <proto/alib.h>

extern ULONG DoMethod(Object *obj, ULONG methodID, ...);

ULONG nwmask=0;

static struct Networkprefs nwp,orgnwp;
static BOOL tested=FALSE;

enum NWPREFS_PAGES
{  NWPREFS_OPTIONS,NWPREFS_PROGRAMS,NWPREFS_PROXY,NWPREFS_CACHE,NWPREFS_PRIVACY,
   NWPREFS_MAIL,
};

enum NWMAIL_PAGES
{  NWMAIL_ADDRESS,NWMAIL_MAIL,NWMAIL_NEWS,NWMAIL_OPTIONS,
};

static struct List tablist,mailtablist;
static struct List imgllist,cooklist,prxtplist,nwprgtplist,mailhelplist,ftphelplist,
   telnethelplist,newshelplist,starttcphelplist,endtcphelplist,caverifylist,
   nocachelist,noproxylist,nocookielist,srchhelplist,qhdrhelplist,spoofidlist;

static UBYTE *tablabels[7];
static UBYTE *mailtablabels[5];
static UBYTE *imgllabels[4];
static UBYTE *cooklabels[4];
static UBYTE *prxtplabels[]={ "HTTP","FTP","Gopher",
#ifndef DEMOVERSION
   "Telnet",
#endif
   NULL };
static UBYTE *nwprgtplabels[4]={ "telnet:",NULL,NULL,NULL };
static UBYTE *srchhelplabels[2];
static UBYTE *mailhelplabels[3];
static UBYTE *ftphelplabels[4];
static UBYTE *telnethelplabels[6];
static UBYTE *newshelplabels[3];
static UBYTE *starttcphelplabels[2];
static UBYTE *endtcphelplabels[2];
static UBYTE *caverifylabels[4];
static UBYTE *qhdrhelplabels[7];
static UBYTE *spoofidlabels[10]=
{  "(AWeb)","Mozilla/3.01","Mozilla/4.0","Mozilla/5.0","Mozilla/6.0",
   "MSIE/4.0","MSIE/5.0","MSIE/5.5","MSIE/6.0",NULL
};

static void *winobject=NULL;
static struct Gadget *toplayout,*tabgad,*pagelayout,*pagegad;
static struct Gadget *imglgad,*parigad,*maxdgad,*homegad,*loixgad,*srchgad,
   *sthpgad,*igsmgad,*asrhgad,*spofgad,*canigad,*rstigad;
static struct Gadget *prxtpgad,*proxygad,*lmpxgad,*pftpgad,*noprlistgad,*noprdelgad;
static struct Gadget *prgtpgad,*prgcmdgad,*prgargsgad,*prghelpgad;
static struct Gadget *capagad,*camegad,*cadigad,*frecgad,*frefgad,*cavegad,*cafrgad,
   *nocalistgad,*nocadelgad;
static struct Gadget *anongad,*fowagad,*spamgad,*ftpegad,*cookgad,*rfccgad,
   *nocolistgad,*nocodelgad;
static struct Gadget *mailtabgad,*mailpagelayout,*mailpagegad;
static struct Gadget *mnadgad,*mnfngad,*mnregad,*mnorgad,*mnsigad,*mnsipgad;
static struct Gadget *mnshgad,*mnmqgad,*mnmqhgad,*mnemgad,
   *mltcgad,*mltcpgad,*mltagad,*mltahgad;
static struct Gadget *mnnhgad,*mnnqgad,*mnnqhgad,*mnaugad,*mnapgad,*mnengad,
   *nwscgad,*nwscpgad,*nwsagad,*nwsahgad;
static struct Gadget *mnmagad,*mnsogad,*mnfrgad,*mnlogad,*mnprgad,*mnnngad;

static UBYTE prefsname1[64],prefsname2[64];
static struct MsgPort *nport;
static struct Window *window=NULL;
static struct Menu *menubar;
static struct NotifyRequest nreq1,nreq2;

enum GADGET_IDS
{  PGID_TABS=1,
   PGID_SPOOFIDPOP,
   PGID_NOCOLIST,PGID_NOCOADD,PGID_NOCODEL,
   PGID_PRXTP,PGID_PROXY,PGID_NOPRLIST,PGID_NOPRADD,PGID_NOPRDEL,
   PGID_NWPRGTP,PGID_NWPRGCMD,PGID_NWPRGPOP,PGID_NWPRGARGS,
   PGID_POPCAPA,PGID_CAVERIFY,PGID_NOCALIST,PGID_NOCAADD,PGID_NOCADEL,
   PGID_MAILTABS,PGID_POPMMSI,PGID_MNEM,PGID_POPMLTC,PGID_MNEN,PGID_POPNWSC,
   PGID_SAVE,PGID_USE,PGID_TEST,PGID_CANCEL,
};

static USHORT endmode;  /* gadget id that caused end (save, use) */

static ULONG prgpopmsg[]=
{  MSG_SET_REQTITLE_TELNET,
   MSG_SET_REQTITLE_STARTTCP,
   MSG_SET_REQTITLE_ENDTCP,
};

/*---------------------------------------------------------------------------*/

static void Makenocookielist(struct List *list)
{  
#ifndef DEMOVERSION
   struct Nocookie *nc;
   struct Node *node;
   for(nc=nwp.nocookie.first;nc->next;nc=nc->next)
   {  if(node=AllocListBrowserNode(1,
         LBNA_UserData,nc,
         LBNCA_CopyText,TRUE,
         LBNCA_Editable,TRUE,
         LBNCA_Text,nc->name,
         LBNCA_MaxChars,128,
         TAG_END))
         AddTail(list,node);
   }
#endif
}

static void *Makeoptionspage(void)
{  static UBYTE maxdkey[2],loixkey[2];
   void *object,*layout1,*layout2;
   maxdkey[0]=*Hotkey(CFGAWEBSTR(MSG_SET_NETWORK_MAXDISK));
   loixkey[0]=*Hotkey(CFGAWEBSTR(MSG_SET_NETWORK_LOCALINDEX));
   object=
   VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      StartMember,layout1=HLayoutObject,
         LAYOUT_VertAlignment,LALIGN_CENTER,
         StartMember,imglgad=ChooserObject,
            CHOOSER_PopUp,TRUE,
            CHOOSER_Labels,&imgllist,
            CHOOSER_Active,nwp.loadimg,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_NETWORK_IMGLOAD)),
         StartImage,LabelObject,
            LABEL_Text,CFGAWEBSTR(MSG_SET_NETWORK_LOCALINDEX),
         EndImage,
         StartMember,loixgad=StringObject,
            GA_TabCycle,TRUE,
            STRINGA_TextVal,nwp.localindex,
            STRINGA_MaxChars,127,
            REACTION_CommKey,loixkey,
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         LAYOUT_VertAlignment,LALIGN_CENTER,
         StartMember,parigad=IntegerObject,
            GA_TabCycle,TRUE,
            INTEGER_Minimum,1,
#ifndef DEMOVERSION
            INTEGER_Maximum,256,
#else
            INTEGER_Maximum,2,
#endif
            INTEGER_Number,nwp.maxconnect,
#ifdef LOCALONLY
            GA_Disabled,TRUE,
#endif
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_NETWORK_MAXCONN)),
         StartImage,LabelObject,
            LABEL_Text,CFGAWEBSTR(MSG_SET_NETWORK_MAXDISK),
         EndImage,
         StartMember,maxdgad=IntegerObject,
            GA_TabCycle,TRUE,
            INTEGER_Minimum,1,
#ifndef DEMOVERSION
            INTEGER_Maximum,256,
#else
            INTEGER_Maximum,2,
#endif
            INTEGER_Number,nwp.maxdiskread,
            REACTION_CommKey,maxdkey,
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,layout2=VLayoutObject,
         StartMember,homegad=StringObject,
            GA_TabCycle,TRUE,
            STRINGA_TextVal,nwp.homeurl,
            STRINGA_MaxChars,127,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_NETWORK_HOMEPAGE)),
         StartMember,HLayoutObject,
            LAYOUT_SpaceInner,FALSE,
            StartMember,srchgad=StringObject,
               GA_TabCycle,TRUE,
               STRINGA_TextVal,nwp.searchurl,
               STRINGA_MaxChars,127,
            EndMember,
            StartMember,ChooserObject,
               CHOOSER_DropDown,TRUE,
               CHOOSER_Labels,&srchhelplist,
               CHOOSER_AutoFit,TRUE,
            EndMember,
#ifdef DEMOVERSION
            GA_Disabled,TRUE,
#endif
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_NETWORK_AUTOSEARCHURL)),
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         StartMember,VLayoutObject,
            StartMember,VLayoutObject,
               StartMember,sthpgad=CheckBoxObject,
                  GA_Text,CFGAWEBSTR(MSG_SET_NETWORK_STARTHOME),
                  GA_Selected,nwp.starthomepage,
               EndMember,
               CHILD_WeightedWidth,0,
               StartMember,igsmgad=CheckBoxObject,
                  GA_Text,CFGAWEBSTR(MSG_SET_NETWORK_IGNORE),
                  GA_Selected,nwp.ignoremime,
#ifdef LOCALONLY
                  GA_Disabled,TRUE,
#endif
               EndMember,
               CHILD_WeightedWidth,0,
               StartMember,asrhgad=CheckBoxObject,
                  GA_Text,CFGAWEBSTR(MSG_SET_NETWORK_AUTOSEARCH),
                  GA_Selected,nwp.autosearch,
#ifdef DEMOVERSION
                  GA_Disabled,TRUE,
#endif
               EndMember,
               CHILD_WeightedWidth,0,
               StartMember,canigad=CheckBoxObject,
                  GA_Text,CFGAWEBSTR(MSG_SET_NETWORK_CONTANIM),
                  GA_Selected,nwp.contanim,
               EndMember,
               CHILD_WeightedWidth,0,
               StartMember,rstigad=CheckBoxObject,
                  GA_Text,CFGAWEBSTR(MSG_SET_NETWORK_RESTRICT_IMAGES),
                  GA_Selected,nwp.restrictimages,
               EndMember,
               CHILD_WeightedWidth,0,
            EndMember,
         EndMember,
         CHILD_WeightedWidth,0,
         CHILD_WeightedHeight,0,
         StartMember,VLayoutObject,
            LAYOUT_BevelStyle,BVS_GROUP,
            LAYOUT_SpaceOuter,TRUE,
            StartMember,HLayoutObject,
               LAYOUT_SpaceInner,FALSE,
               StartMember,spofgad=StringObject,
                  GA_TabCycle,TRUE,
                  STRINGA_TextVal,nwp.spoofid,
                  STRINGA_MaxChars,127,
               EndMember,
               StartMember,ChooserObject,
                  GA_ID,PGID_SPOOFIDPOP,
                  GA_RelVerify,TRUE,
                  CHOOSER_DropDown,TRUE,
                  CHOOSER_Labels,&spoofidlist,
                  CHOOSER_AutoFit,TRUE,
               EndMember,
#ifdef DEMOVERSION
               GA_Disabled,TRUE,
#endif
            EndMember,
            MemberLabel(CFGAWEBSTR(MSG_SET_NETWORK_SPOOFID)),
            StartMember,HLayoutObject,
               LAYOUT_HorizAlignment,LALIGN_RIGHT,
               StartMember,SpaceObject,
               EndMember,
               StartImage,LabelObject,
                  LABEL_Text,CFGAWEBSTR(MSG_SET_NETWORK_SPOOFID_EXPL),
                  LABEL_Justification,LJ_RIGHT,
               EndImage,
            EndMember,
         EndMember,
         CHILD_WeightedHeight,0,
      EndMember,
   End;
   if(layout1 && layout2)
   {  SetAttrs(layout1,LAYOUT_AlignLabels,layout2,TAG_END);
      SetAttrs(layout2,LAYOUT_AlignLabels,layout1,TAG_END);
   }
   return object;
}

void Makenwprgtplist(struct List *list,UBYTE **labels)
{  struct Node *node;
   short i;
   for(i=0;labels[i];i++)
   {  if(node=AllocChooserNode(
         CNA_Text,labels[i],
         TAG_END))
         AddTail(list,node);
   }
}

static void *Makeprogramspage(void)
{  return
   VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      LAYOUT_FixedVert,FALSE,
      StartMember,prgtpgad=ChooserObject,
         CHOOSER_PopUp,TRUE,
         CHOOSER_Labels,&nwprgtplist,
         CHOOSER_Active,0,
         GA_ID,PGID_NWPRGTP,
         GA_RelVerify,TRUE,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_NWPRG_TYPE)),
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,prgcmdgad=StringObject,
            GA_TabCycle,TRUE,
            GA_ID,PGID_NWPRGCMD,
            GA_RelVerify,TRUE,
            STRINGA_TextVal,nwp.telnetcmd?nwp.telnetcmd:NULLSTRING,
            STRINGA_MaxChars,127,
         EndMember,
         StartMember,ButtonObject,
            BUTTON_AutoButton,BAG_POPFILE,
            GA_ID,PGID_NWPRGPOP,
            GA_RelVerify,TRUE,
         EndMember,
         CHILD_MaxWidth,20,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_NWPRG_CMD)),
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,prgargsgad=StringObject,
            GA_TabCycle,TRUE,
            GA_ID,PGID_NWPRGARGS,
            GA_RelVerify,TRUE,
            STRINGA_TextVal,nwp.telnetargs?nwp.telnetargs:NULLSTRING,
            STRINGA_MaxChars,127,
         EndMember,
         StartMember,prghelpgad=ChooserObject,
            CHOOSER_DropDown,TRUE,
            CHOOSER_Labels,&telnethelplist,
            CHOOSER_AutoFit,TRUE,
         EndMember,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_NWPRG_ARGS)),
#ifdef DEMOVERSION
      GA_Disabled,TRUE,
#endif
   End;
}

static void Makenoproxylist(struct List *list)
{  
#ifndef DEMOVERSION
   struct Noproxy *nc;
   struct Node *node;
   for(nc=nwp.noproxy.first;nc->next;nc=nc->next)
   {  if(node=AllocListBrowserNode(1,
         LBNA_UserData,nc,
         LBNCA_CopyText,TRUE,
         LBNCA_Editable,TRUE,
         LBNCA_Text,nc->name,
         LBNCA_MaxChars,128,
         TAG_END))
         AddTail(list,node);
   }
#endif
}

static void *Makeproxypage(void)
{  return
   StartMember,VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      StartMember,HLayoutObject,
         StartMember,prxtpgad=ChooserObject,
            CHOOSER_PopUp,TRUE,
            CHOOSER_Labels,&prxtplist,
            CHOOSER_Active,0,
            GA_ID,PGID_PRXTP,
            GA_RelVerify,TRUE,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_PROXY_TYPE)),
         CHILD_WeightedWidth,0,
         StartMember,proxygad=StringObject,
            GA_TabCycle,TRUE,
            STRINGA_TextVal,nwp.httpproxy?nwp.httpproxy:NULLSTRING,
            STRINGA_MaxChars,127,
            GA_ID,PGID_PROXY,
            GA_RelVerify,TRUE,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_PROXY_NAME)),
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,lmpxgad=CheckBoxObject,
         GA_Text,CFGAWEBSTR(MSG_SET_PROXY_LIMITED),
         GA_Selected,nwp.limitproxy,
      EndMember,
      CHILD_WeightedWidth,0,
      CHILD_WeightedHeight,0,
      StartMember,pftpgad=CheckBoxObject,
         GA_Text,CFGAWEBSTR(MSG_SET_PROXY_PASSIVEFTP),
         GA_Selected,nwp.passiveftp,
      EndMember,
      CHILD_WeightedWidth,0,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         LAYOUT_SpaceOuter,TRUE,
         LAYOUT_BevelStyle,BVS_GROUP,
         LAYOUT_Label,CFGAWEBSTR(MSG_SET_PROXY_NOPROXYLABEL),
         StartMember,noprlistgad=ListBrowserObject,
            GA_ID,PGID_NOPRLIST,
            GA_RelVerify,TRUE,
            LISTBROWSER_ShowSelected,TRUE,
            LISTBROWSER_Labels,&noproxylist,
            LISTBROWSER_Editable,TRUE,
            LISTBROWSER_Selected,0,
         EndMember,
         StartMember,VLayoutObject,
            StartMember,ButtonObject,
               GA_ID,PGID_NOPRADD,
               GA_RelVerify,TRUE,
               GA_Text,CFGAWEBSTR(MSG_SET_PROXY_NOPROXYADD),
            EndMember,
            StartMember,noprdelgad=ButtonObject,
               GA_ID,PGID_NOPRDEL,
               GA_RelVerify,TRUE,
               GA_Text,CFGAWEBSTR(MSG_SET_PROXY_NOPROXYDEL),
               GA_Disabled,!noproxylist.lh_Head->ln_Succ,
            EndMember,
         EndMember,
         CHILD_WeightedHeight,0,
         CHILD_WeightedWidth,0,
#ifdef DEMOVERSION
         GA_Disabled,TRUE,
#endif
      EndMember,
#ifdef LOCALONLY
      GA_Disabled,TRUE,
#endif
   End;
}

static void Makenocachelist(struct List *list)
{  
#ifndef DEMOVERSION
   struct Nocache *nc;
   struct Node *node;
   for(nc=nwp.nocache.first;nc->next;nc=nc->next)
   {  if(node=AllocListBrowserNode(1,
         LBNA_UserData,nc,
         LBNCA_CopyText,TRUE,
         LBNCA_Editable,TRUE,
         LBNCA_Text,nc->name,
         LBNCA_MaxChars,128,
         TAG_END))
         AddTail(list,node);
   }
#endif
}

static void *Makecachepage(void)
{  return
   VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,capagad=StringObject,
            GA_TabCycle,TRUE,
            STRINGA_TextVal,nwp.cachepath,
            STRINGA_MaxChars,127,
         EndMember,
         StartMember,ButtonObject,
            BUTTON_AutoButton,BAG_POPDRAWER,
            GA_ID,PGID_POPCAPA,
            GA_RelVerify,TRUE,
         EndMember,
         CHILD_MaxWidth,20,
#ifdef LOCALONLY
         GA_Disabled,TRUE,
#endif
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_CACHE_CACHEPATH)),
      CHILD_WeightedHeight,0,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         StartMember,cavegad=ChooserObject,
            GA_ID,PGID_CAVERIFY,
            GA_RelVerify,TRUE,
            CHOOSER_PopUp,TRUE,
            CHOOSER_Labels,&caverifylist,
            CHOOSER_Active,nwp.caverify,
         EndMember,
         MemberLabel(CFGAWEBSTR(MSG_SET_CACHE_VERIFY)),
         CHILD_WeightedWidth,0,
         StartMember,cafrgad=CheckBoxObject,
            GA_Text,CFGAWEBSTR(MSG_SET_CACHE_FASTRESPONSE),
            GA_Selected,nwp.fastresponse,
            GA_Disabled,(nwp.caverify==CAVERIFY_NEVER),
         EndMember,
#ifdef LOCALONLY
         GA_Disabled,TRUE,
#endif
      EndMember,
      CHILD_WeightedHeight,0,
      CHILD_WeightedWidth,0,
      StartMember,VLayoutObject,
         StartMember,HLayoutObject,
            StartMember,VLayoutObject,
               LAYOUT_SpaceOuter,TRUE,
               LAYOUT_BevelStyle,BVS_GROUP,
               LAYOUT_Label,CFGAWEBSTR(MSG_SET_CACHE_SIZELABEL),
               StartMember,camegad=IntegerObject,
                  GA_TabCycle,TRUE,
                  INTEGER_Minimum,50,
                  INTEGER_Maximum,1000000,
                  INTEGER_Number,nwp.camemsize,
               EndMember,
               CHILD_MaxWidth,120,
               MemberLabel(CFGAWEBSTR(MSG_SET_CACHE_DOCMEM)),
               StartMember,cadigad=IntegerObject,
                  GA_TabCycle,TRUE,
                  INTEGER_Minimum,0,
                  INTEGER_Maximum,1000000,
                  INTEGER_Number,nwp.cadisksize,
#ifdef LOCALONLY
                  GA_Disabled,TRUE,
#endif
               EndMember,
               CHILD_MaxWidth,120,
               MemberLabel(CFGAWEBSTR(MSG_SET_CACHE_DOCDISK)),
            EndMember,
            StartMember,VLayoutObject,
               LAYOUT_SpaceOuter,TRUE,
               LAYOUT_BevelStyle,BVS_GROUP,
               LAYOUT_Label,CFGAWEBSTR(MSG_SET_CACHE_FREELABEL),
               StartMember,frecgad=IntegerObject,
                  GA_TabCycle,TRUE,
                  INTEGER_Minimum,0,
                  INTEGER_Maximum,2048,
                  INTEGER_Number,nwp.minfreechip,
               EndMember,
               CHILD_MaxWidth,120,
               MemberLabel(CFGAWEBSTR(MSG_SET_CACHE_CHIP)),
               StartMember,frefgad=IntegerObject,
                  GA_TabCycle,TRUE,
                  INTEGER_Minimum,0,
                  INTEGER_Maximum,1000000,
                  INTEGER_Number,nwp.minfreefast,
               EndMember,
               CHILD_MaxWidth,120,
               MemberLabel(CFGAWEBSTR(MSG_SET_CACHE_FAST)),
            EndMember,
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         LAYOUT_SpaceOuter,TRUE,
         LAYOUT_BevelStyle,BVS_GROUP,
         LAYOUT_Label,CFGAWEBSTR(MSG_SET_CACHE_NOCACHELABEL),
         StartMember,nocalistgad=ListBrowserObject,
            GA_ID,PGID_NOCALIST,
            GA_RelVerify,TRUE,
            LISTBROWSER_ShowSelected,TRUE,
            LISTBROWSER_Labels,&nocachelist,
            LISTBROWSER_Editable,TRUE,
            LISTBROWSER_Selected,0,
         EndMember,
         StartMember,VLayoutObject,
            StartMember,ButtonObject,
               GA_ID,PGID_NOCAADD,
               GA_RelVerify,TRUE,
               GA_Text,CFGAWEBSTR(MSG_SET_CACHE_NOCACHEADD),
            EndMember,
            StartMember,nocadelgad=ButtonObject,
               GA_ID,PGID_NOCADEL,
               GA_RelVerify,TRUE,
               GA_Text,CFGAWEBSTR(MSG_SET_CACHE_NOCACHEDEL),
               GA_Disabled,!nocachelist.lh_Head->ln_Succ,
            EndMember,
         EndMember,
         CHILD_WeightedHeight,0,
         CHILD_WeightedWidth,0,
#ifdef DEMOVERSION
         GA_Disabled,TRUE,
#endif
      EndMember,
   End;
}

static void *Makeprivacypage(void)
{  return
   VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      StartMember,VLayoutObject,
         StartMember,anongad=CheckBoxObject,
            GA_Text,CFGAWEBSTR(MSG_SET_NETWORK_ANONYMOUS),
            GA_Selected,!nwp.referer,
#ifdef LOCALONLY
            GA_Disabled,TRUE,
#endif
         EndMember,
         StartMember,fowagad=CheckBoxObject,
            GA_Text,CFGAWEBSTR(MSG_SET_NETWORK_FORMWARN),
            GA_Selected,nwp.formwarn,
#ifdef DEMOVERSION
            GA_Disabled,TRUE,
#endif
         EndMember,
         StartMember,spamgad=CheckBoxObject,
            GA_Text,CFGAWEBSTR(MSG_SET_NETWORK_SPAMBLOCK),
            GA_Selected,nwp.spamblock,
#ifdef LOCALONLY
            GA_Disabled,TRUE,
#endif
         EndMember,
         StartMember,ftpegad=CheckBoxObject,
            GA_Text,CFGAWEBSTR(MSG_SET_NETWORK_FTPEMAILADDR),
            GA_Selected,nwp.ftpemailaddr,
#ifdef LOCALONLY
            GA_Disabled,TRUE,
#endif
         EndMember,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         StartMember,VLayoutObject,
            StartMember,cookgad=ChooserObject,
               CHOOSER_PopUp,TRUE,
               CHOOSER_Labels,&cooklist,
               CHOOSER_Active,nwp.cookies,
#ifdef DEMOVERSION
               GA_Disabled,TRUE,
#endif
            EndMember,
            MemberLabel(CFGAWEBSTR(MSG_SET_NETWORK_COOKIES)),
            StartMember,rfccgad=CheckBoxObject,
               GA_Text,"RFC 210_9",       /***** Localize this.... ****/
               GA_Selected,nwp.rfc2109,
#ifdef LOCALONLY
            GA_Disabled,TRUE,
#endif
            EndMember,
         EndMember,
         CHILD_WeightedWidth,0,
         CHILD_WeightedHeight,0,
         StartMember,HLayoutObject,
            LAYOUT_SpaceOuter,TRUE,
            LAYOUT_BevelStyle,BVS_GROUP,
            LAYOUT_Label,CFGAWEBSTR(MSG_SET_NETWORK_NOCOOKIELABEL),
            StartMember,nocolistgad=ListBrowserObject,
               GA_ID,PGID_NOCOLIST,
               GA_RelVerify,TRUE,
               LISTBROWSER_ShowSelected,TRUE,
               LISTBROWSER_Labels,&nocookielist,
               LISTBROWSER_Editable,TRUE,
               LISTBROWSER_Selected,0,
            EndMember,
            StartMember,VLayoutObject,
               StartMember,ButtonObject,
                  GA_ID,PGID_NOCOADD,
                  GA_RelVerify,TRUE,
                  GA_Text,CFGAWEBSTR(MSG_SET_NETWORK_NOCOOKIEADD),
               EndMember,
               StartMember,nocodelgad=ButtonObject,
                  GA_ID,PGID_NOCODEL,
                  GA_RelVerify,TRUE,
                  GA_Text,CFGAWEBSTR(MSG_SET_NETWORK_NOCOOKIEDEL),
                  GA_Disabled,!nocookielist.lh_Head->ln_Succ,
               EndMember,
            EndMember,
            CHILD_WeightedHeight,0,
            CHILD_WeightedWidth,0,
#ifdef DEMOVERSION
            GA_Disabled,TRUE,
#endif
         EndMember,
      EndMember,
   End;
}

static void *Makemailaddresspage(void)
{  return VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      LAYOUT_FixedVert,FALSE,
      StartMember,mnadgad=StringObject,
         GA_TabCycle,TRUE,
         STRINGA_TextVal,nwp.emailaddr,
         STRINGA_MaxChars,127,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_ADDRESS)),
      CHILD_WeightedHeight,0,
      StartMember,mnfngad=StringObject,
         GA_TabCycle,TRUE,
         STRINGA_TextVal,nwp.fullname,
         STRINGA_MaxChars,127,
         GA_Disabled,nwp.extmailer && nwp.extnewsreader,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_REALNAME)),
      CHILD_WeightedHeight,0,
      StartMember,mnregad=StringObject,
         GA_TabCycle,TRUE,
         STRINGA_TextVal,nwp.replyaddr,
         STRINGA_MaxChars,127,
         GA_Disabled,nwp.extnewsreader,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_REPLYTO)),
      CHILD_WeightedHeight,0,
      StartMember,mnorgad=StringObject,
         GA_TabCycle,TRUE,
         STRINGA_TextVal,nwp.organization,
         STRINGA_MaxChars,127,
         GA_Disabled,nwp.extnewsreader,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_ORGANIZATION)),
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,mnsigad=StringObject,
            GA_TabCycle,TRUE,
            STRINGA_TextVal,nwp.sigfile,
            STRINGA_MaxChars,127,
            GA_Disabled,nwp.extmailer && nwp.extnewsreader,
         EndMember,
         StartMember,mnsipgad=ButtonObject,
            BUTTON_AutoButton,BAG_POPFILE,
            GA_ID,PGID_POPMMSI,
            GA_RelVerify,TRUE,
            GA_Disabled,nwp.extmailer && nwp.extnewsreader,
         EndMember,
         CHILD_MaxWidth,20,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_SIGNATURE)),
      CHILD_WeightedHeight,0,
#ifdef LOCALONLY
      GA_Disabled,TRUE,
#endif
   End;
}

static void *Makemailmailpage(void)
{  return VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      LAYOUT_FixedVert,FALSE,
      StartMember,mnshgad=StringObject,
         GA_TabCycle,TRUE,
         STRINGA_TextVal,nwp.smtphost,
         STRINGA_MaxChars,127,
         GA_Disabled,nwp.extmailer,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_SMTPHOST)),
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,mnmqgad=StringObject,
            GA_TabCycle,TRUE,
            STRINGA_TextVal,nwp.mailquotehdr,
            STRINGA_MaxChars,127,
            GA_Disabled,nwp.extmailer,
         EndMember,
         StartMember,mnmqhgad=ChooserObject,
            CHOOSER_DropDown,TRUE,
            CHOOSER_Labels,&qhdrhelplist,
            CHOOSER_AutoFit,TRUE,
            GA_Disabled,nwp.extmailer,
         EndMember,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_MAILQUOTEHDR)),
      CHILD_WeightedHeight,0,
      StartMember,mnemgad=CheckBoxObject,
         GA_ID,PGID_MNEM,
         GA_RelVerify,TRUE,
         GA_Text,CFGAWEBSTR(MSG_SET_MAIL_EXTMAILER),
         GA_Selected,nwp.extmailer,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,mltcgad=StringObject,
            GA_TabCycle,TRUE,
            STRINGA_TextVal,nwp.mailtocmd?nwp.mailtocmd:NULLSTRING,
            STRINGA_MaxChars,127,
            GA_Disabled,!nwp.extmailer,
         EndMember,
         StartMember,mltcpgad=ButtonObject,
            BUTTON_AutoButton,BAG_POPFILE,
            GA_ID,PGID_POPMLTC,
            GA_RelVerify,TRUE,
            GA_Disabled,!nwp.extmailer,
         EndMember,
         CHILD_MaxWidth,20,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_EXTMCOMMAND)),
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,mltagad=StringObject,
            GA_TabCycle,TRUE,
            STRINGA_TextVal,nwp.mailtoargs?nwp.mailtoargs:NULLSTRING,
            STRINGA_MaxChars,127,
            GA_Disabled,!nwp.extmailer,
         EndMember,
         StartMember,mltahgad=ChooserObject,
            CHOOSER_DropDown,TRUE,
            CHOOSER_Labels,&mailhelplist,
            CHOOSER_AutoFit,TRUE,
            GA_Disabled,!nwp.extmailer,
         EndMember,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_EXTMARGS)),
      CHILD_WeightedHeight,0,
#ifdef LOCALONLY
      GA_Disabled,TRUE,
#endif
   End;
}

static void *Makemailnewspage(void)
{  return VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      LAYOUT_FixedVert,FALSE,
      StartMember,mnnhgad=StringObject,
         GA_TabCycle,TRUE,
         STRINGA_TextVal,nwp.nntphost,
         STRINGA_MaxChars,127,
         GA_Disabled,nwp.extnewsreader,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_NNTPHOST)),
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,mnnqgad=StringObject,
            GA_TabCycle,TRUE,
            STRINGA_TextVal,nwp.newsquotehdr,
            STRINGA_MaxChars,127,
            GA_Disabled,nwp.extnewsreader,
         EndMember,
         StartMember,mnnqhgad=ChooserObject,
            CHOOSER_DropDown,TRUE,
            CHOOSER_Labels,&qhdrhelplist,
            CHOOSER_AutoFit,TRUE,
            GA_Disabled,nwp.extnewsreader,
         EndMember,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_NEWSQUOTEHDR)),
      CHILD_WeightedHeight,0,
      StartMember,mnaugad=StringObject,
         GA_TabCycle,TRUE,
         STRINGA_TextVal,nwp.newsauthuser,
         STRINGA_MaxChars,127,
         GA_Disabled,nwp.extnewsreader,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_NEWSUSER)),
      CHILD_WeightedHeight,0,
      StartMember,mnapgad=StringObject,
         GA_TabCycle,TRUE,
         STRINGA_TextVal,nwp.newsauthpass,
         STRINGA_MaxChars,127,
         GA_Disabled,nwp.extnewsreader,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_NEWSPASSWORD)),
      CHILD_WeightedHeight,0,
      StartMember,mnengad=CheckBoxObject,
         GA_ID,PGID_MNEN,
         GA_RelVerify,TRUE,
         GA_Text,CFGAWEBSTR(MSG_SET_MAIL_EXTNEWS),
         GA_Selected,nwp.extnewsreader,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,nwscgad=StringObject,
            GA_TabCycle,TRUE,
            STRINGA_TextVal,nwp.newscmd?nwp.newscmd:NULLSTRING,
            STRINGA_MaxChars,127,
            GA_Disabled,!nwp.extnewsreader,
         EndMember,
         StartMember,nwscpgad=ButtonObject,
            BUTTON_AutoButton,BAG_POPFILE,
            GA_ID,PGID_POPNWSC,
            GA_RelVerify,TRUE,
            GA_Disabled,!nwp.extnewsreader,
         EndMember,
         CHILD_MaxWidth,20,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_EXTNCOMMAND)),
      CHILD_WeightedHeight,0,
      StartMember,HLayoutObject,
         LAYOUT_SpaceInner,FALSE,
         StartMember,nwsagad=StringObject,
            GA_TabCycle,TRUE,
            STRINGA_TextVal,nwp.newsargs?nwp.newsargs:NULLSTRING,
            STRINGA_MaxChars,127,
            GA_Disabled,!nwp.extnewsreader,
         EndMember,
         StartMember,nwsahgad=ChooserObject,
            CHOOSER_DropDown,TRUE,
            CHOOSER_Labels,&newshelplist,
            CHOOSER_AutoFit,TRUE,
            GA_Disabled,!nwp.extnewsreader,
         EndMember,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_EXTNARGS)),
      CHILD_WeightedHeight,0,
#ifdef LOCALONLY
      GA_Disabled,TRUE,
#endif
   End;
}

static void *Makemailoptionspage(void)
{  return VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      LAYOUT_FixedVert,FALSE,
      StartMember,mnmagad=IntegerObject,
         GA_TabCycle,TRUE,
         INTEGER_Minimum,0,
         INTEGER_Maximum,1000,
         INTEGER_Number,nwp.maxartnews,
      EndMember,
      MemberLabel(CFGAWEBSTR(MSG_SET_MAIL_MAXARTICLES)),
      CHILD_WeightedHeight,0,
      CHILD_MaxWidth,120,
      StartMember,mnsogad=CheckBoxObject,
         GA_Text,CFGAWEBSTR(MSG_SET_MAIL_SORTED),
         GA_Selected,nwp.sortednews,
         GA_Disabled,nwp.extnewsreader,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,mnfrgad=CheckBoxObject,
         GA_Text,CFGAWEBSTR(MSG_SET_MAIL_FRAMED),
         GA_Selected,nwp.framednews,
         GA_Disabled,nwp.extnewsreader,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,mnlogad=CheckBoxObject,
         GA_Text,CFGAWEBSTR(MSG_SET_MAIL_LONGHDRS),
         GA_Selected,nwp.longhdrnews,
         GA_Disabled,nwp.extnewsreader,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,mnprgad=CheckBoxObject,
         GA_Text,CFGAWEBSTR(MSG_SET_MAIL_PROPFONT),
         GA_Selected,nwp.propnews,
         GA_Disabled,nwp.extnewsreader,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,mnnngad=CheckBoxObject,
         GA_Text,CFGAWEBSTR(MSG_SET_MAIL_NEWSBYNUMBER),
         GA_Selected,nwp.newsbynum,
         GA_Disabled,nwp.extnewsreader,
      EndMember,
      CHILD_WeightedHeight,0,
#ifdef LOCALONLY
      GA_Disabled,TRUE,
#endif
   End;
}

static void *Makemailpage(void)
{  return VLayoutObject,
      LAYOUT_VertAlignment,LALIGN_TOP,
      LAYOUT_FixedVert,FALSE,
      LAYOUT_SpaceInner,FALSE,
      StartMember,mailtabgad=ClickTabObject,
         CLICKTAB_Labels,&mailtablist,
         CLICKTAB_Current,0,
         GA_ID,PGID_MAILTABS,
         GA_RelVerify,TRUE,
      EndMember,
      CHILD_WeightedHeight,0,
      StartMember,mailpagelayout=HLayoutObject,
         LAYOUT_SpaceOuter,TRUE,
         LAYOUT_VertAlignment,LALIGN_TOP,
         StartMember,mailpagegad=PageObject,
            PAGE_Add,Makemailaddresspage(),
            PAGE_Add,Makemailmailpage(),
            PAGE_Add,Makemailnewspage(),
            PAGE_Add,Makemailoptionspage(),
         EndMember,
      EndMember,
   End;
}

/*---------------------------------------------------------------------------*/

#ifndef DEMOVERSION

/* Set selected spoof ID */
static void Dospoofidpop(short code)
{  Setgadgetattrs(spofgad,window,NULL,
      STRINGA_TextVal,code?spoofidlabels[code]:NULLSTRING,
      TAG_END);
}

/* save listbrowser edited */
static void Donocolist(void)
{  long selected=Getvalue(nocolistgad,LISTBROWSER_Selected);
   long event=Getvalue(nocolistgad,LISTBROWSER_RelEvent);
   struct Node *node;
   struct Nocache *nc=NULL,*nnc;
   UBYTE *p=NULL;
   if(event==LBRE_EDIT && (node=Getnode(&nocookielist,selected)))
   {  Setgadgetattrs(nocolistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
      GetListBrowserNodeAttrs(node,LBNA_UserData,&nc,LBNCA_Text,&p,TAG_END);
      if(nc && p)
      {  REMOVE(nc);
         nnc=Addnocookie(&nwp.nocookie,p);
         Freenocookie(nc);
         for(selected=0,nc=nwp.nocookie.first;nc->next && nc!=nnc;nc=nc->next,selected++);
         Freebrowserlist(&nocookielist);
         Makenocookielist(&nocookielist);
      }
      Setgadgetattrs(nocolistgad,window,NULL,
         LISTBROWSER_Labels,&nocookielist,
         LISTBROWSER_Selected,selected,
         TAG_END);
      Setgadgetattrs(nocolistgad,window,NULL,
         LISTBROWSER_MakeVisible,selected,
         TAG_END);
   }
   else if(event==LBRE_DOUBLECLICK) /* start editing */
   {  Setgadgetattrs(nocolistgad,window,NULL,
         LISTBROWSER_MakeVisible,selected,
         LISTBROWSER_EditColumn,0,
         LISTBROWSER_EditNode,selected,
         TAG_END);
      ActivateLayoutGadget(toplayout,window,0L,(ULONG)nocolistgad);
   }
}

/* add a new entry in the nocookie list */
static void Donocoadd(void)
{  long n;
   struct Nocache *nc,*nnc;
   struct TagItem strtags[]=
   {  STRINGA_BufferPos,4,
      TAG_END,0
   };
   Setgadgetattrs(nocolistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&nocookielist);
   nnc=Addnocookie(&nwp.nocookie,"www.");
   for(n=0,nc=nwp.nocookie.first;nc->next && nc!=nnc;nc=nc->next,n++);
   Makenocookielist(&nocookielist);
   Setgadgetattrs(nocolistgad,window,NULL,
      LISTBROWSER_Labels,&nocookielist,
      LISTBROWSER_Selected,n,
      TAG_END);
   Setgadgetattrs(nocodelgad,window,NULL,GA_Disabled,FALSE,TAG_END);
   Setgadgetattrs(nocolistgad,window,NULL,
      LISTBROWSER_MakeVisible,n,
      LISTBROWSER_EditColumn,0,
      LISTBROWSER_EditNode,n,
      LISTBROWSER_EditTags,strtags,
      TAG_END);
   ActivateLayoutGadget(toplayout,window,0L,(ULONG)nocolistgad);
}

/* delete current nocookie */
static void Donocodel(void)
{  long selected=Getvalue(nocolistgad,LISTBROWSER_Selected);
   struct Node *node;
   struct Nocache *nc,*nd;
   if(node=Getnode(&nocookielist,selected))
   {  Setgadgetattrs(nocolistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
      GetListBrowserNodeAttrs(node,LBNA_UserData,&nc,TAG_END);
      for(nd=nwp.nocookie.first,selected=0;nd!=nc;nd=nd->next,selected++);
      if(!nd->next->next) selected--;
      if(nc)
      {  REMOVE(nc);
         Freenocookie(nc);
      }
      Freebrowserlist(&nocookielist);
      Makenocookielist(&nocookielist);
      Setgadgetattrs(nocolistgad,window,NULL,
         LISTBROWSER_Labels,&nocookielist,
         LISTBROWSER_Selected,selected,
         TAG_END);
      Setgadgetattrs(nocolistgad,window,NULL,
         LISTBROWSER_MakeVisible,selected,
         TAG_END);
      Setgadgetattrs(nocodelgad,window,NULL,
         GA_Disabled,selected<0,
         TAG_END);
   }
}

#endif /* !DEMOVERSION */

#ifndef LOCALONLY
/* return a pointer to the proxy string for this code */
static UBYTE **Proxyptr(short code)
{  return &(&nwp.httpproxy)[code]; /* use ptr to first string as array */
}

/* handle proxy type select */
static void Doprxtp(WORD code)
{  UBYTE *proxy=*Proxyptr(code);
   Setgadgetattrs(proxygad,window,NULL,
      STRINGA_TextVal,proxy?proxy:NULLSTRING,
      STRINGA_DispPos,0,
      TAG_END);
}

/* handle proxy string entered */
static void Doproxy(void)
{  short nr=Getvalue(prxtpgad,CHOOSER_Active);
   UBYTE **proxy=Proxyptr(nr);
   UBYTE *p=(UBYTE *)Getvalue(proxygad,STRINGA_TextVal);
   UBYTE *q;
   if(STRNIEQUAL(p,"http://",7) || STRNIEQUAL(p,"https://",8)) q=Dupstr(p,-1);
   else if(*p)
   {  if(q=ALLOCTYPE(UBYTE,strlen(p)+8,MEMF_PUBLIC))
      {  strcpy(q,"http://");
         strcat(q,p);
      }
   }
   else q=NULL;
   if(*proxy) FREE(*proxy);
   *proxy=q;
   if(q)
   {  Setgadgetattrs(proxygad,window,NULL,
         STRINGA_TextVal,q,
         STRINGA_DispPos,0,
         TAG_END);
   }
}
#endif /* !LOCALONLY */

#ifndef DEMOVERSION

/* save listbrowser edited */
static void Donoprlist(void)
{  long selected=Getvalue(noprlistgad,LISTBROWSER_Selected);
   long event=Getvalue(noprlistgad,LISTBROWSER_RelEvent);
   struct Node *node;
   struct Nocache *nc=NULL,*nnc;
   UBYTE *p=NULL;
   if(event==LBRE_EDIT && (node=Getnode(&noproxylist,selected)))
   {  Setgadgetattrs(noprlistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
      GetListBrowserNodeAttrs(node,LBNA_UserData,&nc,LBNCA_Text,&p,TAG_END);
      if(nc && p)
      {  REMOVE(nc);
         nnc=Addnoproxy(&nwp.noproxy,p);
         Freenoproxy(nc);
         for(selected=0,nc=nwp.noproxy.first;nc->next && nc!=nnc;nc=nc->next,selected++);
         Freebrowserlist(&noproxylist);
         Makenoproxylist(&noproxylist);
      }
      Setgadgetattrs(noprlistgad,window,NULL,
         LISTBROWSER_Labels,&noproxylist,
         LISTBROWSER_Selected,selected,
         TAG_END);
      Setgadgetattrs(noprlistgad,window,NULL,
         LISTBROWSER_MakeVisible,selected,
         TAG_END);
   }
   else if(event==LBRE_DOUBLECLICK) /* start editing */
   {  Setgadgetattrs(noprlistgad,window,NULL,
         LISTBROWSER_MakeVisible,selected,
         LISTBROWSER_EditColumn,0,
         LISTBROWSER_EditNode,selected,
         TAG_END);
      ActivateLayoutGadget(toplayout,window,0L,(ULONG)noprlistgad);
   }
}

/* add a new entry in the noproxy list */
static void Donopradd(void)
{  long n;
   struct Nocache *nc,*nnc;
   struct TagItem strtags[]=
   {  STRINGA_BufferPos,4,
      TAG_END,0
   };
   Setgadgetattrs(noprlistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&noproxylist);
   nnc=Addnoproxy(&nwp.noproxy,"www.");
   for(n=0,nc=nwp.noproxy.first;nc->next && nc!=nnc;nc=nc->next,n++);
   Makenoproxylist(&noproxylist);
   Setgadgetattrs(noprlistgad,window,NULL,
      LISTBROWSER_Labels,&noproxylist,
      LISTBROWSER_Selected,n,
      TAG_END);
   Setgadgetattrs(noprdelgad,window,NULL,GA_Disabled,FALSE,TAG_END);
   Setgadgetattrs(noprlistgad,window,NULL,
      LISTBROWSER_MakeVisible,n,
      LISTBROWSER_EditColumn,0,
      LISTBROWSER_EditNode,n,
      LISTBROWSER_EditTags,strtags,
      TAG_END);
   ActivateLayoutGadget(toplayout,window,0L,(ULONG)noprlistgad);
}

/* delete current noproxy */
static void Donoprdel(void)
{  long selected=Getvalue(noprlistgad,LISTBROWSER_Selected);
   struct Node *node;
   struct Nocache *nc,*nd;
   if(node=Getnode(&noproxylist,selected))
   {  Setgadgetattrs(noprlistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
      GetListBrowserNodeAttrs(node,LBNA_UserData,&nc,TAG_END);
      for(nd=nwp.noproxy.first,selected=0;nd!=nc;nd=nd->next,selected++);
      if(!nd->next->next) selected--;
      if(nc)
      {  REMOVE(nc);
         Freenoproxy(nc);
      }
      Freebrowserlist(&noproxylist);
      Makenoproxylist(&noproxylist);
      Setgadgetattrs(noprlistgad,window,NULL,
         LISTBROWSER_Labels,&noproxylist,
         LISTBROWSER_Selected,selected,
         TAG_END);
      Setgadgetattrs(noprlistgad,window,NULL,
         LISTBROWSER_MakeVisible,selected,
         TAG_END);
      Setgadgetattrs(noprdelgad,window,NULL,
         GA_Disabled,selected<0,
         TAG_END);
   }
}

/* return pointer to network prog string for code */
static UBYTE **Nwprgptr(short code)
{  return &(&nwp.telnetcmd)[2*code];  /* use ptr to first string as array */
}

/* handle program type select */
static void Donwprgtp(short code)
{  UBYTE **p=Nwprgptr(code);
   void *help=NULL;
#ifndef DEMOVERSION
   Setgadgetattrs(prgcmdgad,window,NULL,
      STRINGA_TextVal,p[0]?p[0]:NULLSTRING,
      STRINGA_DispPos,0,
      TAG_END);
   Setgadgetattrs(prgargsgad,window,NULL,
      STRINGA_TextVal,p[1]?p[1]:NULLSTRING,
      STRINGA_DispPos,0,
      TAG_END);
   switch(code)
   {  case 0:help=&telnethelplist;break;
      case 1:help=&starttcphelplist;break;
      case 2:help=&endtcphelplist;break;
   }
   Setgadgetattrs(prghelpgad,window,NULL,
      CHOOSER_Labels,help,
      TAG_END);
#endif
}

/* handle program command entered */
static void Donwprgcmd(void)
{  short code=Getvalue(prgtpgad,CHOOSER_Active);
   UBYTE **p=Nwprgptr(code);
   Getnonnullstringvalue(p,prgcmdgad);
}

/* handle program command popup */
static void Donwprgpop(void)
{  short code=Getvalue(prgtpgad,CHOOSER_Active);
   Popfile(winobject,window,toplayout,CFGAWEBSTR(prgpopmsg[code]),prgcmdgad);
   Donwprgcmd();
}

/* handle program args entered */
static void Donwprgargs(void)
{  short code=Getvalue(prgtpgad,CHOOSER_Active);
   UBYTE **p=Nwprgptr(code);
   Getnonnullstringvalue(p+1,prgargsgad);
}

#endif /* !DEMOVERSION */

#ifndef LOCALONLY

/* handle cache file popup */
static void Dopopcapa(void)
{  Popdrawer(winobject,window,toplayout,CFGAWEBSTR(MSG_SET_REQTITLE_CACHEPATH),capagad);
}

/* en/disable fast response */
static void Docaverify(void)
{  short code=Getvalue(cavegad,CHOOSER_Active);
   Setgadgetattrs(cafrgad,window,NULL,GA_Disabled,code==CAVERIFY_NEVER,TAG_END);
}

#endif /* !LOCALONLY */

#ifndef DEMOVERSION

/* save listbrowser edited */
static void Donocalist(void)
{  long selected=Getvalue(nocalistgad,LISTBROWSER_Selected);
   long event=Getvalue(nocalistgad,LISTBROWSER_RelEvent);
   struct Node *node;
   struct Nocache *nc=NULL,*nnc;
   UBYTE *p=NULL;
   if(event==LBRE_EDIT && (node=Getnode(&nocachelist,selected)))
   {  Setgadgetattrs(nocalistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
      GetListBrowserNodeAttrs(node,LBNA_UserData,&nc,LBNCA_Text,&p,TAG_END);
      if(nc && p)
      {  REMOVE(nc);
         nnc=Addnocache(&nwp.nocache,p);
         Freenocache(nc);
         for(selected=0,nc=nwp.nocache.first;nc->next && nc!=nnc;nc=nc->next,selected++);
         Freebrowserlist(&nocachelist);
         Makenocachelist(&nocachelist);
      }
      Setgadgetattrs(nocalistgad,window,NULL,
         LISTBROWSER_Labels,&nocachelist,
         LISTBROWSER_Selected,selected,
         TAG_END);
      Setgadgetattrs(nocalistgad,window,NULL,
         LISTBROWSER_MakeVisible,selected,
         TAG_END);
   }
   else if(event==LBRE_DOUBLECLICK) /* start editing */
   {  Setgadgetattrs(nocalistgad,window,NULL,
         LISTBROWSER_MakeVisible,selected,
         LISTBROWSER_EditColumn,0,
         LISTBROWSER_EditNode,selected,
         TAG_END);
      ActivateLayoutGadget(toplayout,window,0L,(ULONG)nocalistgad);
   }
}

/* add a new entry in the nocache list */
static void Donocaadd(void)
{  long n;
   struct Nocache *nc,*nnc;
   struct TagItem strtags[]=
   {  STRINGA_BufferPos,4,
      TAG_END,0
   };
   Setgadgetattrs(nocalistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
   Freebrowserlist(&nocachelist);
   nnc=Addnocache(&nwp.nocache,"www.");
   for(n=0,nc=nwp.nocache.first;nc->next && nc!=nnc;nc=nc->next,n++);
   Makenocachelist(&nocachelist);
   Setgadgetattrs(nocalistgad,window,NULL,
      LISTBROWSER_Labels,&nocachelist,
      LISTBROWSER_Selected,n,
      TAG_END);
   Setgadgetattrs(nocadelgad,window,NULL,GA_Disabled,FALSE,TAG_END);
   Setgadgetattrs(nocalistgad,window,NULL,
      LISTBROWSER_MakeVisible,n,
      LISTBROWSER_EditColumn,0,
      LISTBROWSER_EditNode,n,
      LISTBROWSER_EditTags,strtags,
      TAG_END);
   ActivateLayoutGadget(toplayout,window,0L,(ULONG)nocalistgad);
}

/* delete current nocache */
static void Donocadel(void)
{  long selected=Getvalue(nocalistgad,LISTBROWSER_Selected);
   struct Node *node;
   struct Nocache *nc,*nd;
   if(node=Getnode(&nocachelist,selected))
   {  Setgadgetattrs(nocalistgad,window,NULL,LISTBROWSER_Labels,~0,TAG_END);
      GetListBrowserNodeAttrs(node,LBNA_UserData,&nc,TAG_END);
      for(nd=nwp.nocache.first,selected=0;nd!=nc;nd=nd->next,selected++);
      if(!nd->next->next) selected--;
      if(nc)
      {  REMOVE(nc);
         Freenocache(nc);
      }
      Freebrowserlist(&nocachelist);
      Makenocachelist(&nocachelist);
      Setgadgetattrs(nocalistgad,window,NULL,
         LISTBROWSER_Labels,&nocachelist,
         LISTBROWSER_Selected,selected,
         TAG_END);
      Setgadgetattrs(nocalistgad,window,NULL,
         LISTBROWSER_MakeVisible,selected,
         TAG_END);
      Setgadgetattrs(nocadelgad,window,NULL,
         GA_Disabled,selected<0,
         TAG_END);
   }
}

#endif /* !DEMOVERSION */

#ifndef LOCALONLY

/* Mail signature file popup */
static void Dopopsigfile(void)
{  Popfile(winobject,window,toplayout,CFGAWEBSTR(MSG_SET_REQTITLE_SIGNATURE),mnsigad);
}

/* External mailer */
static void Doextmailer(void)
{  BOOL exm=Getselected(mnemgad);
   BOOL exn=Getselected(mnengad);
   Setgadgetattrs(mnshgad,window,NULL,GA_Disabled,exm,TAG_END);
   Setgadgetattrs(mnmqgad,window,NULL,GA_Disabled,exm,TAG_END);
   Setgadgetattrs(mnmqhgad,window,NULL,GA_Disabled,exm,TAG_END);
   Setgadgetattrs(mltcgad,window,NULL,GA_Disabled,!exm,TAG_END);
   Setgadgetattrs(mltcpgad,window,NULL,GA_Disabled,!exm,TAG_END);
   Setgadgetattrs(mltagad,window,NULL,GA_Disabled,!exm,TAG_END);
   Setgadgetattrs(mltahgad,window,NULL,GA_Disabled,!exm,TAG_END);
   /* Not on this page: */
   Setgadgetattrs(mnfngad,NULL,NULL,GA_Disabled,exm && exn,TAG_END);
   Setgadgetattrs(mnregad,NULL,NULL,GA_Disabled,exm && exn,TAG_END);
   Setgadgetattrs(mnsigad,NULL,NULL,GA_Disabled,exm && exn,TAG_END);
   Setgadgetattrs(mnsipgad,NULL,NULL,GA_Disabled,exm && exn,TAG_END);
}

/* Mailto command popup */
static void Dopopmailto(void)
{  Popfile(winobject,window,toplayout,CFGAWEBSTR(MSG_SET_REQTITLE_MAILTO),mltcgad);
}

/* External newsreader */
static void Doextnewsreader(void)
{  BOOL exm=Getselected(mnemgad);
   BOOL exn=Getselected(mnengad);
   Setgadgetattrs(mnnhgad,window,NULL,GA_Disabled,exn,TAG_END);
   Setgadgetattrs(mnnqgad,window,NULL,GA_Disabled,exn,TAG_END);
   Setgadgetattrs(mnnqhgad,window,NULL,GA_Disabled,exn,TAG_END);
   Setgadgetattrs(mnaugad,window,NULL,GA_Disabled,exn,TAG_END);
   Setgadgetattrs(mnapgad,window,NULL,GA_Disabled,exn,TAG_END);
   Setgadgetattrs(nwscgad,window,NULL,GA_Disabled,!exn,TAG_END);
   Setgadgetattrs(nwscpgad,window,NULL,GA_Disabled,!exn,TAG_END);
   Setgadgetattrs(nwsagad,window,NULL,GA_Disabled,!exn,TAG_END);
   Setgadgetattrs(nwsahgad,window,NULL,GA_Disabled,!exn,TAG_END);
   /* Not on this page: */
   Setgadgetattrs(mnfngad,NULL,NULL,GA_Disabled,exm && exn,TAG_END);
   Setgadgetattrs(mnorgad,NULL,NULL,GA_Disabled,exn,TAG_END);
   Setgadgetattrs(mnregad,NULL,NULL,GA_Disabled,exm && exn,TAG_END);
   Setgadgetattrs(mnsigad,NULL,NULL,GA_Disabled,exm && exn,TAG_END);
   Setgadgetattrs(mnsipgad,NULL,NULL,GA_Disabled,exm && exn,TAG_END);
   Setgadgetattrs(mnmagad,NULL,NULL,GA_Disabled,exn,TAG_END);
   Setgadgetattrs(mnsogad,NULL,NULL,GA_Disabled,exn,TAG_END);
   Setgadgetattrs(mnfrgad,NULL,NULL,GA_Disabled,exn,TAG_END);
   Setgadgetattrs(mnlogad,NULL,NULL,GA_Disabled,exn,TAG_END);
   Setgadgetattrs(mnprgad,NULL,NULL,GA_Disabled,exn,TAG_END);
   Setgadgetattrs(mnnngad,NULL,NULL,GA_Disabled,exn,TAG_END);
}

/* News command popup */
static void Dopopnews(void)
{  Popfile(winobject,window,toplayout,CFGAWEBSTR(MSG_SET_REQTITLE_NEWS),nwscgad);
}

#endif /* !LOCALONLY */

static void Docursor(short n)
{  
#ifndef DEMOVERSION
   long page=Getvalue(pagegad,PAGE_Current);
   switch(page)
   {  case NWPREFS_OPTIONS:
         Moveselected(window,nocolistgad,&nocookielist,n);
         break;
      case NWPREFS_PROXY:
         Moveselected(window,noprlistgad,&noproxylist,n);
         break;
      case NWPREFS_CACHE:
         Moveselected(window,nocalistgad,&nocachelist,n);
         break;
   }
#endif
}

/* get data from gadgets */
static void Copydata(void)
{  nwp.loadimg=Getvalue(imglgad,CHOOSER_Active);
   nwp.maxconnect=Getvalue(parigad,INTEGER_Number);
   nwp.maxdiskread=Getvalue(maxdgad,INTEGER_Number);
   Getstringvalue(&nwp.homeurl,homegad);
   Getstringvalue(&nwp.localindex,loixgad);
   Getstringvalue(&nwp.searchurl,srchgad);
   Getstringvalue(&nwp.spoofid,spofgad);
   nwp.ignoremime=Getselected(igsmgad);
   nwp.starthomepage=Getselected(sthpgad);
   nwp.autosearch=Getselected(asrhgad);
   nwp.contanim=Getselected(canigad);
   nwp.restrictimages=Getselected(rstigad);
   nwp.limitproxy=Getselected(lmpxgad);
   nwp.passiveftp=Getselected(pftpgad);
#ifndef LOCALONLY
   /* Copy proxy settings from gadgets */
   Doproxy();
#endif
   Getstringvalue(&nwp.cachepath,capagad);
   nwp.camemsize=Getvalue(camegad,INTEGER_Number);
   nwp.cadisksize=Getvalue(cadigad,INTEGER_Number);
   nwp.minfreechip=Getvalue(frecgad,INTEGER_Number);
   nwp.minfreefast=Getvalue(frefgad,INTEGER_Number);
   nwp.caverify=Getvalue(cavegad,CHOOSER_Active);
   nwp.fastresponse=Getselected(cafrgad);
   nwp.referer=!Getselected(anongad);
   nwp.formwarn=Getselected(fowagad);
   nwp.spamblock=Getselected(spamgad);
   nwp.ftpemailaddr=Getselected(ftpegad);
   nwp.cookies=Getvalue(cookgad,CHOOSER_Active);
   nwp.rfc2109=Getselected(rfccgad);
   Getstringvalue(&nwp.emailaddr,mnadgad);
   Getstringvalue(&nwp.replyaddr,mnregad);
   Getstringvalue(&nwp.fullname,mnfngad);
   Getstringvalue(&nwp.organization,mnorgad);
   Getstringvalue(&nwp.smtphost,mnshgad);
   Getstringvalue(&nwp.nntphost,mnnhgad);
   Getstringvalue(&nwp.sigfile,mnsigad);
   Getstringvalue(&nwp.newsauthuser,mnaugad);
   Getstringvalue(&nwp.newsauthpass,mnapgad);
   Getstringvalue(&nwp.mailquotehdr,mnmqgad);
   Getstringvalue(&nwp.newsquotehdr,mnnqgad);
   Getnonnullstringvalue(&nwp.mailtocmd,mltcgad);
   Getnonnullstringvalue(&nwp.mailtoargs,mltagad);
   Getnonnullstringvalue(&nwp.newscmd,nwscgad);
   Getnonnullstringvalue(&nwp.newsargs,nwsagad);
   nwp.extmailer=Getselected(mnemgad);
   nwp.extnewsreader=Getselected(mnengad);
   nwp.maxartnews=Getvalue(mnmagad,INTEGER_Number);
   nwp.framednews=Getselected(mnfrgad);
   nwp.sortednews=Getselected(mnsogad);
   nwp.longhdrnews=Getselected(mnlogad);
   nwp.propnews=Getselected(mnprgad);
   nwp.newsbynum=Getselected(mnnngad);
}

/* set gadgets to changed data */
static void Setdata(void)
{  long page=Getvalue(pagegad,PAGE_Current);
   long mailpage=Getvalue(mailpagegad,PAGE_Current);
   UBYTE **p;
   struct Window *win;
   win=(page==NWPREFS_OPTIONS)?window:NULL;
   Setgadgetattrs(imglgad,win,NULL,CHOOSER_Active,nwp.loadimg,TAG_END);
   Setgadgetattrs(parigad,win,NULL,INTEGER_Number,nwp.maxconnect,TAG_END);
   Setgadgetattrs(maxdgad,win,NULL,INTEGER_Number,nwp.maxdiskread,TAG_END);
   Setgadgetattrs(homegad,win,NULL,STRINGA_TextVal,nwp.homeurl,TAG_END);
   Setgadgetattrs(loixgad,win,NULL,STRINGA_TextVal,nwp.localindex,TAG_END);
   Setgadgetattrs(srchgad,win,NULL,STRINGA_TextVal,nwp.searchurl,TAG_END);
   Setgadgetattrs(spofgad,win,NULL,STRINGA_TextVal,nwp.spoofid,TAG_END);
   Setgadgetattrs(igsmgad,win,NULL,GA_Selected,nwp.ignoremime,TAG_END);
   Setgadgetattrs(sthpgad,win,NULL,GA_Selected,nwp.starthomepage,TAG_END);
   Setgadgetattrs(asrhgad,win,NULL,GA_Selected,nwp.autosearch,TAG_END);
   Setgadgetattrs(canigad,win,NULL,GA_Selected,nwp.contanim,TAG_END);
   Setgadgetattrs(rstigad,win,NULL,GA_Selected,nwp.restrictimages,TAG_END);
#ifndef DEMOVERSION
   win=(page==NWPREFS_PROGRAMS)?window:NULL;
   p=Nwprgptr(Getvalue(prgtpgad,CHOOSER_Active));
   Setgadgetattrs(prgcmdgad,win,NULL,STRINGA_TextVal,p[0]?p[0]:NULLSTRING,TAG_END);
   Setgadgetattrs(prgargsgad,win,NULL,STRINGA_TextVal,p[1]?p[1]:NULLSTRING,TAG_END);
#endif
#ifndef LOCALONLY
   win=(page==NWPREFS_PROXY)?window:NULL;
   p=Proxyptr(Getvalue(prxtpgad,CHOOSER_Active));
   Setgadgetattrs(proxygad,win,NULL,STRINGA_TextVal,p[0]?p[0]:NULLSTRING,TAG_END);
   Setgadgetattrs(lmpxgad,win,NULL,GA_Selected,nwp.limitproxy,TAG_END);
   Setgadgetattrs(pftpgad,win,NULL,GA_Selected,nwp.passiveftp,TAG_END);
#endif
#ifndef DEMOVERSION
   Setgadgetattrs(noprlistgad,win,NULL,LISTBROWSER_Selected,~0,TAG_END);
   Freebrowserlist(&noproxylist);
   Makenoproxylist(&noproxylist);
   Setgadgetattrs(noprlistgad,win,NULL,
      LISTBROWSER_Labels,&noproxylist,
      LISTBROWSER_Selected,0,
      LISTBROWSER_MakeVisible,0,
      TAG_END);
   Setgadgetattrs(noprdelgad,win,NULL,GA_Disabled,!noproxylist.lh_Head->ln_Succ,TAG_END);
#endif
   win=(page==NWPREFS_CACHE)?window:NULL;
   Setgadgetattrs(capagad,win,NULL,
      STRINGA_TextVal,nwp.cachepath,
      STRINGA_DispPos,0,
      TAG_END);
   Setgadgetattrs(camegad,win,NULL,INTEGER_Number,nwp.camemsize,TAG_END);
   Setgadgetattrs(cadigad,win,NULL,INTEGER_Number,nwp.cadisksize,TAG_END);
   Setgadgetattrs(frecgad,win,NULL,INTEGER_Number,nwp.minfreechip,TAG_END);
   Setgadgetattrs(frefgad,win,NULL,INTEGER_Number,nwp.minfreefast,TAG_END);
   Setgadgetattrs(cavegad,win,NULL,CHOOSER_Active,nwp.caverify,TAG_END);
   Setgadgetattrs(cafrgad,win,NULL,
      GA_Selected,nwp.fastresponse,
      GA_Disabled,(nwp.caverify==CAVERIFY_NEVER),
      TAG_END);
#ifndef DEMOVERSION
   Setgadgetattrs(nocalistgad,win,NULL,LISTBROWSER_Selected,~0,TAG_END);
   Freebrowserlist(&nocachelist);
   Makenocachelist(&nocachelist);
   Setgadgetattrs(nocalistgad,win,NULL,
      LISTBROWSER_Labels,&nocachelist,
      LISTBROWSER_Selected,0,
      LISTBROWSER_MakeVisible,0,
      TAG_END);
   Setgadgetattrs(nocadelgad,win,NULL,GA_Disabled,!nocachelist.lh_Head->ln_Succ,TAG_END);
#endif
#ifndef LOCALONLY
   win=(page==NWPREFS_PRIVACY)?window:NULL;
   Setgadgetattrs(anongad,win,NULL,GA_Selected,!nwp.referer,TAG_END);
   Setgadgetattrs(fowagad,win,NULL,GA_Selected,nwp.formwarn,TAG_END);
   Setgadgetattrs(spamgad,win,NULL,GA_Selected,nwp.spamblock,TAG_END);
   Setgadgetattrs(ftpegad,win,NULL,GA_Selected,nwp.ftpemailaddr,TAG_END);
   Setgadgetattrs(cookgad,win,NULL,CHOOSER_Active,nwp.cookies,TAG_END);
   Setgadgetattrs(rfccgad,win,NULL,GA_Selected,nwp.rfc2109,TAG_END);
#ifndef DEMOVERSION
   Setgadgetattrs(nocolistgad,win,NULL,LISTBROWSER_Selected,~0,TAG_END);
   Freebrowserlist(&nocookielist);
   Makenocookielist(&nocookielist);
   Setgadgetattrs(nocolistgad,win,NULL,
      LISTBROWSER_Labels,&nocookielist,
      LISTBROWSER_Selected,0,
      LISTBROWSER_MakeVisible,0,
      TAG_END);
   Setgadgetattrs(nocodelgad,win,NULL,GA_Disabled,!nocookielist.lh_Head->ln_Succ,TAG_END);
#endif
   win=(page==NWPREFS_MAIL && mailpage==NWMAIL_ADDRESS)?window:NULL;
   Setgadgetattrs(mnadgad,win,NULL,STRINGA_TextVal,nwp.emailaddr,TAG_END);
   Setgadgetattrs(mnfngad,win,NULL,
      STRINGA_TextVal,nwp.fullname,
      GA_Disabled,nwp.extmailer && nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnregad,win,NULL,
      STRINGA_TextVal,nwp.replyaddr,
      GA_Disabled,nwp.extmailer && nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnorgad,win,NULL,
      STRINGA_TextVal,nwp.organization,
      GA_Disabled,nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnsigad,win,NULL,
      STRINGA_TextVal,nwp.sigfile,
      GA_Disabled,nwp.extmailer && nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnsipgad,win,NULL,
      GA_Disabled,nwp.extmailer && nwp.extnewsreader,
      TAG_END);
   win=(page==NWPREFS_MAIL && mailpage==NWMAIL_MAIL)?window:NULL;
   Setgadgetattrs(mnshgad,win,NULL,
      STRINGA_TextVal,nwp.smtphost,
      GA_Disabled,nwp.extmailer,
      TAG_END);
   Setgadgetattrs(mnmqgad,win,NULL,
      STRINGA_TextVal,nwp.mailquotehdr,
      GA_Disabled,nwp.extmailer,
      TAG_END);
   Setgadgetattrs(mnmqhgad,win,NULL,GA_Disabled,nwp.extmailer,TAG_END);
   Setgadgetattrs(mnemgad,win,NULL,GA_Selected,nwp.extmailer,TAG_END);
   Setgadgetattrs(mltcgad,win,NULL,
      STRINGA_TextVal,nwp.mailtocmd,
      GA_Disabled,!nwp.extmailer,
      TAG_END);
   Setgadgetattrs(mltcpgad,win,NULL,GA_Disabled,!nwp.extmailer,TAG_END);
   Setgadgetattrs(mltagad,win,NULL,
      STRINGA_TextVal,nwp.mailtoargs,
      GA_Disabled,!nwp.extmailer,
      TAG_END);
   Setgadgetattrs(mltahgad,win,NULL,GA_Disabled,!nwp.extmailer,TAG_END);
   win=(page==NWPREFS_MAIL && mailpage==NWMAIL_NEWS)?window:NULL;
   Setgadgetattrs(mnnhgad,win,NULL,
      STRINGA_TextVal,nwp.nntphost,
      GA_Disabled,nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnnqgad,win,NULL,
      STRINGA_TextVal,nwp.newsquotehdr,
      GA_Disabled,nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnnqhgad,win,NULL,GA_Disabled,nwp.extnewsreader,TAG_END);
   Setgadgetattrs(mnaugad,win,NULL,
      STRINGA_TextVal,nwp.newsauthuser,
      GA_Disabled,nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnapgad,win,NULL,
      STRINGA_TextVal,nwp.newsauthpass,
      GA_Disabled,nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnengad,win,NULL,GA_Selected,nwp.extnewsreader,TAG_END);
   Setgadgetattrs(nwscgad,win,NULL,
      STRINGA_TextVal,nwp.newscmd,
      GA_Disabled,!nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(nwscpgad,win,NULL,GA_Disabled,!nwp.extnewsreader,TAG_END);
   Setgadgetattrs(nwsagad,win,NULL,
      STRINGA_TextVal,nwp.newsargs,
      GA_Disabled,!nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(nwsahgad,win,NULL,GA_Disabled,!nwp.extnewsreader,TAG_END);
   win=(page==NWPREFS_MAIL && mailpage==NWMAIL_OPTIONS)?window:NULL;
   Setgadgetattrs(mnmagad,win,NULL,
      GA_Selected,nwp.maxartnews,
      GA_Disabled,nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnsogad,win,NULL,
      GA_Selected,nwp.sortednews,
      GA_Disabled,nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnfrgad,win,NULL,
      GA_Selected,nwp.framednews,
      GA_Disabled,nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnlogad,win,NULL,
      GA_Selected,nwp.longhdrnews,
      GA_Disabled,nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnprgad,win,NULL,
      GA_Selected,nwp.propnews,
      GA_Disabled,nwp.extnewsreader,
      TAG_END);
   Setgadgetattrs(mnnngad,win,NULL,
      GA_Selected,nwp.newsbynum,
      GA_Disabled,nwp.extnewsreader,
      TAG_END);
#endif /* !LOCALONLY */
}

/*---------------------------------------------------------------------------*/

static void Localize(void)
{  tablabels[0]=CFGAWEBSTR(MSG_SET_CTNW_OPTIONS);
   tablabels[1]=CFGAWEBSTR(MSG_SET_CTNW_PROGRAMS);
   tablabels[2]=CFGAWEBSTR(MSG_SET_CTNW_PROXY);
   tablabels[3]=CFGAWEBSTR(MSG_SET_CTNW_CACHE);
   tablabels[4]=CFGAWEBSTR(MSG_SET_CTNW_PRIVACY);
   tablabels[5]=CFGAWEBSTR(MSG_SET_CTNW_MAIL);
   tablabels[6]=NULL;
   mailtablabels[0]=CFGAWEBSTR(MSG_SET_MAIL_TAB_ADDRESS);
   mailtablabels[1]=CFGAWEBSTR(MSG_SET_MAIL_TAB_MAIL);
   mailtablabels[2]=CFGAWEBSTR(MSG_SET_MAIL_TAB_NEWS);
   mailtablabels[3]=CFGAWEBSTR(MSG_SET_MAIL_TAB_OPTIONS);
   mailtablabels[4]=NULL;
   srchhelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_S);
   srchhelplabels[1]=NULL;
   mailhelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_E);
   mailhelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_N);
   mailhelplabels[2]=NULL;
   ftphelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_H);
   ftphelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_F);
   ftphelplabels[2]=CFGAWEBSTR(MSG_SET_HLP_N);
   ftphelplabels[3]=NULL;
   telnethelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_L);
   telnethelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_W);
   telnethelplabels[2]=CFGAWEBSTR(MSG_SET_HLP_H);
   telnethelplabels[3]=CFGAWEBSTR(MSG_SET_HLP_P);
   telnethelplabels[4]=CFGAWEBSTR(MSG_SET_HLP_N);
   telnethelplabels[5]=NULL;
   newshelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_A);
   newshelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_N);
   newshelplabels[2]=NULL;
   starttcphelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_N);
   starttcphelplabels[1]=NULL;
   endtcphelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_NOARGS);
   endtcphelplabels[1]=NULL;
   imgllabels[0]=CFGAWEBSTR(MSG_SET_IMGL_OFF);
   imgllabels[1]=CFGAWEBSTR(MSG_SET_IMGL_MAPS);
   imgllabels[2]=CFGAWEBSTR(MSG_SET_IMGL_ALL);
   imgllabels[3]=NULL;
   nwprgtplabels[1]=CFGAWEBSTR(MSG_SET_NWPRG_STARTTCP);
   nwprgtplabels[2]=CFGAWEBSTR(MSG_SET_NWPRG_ENDTCP);
   nwprgtplabels[3]=NULL;
   caverifylabels[0]=CFGAWEBSTR(MSG_SET_CAVERIFY_ALWAYS);
   caverifylabels[1]=CFGAWEBSTR(MSG_SET_CAVERIFY_ONCE);
   caverifylabels[2]=CFGAWEBSTR(MSG_SET_CAVERIFY_NEVER);
   caverifylabels[3]=NULL;
   cooklabels[0]=CFGAWEBSTR(MSG_SET_COOKIES_NEVER);
   cooklabels[1]=CFGAWEBSTR(MSG_SET_COOKIES_ASK);
   cooklabels[2]=CFGAWEBSTR(MSG_SET_COOKIES_QUIET);
   cooklabels[3]=NULL;
   qhdrhelplabels[0]=CFGAWEBSTR(MSG_SET_HLP_D);
   qhdrhelplabels[1]=CFGAWEBSTR(MSG_SET_HLP_E);
   qhdrhelplabels[2]=CFGAWEBSTR(MSG_SET_HLP_G);
   qhdrhelplabels[3]=CFGAWEBSTR(MSG_SET_HLP_I3);
   qhdrhelplabels[4]=CFGAWEBSTR(MSG_SET_HLP_N2);
   qhdrhelplabels[5]=CFGAWEBSTR(MSG_SET_HLP_S2);
   qhdrhelplabels[6]=NULL;
}

BOOL Opennetwork(void)
{  BOOL ok=FALSE;
   if(window)
   {  WindowToFront(window);
      ActivateWindow(window);
      return FALSE;
   }
   (&tablist)->lh_Head = (struct Node *)&((&tablist)->lh_Tail);
   (&tablist)->lh_Tail = NULL;
   (&tablist)->lh_TailPred = (struct Node *)&((&tablist)->lh_Head);
   (&mailtablist)->lh_Head = (struct Node *)&((&mailtablist)->lh_Tail);
   (&mailtablist)->lh_Tail = NULL;
   (&mailtablist)->lh_TailPred = (struct Node *)&((&mailtablist)->lh_Head);
   (&imgllist)->lh_Head = (struct Node *)&((&imgllist)->lh_Tail);
   (&imgllist)->lh_Tail = NULL;
   (&imgllist)->lh_TailPred = (struct Node *)&((&imgllist)->lh_Head);
   (&cooklist)->lh_Head = (struct Node *)&((&cooklist)->lh_Tail);
   (&cooklist)->lh_Tail = NULL;
   (&cooklist)->lh_TailPred = (struct Node *)&((&cooklist)->lh_Head);
   (&nocookielist)->lh_Head = (struct Node *)&((&nocookielist)->lh_Tail);
   (&nocookielist)->lh_Tail = NULL;
   (&nocookielist)->lh_TailPred = (struct Node *)&((&nocookielist)->lh_Head);
   (&prxtplist)->lh_Head = (struct Node *)&((&prxtplist)->lh_Tail);
   (&prxtplist)->lh_Tail = NULL;
   (&prxtplist)->lh_TailPred = (struct Node *)&((&prxtplist)->lh_Head);
   (&noproxylist)->lh_Head = (struct Node *)&((&noproxylist)->lh_Tail);
   (&noproxylist)->lh_Tail = NULL;
   (&noproxylist)->lh_TailPred = (struct Node *)&((&noproxylist)->lh_Head);
   (&nwprgtplist)->lh_Head = (struct Node *)&((&nwprgtplist)->lh_Tail);
   (&nwprgtplist)->lh_Tail = NULL;
   (&nwprgtplist)->lh_TailPred = (struct Node *)&((&nwprgtplist)->lh_Head);
   (&srchhelplist)->lh_Head = (struct Node *)&((&srchhelplist)->lh_Tail);
   (&srchhelplist)->lh_Tail = NULL;
   (&srchhelplist)->lh_TailPred = (struct Node *)&((&srchhelplist)->lh_Head);
   (&mailhelplist)->lh_Head = (struct Node *)&((&mailhelplist)->lh_Tail);
   (&mailhelplist)->lh_Tail = NULL;
   (&mailhelplist)->lh_TailPred = (struct Node *)&((&mailhelplist)->lh_Head);
   (&ftphelplist)->lh_Head = (struct Node *)&((&ftphelplist)->lh_Tail);
   (&ftphelplist)->lh_Tail = NULL;
   (&ftphelplist)->lh_TailPred = (struct Node *)&((&ftphelplist)->lh_Head);
   (&telnethelplist)->lh_Head = (struct Node *)&((&telnethelplist)->lh_Tail);
   (&telnethelplist)->lh_Tail = NULL;
   (&telnethelplist)->lh_TailPred = (struct Node *)&((&telnethelplist)->lh_Head);
   (&newshelplist)->lh_Head = (struct Node *)&((&newshelplist)->lh_Tail);
   (&newshelplist)->lh_Tail = NULL;
   (&newshelplist)->lh_TailPred = (struct Node *)&((&newshelplist)->lh_Head);
   (&starttcphelplist)->lh_Head = (struct Node *)&((&starttcphelplist)->lh_Tail);
   (&starttcphelplist)->lh_Tail = NULL;
   (&starttcphelplist)->lh_TailPred = (struct Node *)&((&starttcphelplist)->lh_Head);
   (&endtcphelplist)->lh_Head = (struct Node *)&((&endtcphelplist)->lh_Tail);
   (&endtcphelplist)->lh_Tail = NULL;
   (&endtcphelplist)->lh_TailPred = (struct Node *)&((&endtcphelplist)->lh_Head);
   (&caverifylist)->lh_Head = (struct Node *)&((&caverifylist)->lh_Tail);
   (&caverifylist)->lh_Tail = NULL;
   (&caverifylist)->lh_TailPred = (struct Node *)&((&caverifylist)->lh_Head);
   (&nocachelist)->lh_Head = (struct Node *)&((&nocachelist)->lh_Tail);
   (&nocachelist)->lh_Tail = NULL;
   (&nocachelist)->lh_TailPred = (struct Node *)&((&nocachelist)->lh_Head);
   (&qhdrhelplist)->lh_Head = (struct Node *)&((&qhdrhelplist)->lh_Tail);
   (&qhdrhelplist)->lh_Tail = NULL;
   (&qhdrhelplist)->lh_TailPred = (struct Node *)&((&qhdrhelplist)->lh_Head);
   (&spoofidlist)->lh_Head = (struct Node *)&((&spoofidlist)->lh_Tail);
   (&spoofidlist)->lh_Tail = NULL;
   (&spoofidlist)->lh_TailPred = (struct Node *)&((&spoofidlist)->lh_Head);
   if(!tablabels[0]) Localize();
   if(nport=CreateMsgPort())
   {  strcpy(prefsname1,"ENV:" DEFAULTCFG);
      if(*configname) AddPart(prefsname1,configname,64);
      AddPart(prefsname1,"network",64);
      nreq1.nr_Name=prefsname1;
      nreq1.nr_Port=nport;
      nreq1.nr_Flags=NRF_SEND_MESSAGE;
      StartNotify(&nreq1);
      strcpy(prefsname2,"ENV:" DEFAULTCFG);
      if(*configname) AddPart(prefsname2,configname,64);
      AddPart(prefsname2,"nocookie",64);
      nreq2.nr_Name=prefsname2;
      nreq2.nr_Port=nport;
      nreq2.nr_Flags=NRF_SEND_MESSAGE;
      StartNotify(&nreq2);
      Copynetworkprefs(&defprefs.network,&nwp);
      Loadnetworkprefs(&nwp,FALSE,NULL);
      Copynetworkprefs(&nwp,&orgnwp);
      tested=FALSE;
      Makeclicktablist(&tablist,tablabels);
      Makeclicktablist(&mailtablist,mailtablabels);
      Makechooserlist(&imgllist,imgllabels,FALSE);
      Makechooserlist(&cooklist,cooklabels,FALSE);
      Makenocookielist(&nocookielist);
      Makechooserlist(&prxtplist,prxtplabels,FALSE);
      Makenoproxylist(&noproxylist);
      Makenwprgtplist(&nwprgtplist,nwprgtplabels);
      Makechooserlist(&srchhelplist,srchhelplabels,TRUE);
      Makechooserlist(&mailhelplist,mailhelplabels,TRUE);
      Makechooserlist(&ftphelplist,ftphelplabels,TRUE);
      Makechooserlist(&telnethelplist,telnethelplabels,TRUE);
      Makechooserlist(&newshelplist,newshelplabels,TRUE);
      Makechooserlist(&starttcphelplist,starttcphelplabels,TRUE);
      Makechooserlist(&endtcphelplist,endtcphelplabels,TRUE);
      Makechooserlist(&caverifylist,caverifylabels,FALSE);
      Makechooserlist(&qhdrhelplist,qhdrhelplabels,TRUE);
      Makechooserlist(&spoofidlist,spoofidlabels,FALSE);
      Makenocachelist(&nocachelist);
      winobject=WindowObject,
            WA_Title,CFGAWEBSTR(MSG_SET_REQTITLE_NETWORKWINDOW),
            WA_Left,setprefs.netx,
            WA_Top,setprefs.nety,
            WA_InnerWidth,setprefs.netw,
            WA_InnerHeight,setprefs.neth,
            WA_AutoAdjust,TRUE,
            WA_CloseGadget,TRUE,
            WA_DragBar,TRUE,
            WA_DepthGadget,TRUE,
            WA_SizeGadget,TRUE,
            WA_Activate,TRUE,
            WA_IDCMP,IDCMP_RAWKEY|IDCMP_MENUPICK,
            WA_SimpleRefresh,TRUE,
            WA_PubScreen,pubscreen,
            setprefs.netw?TAG_IGNORE:WINDOW_Position,WPOS_CENTERSCREEN,
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
                     PAGE_Add,Makeprogramspage(),
                     PAGE_Add,Makeproxypage(),
                     PAGE_Add,Makecachepage(),
                     PAGE_Add,Makeprivacypage(),
                     PAGE_Add,Makemailpage(),
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
         {  
            if((menubar=CreateMenus(menubase,
                  GTMN_FrontPen,drawinfo->dri_Pens[BARDETAILPEN],
                  TAG_END))
             && LayoutMenus(menubar,visualinfo,TAG_END))
               SetMenuStrip(window,menubar);
            GetAttr(WINDOW_SigMask,winobject,&nwmask);
            nwmask|=1<<nport->mp_SigBit;
            ok=TRUE;
         }
      }
   }
   if(!ok) Closenetwork();
   return ok;
}

/* returns TRUE is window remains open, FALSE if window should close */
BOOL Processnetwork(void)
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
                  Setgadgetattrs(pagegad,window,NULL,PAGE_Current,code,TAG_END);
                  RethinkLayout(pagelayout,window,NULL,TRUE);
                  break;
#ifndef DEMOVERSION
               case PGID_SPOOFIDPOP:
                  Dospoofidpop(code);
                  break;
               case PGID_NOCOLIST:
                  Donocolist();
                  break;
               case PGID_NOCOADD:
                  Donocoadd();
                  break;
               case PGID_NOCODEL:
                  Donocodel();
                  break;
#endif
#ifndef LOCALONLY
               case PGID_PRXTP:
                  Doprxtp(code);
                  break;
               case PGID_PROXY:
                  Doproxy();
                  break;
#endif
#ifndef DEMOVERSION
               case PGID_NOPRLIST:
                  Donoprlist();
                  break;
               case PGID_NOPRADD:
                  Donopradd();
                  break;
               case PGID_NOPRDEL:
                  Donoprdel();
                  break;
               case PGID_NWPRGTP:
                  Donwprgtp(code);
                  break;
               case PGID_NWPRGPOP:
                  Donwprgpop();
                  break;
               case PGID_NWPRGCMD:
                  Donwprgcmd();
                  break;
               case PGID_NWPRGARGS:
                  Donwprgargs();
                  break;
#endif
#ifndef LOCALONLY
               case PGID_POPCAPA:
                  Dopopcapa();
                  break;
               case PGID_CAVERIFY:
                  Docaverify();
                  break;
#endif
#ifndef DEMOVERSION
               case PGID_NOCALIST:
                  Donocalist();
                  break;
               case PGID_NOCAADD:
                  Donocaadd();
                  break;
               case PGID_NOCADEL:
                  Donocadel();
                  break;
#endif
               case PGID_MAILTABS:
                  Setgadgetattrs(mailpagegad,window,NULL,PAGE_Current,code,TAG_END);
                  RethinkLayout(mailpagelayout,window,NULL,TRUE);
                  break;
#ifndef LOCALONLY
               case PGID_POPMMSI:
                  Dopopsigfile();
                  break;
               case PGID_MNEM:
                  Doextmailer();
                  break;
               case PGID_POPMLTC:
                  Dopopmailto();
                  break;
               case PGID_MNEN:
                  Doextnewsreader();
                  break;
               case PGID_POPNWSC:
                  Dopopnews();
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
                  EndNotify(&nreq1);
                  EndNotify(&nreq2);
                  Savenetworkprefs(&nwp,FALSE,NULL);
                  Savenocookieprefs(&nwp,FALSE,NULL);
                  StartNotify(&nreq1);
                  StartNotify(&nreq2);
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
                     CFGAWEBSTR(MSG_SET_REQTITLE_OPENNETWORK),"network",FALSE))
                  {  Disposenetworkprefs(&nwp);
                     Copynetworkprefs(&defprefs.network,&nwp);
                     Loadnetworkprefs(&nwp,FALSE,path);
                     Setdata();
                     FREE(path);
                  }
                  break;
               case MID_SAVEAS:
                  if(path=Filereq(winobject,window,toplayout,
                     CFGAWEBSTR(MSG_SET_REQTITLE_SAVENETWORK),"network",TRUE))
                  {  Copydata();
                     Savenetworkprefs(&nwp,FALSE,path);
                     Savenocookieprefs(&nwp,FALSE,path);
                     FREE(path);
                  }
                  break;
               case MID_QUIT:
                  done=TRUE;
                  break;
               case MID_DEFAULTS:
                  Disposenetworkprefs(&nwp);
                  Copynetworkprefs(&defprefs.network,&nwp);
                  Setdata();
                  break;
               case MID_LASTSAVED:
                  Disposenetworkprefs(&nwp);
                  Copynetworkprefs(&defprefs.network,&nwp);
                  Loadnetworkprefs(&nwp,TRUE,NULL);
                  Setdata();
                  break;
               case MID_RESTORE:
                  Disposenetworkprefs(&nwp);
                  Copynetworkprefs(&orgnwp,&nwp);
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
                  Dimensions(window,&setprefs.netx);
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
      {  Disposenetworkprefs(&nwp);
         Copynetworkprefs(&defprefs.network,&nwp);
         Loadnetworkprefs(&nwp,FALSE,NULL);
         Setdata();
         Disposenetworkprefs(&orgnwp);
         Copynetworkprefs(&nwp,&orgnwp);
         tested=FALSE;
      }
   }
   if(done)
   {  if(tested)
      {  Disposenetworkprefs(&nwp);
         Copynetworkprefs(&orgnwp,&nwp);
         if(!endmode) endmode=PGID_USE;
      }
      Closenetwork();
   }
   return (BOOL)!done;
}

void Closenetwork(void)
{  nwmask=0;
   if(window)
   {  ClearMenuStrip(window);
      Dimensions(window,&setprefs.netx);
   }
   if(winobject) DisposeObject(winobject);winobject=NULL;window=NULL;
   if(menubar) FreeMenus(menubar);menubar=NULL;
   if(endmode)
   {  if(endmode==PGID_SAVE)
      {  Savenetworkprefs(&nwp,TRUE,NULL);
         Savenocookieprefs(&nwp,TRUE,NULL);
      }
      Savenetworkprefs(&nwp,FALSE,NULL);
      Savenocookieprefs(&nwp,FALSE,NULL);
   }
   endmode=0;
   Freeclicktablist(&tablist);
   Freeclicktablist(&mailtablist);
   Freechooserlist(&imgllist);
   Freechooserlist(&cooklist);
   Freebrowserlist(&nocookielist);
   Freechooserlist(&prxtplist);
   Freebrowserlist(&noproxylist);
   Freechooserlist(&nwprgtplist);
   Freechooserlist(&srchhelplist);
   Freechooserlist(&mailhelplist);
   Freechooserlist(&ftphelplist);
   Freechooserlist(&telnethelplist);
   Freechooserlist(&newshelplist);
   Freechooserlist(&starttcphelplist);
   Freechooserlist(&endtcphelplist);
   Freechooserlist(&caverifylist);
   Freebrowserlist(&nocachelist);
   Freechooserlist(&qhdrhelplist);
   Freechooserlist(&spoofidlist);
   Disposenetworkprefs(&nwp);
   Disposenetworkprefs(&orgnwp);
   if(nreq1.nr_Name) EndNotify(&nreq1);
   memset(&nreq1,0,sizeof(nreq1));
   if(nreq2.nr_Name) EndNotify(&nreq2);
   memset(&nreq2,0,sizeof(nreq2));
   if(nport) DeleteMsgPort(nport);nport=NULL;
}
