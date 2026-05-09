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

/* memory.c AWeb memory manager */

#include "awebjs.h"
#include "awebplugin.h"

#include <proto/exec.h>
#include <exec/execbase.h>

/* Storage lives in jslib.c (Opened with awebjs.aweblib). */
extern struct Library *AwebPluginBase;
extern struct ExecBase *SysBase;
extern BOOL httpdebug;

/*-----------------------------------------------------------------------*/

/* Harden JS allocator: header magic/state, tail canary, poison, quarantine.
 * All state must live at file scope: duplicating statics inside JPallocmem()
 * and JFreemem() created two independent semaphore/quarantine sets and broke
 * locking — immediate heap corruption when alloc and free interleaved. */

#define JMEM_MAGIC_ALLOC 0x4A53414CUL
#define JMEM_MAGIC_FREE  0x4A534652UL
#define JMEM_TAIL_MAGIC  0x4A535441UL
#define JMEM_STATE_ALLOC 1UL
#define JMEM_STATE_FREE  2UL
#define JMEM_TAILSIZE    8UL
/* Poisoning allocated bytes to 0xCD is useful, but too aggressive for legacy
 * JS engine startup (many structs rely on zero init). Keep off by default. */
#define JMEM_POISON_ALLOC 0
/* Set to 0 to disable the quarantine ring (useful if early-init code has
 * latent use-after-free bugs that hard-crash before diagnostics can be read). */
#define JMEM_QUARANTINE  0

struct Jmemhdr
{
   ULONG magic;
   ULONG state;
   ULONG reqsize;
   ULONG totalsize;
   void *pool;
   ULONG flags;
};

static struct SignalSemaphore jmem_sema;
static struct SignalSemaphore jmem_log_sema;
static BOOL jmem_inited;
static void *jmem_quar[32];
static ULONG jmem_quarsz[32];
static ULONG jmem_quaridx;
static ULONG jmem_op;

/* Per-interpreter session: tracks JPallocmem/JFreemem for one CreatePool (jc->pool) so hosts
 * can spot blocks not explicitly freed before DeletePool (outstanding_bytes). DeletePool
 * still reclaims pooled memory from the OS; non-zero outstanding means investigate JFreemem. */
static void *jmem_session_pool;
static ULONG jmem_session_allocs;
static ULONG jmem_session_frees;
static LONG jmem_session_bytes;
static ULONG jmem_session_total_alloc_bytes;
static ULONG jmem_session_total_free_bytes;
static LONG jmem_session_peak_bytes;

static BOOL Jmem_canlog(void)
{
   if(!httpdebug) return FALSE;
   if(!AwebPluginBase) return FALSE;
   if(!SysBase) return FALSE;
   if(SysBase->TDNestCnt) return FALSE;
   if(SysBase->IDNestCnt) return FALSE;
   return TRUE;
}

/* Some early-init/teardown paths may run under Forbid() or other contexts
 * where blocking semaphore waits are unsafe. Keep semaphore usage optional. */
#define JMEM_USE_SEMA 1

#if JMEM_USE_SEMA
#define JMEM_TRY_LOCK()    AttemptSemaphore(&jmem_sema)
#define JMEM_UNLOCK()      ReleaseSemaphore(&jmem_sema)
#define JMEM_LOG_TRY_LOCK() AttemptSemaphore(&jmem_log_sema)
#define JMEM_LOG_UNLOCK()  ReleaseSemaphore(&jmem_log_sema)
#else
#define JMEM_TRY_LOCK()    TRUE
#define JMEM_UNLOCK()      ((void)0)
#define JMEM_LOG_TRY_LOCK() TRUE
#define JMEM_LOG_UNLOCK()  ((void)0)
#endif

static void Jmem_log_alloc(struct Jmemhdr *h, void *user)
{
   if(!jmem_inited) return;
   if(Jmem_canlog())
   {
      if(JMEM_LOG_TRY_LOCK())
      {
      Aprintf("[js:mem] #%ld ALLOC user=%08lx hdr=%08lx req=%ld total=%ld pool=%08lx flags=%08lx\n",
         (long)jmem_op,(ULONG)user,(ULONG)h,(long)h->reqsize,(long)h->totalsize,(ULONG)h->pool,(ULONG)h->flags);
         JMEM_LOG_UNLOCK();
      }
   }
}

