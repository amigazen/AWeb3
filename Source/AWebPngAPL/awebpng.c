/**********************************************************************
 * 
 * This file is part of the AWeb-II distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
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

/* awebpng.c - AWeb png plugin main */

#include "pluginlib.h"
#include "awebpng.h"
#include <libraries/awebplugin.h>
#include <proto/awebplugin.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/Picasso96.h>
#ifdef DEBUG_PLUGINS
#include <proto/lowlevel.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* PngLog — one Aprintf() per line. When DEBUG_PLUGINS: lowlevel        */
/* library ElapsedTime() (V40) gives +sec.mmm since previous PngLog.    */
/* proto/lowlevel.h: EClockVal, ElapsedTime prototype, libcall pragmas. */
/* ------------------------------------------------------------------ */

struct Library *DosBase;
struct Library *UtilityBase;

#ifdef DEBUG_PLUGINS
/* Storage for extern in proto/lowlevel.h; pragmas use this base. */
struct Library *LowLevelBase;
static struct EClockVal PngElapsedCtx;

static struct SignalSemaphore png_log_sema;
static BOOL png_log_sema_init=FALSE;

void PngLog(const char *facility,const char *fmt,...)
{  char etbuf[24];
   char msg[1024];
   ULONG tid;
   ULONG et;
   ULONG sec;
   ULONG frac;
   ULONG ms;
   struct Task *task;
   long ml;
   va_list ap;

   if(!AwebPluginBase || !DosBase || !fmt)
      return;
   if(!png_log_sema_init)
      return;

   /* Hold the mutex only around shared ElapsedTime context — never across
    * Aprintf(). Decode runs in a subtask; Aprintf/DOS can block until the
    * browser runs, while the browser may wait on decoder progress, which
    * deadlocks if we still hold png_log_sema. */
   ObtainSemaphore(&png_log_sema);
   if(LowLevelBase)
   {  et=ElapsedTime(&PngElapsedCtx);
      sec=(et>>16)&0xFFFFUL;
      frac=et&0xFFFFUL;
      ms=(frac*1000UL)/65536UL;
      sprintf(etbuf,"+%lu.%03lu",(unsigned long)sec,(unsigned long)ms);
   }
   else
   {  strcpy(etbuf,"+?.???");
   }
   ReleaseSemaphore(&png_log_sema);

   task=FindTask(NULL);
   tid=(ULONG)task;

   va_start(ap,fmt);
   vsprintf(msg,fmt,ap);
   va_end(ap);

   ml=(long)strlen(msg);
   if(ml>=(long)sizeof(msg))
      msg[sizeof(msg)-1]='\0';
   while(ml>0 && (msg[ml-1]=='\n' || msg[ml-1]=='\r'))
   {  ml--;
      msg[ml]='\0';
   }

   Aprintf("%s %s[0x%08lX] %s: %s\n",
      etbuf,
      PLUGIN_LIBNAME,
      (unsigned long)tid,
      facility?facility:"png",
      msg);
}
#endif /* DEBUG_PLUGINS */


/* Library bases */
struct Library *P96Base;
struct Library *AwebPluginBase;
struct GfxBase *GfxBase;

ULONG Initpluginlib(struct AwebPngBase *base)
{  GfxBase=(struct GfxBase *)OpenLibrary("graphics.library",39);
   UtilityBase=OpenLibrary("utility.library",39);
   DosBase=OpenLibrary("dos.library",37);
   P96Base=OpenLibrary("Picasso96.library",0);
   AwebPluginBase=OpenLibrary("awebplugin.library",0);
#ifdef DEBUG_PLUGINS
   LowLevelBase=OpenLibrary("lowlevel.library",40);
   if(!png_log_sema_init)
   {  InitSemaphore(&png_log_sema);
      png_log_sema_init=TRUE;
   }
   if(LowLevelBase)
   {  ObtainSemaphore(&png_log_sema);
      PngElapsedCtx.ev_hi=0UL;
      PngElapsedCtx.ev_lo=0UL;
      /* First ElapsedTime() return is undefined; consume it before logging. */
      (void)ElapsedTime(&PngElapsedCtx);
      ReleaseSemaphore(&png_log_sema);
   }
#endif
   return (ULONG)(GfxBase && UtilityBase && DosBase && AwebPluginBase);
}

void Expungepluginlib(struct AwebPngBase *base)
{  if(base->sourcedriver) Amethod(NULL,AOM_INSTALL,base->sourcedriver,NULL);
   if(base->copydriver) Amethod(NULL,AOM_INSTALL,base->copydriver,NULL);
   if(P96Base) CloseLibrary(P96Base);
#ifdef DEBUG_PLUGINS
   if(LowLevelBase) CloseLibrary(LowLevelBase);
#endif
   if(AwebPluginBase) CloseLibrary(AwebPluginBase);
   if(DosBase) CloseLibrary(DosBase);
   if(UtilityBase) CloseLibrary(UtilityBase);
   if(GfxBase) CloseLibrary(GfxBase);
}

__asm __saveds ULONG Initplugin(register __a0 struct Plugininfo *pi)
{  if(!PluginBase->sourcedriver)
   {  PluginBase->sourcedriver=Amethod(NULL,AOM_INSTALL,0,Dispatchsource);
   }
   if(!PluginBase->copydriver)
   {  PluginBase->copydriver=Amethod(NULL,AOM_INSTALL,0,Dispatchcopy);
   }
   pi->sourcedriver=PluginBase->sourcedriver;
   pi->copydriver=PluginBase->copydriver;
   return (ULONG)(PluginBase->sourcedriver && PluginBase->copydriver);
}
