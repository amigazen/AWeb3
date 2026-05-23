/**********************************************************************
 * 
 * This file is part of the AWeb distribution
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

/* awebsvg.c - AWeb SVG plugin main */

#include "pluginlib.h"
#include "awebsvg.h"
#include <string.h>
#include <libraries/awebplugin.h>
#include <libraries/ttengine.h>

#include <proto/awebplugin.h>
#include <proto/exec.h>
#include <proto/utility.h>

/* Use the library's documented minimum version rather than picking a
 * tighter number locally.  AWeb's own InitTTEngine() opens with the
 * same TTENGINEMINVERSION (see Source/AWebAPL/ttengine.c), so any
 * install on which the HTML renderer can produce TrueType text will
 * also let our SVG plugin draw text.  Requiring v7 here had the
 * symptom of plugin text silently disappearing on installs that ship
 * v6: OpenLibrary returned NULL, TTEngineAvail stayed FALSE, and
 * SvgTextAccumulate became a no-op for every <text>. */
#define SVG_TTENGINE_MIN_VERSION TTENGINEMINVERSION

/* Library base variables */
struct Library *GfxBase;
struct Library *IntuitionBase;
struct Library *UtilityBase;
struct Library *AwebPluginBase;
struct Library *DOSBase;
struct Library *P96Base;
struct Library *TTEngineBase;
BOOL            TTEngineAvail;

ULONG Initpluginlib(struct AwebSvgBase *base)
{  GfxBase=(struct Library *)OpenLibrary("graphics.library",39);
   IntuitionBase=(struct Library *)OpenLibrary("intuition.library",39);
   UtilityBase=(struct Library *)OpenLibrary("utility.library",39);
   AwebPluginBase=(struct Library *)OpenLibrary("awebplugin.library",0);
   DOSBase=(struct Library *)OpenLibrary("dos.library",37);
   P96Base=OpenLibrary("Picasso96.library",0);
   /* OPTIONAL: ttengine.library.  Failure is non-fatal; the renderer
    * skips every <text> element when TTEngineAvail is FALSE. */
   TTEngineBase=OpenLibrary("ttengine.library",SVG_TTENGINE_MIN_VERSION);
   TTEngineAvail=(BOOL)(TTEngineBase!=NULL);
   /* Always-on diagnostic: this prints to AWeb's debug log so the
    * "did ttengine actually load?" question can be answered without
    * a rebuild.  Aprintf is awebplugin.library's logging routine. */
   if(AwebPluginBase)
   {  if(TTEngineBase)
      {  Aprintf("SVG[init]: ttengine.library opened (v%lu.%lu)\n",
            (unsigned long)TTEngineBase->lib_Version,
            (unsigned long)TTEngineBase->lib_Revision);
      }
      else
      {  Aprintf("SVG[init]: ttengine.library NOT available "
            "(OpenLibrary v>=%lu failed); <text> elements will be skipped\n",
            (unsigned long)SVG_TTENGINE_MIN_VERSION);
      }
   }
   (void)base;
   return (ULONG)(GfxBase && IntuitionBase && UtilityBase && AwebPluginBase && DOSBase);
}

void Expungepluginlib(struct AwebSvgBase *base)
{  if(base->sourcedriver) { Amethod(NULL,AOM_INSTALL,base->sourcedriver,NULL); base->sourcedriver=0; }
   if(base->copydriver)   { Amethod(NULL,AOM_INSTALL,base->copydriver,NULL);   base->copydriver=0; }
   if(TTEngineBase)   { CloseLibrary(TTEngineBase);   TTEngineBase=NULL; }
   if(AwebPluginBase) { CloseLibrary(AwebPluginBase); AwebPluginBase=NULL; }
   if(UtilityBase)    { CloseLibrary(UtilityBase);    UtilityBase=NULL; }
   if(IntuitionBase)  { CloseLibrary(IntuitionBase);  IntuitionBase=NULL; }
   if(GfxBase)        { CloseLibrary(GfxBase);        GfxBase=NULL; }
   if(DOSBase)        { CloseLibrary(DOSBase);        DOSBase=NULL; }
   if(P96Base)        { CloseLibrary(P96Base);        P96Base=NULL; }
   TTEngineAvail=FALSE;
}

__asm ULONG Initplugin(register __a0 struct Plugininfo *pi)
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

#define ISSAFE(s,f) (s->structsize>=((long)&s->f-(long)s+sizeof(s->f)))

__asm void Queryplugin(register __a0 struct Pluginquery *pq)
{
   if(ISSAFE(pq,command)) pq->command=FALSE;
}   

__asm void Commandplugin(register __a0 struct Plugincommand *pc)
{  if(pc->structsize<sizeof(struct Plugincommand)) return;
   pc->rc=10;
}

