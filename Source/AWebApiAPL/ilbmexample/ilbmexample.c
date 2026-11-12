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

/* ILBMExample.awebplugin
 *
 * Example AWeb plugin module - library code
 *
 * This example implements a simple progressive ILBM decoder.
 * It is slow, uses only standard bitmaps, doesn't support
 * scaling or transparency.
 * It is a fairly good example of how to write an AWeb plugin
 * module, though.
 *
 */

#include "pluginlib.h"
#include "ilbmexample.h"
#include <libraries/awebplugin.h>
#include <clib/awebplugin_protos.h>
#include <clib/exec_protos.h>
#include <pragmas/awebplugin_pragmas.h>
#include <pragmas/exec_sysbase_pragmas.h>

/* Library bases we use */
void *GfxBase,*UtilityBase,*AwebPluginBase;

/* Actions to be performed when our library has been loaded */
ULONG Initpluginlib(struct ILBMExampleBase *base)
{  
   /* Open the libraries we need */
   GfxBase=OpenLibrary("graphics.library",39);
   UtilityBase=OpenLibrary("utility.library",39);
   AwebPluginBase=OpenLibrary("awebplugin.library",0);

   /* Return nonzero if success */
   return (ULONG)(GfxBase && UtilityBase && AwebPluginBase);
}

/* Actions to be performed when our library is about to be expunged */
void Expungepluginlib(struct ILBMExampleBase *base)
{  
   /* If we have installed our dispatchers, deinstall them now. */
   if(base->sourcedriver) Amethod(NULL,AOM_INSTALL,base->sourcedriver,NULL);
   if(base->copydriver) Amethod(NULL,AOM_INSTALL,base->copydriver,NULL);
   
   /* Close all opened libraries */
   if(AwebPluginBase) CloseLibrary(AwebPluginBase);
   if(UtilityBase) CloseLibrary(UtilityBase);
   if(GfxBase) CloseLibrary(GfxBase);
}

/* The first library function */
__asm __saveds ULONG Initplugin(register __a0 struct Plugininfo *pi)
{  
   /* Install our dispatchers */
   if(!PluginBase->sourcedriver)
   {  PluginBase->sourcedriver=Amethod(NULL,AOM_INSTALL,0,Dispatchsource);
   }
   if(!PluginBase->copydriver)
   {  PluginBase->copydriver=Amethod(NULL,AOM_INSTALL,0,Dispatchcopy);
   }
   
   /* Return the class IDs */
   pi->sourcedriver=PluginBase->sourcedriver;
   pi->copydriver=PluginBase->copydriver;
   
   /* Return nonzero if success */
   return (ULONG)(PluginBase->sourcedriver && PluginBase->copydriver);
}

/* Query function. Handle in a safe way. There is generally no need to
 * change this code, it is generated from the #defines in pluginlib.h */

#define ISSAFE(s,f) (s->structsize>=((long)&s->f-(long)s+sizeof(s->f)))

__asm __saveds void Queryplugin(register __a0 struct Pluginquery *pq)
{
#ifdef PLUGIN_COMMANDPLUGIN
   if(ISSAFE(pq,command)) pq->command=TRUE;
#endif
#ifdef PLUGIN_FILTERPLUGIN
   if(ISSAFE(pq,filter)) pq->filter=TRUE;
#endif
}   

