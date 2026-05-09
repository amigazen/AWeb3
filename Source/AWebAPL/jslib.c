/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
 * Changes Copyright (C) 2025-2026 amigazen project
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

/* jslib.c - AWeb js awebplugin library module */

#include "awebjs.h"
#include "jprotos.h"
#include "keyfile.h"
#include "awebplugin.h"
#include <stdarg.h>
#include <libraries/locale.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <exec/resident.h>
#include <exec/exec.h>
#include <graphics/gfxbase.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <images/label.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/locale.h>
#include <proto/asl.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/label.h>
#include <time.h>

#define PUDDLESIZE         16*1024
#define TRESHSIZE          4*1024

struct ExecBase *SysBase;
/* jdata.c GC traces use this; host aweb.c has its own BOOL httpdebug — they are not wired together yet. */
BOOL httpdebug = TRUE;
struct Locale *locale;
struct Hook idcmphook;

/* Runtime error values */
#define JERRORS_CONTINUE   -1 /* Don't show errors and try to continue script */
#define JERRORS_OFF        0  /* Don't show errors and stop script */
#define JERRORS_ON         1  /* Show errors and stop script */

/*-----------------------------------------------------------------------*/
/* AWebLib module startup */

__asm __saveds struct Library *Initlib(
   register __a6 struct ExecBase *sysbase,
   register __a0 struct SegList *seglist,
   register __d0 struct Library *libbase);

__asm __saveds struct Library *Openlib(
   register __a6 struct Library *libbase);

__asm __saveds struct SegList *Closelib(
   register __a6 struct Library *libbase);

__asm __saveds struct SegList *Expungelib(
   register __a6 struct Library *libbase);

__asm __saveds ULONG Extfunclib(void);

__asm __saveds void *Newjcontext(
   register __a0 UBYTE *screenname);

__asm __saveds BOOL Runjprogram(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *fscope,
   register __a2 UBYTE *source,
   register __a3 struct Jobject *jthis,
   register __a4 struct Jobject **gwtab,
   register __d0 ULONG protkey,
   register __d1 ULONG userdata);

__asm __saveds void *Newjobject(
   register __a0 struct Jcontext *jc);

__asm __saveds void Disposejobject(
   register __a0 struct Jobject *jo);

__asm __saveds struct Jobject *AddjfunctionA(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo,
   register __a2 UBYTE *name,
   register __a3 void (*code)(void *),
   register __a4 UBYTE **args);

__asm __saveds struct Variable *Jfargument(
   register __a0 struct Jcontext *jc,
   register __d0 long n);

__asm __saveds UBYTE *Jtostring(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv);

__asm __saveds void Jasgstring(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv,
   register __a2 UBYTE *string);

__asm __saveds void Jasgobject(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv,
   register __a2 struct Jobject *jo);

__asm __saveds void Setjobject(
   register __a0 struct Jobject *jo,
   register __a1 Objhookfunc *hook,
   register __a2 void *internal,
   register __a3 void (*dispose)(void *));

__asm __saveds struct Variable *Jproperty(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo,
   register __a2 UBYTE *name);

__asm __saveds void Setjproperty(
   register __a0 struct Variable *jv,
   register __a1 Varhookfunc *hook,
   register __a2 void *hookdata);

__asm __saveds struct Jobject *Jthis(
   register __a0 struct Jcontext *jc);

__asm __saveds void *Jointernal(
   register __a0 struct Jobject *jo);

__asm __saveds void Jasgboolean(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv,
   register __d0 BOOL bvalue);

__asm __saveds BOOL Jtoboolean(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv);

__asm __saveds struct Jobject *Newjarray(
   register __a0 struct Jcontext *jc);

__asm __saveds struct Variable *Jnewarrayelt(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo);

__asm __saveds struct Jobject *Jtoobject(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv);

__asm __saveds long Jtonumber(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv);

__asm __saveds void Jasgnumber(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv,
   register __d0 long nvalue);

__asm __saveds BOOL Jisarray(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo);

__asm __saveds struct Jobject *Jfindarray(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo,
   register __a2 UBYTE *name);

__asm __saveds void Jsetprototype(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo,
   register __a2 struct Jobject *proto);

__asm __saveds ULONG Jgetuserdata(
   register __a0 struct Jcontext *jc);

__asm __saveds BOOL Jisnumber(
   register __a0 struct Variable *jv);

__asm __saveds void Clearjobject(
   register __a0 struct Jobject *jo,
   register __a1 UBYTE **except);

__asm __saveds void Freejobject(
   register __a0 struct Jobject *jo);

__asm __saveds void Jdumpobjects(
   register __a0 struct Jcontext *jc);

__asm __saveds struct Variable *Jgetreturnvalue(
   register __a0 struct Jcontext *jc);

__asm __saveds void Jpprotect(
   register __a0 struct Variable *var,
   register __d0 ULONG protkey);

__asm __saveds void Jcprotect(
   register __a0 struct Jcontext *jc,
   register __d0 ULONG protkey);

__asm __saveds UBYTE *Jpname(
   register __a0 struct Variable *var);

__asm __saveds void *Jdisposehook(
   register __a0 struct Jobject *jo);

__asm __saveds void Jsetfeedback(
   register __a0 struct Jcontext *jc,
   register __a1 Jfeedback *jf);

__asm __saveds void Jdebug(
   register __a0 struct Jcontext *jc,
   register __d0 BOOL debugon);

__asm __saveds void Jerrors(
   register __a0 struct Jcontext *jc,
   register __d0 BOOL comperrors,
   register __d1 long runerrors,
   register __d2 BOOL watch);

__asm __saveds void Jkeepobject(
   register __a0 struct Jobject *jo,
   register __d0 BOOL used);

__asm __saveds void Jgarbagecollect(
   register __a0 struct Jcontext *jc);

__asm __saveds struct Variable *Jaddproperty(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo,
   register __a2 UBYTE *name);

__asm __saveds void Jallowgc(
   register __a0 struct Jcontext *jc,
   register __d0 BOOL allow);

__asm __saveds void Jsetjstrace(
   register __a0 struct Jcontext *jc,
   register __d0 BOOL on);

