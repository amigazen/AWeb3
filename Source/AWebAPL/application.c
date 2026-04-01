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

/* application.c - AWeb application context object */

#include "aweb.h"
#include "application.h"
#include "timer.h"
#include "task.h"
#include "window.h"
#include "url.h"
#include "startup.h"
#include "jslib.h"
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <intuition/pointerclass.h>
#include <proto/graphics.h>
#include <graphics/gfx.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <images/bitmap.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/utility.h>
#include <proto/layers.h>
#include <proto/wb.h>
#include <proto/bitmap.h>
#include <proto/icon.h>
#include <proto/asl.h>

/*------------------------------------------------------------------------*/

struct Application
{  struct Aobject object;
   struct Screen *screen;
   struct DrawInfo *drawinfo;
   struct VisualInfo *vinfo;
   UBYTE *screenname;
   short pubsignum;
   struct TextFont *systemfont;
   struct NewMenu *menus;
   short bgpen,textpen,linkpen,vlinkpen,alinkpen;
   short blackpen,whitepen,tooltippen;
   struct MsgPort *windowport;
   struct MsgPort *appport;            /* App window or App icon port */
   struct DiskObject *appdob;
   struct AppIcon *appicon;
   USHORT flags;
   LIST(Child) usescreen;
   LIST(Child) usemenus;
   LIST(Child) usebrowser;
   LIST(Child) useoverlap;
   LIST(Child) wantblink;
   LIST(Child) wantmarquee;
   UBYTE *savepath;
   void *blinktimer;
   void *marqueetimer;
   void *animtimer;
   struct SignalSemaphore semaphore;
   short imagewidth,imageheight;
   struct Image *buttons;
   struct BitMap *buttonsbitmap;
   APTR buttonstranspmask;
   short buttonwidth,buttonheight;
   short secbuttonwidth,secbuttonheight;
   short nsbuttonwidth,nsbuttonheight;
   short buttonoffset[NR_BUTTONS][4];  /* x,y,x,y (normal and selected) */
   BOOL buttonstransparent;
   struct Image *animation;
   struct BitMap *animbitmap;
   short animx,animy,animw,animh,animframes,animdx,animdy,animrx,animry;
   void *deficon,*defmapicon,*deferricon;
   void (*processfun[128])(void);
   void *resizehorobj,*resizevertobj;  /* Pointerclass objects */
   void *handobj;                      /* Pointerclass object */
   struct Jcontext *jcontext;
   struct Jobject *jnavigator;         /* JS navigator object */
   struct Jobject *jscreen;            /* JS screen object */
};

#define APPF_SCREENVALID      0x0001   /* Screen data is valid */
#define APPF_MENUSVALID       0x0002   /* Menus are valid */
#define APPF_OURSCREEN        0x0004   /* We opened the screen */
#define APPF_OURBGPENS        0x0008   /* We obtained bg and text pens */
#define APPF_BLINKON          0x0010   /* Blink phase is on */
#define APPF_ANIMON           0x0020   /* Animation gadgets are on */
#define APPF_ICONIFIED        0x0040   /* App is iconified */

struct Child
{  NODE(Child);
   struct Aobject *object;
};

static LIST(Application) apps;

static struct Jcontext *jctofree;

struct Animgad
{  NODE(Animgad);
   struct Window *win;
   struct Gadget *gad;
};

static LIST(Animgad) animgads;

#define AOAPP_Blinktimer      (AOAPP_Dummy+128)
   /* (BOOL) Toggle blink status */

#define AOAPP_Marqueetimer    (AOAPP_Dummy+130)
   /* (BOOL) Update marquee scroll positions */

#define AOAPP_Animtimer       (AOAPP_Dummy+129)
   /* (BOOL) Step continuous transfer anim */

static struct TagItem blinktimermap[]=
{  AOTIM_Ready,AOAPP_Blinktimer,
   TAG_END,
};

static struct TagItem marqueetimermap[]=
{  AOTIM_Ready,AOAPP_Marqueetimer,
   TAG_END,
};

static struct TagItem animtimermap[]=
{  AOTIM_Ready,AOAPP_Animtimer,
   TAG_END,
};

static struct EasyStruct closereq=
{  sizeof(struct EasyStruct),
   0,
   "AWeb Request",
   "AWeb can't close its screen.\nPlease close all windows.",
   "OK",
};

enum LOADREQ_LEVELS
{  LQL_IMGICON=1,LQL_MAPICON,LQL_ERRICON,
   LQL_GUIDOB,LQL_GUIIMG,LQL_ANIMDOB,LQL_ANIMIMG,
   LQL_NUMBERPLUS1
};
#define LQL_NUMBER (LQL_NUMBERPLUS1-1)

struct Menuflags
{  UBYTE *cmd;
   ULONG flags;
};
static struct Menuflags menuflags[]=
{  { "@DRAGGING",CHECKIT|MENUTOGGLE },
   { "@NOPROXY",CHECKIT|MENUTOGGLE },
   { "@DEBUGJS",CHECKIT|MENUTOGGLE },
   { "@LOADIMGALL",CHECKIT },
   { "@LOADIMGMAPS",CHECKIT },
   { "@LOADIMGOFF",CHECKIT },
   { "@BGIMAGES",CHECKIT|MENUTOGGLE },
   { "@BGSOUND",CHECKIT|MENUTOGGLE },
   { NULL,0 }
};

static UWORD __chip fontmap[8]=
{  0x0000,0x7e00,0x7e00,0x7e00,0x7e00,0x7e00,0x7e00,0x0000
};
static ULONG charloc[224];
static UWORD charspace[224];
static UWORD charkern[224];

struct TextFont pwfont;

extern struct SignalSemaphore displaysema;

/*-----------------------------------------------------------------------*/

/* Pointer data. */
static UWORD resizehordata[]=
{  0x0000,0x06c0,0x06c0,0x06c0,0x16d0,0x36d8,0x76dc,0xfefe,
   0x76dc,0x36d8,0x16d0,0x06c0,0x06c0,0x06c0,0x0000,0x0000,
   
   0x0000,0x06c0,0x0440,0x0440,0x1450,0x2448,0x4444,0x8002,
   0x4444,0x2448,0x1450,0x0440,0x0440,0x06c0,0x0000,0x0000,
};
static UWORD resizevertdata[]=
{  0x0100,0x0380,0x07c0,0x0fe0,0x0100,0x7ffc,0x7ffc,0x0000,
   0x7ffc,0x7ffc,0x0100,0x0fe0,0x07c0,0x0380,0x0100,0x0000,
   
   0x0100,0x0280,0x0440,0x0820,0x0000,0x7efc,0x4004,0x0000,
   0x4004,0x7efc,0x0000,0x0820,0x0440,0x0280,0x0100,0x0000,
};
static UWORD handdata[]=
{  0x0400,0x0e00,0x0e00,0x0e00,0x0e00,0x0f80,0x0fe0,0x4ff8,
   0xeff8,0x7ff8,0x3ff8,0x3ff8,0x1ff8,0x1ff8,0x0ff0,0x0ff0,
   
   0x0400,0x0a00,0x0a00,0x0a00,0x0a00,0x0b80,0x0ae0,0x4ab8,
   0xa828,0x5808,0x2808,0x2808,0x1008,0x1008,0x0810,0x0ff0
};

static struct BitMap resizehorbmap=
{  2,16,0,2,0,(PLANEPTR)resizehordata,(PLANEPTR)(resizehordata+16)
};
static struct BitMap resizevertbmap=
{  2,16,0,2,0,(PLANEPTR)resizevertdata,(PLANEPTR)(resizevertdata+16)
};
static struct BitMap handbmap=
{  2,16,0,2,0,(PLANEPTR)handdata,(PLANEPTR)(handdata+16)
};

static int resizehorhot[2]={ 7,7 };
static int resizeverthot[2]={ 7,7 };
static int handhot[2]={ 5,1 };

   
/*-----------------------------------------------------------------------*/

/* Read pointer data */
static void Readpointerdata(USHORT type)
{  UBYTE *filename;
   UWORD *data;
   int *hot;
   long fh;
   UBYTE buf[512]="";
   long x,y;
   UBYTE *p;
   UBYTE line[16];
   UWORD w1,w2;
   switch(type)
   {  case APTR_RESIZEHOR:
         filename="AWeb:images/vsizepointer";
         data=resizehordata;
         hot=resizehorhot;
         break;
      case APTR_RESIZEVERT:
         filename="AWeb:images/hsizepointer";
         data=resizevertdata;
         hot=resizeverthot;
         break;
      case APTR_HAND:
         filename="AWeb:images/handpointer";
         data=handdata;
         hot=handhot;
         break;
      default:
         return;
   }
   if(fh=Open(filename,MODE_OLDFILE))
   {  x=Read(fh,buf,511);
      if(x>0) buf[x]='\0';
      Close(fh);
   }
   if(*buf)
   {  x=0;
      y=0;
      for(p=buf;*p && y<16;p++)
      {  switch(*p)
         {  default:
               if(x<16) line[x++]=0;
               break;
            case '1':
            case '2':
            case '3':
               if(x<16) line[x++]=*p-'0';
               break;
            case '4':
            case '5':
            case '6':
            case '7':
               if(x<16)
               {  line[x]=*p-'4';
                  hot[0]=x;
                  hot[1]=y;
                  x++;
               }
               break;
            case '\n':
               for(;x<16;x++) line[x]=0;
               w1=w2=0;
               for(x=0;x<16;x++)
               {  w1|=(line[x]&1)<<(15-x);
                  w2|=((line[x]&2)>>1)<<(15-x);
               }
               data[y]=w1;
               data[y+16]=w2;
               x=0;
               y++;
               break;
         }
      }
      for(;y<16;y++)
      {  data[y]=0;
         data[y+16]=0;
      }
   }
}

