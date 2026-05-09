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

#include <string.h>

#include <proto/exec.h>

static struct SignalSemaphore memsema;

void *fastpool,*chippool;

#ifdef BETAKEYFILE
extern BOOL nopool;
#endif

static ULONG awebmem_used=0;
static ULONG awebmem_peak=0;
static ULONG awebmem_used_chip=0;
static ULONG awebmem_used_fast=0;
static ULONG awebmem_peak_chip=0;
static ULONG awebmem_peak_fast=0;

/*-----------------------------------------------------------------------*/

BOOL Initmemory(void)
{  InitSemaphore(&memsema);
   /* CreatePool memFlags: avoid MEMF_CLEAR on the pool (Exec autodocs). Clearing
    * on the pool zeros each new puddle and again on every AllocPooled, which is
    * slower than one explicit memset per block in Pallocmem(). Puddle/threshold
    * (PUDDLESIZE, TRESHSIZE in aweb.h) satisfy threshSize <= puddleSize; */
   if(!(fastpool=CreatePool(MEMF_PUBLIC,PUDDLESIZE,TRESHSIZE)))
      return FALSE;
   if(!(chippool=CreatePool(MEMF_CHIP|MEMF_PUBLIC,PUDDLESIZE,TRESHSIZE)))
      return FALSE;
   return TRUE;
}

void Freememory(void)
{  if(chippool) DeletePool(chippool);
   if(fastpool) DeletePool(fastpool);
}

void *Pallocmem(long size,ULONG flags,void *pool)
{  void *mem;
   ULONG asize;
   BOOL ischip;
#ifdef BETAKEYFILE
   if(nopool) pool=NULL;
#endif
   ObtainSemaphore(&memsema);
   asize=(ULONG)(size+8);
   ischip=BOOLVAL(flags&MEMF_CHIP);
   if(pool) mem=AllocPooled(pool,asize);
   else mem=AllocMem(asize,flags|MEMF_PUBLIC|MEMF_CLEAR);
   ReleaseSemaphore(&memsema);
   if(mem)
   {  /* Pooled path: match prior MEMF_CLEAR-on-pool behaviour (user buffer always
       * zeroed; callers often pass MEMF_PUBLIC/MEMF_ANY only). Single memset replaces
       * Exec double-clear when memFlags included MEMF_CLEAR on CreatePool. */
      if(pool) memset(mem,0,(size_t)asize);
      *(void **)mem=pool;
      *(long *)((ULONG)mem+4)=size+8;
      ObtainSemaphore(&memsema);
      awebmem_used+=asize;
      if(awebmem_used>awebmem_peak) awebmem_peak=awebmem_used;
      if(ischip)
      {  awebmem_used_chip+=asize;
         if(awebmem_used_chip>awebmem_peak_chip) awebmem_peak_chip=awebmem_used_chip;
      }
      else
      {  awebmem_used_fast+=asize;
         if(awebmem_used_fast>awebmem_peak_fast) awebmem_peak_fast=awebmem_used_fast;
      }
      ReleaseSemaphore(&memsema);
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
   BOOL ischip;
   if(mem)
   {  pool=*(void **)((ULONG)mem-8);
      size=*(long *)((ULONG)mem-4);
      ischip=BOOLVAL(TypeOfMem((void *)((ULONG)mem-8))&MEMF_CHIP);
      ObtainSemaphore(&memsema);
      if(size>0 && awebmem_used>=(ULONG)size) awebmem_used-=(ULONG)size;
      if(size>0)
      {  if(ischip)
         {  if(awebmem_used_chip>=(ULONG)size) awebmem_used_chip-=(ULONG)size;
         }
         else
         {  if(awebmem_used_fast>=(ULONG)size) awebmem_used_fast-=(ULONG)size;
         }
      }
      if(pool) FreePooled(pool,(void *)((ULONG)mem-8),size);
      else FreeMem((void *)((ULONG)mem-8),size);
      ReleaseSemaphore(&memsema);
   }
}

ULONG Awebmemused(void)
{  ULONG used;
   ObtainSemaphore(&memsema);
   used=awebmem_used;
   ReleaseSemaphore(&memsema);
   return used;
}

ULONG Awebmempeak(void)
{  ULONG peak;
   ObtainSemaphore(&memsema);
   peak=awebmem_peak;
   ReleaseSemaphore(&memsema);
   return peak;
}

ULONG Awebmemusedchip(void)
{  ULONG used;
   ObtainSemaphore(&memsema);
   used=awebmem_used_chip;
   ReleaseSemaphore(&memsema);
   return used;
}

ULONG Awebmemusedfast(void)
{  ULONG used;
   ObtainSemaphore(&memsema);
   used=awebmem_used_fast;
   ReleaseSemaphore(&memsema);
   return used;
}

ULONG Awebmempeakchip(void)
{  ULONG peak;
   ObtainSemaphore(&memsema);
   peak=awebmem_peak_chip;
   ReleaseSemaphore(&memsema);
   return peak;
}

ULONG Awebmempeakfast(void)
{  ULONG peak;
   ObtainSemaphore(&memsema);
   peak=awebmem_peak_fast;
   ReleaseSemaphore(&memsema);
   return peak;
}