__asm __saveds void Jseterrconsole(
   register __a0 struct Jcontext *jc,
   register __d0 BOOL on);

__asm __saveds void Jsetlinenumber(
   register __a0 struct Jcontext *jc,
   register __d0 long linenr);

__asm __saveds void Jsetobjasfunc(
   register __a0 struct Jobject *jo,
   register __d0 BOOL asfunc);

__asm __saveds void Jsetscreen(
   register __a0 struct Jcontext *jc,
   register __a1 UBYTE *screenname);

__asm __saveds void Jaddeventhandler(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo,
   register __a2 UBYTE *name,
   register __a3 UBYTE *source);

/* Function declarations for project dependent hook functions */
static ULONG Initaweblib(struct Library *libbase);
static void Expungeaweblib(struct Library *libbase);

struct Library *AWebJSBase;

/* Set when awebjs.aweblib is opened; required by awebplugin.h pragmas for Aprintf(). */
struct Library *AwebPluginBase;

static void Jslib_write(UBYTE *s)
{
   BPTR fh;
   LONG n;

   if(!s) return;
   fh=Output();
   if(!fh) return;
   n=(LONG)strlen((char *)s);
   if(n>0) Write(fh,(APTR)s,n);
}

static void Jslib_trace_cnt(const char *tag, struct Library *libbase)
{
   UBYTE buf[128];
   ULONG oc;

   if(!tag) return;
   oc = (libbase ? (ULONG)libbase->lib_OpenCnt : 0UL);
   sprintf((char *)buf,"[JS] awebjs: %s OpenCnt=%lu PluginBase=%08lx\n",
      tag,(unsigned long)oc,(ULONG)AwebPluginBase);
   Jslib_write(buf);
}

static APTR libseglist;

/* Functable slot addresses need plain-C prototypes matching __saveds defs below. */
__saveds void Freejcontext(struct Jcontext *jc);
__saveds void Jgetjmemsessionstats(struct JmemSessionStats *st);

LONG __saveds __asm Libstart(void)
{  return -1;
}

static APTR functable[]=
{  Openlib,
   Closelib,
   Expungelib,
   Extfunclib,
   Newjcontext,
   Freejcontext,
   Runjprogram,
   Newjobject,
   Disposejobject,
   AddjfunctionA,
   Jfargument,
   Jtostring,
   Jasgstring,
   Jasgobject,
   Setjobject,
   Jproperty,
   Jaddproperty,
   Setjproperty,
   Jthis,
   Jointernal,
   Jasgboolean,
   Jtoboolean,
   Newjarray,
   Jnewarrayelt,
   Jtoobject,
   Jtonumber,
   Jasgnumber,
   Jisarray,
   Jfindarray,
   Jsetprototype,
   Jgetuserdata,
   Jisnumber,
   Clearjobject,
   Freejobject,
   Jdumpobjects,
   Jgetreturnvalue,
   Jpprotect,
   Jcprotect,
   Jpname,
   Jdisposehook,
   Jsetfeedback,
   Jdebug,
   Jerrors,
   Jkeepobject,
   Jgarbagecollect,
   Jsetlinenumber,
   Jsetobjasfunc,
   Jsetscreen,
   Jaddeventhandler,
   Jallowgc,
   Jsetjstrace,
   Jseterrconsole,
   Jgetjmemsessionstats,
   (APTR)-1
};

/* Init table used in library initialization. */
static ULONG inittab[]=
{  sizeof(struct Library),
   (ULONG) functable,
   0,
   (ULONG) Initlib
};

static char __aligned libname[]="awebjs.aweblib";
static char __aligned libid[]="awebjs.aweblib " AWEBLIBVSTRING " " __AMIGADATE__;

/* The ROM tag */
struct Resident __aligned romtag=
{  RTC_MATCHWORD,
   &romtag,
   &romtag+1,
   RTF_AUTOINIT,
   AWEBLIBVERSION,
   NT_LIBRARY,
   0,
   libname,
   libid,
   inittab
};

__asm __saveds struct Library *Initlib(
   register __a6 struct ExecBase *sysbase,
   register __a0 struct SegList *seglist,
   register __d0 struct Library *libbase)
{
   SysBase=sysbase;
   AWebJSBase=libbase;
   libbase->lib_Revision=AWEBLIBREVISION;
   libseglist=seglist;
   if(!Initaweblib(libbase))
   {  Expungeaweblib(libbase);
      libbase=NULL;
   }
   return libbase;
}

__asm __saveds struct Library *Openlib(
   register __a6 struct Library *libbase)
{  libbase->lib_OpenCnt++;
   libbase->lib_Flags&=~LIBF_DELEXP;
   Jslib_trace_cnt("Openlib enter",libbase);
   if(libbase->lib_OpenCnt==1)
   {  AwebPluginBase=OpenLibrary("awebplugin.library",0);
      Jslib_trace_cnt("Openlib opened plugin",libbase);
      if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
      {  Aprintf("[JS] Openlib ok\n");
      }
      else if(httpdebug)
      {  UBYTE buf[96];
         sprintf((char *)buf,"[JS] awebjs: Aprintf disabled (AwebPluginBase=%08lx)\n",(ULONG)AwebPluginBase);
         Jslib_write(buf);
      }
   }
   Jslib_trace_cnt("Openlib exit",libbase);
   return libbase;
}

__asm __saveds struct SegList *Closelib(
   register __a6 struct Library *libbase)
{  libbase->lib_OpenCnt--;
   Jslib_trace_cnt("Closelib enter",libbase);
   if(libbase->lib_OpenCnt==0)
   {  if(AwebPluginBase)
      {  CloseLibrary(AwebPluginBase);
         AwebPluginBase=NULL;
      }
      else if(httpdebug)
      {  Jslib_write((UBYTE *)"[JS] awebjs: Closelib with no AwebPluginBase\n");
      }
      if(libbase->lib_Flags&LIBF_DELEXP)
      {  return Expungelib(libbase);
      }
   }
   Jslib_trace_cnt("Closelib exit",libbase);
   return NULL;
}