/*-----------------------------------------------------------------------*/

static ULONG colours[256]=
{  0x000000,0xA0A0A0,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
   0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF,
   0xEFEFEF,0xDFDFDF,0xD3D3D3,0xC3C3C3,0xB7B7B7,0xABABAB,0x9B9B9B,0x8F8F8F,
   0x7F7F7F,0x737373,0x676767,0x575757,0x4B4B4B,0x3B3B3B,0x2F2F2F,0x232323,
   0xFF0000,0xEF0000,0xE30000,0xD70000,0xCB0000,0xBF0000,0xB30000,0xA70000,
   0x9B0000,0x8B0000,0x7F0000,0x730000,0x670000,0x5B0000,0x4F0000,0x400000,
   0xFFDADA,0xFFBABA,0xFF9F9F,0xFF7F7F,0xFF5F5F,0xFF4040,0xFF2020,0xFF0000,
   0xFCA85C,0xFC9840,0xFC8820,0xFC7800,0xE46C00,0xCC6000,0xB45400,0x9C4C00,
   0xFCFCD8,0xFCFCB8,0xFCFC9C,0xFCFC7C,0xFCF85C,0xFCF440,0xFCF420,0xFCF400,
   0xE4D800,0xCCC400,0xB4AC00,0x9C9C00,0x848400,0x706C00,0x585400,0x404000,
   0xD0FC5C,0xC4FC40,0xB4FC20,0xA0FC00,0x90E400,0x80CC00,0x74B400,0x609C00,
   0xD8FCD8,0xBCFCB8,0x9CFC9C,0x80FC7C,0x60FC5C,0x40FC40,0x20FC20,0x00FC00,
   0x00FF00,0x00EF00,0x00E300,0x00D700,0x07CB00,0x07BF00,0x07B300,0x07A700,
   0x079B00,0x078B00,0x077F00,0x077300,0x076700,0x075B00,0x074F00,0x044000,
   0xDAFFFF,0xB8FCFC,0x9CFCFC,0x7CFCF8,0x5CFCFC,0x40FCFC,0x20FCFC,0x00FCFC,
   0x00E4E4,0x00CCCC,0x00B4B4,0x009C9C,0x008484,0x007070,0x005858,0x004040,
   0x5CBCFC,0x40B0FC,0x20A8FC,0x009CFC,0x008CE4,0x007CCC,0x006CB4,0x005C9C,
   0xDADAFF,0xBABFFF,0x9F9FFF,0x7F80FF,0x5F60FF,0x4040FF,0x2025FF,0x0005FF,
   0x0000FF,0x0000EF,0x0000E3,0x0000D7,0x0000CB,0x0000BF,0x0000B3,0x0000A7,
   0x00009B,0x00008B,0x00007F,0x000073,0x000067,0x00005B,0x00004F,0x000040,
   0xF0DAFF,0xE5BAFF,0xDA9FFF,0xD07FFF,0xCA5FFF,0xBF40FF,0xB520FF,0xAA00FF,
   0x9A00E5,0x8000CF,0x7500B5,0x60009F,0x500085,0x450070,0x35005A,0x2A0040,
   0xFFDAFF,0xFFBAFF,0xFF9FFF,0xFF7FFF,0xFF5FFF,0xFF40FF,0xFF20FF,0xFF00FF,
   0xE000E5,0xCA00CF,0xB500B5,0x9F009F,0x850085,0x6F0070,0x5A005A,0x400040,
   0xFFE9DE,0xF7DDD0,0xF0D1C3,0xE9C7B7,0xE1BAAA,0xDAB09E,0xD3A494,0xCC9B89,
   0xC59080,0xBE8676,0xB67C6B,0xAF7363,0xA86B5A,0xA16152,0x9A594A,0x935043,
   0x8B483B,0x843F34,0x7E392E,0x773128,0x702C23,0x69261D,0x611F18,0x5A1B14,
   0x531510,0x4C110D,0x450C09,0x3E0907,0x360504,0x2F0302,0x280101,0x210000,
   0xFF5858,0xFFBE80,0xFFFE82,0x82FF84,0x80FFFF,0x8080FF,0xBF80FF,0xFE80FF,
   0xC72B2B,0xC74F2B,0xC7772B,0xC79F2B,0xC7C72B,0x9FC72B,0x77C72B,0x4FC72B,
   0x2BC733,0x2BC75F,0x2BC78B,0x2BC7B7,0x2BABC7,0x2B7FC7,0x2B53C7,0x2F2BC7,
   0x5B2BC7,0x872BC7,0xB32BC7,0xC72BAF,0xC72B83,0xC72B57,0xC72B2B,0xFFFFFF,
};

/*------------------------------------------------------------------------*/

/* JS methods */
static void Methodjavaenabled(struct Jcontext *jc)
{  Jasgboolean(jc,NULL,FALSE);
}

static void Methodtaintenabled(struct Jcontext *jc)
{  Jasgboolean(jc,NULL,FALSE);
}

/*------------------------------------------------------------------------*/

/* Get the list for this relationship */
static LIST(Child) *Childlist(struct Application *app,long relation)
{  LIST(Child) *list=NULL;
   switch(relation)
   {  case AOREL_APP_USE_SCREEN:
         list=&app->usescreen;
         break;
      case AOREL_APP_USE_MENUS:
         list=&app->usemenus;
         break;
      case AOREL_APP_USE_BROWSER:
         list=&app->usebrowser;
         break;
      case AOREL_APP_USE_OVERLAP:
         list=&app->useoverlap;
         break;
      case AOREL_APP_WANT_BLINK:
         list=&app->wantblink;
         break;
      case AOREL_APP_WANT_MARQUEE:
         list=&app->wantmarquee;
         break;
   }
   return list;
}

/* Send AOM_SET to all children in this relationship */
static void Broadcast(struct Application *app,long relation,...)
{  LIST(Child) *list=Childlist(app,relation);
   struct Amset ams;
   struct Child *ch,*next;
   if(list)
   {  ams.method=AOM_SET;
      ams.tags=VARARG(relation);
      for(ch=list->first;ch->next;ch=next)
      {  next=ch->next;
         AmethodA(ch->object,&ams);
      }
   }
}

/* Send AOM_SET to all children in this relationship, but any one of
 * the children might remove itself as a result of this set to another child. */
static void Broadcastsafe(struct Application *app,long relation,...)
{  LIST(Child) *list=Childlist(app,relation);
   struct Amset ams;
   struct Child *ch,*chs,**havehad;
   long i,n;
   if(list)
   {  ams.method=AOM_SET;
      ams.tags=VARARG(relation);
      for(ch=list->first,i=0;ch->next;ch=ch->next) i++;
      havehad=ALLOCTYPE(struct Child *,i,MEMF_CLEAR);
      n=0;
      for(chs=list->first;chs->next;)
      {  AmethodA(chs->object,&ams);
         if(havehad)
         {  havehad[n++]=chs;
            for(ch=list->first;ch->next;ch=ch->next)
            {  for(i=0;i<n;i++)
               {  if(havehad[i]==ch) break;
               }
               if(i>=n) break;   /* Not yet had */
            }
            chs=ch;              /* Either not yet had, or list tail */
         }
      }
      if(havehad) FREE(havehad);
   }
}

/* Set a new save path */
static void Setsavepath(struct Application *app,UBYTE *path)
{  UBYTE *end,*newpath;
   if(path)
   {  end=PathPart(path);
      newpath=Dupstr(path,end-path);
   }
   else
   {  newpath=Dupstr(prefs.savepath,-1);
   }
   if(newpath)
   {  if(app->savepath) FREE(app->savepath);
      app->savepath=newpath;
   }
}
   

/*------------------------------------------------------------------------*/

