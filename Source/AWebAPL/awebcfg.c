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

/* awebcfg.c - AWeb configuration tool */

#include "awebcfg.h"

__near long __stack=8192;
const char *stack_cookie = "$STACK: 8192";

// Library base pointers - only declare those that don't have proto headers
// Note: gradientslider.gadget doesn't have a proto file in the Amiga NDK
// IntuitionBase, LocaleBase, GfxBase are provided by proto headers
struct Library *IconBase,*GadToolsBase,*AslBase,*ColorWheelBase,*GradientSliderBase;

// Class library base pointers - these are provided by proto headers
struct ClassLibrary *WindowBase,*LayoutBase,*ButtonBase,
   *ListBrowserBase,*ChooserBase,*IntegerBase,*SpaceBase,*CheckBoxBase,
   *StringBase,*LabelBase,*PaletteBase,*GlyphBase,*ClickTabBase;

struct LocaleInfo localeinfo;

static UBYTE versionstring[]="$VER:AWebCfg " AWEBVERSION RELEASECLASS " " __AMIGADATE__;

struct Screen *pubscreen;
struct DrawInfo *drawinfo;
void *visualinfo;
struct Image *amigaimg;

static UBYTE screenname[32]="";
static BOOL browser,program,gui,network;
USHORT cfgcommand;   /* queued CFGCLASS_xxx see below */
static BOOL newdimensions=FALSE;
BOOL nopool=FALSE;
BOOL has35=FALSE;

struct NewMenu menubase[]=
{  NM_TITLE,(STRPTR)MSG_SET_PROJECT_MENU,    0,0,0,0,
    NM_ITEM,(STRPTR)MSG_SET_PROJECT_OPEN,    0,0,0,(APTR)MID_OPEN,
    NM_ITEM,(STRPTR)MSG_SET_PROJECT_SAVEAS,  0,0,0,(APTR)MID_SAVEAS,
    NM_ITEM,NM_BARLABEL,                     0,0,0,0,
    NM_ITEM,(STRPTR)MSG_SET_PROJECT_QUIT,    0,0,0,(APTR)MID_QUIT,
   NM_TITLE,(STRPTR)MSG_SET_EDIT_MENU,       0,0,0,0,
    NM_ITEM,(STRPTR)MSG_SET_EDIT_DEFAULTS,   0,0,0,(APTR)MID_DEFAULTS,
    NM_ITEM,(STRPTR)MSG_SET_EDIT_LASTSAVED,  0,0,0,(APTR)MID_LASTSAVED,
    NM_ITEM,(STRPTR)MSG_SET_EDIT_RESTORE,    0,0,0,(APTR)MID_RESTORE,
   NM_TITLE,(STRPTR)MSG_SET_WINDOWS_MENU,    0,0,0,0,
    NM_ITEM,(STRPTR)MSG_SET_WINDOWS_BROWSER, 0,0,0,(APTR)MID_BROWSER,
    NM_ITEM,(STRPTR)MSG_SET_WINDOWS_PROGRAM, 0,0,0,(APTR)MID_PROGRAM,
    NM_ITEM,(STRPTR)MSG_SET_WINDOWS_GUI,     0,0,0,(APTR)MID_GUI,
    NM_ITEM,(STRPTR)MSG_SET_WINDOWS_NETWORK, 0,0,0,(APTR)MID_NETWORK,
#ifndef NEED35
    NM_ITEM,(STRPTR)MSG_SET_WINDOWS_CLASSACT,0,0,0,(APTR)MID_CLASSACT,
#endif
    NM_ITEM,NM_BARLABEL,                     0,0,0,0,
    NM_ITEM,(STRPTR)MSG_SET_WINDOWS_SNAPSHOT,0,0,0,(APTR)MID_SNAPSHOT,
   NM_END,0,0,0,0,0,
};

static struct MsgPort *cfgport;

struct Settingsprefs setprefs;

void *maincatalog;

/*---------------------------------------------------------------------------*/

UBYTE *Dupstr(UBYTE *str,long length)
{  UBYTE *dup;
   if(!str) return NULL;
   if(length<0) length=strlen(str);
   if(dup=ALLOCTYPE(UBYTE,length+1,0))
   {  memmove(dup,str,length);
      dup[length]='\0';
   }
   return dup;
}

#define Closelib(base) if(base) CloseLibrary((struct Library *)base)

