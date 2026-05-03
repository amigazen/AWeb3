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

/* awebview.c AWebView main - local file viewer */
/* Forked from aweb.c */

#include "aweb.h"
#include "url.h"
#include "application.h"
#include "window.h"
#include "source.h"
#include "frame.h"
#include "jslib.h"
#include "startup.h"
#include "task.h"
#include "locale.h"
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/gadgetclass.h>
#include <dos/dostags.h>
#include <dos/rdargs.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <utility/date.h>
#include <libraries/locale.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/icon.h>
#include <proto/locale.h>
#include <proto/layers.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/wb.h>
#include <proto/iffparse.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/datatypes.h>
#include <proto/keymap.h>
#include <proto/diskfont.h>
#include <proto/button.h>
#include <proto/bitmap.h>
#include <proto/bevel.h>
#include <proto/glyph.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/string.h>
#include <proto/space.h>
#include <proto/label.h>
#include <proto/drawlist.h>
#include <proto/checkbox.h>
#include <proto/integer.h>
#include <proto/chooser.h>
#include <proto/listbrowser.h>
#include <proto/speedbar.h>
#include <proto/scroller.h>
#include <proto/fuelgauge.h>
#include <proto/penmap.h>
#include <proto/colorwheel.h>
#include <proto/timer.h>

__near long __stack=16384;
const char *stack_cookie = "$STACK: 16384";

#ifdef DEVELOPER
#define PROFILE
extern void Setoodebug(UBYTE *types);
extern void Setoomethod(UBYTE *types);
extern void Setoodelay(void);
extern BOOL ookdebug;
#endif
#include "profile.h"

#ifdef BETAKEYFILE
/* extern struct Library *AWebJSBase; */
#endif

struct Library *AWebJSBase;

static void *StartupBase;

struct LocaleInfo localeinfo;
struct Locale *locale;
void *maincatalog;

static BOOL quit=FALSE;
static BOOL icondefer=FALSE,iconify;
static BOOL openedca=FALSE;
static BOOL updateframes=FALSE;
static BOOL flushsources=FALSE;
static BOOL awebpath=FALSE;

BOOL httpdebug=FALSE;
BOOL specdebug=FALSE;
BOOL usetemp=FALSE;
long localblocksize=INPUTBLOCKSIZE;
BOOL profile=FALSE;
BOOL nopool=FALSE;
BOOL has35=FALSE;
BOOL haiku=FALSE;
#ifdef LOCALONLY
BOOL nostartup=TRUE;  /* Always skip startup screen in LOCALONLY builds */
#else
BOOL nostartup=FALSE;
#endif

UBYTE *initialurls[16];
UBYTE localinitialurl[16];

#define AWEBCONTROLPORTNAME   "AWebViewControlPort"

static struct MsgPort *awebcontrolport;

struct Awebcontrolmsg
{  struct Message msg;
   UBYTE **urls;
   UBYTE *local;
};

UBYTE programname[256];

struct PathList
{  BPTR next;
   BPTR lock;
};

struct PathList *ourpathlist;

struct Layermessage
{  struct Layer *layer;
   struct Rectangle rect;
   long xoffset;
   long yoffset;
};

struct Clipinfo
{  struct Layer *layer;
   struct Region *region;
   struct Region *oldregion;
   struct Window *window;     /* only if refreshing */
};

struct Clipcoords
{  struct Coords coords;
   ULONG clipkey;
};

#define CLIPSTACKMAX 48
static ULONG clip_stack[CLIPSTACKMAX];
static short clip_stack_n;

