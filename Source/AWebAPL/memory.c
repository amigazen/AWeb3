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

/* memory.c AWeb memory manager */

#include "aweb.h"

#include <proto/exec.h>

static struct SignalSemaphore memsema;

void *fastpool,*chippool;

#ifdef BETAKEYFILE
extern BOOL nopool;
#endif

/*-----------------------------------------------------------------------*/

BOOL Initmemory(void)
{  InitSemaphore(&memsema);
   if(!(fastpool=CreatePool(MEMF_PUBLIC|MEMF_CLEAR,PUDDLESIZE,TRESHSIZE)))
      return FALSE;
   if(!(chippool=CreatePool(MEMF_CHIP|MEMF_PUBLIC|MEMF_CLEAR,PUDDLESIZE,TRESHSIZE)))
      return FALSE;
   return TRUE;
}

void Freememory(void)
{  if(chippool) DeletePool(chippool);
   if(fastpool) DeletePool(fastpool);
}

void *Pallocmem(long size,ULONG flags,void *pool)
{  void *mem;
#ifdef BETAKEYFILE
   if(nopool) pool=NULL;
#endif
   ObtainSemaphore(&memsema);
   if(pool) mem=AllocPooled(pool,size+8);
   else mem=AllocMem(size+8,flags|MEMF_PUBLIC|MEMF_CLEAR);
   ReleaseSemaphore(&memsema);
   if(mem)
   {  *(void **)mem=pool;
      *(long *)((ULONG)mem+4)=size+8;
      return (void *)((ULONG)mem+8);
   }
   else return NULL;
}

void *Allocmem(long size,ULONG flags)
{  void *pool,*mem;
   if(flags&MEMF_CHIP) pool=chippool;
   else pool=fastpool;
   mem=Pallocmem(size,flags,pool);
   return mem;
}

void Freemem(void *mem)
{  void *pool;
   long size;
   if(mem)
   {  pool=*(void **)((ULONG)mem-8);
      size=*(long *)((ULONG)mem-4);
      ObtainSemaphore(&memsema);
      if(pool) FreePooled(pool,(void *)((ULONG)mem-8),size);
      else FreeMem((void *)((ULONG)mem-8),size);
      ReleaseSemaphore(&memsema);
   }
}