static void Cleanup(void)
{  if(cfgport)
   {  struct Message *msg;
      Forbid();
      RemPort(cfgport);
      while(msg=GetMsg(cfgport)) ReplyMsg(msg);
      Permit();
      DeleteMsgPort(cfgport);
   }
   if(visualinfo) FreeVisualInfo(visualinfo);
   if(drawinfo) FreeScreenDrawInfo(pubscreen,drawinfo);
   if(pubscreen) UnlockPubScreen(NULL,pubscreen);
   Freedefprefs();
   Freememory();
   if(maincatalog) CloseCatalog(maincatalog);
   if(localeinfo.li_Catalog) CloseCatalog(localeinfo.li_Catalog);
   Closelib(WindowBase);
   Closelib(LayoutBase);
   Closelib(ButtonBase);
   Closelib(ListBrowserBase);
   Closelib(ChooserBase);
   Closelib(IntegerBase);
   Closelib(SpaceBase);
   Closelib(CheckBoxBase);
   Closelib(StringBase);
   Closelib(LabelBase);
   Closelib(PaletteBase);
   Closelib(GlyphBase);
   Closelib(ClickTabBase);
   Closelib(GradientSliderBase);
   Closelib(ColorWheelBase);
   Closelib(IntuitionBase);
   Closelib(IconBase);
   Closelib(LocaleBase);
   Closelib(GfxBase);
   Closelib(GadToolsBase);
   Closelib(AslBase);
   exit(0);
}

void Lowlevelreq(UBYTE *msg,...)
{  struct EasyStruct es;
   BOOL opened=FALSE;
   va_list args;
   va_start(args,msg);
   if(!IntuitionBase)
   {  if(IntuitionBase=(struct IntuitionBase *)OpenLibrary("intuition.library",36)) opened=TRUE;
   }
   if(IntuitionBase)
   {  es.es_StructSize=sizeof(struct EasyStruct);
      es.es_Flags=0;
      es.es_Title="AWebCfg";
      es.es_TextFormat=msg;
      es.es_GadgetFormat="Ok";
      EasyRequestArgs(NULL,&es,NULL,args);
      if(opened)
      {  CloseLibrary(IntuitionBase);
         IntuitionBase=NULL;
      }
   }
   else
   {  vprintf(msg,args);
      printf("\n");
   }
   va_end(args);
}

static struct Library *Openlib(UBYTE *name,long version)
{  struct Library *lib=OpenLibrary(name,version);
   if(!lib)
   {  Lowlevelreq(CFGAWEBSTR(MSG_ERROR_CANTOPENV),name,version);
      Cleanup();
      exit(10);
   }
   return lib;
}

#define Openclass(name) (struct ClassLibrary *)Openlib(name,OSNEED(0,44))

static void Getarguments(struct WBStartup *wbs)
{  long args[8]={0};
   UBYTE *argtemplate="BROWSER/S,PROGRAM/S,GUI/S,NETWORK/S,PUBSCREEN/K,CONFIG/K";
   if(wbs)
   {  long oldcd=CurrentDir(wbs->sm_ArgList[0].wa_Lock);
      struct DiskObject *dob=GetDiskObject(wbs->sm_ArgList[0].wa_Name);
      if(dob)
      {  UBYTE **ttp;
         for(ttp=dob->do_ToolTypes;*ttp;ttp++)
         {  if(STRIEQUAL(*ttp,"BROWSER")) browser=TRUE;
            else if(STRIEQUAL(*ttp,"PROGRAM")) program=TRUE;
            else if(STRIEQUAL(*ttp,"GUI")) gui=TRUE;
            else if(STRIEQUAL(*ttp,"NETWORK")) network=TRUE;
            else if(STRNIEQUAL(*ttp,"CONFIG=",7)) strncpy(configname,*ttp+7,31);
            else if(STRNIEQUAL(*ttp,"PUBSCREEN=",10)) strncpy(screenname,*ttp+10,31);
         }
         FreeDiskObject(dob);
      }
      CurrentDir(oldcd);
   }
   else
   {  struct RDArgs *rda=ReadArgs(argtemplate,args,NULL);
      if(rda)
      {  if(args[0]) browser=TRUE;
         if(args[1]) program=TRUE;
         if(args[2]) gui=TRUE;
         if(args[3]) network=TRUE;
         if(args[4]) strncpy(screenname,(UBYTE *)args[4],31);
         if(args[5]) strncpy(configname,(UBYTE *)args[5],31);
         FreeArgs(rda);
      }
   }
}

/* check if duplicate startup. Notify first copy */
static BOOL Dupstartupcheck(void)
{  struct Cfgmsg msg={0};
   struct MsgPort *port;
   if(cfgport=CreateMsgPort())
   {  Forbid();
      if(port=FindPort(AWEBCFGPORTNAME))
      {  msg.mn_ReplyPort=cfgport;
         if(browser) msg.cfgclass|=CFGCLASS_BROWSER;
         if(program) msg.cfgclass|=CFGCLASS_PROGRAM;
         if(gui) msg.cfgclass|=CFGCLASS_GUI;
         if(network) msg.cfgclass|=CFGCLASS_NETWORK;
         PutMsg(port,&msg);
      }
      else
      {  cfgport->ln_Name=AWEBCFGPORTNAME;
         AddPort(cfgport);
      }
      Permit();
      if(port)
      {  WaitPort(cfgport);
         GetMsg(cfgport);
         DeleteMsgPort(cfgport);
         cfgport=NULL;
         return FALSE;
      }
      else return TRUE;
   }
   return FALSE;
}

/* check messages in cfgport and set (command) */
static void Processcfgport(void)
{  struct Cfgmsg *msg;
   while(msg=(struct Cfgmsg *)GetMsg(cfgport))
   {  cfgcommand|=msg->cfgclass;
      ReplyMsg(msg);
   }
}