__asm __saveds struct SegList *Expungelib(
   register __a6 struct Library *libbase)
{  if(libbase->lib_OpenCnt==0)
   {  ULONG size=libbase->lib_NegSize+libbase->lib_PosSize;
      UBYTE *ptr=(UBYTE *)libbase-libbase->lib_NegSize;
      Remove((struct Node *)libbase);
      Jslib_trace_cnt("Expungelib enter",libbase);
      /* Safety: if Expunge happens without Closelib(), ensure awebplugin.library is closed. */
      if(AwebPluginBase)
      {  CloseLibrary(AwebPluginBase);
         AwebPluginBase=NULL;
      }
      Expungeaweblib(libbase);
      FreeMem(ptr,size);
      return libseglist;
   }
   libbase->lib_Flags|=LIBF_DELEXP;
   return NULL;
}

__asm __saveds ULONG Extfunclib(void)
{  return 0;
}

/*-----------------------------------------------------------------------*/

static ULONG Initaweblib(struct Library *libbase)
{  if(!(DOSBase=(struct DosLibrary *)OpenLibrary("dos.library",39)))
   {  return FALSE;
   }
   if(!(LocaleBase=(struct LocaleBase *)OpenLibrary("locale.library",0)))
   {  return FALSE;
   }
   if(!(UtilityBase=OpenLibrary("utility.library",39)))
   {  return FALSE;
   }
   if(!(IntuitionBase=(struct IntuitionBase *)OpenLibrary("intuition.library",39)))
   {  return FALSE;
   }
   if(!(GfxBase=(struct GfxBase *)OpenLibrary("graphics.library",39)))
   {  return FALSE;
   }
   if(!(AslBase=OpenLibrary("asl.library",OSNEED(39,44))))
   {  return FALSE;
   }
   if(!(locale=OpenLocale(NULL)))
   {  return FALSE;
   }
   return TRUE;
}

static void Expungeaweblib(struct Library *libbase)
{  if(locale) CloseLocale(locale);
   if(AslBase) CloseLibrary(AslBase);
   if(GfxBase) CloseLibrary(GfxBase);
   if(IntuitionBase) CloseLibrary(IntuitionBase);
   if(UtilityBase) CloseLibrary(UtilityBase);
   if(LocaleBase) CloseLibrary(LocaleBase);
   if(DOSBase) CloseLibrary(DOSBase);
}

/*-----------------------------------------------------------------------*/

UBYTE *Jdupstr(UBYTE *str,long len,void *pool)
{  UBYTE *dup=NULL;
   if(str)
   {  if(len<0) len=strlen(str);
      if(dup=ALLOCTYPE(UBYTE,len+1,0,pool))
      {  strncpy(dup,str,len);
      }
   }
   return dup;
}

struct Jbuffer *Newjbuffer(void *pool)
{  struct Jbuffer *jb=ALLOCSTRUCT(Jbuffer,1,0,pool);
   if(jb)
   {  jb->pool=pool;
   }
   return jb;
}

void Freejbuffer(struct Jbuffer *jb)
{  if(jb)
   {  if(jb->buffer) FREE(jb->buffer);
      FREE(jb);
   }
}

void Addtojbuffer(struct Jbuffer *jb,UBYTE *text,long length)
{  if(length<0) length=strlen(text);
   if(jb->length+length+1>jb->size)
   {  long newsize=((jb->length+length+1024)/1024)*1024;
      UBYTE *newbuf=ALLOCTYPE(UBYTE,newsize,0,jb->pool);
      if(!newbuf) return;
      if(jb->size)
      {  memmove(newbuf,jb->buffer,jb->size);
         FREE(jb->buffer);
      }
      jb->buffer=newbuf;
      jb->size=newsize;
   }
   if(length)
   {  memmove(jb->buffer+jb->length,text,length);
      jb->length+=length;
   }
}

BOOL Calleractive(void)
{  struct IntuitionBase *ibase=(struct IntuitionBase *)IntuitionBase;
   ULONG lock=LockIBase(0);
   BOOL active=FALSE;
   struct MsgPort *userport;
   if(ibase && ibase->ActiveWindow && (userport=ibase->ActiveWindow->UserPort)
   && userport->mp_SigTask==FindTask(NULL))
   {  active=TRUE;
   }
   UnlockIBase(lock);
   return active;
}

static long __saveds __asm Idcmphook(register __a0 struct Hook *hook,
   register __a1 struct IntuiMessage *msg)
{  struct Jcontext *jc=hook->h_Data;
   if(msg->Class==IDCMP_CHANGEWINDOW)
   {  jc->feedback(jc);
   }
   return 0;
}

/* exec.library RawDoFmt() for "Line ..."; utility.library VSNPrintf() builds requester text.
 * RawDoFmt integer rules: use %ld and 32-bit words in the data stream (see RawDoFmt autodocs).
 * VSNPrintf return = full buffer size needed including NUL (V47+); we only need the written
 * prefix in buf for UI/CLI — do not treat return value as strlen(buf). */
/* Scratch for RawDoFmt PutCh when formatting the line-number prefix. */
struct Jerrcon_lnrfmt
{
   STRPTR dst;
   ULONG room;
};

/* RawDoFmt PutCh: character low 8 bits in D0, userdata in A3; trailing call passes 0 (autodocs).
 * Preserve D2-D7/A2/A4-A6; SAS/C __asm stub follows Amiga register conventions. */
static void __saveds __asm Jerrcon_lnrfmt_putch(register __d0 ULONG c,
   register __a3 struct Jerrcon_lnrfmt *lf)
{
   UBYTE b;

   if(!lf || lf->room==0)
   {
      return;
   }
   b=(UBYTE)(c&0xff);
   *lf->dst++=b;
   *lf->dst='\0';
   lf->room--;
}

static void Jerrcon_writecstr(BPTR fh, UBYTE *s)
{
   LONG n;

   if(!fh || !s)
   {
      return;
   }
   n=(LONG)strlen((char *)s);
   if(n>0)
   {
      Write(fh,(APTR)s,n);
   }
}