/* Load button imagery */
static void Getbuttonsinfo(struct Application *app)
{  struct DiskObject *dob;
   UBYTE **ttp;
   short i;
   app->buttonwidth=app->buttonheight=0;
   app->secbuttonwidth=app->secbuttonheight=0;
   app->nsbuttonwidth=app->nsbuttonheight=0;
   for(i=0;i<NR_BUTTONS;i++)
   {  app->buttonoffset[i][0]=app->buttonoffset[i][1]=
         app->buttonoffset[i][2]=app->buttonoffset[i][3]=-1;
   }
   app->buttonstransparent=FALSE;
   if(!BitMapBase) return;
   if(dob=GetDiskObject("AWeb:Images/def_buttons"))
   {  for(ttp=dob->do_ToolTypes;*ttp;ttp++)
      {  if(STRNIEQUAL(*ttp,"BUTTON_SIZE=",12))
            sscanf(*ttp+12," %hd,%hd",&app->buttonwidth,&app->buttonheight);
         else if(STRNIEQUAL(*ttp,"FORWARD=",8)) sscanf(*ttp+8," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_FORWARD][0],&app->buttonoffset[BUTF_FORWARD][1],
            &app->buttonoffset[BUTF_FORWARD][2],&app->buttonoffset[BUTF_FORWARD][3]);
         else if(STRNIEQUAL(*ttp,"BACK=",5)) sscanf(*ttp+5," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_BACK][0],&app->buttonoffset[BUTF_BACK][1],
            &app->buttonoffset[BUTF_BACK][2],&app->buttonoffset[BUTF_BACK][3]);
         else if(STRNIEQUAL(*ttp,"HOME=",5)) sscanf(*ttp+5," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_HOME][0],&app->buttonoffset[BUTF_HOME][1],
            &app->buttonoffset[BUTF_HOME][2],&app->buttonoffset[BUTF_HOME][3]);
         else if(STRNIEQUAL(*ttp,"ADDHOTLIST=",11)) sscanf(*ttp+11," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_ADDHOTLIST][0],&app->buttonoffset[BUTF_ADDHOTLIST][1],
            &app->buttonoffset[BUTF_ADDHOTLIST][2],&app->buttonoffset[BUTF_ADDHOTLIST][3]);
         else if(STRNIEQUAL(*ttp,"HOTLIST=",8)) sscanf(*ttp+8," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_HOTLIST][0],&app->buttonoffset[BUTF_HOTLIST][1],
            &app->buttonoffset[BUTF_HOTLIST][2],&app->buttonoffset[BUTF_HOTLIST][3]);
         else if(STRNIEQUAL(*ttp,"CANCEL=",7)) sscanf(*ttp+7," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_CANCEL][0],&app->buttonoffset[BUTF_CANCEL][1],
            &app->buttonoffset[BUTF_CANCEL][2],&app->buttonoffset[BUTF_CANCEL][3]);
         else if(STRNIEQUAL(*ttp,"NETSTATUS=",10)) sscanf(*ttp+10," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_NETSTATUS][0],&app->buttonoffset[BUTF_NETSTATUS][1],
            &app->buttonoffset[BUTF_NETSTATUS][2],&app->buttonoffset[BUTF_NETSTATUS][3]);
         else if(STRNIEQUAL(*ttp,"SEARCH=",7)) sscanf(*ttp+7," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_SEARCH][0],&app->buttonoffset[BUTF_SEARCH][1],
            &app->buttonoffset[BUTF_SEARCH][2],&app->buttonoffset[BUTF_SEARCH][3]);
         else if(STRNIEQUAL(*ttp,"RELOAD=",7)) sscanf(*ttp+7," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_RELOAD][0],&app->buttonoffset[BUTF_RELOAD][1],
            &app->buttonoffset[BUTF_RELOAD][2],&app->buttonoffset[BUTF_RELOAD][3]);
         else if(STRNIEQUAL(*ttp,"LOADIMAGES=",11)) sscanf(*ttp+11," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_LOADIMAGES][0],&app->buttonoffset[BUTF_LOADIMAGES][1],
            &app->buttonoffset[BUTF_LOADIMAGES][2],&app->buttonoffset[BUTF_LOADIMAGES][3]);
         else if(STRNIEQUAL(*ttp,"SECBUTTON_SIZE=",15))
            sscanf(*ttp+15," %hd,%hd",&app->secbuttonwidth,&app->secbuttonheight);
         else if(STRNIEQUAL(*ttp,"UNSECURE=",9)) sscanf(*ttp+9," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_UNSECURE][0],&app->buttonoffset[BUTF_UNSECURE][1],
            &app->buttonoffset[BUTF_UNSECURE][2],&app->buttonoffset[BUTF_UNSECURE][3]);
         else if(STRNIEQUAL(*ttp,"SECURE=",7)) sscanf(*ttp+7," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_SECURE][0],&app->buttonoffset[BUTF_SECURE][1],
            &app->buttonoffset[BUTF_SECURE][2],&app->buttonoffset[BUTF_SECURE][3]);
         else if(STRNIEQUAL(*ttp,"NSBUTTON_SIZE=",14))
            sscanf(*ttp+14," %hd,%hd",&app->nsbuttonwidth,&app->nsbuttonheight);
         else if(STRNIEQUAL(*ttp,"NSCANCEL=",9)) sscanf(*ttp+9," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_NSCANCEL][0],&app->buttonoffset[BUTF_NSCANCEL][1],
            &app->buttonoffset[BUTF_NSCANCEL][2],&app->buttonoffset[BUTF_NSCANCEL][3]);
         else if(STRNIEQUAL(*ttp,"NSCANCELALL=",12)) sscanf(*ttp+12," %hd,%hd,%hd,%hd",
            &app->buttonoffset[BUTF_NSCANCELALL][0],&app->buttonoffset[BUTF_NSCANCELALL][1],
            &app->buttonoffset[BUTF_NSCANCELALL][2],&app->buttonoffset[BUTF_NSCANCELALL][3]);
         else if(STRIEQUAL(*ttp,"TRANSPARENT")) app->buttonstransparent=TRUE;
      }
      FreeDiskObject(dob);
      Setloadreqlevel(LQL_GUIDOB,LQL_NUMBER);
      if(app->buttonwidth && app->buttonheight)
      {  app->buttons=BitMapObject,
            BITMAP_SourceFile,"AWeb:Images/def_buttons",
            BITMAP_Screen,app->screen,
            BITMAP_Masking,app->buttonstransparent,
         End;
         if(app->buttons)
         {  GetAttr(BITMAP_BitMap,app->buttons,(ULONG *)&app->buttonsbitmap);
            if(app->buttonstransparent)
               GetAttr(BITMAP_MaskPlane,app->buttons,(ULONG *)&app->buttonstranspmask);
            app->imagewidth=app->buttonwidth;
            app->imageheight=app->buttonheight;
            if(!app->secbuttonwidth) app->secbuttonwidth=app->buttonwidth;
            if(!app->secbuttonheight) app->secbuttonheight=app->buttonheight;
            if(!app->nsbuttonwidth) app->nsbuttonwidth=app->buttonwidth;
            if(!app->nsbuttonheight) app->nsbuttonheight=app->buttonheight;
         }
      }
   }
   Setloadreqlevel(LQL_GUIIMG,LQL_NUMBER);
}

/* Load animation imagery */
static void Getaniminfo(struct Application *app)
{  struct DiskObject *dob;
   UBYTE **ttp;
   app->animw=app->animh=app->animframes=app->animx=app->animy=app->animdx=app->animdy=0;
   app->animrx=app->animry=-1;
   if(!BitMapBase) return;
   if(dob=GetDiskObject("AWeb:Images/def_transferanim"))
   {  for(ttp=dob->do_ToolTypes;*ttp;ttp++)
      {  if(STRNIEQUAL(*ttp,"SIZE=",5))
            sscanf(*ttp+5," %hd,%hd",&app->animw,&app->animh);
         else if(STRNIEQUAL(*ttp,"FIRST=",6))
            sscanf(*ttp+6," %hd,%hd",&app->animx,&app->animy);
         else if(STRNIEQUAL(*ttp,"FRAMES=",7))
            sscanf(*ttp+7," %hd",&app->animframes);
         else if(STRNIEQUAL(*ttp,"DELTA=",6))
            sscanf(*ttp+6," %hd,%hd",&app->animdx,&app->animdy);
         else if(STRNIEQUAL(*ttp,"REST=",5))
            sscanf(*ttp+5," %hd,%hd",&app->animrx,&app->animry);
      }
      FreeDiskObject(dob);
      Setloadreqlevel(LQL_ANIMDOB,LQL_NUMBER);
      if(app->animw && app->animh && app->animframes)
      {  app->animation=BitMapObject,
            BITMAP_SourceFile,"AWeb:Images/def_transferanim",
            BITMAP_Screen,app->screen,
         End;
         if(app->animation)
         {  GetAttr(BITMAP_BitMap,app->animation,(ULONG *)&app->animbitmap);
         }
      }
   }
   if(!app->animation)
   {  app->animw=app->animh=16;
   }
   Setloadreqlevel(LQL_ANIMIMG,LQL_NUMBER);
}

/*------------------------------------------------------------------------*/

/* Free base menus */
static void Freemenus(struct Application *app)
{  short i;
   if(app->menus)
   {  for(i=0;app->menus[i].nm_Type;i++)
      {  if(app->menus[i].nm_Label && app->menus[i].nm_Label!=NM_BARLABEL)
            FREE((void *)app->menus[i].nm_Label);
      }
      FREE(app->menus);
      app->menus=NULL;
   }
}

