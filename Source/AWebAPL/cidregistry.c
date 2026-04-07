/**********************************************************************
 * 
 * This file is part of the AWeb distribution
 *
 * Copyright (C) 2025 amigazen project
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

/* cidregistry.c - Content-ID part registry for cid: URL support */

#include "aweb.h"
#include "cidregistry.h"
#include "ezlists.h"
#include <exec/lists.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <string.h>

/* CID part structure (used for both cid: and data: URLs) */
struct CidPart
{
   NODE(CidPart);
   UBYTE *referer_url;    /* URL of document containing the part (NULL for data: URLs) */
   UBYTE *part_id;        /* Content-ID for cid: URLs, or full data: URL string for data: URLs */
   UBYTE *content_type;
   UBYTE *data;
   long datalen;
};

static LIST(CidPart) cid_parts;
static struct SignalSemaphore cid_sema;

/* Initialize CID registry */
BOOL Initcidregistry(void)
{
   NEWLIST(&cid_parts);
   InitSemaphore(&cid_sema);
   return TRUE;
}

/* Register a part for cid: or data: URL lookup
 * For cid: URLs: referer_url is required, part_id is the Content-ID
 * For data: URLs: referer_url should be the owning document URL, part_id is the full data: URL string */
void Registercidpart(UBYTE *referer_url, UBYTE *part_id,
                     UBYTE *content_type, UBYTE *data, long datalen)
{
   struct CidPart *part;
   struct CidPart *existing;
   BOOL is_data_url;
   
   if(!part_id || !data || datalen <= 0) return;
   is_data_url = (strnicmp(part_id, "data:", 5) == 0);
   /* For cid: URLs, referer_url is required. For data: URLs, we also require referer_url
    * so that document close can release the decoded buffers. */
   if(!referer_url) return;
   
   ObtainSemaphore(&cid_sema);

   /* Avoid duplicates: if this referer+part_id already exists, keep the first and
    * free the incoming data buffer (caller transferred ownership). */
   for(existing = cid_parts.first; existing->next; existing = existing->next)
   {
      if(existing->part_id && STRIEQUAL(existing->part_id, part_id))
      {
         if(is_data_url)
         {
            if(existing->referer_url && STRIEQUAL(existing->referer_url, referer_url))
            {
               FREE(data);
               ReleaseSemaphore(&cid_sema);
               return;
            }
         }
         else
         {
            if(existing->referer_url && STRIEQUAL(existing->referer_url, referer_url))
            {
               FREE(data);
               ReleaseSemaphore(&cid_sema);
               return;
            }
         }
      }
   }
   
   part = (struct CidPart *)AllocVec(sizeof(struct CidPart), MEMF_CLEAR);
   if(part)
   {
      part->referer_url = Dupstr(referer_url, -1);
      part->part_id = Dupstr(part_id, -1);
      if(content_type)
      {
         part->content_type = Dupstr(content_type, -1);
      }
      else
      {
         part->content_type = Dupstr("application/octet-stream", -1);
      }
      part->data = data;  /* Transfer ownership - caller should not free */
      part->datalen = datalen;
      ADDTAIL(&cid_parts, part);
   }
   
   ReleaseSemaphore(&cid_sema);
}

/* Find a part by referer and part ID (Content-ID for cid:, or data: URL string for data:)
 * For cid: URLs: referer_url is required, part_id is the Content-ID
 * For data: URLs: referer_url should be the owning document URL, part_id is the full data: URL string */
BOOL Findcidpart(UBYTE *referer_url, UBYTE *part_id,
                 UBYTE **content_type, UBYTE **data, long *datalen)
{
   struct CidPart *part;
   BOOL found = FALSE;
   UBYTE *cid1;
   UBYTE *cid2;
   UBYTE *end1;
   UBYTE *end2;
   BOOL is_data_url;
   
   if(!part_id) return FALSE;
   
   /* Check if this is a data: URL (starts with "data:") */
   is_data_url = (strnicmp(part_id, "data:", 5) == 0);
   
   /* referer_url is required for cleanup and to avoid cross-document collisions. */
   if(!referer_url) return FALSE;
   
   ObtainSemaphore(&cid_sema);
   
   for(part = cid_parts.first; part->next; part = part->next)
   {
      if(part->part_id)
      {
         if(is_data_url)
         {  /* For data: URLs, match by referer + full data: URL string */
            if(part->referer_url
            && STRIEQUAL(referer_url, part->referer_url)
            && STRIEQUAL(part_id, part->part_id))
            {
               if(content_type) *content_type = part->content_type;
               if(data) *data = part->data;
               if(datalen) *datalen = part->datalen;
               found = TRUE;
               break;
            }
         }
         else
         {  /* For cid: URLs, match by referer + Content-ID */
            if(part->referer_url && STRIEQUAL(referer_url, part->referer_url))
            {
               /* Match Content-ID (case-insensitive, remove angle brackets if present) */
               cid1 = part->part_id;
               cid2 = part_id;
               
               /* Skip angle brackets if present */
               if(*cid1 == '<') cid1++;
               if(*cid2 == '<') cid2++;
               
               /* Find end, skipping closing bracket */
               end1 = cid1 + strlen(cid1);
               end2 = cid2 + strlen(cid2);
               if(end1 > cid1 && end1[-1] == '>') end1--;
               if(end2 > cid2 && end2[-1] == '>') end2--;
               
               /* Compare */
               if((end1 - cid1) == (end2 - cid2))
               {
                  if(!strnicmp(cid1, cid2, end1 - cid1))
                  {
                     if(content_type) *content_type = part->content_type;
                     if(data) *data = part->data;
                     if(datalen) *datalen = part->datalen;
                     found = TRUE;
                     break;
                  }
               }
            }
         }
      }
   }
   
   ReleaseSemaphore(&cid_sema);
   return found;
}

/* Unregister all parts for a referer URL (cleanup)
 * For cid: URLs: unregisters all parts for this referer
 * For data: URLs: data parts are registered under their owning document's referer URL and
 * are cleaned up here the same way. */
void Unregistercidparts(UBYTE *referer_url)
{
   struct CidPart *part, *next;
   
   if(!referer_url) return;
   
   ObtainSemaphore(&cid_sema);
   
   for(part = cid_parts.first; part->next; part = next)
   {
      next = part->next;
      if(part->referer_url && STRIEQUAL(referer_url, part->referer_url))
      {
         REMOVE(part);
         if(part->referer_url) FREE(part->referer_url);
         if(part->part_id) FREE(part->part_id);
         if(part->content_type) FREE(part->content_type);
         if(part->data) FREE(part->data);
         FreeVec(part);
      }
   }
   
   ReleaseSemaphore(&cid_sema);
}

/* Cleanup CID registry */
void Cleanupcidregistry(void)
{
   struct CidPart *part, *next;
   
   ObtainSemaphore(&cid_sema);
   
   for(part = cid_parts.first; part->next; part = next)
   {
      next = part->next;
      REMOVE(part);
      if(part->referer_url) FREE(part->referer_url);
      if(part->part_id) FREE(part->part_id);
      if(part->content_type) FREE(part->content_type);
      if(part->data) FREE(part->data);
      FreeVec(part);
   }
   
   ReleaseSemaphore(&cid_sema);
}