static void Jmem_log_free(struct Jmemhdr *h, void *user, ULONG totalsz)
{
   if(!jmem_inited) return;
   if(Jmem_canlog())
   {
      if(JMEM_LOG_TRY_LOCK())
      {
      Aprintf("[js:mem] #%ld FREE  user=%08lx hdr=%08lx req=%ld total=%ld pool=%08lx flags=%08lx\n",
         (long)jmem_op,(ULONG)user,(ULONG)h,(long)h->reqsize,(long)totalsz,(ULONG)h->pool,(ULONG)h->flags);
         JMEM_LOG_UNLOCK();
      }
   }
}

static void Jmem_log_quar_evict(struct Jmemhdr *oh, ULONG oldsz)
{
   if(!jmem_inited) return;
   if(Jmem_canlog())
   {
      if(JMEM_LOG_TRY_LOCK())
      {
      Aprintf("[js:mem] #%ld QEVCT hdr=%08lx total=%ld pool=%08lx magic=%08lx state=%08lx oh_total=%ld\n",
         (long)jmem_op,(ULONG)oh,(long)oldsz,(ULONG)oh->pool,(ULONG)oh->magic,(ULONG)oh->state,(long)oh->totalsize);
         JMEM_LOG_UNLOCK();
      }
   }
}

static void Jmem_log_quar_insert(struct Jmemhdr *h, ULONG idx)
{
   if(!jmem_inited) return;
   if(Jmem_canlog())
   {
      if(JMEM_LOG_TRY_LOCK())
      {
      Aprintf("[js:mem] #%ld QINS  hdr=%08lx idx=%ld total=%ld pool=%08lx\n",
         (long)jmem_op,(ULONG)h,(long)idx,(long)h->totalsize,(ULONG)h->pool);
         JMEM_LOG_UNLOCK();
      }
   }
}

static void Jmem_log_tail(struct Jmemhdr *h, void *user, ULONG t0, ULONG t1)
{
   if(!jmem_inited) return;
   if(Jmem_canlog())
   {
      if(JMEM_LOG_TRY_LOCK())
      {
      Aprintf("[js:mem] #%ld TAIL  user=%08lx hdr=%08lx t0=%08lx t1=%08lx req=%ld\n",
         (long)jmem_op,(ULONG)user,(ULONG)h,(ULONG)t0,(ULONG)t1,(long)h->reqsize);
         JMEM_LOG_UNLOCK();
      }
   }
}

static void Jmem_log_stack(ULONG *sp, const char *tag)
{
   if(!jmem_inited) return;
   if(!Jmem_canlog()) return;
   if(!sp) return;

   /* Best-effort: dump a few stack words (often return addresses). */
   if(!JMEM_LOG_TRY_LOCK()) return;
   Aprintf("[js:mem] #%ld %s SP=%08lx [%08lx %08lx %08lx %08lx %08lx %08lx]\n",
      (long)jmem_op,
      (tag ? (char *)tag : (char *)"STACK"),
      (ULONG)sp,
      (ULONG)sp[0],(ULONG)sp[1],(ULONG)sp[2],(ULONG)sp[3],(ULONG)sp[4],(ULONG)sp[5]);
   JMEM_LOG_UNLOCK();
}

static void Jmem_init(void)
{
   ULONG i;

   if(jmem_inited)
      return;
#if JMEM_USE_SEMA
   InitSemaphore(&jmem_sema);
   InitSemaphore(&jmem_log_sema);
#endif
   for(i=0;i<32;i++)
   {
      jmem_quar[i]=NULL;
      jmem_quarsz[i]=0;
   }
   jmem_quaridx=0;
   jmem_op=0;
   jmem_inited=TRUE;
   if(Jmem_canlog())
   {
      if(JMEM_LOG_TRY_LOCK())
      {
         Aprintf("[js:mem] INIT done\n");
         JMEM_LOG_UNLOCK();
      }
   }
}