/* Create base menus */
static BOOL Makemenus(struct Application *app)
{  long i,j,nrmenudata;
   struct Menuentry *me;
   struct NewMenu *newmenus;
   short mnum=-1,inum=-1,snum=-1;
   BOOL hasmenu=FALSE,hasitem=FALSE,valid=TRUE;
   if(app->menus) Freemenus(app);
   app->menus=NULL;
   nrmenudata=1;
   for(me=prefs.menus.first;valid && me->next;me=me->next)
   {  nrmenudata++;
      switch(me->type)
      {  case AMENU_MENU:
            hasmenu=TRUE;
            hasitem=FALSE;
            break;
         case AMENU_ITEM:
            valid=hasmenu;
            hasitem=TRUE;
            break;
         case AMENU_SUB:
            valid=hasitem;
            break;
         case AMENU_SEPARATOR:
            valid=hasmenu || hasitem;
            break;
      }
   }
   if(!valid) return FALSE;
   if(!(newmenus=ALLOCSTRUCT(NewMenu,nrmenudata,MEMF_CLEAR))) return FALSE;
   for(i=0,me=prefs.menus.first;me->next;i++,me=me->next)
   {  switch(me->type)
      {  case AMENU_MENU:
            mnum++;
            inum=snum=-1;
            newmenus[i].nm_Type=NM_TITLE;
            newmenus[i].nm_Label=Dupstr(me->title,-1);
            me->menunum=SHIFTMENU(mnum)|SHIFTITEM(NOITEM)|SHIFTSUB(NOSUB);
            break;
         case AMENU_ITEM:
            inum++;
            snum=-1;
            newmenus[i].nm_Type=NM_ITEM;
            newmenus[i].nm_Label=Dupstr(me->title,-1);
            if(me->scut[0]) newmenus[i].nm_CommKey=me->scut;
            for(j=0;menuflags[j].cmd;j++)
            {  if(STRIEQUAL(menuflags[j].cmd,me->cmd))
               {  newmenus[i].nm_Flags|=menuflags[j].flags;
               }
            }
            me->menunum=SHIFTMENU(mnum)|SHIFTITEM(inum)|SHIFTSUB(NOSUB);
            break;
         case AMENU_SUB:
            snum++;
            newmenus[i].nm_Type=NM_SUB;
            newmenus[i].nm_Label=Dupstr(me->title,-1);
            if(me->scut[0]) newmenus[i].nm_CommKey=me->scut;
            for(j=0;menuflags[j].cmd;j++)
            {  if(STRIEQUAL(menuflags[j].cmd,me->cmd))
               {  newmenus[i].nm_Flags|=menuflags[j].flags;
               }
            }
            me->menunum=SHIFTMENU(mnum)|SHIFTITEM(inum)|SHIFTSUB(snum);
            break;
         case AMENU_SEPARATOR:
            if(me->next->next && me->next->type==AMENU_SUB)
            {  snum++;
               newmenus[i].nm_Type=NM_SUB;
               me->menunum=SHIFTMENU(mnum)|SHIFTITEM(inum)|SHIFTSUB(snum);
            }
            else
            {  inum++;
               snum=-1;
               newmenus[i].nm_Type=NM_ITEM;
               me->menunum=SHIFTMENU(mnum)|SHIFTITEM(inum)|SHIFTSUB(NOSUB);
            }
            newmenus[i].nm_Label=NM_BARLABEL;
            break;
      }
   }
   newmenus[i].nm_Type=NM_END;
   app->menus=newmenus;
   app->flags|=APPF_MENUSVALID;
}

/* Menus have changed */
static void Newappmenus(struct Application *app)
{  app->flags&=~APPF_MENUSVALID;
   Broadcast(app,AOREL_APP_USE_MENUS,
      AOAPP_Menus,NULL,
      TAG_END);
   Freemenus(app);
   Makemenus(app);
   Broadcast(app,AOREL_APP_USE_MENUS,
      AOAPP_Menus,app->menus,
      TAG_END);
}

/*------------------------------------------------------------------------*/

/* Repeate byte value across long */
static ULONG Rgbexpand(UBYTE c)
{  ULONG a=c|(c<<8);
   return a|(a<<16);
}

/* Allocate a spread palette */
static void Setcolors(struct Application *app,short depth)
{  struct ViewPort *vp=&app->screen->ViewPort;
   struct ColorMap *cm=vp->ColorMap;
   long skip,i;
   ULONG c,r,g,b;
   /* If grayscale, change the mouse pointer colours to gray */
   if(prefs.loadpalette==LOADPAL_GRAY)
   {  ULONG table[9];
      GetRGB32(cm,17,3,table);
      for(i=0;i<3;i++)
      {  r=table[3*i]>>24;
         g=table[3*i+1]>>24;
         b=table[3*i+2]>>24;
         c=Rgbexpand((299*r + 588*g + 113*b)/1000);
         ReleasePen(cm,17+i);
         ObtainPen(cm,17+i,c,c,c,PEN_EXCLUSIVE);
      }
   }
   if(depth>=4)
   {  i=(1<<depth)-8;               /* We allocated 8 pens */
      if(depth>=5) i-=3;            /* Intuition allocated 3 more for the pointer */
      skip=256/i;
      for(i=0;i<256;i+=skip)
      {  if(prefs.loadpalette==LOADPAL_GRAY)
         {  r=g=b=Rgbexpand(255-i);
         }
         else
         {  c=colours[i];
            r=Rgbexpand(c>>16);
            g=Rgbexpand((c>>8)&0xff);
            b=Rgbexpand(c&0xff);
         }
         ObtainPen(cm,-1,r,g,b,0);
      }
   }
}

/* Load our prefs palette */
void Loadpalette(struct Application *app)
{  short i;
   for(i=0;i<8;i++)
   {  if(prefs.loadpalette==LOADPAL_GRAY && prefs.screendepth<=8)
      {  ULONG r,g,b,c;
         r=prefs.scrpalette[3*i]>>24;
         g=prefs.scrpalette[3*i+1]>>24;
         b=prefs.scrpalette[3*i+2]>>24;
         c=Rgbexpand((299*r + 588*g + 113*b)/1000);
         SetRGB32(&app->screen->ViewPort,i,c,c,c);
      }
      else
      {  SetRGB32(&app->screen->ViewPort,i,prefs.scrpalette[3*i],
           prefs.scrpalette[3*i+1],prefs.scrpalette[3*i+2]);
      }
   }
}

/* Release obtained pens */
static void Releaseapppens(struct Application *app)
{  if(app->flags&APPF_OURBGPENS)
   {  if(app->bgpen>=0) ReleasePen(app->screen->ViewPort.ColorMap,app->bgpen);
      if(app->textpen>=0) ReleasePen(app->screen->ViewPort.ColorMap,app->textpen);
   }
   if(app->linkpen>=0) ReleasePen(app->screen->ViewPort.ColorMap,app->linkpen);
   if(app->vlinkpen>=0) ReleasePen(app->screen->ViewPort.ColorMap,app->vlinkpen);
   if(app->alinkpen>=0) ReleasePen(app->screen->ViewPort.ColorMap,app->alinkpen);
   if(app->blackpen>=0) ReleasePen(app->screen->ViewPort.ColorMap,app->blackpen);
   if(app->whitepen>=0) ReleasePen(app->screen->ViewPort.ColorMap,app->whitepen);
   if(app->tooltippen>=0) ReleasePen(app->screen->ViewPort.ColorMap,app->tooltippen);
   app->bgpen=-1;
   app->textpen=-1;
   app->linkpen=-1;
   app->vlinkpen=-1;
   app->alinkpen=-1;
   app->blackpen=-1;
   app->whitepen=-1;
   app->tooltippen=-1;
   app->flags&=~APPF_OURBGPENS;
}

/* Obtain our default pens */
static void Obtainapppens(struct Application *app)
{  app->linkpen=ObtainBestPen(app->screen->ViewPort.ColorMap,
      prefs.newlink.red,prefs.newlink.green,prefs.newlink.blue,TAG_END);
   app->vlinkpen=ObtainBestPen(app->screen->ViewPort.ColorMap,
      prefs.oldlink.red,prefs.oldlink.green,prefs.oldlink.blue,TAG_END);
   app->alinkpen=ObtainBestPen(app->screen->ViewPort.ColorMap,
      prefs.selectlink.red,prefs.selectlink.green,prefs.selectlink.blue,TAG_END);
   app->blackpen=ObtainBestPen(app->screen->ViewPort.ColorMap,
      0,0,0,TAG_END);
   app->whitepen=ObtainBestPen(app->screen->ViewPort.ColorMap,
      0xffffffff,0xffffffff,0xffffffff,TAG_END);
   app->tooltippen=ObtainBestPen(app->screen->ViewPort.ColorMap,
      0xffffffff,0xffffffff,0xcccccccc,TAG_END);
   if(prefs.screenpens)
   {  app->bgpen=app->drawinfo->dri_Pens[BACKGROUNDPEN];
      app->textpen=app->drawinfo->dri_Pens[TEXTPEN];
   }
   else
   {  app->bgpen=ObtainBestPen(app->screen->ViewPort.ColorMap,
         prefs.background.red,prefs.background.green,prefs.background.blue,TAG_END);
      app->textpen=ObtainBestPen(app->screen->ViewPort.ColorMap,
         prefs.text.red,prefs.text.green,prefs.text.blue,TAG_END);
      app->flags|=APPF_OURBGPENS;
   }
}

/* Pen settings have changed */
static void Newapppens(struct Application *app)
{  Releaseapppens(app);
   Obtainapppens(app);
   Redisplayall();   /* Should be a APP_USE_PENS relation (Window) .... */
}

/* Set anim gadgets */
static void Setanimgadsactive(BOOL active)
{  struct Animgad *ag;
   for(ag=animgads.first;ag->next;ag=ag->next)
   {  Setgadgetattrs(ag->gad,ag->win,NULL,LEDGGA_Active,active,TAG_END);
   }
}

/* Process AppWindow messages */
static void Processappwindow(void)
{  struct Application *app;
   struct AppMessage *msg;
   void *win;
   for(app=apps.first;app->next;app=app->next)
   {  while(msg=(struct AppMessage *)GetMsg(app->appport))
      {  if(win=Findwindow(msg->am_ID))
         {  Asetattrs(win,AOWIN_Appmessage,msg,TAG_END);
         }
         ReplyMsg(msg);
      }
   }
}