static UBYTE days[7][4]=
{  "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
};
static UBYTE months[12][4]=
{  "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

static struct Aobject *aweb;

static ULONG waitmask=0;
static void (*processfun[16])(void);

/* General purpose notify-when-back-in-main queue */
struct Smqnode
{  NODE(Smqnode);
   void *object;
   ULONG queueid;
   ULONG userdata;
   BOOL selected;
};
static LIST(Smqnode) setmsgqueue;

/* Generic JS event handler invokations */
UBYTE *awebonclick="if(this.onclick) return this.onclick();";
UBYTE *awebonchange="if(this.onchange) return this.onchange();";
UBYTE *awebonblur="if(this.onblur) return this.onblur();";
UBYTE *awebonfocus="if(this.onfocus) return this.onfocus();";
UBYTE *awebonmouseover="if(this.onmouseover) return this.onmouseover();";
UBYTE *awebonmouseout="if(this.onmouseout) return this.onmouseout();";
UBYTE *awebonreset="if(this.onreset) return this.onreset();";
UBYTE *awebonsubmit="if(this.onsubmit) return this.onsubmit();";
UBYTE *awebonselect="if(this.onselect) return this.onselect();";
UBYTE *awebonload="if(this.onload) return this.onload();";
UBYTE *awebonunload="if(this.onunload) return this.onunload();";
UBYTE *awebonerror="if(this.onerror) return this.onerror();";
UBYTE *awebonabort="if(this.onabort) return this.onabort();";


/*-----------------------------------------------------------------------*/

/* 0x01 = printable
   0x02 = alphabetic
   0x04 = numeric
   0x08 = 
   0x10 = url (7-bit-alphanumeric . - _ @)
   0x20 = space (space,cr,nl,ff,tab,nbsp)
   0x40 = sgml (low-ascii non separator)
*/

static UBYTE istab[]=
{  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x20,0x00,0x20,0x20,0x00,0x00,
   0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   0x21,0x41,0x01,0x41,0x41,0x41,0x01,0x01,0x41,0x41,0x51,0x41,0x01,0x51,0x51,0x41,
   0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x41,0x01,0x01,0x01,0x01,0x41,
   0x51,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,
   0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x41,0x41,0x41,0x41,0x51,
   0x01,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,
   0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x53,0x41,0x41,0x41,0x41,0x00,
   0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,
   0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x01,
   0x21,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
   0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
   0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,
   0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x01,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,
   0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,
   0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x01,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,
};

BOOL Isurlchar(UBYTE c)
{  if(istab[c]&0x10) return TRUE;
   else return FALSE;
}

BOOL Isprint(UBYTE c)
{  if(istab[c]&0x01) return TRUE;
   else return FALSE;
}

BOOL Isspace(UBYTE c)
{  return BOOLVAL(istab[c]&0x20);
}

BOOL Issgmlchar(UBYTE c)
{  return BOOLVAL(istab[c]&0x40);
}

BOOL Isalpha(UBYTE c)
{  return BOOLVAL(istab[c]&0x02);
}

BOOL Isalnum(UBYTE c)
{  return BOOLVAL(istab[c]&0x06);
}

/*-----------------------------------------------------------------------*/

BOOL Expandbuffer(struct Buffer *buf,long size)
{  if(buf->length+size>buf->size)
   {  long newsize=
         ((buf->length+size+TEXTBLOCKSIZE-1)/TEXTBLOCKSIZE)*TEXTBLOCKSIZE;
      UBYTE *newbuf=ALLOCTYPE(UBYTE,newsize,0);
      if(!newbuf) return FALSE;
      if(buf->size)
      {  memmove(newbuf,buf->buffer,buf->size);
         FREE(buf->buffer);
      }
      buf->buffer=newbuf;
      buf->size=newsize;
   }
   return TRUE;
}

void Freebuffer(struct Buffer *buf)
{  if(buf->buffer) FREE(buf->buffer);
   buf->buffer=NULL;
   buf->size=buf->length=0;
}

BOOL Insertinbuffer(struct Buffer *buf,UBYTE *text,long length,long pos)
{  if(Expandbuffer(buf,length))
   {  if(pos<buf->length)
         memmove(buf->buffer+pos+length,buf->buffer+pos,buf->length-pos);
      if(length) memmove(buf->buffer+pos,text,length);
      buf->length+=length;
      return TRUE;
   }
   return FALSE;
}

BOOL Addtobuffer(struct Buffer *buf,UBYTE *text,long length)
{  if(length<0) length=strlen(text);
   return Insertinbuffer(buf,text,length,buf->length);
}

void Deleteinbuffer(struct Buffer *buf,long pos,long length)
{  if(pos+length>buf->length) length=buf->length-pos;
   if(pos+length<buf->length)
   {  memmove(buf->buffer+pos,buf->buffer+pos+length,
         buf->length-pos-length);
   }
   buf->length-=length;
}

void Setgadgetattrs(struct Gadget *gad,struct Window *win,struct Requester *req,...)
{  struct TagItem *tags=(struct TagItem *)((ULONG *)&req+1);
   if(SetGadgetAttrsA(gad,win,req,tags)) RefreshGList(gad,win,req,1);
}

long Getvalue(void *gad,ULONG tag)
{  long value=0;
   GetAttr(tag,gad,(ULONG *)&value);
   return value;
}

BOOL Getselected(struct Gadget *gad)
{  return (BOOL)Getvalue(gad,GA_Selected);
}

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

void AddtagstrA(struct Buffer *buf,UBYTE *keywd,USHORT f,ULONG value)
{  UBYTE *p;
   UBYTE b[16];
   if(keywd)
   {  if(buf->buffer && buf->buffer[buf->length-1]!='>' && *keywd!='<' && *keywd!='>')
      {  Addtobuffer(buf," ",1);
      }
      Addtobuffer(buf,keywd,strlen(keywd));
   }
   switch(f)
   {  case ATSF_STRING:
         Addtobuffer(buf,"=\"",2);
         for(p=(UBYTE *)value;*p;p++)
         {  if(*p=='"') Addtobuffer(buf,"&quot;",6);
            else Addtobuffer(buf,p,1);
         }
         Addtobuffer(buf,"\"",1);
         break;
      case ATSF_NUMBER:
         sprintf(b,"=\"%d\"",value);
         Addtobuffer(buf,b,strlen(b));
         break;
   }
}

long Copypathlist(void)
{  struct PathList *pl,*plfirst=NULL,*pllast=NULL,*plnew;
   for(pl=ourpathlist;pl;pl=(struct PathList *)BADDR(pl->next))
   {  if(plnew=(struct PathList *)AllocVec(sizeof(struct PathList),MEMF_PUBLIC))
      {  if(plnew->lock=DupLock(pl->lock))
         {  plnew->next=NULL;
            if(pllast) pllast->next=MKBADDR(plnew);
            else plfirst=plnew;
            pllast=plnew;
         }
         else FreeVec(plnew);
      }
   }
   return MKBADDR(plfirst);
}

void Freepathlist(long plbptr)
{  struct PathList *pl,*plnext;
   for(pl=BADDR(plbptr);pl;pl=plnext)
   {  plnext=BADDR(pl->next);
      if(pl->lock) UnLock(pl->lock);
      FreeVec(pl);
   }
}

long Pformatlength(UBYTE *format,UBYTE *argspec,UBYTE **params)
{  long len=0;
   UBYTE *s=format;  /* source */
   UBYTE *a,*p;
   BOOL quoted=FALSE;
   while(*s)
   {  if(*s=='%' && s[1])
      {  if(a=strchr(argspec,s[1]))
         {  if(!quoted) len++;
            for(p=params[a-argspec];p && *p;p++)
            {  if(*p=='"' || *p=='*') len++;
               len++;
            }
            if(!quoted) len++;
            s++; /* skip %, param specifier is skipped at end of loop */
         }
         else len++;
      }
      else
      {  if(*s=='"') quoted=!quoted;
         len++;
      }
      s++;
   }
   return len;
}

UBYTE *Pformat(UBYTE *buffer,UBYTE *format,UBYTE *argspec,UBYTE **params,BOOL quote)
{  UBYTE *d=buffer;  /* destination */
   UBYTE *s=format;  /* source */
   UBYTE *a,*p;
   BOOL quoted=FALSE;
   while(*s)
   {  if(*s=='%' && s[1])
      {  if(a=strchr(argspec,s[1]))
         {  if(quote && !quoted) *d++='"';
            for(p=params[a-argspec];p && *p;p++) 
            {  if(*p=='"' || *p=='*') *d++='*';
               *d++=*p;
            }
            if(quote && !quoted) *d++='"';
            s++; /* skip %, param specifier is skipped at end of loop */
         }
         else *d++=*s;
      }
      else
      {  if(*s=='"') quoted=!quoted;
         *d++=*s;
      }
      s++;
   }
   *d='\0';
   return d;
}

BOOL Spawn(BOOL del,UBYTE *cmd,UBYTE *args,UBYTE *argspec,...)
{  UBYTE **params=(UBYTE **)((ULONG *)&argspec+1);
   UBYTE *conparams[2];
   BOOL result=FALSE;
   long out,len,conlen;
   UBYTE *buffer,*conbuf;
   long pl=Copypathlist();
   long lock;
   __aligned struct FileInfoBlock fib={0};
   BOOL script=FALSE;
   if(lock=Lock(cmd,SHARED_LOCK))
   {  if(Examine(lock,&fib))
      {  if(fib.fib_Protection&FIBF_SCRIPT) script=TRUE;
      }
      UnLock(lock);
   }
   len=20+strlen(cmd)+1+Pformatlength(args,argspec,params);
   if(del) len+=24+strlen(params[0]);
   conparams[0]=AWEBSTR(MSG_AWEB_EXTWINTITLE);
   conparams[1]=(UBYTE *)Agetattr(Aweb(),AOAPP_Screenname);
   conlen=2+Pformatlength(prefs.program.console,"tn",conparams);
   if((buffer=ALLOCTYPE(UBYTE,len,MEMF_PUBLIC))
   && (conbuf=ALLOCTYPE(UBYTE,conlen,MEMF_PUBLIC)))
   {  strcpy(buffer,"failat 30\n");
      if(!script) strcat(buffer,"\"");
      strcat(buffer,cmd);
      if(!script) strcat(buffer,"\"");
      strcat(buffer," ");
      Pformat(buffer+strlen(buffer),args,argspec,params,TRUE);
      if(del) sprintf(buffer+strlen(buffer),"\ndelete \"%s\" quiet",params[0]);
      Pformat(conbuf,prefs.program.console,"tn",conparams,FALSE);
      if(!(out=Open(conbuf,MODE_NEWFILE)))
      {  out=Open("NIL:",MODE_NEWFILE);
      }
      if(out && 0<=SystemTags(buffer,
         SYS_Input,out,
         SYS_Output,NULL,
         SYS_Asynch,TRUE,
         NP_Path,pl,
         TAG_END)) result=TRUE;
      if(!result)
      {  if(out) Close(out);
         if(pl) Freepathlist(pl);
      }
   }
   if(buffer) FREE(buffer);
   if(conbuf) FREE(conbuf);
   return result;
}

static BOOL Openinitialdoc(UBYTE *initialurl,UBYTE local,BOOL veryfirst)
{  void *win;
   struct Url *url;
   UBYTE *urlname,*fragment;
   BOOL opened=FALSE;
   if(local)
   {  if(urlname=ALLOCTYPE(UBYTE,strlen(initialurl)+20,0))
      {  strcpy(urlname,"file:///");
         strcat(urlname,initialurl);
         url=Findurl("",urlname,0);
         FREE(urlname);
      }
      else url=NULL;
   }
   else url=Findurl("",initialurl,0);
   fragment=Fragmentpart(initialurl);
   if(url)
   {  if(veryfirst) win=Firstwindow();
      else win=Anewobject(AOTP_WINDOW,TAG_END);
      if(win) Inputwindoc(win,url,fragment,0);
      opened=TRUE;
   }
   return opened;
}

static void Processcontrol(void)
{  struct Awebcontrolmsg *amsg;
   short i;
   while(amsg=(struct Awebcontrolmsg *)GetMsg(awebcontrolport))
   {  Iconify(FALSE);
      for(i=0;i<16;i++)
      {  if(amsg->urls[i])
            Openinitialdoc(amsg->urls[i],amsg->local[i],FALSE);
      }
      ReplyMsg(amsg);
   }
   Asetattrs(Aweb(),AOAPP_Tofront,TRUE,TAG_END);
}

static BOOL Dupstartupcheck(void)
{  struct Awebcontrolmsg amsg={0};
   struct MsgPort *port;
   short i;
   if(awebcontrolport=CreateMsgPort())
   {  Forbid();
      if(port=FindPort(AWEBCONTROLPORTNAME))
      {  amsg.mn_ReplyPort=awebcontrolport;
         amsg.urls=initialurls;
         amsg.local=localinitialurl;
         PutMsg(port,&amsg);
      }
      else
      {  awebcontrolport->ln_Name=AWEBCONTROLPORTNAME;
         AddPort(awebcontrolport);
         Setprocessfun(awebcontrolport->mp_SigBit,Processcontrol);
      }
      Permit();
      if(port)
      {  WaitPort(awebcontrolport);
         GetMsg(awebcontrolport);
         DeleteMsgPort(awebcontrolport);
         awebcontrolport=NULL;
         for(i=0;i<16;i++)
         {  if(initialurls[i]) FREE(initialurls[i]);
         }
         return FALSE;
      }
      else return TRUE;
   }
   return FALSE;
}

UBYTE *Getmainstr(ULONG msg)
{  return AWEBSTR(msg);
}

/* Locale hook. (hook)->h_Data points at next buffer position. */
static void __saveds __asm Lputchar(register __a0 struct Hook *hook,
   register __a1 UBYTE c)
{  UBYTE *p=hook->h_Data;
   *p++=c;
   hook->h_Data=p;
}

long LprintfA(UBYTE *buffer,UBYTE *fmt,void *args)
{  struct Hook hook;
   hook.h_Entry=(HOOKFUNC)Lputchar;
   hook.h_Data=buffer;
   FormatString(locale,fmt,args,&hook);
   return (UBYTE *)hook.h_Data-buffer;
}

long Lprintf(UBYTE *buffer,UBYTE *fmt,...)
{  void *args=&fmt+1;
   return LprintfA(buffer,fmt,args);
}

long Lprintdate(UBYTE *buffer,UBYTE *fmt,struct DateStamp *ds)
{  struct Hook hook;
   hook.h_Entry=(HOOKFUNC)Lputchar;
   hook.h_Data=buffer;
   FormatDate(locale,fmt,ds,&hook);
   return (UBYTE *)hook.h_Data-buffer;
}

void __saveds __asm Awebbackfillhook(register __a0 struct Hook *hook,
   register __a2 struct RastPort *lrp,
   register __a1 struct Layermessage *msg)
{  struct Abackfilldata *data=hook->h_Data;
   struct RastPort rp=*lrp;
   rp.Layer=NULL;
   if(data)
   {  if(data->flags&ABKFIL_BACKFILL)
      {  SetAPen(&rp,data->bgpen);
         RectFill(&rp,msg->MinX,msg->MinY,msg->MaxX,msg->MaxY);
      }
      if(data->flags&ABKFIL_BORDER)
      {  SetAPen(&rp,data->fgpen);
         Move(&rp,msg->MinX,msg->MinY);
         Draw(&rp,msg->MaxX,msg->MinY);
         Draw(&rp,msg->MaxX,msg->MaxY);
         Draw(&rp,msg->MinX,msg->MaxY);
         Draw(&rp,msg->MinX,msg->MinY);
      }
   }
}

ULONG Clipto(struct RastPort *rp,short minx,short miny,short maxx,short maxy)
{  struct Clipinfo *ci;
   struct Rectangle rect;
   if(!rp || !rp->Layer) return 0;
   ci=ALLOCSTRUCT(Clipinfo,1,MEMF_CLEAR);
   if(!ci) return 0;
   rect.MinX=minx;
   rect.MinY=miny;
   rect.MaxX=maxx;
   rect.MaxY=maxy;
   if(!(ci->region=NewRegion()))
   {  FREE(ci);
      return 0;
   }
   if(!OrRectRegion(ci->region,&rect))
   {  DisposeRegion(ci->region);
      FREE(ci);
      return 0;
   }
   ci->layer=rp->Layer;
   if((ci->window=rp->Layer->Window)
    && Agetattr((void *)ci->window->UserData,AOWIN_Refreshing))
   {  EndRefresh(ci->window,FALSE);
   }
   else ci->window=NULL;
   LockLayerInfo(ci->layer->LayerInfo);
   LockLayer(0,ci->layer);
   ci->oldregion=InstallClipRegion(ci->layer,ci->region);
   if(ci->window) BeginRefresh(ci->window);
   if(clip_stack_n < CLIPSTACKMAX)
      clip_stack[clip_stack_n++] = (ULONG)ci;
   return (ULONG)ci;
}

void Unclipto(ULONG p)
{  struct Clipinfo *ci=(struct Clipinfo *)p;
   if(p && clip_stack_n > 0 && clip_stack[clip_stack_n-1] == p)
      clip_stack_n--;
   if(ci)
   {  if(ci->region)
      {  if(ci->layer)
         {  if(ci->window) EndRefresh(ci->window,FALSE);
            InstallClipRegion(ci->layer,ci->oldregion);
            UnlockLayer(ci->layer);
            UnlockLayerInfo(ci->layer->LayerInfo);
            if(ci->window) BeginRefresh(ci->window);
         }
         DisposeRegion(ci->region);
      }
      FREE(ci);
   }
}

static void Drainallclips(void)
{  while(clip_stack_n > 0)
      Unclipto(clip_stack[clip_stack_n-1]);
}

struct Coords *Clipcoords(void *cframe,struct Coords *coo)
{  struct Clipcoords *clc;
   if(!coo)
   {  clc=ALLOCSTRUCT(Clipcoords,1,MEMF_CLEAR);
      Framecoords(cframe,clc);
      if(clc->win && clc->rp)
      {  clc->clipkey=Clipto(clc->rp,clc->minx,clc->miny,clc->maxx,clc->maxy);
      }
      coo=clc;
      if(coo) coo->nestcount=1;
   }
   if(coo && coo->nestcount) coo->nestcount++;
   return coo;
}

void Unclipcoords(struct Coords *coo)
{  struct Clipcoords *clc=(struct Clipcoords *)coo;
   if(!coo) return;
   /* Match aweb.c: stack Coords use nestcount 0; do not decrement. */
   if(coo->nestcount<=0) return;
   coo->nestcount--;
   if(coo->nestcount==1)
   {  if(clc->clipkey) Unclipto(clc->clipkey);
      FREE(clc);
   }
}

ULONG Today(void)
{  ULONG sec,mic;
   CurrentTime(&sec,&mic);
   return (ULONG)(sec+60*locale->loc_GMTOffset);
}

static UBYTE *Getdatetoken(UBYTE *p,UBYTE *token)
{  while(*p && !isalnum(*p)) p++;
   while(*p && isalnum(*p)) *token++=*p++;
   *token='\0';
   return p;
}

ULONG Scandate(UBYTE *buf)
{  UBYTE *p;
   UBYTE token[16];
   struct ClockData cd={0};
   BOOL ok,first=TRUE;
   short i;
   ULONG stamp;
   p=buf;
   for(;;)
   {  p=Getdatetoken(p,token);
      if(!*token) break;
      ok=FALSE;
      for(i=0;i<12;i++)
      {  if(STRNIEQUAL(token,months[i],3))
         {  cd.month=i+1;
            ok=TRUE;
            break;
         }
      }
      if(ok) continue;
      if(*p==':')
      {  sscanf(token,"%hd",&i);
         cd.hour=i;
         p=Getdatetoken(p,token);
         sscanf(token,"%hd",&i);
         cd.min=i;
         p=Getdatetoken(p,token);
         sscanf(token,"%hd",&i);
         cd.sec=i;
         continue;
      }
      if(sscanf(token,"%hd",&i))
      {  if(first)
         {  cd.mday=i;
            first=FALSE;
         }
         else
         {  if(i<78) i+=2000;          /* e.g. 01 -> 2001 */
            else if(i<1978) i+=1900;   /* e.g. 96 -> 1996 */
                                       /* else: e.g. 1996 */
            cd.year=i;
         }
      }
   }
   stamp=CheckDate(&cd);
   /* In case of invalid date, make sure it is in the past */
   if(!stamp) stamp=1;
   return stamp;
}

void Makedate(ULONG stamp,UBYTE *buf)
{  struct ClockData cd={0};
   Amiga2Date(stamp,&cd);
   sprintf(buf,"%s, %02d %s %4d %02d:%02d:%02d GMT",
      days[cd.wday],cd.mday,months[cd.month-1],cd.year,cd.hour,cd.min,cd.sec);
}

struct Aobject *Aweb(void)
{  return aweb;
}

void Safeclosewindow(struct Window *w)
{  struct IntuiMessage *imsg;
   struct Node *succ;
   Forbid();
   if(w->UserPort)
   {  imsg=(struct IntuiMessage *)w->UserPort->mp_MsgList.lh_Head;
      while(succ=imsg->ExecMessage.mn_Node.ln_Succ)
      {  if(imsg->IDCMPWindow==w)
         {  Remove(imsg);
            ReplyMsg(imsg);
         }
         imsg=(struct IntuiMessage *)succ;
      }
   }
   w->UserPort=NULL;
   ModifyIDCMP(w,0);
   Permit();
   CloseWindow(w);
}

UBYTE *Savepath(void *url)
{  UBYTE *path,*name=NULL,*fname;
   long len;
   if(url && (path=(UBYTE *)Agetattr(Aweb(),AOAPP_Savepath)))
   {  if(fname=(UBYTE *)Agetattr(url,AOURL_Url))
      {  fname=Urlfilename(fname);
      }
      if(!fname || !*fname) fname=Dupstr(prefs.network.localindex,-1);
      len=strlen(path);
      if(fname) len+=strlen(fname);
      if(name=ALLOCTYPE(UBYTE,len+4,MEMF_PUBLIC))
      {  strcpy(name,path);
         AddPart(name,fname?fname:NULLSTRING,len+3);
      }
      if(fname) FREE(fname);
   }
   return name;
}

void Setprocessfun(short sigbit,void (*fun)(void))
{  if(sigbit>=16 && sigbit<32)
   {  processfun[sigbit-16]=fun;
      if(fun) waitmask|=1<<sigbit;
      else waitmask&=~(1<<sigbit);
   }
}

UBYTE *Fullname(UBYTE *name)
{  long lock=Lock(name,SHARED_LOCK);
   UBYTE *buf=NULL;
   if(lock)
   {  if(buf=ALLOCTYPE(UBYTE,STRINGBUFSIZE+1,MEMF_PUBLIC))
      {  if(!NameFromLock(lock,buf,STRINGBUFSIZE))
         {  FREE(buf);
            buf=NULL;
         }
      }
      UnLock(lock);
   }
   return buf;
}

BOOL Readonlyfile(UBYTE *name)
{  long lock;
   __aligned struct InfoData id={0};
   struct FileInfoBlock *fib;
   BOOL readonly=FALSE;
   if(lock=Lock(name,SHARED_LOCK))
   {  if(Info(lock,&id))
      {  readonly=(id.id_DiskState!=ID_VALIDATED);
      }
      if(!readonly && (fib=AllocDosObjectTags(DOS_FIB,TAG_END)))
      {  if(Examine(lock,fib))
         {  readonly=(fib->fib_DirEntryType>=0) || (fib->fib_Protection&FIBF_DELETE);
         }
         FreeDosObject(DOS_FIB,fib);
      }
      UnLock(lock);
   }
   else readonly=TRUE;
   return readonly;
}

void Changedlayout(void)
{  updateframes=TRUE;
}

void Deferflushmem(void)
{  flushsources=TRUE;
}

ULONG Waitprocessaweb(ULONG extramask)
{  ULONG getmask;
   short i;
   do
   {  getmask=Wait(waitmask|extramask);
      for(i=0;i<16;i++)
      {  if((getmask&(1<<(i+16))) && processfun[i])
         {  processfun[i]();
         }
      }
   } while(!(getmask&extramask));
   return (getmask&extramask);
}

void Queuesetmsgdata(void *object,ULONG queueid,ULONG userdata)
{  struct Smqnode *qn,*qnn;
   if(queueid)
   {  if(qn=ALLOCSTRUCT(Smqnode,1,0))
      {  qn->object=object;
         qn->queueid=queueid;
         qn->userdata=userdata;
         ADDTAIL(&setmsgqueue,qn);
      }
   }
   else
   {  for(qn=setmsgqueue.first;qn->next;qn=qnn)
      {  qnn=qn->next;
         if(qn->object==object)
         {  REMOVE(qn);
            FREE(qn);
         }
      }
   }
}

void Queuesetmsg(void *object,ULONG queueid)
{  Queuesetmsgdata(object,queueid,0);
}

static void Setmsgqueue(void)
{  struct Smqnode *qn;
   /* First set the selected flag on all present nodes, then process only
    * the selected ones. */
   for(qn=setmsgqueue.first;qn->next;qn=qn->next)
   {  qn->selected=TRUE;
   }
   while(setmsgqueue.first->next && setmsgqueue.first->selected)
   {  qn=REMHEAD(&setmsgqueue);
      Asetattrs(qn->object,
         AOBJ_Queueid,qn->queueid,
         AOBJ_Queuedata,qn->userdata,
         TAG_END);
      FREE(qn);
   }
}

struct Library *Openaweblib(UBYTE *name)
{  struct Library *base=OpenLibrary(name,AWEBLIBVERSION);
   if(base)
   {  if(base->lib_Version!=AWEBLIBVERSION || base->lib_Revision!=AWEBLIBREVISION)
      {  CloseLibrary(base);
         base=NULL;
      }
   }
   if(!base)
   {  Lowlevelreq(AWEBSTR(MSG_ERROR_CANTOPEN),name);
   }
   return base;
}

struct Library *Openjslib(void)
{  if(!AWebJSBase)
   {  if(!(AWebJSBase=OpenLibrary("aweblib/awebjs.aweblib",0))
      && !(AWebJSBase=OpenLibrary("PROGDIR:aweblib/awebjs.aweblib",0))
      && !(AWebJSBase=OpenLibrary("AWeb:aweblib/awebjs.aweblib",0)))
      {  Lowlevelreq(AWEBSTR(MSG_ERROR_CANTOPEN),"awebjs.aweblib");
      }
   }
   return AWebJSBase;
}

BOOL Stackoverflow(void)
{  struct Task *task=FindTask(NULL);
   /* Since (task) is a stack variable, (&task) is a good approximation of
    * the current stack pointer */
   return (BOOL)((ULONG)&task<(ULONG)task->tc_SPLower+4000);
}

BOOL Awebactive(void)
{  struct IntuitionBase *ibase=(struct IntuitionBase *)IntuitionBase;
   ULONG lock=LockIBase(0);
   struct Task *wtask=NULL;
   if(ibase->ActiveWindow && ibase->ActiveWindow->UserPort)
   {  wtask=ibase->ActiveWindow->UserPort->mp_SigTask;
   }
   UnlockIBase(lock);
   return Isawebtask(wtask);
}

/*-----------------------------------------------------------------------*/

void Openloadreq(struct Screen *screen)
{  if(!StartupBase) StartupBase=OpenLibrary("aweblib/startup.aweblib",0);
   if(!StartupBase) StartupBase=OpenLibrary("PROGDIR:aweblib/startup.aweblib",0);
   if(!StartupBase) StartupBase=OpenLibrary("AWeb:aweblib/startup.aweblib",0);
   if(StartupBase) Startupopen(screen,awebversion);
}

static void Closeloadreq(void)
{  if(StartupBase)
   {  Startupclose();
      CloseLibrary(StartupBase);
      StartupBase=NULL;
   }
}

void Setloadreqstate(USHORT state)
{  if(StartupBase) Startupstate(state);
}

void Setloadreqlevel(long ready,long total)
{  if(StartupBase) Startuplevel(ready,total);
}

/*-----------------------------------------------------------------------*/

static void Cleanup(void)
{  Drainallclips();
   Exitcache();
   Freetooltip();
   Freemime();
   Freeboopsi();
#ifndef LOCALONLY
   Freeauthor();
   Freetcp();     /* MUST be called before Freerequest(), Freeprefs() */
#endif
   Freecookie();
   if(aweb)
   {  Adisposeobject(aweb);   /* MUST be called before Freerequest() */
      aweb=NULL;
   }
   Freerequest();
   Freeprefs();
   Freeobject();  /* MUST be called before Freesupport(), Freeapplication() */

   Freeapplication();   /* after Freeobject bcz it frees the JS context */
   Freearexx();   /* after Freeobject() bcz it waits for scripts to complete */
#ifndef LOCALONLY
   Freehttp();    /* after Freeobject() bcz tasks must be stopped */
   Freenameserv();/* after Freeobject() bcz network tasks use hent structures freed here */
   Freeamissl();  /* after Freehttp() bcz all SSL connections must be closed first */
#endif
   Freesupport();
   if(locale) CloseLocale(locale);
   if(localeinfo.li_Catalog) CloseCatalog(localeinfo.li_Catalog);
   if(AWebJSBase) CloseLibrary(AWebJSBase);
   if(ButtonBase) CloseLibrary((struct Library *)ButtonBase);
   if(openedca)
   {  if(BitMapBase) CloseLibrary((struct Library *)BitMapBase);
      if(BevelBase) CloseLibrary((struct Library *)BevelBase);
      if(GlyphBase) CloseLibrary((struct Library *)GlyphBase);
      if(WindowBase) CloseLibrary((struct Library *)WindowBase);
      if(LayoutBase) CloseLibrary((struct Library *)LayoutBase);
      if(StringBase) CloseLibrary((struct Library *)StringBase);
      if(SpaceBase) CloseLibrary((struct Library *)SpaceBase);
      if(LabelBase) CloseLibrary((struct Library *)LabelBase);
      if(DrawListBase) CloseLibrary((struct Library *)DrawListBase);
      if(CheckBoxBase) CloseLibrary((struct Library *)CheckBoxBase);
      if(IntegerBase) CloseLibrary((struct Library *)IntegerBase);
      if(ChooserBase) CloseLibrary((struct Library *)ChooserBase);
      if(ListBrowserBase) CloseLibrary((struct Library *)ListBrowserBase);
      if(SpeedBarBase) CloseLibrary((struct Library *)SpeedBarBase);
      if(ScrollerBase) CloseLibrary((struct Library *)ScrollerBase);
      if(FuelGaugeBase) CloseLibrary((struct Library *)FuelGaugeBase);
      if(PenMapBase) CloseLibrary((struct Library *)PenMapBase);
   }
   if(WorkbenchBase) CloseLibrary(WorkbenchBase);
   if(IFFParseBase) CloseLibrary(IFFParseBase);
   if(LocaleBase) CloseLibrary(LocaleBase);
   if(IconBase) CloseLibrary(IconBase);
   if(KeymapBase) CloseLibrary(KeymapBase);
   if(AslBase) CloseLibrary(AslBase);
   if(DataTypesBase) CloseLibrary(DataTypesBase);
   if(GadToolsBase) CloseLibrary(GadToolsBase);
   if(ColorWheelBase) CloseLibrary(ColorWheelBase);
   if(LayersBase) CloseLibrary(LayersBase);
   if(DiskfontBase) CloseLibrary(DiskfontBase);
   if(UtilityBase) CloseLibrary(UtilityBase);
   if(GfxBase) CloseLibrary(GfxBase);
   if(IntuitionBase) CloseLibrary(IntuitionBase);
   if(awebcontrolport)
   {  struct Message *msg;
      Forbid();
      RemPort(awebcontrolport);
      while(msg=GetMsg(awebcontrolport)) ReplyMsg(msg);
      Permit();
      DeleteMsgPort(awebcontrolport);
   }
   /* Keep our pooled memory alive until all libraries/classes are closed. */
   Freememory();
   report();
}

void Quit(BOOL immediate)
{  if(immediate || !Quitreq()) quit=TRUE;
}

void Iconify(BOOL i)
{  icondefer=TRUE;
   iconify=i;
}

void Lowlevelreq(UBYTE *msg,...)
{  struct EasyStruct es;
   BOOL opened=FALSE;
   struct Window *window=(struct Window *)Agetattr(Firstwindow(),AOWIN_Window);
   va_list args;
   va_start(args,msg);
   if(!IntuitionBase)
   {  if(IntuitionBase=(struct IntuitionBase *)OpenLibrary("intuition.library",36)) opened=TRUE;
   }
   if(IntuitionBase)
   {  es.es_StructSize=sizeof(struct EasyStruct);
      es.es_Flags=0;
      es.es_Title="AWeb";
      es.es_TextFormat=msg;
      es.es_GadgetFormat="Ok";
      EasyRequestArgs(window,&es,NULL,args);
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

static void *Openlib(UBYTE *name,long version)
{  struct Library *lib=OpenLibrary(name,version);
   if(!lib)
   {  Lowlevelreq(AWEBSTR(MSG_ERROR_CANTOPENV),name,version);
      Cleanup();
      exit(10);
   }
   return lib;
}

struct ClassLibrary *Openclass(UBYTE *name,long version)
{  struct ClassLibrary *base;
   if(!(base=(struct ClassLibrary *)OpenLibrary(name,version)))
   {  UBYTE *msg=AWEBSTR(MSG_ERROR_CANTOPEN);
#ifdef DEMOVERSION
      UBYTE *extramsg=
         "\nYou need a recent version of Reaction."
         "\nAvailable via FTP from"
         "\nftp.thule.no/pub/reaction/";
      UBYTE *buf;
      buf=ALLOCTYPE(UBYTE,strlen(msg)+strlen(extramsg)+4,0);
      if(buf)
      {  strcpy(buf,msg);
         strcat(buf,extramsg);
         msg=buf;
      }
#endif
      Lowlevelreq(msg,name);
#ifdef DEMOVERSION
      if(buf) FREE(buf);
#endif
      Cleanup();
      exit(10);
   }
   return base;
}

/* Create AWeb: assign early, before any code tries to access it */
static void InitAWebAssign(void)
{  long lock;
   struct Process *process;
   struct DosList *doslist;
   /* Initialize to FALSE - only set TRUE if we create the assign */
   awebpath=FALSE;
   /* Check if AWeb: assign already exists using FindDosEntry (doesn't trigger mount prompt) */
   /* Check both DEVICES and ASSIGNS lists */
   doslist=LockDosList(LDF_DEVICES|LDF_ASSIGNS|LDF_READ);
   if(doslist)
   {  doslist=FindDosEntry(doslist,"AWeb",LDF_DEVICES|LDF_ASSIGNS);
      if(doslist)
      {  /* Assign already exists, don't create or remove it */
         UnLockDosList(LDF_DEVICES|LDF_ASSIGNS|LDF_READ);
         return;
      }
      UnLockDosList(LDF_DEVICES|LDF_ASSIGNS|LDF_READ);
   }
   /* Assign doesn't exist, create it */
   /* Try PROGDIR: first (safest, works on all AmigaOS versions) */
   lock=Lock("PROGDIR:",ACCESS_READ);
   if(!lock)
   {  /* PROGDIR: not available, fall back to current working directory (CLI only) */
      process=(struct Process *)FindTask(NULL);
      if(process && process->pr_CLI)
      {  lock=DupLock(0);
      }
   }
   if(lock)
   {  /* Try to create the assign */
      if(AssignLock("AWeb",lock))
      {  /* We successfully created it, mark for removal on exit */
         awebpath=TRUE;
      }
      else
      {  /* Failed to create assign */
         UnLock(lock);
      }
   }
}

static BOOL Initall(void)
{
   if(!(locale=OpenLocale(NULL))) return FALSE;
   if(!Initversion()) return FALSE;
   if(!Initarexx()) return FALSE;   /* must be inited before window */
   if(!Initmime()) return FALSE;    /* must be inited before prefs */
   if(!Initdefprefs()) return FALSE;/* must be inited before prefs */
   if(!Initprefs()) return FALSE;   /* must be inited before window,layout, cache */
   if(!Initboopsi()) return FALSE;  /* must be inited before window */
   if(!Initrequest()) return FALSE; /* must be inited before io */
   if(!Installapplication()) return FALSE;   /* Opens the startup window */
   if(!Installurl()) return FALSE;
   if(!Installscroller()) return FALSE;
   if(!Installdocsource()) return FALSE;
   if(!Installdocument()) return FALSE;
   if(!Installdocext()) return FALSE;
   if(!Installbody()) return FALSE;
   if(!Installlink()) return FALSE;
   if(!Installelement()) return FALSE;
   if(!Installtext()) return FALSE;
   if(!Installbreak()) return FALSE;
   if(!Installruler()) return FALSE;
   if(!Installbullet()) return FALSE;
   if(!Installtable()) return FALSE;
   if(!Installmap()) return FALSE;
   if(!Installarea()) return FALSE;
   if(!Installwinhis()) return FALSE;
   if(!Installname()) return FALSE;
   if(!Installframe()) return FALSE;
   if(!Installframeset()) return FALSE;
   if(!Installform()) return FALSE;
   if(!Installfield()) return FALSE;
   if(!Installbutton()) return FALSE;
   if(!Installcheckbox()) return FALSE;
   if(!Installradio()) return FALSE;
   if(!Installinput()) return FALSE;
   if(!Installselect()) return FALSE;
   if(!Installtextarea()) return FALSE;
   if(!Installhidden()) return FALSE;
   if(!Installfilefield()) return FALSE;
   if(!Installfetch()) return FALSE;
   if(!Installsource()) return FALSE;
   if(!Installcopy()) return FALSE;
   if(!Installfile()) return FALSE;
   if(!Installcache()) return FALSE;
   if(!Installwindow()) return FALSE;
   if(!Installwhiswindow()) return FALSE;
   if(!Installfilereq()) return FALSE;
   if(!Installsaveas()) return FALSE;
   if(!Installimgsource()) return FALSE;
   if(!Installimgcopy()) return FALSE;
   if(!Installpopup()) return FALSE;
   if(!Installextprog()) return FALSE;
   if(!Installnetstatwin()) return FALSE;
   if(!Installhotlist()) return FALSE;
   if(!Installcabrowse()) return FALSE;
   if(!Installeditor()) return FALSE;
   if(!Installsearch()) return FALSE;
   if(!Installprintwindow()) return FALSE;
   if(!Installprint()) return FALSE;
   if(!Installsaveiff()) return FALSE;
   if(!Installtask()) return FALSE;
   if(!Installsourcedriver()) return FALSE;
   if(!Installcopydriver()) return FALSE;
   if(!Installsoundsource()) return FALSE;
   if(!Installsoundcopy()) return FALSE;
   if(!Installtimer()) return FALSE;
   if(!Installinfo()) return FALSE;
   if(!Installauthedit()) return FALSE;

   if(!(aweb=Anewobject(AOTP_APPLICATION,TAG_END))) return FALSE;
   Setloadreqstate(LRQ_FONTS);
   if(!Initprefs2()) return FALSE;
   if(!Initurl()) return FALSE;
   Setloadreqstate(LRQ_CACHE);
   if(!Initcache()) return FALSE;   /* must be inited before url(2) */
   if(!Initurl2()) return FALSE;
#ifndef LOCALONLY
   if(!Initnameserv()) return FALSE;
   if(!Initauthor()) return FALSE;
#endif
   if(!Initcookie()) return FALSE;
#ifndef LOCALONLY
   if(!Inittcp()) return FALSE;
   if(!Inithttp()) return FALSE;
#endif
   if(!Initselect()) return FALSE;
   if(!Initcheckbox()) return FALSE;
   if(!Initradio()) return FALSE;
   
   Closeloadreq();
   
   if(!Anewobject(AOTP_WINDOW,TAG_END)) return FALSE;
   if(!Initscroller()) return FALSE;   /* Needs a window */
   return TRUE;
}

static void Getprogramname(struct WBStartup *wbs)
{  long progdir;
   UBYTE progbuf[40];
   if(wbs)
   {  if(NameFromLock(wbs->sm_ArgList[0].wa_Lock,programname,256))
      {  AddPart(programname,wbs->sm_ArgList[0].wa_Name,256);
      }
   }
   else
   {  if((progdir=GetProgramDir())
      && NameFromLock(progdir,programname,256)
      && GetProgramName(progbuf,40))
      {  AddPart(programname,progbuf,256);
      }
   }
}

/* Check if URL string starts with a known protocol prefix */
static BOOL Hasprotocol(UBYTE *url)
{  if(!url) return FALSE;
   /* Check for protocols that use "://" */
   if(STRNIEQUAL(url,"HTTP://",7)) return TRUE;
   if(STRNIEQUAL(url,"HTTPS://",8)) return TRUE;
   if(STRNIEQUAL(url,"FTP://",6)) return TRUE;
   if(STRNIEQUAL(url,"GOPHER://",9)) return TRUE;
   if(STRNIEQUAL(url,"GEMINI://",9)) return TRUE;
   if(STRNIEQUAL(url,"SPARTAN://",10)) return TRUE;
   if(STRNIEQUAL(url,"FILE://",7)) return TRUE;
   if(STRNIEQUAL(url,"NNTP://",7)) return TRUE;
   if(STRNIEQUAL(url,"TELNET://",9)) return TRUE;
   /* Check for protocols that use ":" without "//" */
   if(STRNIEQUAL(url,"MAILTO:",7)) return TRUE;
   if(STRNIEQUAL(url,"NEWS:",5)) return TRUE;
   if(STRNIEQUAL(url,"X-AWEB:",7)) return TRUE;
   if(STRNIEQUAL(url,"ABOUT:",6)) return TRUE;
   if(STRNIEQUAL(url,"CID:",4)) return TRUE;
   if(STRNIEQUAL(url,"DATA:",5)) return TRUE;
   return FALSE;
}

static void Getarguments(struct WBStartup *wbs)
{  long args[16]={0};
#ifdef LOCALONLY
   UBYTE *argtemplate="FILE/M,CONFIG/K";
#else
   UBYTE *argtemplate="URL/M,LOCAL/S,CONFIG/K,HOTLIST/K"
#ifndef DEMOVERSION
      ",HAIKU=VVIIV/S"
#endif
      ",HTTPDEBUG/S,NOSTARTUP/S"
#ifdef BETAKEYFILE
#ifdef DEMOVERSION
      ",Q/S"
#endif
      ",NOPOOL/S,SPECDEBUG/S"
#ifdef DEVELOPER
      ",BLOCK/K/N,PROFILE/S,OODEBUG/K,OOMETHOD/K,OODELAY/S,USETEMP/S,OOKDEBUG/S"
#endif
#endif
      ;
#endif
   long i,nurl=0;
   struct Process *process;
   if(wbs)
   {  long oldcd=CurrentDir(wbs->sm_ArgList[0].wa_Lock);
      struct DiskObject *dob=GetDiskObject(wbs->sm_ArgList[0].wa_Name);
      if(dob)
      {  UBYTE **ttp;
         for(ttp=dob->do_ToolTypes;ttp && *ttp;ttp++)
         {
#ifdef LOCALONLY
            if(STRNIEQUAL(*ttp,"FILE=",5))
            {  if(nurl<16)
               {  UBYTE *filestr=*ttp+5;
                  initialurls[nurl]=Dupstr(filestr,-1);
                  /* Always treat as local file path */
                  localinitialurl[nurl]=1;
                  nurl++;
               }
            }
#else
            if(STRNIEQUAL(*ttp,"URL=",4))
            {  if(nurl<16)
               {  UBYTE *urlstr=*ttp+4;
                  initialurls[nurl]=Dupstr(urlstr,-1);
                  /* If URL lacks protocol, treat as local file path */
                  if(initialurls[nurl] && !Hasprotocol(initialurls[nurl]))
                  {  localinitialurl[nurl]=1;
                  }
                  else
                  {  localinitialurl[nurl]=0;
                  }
                  nurl++;
               }
            }
            else if(STRIEQUAL(*ttp,"LOCAL")) args[1]=TRUE;
#endif
            else if(STRNIEQUAL(*ttp,"CONFIG=",7)) Setprefsname(*ttp+7);
            else if(STRNIEQUAL(*ttp,"HOTLIST=",8)) Sethotlistname(*ttp+8);
            else if(STRIEQUAL(*ttp,"HTTPDEBUG")) httpdebug=TRUE;
            else if(STRIEQUAL(*ttp,"NOSTARTUP")) nostartup=TRUE;
#ifndef DEMOVERSION
            else if(STRIEQUAL(*ttp,"VVIIV")) haiku=TRUE;
#endif
#ifdef BETAKEYFILE
            else if(STRIEQUAL(*ttp,"NOPOOL")) nopool=TRUE;
#ifdef DEVELOPER
            else if(STRNIEQUAL(*ttp,"BLOCK=",6)) sscanf(*ttp+6," %ld",&localblocksize);
#endif
#endif
         }
         FreeDiskObject(dob);
      }
      CurrentDir(oldcd);
      for(i=1;i<wbs->sm_NumArgs && nurl<16;i++)
      {  UBYTE buffer[256];
         if(NameFromLock(wbs->sm_ArgList[i].wa_Lock,buffer,255)
         && AddPart(buffer,wbs->sm_ArgList[i].wa_Name,255))
         {  localinitialurl[nurl]=1;
            initialurls[nurl++]=Dupstr(buffer,-1);
         }
      }
      /* CRITICAL: Check if mn_ReplyPort is NULL before accessing mp_SigTask */
      /* If mn_ReplyPort is NULL, accessing ->mp_SigTask will cause a crash */
      /* The crash pattern shows dar=0x00000070, which is offset 112 (mp_SigTask offset) from NULL */
      /* Note: mn_ReplyPort is inside sm_Message structure according to workbench/startup.h */
      if(wbs->sm_Message.mn_ReplyPort)
      {  process=(struct Process *)wbs->sm_Message.mn_ReplyPort->mp_SigTask;
      }
      else
      {  /* Fall back to FindTask(NULL) if mn_ReplyPort is NULL */
         process=(struct Process *)FindTask(NULL);
      }
   }
   else
      {  struct RDArgs *rda=ReadArgs(argtemplate,args,NULL);
         if(rda)
         {  UBYTE **p;
            for(p=(UBYTE **)args[0];p && *p;p++)
            {  if(nurl<16)
               {
#ifdef LOCALONLY
                  UBYTE *filestr=*p;
                  initialurls[nurl]=Dupstr(filestr,-1);
                  /* Always treat as local file path */
                  localinitialurl[nurl]=1;
#else
                  UBYTE *urlstr=*p;
                  initialurls[nurl]=Dupstr(urlstr,-1);
                  /* If URL lacks protocol, treat as local file path */
                  if(initialurls[nurl] && !Hasprotocol(initialurls[nurl]))
                  {  localinitialurl[nurl]=1;
                  }
                  else
                  {  localinitialurl[nurl]=0;
                  }
#endif
                  nurl++;
               }
            }
#ifdef LOCALONLY
            if(args[1]) Setprefsname((UBYTE *)args[1]);
#else
            if(args[2]) Setprefsname((UBYTE *)args[2]);
            if(args[3]) Sethotlistname((UBYTE *)args[3]);
#endif
#ifndef DEMOVERSION
         if(args[4]) haiku=TRUE;
         if(args[5]) httpdebug=TRUE;
         if(args[6]) nostartup=TRUE;
#else
         if(args[4]) httpdebug=TRUE;
         if(args[5]) nostartup=TRUE;
#endif
#ifdef BETAKEYFILE
#ifndef DEMOVERSION
         if(args[7]) nopool=TRUE;
         if(args[8]) specdebug=TRUE;
#else
         if(args[7]) nopool=TRUE;
         if(args[8]) specdebug=TRUE;
#endif
#ifdef DEVELOPER
#ifndef DEMOVERSION
         if(args[9]) localblocksize=*(long *)args[9];
         if(args[10]) profile=TRUE;
         if(args[11]) Setoodebug((UBYTE *)args[11]);
         if(args[12]) Setoomethod((UBYTE *)args[12]);
         if(args[13]) Setoodelay();
         if(args[14]) usetemp=TRUE;
         if(args[15]) ookdebug=TRUE;
#else
         if(args[9]) localblocksize=*(long *)args[9];
         if(args[10]) profile=TRUE;
         if(args[11]) Setoodebug((UBYTE *)args[11]);
         if(args[12]) Setoomethod((UBYTE *)args[12]);
         if(args[13]) Setoodelay();
         if(args[14]) usetemp=TRUE;
         if(args[15]) ookdebug=TRUE;
#endif
#endif
#endif
         FreeArgs(rda);
      }
      process=(struct Process *)FindTask(NULL);
   }
   if(args[1])
   {  for(i=0;i<16;i++) localinitialurl[i]=1;
   }
   if(process && process->pr_CLI)
   {  struct CommandLineInterface *cli=(struct CommandLineInterface *)BADDR(process->pr_CLI);
      ourpathlist=(struct PathList *)BADDR(cli->cli_CommandDir);
   }
}

static BOOL Initialdocs(void)
{  short i;
   BOOL opened=FALSE;
   for(i=0;i<16;i++)
   {  if(initialurls[i])
      {  opened=Openinitialdoc(initialurls[i],localinitialurl[i],i==0);
         FREE(initialurls[i]);
      }
   }
   return opened;
}

int main(int fromcli,struct WBStartup *wbs)
{  ULONG getmask;
   struct Url *home;
   struct Library *execbase=*((struct Library **)4);
   short i;
#ifdef NETDEMO
#ifndef OSVERSION
   ULONG secs,mics,nextannoy;
   #define ANNOYPERIOD 30*60
#endif
#endif
   NEWLIST(&setmsgqueue);
#ifdef NEED35
   WorkbenchBase=OpenLibrary("workbench.library",44);
   if(WorkbenchBase)
   {  CloseLibrary(WorkbenchBase);
   }
   else
   {  UBYTE *msg=AWEBSTR(MSG_ERROR_NEEDOS30);
      UBYTE *buf=(UBYTE *)AllocVec(strlen(msg)+1,MEMF_PUBLIC);
      if(buf)
      {  UBYTE *p;
         strcpy(buf,msg);
         for(p=buf;*p && strncmp(p,"3.0",3);p++);
         if(*p) p[2]='5';
         Lowlevelreq(buf);
         FreeVec(buf);
      }
      return 0;
   }
#else
   if(execbase->lib_Version<39)
   {  Lowlevelreq(AWEBSTR(MSG_ERROR_NEEDOS30));
      return 0;
   }
#endif
#ifdef NOKEYFILE
   UtilityBase=Openlib("utility.library",OSNEED(39,40));
   IntuitionBase=Openlib("intuition.library",OSNEED(39,40));
   GfxBase=Openlib("graphics.library",OSNEED(39,40));
   DiskfontBase=Openlib("diskfont.library",OSNEED(39,44));
   LayersBase=Openlib("layers.library",OSNEED(39,40));
   AslBase=Openlib("asl.library",OSNEED(39,44));
   ColorWheelBase=Openlib("gadgets/colorwheel.gadget",OSNEED(39,44));
   GadToolsBase=Openlib("gadtools.library",OSNEED(39,40));
   IconBase=Openlib("icon.library",OSNEED(39,44));
   LocaleBase=Openlib("locale.library",OSNEED(0,44));
   DataTypesBase=Openlib("datatypes.library",OSNEED(39,44));
   KeymapBase=Openlib("keymap.library",OSNEED(37,40));
#endif
   Getprogramname(fromcli?NULL:wbs);
   /* Create AWeb: assign after Getprogramname but before any code that might access it */
   InitAWebAssign();
   if(!Initmemory())
   {  Cleanup();
      exit(10);
   }
#ifdef BETAKEYFILE
   IconBase=Openlib("icon.library",OSNEED(39,44));
   LocaleBase=Openlib("locale.library",OSNEED(0,44));
#endif
   IFFParseBase=Openlib("iffparse.library",OSNEED(39,40));
   WorkbenchBase=Openlib("workbench.library",OSNEED(39,44));
   if(WorkbenchBase->lib_Version>=44) has35=TRUE;
   ButtonBase=Openclass("gadgets/button.gadget",OSNEED(0,44));
   openedca=TRUE;
   BitMapBase=Openclass("images/bitmap.image",OSNEED(0,44));
   BevelBase=Openclass("images/bevel.image",OSNEED(0,44));
   GlyphBase=Openclass("images/glyph.image",OSNEED(0,44));
   WindowBase=Openclass("window.class",OSNEED(0,44));
   LayoutBase=Openclass("gadgets/layout.gadget",OSNEED(0,44));
   StringBase=Openclass("gadgets/string.gadget",OSNEED(0,44));
   SpaceBase=Openclass("gadgets/space.gadget",OSNEED(0,44));
   LabelBase=Openclass("images/label.image",OSNEED(0,44));
   DrawListBase=Openclass("images/drawlist.image",OSNEED(0,44));
   CheckBoxBase=Openclass("gadgets/checkbox.gadget",OSNEED(0,44));
   IntegerBase=Openclass("gadgets/integer.gadget",OSNEED(0,44));
   ChooserBase=Openclass("gadgets/chooser.gadget",OSNEED(0,44));
   ListBrowserBase=Openclass("gadgets/listbrowser.gadget",OSNEED(0,44));
   SpeedBarBase=Openclass("gadgets/speedbar.gadget",OSNEED(0,44));
   ScrollerBase=Openclass("gadgets/scroller.gadget",OSNEED(0,44));
   FuelGaugeBase=Openclass("gadgets/fuelgauge.gadget",OSNEED(0,44));
   PenMapBase=Openclass("images/penmap.image",OSNEED(0,44));
   Initsupport();
   localeinfo.li_LocaleBase=LocaleBase;
   localeinfo.li_Catalog=OpenCatalogA(NULL,"aweb.catalog",NULL);
   maincatalog=localeinfo.li_Catalog;
   Getarguments(fromcli?NULL:wbs);
   setupprof();
   if(Dupstartupcheck()
   && Initall())
   {  Initialrequester(Aboutreq,NULL);
      if(*prefs.program.startupscript)
      {  Sendarexxcmd(Agetattr(Firstwindow(),AOWIN_Key),prefs.program.startupscript);
      }
      if(!Initialdocs())
      {  UBYTE *fragment;
         if(prefs.network.homeurl && *prefs.network.homeurl && prefs.network.starthomepage
          && (home=Findurl("",prefs.network.homeurl,0)))
         {  fragment=Fragmentpart(prefs.network.homeurl);
            Inputwindoc(Firstwindow(),home,fragment,NULL);
         }
      }
      {  void *win=Firstwindow();
         void *whis=NULL;
         ULONG key=0,nr=0;
         Agetattrs(win,
            AOBJ_Winhis,&whis,
            AOWIN_Key,&key,
            AOWIN_Windownr,&nr,
            TAG_END);
         if(prefs.program.aanetstat) Opennetstat();
         if(prefs.program.aawinhis) Openwhiswindow(whis,nr);
         if(prefs.program.aahotlist) Hotlistviewer(key);
         Delay(2);
         Asetattrs(Firstwindow(),AOWIN_Activate,TRUE,TAG_END);
      }
#ifdef NETDEMO
#ifndef OSVERSION
      Demorequest();
      CurrentTime(&secs,&mics);
      nextannoy=secs+ANNOYPERIOD;
#endif
#endif
      while(!quit)
      {  /* Don't wait if there are still queuenodes queued */
         if(ISEMPTY(&setmsgqueue))
         {  getmask=Wait(waitmask|SIGBREAKF_CTRL_C);
         }
         else
         {  getmask=SetSignal(0,0);
         }
         if(getmask&SIGBREAKF_CTRL_C)
         {  Quit(TRUE);
            break;
         }
         for(i=0;i<16;i++)
         {  if((getmask&(1<<(i+16))) && processfun[i])
            {  processfun[i]();
            }
         }
         if(flushsources)
         {  Flushsources(SRCFT_EXCESS);
            flushsources=FALSE;
         }
         if(updateframes)
         {  Doupdateframes();
            updateframes=FALSE;
         }
         Setmsgqueue();
#ifdef NETDEMO
#ifndef OSVERSION
         CurrentTime(&secs,&mics);
         if(secs>=nextannoy)
         {  quit=TRUE;
         }
#endif
#endif
         if(icondefer)
         {  Asetattrs(Aweb(),AOAPP_Iconify,iconify,TAG_END);
            icondefer=FALSE;
         }
      }
      if(*prefs.program.shutdownscript)
      {  Sendarexxcmd(Agetattr(Firstwindow(),AOWIN_Key),prefs.program.shutdownscript);
      }
#ifdef NETDEMO
#ifndef OSVERSION
      Demorequest();
#endif
#endif
   }
   Closeloadreq();
   Cleanup();
   if(awebpath) AssignLock("AWeb",NULL);
   return 0;
}

#ifdef PROFILE
struct prof
{  long secs,mics,n;
   char *string;
};
static struct prof profs[100];
static long nprof;
static struct MsgPort *profport;
static struct timerequest *profrequest;

void setupprof(void)
{  long i;
   if(profile
   && (profport=CreateMsgPort())
   && (profrequest=(struct timerequest *)CreateStdIO(profport))
   && !OpenDevice("timer.device",UNIT_MICROHZ,profrequest,0))
   {  TimerBase=profrequest->io_Device;
      for(i=0;i<10000;i++)
      {  prolog("....Profile Reference");
         epilog("....Profile Reference");
      }
   }
}

void prolog(char *p)
{  struct timeval tv;
   long n;
   if(*p)
   {  /* virgin string */
      profs[nprof].string=p+4;
      *(long *)p=nprof;
      nprof++;
   }
   n=*(long *)p;
   if(TimerBase && profile && n>=0 && n<nprof)
   {  GetSysTime(&tv);
      profs[n].secs-=tv.tv_secs;
      profs[n].mics-=tv.tv_micro;
      profs[n].n++;
   }
}
void epilog(char *p)
{  struct timeval tv;
   long n=*(long *)p;
   if(TimerBase && profile && n>=0 && n<nprof)
   {  GetSysTime(&tv);
      profs[n].secs+=tv.tv_secs;
      profs[n].mics+=tv.tv_micro;
   }
}

void report(void)
{  int i;
   if(profile)
   {  for(i=0;i<nprof;i++)
      {  printf("%40.40s %12d %6d %12d\n",profs[i].string,
            1000000*profs[i].secs+profs[i].mics,
            profs[i].n,
            profs[i].n?((1000000*profs[i].secs+profs[i].mics)/profs[i].n):0);
      }
   }
   if(TimerBase) CloseDevice(profrequest);
   if(profrequest) DeleteStdIO((struct IOStdReq *)profrequest);
   if(profport) DeleteMsgPort(profport);
}
#endif

#ifdef BETAKEYFILE
void Mprintf(UBYTE *fmt,...)
{  static struct SignalSemaphore sema;
   static BOOL inited=FALSE;
   va_list p;
   Forbid();
   if(!inited)
   {  InitSemaphore(&sema);
      inited=TRUE;
   }
   Permit();
   ObtainSemaphore(&sema);
   va_start(p,fmt);
   vprintf(fmt,p);
   va_end(p);
   ReleaseSemaphore(&sema);
}
#endif