void *JPallocmem(long size,ULONG flags,void *pool)
{
   void *mem;
   ULONG asize;
   BOOL use_pool;
   BOOL do_clear;
   struct Jmemhdr *h;
   ULONG *t;
   UBYTE *user;
   ULONG i2;
   ULONG t0;
   ULONG t1;

   Jmem_init();
   jmem_op++;

   if(size<=0)
      return NULL;

   use_pool=BOOLVAL(pool!=NULL);
   /* Be conservative: clear by default to preserve legacy assumptions. */
   do_clear=TRUE;
   asize=(ULONG)size + (ULONG)sizeof(struct Jmemhdr) + JMEM_TAILSIZE;

   if(use_pool)
      mem=AllocPooled(pool,asize);
   else
      mem=AllocMem(asize,flags|MEMF_ANY|MEMF_CLEAR);

   if(!mem)
   {
      if(Jmem_canlog())
      {
         if(JMEM_LOG_TRY_LOCK())
         {
            Aprintf("[js:mem] #%ld ALLOCFAIL req=%ld total=%ld pool=%08lx flags=%08lx\n",
               (long)jmem_op,(long)size,(long)asize,(ULONG)pool,(ULONG)flags);
            JMEM_LOG_UNLOCK();
         }
      }
      return NULL;
   }

   h=(struct Jmemhdr *)mem;
   h->magic=JMEM_MAGIC_ALLOC;
   h->state=JMEM_STATE_ALLOC;
   h->reqsize=(ULONG)size;
   h->totalsize=asize;
   h->pool=pool;
   h->flags=flags;
   user=(UBYTE *)mem + sizeof(struct Jmemhdr);
   if(do_clear)
   {  for(i2=0;i2<(ULONG)size;i2++) user[i2]=0;
   }
   else
   {  /* Optional: poison newly allocated bytes to catch use-before-init. */
#if JMEM_POISON_ALLOC
      for(i2=0;i2<(ULONG)size;i2++) user[i2]=0xCD;
#else
      for(i2=0;i2<(ULONG)size;i2++) user[i2]=0;
#endif
   }
   t=(ULONG *)(user + (ULONG)size);
   t[0]=JMEM_TAIL_MAGIC;
   t[1]=(ULONG)size;
   t0=t[0];
   t1=t[1];
   Jmem_log_alloc(h,(void *)user);
   Jmem_log_tail(h,(void *)user,t0,t1);
   if(use_pool && pool==jmem_session_pool)
   {
      jmem_session_allocs++;
      jmem_session_bytes+=(LONG)asize;
      jmem_session_total_alloc_bytes+=(ULONG)asize;
      if(jmem_session_bytes>jmem_session_peak_bytes)
      {
         jmem_session_peak_bytes=jmem_session_bytes;
      }
   }
   return (void *)user;
}

void JFreemem(void *mem)
{
   struct Jmemhdr *h;
   UBYTE *user;
   ULONG *t;
   ULONG i2;
   ULONG totalsz;
   void *old;
   ULONG oldsz;
   struct Jmemhdr *oh;
   ULONG idx;
   ULONG t0;
   ULONG t1;

   Jmem_init();
   jmem_op++;

   if(!mem)
      return;

   user=(UBYTE *)mem;
   h=(struct Jmemhdr *)(user - sizeof(struct Jmemhdr));
   if(h->magic!=JMEM_MAGIC_ALLOC)
   {
      if(Jmem_canlog())
      {
         if(JMEM_LOG_TRY_LOCK())
         {
            Aprintf("[js:mem] #%ld BADMAGIC user=%08lx hdr=%08lx magic=%08lx state=%08lx\n",
               (long)jmem_op,(ULONG)mem,(ULONG)h,(ULONG)h->magic,(ULONG)h->state);
            JMEM_LOG_UNLOCK();
         }
      }
      Jmem_log_stack((ULONG *)&mem, "BADMAGICSP");
      return;
   }
   if(h->state!=JMEM_STATE_ALLOC)
   {
      if(Jmem_canlog())
      {
         if(JMEM_LOG_TRY_LOCK())
         {
            Aprintf("[js:mem] #%ld DOUBLEFREE user=%08lx hdr=%08lx magic=%08lx state=%08lx\n",
               (long)jmem_op,(ULONG)mem,(ULONG)h,(ULONG)h->magic,(ULONG)h->state);
            JMEM_LOG_UNLOCK();
         }
      }
      Jmem_log_stack((ULONG *)&mem, "DOUBLEFREESP");
      return;
   }
   t=(ULONG *)(user + h->reqsize);
   t0=t[0];
   t1=t[1];
   if(t[0]!=JMEM_TAIL_MAGIC || t[1]!=h->reqsize)
   {
      Jmem_log_tail(h,(void *)mem,t0,t1);
      if(Jmem_canlog())
      {
         if(JMEM_LOG_TRY_LOCK())
         {
            Aprintf("[js:mem] #%ld TAILCORRUPT user=%08lx hdr=%08lx req=%ld\n",
               (long)jmem_op,(ULONG)mem,(ULONG)h,(long)h->reqsize);
            JMEM_LOG_UNLOCK();
         }
      }
      Jmem_log_stack((ULONG *)&mem, "TAILSP");
      /* continue to free, but keep evidence */
   }
   h->state=JMEM_STATE_FREE;
   h->magic=JMEM_MAGIC_FREE;
   /* Do not poison freed bytes by default; some legacy code reads fields
    * after FREE during teardown. Header/state changes still catch double frees. */

   totalsz=h->totalsize;
   Jmem_log_free(h,mem,totalsz);
   if(h->pool==jmem_session_pool)
   {
      jmem_session_frees++;
      jmem_session_bytes-=(LONG)totalsz;
      jmem_session_total_free_bytes+=(ULONG)totalsz;
   }

#if JMEM_QUARANTINE
   if(JMEM_TRY_LOCK())
   {
      idx=jmem_quaridx;
      old=jmem_quar[idx];
      oldsz=jmem_quarsz[idx];
      jmem_quar[idx]=(void *)h;
      jmem_quarsz[idx]=totalsz;
      jmem_quaridx=idx+1;
      if(jmem_quaridx>=32)
         jmem_quaridx=0;
      Jmem_log_quar_insert(h,idx);
      if(old)
      {
         oh=(struct Jmemhdr *)old;
         Jmem_log_quar_evict(oh,oldsz);
         if(oh->pool)
            FreePooled(oh->pool,oh,oldsz);
         else
            FreeMem(oh,oldsz);
      }
      JMEM_UNLOCK();
   }
   else
   {
      if(h->pool)
         FreePooled(h->pool,h,totalsz);
      else
         FreeMem(h,totalsz);
   }
#else
   /* Free immediately. */
   if(h->pool)
      FreePooled(h->pool,h,totalsz);
   else
      FreeMem(h,totalsz);
#endif
}

