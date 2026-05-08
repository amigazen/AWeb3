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

/* awebjs.c - AWeb js main */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/locale.h>

#include "awebjs.h"
#include "jslib.h"
#include "jprotos.h"
#include "keyfile.h"
#include "arexxjs.h"

char version[]="\0$VER:AWebJS " AWEBVERSION " " __AMIGADATE__;

__near long __stack=16348;

struct Library *AWebJSBase;
// Library base pointers are now provided by proto headers
// Note: Not static so arexxjs.c can access it for jslib.h pragma libcall directives
// Type is struct Library * to match what pragma libcall expects

static struct Jobject *globalscope = NULL;

static void DumpExitReport(struct Jcontext *jc, struct Jobject *gs)
{
   BPTR outfh;
   struct JmemSessionStats st;
   struct Jobject *jo;
   ULONG live;
   LONG netblocks;
   ULONG props;
   struct Variable *v;
   UBYTE *ctor;
   UBYTE *fname;
   UBYTE *except[16];
   int exn;

   outfh=Output();
   if(!outfh || !jc)
   {
      return;
   }

   exn=0;
   except[exn++]="write";
   except[exn++]="writeln";
   except[exn++]="print";
   except[exn++]="gc";
   except[exn++]="readln";
   except[exn++]="require";
   except[exn++]="delay";
   except[exn++]="ARexx";
   except[exn]=NULL;
   if(gs)
   {
      Clearjobject(gs,except);
   }
   Jgarbagecollect(jc);

   memset(&st,0,sizeof(st));
   Jgetjmemsessionstats(&st);

   live=0;
   for(jo=jc->objects.first;jo && jo->next;jo=jo->next)
   {
      live++;
   }
   netblocks=(LONG)st.jp_allocs-(LONG)st.jp_frees;

   FPrintf(outfh,"\n");
   FPrintf(outfh,"AWebJS: exit cleanup report\n");

   FPrintf(outfh,"\n");
   FPrintf(outfh,"  JS objects:\n");
   FPrintf(outfh,"    created=%lu   (times Newobject() was called)\n",(unsigned long)jc->obj_created);
   FPrintf(outfh,"    disposed=%lu  (times Disposeobject() was called)\n",(unsigned long)jc->obj_disposed);
   FPrintf(outfh,"    live_now=%lu  (objects still linked in jc->objects after Clear globals + GC)\n",
      (unsigned long)live);
   if(live==0)
   {
      FPrintf(outfh,"    result: GOOD no objects remain reachable at this point.\n");
   }
   else
   {
      FPrintf(outfh,"    result: INFO objects listed below were still reachable.\n");
   }

   FPrintf(outfh,"\n");
   FPrintf(outfh,"  Memory blocks (JPallocmem/JFreemem on the interpreter pool):\n");
   FPrintf(outfh,"    alloc_blocks=%lu   (number of JPallocmem() allocations)\n",(unsigned long)st.jp_allocs);
   FPrintf(outfh,"    free_blocks=%lu    (number of JFreemem() frees)\n",(unsigned long)st.jp_frees);
   FPrintf(outfh,"    net_blocks=%ld     (alloc_blocks - free_blocks)\n",(long)netblocks);
   FPrintf(outfh,"    note: net_blocks>0 is expected BEFORE Freejcontext because the context/pools still exist.\n");

   FPrintf(outfh,"\n");
   FPrintf(outfh,"  Memory bytes (interpreter pool session):\n");
   FPrintf(outfh,"    total_alloc_bytes=%lu   (sum of bytes allocated by JPallocmem in this session)\n",
      (unsigned long)st.total_alloc_bytes);
   FPrintf(outfh,"    total_free_bytes=%lu    (sum of bytes freed by JFreemem in this session)\n",
      (unsigned long)st.total_free_bytes);
   FPrintf(outfh,"    outstanding_bytes=%ld   (bytes still allocated)\n",
      (long)st.outstanding_bytes);
   FPrintf(outfh,"    peak_outstanding_bytes=%ld (highest reached during the run)\n",
      (long)st.peak_outstanding_bytes);

   if(live)
   {
      FPrintf(outfh,"\n  Live JavaScript objects:\n");
      for(jo=jc->objects.first;jo && jo->next;jo=jo->next)
      {
         ctor=NULL;
         fname=NULL;
         if(jo->constructor && jo->constructor->function && jo->constructor->function->name)
         {
            ctor=jo->constructor->function->name;
         }
         if(jo->function && jo->function->name)
         {
            fname=jo->function->name;
         }

         props=0;
         for(v=jo->properties.first;v && v->next;v=v->next)
         {
            props++;
         }

         FPrintf(outfh,"    %08lx type=%ld keep=%ld props=%lu%s%s%s%s\n",
            (ULONG)jo,(long)jo->type,(long)jo->keepnr,(unsigned long)props,
            ctor ? " ctor=" : "", ctor ? ctor : "",
            fname ? " func=" : "", fname ? fname : "");
      }
   }
}