/* lnr<0 gives general requester. pos<0 gives runtime, pos>=0 gives parsing requester.
 * Returns TRUE when to ignore, FALSE when to stop.
 * But: lnr<0 && pos>0 gives loop warning requester;
 * returns TRUE when to stop, FALSE when to continue. */
BOOL Errorrequester(struct Jcontext *jc,long lnr,UBYTE *line,long pos,UBYTE *msg,va_list args)
{  struct ClassLibrary *WindowBase=NULL,*LayoutBase=NULL,*ButtonBase=NULL,*LabelBase=NULL;
   struct Jerrcon_lnrfmt lfmt;
   BOOL ignore=FALSE;
   void *winobj,*buttonrow;
   ULONG sigmask,result;
   UBYTE lnrbuf[32],src[128],buf[128];
   UBYTE *p,*q;
   long len;
   BOOL done=FALSE;
   struct TextAttr ta={ 0 };
   struct Screen *screen;
   BPTR errfh;

   if(!jc)
   {
      return FALSE;
   }
   if(lnr>=0)
   {  lfmt.dst=lnrbuf;
      lfmt.room=(ULONG)(sizeof(lnrbuf)-1);
      lnrbuf[0]='\0';
      RawDoFmt((CONST_STRPTR)"Line %ld\n",(APTR)&lnr,(VOID (*)())Jerrcon_lnrfmt_putch,(APTR)&lfmt);
      if(pos>=0)
      {  if(line)
         {  for(p=line+pos-1;p>=line && *p!='\n' && *p!='\r';p--);
            pos-=(p+1-line);
            line=p+1;
            for(p=line;*p && *p!='\n' && *p!='\r';p++);
            len=p-line;
            if(pos<=30)
            {  q=line;
               if(len>60) len=60;
            }
            else
            {  if(len>60)
               {  if(len>pos+30)
                  {  q=line+pos-30;
                     pos=30;
                     len=60;
                  }
                  else
                  {  q=line+len-60;
                     pos-=len-60;
                     len=60;
                  }
               }
               else
               {  q=line;
               }
            }
            strncpy(src,q,len);
            p=src+len;
         }
         else
         {  p=src;
         }
         *p++='\n';
         for(;pos;pos--) *p++=' ';
         *p++='^';
         *p++='\0';
      }
      else if(line)
      {  strncpy(src,line,60);
      }
      else
      {  *src='\0';
      }
   }
   else
   {  *lnrbuf='\0';
      *src='\0';
   }
   /* fmt + args: va_list as RawDoFmt data stream (utility VSNPrintf autodocs). */
   VSNPrintf((STRPTR)buf,(ULONG)sizeof(buf),(CONST_STRPTR)msg,(CONST_APTR)args);
   if(jc->errconsole)
   {  errfh=Output();
      if(!errfh)
      {
         if(lnr<0 && pos>0)
         {
            return TRUE;
         }
         return FALSE;
      }
      if(lnr<0 && pos>0)
      {
         Jerrcon_writecstr(errfh,(UBYTE *)"JavaScript warning\n");
      }
      else
      {
         Jerrcon_writecstr(errfh,(UBYTE *)"JavaScript error\n");
      }
      if(lnr>=0 && lnrbuf[0])
      {
         Jerrcon_writecstr(errfh,lnrbuf);
      }
      if(src[0])
      {
         Jerrcon_writecstr(errfh,src);
         Jerrcon_writecstr(errfh,(UBYTE *)"\n");
      }
      Jerrcon_writecstr(errfh,buf);
      Jerrcon_writecstr(errfh,(UBYTE *)"\n");
      if(lnr<0 && pos>0)
      {
         return TRUE;
      }
      return FALSE;
   }
   ta.ta_Name=((struct GfxBase *)GfxBase)->DefaultFont->ln_Name;
   ta.ta_YSize=((struct GfxBase *)GfxBase)->DefaultFont->tf_YSize;
   if(!(screen=LockPubScreen(jc->screenname)))
   {  screen=LockPubScreen(NULL);
   }
   if(screen
   && (WindowBase=(struct ClassLibrary *)OpenLibrary("window.class",OSNEED(0,44)))
   && (LayoutBase=(struct ClassLibrary *)OpenLibrary("gadgets/layout.gadget",OSNEED(0,44)))
   && (ButtonBase=(struct ClassLibrary *)OpenLibrary("gadgets/button.gadget",OSNEED(0,44)))
   && (LabelBase=(struct ClassLibrary *)OpenLibrary("images/label.image",OSNEED(0,44))))
   {  winobj=WindowObject,
         WA_Title,(lnr<0 && pos>0)?"JavaScript warning":"JavaScript error",
         WA_DepthGadget,TRUE,
         WA_DragBar,TRUE,
         WA_Activate,Calleractive(),
         WA_AutoAdjust,TRUE,
         WA_SimpleRefresh,TRUE,
         WA_PubScreen,screen,
         WINDOW_Position,WPOS_CENTERSCREEN,
         WINDOW_IDCMPHook,&idcmphook,
         WINDOW_IDCMPHookBits,IDCMP_CHANGEWINDOW,
         WINDOW_Layout,VLayoutObject,
            LAYOUT_DeferLayout,TRUE,
            StartMember,VLayoutObject,
               LAYOUT_SpaceOuter,TRUE,
               StartImage,LabelObject,
                  LABEL_Text,lnrbuf,
               EndImage,
               StartImage,LabelObject,
                  IA_Font,&ta,
                  LABEL_Text,src,
                  LABEL_Underscore,0,
               EndImage,
               StartImage,LabelObject,
                  LABEL_Text,buf,
                  LABEL_Underscore,0,
               EndImage,
            EndMember,
            StartMember,buttonrow=HLayoutObject,
               LAYOUT_BevelStyle,BVS_SBAR_VERT,
               LAYOUT_SpaceOuter,TRUE,
               LAYOUT_EvenSize,TRUE,
               StartMember,ButtonObject,
                  GA_ID,1,
                  GA_RelVerify,TRUE,
                  GA_Text,"_Ok",
               EndMember,
               CHILD_WeightedWidth,0,
               CHILD_NominalSize,TRUE,
            EndMember,
         End,
      EndWindow;
      if(winobj && lnr>=0 && !(jc->dflags&DEBF_DOPEN))
      {  SetAttrs(buttonrow,
            StartMember,ButtonObject,
               GA_ID,2,
               GA_RelVerify,TRUE,
               GA_Text,pos<0?"_Debug":"Ignore _All",
            EndMember,
            CHILD_WeightedWidth,0,
            CHILD_NominalSize,TRUE,
            TAG_END);
      }
      if(winobj && lnr<0 && pos>0)
      {  SetAttrs(buttonrow,
            StartMember,ButtonObject,
               GA_ID,2,
               GA_RelVerify,TRUE,
               GA_Text,"_Stop",
            EndMember,
            CHILD_WeightedWidth,0,
            CHILD_NominalSize,TRUE,
            TAG_END);
      }
      if(winobj && RA_OpenWindow(winobj))
      {  GetAttr(WINDOW_SigMask,winobj,&sigmask);
         while(!done)
         {  Wait(sigmask);
            while(!done && (result=RA_HandleInput(winobj,NULL))!=WMHI_LASTMSG)
            {  switch(result&WMHI_CLASSMASK)
               {  case WMHI_GADGETUP:
                     if(result&WMHI_GADGETMASK)
                     {  done=TRUE;
                        ignore=((result&WMHI_GADGETMASK)==2);
                     }
                     break;
               }
            }
         }
      }
      if(winobj) DisposeObject(winobj);
      /* Allow window refreshing after requester closes */
      jc->feedback(jc);
   }
   if(screen) UnlockPubScreen(NULL,screen);
   if(LabelBase) CloseLibrary((struct Library *)LabelBase);
   if(ButtonBase) CloseLibrary((struct Library *)ButtonBase);
   if(LayoutBase) CloseLibrary((struct Library *)LayoutBase);
   if(WindowBase) CloseLibrary((struct Library *)WindowBase);
   return ignore;
}