static void Adjustmenus35(void)
{  short i;
   BOOL shift=FALSE;
   for(i=0;menubase[i].nm_Type!=NM_END;i++)
   {  if(menubase[i].nm_Type==NM_ITEM
      && menubase[i].nm_UserData==(APTR)MID_CLASSACT)
      {  shift=TRUE;
      }
      if(shift)
      {  menubase[i]=menubase[i+1];
      }
   }
}

static void Localizemenus(void)
{  short i;
   UBYTE *str;
   for(i=0;menubase[i].nm_Type!=NM_END;i++)
   {  if(menubase[i].nm_Label!=NM_BARLABEL)
      {  str=CFGAWEBSTR((long)menubase[i].nm_Label);
         if(strlen(str)>2 && str[1]=='/')
         {  menubase[i].nm_Label=str+2;
            menubase[i].nm_CommKey=str;
         }
         else menubase[i].nm_Label=str;
      }
   }
}

static void Openclassact(void)
{  UBYTE buf[64];
   long out;
   BOOL result=FALSE;
   strcpy(buf,"SYS:Prefs/Reaction");
   if(*screenname)
   {  strcat(buf," PUBSCREEN ");
      strcat(buf,screenname);
   }
   out=Open("NIL:",MODE_NEWFILE);
   if(out && 0<=SystemTags(buf,
      SYS_Input,out,
      SYS_Output,NULL,
      SYS_Asynch,TRUE,
      TAG_END)) result=TRUE;
   if(!result && out) Close(out);
}

void Makechooserlist(struct List *list,UBYTE **labels,BOOL readonly)
{  struct Node *node;
   short i;
   for(i=0;labels[i];i++)
   {  if(node=AllocChooserNode(
         CNA_Text,labels[i],
         CNA_ReadOnly,readonly,
         TAG_END))
         AddTail(list,node);
   }
}

void Freechooserlist(struct List *list)
{  struct Node *node;
   if(list->lh_Head)
   {  while(node=RemHead(list)) FreeChooserNode(node);
   }
}

void Makeclicktablist(struct List *list,UBYTE **labels)
{  struct Node *node;
   short i;
   for(i=0;labels[i];i++)
   {  if(node=AllocClickTabNode(
         TNA_Text,labels[i],
         TNA_Number,i,
//         TNA_Enabled,TRUE,
         TNA_Spacing,4,
         TAG_END))
         AddTail(list,node);
   }
}

void Freeclicktablist(struct List *list)
{  struct Node *node;
   if(list->lh_Head)
   {  while(node=RemHead(list)) FreeClickTabNode(node);
   }
}

/*
void Makeradiolist(struct List *list,UBYTE **labels)
{  struct Node *node;
   short i;
   for(i=0;labels[i];i++)
   {  if(node=AllocRadioButtonNode(i,
         RBNA_Labels,labels[i],
         TAG_END))
         AddTail(list,node);
   }
}

void Freeradiolist(struct List *list)
{  struct Node *node;
   if(list->lh_Head)
   {  while(node=RemHead(list)) FreeRadioButtonNode(node);
   }
}
*/

void Freebrowserlist(struct List *list)
{  struct Node *node;
   if(list->lh_Head)
   {  while(node=RemHead(list)) FreeListBrowserNode(node);
   }
}

void Setgadgetattrs(struct Gadget *gad,struct Window *win,struct Requester *req,...)
{  struct TagItem *tags=(struct TagItem *)((ULONG *)&req+1);
   if(SetGadgetAttrsA(gad,win,req,tags) && win) RefreshGList(gad,win,req,1);
}

struct Node *Getnode(struct List *list,long n)
{  struct Node *node=list->lh_Head;
   while(node->ln_Succ && n)
   {  node=node->ln_Succ;
      n--;
   }
   if(node->ln_Succ) return node;
   else return NULL;
}

long Getvalue(struct Gadget *gad,ULONG tag)
{  long value=0;
   GetAttr(tag,gad,(ULONG *)&value);
   return value;
}

void Getstringvalue(UBYTE **ptr,struct Gadget *gad)
{  UBYTE *v=(UBYTE *)Getvalue(gad,STRINGA_TextVal);
   UBYTE *p;
   if(v && (p=Dupstr(v,-1)))
   {  if(*ptr) FREE(*ptr);
      *ptr=p;
   }
}

void Getnonnullstringvalue(UBYTE **ptr,struct Gadget *gad)
{  UBYTE *v=(UBYTE *)Getvalue(gad,STRINGA_TextVal);
   if(*ptr) FREE(*ptr);
   if(v && *v) *ptr=Dupstr(v,-1);
   else *ptr=NULL;
}

long Getselected(struct Gadget *gad)
{  return (gad->Flags&GFLG_SELECTED)!=0;
}

void Adjustintgad(struct Window *window,struct Gadget *gad)
{  long value=Getvalue(gad,INTEGER_Number);
   Setgadgetattrs(gad,window,NULL,INTEGER_Number,value,TAG_END);
}