/* Process AppIcon message */
static void Processappicon(void)
{  struct Application *app;
   struct AppMessage *msg;
   short i;
   UBYTE buffer[264]="file:///";
   UBYTE *filename=buffer+8;
   void *win;
   void *url;
   for(app=apps.first;app->next;app=app->next)
   {  while(msg=(struct AppMessage *)GetMsg(app->appport))
      {  Iconify(FALSE);
         for(i=0;i<msg->am_NumArgs;i++)
         {  if(NameFromLock(msg->am_ArgList[i].wa_Lock,filename,256)
            && AddPart(filename,msg->am_ArgList[i].wa_Name,256))
            {  if(url=Findurl("",buffer,0))
               {  if(win=Anewobject(AOTP_WINDOW,TAG_END))
                  {  Inputwindoc(win,url,NULL,0);
                  }
               }
            }
         }
         ReplyMsg(msg);
      }
   }
}

/*------------------------------------------------------------------------*/

/* Close the screen */
static void Appclosescreen(struct Application *app)
{  if(app->screen)
   {  app->flags&=~APPF_SCREENVALID;
      Tooltip(NULL,0,0);
      Broadcastsafe(app,AOREL_APP_USE_SCREEN,
         AOAPP_Screenvalid,FALSE,
         TAG_END);
      if(app->buttons)
      {  DisposeObject(app->buttons);
         app->buttons=NULL;
         app->buttonsbitmap=NULL;
         app->buttonstranspmask=NULL;
      }
      if(app->animation)
      {  DisposeObject(app->animation);
         app->animation=NULL;
         app->animbitmap=NULL;
      }
      if(app->deficon)
      {  DisposeObject(app->deficon);
         app->deficon=NULL;
      }
      if(app->defmapicon)
      {  DisposeObject(app->defmapicon);
         app->defmapicon=NULL;
      }
      if(app->deferricon)
      {  DisposeObject(app->deferricon);
         app->deferricon=NULL;
      }
      if(app->vinfo)
      {  FreeVisualInfo(app->vinfo);
         app->vinfo=NULL;
      }
      if(app->drawinfo)
      {  FreeScreenDrawInfo(app->screen,app->drawinfo);
         app->drawinfo=NULL;
      }
      Releaseapppens(app);
      if(app->flags&APPF_OURSCREEN)
      {  SetSignal(0,1<<app->pubsignum);  /* Clear the signal */
         while(!PubScreenStatus(app->screen,PSNF_PRIVATE))
         {  ULONG rwmask=0,mask;
            struct Window *rw=BuildEasyRequest(app->screen->FirstWindow,&closereq,0);
            if(rw==(struct Window *)-1) rw=NULL;
            if(rw && rw->UserPort) rwmask=(1<<rw->UserPort->mp_SigBit);
            for(;;)
            {  mask=Waitprocessaweb((1<<app->pubsignum)|rwmask);
               if(mask&(1<<app->pubsignum)) break;
               if(mask&rwmask)
               {  if(SysReqHandler(rw,NULL,FALSE)>=0) break;
               }
            }
            if(rw) FreeSysRequest(rw);
         }
         CloseScreen(app->screen);
      }
      else
      {  UnlockPubScreen(NULL,app->screen);
      }
      app->screen=NULL;
      app->flags&=~APPF_OURSCREEN;
      if(app->appport)
      {  Setprocessfun(app->appport->mp_SigBit,NULL);
         Processappwindow();
         DeleteMsgPort(app->appport);
         app->appport=NULL;
      }
      if(app->screenname)
      {  FREE(app->screenname);
         app->screenname=NULL;
      }
      if(app->pubsignum>=0)
      {  FreeSignal(app->pubsignum);
         app->pubsignum=-1;
      }
   }
}

/* Open the screen */
static BOOL Appopenscreen(struct Application *app,BOOL loadreq)
{  short i;
   if(prefs.screentype==SCRTYPE_OWN)
   {  if((app->pubsignum=AllocSignal(-1))<0) return FALSE;
      if(app->screen=OpenScreenTags(NULL,
         SA_Width,prefs.screenwidth,
         SA_Height,prefs.screenheight,
         SA_Depth,prefs.screendepth,
         SA_Type,PUBLICSCREEN,
         SA_DisplayID,prefs.screenmode,
         SA_AutoScroll,TRUE,
         SA_Pens,prefs.scrdrawpens,
         SA_SysFont,TRUE,
         SA_Interleaved,TRUE,
         SA_Overscan,OSCAN_TEXT,
         SA_PubName,"AWeb",
         SA_PubSig,app->pubsignum,
         SA_SharePens,TRUE,
         SA_Title,AWEBSTR(MSG_AWEB_SCREENTITLE),
         TAG_END))
      {  app->flags|=APPF_OURSCREEN;
         app->screenname=Dupstr("AWeb",-1);
         for(i=0;i<8;i++)
         {  ObtainPen(app->screen->ViewPort.ColorMap,i,0,0,0,PEN_NO_SETCOLOR);
         }
         Loadpalette(app);
         if(prefs.loadpalette && prefs.screendepth<=8)
         {  Setcolors(app,prefs.screendepth);
         }
      }
      else  /* Own screen failed */
      {  FreeSignal(app->pubsignum);
         app->pubsignum=-1;
         prefs.screentype=SCRTYPE_DEFAULT;   /* Try on WB */
      }
   }
   else if(prefs.screentype==SCRTYPE_NAMED)
   {  if(app->screen=LockPubScreen(prefs.screenname))
      {  app->screenname=Dupstr(prefs.screenname,-1);
      }
      else
      {  prefs.screentype=SCRTYPE_DEFAULT;   /* Try on WB */
      }
   }
   else
   {  prefs.screentype=SCRTYPE_DEFAULT;
   }
   if(prefs.screentype==SCRTYPE_DEFAULT)
   {  struct List *scrlist;
      struct PubScreenNode *scrnode;
      if(!(app->screen=LockPubScreen(NULL))) return FALSE;
      scrlist=LockPubScreenList();
      for(scrnode=(struct PubScreenNode *)scrlist->lh_Head;
         scrnode->ln_Succ;
         scrnode=(struct PubScreenNode *)scrnode->ln_Succ)
      {  if(scrnode->psn_Screen==app->screen)
         {  app->screenname=Dupstr(scrnode->ln_Name,-1);
            break;
         }
      }
      UnlockPubScreenList();
      if(!app->screenname) app->screenname=Dupstr("Workbench",-1);
      if(app->appport=CreateMsgPort())
      {  Setprocessfun(app->appport->mp_SigBit,Processappwindow);
      }
   }
   if(!(app->drawinfo=GetScreenDrawInfo(app->screen))) return FALSE;
   Obtainapppens(app);
   if(app->flags&APPF_OURSCREEN)
   {  for(i=0;i<12;i++) prefs.scrdrawpens[i]=app->drawinfo->dri_Pens[i];
   }
   if(loadreq) Openloadreq(app->screen);
   Setloadreqstate(LRQ_IMAGES);
   if(!(app->vinfo=GetVisualInfo(app->screen,TAG_END))) return FALSE;
   app->deficon=BitMapObject,
      BITMAP_SourceFile,"AWeb:Images/def_image",
      BITMAP_Screen,app->screen,
   End;
   Setloadreqlevel(LQL_IMGICON,LQL_NUMBER);
   app->defmapicon=BitMapObject,
      BITMAP_SourceFile,"AWeb:Images/def_imagemap",
      BITMAP_Screen,app->screen,
   End;
   Setloadreqlevel(LQL_MAPICON,LQL_NUMBER);
   app->deferricon=BitMapObject,
      BITMAP_SourceFile,"AWeb:Images/def_errimage",
      BITMAP_Screen,app->screen,
   End;
   Setloadreqlevel(LQL_ERRICON,LQL_NUMBER);
   Getbuttonsinfo(app);
   Getaniminfo(app);
   if(app->flags&APPF_OURSCREEN)
   {  PubScreenStatus(app->screen,0);
   }
   if(app->jcontext)
   {  Jsetscreen(app->jcontext,app->screenname);
   }
   app->flags|=APPF_SCREENVALID;
   Broadcast(app,AOREL_APP_USE_SCREEN,
      AOAPP_Screenvalid,TRUE,
      TAG_END);
   return TRUE;
}

/* Remove the app icon */
static void Removeappicon(struct Application *app)
{  struct Message *msg;
   if(app->appport)
   {  if(app->appdob)
      {  if(app->appicon)
         {  Forbid();
            while(msg=GetMsg(app->appport)) ReplyMsg(msg);
            RemoveAppIcon(app->appicon);
            Permit();
            app->appicon=NULL;
         }
         FreeDiskObject(app->appdob);
         app->appdob=NULL;
      }
      Setprocessfun(app->appport->mp_SigBit,NULL);
      DeleteMsgPort(app->appport);
      app->appport=NULL;
   }
}

/* (Un)iconify */
static void Appiconify(struct Application *app,BOOL hide)
{  if(hide && !(app->flags&APPF_ICONIFIED))
   {  Appclosescreen(app);
      if(app->appport=CreateMsgPort())
      {  
#ifndef NEED35
         if(IconBase->lib_Version<44)
         {  app->appdob=GetDiskObjectNew(programname);
         }
         else
#endif
         {  app->appdob=GetIconTags(programname,
               ICONGETA_FailIfUnavailable,FALSE,
               TAG_END);
         }
         if(app->appdob)
         {  app->appdob->do_CurrentX=NO_ICON_POSITION;
            app->appdob->do_CurrentY=NO_ICON_POSITION;
            if(app->appicon=AddAppIcon(0,0,FilePart(programname),
               app->appport,NULL,app->appdob,TAG_END))
            {  app->flags|=APPF_ICONIFIED;
               Setprocessfun(app->appport->mp_SigBit,Processappicon);
            }
         }
      }
      if(!(app->flags&APPF_ICONIFIED))
      {  /* Uniconify again */
         hide=FALSE;
         app->flags|=APPF_ICONIFIED;
      }
   }
   if(!hide && (app->flags&APPF_ICONIFIED))
   {  Removeappicon(app);
      app->flags&=~APPF_ICONIFIED;
      Appopenscreen(app,FALSE);
   }
}

