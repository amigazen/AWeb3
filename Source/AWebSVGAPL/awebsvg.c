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

#include <proto/awebplugin.h>
#include <proto/exec.h>
#include <proto/utility.h>

/* Library base variables */
struct Library *GfxBase;
struct Library *IntuitionBase;
struct Library *UtilityBase;
struct Library *AwebPluginBase;
struct Library *DOSBase;
struct Library *P96Base;

ULONG Initpluginlib(struct AwebSvgBase *base)
{  GfxBase=(struct Library *)OpenLibrary("graphics.library",39);
   IntuitionBase=(struct Library *)OpenLibrary("intuition.library",39);
   UtilityBase=(struct Library *)OpenLibrary("utility.library",39);
   AwebPluginBase=(struct Library *)OpenLibrary("awebplugin.library",0);
   DOSBase=(struct Library *)OpenLibrary("dos.library",37);
   P96Base=OpenLibrary("Picasso96.library",0);
   (void)base;
   return (ULONG)(GfxBase && IntuitionBase && UtilityBase && AwebPluginBase && DOSBase);
}

void Expungepluginlib(struct AwebSvgBase *base)
{  if(base->sourcedriver) { Amethod(NULL,AOM_INSTALL,base->sourcedriver,NULL); base->sourcedriver=0; }
   if(base->copydriver)   { Amethod(NULL,AOM_INSTALL,base->copydriver,NULL);   base->copydriver=0; }
   if(AwebPluginBase) { CloseLibrary(AwebPluginBase); AwebPluginBase=NULL; }
   if(UtilityBase)    { CloseLibrary(UtilityBase);    UtilityBase=NULL; }
   if(IntuitionBase)  { CloseLibrary(IntuitionBase);  IntuitionBase=NULL; }
   if(GfxBase)        { CloseLibrary(GfxBase);        GfxBase=NULL; }
   if(DOSBase)        { CloseLibrary(DOSBase);        DOSBase=NULL; }
   if(P96Base)        { CloseLibrary(P96Base);        P96Base=NULL; }
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