long Reqheight(struct Screen *screen)
{  struct DimensionInfo dim={0};
   long height;
   if(GetDisplayInfoData(NULL,(UBYTE *)&dim,sizeof(dim),DTAG_DIMS,
      GetVPModeID(&screen->ViewPort)))
   {  height=(dim.Nominal.MaxY-dim.Nominal.MinY+1)*4/5;
   }
   else
   {  height=screen->Height*4/5;
   }
   return height;
}

void Popdrawer(void *winobj,struct Window *window,struct Gadget *layout,
   UBYTE *title,struct Gadget *gad)
{  struct FileRequester *fr;
   if(fr=AllocAslRequestTags(ASL_FileRequest,
      ASLFR_Window,window,
      ASLFR_TitleText,title,
      ASLFR_InitialHeight,Reqheight(window->WScreen),
      ASLFR_InitialDrawer,Getvalue(gad,STRINGA_TextVal),
      ASLFR_DoSaveMode,TRUE,
      ASLFR_DrawersOnly,TRUE,
      TAG_END))
   {  SetGadgetAttrs(layout,window,NULL,LAYOUT_DeferLayout,FALSE,TAG_END);
      SetAttrs(winobj,WA_BusyPointer,TRUE,GA_ReadOnly,TRUE,TAG_END);
      if(AslRequest(fr,NULL))
      {  UBYTE *p=Dupstr(fr->fr_Drawer,-1);
         if(p)
         {  Setgadgetattrs(gad,window,NULL,
               STRINGA_TextVal,p,
               STRINGA_DispPos,0,
               TAG_END);
         }
      }
      SetAttrs(winobj,WA_BusyPointer,FALSE,GA_ReadOnly,FALSE,TAG_END);
      SetGadgetAttrs(layout,window,NULL,LAYOUT_DeferLayout,TRUE,TAG_END);
      FreeAslRequest(fr);
   }
}

void Popfile(void *winobj,struct Window *window,struct Gadget *layout,
   UBYTE *title,struct Gadget *gad)
{  struct FileRequester *fr;
   UBYTE *path=(UBYTE *)Getvalue(gad,STRINGA_TextVal);
   UBYTE *drawer=NULL,*file=NULL;
   if(path)
   {  drawer=Dupstr(path,PathPart(path)-path);
      file=FilePart(path);
   }
   if(fr=AllocAslRequestTags(ASL_FileRequest,
      ASLFR_Window,window,
      ASLFR_TitleText,title,
      ASLFR_InitialHeight,Reqheight(window->WScreen),
      ASLFR_InitialDrawer,drawer?drawer:NULLSTRING,
      ASLFR_InitialFile,file?file:NULLSTRING,
      ASLFR_RejectIcons,TRUE,
      TAG_END))
   {  SetGadgetAttrs(layout,window,NULL,LAYOUT_DeferLayout,FALSE,TAG_END);
      SetAttrs(winobj,WA_BusyPointer,TRUE,GA_ReadOnly,TRUE,TAG_END);
      if(AslRequest(fr,NULL))
      {  long len=strlen(fr->fr_Drawer)+strlen(fr->fr_File)+2;
         if(path=ALLOCTYPE(UBYTE,len,0))
         {  strcpy(path,fr->fr_Drawer);
            AddPart(path,fr->fr_File,len);
            Setgadgetattrs(gad,window,NULL,
               STRINGA_TextVal,path,
               STRINGA_DispPos,0,
               TAG_END);
            FREE(path);
         }
      }
      SetAttrs(winobj,WA_BusyPointer,FALSE,GA_ReadOnly,FALSE,TAG_END);
      SetGadgetAttrs(layout,window,NULL,LAYOUT_DeferLayout,TRUE,TAG_END);
      FreeAslRequest(fr);
   }
   if(drawer) FREE(drawer);
}

UBYTE *Prefsscreenmodename(ULONG modeid,long w,long h,long d)
{  struct NameInfo ni={0};
   UBYTE *p=NULL;
   long len;
   if(!GetDisplayInfoData(NULL,(UBYTE *)&ni,sizeof(ni),DTAG_NAME,modeid))
      strcpy(ni.Name,"(????)");
   len=strlen(ni.Name)+24;
   if(p=ALLOCTYPE(UBYTE,len,MEMF_PUBLIC))
   {  sprintf(p,"%s, %d � %d � %d",ni.Name,w,h,1<<d);
   }
   return p;
}