void *JGetpool(void *p)
{
   struct Jmemhdr *h;

   if(!p)
      return NULL;
   h=(struct Jmemhdr *)((UBYTE *)p - sizeof(struct Jmemhdr));
   if(h->magic!=JMEM_MAGIC_ALLOC)
      return NULL;
   return h->pool;
}

/*-----------------------------------------------------------------------*/

void JmemSessionBind(void *pool)
{
   jmem_session_pool=pool;
   jmem_session_allocs=0;
   jmem_session_frees=0;
   jmem_session_bytes=0;
   jmem_session_total_alloc_bytes=0;
   jmem_session_total_free_bytes=0;
   jmem_session_peak_bytes=0;
}

void JmemSessionUnbind(void)
{
   jmem_session_pool=NULL;
}

void JmemSessionGetStats(ULONG *allocs,ULONG *frees,LONG *outstanding_bytes)
{
   if(allocs)
   {
      *allocs=jmem_session_allocs;
   }
   if(frees)
   {
      *frees=jmem_session_frees;
   }
   if(outstanding_bytes)
   {
      *outstanding_bytes=jmem_session_bytes;
   }
}

void JmemSessionGetStatsEx(
   ULONG *allocs,ULONG *frees,LONG *outstanding_bytes,
   ULONG *total_alloc_bytes,ULONG *total_free_bytes,LONG *peak_outstanding_bytes)
{
   JmemSessionGetStats(allocs,frees,outstanding_bytes);
   if(total_alloc_bytes)
   {
      *total_alloc_bytes=jmem_session_total_alloc_bytes;
   }
   if(total_free_bytes)
   {
      *total_free_bytes=jmem_session_total_free_bytes;
   }
   if(peak_outstanding_bytes)
   {
      *peak_outstanding_bytes=jmem_session_peak_bytes;
   }
}

/* After Freeexecute(): jc->objects must be empty; print JP session and sanity counts. */
void JmemPrintTeardown(struct Jcontext *jc)
{
   ULONG objc;
   ULONG a;
   ULONG f;
   LONG b;
   LONG net;
   struct Jobject *jo;

   objc=0;
   if(jc)
   {
      for(jo=jc->objects.first;jo && jo->next;jo=jo->next)
      {
         objc++;
      }
   }
   JmemSessionGetStats(&a,&f,&b);
   net=(LONG)a-(LONG)f;
   if(AwebPluginBase && Jmem_canlog() && JMEM_LOG_TRY_LOCK())
   {
      Aprintf(
         "[js:mem] sessionTeardown JPalloc=%lu JFreemem=%lu netBlocks=%ld outstandingBytes=%ld objectsLeft=%lu%s\n",
         (unsigned long)a,(unsigned long)f,(long)net,(long)b,(unsigned long)objc,
         (objc!=0) ? " (objects_left after Freeexecute)" : "");
      JMEM_LOG_UNLOCK();
   }
   /* Otherwise: do not write to CLI from inside the aweblib. */
}