/* Process messages in windowport. Find the first message, and its windows
 * userdata, which is an object. Call the function for that object type.
 * Loop until port is empty, for each application... */
static void Processapp(void)
{  struct Application *app;
   struct IntuiMessage *imsg;
   struct Aobject *obj;
   for(app=apps.first;app->next;app=app->next)
   {  for(;;)
      {  Forbid();
         imsg=(struct IntuiMessage *)app->windowport->mp_MsgList.lh_Head;
         Permit();
         if(imsg && imsg->ln_Succ)
         {  obj=(struct Aobject *)imsg->IDCMPWindow->UserData;
            if(obj && obj->objecttype<128 && app->processfun[obj->objecttype])
            {  app->processfun[obj->objecttype]();
            }
            else
            {  Remove(imsg);
#ifdef DEVELOPER
               printf("Undeliverable message: %08x(%x)\n",obj,obj?obj->objecttype:0);
#endif
               ReplyMsg(imsg);
            }
         }
         else break;
      }
   }
}

/*------------------------------------------------------------------------*/

static long Setapplication(struct Application *app,struct Amset *ams)
{  ULONG newprefs=0;
   struct TagItem *tag,*tstate=ams->tags;
   short prctype;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOAPP_Tofront:
            if(app->screen)
            {  if(tag->ti_Data) ScreenToFront(app->screen);
               else ScreenToBack(app->screen);
            }
            break;
         case AOAPP_Newprefs:
            newprefs|=tag->ti_Data;
            break;
         case AOAPP_Savepath:
            Setsavepath(app,(UBYTE *)tag->ti_Data);
            break;
         case AOAPP_Menus:
            Newappmenus(app);
            break;
         case AOAPP_Browsersettings:
            Broadcast(app,AOREL_APP_USE_BROWSER,AOAPP_Browsersettings,TRUE,TAG_END);
            Doupdateframes();
            break;
         case AOAPP_Browserpens:
            Newapppens(app);
            break;
         case AOAPP_Overlapsetting:
            Broadcast(app,AOREL_APP_USE_OVERLAP,AOAPP_Overlapsetting,TRUE,TAG_END);
            break;
         case AOAPP_Blink:
            Asetattrs(app->blinktimer,
               AOTIM_Waitseconds,prefs.blinkrate/10,
               AOTIM_Waitmicros,(prefs.blinkrate%10)*100000+1,
               TAG_END);
            break;
         case AOAPP_Processtype:
            prctype=tag->ti_Data;
            break;
         case AOAPP_Processfun:
            if(prctype>0 && prctype<128)
            {  app->processfun[prctype]=(void (*)(void))tag->ti_Data;
               prctype=0;
            }
            break;
         case AOAPP_Animgadsetting:
            if((app->flags&APPF_ANIMON) && prefs.contanim)
            {  Asetattrs(app->animtimer,
                  AOTIM_Waitseconds,0,
                  AOTIM_Waitmicros,10000,
                  TAG_END);
            }
            break;
         case AOAPP_Iconify:
            Appiconify(app,tag->ti_Data);
            break;
      }
   }
   if(app->flags&APPF_SCREENVALID)
   {  if(newprefs&PREFSF_SCREEN)
      {  if(app->screen) Appclosescreen(app);
         if(!Appopenscreen(app,FALSE)) Quit(TRUE);
      }
      else if(newprefs&PREFSF_BUTTONS)
      {  Rebuildallbuttons();
      }
      else if(newprefs&PREFSF_SHOWBUTTONS)
      {  Reopenallwindows();
      }
   }
   return 0;
}

static struct Application *Newapplication(struct Amset *ams)
{  struct Application *app;
   struct TextAttr sysattr={0};
   if(app=Allocobject(AOTP_APPLICATION,sizeof(struct Application),ams))
   {  ADDTAIL(&apps,app);
      NEWLIST(&app->usescreen);
      NEWLIST(&app->usemenus);
      NEWLIST(&app->usebrowser);
      NEWLIST(&app->useoverlap);
      NEWLIST(&app->wantblink);
      NEWLIST(&app->wantmarquee);
      InitSemaphore(&app->semaphore);
      app->pubsignum=-1;
      app->bgpen=-1;
      app->textpen=-1;
      app->linkpen=-1;
      app->vlinkpen=-1;
      app->alinkpen=-1;
      app->blackpen=-1;
      app->whitepen=-1;
      app->tooltippen=-1;
      sysattr.ta_Name=((struct GfxBase *)GfxBase)->DefaultFont->ln_Name;
      sysattr.ta_YSize=((struct GfxBase *)GfxBase)->DefaultFont->tf_YSize;
      app->systemfont=OpenFont(&sysattr);
      app->windowport=CreateMsgPort();
      Setprocessfun(app->windowport->mp_SigBit,Processapp);
      app->savepath=Dupstr(prefs.savepath,-1);
      Setapplication(app,ams);
      Makemenus(app);
      Appopenscreen(app,!nostartup);
      app->blinktimer=Anewobject(AOTP_TIMER,
         AOBJ_Target,app,
         AOBJ_Map,blinktimermap,
         AOTIM_Waitseconds,prefs.blinkrate/10,
         AOTIM_Waitmicros,(prefs.blinkrate%10)*100000,
         TAG_END);
      app->animtimer=Anewobject(AOTP_TIMER,
         AOBJ_Target,app,
         AOBJ_Map,animtimermap,
         TAG_END);
      app->resizehorobj=NewObject(NULL,"pointerclass",
         POINTERA_BitMap,&resizehorbmap,
         POINTERA_WordWidth,1,
         POINTERA_XOffset,-resizehorhot[0],
         POINTERA_YOffset,-resizehorhot[1],
//         POINTERA_XResolution,POINTERXRESN_SCREENRES,
//         POINTERA_YResolution,POINTERYRESN_SCREENRESASPECT,
         TAG_END);
      app->resizevertobj=NewObject(NULL,"pointerclass",
         POINTERA_BitMap,&resizevertbmap,
         POINTERA_WordWidth,1,
         POINTERA_XOffset,-resizeverthot[0],
         POINTERA_YOffset,-resizeverthot[1],
//         POINTERA_XResolution,POINTERXRESN_SCREENRES,
//         POINTERA_YResolution,POINTERYRESN_SCREENRESASPECT,
         TAG_END);
      app->handobj=NewObject(NULL,"pointerclass",
         POINTERA_BitMap,&handbmap,
         POINTERA_WordWidth,1,
         POINTERA_XOffset,-handhot[0],
         POINTERA_YOffset,-handhot[1],
         TAG_END);
   }
   return app;
}

static long Getapplication(struct Application *app,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOAPP_Programname:
            PUTATTR(tag,programname);
            break;
         case AOAPP_Screen:
            PUTATTR(tag,app->screen);
            break;
         case AOAPP_Screenname:
            PUTATTR(tag,app->screenname);
            break;
         case AOAPP_Colormap:
            PUTATTR(tag,app->screen?app->screen->ViewPort.ColorMap:NULL);
            break;
         case AOAPP_Drawinfo:
            PUTATTR(tag,app->drawinfo);
            break;
         case AOAPP_Screenfont:
            PUTATTR(tag,app->drawinfo?app->drawinfo->dri_Font:NULL);
            break;
         case AOAPP_Systemfont:
            PUTATTR(tag,app->systemfont);
            break;
         case AOAPP_Screenwidth:
            PUTATTR(tag,app->screen?app->screen->Width:0);
            break;
         case AOAPP_Screenheight:
            PUTATTR(tag,app->screen?app->screen->Height:0);
            break;
         case AOAPP_Screendepth:
            PUTATTR(tag,app->screen?
               GetBitMapAttr(app->screen->RastPort.BitMap,BMA_DEPTH):0);
            break;
         case AOAPP_Configname:
            PUTATTR(tag,configname);
            break;
         case AOAPP_Screenvalid:
            PUTATTR(tag,BOOLVAL(app->flags&APPF_SCREENVALID));
            break;
         case AOAPP_Menus:
            PUTATTR(tag,(app->flags&APPF_MENUSVALID?app->menus:NULL));
            break;
         case AOAPP_Visualinfo:
            PUTATTR(tag,app->vinfo);
            break;
         case AOAPP_Browsebgpen:
            PUTATTR(tag,app->bgpen);
            break;
         case AOAPP_Textpen:
            PUTATTR(tag,app->textpen);
            break;
         case AOAPP_Linkpen:
            PUTATTR(tag,app->linkpen);
            break;
         case AOAPP_Vlinkpen:
            PUTATTR(tag,app->vlinkpen);
            break;
         case AOAPP_Alinkpen:
            PUTATTR(tag,app->alinkpen);
            break;
         case AOAPP_Windowport:
            PUTATTR(tag,app->windowport);
            break;
         case AOAPP_Savepath:
            PUTATTR(tag,app->savepath);
            break;
         case AOAPP_Deficon:
            PUTATTR(tag,app->deficon);
            break;
         case AOAPP_Defmapicon:
            PUTATTR(tag,app->defmapicon?app->defmapicon:app->deficon);
            break;
         case AOAPP_Deferricon:
            PUTATTR(tag,app->deferricon?app->deferricon:app->deficon);
            break;
         case AOAPP_Blackpen:
            PUTATTR(tag,app->blackpen);
            break;
         case AOAPP_Whitepen:
            PUTATTR(tag,app->whitepen);
            break;
         case AOAPP_Tooltippen:
            PUTATTR(tag,app->tooltippen);
            break;
         case AOAPP_Semaphore:
            PUTATTR(tag,&app->semaphore);
            break;
         case AOAPP_Pwfont:
            PUTATTR(tag,&pwfont);
            break;
         case AOAPP_Jcontext:
            if(prefs.dojs && Openjslib())
            {  if(!app->jcontext)
               {  app->jcontext=Newjcontext(app->screenname);
               }
               PUTATTR(tag,app->jcontext);
            }
            else
            {  PUTATTR(tag,NULL);
            }
            break;
         case AOAPP_Jnavigator:
            PUTATTR(tag,app->jnavigator);
            break;
         case AOAPP_Appwindowport:
            PUTATTR(tag,(app->flags&APPF_ICONIFIED)?NULL:app->appport);
            break;
         case AOAPP_Iconified:
            PUTATTR(tag,BOOLVAL(app->flags&APPF_ICONIFIED));
            break;
         case AOAPP_Jscreen:
            PUTATTR(tag,app->jscreen);
            break;
      }
   }
   return 0;
}