/* If (cp) nonnull, find best pen for this rgb. If (cp) null, use (pen) */
enum CGADGET_IDS
{  CGID_OK=1,CGID_CANCEL,CGID_GRADIENT,CGID_WHEEL,
   CGID_RED,CGID_GREEN,CGID_BLUE,
};
BOOL Popcolor(void *winobj,struct Window *pwin,struct Gadget *layout,
   struct Colorprefs *cp,long pen)
{  struct Window *win;
   struct IntuiMessage *msg;
   struct Gadget *cwgad,*gsgad,*rgad,*ggad,*bgad;
   struct Screen *scr=pwin->WScreen;
   struct ColorMap *cmap=scr->ViewPort.ColorMap;
   struct ColorWheelRGB rgb,undo;
   struct ColorWheelHSB hsb;
   struct NewGadget ng={0};
   struct Gadget *glist=NULL,*gad;
   struct TextAttr ta={0};
   UBYTE rbuf[]="R:%3ld",bbuf[]="B:%3ld",gbuf[]="G:%3ld";
   BOOL result=FALSE;
   WORD pens[3];
   BOOL done=FALSE;
   ULONG value32;
   short gadh,gadw,labelw,sliderw,winw,winh,offx,offy;
   ULONG windowmask,gotmask;
   SetGadgetAttrs(layout,pwin,NULL,LAYOUT_DeferLayout,FALSE);
   SetAttrs(winobj,
      WA_BusyPointer,TRUE,
      GA_ReadOnly,TRUE,
      TAG_END);
   if(cp)
   {  pen=ObtainBestPen(cmap,cp->red,cp->green,cp->blue,TAG_END);
      rgb.cw_Red=cp->red;
      rgb.cw_Green=cp->green;
      rgb.cw_Blue=cp->blue;
   }
   else
   {  GetRGB32(cmap,pen,1,&rgb.cw_Red);
      undo=rgb;
   }
   pens[0]=ObtainBestPen(cmap,0xffffffff,0xffffffff,0xffffffff,TAG_END);
   pens[1]=ObtainBestPen(cmap,0x00000000,0x00000000,0x00000000,TAG_END);
   pens[2]=-1;
   offx=scr->WBorLeft;
   offy=scr->RastPort.TxHeight+scr->WBorTop+1;
   gadh=scr->RastPort.TxHeight+4;
   gadw=TextLength(&scr->RastPort,CFGAWEBSTR(MSG_SET_COLOUR_CANCEL),6)+16;
   labelw=6*scr->RastPort.Font->tf_XSize;
   winh=80+4*gadh+6*4;
   winw=2*gadw+16;
   if(winw<152) winw=152;
   sliderw=winw-8-labelw;
   ta.ta_Name=scr->RastPort.Font->ln_Name;
   ta.ta_YSize=scr->RastPort.Font->tf_YSize;
   gad=CreateContext(&glist);
   ng.ng_TextAttr=&ta;
   ng.ng_VisualInfo=visualinfo;
   ng.ng_LeftEdge=offx+4;
   ng.ng_TopEdge=offy+3*gadh+100;
   ng.ng_Width=gadw;
   ng.ng_Height=gadh;
   ng.ng_GadgetText=CFGAWEBSTR(MSG_SET_COLOUR_OK);
   ng.ng_GadgetID=CGID_OK;
   gad=CreateGadget(BUTTON_KIND,gad,&ng,TAG_END);
   ng.ng_LeftEdge=offx+winw-4-gadw;
   ng.ng_GadgetText=CFGAWEBSTR(MSG_SET_COLOUR_CANCEL);
   ng.ng_GadgetID=CGID_CANCEL;
   gad=CreateGadget(BUTTON_KIND,gad,&ng,TAG_END);
   ng.ng_LeftEdge=offx+4;
   ng.ng_TopEdge=offy+88;
   ng.ng_Width=sliderw;
   ng.ng_Height=gadh;
   ng.ng_GadgetText=NULL;
   ng.ng_GadgetID=CGID_RED;
   rbuf[0]=*CFGAWEBSTR(MSG_SET_COLOUR_RED);
   gbuf[0]=*CFGAWEBSTR(MSG_SET_COLOUR_GREEN);
   bbuf[0]=*CFGAWEBSTR(MSG_SET_COLOUR_BLUE);
   rgad=gad=CreateGadget(SLIDER_KIND,gad,&ng,
      GTSL_Min,0,
      GTSL_Max,255,
      GTSL_Level,rgb.cw_Red>>24,
      GTSL_LevelFormat,rbuf,
      GTSL_MaxLevelLen,5,
      GTSL_LevelPlace,PLACETEXT_RIGHT,
      TAG_END);
   ng.ng_TopEdge+=4+gadh;
   ng.ng_GadgetID=CGID_GREEN;
   ggad=gad=CreateGadget(SLIDER_KIND,gad,&ng,
      GTSL_Min,0,
      GTSL_Max,255,
      GTSL_Level,rgb.cw_Green>>24,
      GTSL_LevelFormat,gbuf,
      GTSL_MaxLevelLen,5,
      GTSL_LevelPlace,PLACETEXT_RIGHT,
      TAG_END);
   ng.ng_TopEdge+=4+gadh;
   ng.ng_GadgetID=CGID_BLUE;
   bgad=gad=CreateGadget(SLIDER_KIND,gad,&ng,
      GTSL_Min,0,
      GTSL_Max,255,
      GTSL_Level,rgb.cw_Blue>>24,
      GTSL_LevelFormat,bbuf,
      GTSL_MaxLevelLen,5,
      GTSL_LevelPlace,PLACETEXT_RIGHT,
      TAG_END);
   if(gad
   && (gsgad=(struct Gadget *)NewObject(NULL,"gradientslider.gadget",
      GA_Top,offy+4,
      GA_Left,offx+88,
      GA_Width,16,
      GA_Height,80,
      GRAD_PenArray,pens,
      PGA_Freedom,LORIENT_VERT,
      GA_ID,CGID_GRADIENT,
      ICA_TARGET,ICTARGET_IDCMP,
      TAG_END))
   && (cwgad=(struct Gadget *)NewObject(NULL,"colorwheel.gadget",
      GA_Top,offy+4,
      GA_Left,offx+4,
      GA_Width,80,
      GA_Height,80,
      WHEEL_RGB,&rgb,
      WHEEL_Screen,scr,
      WHEEL_GradientSlider,gsgad,
      GA_Previous,gsgad,
      GA_ID,CGID_WHEEL,
      ICA_TARGET,ICTARGET_IDCMP,
      TAG_END))
   && (win=OpenWindowTags(NULL,
      WA_Left,pwin->LeftEdge+20,
      WA_Top,pwin->TopEdge+20,
      WA_InnerWidth,winw,
      WA_InnerHeight,winh,
      WA_PubScreen,scr,
      WA_DragBar,TRUE,
      WA_DepthGadget,TRUE,
      WA_CloseGadget,TRUE,
      WA_AutoAdjust,TRUE,
      WA_Activate,TRUE,
      WA_SimpleRefresh,TRUE,
      WA_Title,CFGAWEBSTR(MSG_SET_COLOUR_TITLE),
      WA_IDCMP,IDCMP_CLOSEWINDOW|IDCMP_IDCMPUPDATE|IDCMP_REFRESHWINDOW|
         BUTTONIDCMP|SLIDERIDCMP,
      TAG_END)))
   {  AddGList(win,gsgad,-1,-1,NULL);
      AddGList(win,glist,-1,-1,NULL);
      RefreshGList(gsgad,win,NULL,-1);
      GT_RefreshWindow(win,NULL);
      DrawBevelBox(win->RPort,offx+108,offy+4,winw-112,80,
         GT_VisualInfo,visualinfo,
         GTBB_Recessed,TRUE,
         TAG_END);
      SetAPen(win->RPort,pen);
      RectFill(win->RPort,offx+110,offy+5,offx+winw-7,offy+82);
      windowmask=1<<win->UserPort->mp_SigBit;
      while(!done)
      {  gotmask=Wait(windowmask);
         while(msg=GT_GetIMsg(win->UserPort))
         {  switch(msg->Class)
            {  case IDCMP_CLOSEWINDOW:
                  done=TRUE;
                  break;
               case IDCMP_IDCMPUPDATE:
                  GetAttr(WHEEL_HSB,cwgad,(ULONG *)&hsb);
                  ConvertHSBToRGB(&hsb,&rgb);
                  GT_SetGadgetAttrs(rgad,win,NULL,
                     GTSL_Level,rgb.cw_Red>>24,TAG_END);
                  GT_SetGadgetAttrs(ggad,win,NULL,
                     GTSL_Level,rgb.cw_Green>>24,TAG_END);
                  GT_SetGadgetAttrs(bgad,win,NULL,
                     GTSL_Level,rgb.cw_Blue>>24,TAG_END);
                  if(cp)
                  {  ReleasePen(cmap,pen);
                     pen=ObtainBestPen(cmap,
                        rgb.cw_Red,rgb.cw_Green,rgb.cw_Blue,TAG_END);
                     SetAPen(win->RPort,pen);
                     RectFill(win->RPort,offx+110,offy+5,offx+winw-7,offy+82);
                  }
                  else SetRGB32(&scr->ViewPort,pen,
                     rgb.cw_Red,rgb.cw_Green,rgb.cw_Blue);
                  break;
               case IDCMP_GADGETUP:
                  switch(((struct Gadget *)msg->IAddress)->GadgetID)
                  {  case CGID_OK:
                        if(cp)
                        {  cp->red=rgb.cw_Red;
                           cp->green=rgb.cw_Green;
                           cp->blue=rgb.cw_Blue;
                        }
                        done=TRUE;
                        result=TRUE;
                        break;
                     case CGID_CANCEL:
                        if(!cp)
                        {  SetRGB32(&scr->ViewPort,pen,
                              undo.cw_Red,undo.cw_Green,undo.cw_Blue);
                        }
                        done=TRUE;
                        break;
                  }
                  break;
               case IDCMP_MOUSEMOVE:
                  value32=msg->Code;
                  value32|=value32<<8;
                  value32|=value32<<16;
                  switch(((struct Gadget *)msg->IAddress)->GadgetID)
                  {  case CGID_RED:    rgb.cw_Red=value32;break;
                     case CGID_GREEN:  rgb.cw_Green=value32;break;
                     case CGID_BLUE:   rgb.cw_Blue=value32;break;
                  }
                  Setgadgetattrs(cwgad,win,NULL,
                     WHEEL_RGB,&rgb,TAG_END);
                  if(cp)
                  {  ReleasePen(cmap,pen);
                     pen=ObtainBestPen(cmap,
                        rgb.cw_Red,rgb.cw_Green,rgb.cw_Blue,TAG_END);
                     SetAPen(win->RPort,pen);
                     RectFill(win->RPort,offx+110,offy+5,offx+winw-7,offy+82);
                  }
                  else SetRGB32(&scr->ViewPort,pen,
                     rgb.cw_Red,rgb.cw_Green,rgb.cw_Blue);
                  break;
               case IDCMP_REFRESHWINDOW:
                  GT_BeginRefresh(win);
                  GT_EndRefresh(win,TRUE);
                  break;
            }
            GT_ReplyIMsg(msg);
         }
      }
   }
   if(win) CloseWindow(win);
   if(cwgad) DisposeObject(cwgad);
   if(gsgad) DisposeObject(gsgad);
   FreeGadgets(glist);
   if(pens[0]>=0) ReleasePen(cmap,pens[0]);
   if(pens[1]>=0) ReleasePen(cmap,pens[1]);
   SetAttrs(winobj,
      WA_BusyPointer,FALSE,
      GA_ReadOnly,FALSE,
      TAG_END);
   SetGadgetAttrs(layout,pwin,NULL,LAYOUT_DeferLayout,TRUE);
   return result;
}

