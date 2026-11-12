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

/* awebgif.c - AWeb gif plugin main */

#include "pluginlib.h"
struct Library *CyberGfxBase;
#include <awebgfx.h>
#include "awebgif.h"
#include <libraries/awebplugin.h>
#include <proto/awebplugin.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
BOOL animate;

/* Library bases */
struct Library *AwebPluginBase;
struct Library *DOSBase;

ULONG Initpluginlib(struct AwebGifBase *base)
{  ULONG result;
   GfxBase=(struct GfxBase *)OpenLibrary("graphics.library",39);
   IntuitionBase=(struct IntuitionBase *)OpenLibrary("intuition.library",39);
   UtilityBase=OpenLibrary("utility.library",39);
   CyberGfxBase=AwebOpenRtgGfx();
   AwebPluginBase=OpenLibrary("awebplugin.library",0);
   /* olsen: I need StrToLong() in the library. */
   DOSBase=OpenLibrary("dos.library",37);
   animate=TRUE;
   result=(ULONG)(GfxBase && IntuitionBase && UtilityBase && AwebPluginBase && DOSBase != NULL);
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Initpluginlib: base=0x%08lx, GfxBase=0x%08lx, IntuitionBase=0x%08lx\n", (ULONG)base, (ULONG)GfxBase, (ULONG)IntuitionBase);
      Aprintf("GIF: Initpluginlib: UtilityBase=0x%08lx, CyberGfxBase=0x%08lx, AwebPluginBase=0x%08lx\n", (ULONG)UtilityBase, (ULONG)CyberGfxBase, (ULONG)AwebPluginBase);
      Aprintf("GIF: Initpluginlib: DOSBase=0x%08lx, result=%ld\n", (ULONG)DOSBase, result);
   }
#endif
   return result;
}

void Expungepluginlib(struct AwebGifBase *base)
{  if(base->sourcedriver) 
   {  Amethod(NULL,AOM_INSTALL,base->sourcedriver,NULL);
   }
   if(base->copydriver) 
   {  Amethod(NULL,AOM_INSTALL,base->copydriver,NULL);
   }
   if(CyberGfxBase) CloseLibrary(CyberGfxBase);
   if(AwebPluginBase) CloseLibrary(AwebPluginBase);
   if(UtilityBase) CloseLibrary(UtilityBase);
   if(IntuitionBase) CloseLibrary(IntuitionBase);
   if(GfxBase) CloseLibrary(GfxBase);
   if(DOSBase != NULL) CloseLibrary(DOSBase);
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Expungepluginlib: Done, base=0x%08lx\n", (ULONG)base);
   }
#endif
}

__asm __saveds ULONG Initplugin(register __a0 struct Plugininfo *pi)
{  ULONG result;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Initplugin called, pi=0x%08lx, PluginBase=0x%08lx\n", (ULONG)pi, (ULONG)PluginBase);
   }
#endif
   if(!PluginBase->sourcedriver)
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
   {  Aprintf("GIF: Initplugin: Installing sourcedriver\n");
      }
      #endif
      PluginBase->sourcedriver=Amethod(NULL,AOM_INSTALL,0,Dispatchsource);
      #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Initplugin: sourcedriver=0x%08lx\n", PluginBase->sourcedriver);
      }
      #endif
   }
   else
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
   {  Aprintf("GIF: Initplugin: sourcedriver already installed=0x%08lx\n", PluginBase->sourcedriver);
      }
      #endif
   }
   if(!PluginBase->copydriver)
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
   {  Aprintf("GIF: Initplugin: Installing copydriver\n");
      }
      #endif
      PluginBase->copydriver=Amethod(NULL,AOM_INSTALL,0,Dispatchcopy);
      #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Initplugin: copydriver=0x%08lx\n", PluginBase->copydriver);
      }
      #endif
   }
   else
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
   {  Aprintf("GIF: Initplugin: copydriver already installed=0x%08lx\n", PluginBase->copydriver);
      }
      #endif
   }
   pi->sourcedriver=PluginBase->sourcedriver;
   pi->copydriver=PluginBase->copydriver;
   result=(ULONG)(PluginBase->sourcedriver && PluginBase->copydriver);
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Initplugin: returning %ld\n", result);
   }
#endif
   return result;
}

#define ISSAFE(s,f) (s->structsize>=((long)&s->f-(long)s+sizeof(s->f)))

__asm __saveds void Queryplugin(register __a0 struct Pluginquery *pq)
{  if(!pq) return;
   if(!AwebPluginBase) return;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Queryplugin called, pq=0x%08lx, structsize=%ld\n", (ULONG)pq, pq->structsize);
   }
#endif
   if(ISSAFE(pq,command)) 
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
   {  Aprintf("GIF: Queryplugin: Setting command=TRUE\n");
      }
      #endif
      pq->command=TRUE;
   }
   else
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
   {  Aprintf("GIF: Queryplugin: pq->command not safe to access\n");
   }
      #endif
   }
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Queryplugin: Done\n");
   }
#endif
}   

__asm __saveds void Commandplugin(register __a0 struct Plugincommand *pc)
{  if(!pc) return;
   if(!AwebPluginBase) return;
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Commandplugin called, pc=0x%08lx\n", (ULONG)pc);
   }
#endif
   if(pc->structsize<sizeof(struct Plugincommand)) 
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
   {  Aprintf("GIF: Commandplugin: structsize too small, returning\n");
      }
      #endif
      return;
   }
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Commandplugin: command='%s'\n", pc->command ? (char *)pc->command : "(null)");
   }
#endif
   /* olsen: use utility.library instead of the compiler runtime library. */
   if(!Stricmp(pc->command,"STARTANIM"))
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
   {  Aprintf("GIF: Commandplugin: STARTANIM\n");
      }
      #endif
      animate=TRUE;
   }
   else if(!Stricmp(pc->command,"STOPANIM"))
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
   {  Aprintf("GIF: Commandplugin: STOPANIM\n");
      }
      #endif
      animate=FALSE;
   }
   else if(!Stricmp(pc->command,"ANIM"))
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
   {  Aprintf("GIF: Commandplugin: ANIM, animate=%ld\n", animate);
      }
      #endif
      pc->result=Dupstr(animate?"1":"0",-1);
      #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
      {  Aprintf("GIF: Commandplugin: result='%s'\n", pc->result ? (char *)pc->result : "(null)");
      }
      #endif
   }
   else
   {  #ifdef DEBUG_PLUGINS
      if(AwebPluginBase)
   {  Aprintf("GIF: Commandplugin: Unknown command, rc=10\n");
      }
      #endif
      pc->rc=10;
   }
#ifdef DEBUG_PLUGINS
   if(AwebPluginBase)
   {  Aprintf("GIF: Commandplugin: Done\n");
   }
#endif
}
