/**********************************************************************
 *
 * This file is part of the AWeb distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
 * Changes Copyright (C) 2026 amigazen project
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

/* awebwebp.c - AWeb WebP plugin main
 *
 * The AWeb WebP plugin replaces the original JFIF (JPEG) plugin
 * design with a libwebp-based decoder that supports both lossy
 * (VP8) and lossless (VP8L) WebP images, including images with
 * an alpha channel.  Decoding is performed incrementally so that
 * partial WebP downloads can be displayed before all data has
 * arrived, matching the streaming model of the other AWeb image
 * plugins (JFIF, PNG, GIF).
 */

#include "pluginlib.h"
struct Library *CyberGfxBase;
#include <awebgfx.h>
#include "awebwebp.h"
#include <libraries/awebplugin.h>
#include <proto/awebplugin.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/graphics.h>
/* Library base variables.  These are declared as struct Library *
 * (or struct GfxBase * for GfxBase) and shared with the source and
 * copy drivers via the extern declarations in awebwebp.h. */
struct Library *DOSBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase;
struct Library *AwebPluginBase;

/* Open all required libraries.  Returns TRUE if the mandatory
 * libraries opened, FALSE otherwise.  Picasso96 is optional - if
 * absent we fall back to standard graphics.library bitmaps. */
ULONG Initpluginlib(struct AwebWebpBase *base)
{  DOSBase=OpenLibrary("dos.library",39);
   GfxBase=(struct GfxBase *)OpenLibrary("graphics.library",39);
   UtilityBase=OpenLibrary("utility.library",39);
   CyberGfxBase=AwebOpenRtgGfx();
   AwebPluginBase=OpenLibrary("awebplugin.library",0);
   return (ULONG)(DOSBase && GfxBase && UtilityBase && AwebPluginBase);
}

void Expungepluginlib(struct AwebWebpBase *base)
{  if(base->sourcedriver) Amethod(NULL,AOM_INSTALL,base->sourcedriver,NULL);
   if(base->copydriver) Amethod(NULL,AOM_INSTALL,base->copydriver,NULL);
   if(CyberGfxBase) CloseLibrary(CyberGfxBase);
   if(AwebPluginBase) CloseLibrary(AwebPluginBase);
   if(UtilityBase) CloseLibrary(UtilityBase);
   if(GfxBase) CloseLibrary((struct Library *)GfxBase);
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