long Moveselected(struct Window *win,struct Gadget *gad,struct List *list,short n)
{  long selected;
   BOOL update=FALSE;
   selected=Getvalue(gad,LISTBROWSER_Selected);
   if(n<0 && selected>0)
   {  selected--;
      update=TRUE;
   }
   else if(n>0)
   {  struct Node *node=Getnode(list,selected);
      if(node && node->ln_Succ->ln_Succ)
      {  selected++;
         update=TRUE;
      }
   }
   if(update)
   {  Setgadgetattrs(gad,win,NULL,
         LISTBROWSER_Selected,selected,
         LISTBROWSER_MakeVisible,selected,
         TAG_END);
   }
   return update?selected:-1;
}

/* Aks for file name to load/save settings. Returns dynamic string */
UBYTE *Filereq(void *winobj,struct Window *window,struct Gadget *layout,
   UBYTE *title,UBYTE *name,BOOL save)
{  struct FileRequester *fr;
   UBYTE *path=NULL;
   if(fr=AllocAslRequestTags(ASL_FileRequest,
      ASLFR_Window,window,
      ASLFR_SleepWindow,TRUE,
      ASLFR_TitleText,title,
      ASLFR_InitialHeight,Reqheight(window->WScreen),
      ASLFR_InitialDrawer,"ENVARC:" DEFAULTCFG,
      ASLFR_InitialFile,name,
      ASLFR_RejectIcons,TRUE,
      ASLFR_DoSaveMode,save,
      TAG_END))
   {  SetGadgetAttrs(layout,window,NULL,LAYOUT_DeferLayout,FALSE,TAG_END);
      SetAttrs(winobj,WA_BusyPointer,TRUE,GA_ReadOnly,TRUE,TAG_END);
      if(AslRequest(fr,NULL))
      {  long len=strlen(fr->fr_Drawer)+strlen(fr->fr_File)+2;
         if(path=ALLOCTYPE(UBYTE,len,0))
         {  strcpy(path,fr->fr_Drawer);
            AddPart(path,fr->fr_File,len);
         }
      }
      SetAttrs(winobj,WA_BusyPointer,FALSE,GA_ReadOnly,FALSE,TAG_END);
      SetGadgetAttrs(layout,window,NULL,LAYOUT_DeferLayout,TRUE,TAG_END);
      FreeAslRequest(fr);
   }
   return path;
}

