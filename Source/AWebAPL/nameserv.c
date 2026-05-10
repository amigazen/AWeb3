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

/* nameserv.c - aweb name cache */

#include <netdb.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/timer.h>
#include <dos/dos.h>
#include "aweb.h"
#include "awebtcp.h"

struct Hostname
{  NODE(Hostname);
   struct hostent hent;
   UBYTE *hostname;        /* official host name */
   UBYTE *name;            /* requested name */
   UBYTE *addrp;           /* points to addr */
   UBYTE *nullp;           /* terminates list */
   UBYTE addr[4];          /* internet address */
};

static LIST(Hostname) names;
static BOOL inited;

static struct SignalSemaphore namesema;

/*-----------------------------------------------------------------------*/

BOOL Initnameserv(void)
{  InitSemaphore(&namesema);
   NEWLIST(&names);
   inited=TRUE;
   return TRUE;
}

void Freenameserv(void)
{  struct Hostname *hn;
   if(inited)
   {  while(hn=(struct Hostname *)REMHEAD(&names))
      {  if(hn->hostname) FREE(hn->hostname);
         if(hn->name) FREE(hn->name);
         FREE(hn);
      }
   }
}

struct hostent *Lookup(UBYTE *name,struct Library *base)
{  struct Hostname *hn;
   struct hostent *hent=NULL;
   short i;
   ObtainSemaphore(&namesema);
   for(hn=names.first;hn->next;hn=hn->next)
   {  if(STRIEQUAL(hn->name,name))
      {  hent=&hn->hent;
         break;  /* Found it, exit loop */
      }
   }
   ReleaseSemaphore(&namesema);
   if(hent) return hent;
   
   /* CRITICAL: Add timeout protection for DNS lookups to prevent hanging */
   if(CheckSignal(SIGBREAKF_CTRL_C)) {
      AwebLog("dns", "lookup aborted host=%s (break)", name);
      return NULL;
   }
   if(CheckSignal(SIGBREAKF_CTRL_D)) {
      AwebLog("dns", "lookup aborted host=%s (exit)", name);
      return NULL;
   }
   if(CheckSignal(SIGBREAKF_CTRL_E)) {
      AwebLog("dns", "lookup aborted host=%s (exit break)", name);
      return NULL;
   }
   AwebLog("dns", "lookup start host=%s", name);
   if(CheckSignal(SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_D | SIGBREAKF_CTRL_E)) {
      AwebLog("dns", "lookup aborted host=%s (signals before gethostbyname)", name);
      return NULL;
   }
   if(CheckSignal(SIGBREAKF_CTRL_F)) {
      AwebLog("dns", "lookup aborted host=%s (task term)", name);
      return NULL;
   }
   hent = a_gethostbyname(name,base);
   if(hent && hent->h_name && hent->h_addr_list && hent->h_addr_list[0])
   {  if(hn=ALLOCSTRUCT(Hostname,1,MEMF_CLEAR|MEMF_PUBLIC))
      {  if((hn->hostname=Dupstr(hent->h_name,-1))
         && (hn->name=Dupstr(name,-1)))
         {  hn->hent=*hent;
            hn->hent.h_name=hn->hostname;
            for(i=0;i<4;i++) hn->addr[i]=(*hent->h_addr_list)[i];
            hn->addrp=hn->addr;
            hn->hent.h_addr_list=&hn->addrp;
            hent=&hn->hent;
            ObtainSemaphore(&namesema);
            ADDTAIL(&names,hn);
            ReleaseSemaphore(&namesema);
            AwebLog("dns", "lookup ok host=%s -> %s", name, hn->hostname);
         }
         else
         {  if(hn->hostname) FREE(hn->hostname);
            if(hn->name) FREE(hn->name);
            FREE(hn);
         }
      }
   }
   else
   {  /* DNS lookup failed or returned invalid structure */
      AwebLog("dns", "lookup failed host=%s", name);
   }
   
   return hent;
}