BOOL Feedback(struct Jcontext *jc)
{  static long counter=0;
   unsigned int clock[2]={ 0,0 };
   BOOL run=TRUE;
   if(jc->flags&EXF_STOP) run=FALSE;
   /* Don't get the time etc with _every_ call to avoid excessive overhead */
   if(run && ++counter>20)
   {  counter=0;
      timer(clock);
      if(run && jc->warntime && clock[0]>jc->warntime)
      {  run=!Errorrequester(jc,-1,NULL,1,"JavaScript runs for one minute",NULL);
         if(run)
         {  timer(clock);
            jc->warntime=clock[0]+60;
         }
      }
      if(run && jc->warnmem && AvailMem(0)<jc->warnmem)
      {  run=!Errorrequester(jc,-1,NULL,1,"JavaScript seems to be eating up all memory",NULL);
         if(run)
         {  jc->warnmem=AvailMem(0)/4;
         }
      }
      if(run && jc->feedback && clock[0]>jc->fbtime)
      {  run=jc->feedback(jc);
         jc->fbtime=clock[0]+1;
      }
   }
   return run;
}

/*-----------------------------------------------------------------------*/

__asm __saveds void *Newjcontext(register __a0 UBYTE *screenname)
{  struct Jcontext *jc=NULL;
   void *pool;
   /* New Jcontext is created in its own pool */
   if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
   {  Aprintf("[JS] Newjcontext enter\n");
   }
   if(pool=CreatePool(MEMF_PUBLIC|MEMF_CLEAR,PUDDLESIZE,TRESHSIZE))
   {  if(jc=ALLOCSTRUCT(Jcontext,1,0,pool))
      {  jc->pool=pool;
         /* Objects and variables must use this pool: Pallocmem(NULL,...) uses AllocMem
          * and those blocks are not reclaimed by DeletePool(jc->pool). */
         jc->objpool=pool;
         jc->varpool=pool;
         NEWLIST(&jc->objects);
         NEWLIST(&jc->tmp);
         JmemSessionBind(jc->pool);
         if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
         {  Aprintf("[JS] Newjcontext before Newexecute\n");
         }
         Newexecute(jc);
         if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
         {  Aprintf("[JS] Newjcontext after Newexecute\n");
         }
         jc->screenname=screenname;
      }
   }
   if(!jc)
   {  if(pool) DeletePool(pool);
   }
   if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
   {  Aprintf("[JS] Newjcontext exit\n");
   }
   return jc;
}

__saveds void Freejcontext(struct Jcontext *jc)
{
   if(jc && jc->pool)
   {
      Freeexecute(jc);
      JmemPrintTeardown(jc);
      JmemSessionUnbind();
      DeletePool(jc->pool);
   }
}

__saveds void Jgetjmemsessionstats(struct JmemSessionStats *st)
{
   if(!st)
   {
      return;
   }
   st->total_alloc_bytes=0;
   st->total_free_bytes=0;
   st->peak_outstanding_bytes=0;
   JmemSessionGetStatsEx(
      &st->jp_allocs,&st->jp_frees,&st->outstanding_bytes,
      &st->total_alloc_bytes,&st->total_free_bytes,&st->peak_outstanding_bytes);
}