/* Standard I/O */
static __saveds __asm void aWrite(register __a0 struct Jcontext *jc)
{  struct Jvar *jv;
   long n;
   for(n=0;jv=Jfargument(jc,n);n++)
   {  printf("%s",Jtostring(jc,jv));
   }
}

static __saveds __asm void aWriteln(register __a0 struct Jcontext *jc)
{  aWrite(jc);
   printf("\n");
}

static __saveds __asm void aReadln(register __a0 struct Jcontext *jc)
{  UBYTE buf[80];
   long l;
   if(!fgets(buf,80,stdin)) *buf='\0';
   l=strlen(buf);
   if(l && buf[l-1]=='\n') buf[l-1]='\0';
   Jasgstring(jc,NULL,buf);
}

/* Not named Feedback: jprotos.h/jslib.c already declare BOOL Feedback(struct Jcontext *). */
static __saveds __asm BOOL Ctrlcjsfeedback(register __a0 struct Jcontext *jc)
{  if(SetSignal(0,0)&SIGBREAKF_CTRL_C) return FALSE;
   else return TRUE;
}

/* Expose engine GC to scripts (self-test memory churn; matches Jgarbagecollect). */
static __saveds __asm void aGc(register __a0 struct Jcontext *jc)
{
   Jgarbagecollect(jc);
}

/* require - JavaScript function to load and execute JavaScript files */
static __saveds __asm void require(register __a0 struct Jcontext *jc)
{  STRPTR filename;
   APTR source;
   FILE *fh;
   ULONG length;
   ULONG n;
   ULONG i;
   struct Jobject *jgscope[4];
   struct Jvar *jv;
   struct Jobject *jthis;
   
   jthis = Jthis(jc);
   i = 0;
   jgscope[i++] = globalscope;
   
   if(jthis != globalscope)
   {
      jgscope[i++] = jthis;
   }
   jgscope[i] = NULL;
   
   for(n = 0; jv = Jfargument(jc, n); n++)
   {
      filename = Jtostring(jc, jv);
      if((fh = fopen(filename, "r")))
      {
          fseek(fh, 0, SEEK_END);
          length = ftell(fh);
          fseek(fh, 0, SEEK_SET);
          if((source = AllocVec(length + 1, MEMF_PUBLIC | MEMF_CLEAR)))
          {  
              fread(source, length, 1, fh);
              Jerrors(jc, TRUE, 1, FALSE);
              Runjprogram(jc, globalscope, source, jthis, jgscope, 0, 0);
              Jgarbagecollect(jc);
              FreeVec(source);
          }
          fclose(fh);     
      }
      else
      {
          printf("failed to load %s\n", filename);
      }
   }
}

/* delay - JavaScript function to delay execution */
static __saveds __asm void delay(register __a0 struct Jcontext *jc)
{  struct Jvar *jv;
   ULONG ticks;
   
   jv = Jfargument(jc, 0);
   if(jv)
   {
       ticks = (ULONG)Jtonumber(jc, jv);
       Delay(ticks);
   }
}