static long Updateapplication(struct Application *app,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOAPP_Blinktimer:
            if(prefs.blinkrate)
            {  app->flags^=APPF_BLINKON;
               Asetattrs(app->blinktimer,
                  AOTIM_Waitseconds,prefs.blinkrate/10,
                  AOTIM_Waitmicros,(prefs.blinkrate%10)*100000,
                  TAG_END);
            }
            else
            {  app->flags|=APPF_BLINKON;
            }
            Broadcast(app,AOREL_APP_WANT_BLINK,
               AOAPP_Blink,BOOLVAL(app->flags&APPF_BLINKON),
               TAG_END);
            break;
         case AOAPP_Marqueetimer:
            /* Update all marquee elements (timer is only created when needed) */
            if(app->marqueetimer && !ISEMPTY(&app->wantmarquee))
            {  Broadcast(app,AOREL_APP_WANT_MARQUEE,
                  AOAPP_Marquee,TRUE,
                  TAG_END);
               /* Re-arm one-shot timer */
               Asetattrs(app->marqueetimer,
                  AOTIM_Waitseconds,0,
                  AOTIM_Waitmicros,50000,
                  TAG_END);
            }
            break;
         case AOAPP_Animtimer:
            if(prefs.contanim && (app->flags&APPF_ANIMON))
            {  Asetattrs(app->animtimer,
                  AOTIM_Waitseconds,0,
                  AOTIM_Waitmicros,100000,
                  TAG_END);
               Setanimgadsactive(TRUE);
            }
            break;
      }
   }
   return 0;
}

static long Addchildapplication(struct Application *app,struct Amadd *ama)
{  struct Child *ch;
   LIST(Child) *list=Childlist(app,ama->relation);
   if(list)
   {  if(ch=ALLOCSTRUCT(Child,1,MEMF_CLEAR))
      {  ch->object=ama->child;
         ADDTAIL(list,ch);
         if(ama->relation==AOREL_APP_WANT_MARQUEE)
         {  if(!app->marqueetimer)
            {  app->marqueetimer=Anewobject(AOTP_TIMER,
                  AOBJ_Target,app,
                  AOBJ_Map,marqueetimermap,
                  AOTIM_Waitseconds,0,
                  AOTIM_Waitmicros,50000,
                  TAG_END);
            }
         }
      }
   }
   return 0;
}

static long Remchildapplication(struct Application *app,struct Amadd *ama)
{  struct Child *ch;
   LIST(Child) *list=Childlist(app,ama->relation);
   if(list)
   {  for(ch=list->first;ch->next;ch=ch->next)
      {  if(ch->object==ama->child)
         {  REMOVE(ch);
            FREE(ch);
            break;
         }
      }
      if(ama->relation==AOREL_APP_WANT_MARQUEE)
      {  if(app->marqueetimer && ISEMPTY(&app->wantmarquee))
         {  Adisposeobject(app->marqueetimer);
            app->marqueetimer=NULL;
         }
      }
   }
   return 0;
}