UBYTE *Hotkey(UBYTE *label)
{  UBYTE *p=strchr(label,'_');
   if(p) return p+1;
   else return NULL;
}

BOOL Ownscreen(void)
{  return (BOOL)STRIEQUAL(screenname,"AWeb");
}

void Dimensions(struct Window *window,short *dim)
{  short x,y,w,h;
   x=window->LeftEdge;
   y=window->TopEdge;
   w=window->Width-window->BorderLeft-window->BorderRight;
   h=window->Height-window->BorderTop-window->BorderBottom;
   if(x!=dim[0] || y!=dim[1] || w!=dim[2] || h!=dim[3])
   {  dim[0]=x;
      dim[1]=y;
      dim[2]=w;
      dim[3]=h;
      newdimensions=TRUE;
   }
}

void main(int fromcli,struct WBStartup *wbs)
{  ULONG gotmask,cfgmask;
   short nrwindows=0;
   struct Library *WorkbenchBase=OpenLibrary("workbench.library",0);
   if(WorkbenchBase)
   {  if(WorkbenchBase->lib_Version>=44) has35=TRUE;
      CloseLibrary(WorkbenchBase);
   }
   if((IconBase=(struct Library *)Openlib("icon.library",OSNEED(39,44)))
   && (IntuitionBase=(struct IntuitionBase *)Openlib("intuition.library",OSNEED(39,40)))
   && (LocaleBase=(struct LocaleBase *)Openlib("locale.library",OSNEED(38,44)))
   && (GfxBase=(struct GfxBase *)Openlib("graphics.library",OSNEED(39,40)))
   && (GadToolsBase=(struct Library *)Openlib("gadtools.library",OSNEED(39,40)))
   && (AslBase=(struct Library *)Openlib("asl.library",OSNEED(39,44)))
   && (ColorWheelBase=(struct Library *)Openlib("gadgets/colorwheel.gadget",OSNEED(39,44)))
   && (GradientSliderBase=(struct Library *)Openlib("gadgets/gradientslider.gadget",OSNEED(39,44)))
   && (WindowBase=(struct ClassLibrary *)Openclass("window.class"))
   && (LayoutBase=(struct ClassLibrary *)Openclass("gadgets/layout.gadget"))
   && (ButtonBase=(struct ClassLibrary *)Openclass("gadgets/button.gadget"))
   && (ListBrowserBase=(struct ClassLibrary *)Openclass("gadgets/listbrowser.gadget"))
   && (ChooserBase=(struct ClassLibrary *)Openclass("gadgets/chooser.gadget"))
   && (IntegerBase=(struct ClassLibrary *)Openclass("gadgets/integer.gadget"))
   && (SpaceBase=(struct ClassLibrary *)Openclass("gadgets/space.gadget"))
   && (CheckBoxBase=(struct ClassLibrary *)Openclass("gadgets/checkbox.gadget"))
   && (StringBase=(struct ClassLibrary *)Openclass("gadgets/string.gadget"))
   && (PaletteBase=(struct ClassLibrary *)Openclass("gadgets/palette.gadget"))
   && (ClickTabBase=(struct ClassLibrary *)Openclass("gadgets/clicktab.gadget"))
   && (LabelBase=(struct ClassLibrary *)Openclass("images/label.image"))
   && (GlyphBase=(struct ClassLibrary *)Openclass("images/glyph.image"))
   && Initmemory()
   )
   {  
      localeinfo.li_LocaleBase=LocaleBase;
      localeinfo.li_Catalog=OpenCatalogA(NULL,"awebcfg.catalog",NULL);
      maincatalog=OpenCatalogA(NULL,"aweb.catalog",NULL);
#ifndef NEED35
      if(has35)
      {  Adjustmenus35();
      }
#endif
      Initdefprefs();
      Localizemenus();
      Getarguments(fromcli?NULL:wbs);
      if(Dupstartupcheck())
      {  cfgmask=1<<cfgport->mp_SigBit;
         if((pubscreen=LockPubScreen(*screenname?screenname:NULL))
         && (visualinfo=GetVisualInfo(pubscreen,TAG_END))
         && (drawinfo=GetScreenDrawInfo(pubscreen))
         && (amigaimg=NewObject(NULL,"sysiclass",
               SYSIA_DrawInfo,drawinfo,
               SYSIA_Which,AMIGAKEY,
               TAG_END))
         )
         {  if(!Prefsconfiguseslegacyawebpath(NULL))
            {  Loadsettingsprefs(&setprefs,FALSE,NULL);
            }
            if(browser && Openbrowser()) nrwindows++;
            if(program && Openprogram()) nrwindows++;
            if(gui && Opengui()) nrwindows++;
            if(network && Opennetwork()) nrwindows++;
            while(nrwindows)
            {  gotmask=Wait(brmask|prmask|uimask|nwmask|cfgmask|SIGBREAKF_CTRL_C);
               if(gotmask&SIGBREAKF_CTRL_C) break;
               if(gotmask&brmask)
               {  if(!Processbrowser()) nrwindows--;
               }
               if(gotmask&prmask)
               {  if(!Processprogram()) nrwindows--;
               }
               if(gotmask&uimask)
               {  if(!Processgui()) nrwindows--;
               }
               if(gotmask&nwmask)
               {  if(!Processnetwork()) nrwindows--;
               }
               if(gotmask&cfgmask)
               {  Processcfgport();
               }
               if(cfgcommand&CFGCLASS_QUIT) break;
               if((cfgcommand&CFGCLASS_BROWSER) && Openbrowser()) nrwindows++;
               if((cfgcommand&CFGCLASS_PROGRAM) && Openprogram()) nrwindows++;
               if((cfgcommand&CFGCLASS_GUI) && Opengui()) nrwindows++;
               if((cfgcommand&CFGCLASS_NETWORK) && Opennetwork()) nrwindows++;
               if(cfgcommand&CFGCLASS_SNAPSHOT)
               {  Savesettingsprefs(&setprefs,FALSE,NULL);
                  newdimensions=FALSE;
               }
               if(cfgcommand&CFGCLASS_CLASSACT) Openclassact();
               cfgcommand=0;
            }
            Closebrowser();
            Closeprogram();
            Closegui();
            Closenetwork();
            if(newdimensions) Savesettingsprefs(&setprefs,FALSE,NULL);
         }
      }
   }
   Cleanup();
}