void main()
/* ReadArgs: FILES (multi), PUBSCREEN/K, DEBUG/S.
 * DEBUG (= -D): Jdebug(TRUE) — interactive JS debugger (break/step, DEBF_*), not stack-on-error.
 * Stacktrace: always on (Jsetjstrace) like other JS shells.
 * Errors: Jseterrconsole routes compile/runtime Errorrequester output to stderr (no Intuition). */
{  long args[5]={0};
   UBYTE *argtemplate="FILES/M/A,PUBSCREEN/K,-D=DEBUG/S";
   struct RDArgs *rda;
   UBYTE **p;
   UBYTE *source;
   FILE *f;
   long l;
   struct Jcontext *jc;
   void *jo;
   void *jo_arexx;
   void *jv;
   void *dtbase;
   struct Jobject *jgscope[4];
   
/* window.class bug workaround */
dtbase=OpenLibrary("datatypes.library",0);
   if(rda=ReadArgs(argtemplate,args,NULL))
   {  /* Try both library names for compatibility */
      if((AWebJSBase=OpenLibrary("aweblib/javascript.aweblib",0))
      || (AWebJSBase=OpenLibrary("javascript.aweblib",0))
      || (AWebJSBase=OpenLibrary("PROGDIR:aweblib/javascript.aweblib",0))
      || (AWebJSBase=OpenLibrary("PROGDIR:javascript.aweblib",0))
      || (AWebJSBase=OpenLibrary("aweblib/awebjs.aweblib",0))
      || (AWebJSBase=OpenLibrary("awebjs.aweblib",0))
      || (AWebJSBase=OpenLibrary("PROGDIR:aweblib/awebjs.aweblib",0))
      || (AWebJSBase=OpenLibrary("PROGDIR:awebjs.aweblib",0)))
      {  if(jc=Newjcontext((UBYTE *)args[1]))
         {  Jseterrconsole(jc,TRUE);
            Jsetjstrace(jc,TRUE);
            Jsetfeedback(jc,Ctrlcjsfeedback);
            Jerrors(jc,TRUE,JERRORS_ON,TRUE);
            initarexx(jc);
            
            if(jo=Newjobject(jc))
            {  
               globalscope = jo;
               jgscope[0] = jo;
               jgscope[1] = NULL;
               
               Addjfunction(jc,jo,"write",aWrite,"string",NULL);
               Addjfunction(jc,jo,"writeln",aWriteln,"string",NULL);
               Addjfunction(jc,jo,"print",aWriteln,"string",NULL);
               Addjfunction(jc,jo,"gc",aGc,NULL);
               Addjfunction(jc,jo,"readln",aReadln,NULL);
               Addjfunction(jc,jo,"require",require,"filename",NULL);
               Addjfunction(jc,jo,"delay",delay,"ticks",NULL);
               
               if((jo_arexx = Newjobject(jc)))
               {
                   if((jv = Jproperty(jc, jo, "ARexx")))
                   {
                       Jasgobject(jc, jv, jo_arexx);
                       Addjfunction(jc, jo_arexx, "SendCommand", SendCommand, "host", "command", NULL);
                   }
               }
               
               if(args[2])
               {  Jdebug(jc,TRUE);
               }
               for(p=(UBYTE **)args[0];*p;p++)
               {  if(f=fopen(*p,"r"))
                  {  fseek(f,0,SEEK_END);
                     l=ftell(f);
                     fseek(f,0,SEEK_SET);
                     if(source=AllocVec(l+1,MEMF_PUBLIC|MEMF_CLEAR))
                     {  fread(source,l,1,f);
                        jc->sourcename=*p;
                        Jerrors(jc,TRUE,1,FALSE);
                        Runjprogram(jc,jo,source,jo,jgscope,0,0);
                        Jgarbagecollect(jc);
                        FreeVec(source);
                     }
                     fclose(f);
                  }
                  else fprintf(stderr,"Can't open %s\n",*p);
               }
            }
            freearexx(jc);
            DumpExitReport(jc,globalscope);
            Freejcontext(jc);
         }
         CloseLibrary(AWebJSBase);
      }
      else
      {
          printf("Couldn't open javascript.aweblib or awebjs.aweblib library\n");
      }
      FreeArgs(rda);
   }
/* window.class bug workaround */
if(dtbase) CloseLibrary(dtbase);
}