__asm __saveds BOOL Runjprogram(register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *fscope,
   register __a2 UBYTE *source,
   register __a3 struct Jobject *jthis,
   register __a4 struct Jobject **gwtab,
   register __d0 ULONG protkey,
   register __d1 ULONG userdata)
{  BOOL result=TRUE;
   ULONG olduserdata,oldprotkey,olddflags;
   long oldwarnmem;
   struct Value val={0};
   LIST(Jobject) temps;
   struct Jobject *jo,*jn;
   unsigned int clock[2]={ 0,0 };
   if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
   {  Aprintf("[JS] Runjprogram enter\n");
   }
   if(jc && source)
   {  idcmphook.h_Entry=(HOOKFUNC)Idcmphook;
      idcmphook.h_Data=jc;
      jc->flags&=~(JCF_ERROR|JCF_IGNORE|EXF_STOP);
      jc->generation++;
      jc->fscope=fscope;
      /* Ensure built-in objects are in the fscope - add them if not already present */
      if(fscope)
      {  if(!Getproperty(fscope,"Math")) Initmath(jc,fscope);
         if(!Getproperty(fscope,"Array")) Initarray(jc,fscope);
         if(!Getproperty(fscope,"Date")) Initdate(jc,fscope);
         if(!Getproperty(fscope,"Object")) Initobject(jc,fscope);
         if(!Getproperty(fscope,"Boolean")) Initboolean(jc,fscope);
         if(!Getproperty(fscope,"Function")) Initfunction(jc,fscope);
         if(!Getproperty(fscope,"Number")) Initnumber(jc,fscope);
         if(!Getproperty(fscope,"String")) Initstring(jc,fscope);
         if(!Getproperty(fscope,"RegExp")) Initregexp(jc,fscope);
         if(!Getproperty(fscope,"Error")) Initerror(jc,fscope);
         /* Copy global functions from jc->functions.first->local to fscope */
         if(jc->functions.first)
         {  struct Variable *var,*fvar;
            for(var=jc->functions.first->local.first;var && var->next;var=var->next)
            {  if(var->name && var->val.type==VTP_OBJECT && var->val.value.obj.ovalue
                  && var->val.value.obj.ovalue->function
                  && !Getproperty(fscope,var->name))
               {  if(fvar=Addproperty(fscope,var->name))
                  {  Asgobject(&fvar->val,var->val.value.obj.ovalue);
                     fvar->flags|=VARF_DONTDELETE;
                  }
               }
            }
         }
      }
      jc->warntime=0;
      jc->warnmem=0;
      if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
      {  Aprintf("[JS] Runjprogram before Jcompile\n");
      }
      Jcompile(jc,source);
      if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
      {  Aprintf("[JS] Runjprogram after Jcompile\n");
      }
      jc->linenr=0;
      if(!(jc->flags&JCF_ERROR))
      {  /* Remember existing temporary objects (see comment in jexe.c:Exedot()) */
         NEWLIST(&temps);
         for(jo=jc->objects.first;jo->next;jo=jn)
         {  jn=jo->next;
            if(jo->flags&OBJF_TEMP)
            {  REMOVE(jo);
               ADDTAIL(&temps,jo);
            }
         }
         olduserdata=jc->userdata;
         jc->userdata=userdata;
         oldprotkey=jc->protkey;
         jc->protkey=protkey;
         olddflags=jc->dflags;
         jc->dflags&=~DEBF_DOPEN;
         oldwarnmem=jc->warnmem;
         if((jc->dflags&DEBF_DEBUG) && (jc->dflags&DEBF_DBREAK))
         {  Startdebugger(jc);
         }
         if(jc->flags&EXF_WARNINGS)
         {  timer(clock);
            jc->warntime=clock[0]+60;
            jc->warnmem=AvailMem(0)/4;
         }
         if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
         {  Aprintf("[JS] Runjprogram before Jexecute\n");
         }
         Jexecute(jc,jthis,gwtab);
         if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
         {  Aprintf("[JS] Runjprogram after Jexecute\n");
         }
         if(jc->dflags&DEBF_DOPEN)
         {  Stopdebugger(jc);
         }
         jc->dflags=olddflags;
         Asgvalue(&val,jc->val);
         if(val.type==VTP_UNDEFINED) result=TRUE;
         else if(val.type==VTP_OBJECT && !val.value.obj.ovalue) result=TRUE;
         else
         {  Toboolean(&val,jc);
            result=val.value.bvalue;
         }
         Clearvalue(&val);
         jc->warnmem=oldwarnmem;
         jc->userdata=olduserdata;
         jc->protkey=oldprotkey;
         while(jo=REMHEAD(&temps))
         {  ADDTAIL(&jc->objects,jo);
         }
      }
      else
      {  /* Compile failed: partial AST and any nodes would otherwise stay in jc->program
          * and the next Runjprogram would append to a broken tree, leaking compile memory. */
         if(jc->program)
         {  Jdispose((struct Element *)jc->program);
            jc->program=NULL;
         }
      }
   }
   if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
   {  Aprintf("[JS] Runjprogram exit\n");
   }
   return result;
}

void __stdargs _XCEXIT(long x)
{
}

__asm __saveds void *Newjobject(
   register __a0 struct Jcontext *jc)
{  struct Jobject *jo;
   if(jo=Newobject(jc))
   {  Initconstruct(jc,jo,NULL,NULL);
   }
   return jo;
}

__asm __saveds void Disposejobject(
   register __a0 struct Jobject *jo)
{  struct Variable *var;
   /* In order to cope with reference loops, all references are cleared before
    * the object is unused.
    * Also, all possible references to external objects are cleared. */
   if(jo)
   {  for(var=jo->properties.first;var->next;var=var->next)
      {  if(var->val.type==VTP_OBJECT)
         {  Clearvalue(&var->val);
         }
         var->hookdata=NULL;
      }
      jo->internal=NULL;
      jo->dispose=NULL;
      jo->keepnr=0;
   }
}

__asm __saveds struct Jobject *AddjfunctionA(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo,
   register __a2 UBYTE *name,
   register __a3 void (*code)(void *),
   register __a4 UBYTE **args)
{  struct Jobject *f=NULL;
   struct Variable *prop;
   if(name)
   {  if((prop=Getownproperty(jo,name))
      || (prop=Addproperty(jo,name)))
      {  if(f=InternalfunctionA(jc,name,code,args))
         {  if(f->function)
            {  f->function->fscope=jo;
            }
            Asgfunction(&prop->val,f,jo);
         }
      }
   }
   return f;
}

struct Jobject *Addjfunction(struct Jcontext *jc,struct Jobject *jo,UBYTE *name,
   void (*code)(void *),...)
{  UBYTE **args;
   struct Jobject *result;
   args=(UBYTE **)VARARG(code);
   result=AddjfunctionA(jc,jo,name,code,args);
   return result;
}

__asm __saveds struct Variable *Jfargument(
   register __a0 struct Jcontext *jc,
   register __d0 long n)
{  struct Jobject *jo;
   struct Variable *var;
   jo=jc->functions.first->arguments;
   var=Arrayelt(jo,n);
   return var;
}

