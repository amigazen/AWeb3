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

/* awebjfif.c - AWeb jfif plugin main */

#include "pluginlib.h"
#include "awebjfif.h"
#include <libraries/awebplugin.h>
#include <libraries/Picasso96.h>

#include <proto/awebplugin.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/Picasso96.h>

/* Library base variables */
struct Library *DOSBase;
struct Library *GfxBase;
struct Library *UtilityBase;
struct Library *P96Base;
struct Library *AwebPluginBase;

ULONG Initpluginlib(struct AwebJfifBase *base)
{  DOSBase=OpenLibrary("dos.library",39);
   GfxBase=OpenLibrary("graphics.library",39);
   UtilityBase=OpenLibrary("utility.library",39);
   P96Base=OpenLibrary("Picasso96.library",0);
   AwebPluginBase=OpenLibrary("awebplugin.library",0);
   return (ULONG)(DOSBase && GfxBase && UtilityBase && AwebPluginBase);
}

void Expungepluginlib(struct AwebJfifBase *base)
{  if(base->sourcedriver) Amethod(NULL,AOM_INSTALL,base->sourcedriver,NULL);
   if(base->copydriver) Amethod(NULL,AOM_INSTALL,base->copydriver,NULL);
   if(P96Base) CloseLibrary(P96Base);
   if(AwebPluginBase) CloseLibrary(AwebPluginBase);
   if(UtilityBase) CloseLibrary(UtilityBase);
   if(GfxBase) CloseLibrary(GfxBase);
   if(DOSBase) CloseLibrary(DOSBase);
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