static long Jsetupapplication(struct Application *app,struct Amjsetup *js)
{  struct Jvar *jv;
   UBYTE buf[256],*p;
   if(prefs.dojs && Openjslib())
   {  if(!app->jcontext)
      {  app->jcontext=Newjcontext(app->screenname);
      }
      if(app->jcontext)
      {  Jallowgc(app->jcontext,FALSE);
         if(!app->jnavigator)
         {  app->jnavigator=Newjobject(app->jcontext);
            Jkeepobject(app->jnavigator,TRUE);
         }
         if(app->jnavigator)
         {  if(jv=Jproperty(app->jcontext,app->jnavigator,"appCodeName"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
#ifndef DEMOVERSION
               if(*prefs.spoofid)
               {  strcpy(buf,prefs.spoofid);
                  if(p=strchr(buf,'/')) *p='\0';
               }
               else
#endif
               {  strcpy(buf,"AWeb");
               }
               Jasgstring(app->jcontext,jv,buf);
            }
            if(jv=Jproperty(app->jcontext,app->jnavigator,"appName"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               p="AWeb";
#ifndef DEMOVERSION
               if(*prefs.spoofid)
               {  if(STRNIEQUAL(prefs.spoofid,"Mozilla/",8)) p="Netscape";
                  else if(STRNIEQUAL(prefs.spoofid,"MSIE/",5)) p="Microsoft Internet Explorer";
               }
#endif
               Jasgstring(app->jcontext,jv,p);
            }
            if(jv=Jproperty(app->jcontext,app->jnavigator,"appVersion"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
#ifndef DEMOVERSION
               if(p=strchr(prefs.spoofid,'/'))
               {  strcpy(buf,p+1);
               }
               else
#endif
               {  p=FULLRELEASE
                     #ifdef BETAVERSION
                        " [" BETARELEASE "beta]"
                     #endif
                  ;
                  strcpy(buf,p);
               }
               strcat(buf," (Amiga; I)");
               Jasgstring(app->jcontext,jv,buf);
            }
            if(jv=Jproperty(app->jcontext,app->jnavigator,"userAgent"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
#ifndef DEMOVERSION
               if(*prefs.spoofid)
               {  sprintf(buf,"%s; (Spoofed by Amiga-AWeb/%s)",prefs.spoofid,awebversion);
               }
               else
#endif
               {  strcpy(buf,"Mozilla/5.0 (AmigaOS; U; AmigaOS 3.2; en-US; rv:1.9.2.28) Gecko/20120306 Firefox/3.6.28");
               }
               Jasgstring(app->jcontext,jv,buf);
            }
            Addjfunction(app->jcontext,app->jnavigator,"javaEnabled",Methodjavaenabled,NULL);
            Addjfunction(app->jcontext,app->jnavigator,"taintEnabled",Methodtaintenabled,NULL);
            Jsetupprefs(app->jcontext,app->jnavigator);
         }
         if(!app->jscreen)
         {  app->jscreen=Newjobject(app->jcontext);
            Jkeepobject(app->jscreen,TRUE);
         }
         if(app->jscreen)
         {  if(jv=Jproperty(app->jcontext,app->jscreen,"width"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgnumber(app->jcontext,jv,app->screen?app->screen->Width:0);
            }
            if(jv=Jproperty(app->jcontext,app->jscreen,"height"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgnumber(app->jcontext,jv,app->screen?app->screen->Height:0);
            }
         }
         Jsetupallwindows(app->jcontext);
         Jallowgc(app->jcontext,TRUE);
      }
   }
   return 0;
}

static void Disposeapplication(struct Application *app)
{  struct Child *ch;
   struct Message *msg;
   REMOVE(app);
   if(app->jnavigator) Disposejobject(app->jnavigator);
   if(app->jscreen) Disposejobject(app->jscreen);
   /* Can't free the jcontext here because other objects might want to
    * dispose their js objects after us. */
   if(app->jcontext) jctofree=app->jcontext;
   if(app->screen) Appclosescreen(app);
   if(app->flags&APPF_ICONIFIED) Removeappicon(app);
   while(ch=REMHEAD(&app->usescreen))
   {  Asetattrs(ch->object,AOBJ_Application,NULL,TAG_END);
      FREE(ch);
   }
   while(ch=REMHEAD(&app->usemenus))
   {  Asetattrs(ch->object,AOBJ_Application,NULL,TAG_END);
      FREE(ch);
   }
   while(ch=REMHEAD(&app->usebrowser))
   {  Asetattrs(ch->object,AOBJ_Application,NULL,TAG_END);
      FREE(ch);
   }
   while(ch=REMHEAD(&app->useoverlap))
   {  Asetattrs(ch->object,AOBJ_Application,NULL,TAG_END);
      FREE(ch);
   }
   while(ch=REMHEAD(&app->wantblink))
   {  Asetattrs(ch->object,AOBJ_Application,NULL,TAG_END);
      FREE(ch);
   }
   while(ch=REMHEAD(&app->wantmarquee))
   {  Asetattrs(ch->object,AOBJ_Application,NULL,TAG_END);
      FREE(ch);
   }
   if(app->animtimer) Adisposeobject(app->animtimer);
   if(app->blinktimer) Adisposeobject(app->blinktimer);
   if(app->marqueetimer) Adisposeobject(app->marqueetimer);
   if(app->systemfont) CloseFont(app->systemfont);
   if(app->menus) Freemenus(app);
   if(app->windowport)
   {  Setprocessfun(app->windowport->mp_SigBit,NULL);
      DeleteMsgPort(app->windowport);
   }
   if(app->resizehorobj) DisposeObject(app->resizehorobj);
   if(app->resizevertobj) DisposeObject(app->resizevertobj);
   if(app->handobj) DisposeObject(app->handobj);
   if(app->savepath) FREE(app->savepath);
   Amethodas(AOTP_OBJECT,app,AOM_DISPOSE);
}

static void Deinstallapplication(void)
{  void *p;
   while(p=REMHEAD(&apps)) Adisposeobject(p);
   if(pwfont.tf_Extension)
   {  FreeMem(pwfont.tf_Extension,sizeof(struct TextFontExtension));
   }
   while(p=REMHEAD(&animgads)) FREE(p);
}

static long Dispatch(struct Application *app,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newapplication((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setapplication(app,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getapplication(app,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updateapplication(app,(struct Amset *)amsg);
         break;
      case AOM_ADDCHILD:
         result=Addchildapplication(app,(struct Amadd *)amsg);
         break;
      case AOM_REMCHILD:
         result=Remchildapplication(app,(struct Amadd *)amsg);
         break;
      case AOM_JSETUP:
         result=Jsetupapplication(app,(struct Amjsetup *)amsg);
         break;
      case AOM_DISPOSE:
         Disposeapplication(app);
         break;
      case AOM_DEINSTALL:
         Deinstallapplication();
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installapplication(void)
{  short i;
   NEWLIST(&apps);
   NEWLIST(&animgads);
   closereq.es_Title=AWEBSTR(MSG_REQUEST_TITLE);
   closereq.es_TextFormat=haiku?HAIKU24:AWEBSTR(MSG_SCRCLOSE_TEXT);
   closereq.es_GadgetFormat=AWEBSTR(MSG_SCRCLOSE_OK);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_APPLICATION,Dispatch)) return FALSE;
   pwfont.tf_YSize=8;
   pwfont.tf_XSize=8;
   pwfont.tf_Baseline=7;
   pwfont.tf_Accessors=1;
   pwfont.tf_LoChar=32;
   pwfont.tf_HiChar=255;
   pwfont.tf_CharData=fontmap;
   pwfont.tf_Modulo=2;
   pwfont.tf_CharLoc=charloc;
   pwfont.tf_CharSpace=charspace;
   pwfont.tf_CharKern=charkern;
   for(i=0;i<224;i++)
   {  charloc[i]=8;
      charspace[i]=8;
      charkern[i]=0;
   }
   Readpointerdata(APTR_HAND);
   Readpointerdata(APTR_RESIZEHOR);
   Readpointerdata(APTR_RESIZEVERT);
   return TRUE;
}

void Freeapplication(void)
{  if(jctofree && Openjslib())
   {  Freejcontext(jctofree);
      jctofree=NULL;
   }
}

struct Image *Buttonimage(void *ap,short type,UWORD *data,long width,long height)
{  struct Application *app=ap;
   struct Image *img=NULL;
   BOOL select=(app->buttonoffset[type][2]>=0 && app->buttonoffset[type][3]>=0);
   short w,h;
   if(type>=BUTF_NSCANCEL)
   {  w=app->nsbuttonwidth;h=app->nsbuttonheight;
   }
   else if(type>=BUTF_UNSECURE)
   {  w=app->secbuttonwidth;h=app->secbuttonheight;
   }
   else
   {  w=app->buttonwidth;h=app->buttonheight;
   }
   if(app->buttonsbitmap && app->buttonoffset[type][0]>=0 && app->buttonoffset[type][1]>=0)
   {  img=BitMapObject,
         BITMAP_BitMap,app->buttonsbitmap,
         BITMAP_MaskPlane,app->buttonstranspmask,
         BITMAP_Width,w,
         BITMAP_Height,h,
         BITMAP_OffsetX,app->buttonoffset[type][0],
         BITMAP_OffsetY,app->buttonoffset[type][1],
         (select?BITMAP_SelectBitMap:TAG_IGNORE),app->buttonsbitmap,
         BITMAP_SelectMaskPlane,app->buttonstranspmask,
         BITMAP_SelectWidth,w,
         BITMAP_SelectHeight,h,
         BITMAP_SelectOffsetX,app->buttonoffset[type][2],
         BITMAP_SelectOffsetY,app->buttonoffset[type][3],
         BITMAP_Masking,app->buttonstransparent,
      End;
   }
   if(!img && data)
   {  img=NewObject(Gadimgclass(),NULL,
         SYSIA_DrawInfo,app->drawinfo,
         IA_Width,width,
         IA_Height,height,
         IA_Data,data,
         TAG_END);
      if(width>app->imagewidth) app->imagewidth=width;
      if(height>app->imageheight) app->imageheight=height;
   }
   return img;
}

struct Gadget *Animgadget(void *ap,void *capens)
{  struct Application *app=ap;
   struct Gadget *gad=NewObject(Ledgadclass(),NULL,
      GA_Width,/*app->animbitmap?(app->animw+2):*/24,
      GA_Height,/*app->animbitmap?(app->animh+2):*/24,
      LEDGGA_AnimBitMap,app->animbitmap,
      LEDGGA_AnimX,app->animx,
      LEDGGA_AnimY,app->animy,
      LEDGGA_AnimWidth,app->animw,
      LEDGGA_AnimHeight,app->animh,
      LEDGGA_AnimDeltaX,app->animdx,
      LEDGGA_AnimDeltaY,app->animdy,
      LEDGGA_AnimFrames,app->animframes,
      LEDGGA_RestX,app->animrx,
      LEDGGA_RestY,app->animry,
      LEDGGA_SpecialPens,capens,
      TAG_END);
   return gad;
}

struct IntuiMessage *Getimessage(struct MsgPort *port,short type)
{  struct IntuiMessage *imsg;
   struct Aobject *obj;
   Forbid();
   for(imsg=(struct IntuiMessage *)port->mp_MsgList.lh_Head;imsg->ln_Succ;
      imsg=(struct IntuiMessage *)imsg->ln_Succ)
   {  if(imsg->IDCMPWindow)
      {  obj=(struct Aobject *)imsg->IDCMPWindow->UserData;
         if(obj && obj->objecttype==type) break;
      }
   }
   Permit();
   if(imsg->ln_Succ) Remove(imsg);
   else imsg=NULL;
   return imsg;
}

void *Apppointer(struct Application *app,USHORT ptrtype)
{  void *ptr=NULL;
   if(app)
   {  switch(ptrtype)
      {  case APTR_RESIZEHOR:
            ptr=app->resizehorobj;
            break;
         case APTR_RESIZEVERT:
            ptr=app->resizevertobj;
            break;
         case APTR_HAND:
            ptr=app->handobj;
            break;
      }
   }
   return ptr;
}

void Addanimgad(struct Window *win,struct Gadget *gad)
{  struct Animgad *ag;
   if(ag=ALLOCSTRUCT(Animgad,1,MEMF_PUBLIC))
   {  ag->win=win;
      ag->gad=gad;
      ADDTAIL(&animgads,ag);
   }
}

void Remanimgad(struct Gadget *gad)
{  struct Animgad *ag;
   for(ag=animgads.first;ag->next;ag=ag->next)
   {  if(ag->gad==gad)
      {  REMOVE(ag);
         FREE(ag);
         break;
      }
   }
}

BOOL Setanimgads(BOOL onoff)
{  BOOL oldonoff=FALSE;
   struct Application *app=(struct Application *)Aweb();
   if(app)
   {  oldonoff=BOOLVAL(app->flags&APPF_ANIMON);
      SETFLAG(app->flags,APPF_ANIMON,onoff);
      if(oldonoff && !onoff)
      {  /* Set gadget off */
         Setanimgadsactive(FALSE);
      }
      else if(onoff)
      {  if(!oldonoff && prefs.contanim)
         {  /* Start continuous animation */
            Asetattrs(app->animtimer,
               AOTIM_Waitseconds,0,
               AOTIM_Waitmicros,10000,
               TAG_END);
            Setanimgadsactive(TRUE);
         }
         if(!prefs.contanim)
         {  /* Step animation ahead */
            Setanimgadsactive(TRUE);
         }
      }
   }
   return oldonoff;
}

USHORT Menunumfromcmd(UBYTE *cmd)
{  struct Menuentry *me;
   for(me=prefs.menus.first;me->next;me=me->next)
   {  if(STRIEQUAL(me->cmd,cmd)) return me->menunum;
   }
   return (USHORT)MENUNULL;
}

struct Menuentry *Menuentryfromkey(UBYTE key)
{  struct Menuentry *me;
   key=toupper(key);
   for(me=prefs.menus.first;me->next;me=me->next)
   {  switch(me->type)
      {  case AMENU_ITEM:
         case AMENU_SUB:
            if(me->scut[0]==key) return me;
            break;
      }
   }
   return NULL;
}

struct Menuentry *Menuentryfromnum(USHORT menunum)
{  struct Menuentry *me;
   for(me=prefs.menus.first;me->next;me=me->next)
   {  if(me->menunum==menunum) return me;
   }
   return NULL;
}