__asm __saveds UBYTE *Jtostring(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv)
{  if(!jc || !jv)
   {  return NULL;
   }
   Tostring(&jv->val,jc);
   return jv->val.value.svalue;
}

__asm __saveds void Jasgstring(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv,
   register __a2 UBYTE *string)
{  struct Value *val;
   if(jv) val=&jv->val;
   else val=&jc->functions.first->retval;
   Asgstring(val,string?string:(UBYTE *)"",jc->pool);
}

__asm __saveds void Jasgobject(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv,
   register __a2 struct Jobject *jo)
{  struct Value *val;
   if(jv) val=&jv->val;
   else val=&jc->functions.first->retval;
   Asgobject(val,jo);
}

__asm __saveds void Setjobject(
   register __a0 struct Jobject *jo,
   register __a1 Objhookfunc *hook,
   register __a2 void *internal,
   register __a3 void (*dispose)(void *))
{  jo->hook=hook;
   jo->internal=internal;
   jo->dispose=dispose;
}

__asm __saveds struct Variable *Jproperty(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo,
   register __a2 UBYTE *name)
{  struct Variable *var=NULL;
   if(!name) name="";
   if(!(var=Getownproperty(jo,name)))
   {  var=Addproperty(jo,name);
   }
   return (struct Variable *)var;
}

__asm __saveds void Setjproperty(
   register __a0 struct Variable *jv,
   register __a1 Varhookfunc *hook,
   register __a2 void *hookdata)
{  if(hook==(Varhookfunc *)-1)
   {  jv->hook=Constantvhook;
   }
   else if(hook==(Varhookfunc *)-2)
   {  jv->flags|=VARF_SYNONYM;
      jv->hookdata=hookdata;
   }
   else
   {  jv->hook=hook;
      jv->hookdata=hookdata;
   }
}

__asm __saveds struct Jobject *Jthis(
   register __a0 struct Jcontext *jc)
{  return jc?jc->jthis:NULL;
}

__asm __saveds void *Jointernal(
   register __a0 struct Jobject *jo)
{  return jo?jo->internal:NULL;
}

__asm __saveds void Jasgboolean(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv,
   register __d0 BOOL bvalue)
{  struct Value *val;
   if(jv) val=&jv->val;
   else val=&jc->functions.first->retval;
   Asgboolean(val,bvalue);
}

__asm __saveds BOOL Jtoboolean(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv)
{  Toboolean(&jv->val,jc);
   return jv->val.value.bvalue;
}

__asm __saveds struct Jobject *Newjarray(
   register __a0 struct Jcontext *jc)
{  return Newarray(jc);
}

__asm __saveds struct Variable *Jnewarrayelt(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo)
{  return (struct Variable *)Addarrayelt(jc,jo);
}

__asm __saveds struct Jobject *Jtoobject(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv)
{  Toobject(&jv->val,jc);
   return jv->val.value.obj.ovalue;
}

__asm __saveds long Jtonumber(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv)
{  long n;
   Tonumber(&jv->val,jc);
   if(jv->val.attr==VNA_VALID) n=(long)jv->val.value.nvalue;
   else n=0;
   return n;
}

__asm __saveds void Jasgnumber(
   register __a0 struct Jcontext *jc,
   register __a1 struct Variable *jv,
   register __d0 long nvalue)
{  struct Value *val;
   if(jv) val=&jv->val;
   else val=&jc->functions.first->retval;
   Asgnumber(val,VNA_VALID,(double)nvalue);
}

__asm __saveds BOOL Jisarray(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo)
{  return Isarray(jo);
}

__asm __saveds struct Jobject *Jfindarray(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo,
   register __a2 UBYTE *name)
{  struct Variable *var;
   struct Jobject *ja=NULL;
   if(var=Getownproperty(jo,name))
   {  if(var->val.type==VTP_OBJECT && var->val.value.obj.ovalue && Isarray(var->val.value.obj.ovalue))
      {  ja=var->val.value.obj.ovalue;
      }
   }
   return ja;
}

__asm __saveds void Jsetprototype(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo,
   register __a2 struct Jobject *proto)
{  struct Variable *var;
   if((var=Getownproperty(jo,"prototype"))
   || (var=Addproperty(jo,"prototype")))
   {  if(!proto->constructor) proto->constructor=jo;
      proto->hook=Prototypeohook;
      Asgobject(&var->val,proto);
   }
}

__asm __saveds ULONG Jgetuserdata(
   register __a0 struct Jcontext *jc)
{  if(jc)
   {  return jc->userdata;
   }
   else
   {  return NULL;
   }
}

__asm __saveds BOOL Jisnumber(
   register __a0 struct Variable *jv)
{  return (BOOL)(jv && jv->val.type==VTP_NUMBER && jv->val.attr==VNA_VALID);
}

__asm __saveds void Clearjobject(
   register __a0 struct Jobject *jo,
   register __a1 UBYTE **except)
{  if(jo)
   {  Clearobject(jo,except);
   }
}

__asm __saveds void Freejobject(
   register __a0 struct Jobject *jo)
{
   /* Newjobject() creates a full Jobject via Newobject(), which links it into
    * jc->objects. Callers (e.g. prefs.c) use Freejobject() to discard temporary
    * objects after copying their properties into a longer-lived object.
    *
    * Never call Freejobject(jo) on a jo that is still referenced from any Value
    * (e.g. immediately after Jasgobject/Jsetprototype stored jo in a property):
    * that use-after-free corrupts the pool and the next Disposeobject/FREE can hang. */
   if(jo)
   {  /* NODE(Jobject) comes from ezlists.h (next/prev), not Exec's struct Node.
       * Use the matching list macros to unlink. */
      if(jo->next && jo->prev)
      {  REMOVE(jo);
      }
      Disposeobject(jo);
   }
}

__asm __saveds void Jdumpobjects(
   register __a0 struct Jcontext *jc)
{  if(jc)
   {  Dumpobjects(jc);
   }
}

__asm __saveds struct Variable *Jgetreturnvalue(
   register __a0 struct Jcontext *jc)
{  struct Variable *var;
   if(jc && jc->result->val.type) var=jc->result;
   else var=NULL;
   return var;
}

__asm __saveds void Jpprotect(
   register __a0 struct Variable *var,
   register __d0 ULONG protkey)
{  if(var)
   {  var->protkey=protkey;
   }
}

__asm __saveds void Jcprotect(
   register __a0 struct Jcontext *jc,
   register __d0 ULONG protkey)
{  if(jc)
   {  jc->protkey=protkey;
   }
}

__asm __saveds UBYTE *Jpname(
   register __a0 struct Variable *var)
{  if(var) return var->name;
   else return NULL;
}

__asm __saveds void *Jdisposehook(
   register __a0 struct Jobject *jo)
{  void *hook=NULL;
   if(jo) hook=(void *)jo->dispose;
   return hook;
}

__asm __saveds void Jsetfeedback(
   register __a0 struct Jcontext *jc,
   register __a1 Jfeedback *jf)
{  if(jc)
   {  jc->feedback=jf;
      if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
      {  Aprintf("[JS] Jsetfeedback\n");
      }
   }
}

__asm __saveds void Jdebug(
   register __a0 struct Jcontext *jc,
   register __d0 BOOL debugon)
{  if(jc)
   {  if(debugon)
      {  jc->dflags|=DEBF_DEBUG|DEBF_DBREAK;
      }
      else
      {  jc->dflags&=~DEBF_DEBUG|DEBF_DBREAK;
      }
      if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
      {  Aprintf("[JS] Jdebug\n");
      }
   }
}

__asm __saveds void Jerrors(
   register __a0 struct Jcontext *jc,
   register __d0 BOOL comperrors,
   register __d1 long runerrors,
   register __d2 BOOL watch)
{  if(jc)
   {  if(comperrors)
      {  jc->flags|=JCF_ERRORS;
      }
      else
      {  jc->flags&=~JCF_ERRORS;
      }
      jc->flags&=~(EXF_ERRORS|EXF_DONTSTOP);
      switch(runerrors)
      {  case JERRORS_CONTINUE:
            jc->flags|=EXF_DONTSTOP;
            break;
         case JERRORS_OFF:
            break;
         default:
            jc->flags|=EXF_ERRORS;
            break;
      }
      if(watch)
      {  jc->flags|=EXF_WARNINGS;
      }
      else
      {  jc->flags&=~EXF_WARNINGS;
      }
      if(AwebPluginBase && SysBase && !SysBase->TDNestCnt && !SysBase->IDNestCnt)
      {  Aprintf("[JS] Jerrors\n");
      }
   }
}

__asm __saveds void Jkeepobject(
   register __a0 struct Jobject *jo,
   register __d0 BOOL used)
{  if(jo) Keepobject(jo,used);
}

__asm __saveds void Jgarbagecollect(
   register __a0 struct Jcontext *jc)
{
   BPTR fh;
   static const char entermsg[] = "[JS] Jgarbagecollect enter\n";
   static const char exitmsg[]  = "[JS] Jgarbagecollect exit\n";

   /* Use DOS Output() breadcrumbs here (not Aprintf):
    * this runs exactly at the hang site and must not depend on awebplugin.library. */
   fh=Output();
   if(fh)
   {
      Write(fh,(APTR)entermsg,(LONG)sizeof(entermsg)-1);
   }
   if(jc)
   {
      Garbagecollect(jc);
   }
   fh=Output();
   if(fh)
   {
      Write(fh,(APTR)exitmsg,(LONG)sizeof(exitmsg)-1);
   }
}

__asm __saveds struct Variable *Jaddproperty(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo,
   register __a2 UBYTE *name)
{  struct Variable *var=NULL;
   if(!name) name="";
   if(jc && jo && !Getownproperty(jo,name))
   {  var=Addproperty(jo,name);
   }
   return var;
}

__asm __saveds void Jallowgc(
   register __a0 struct Jcontext *jc,
   register __d0 BOOL allow)
{  if(jc)
   {  if(allow)
      {  jc->nogc--;
      }
      else
      {  jc->nogc++;
      }
   }
}

__asm __saveds void Jsetjstrace(
   register __a0 struct Jcontext *jc,
   register __d0 BOOL on)
{  if(jc)
   {  jc->jstrace=on;
   }
}

__asm __saveds void Jseterrconsole(
   register __a0 struct Jcontext *jc,
   register __d0 BOOL on)
{  if(jc)
   {  jc->errconsole=on;
   }
}

__asm __saveds void Jsetlinenumber(
   register __a0 struct Jcontext *jc,
   register __d0 long linenr)
{  if(jc) jc->linenr=linenr;
}

__asm __saveds void Jsetobjasfunc(
   register __a0 struct Jobject *jo,
   register __d0 BOOL asfunc)
{  if(jo)
   {  if(asfunc)
      {  jo->flags|=OBJF_ASFUNCTION;
      }
      else
      {  jo->flags&=~OBJF_ASFUNCTION;
      }
   }
}

__asm __saveds void Jsetscreen(
   register __a0 struct Jcontext *jc,
   register __a1 UBYTE *screenname)
{  if(jc)
   {  jc->screenname=screenname;
   }
}

__asm __saveds void Jaddeventhandler(
   register __a0 struct Jcontext *jc,
   register __a1 struct Jobject *jo,
   register __a2 UBYTE *name,
   register __a3 UBYTE *source)
{  struct Variable *var;
   struct Jobject *jeventh;
   /* Older logic skipped entirely when an own property already existed. A first pass
    * with source==NULL installs an own slot (e.g. onfocus=null); a later pass with the
    * real attribute string must replace it, or DOM0 wrappers never see a callable handler. */
   if(jc && jo && name)
   {  if(!(var=Getownproperty(jo,name)))
      {  if(!(var=Addproperty(jo,name)))
         {  return;
         }
      }
      if(source)
      {  jc->generation++;
         if(jeventh=Jcompiletofunction(jc,source,name))
         {  Asgfunction(&var->val,jeventh,jo);
         }
         else
         {  Asgstring(&var->val,source,jc->pool);
         }
      }
      else
      {  Asgobject(&var->val,NULL);
      }
   }
}
