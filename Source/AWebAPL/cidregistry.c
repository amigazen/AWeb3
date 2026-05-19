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
   UBYTE *referer_url;
   UBYTE *part_id;
   UBYTE *content_type;
   UBYTE *data;
   long datalen;
};

static LIST(CidPart) cid_parts;
static struct SignalSemaphore cid_sema;

/* Free one registry entry (all fields use AWeb FREE / Allocmem). */
static void CidFreePart(struct CidPart *part)
{
   if(!part) return;
   if(part->referer_url) FREE(part->referer_url);
   if(part->part_id) FREE(part->part_id);
   if(part->content_type) FREE(part->content_type);
   if(part->data) FREE(part->data);
   FREE(part);
}

/* Copy payload into AWeb pool (caller keeps its own buffer). */
static UBYTE *CidCopyPayload(UBYTE *data, long datalen)
{
   UBYTE *copy;

   if(!data || datalen <= 0) return NULL;
   copy = ALLOCTYPE(UBYTE, datalen, MEMF_PUBLIC);
   if(copy) memcpy(copy, data, datalen);
   return copy;
}

/* Case-insensitive Content-ID match (angle brackets optional). */
static BOOL CidPartIdsMatch(UBYTE *stored_id, UBYTE *lookup_id)
{
   UBYTE *cid1;
   UBYTE *cid2;
   UBYTE *end1;
   UBYTE *end2;
   long len1;
   long len2;

   if(!stored_id || !lookup_id) return FALSE;

   cid1 = stored_id;
   cid2 = lookup_id;
   if(*cid1 == '<') cid1++;
   if(*cid2 == '<') cid2++;

   end1 = cid1 + strlen(cid1);
   end2 = cid2 + strlen(cid2);
   if(end1 > cid1 && end1[-1] == '>') end1--;
   if(end2 > cid2 && end2[-1] == '>') end2--;

   len1 = end1 - cid1;
   len2 = end2 - cid2;
   if(len1 <= 0 || len1 != len2) return FALSE;
   if(strnicmp(cid1, cid2, len1)) return FALSE;
   return TRUE;
}

/* Initialize CID registry */
BOOL Initcidregistry(void)
{
   NEWLIST(&cid_parts);
   InitSemaphore(&cid_sema);
   return TRUE;
}

/* Register a part for cid: or data: URL lookup.
 * Copies data into the AWeb memory pool; caller retains its buffer.
 * Returns TRUE if a new entry was added. */
BOOL Registercidpart(UBYTE *referer_url, UBYTE *part_id,
                     UBYTE *content_type, UBYTE *data, long datalen)
{
   struct CidPart *part;
   struct CidPart *existing;
   UBYTE *payload;
   BOOL is_data_url;

   if(!part_id || !data || datalen <= 0 || !referer_url) return FALSE;

   payload = CidCopyPayload(data, datalen);
   if(!payload) return FALSE;

   is_data_url = (strnicmp(part_id, "data:", 5) == 0);

   ObtainSemaphore(&cid_sema);

   for(existing = cid_parts.first; existing; existing = existing->next)
   {
      if(!existing->part_id || !existing->referer_url) continue;
      if(!STRIEQUAL(existing->referer_url, referer_url)) continue;

      if(is_data_url)
      {
         if(STRIEQUAL(existing->part_id, part_id))
         {  FREE(payload);
            ReleaseSemaphore(&cid_sema);
            return FALSE;
         }
      }
      else if(CidPartIdsMatch(existing->part_id, part_id))
      {  FREE(payload);
         ReleaseSemaphore(&cid_sema);
         return FALSE;
      }
   }

   part = (struct CidPart *)Allocmem((long)sizeof(struct CidPart), MEMF_CLEAR);
   if(!part)
   {  FREE(payload);
      ReleaseSemaphore(&cid_sema);
      return FALSE;
   }

   part->referer_url = Dupstr(referer_url, -1);
   part->part_id = Dupstr(part_id, -1);
   if(content_type)
   {  part->content_type = Dupstr(content_type, -1);
   }
   else
   {  part->content_type = Dupstr("application/octet-stream", -1);
   }

   if(!part->referer_url || !part->part_id || !part->content_type)
   {  CidFreePart(part);
      FREE(payload);
      ReleaseSemaphore(&cid_sema);
      return FALSE;
   }

   part->data = payload;
   part->datalen = datalen;
   ADDTAIL(&cid_parts, part);

   ReleaseSemaphore(&cid_sema);
   return TRUE;
}

/* Find a part by referer and part ID */
BOOL Findcidpart(UBYTE *referer_url, UBYTE *part_id,
                 UBYTE **content_type, UBYTE **data, long *datalen)
{
   struct CidPart *part;
   BOOL found;
   BOOL is_data_url;

   if(!part_id || !referer_url) return FALSE;

   is_data_url = (strnicmp(part_id, "data:", 5) == 0);
   found = FALSE;

   ObtainSemaphore(&cid_sema);

   for(part = cid_parts.first; part; part = part->next)
   {
      if(!part->part_id || !part->referer_url) continue;
      if(!STRIEQUAL(referer_url, part->referer_url)) continue;

      if(is_data_url)
      {
         if(STRIEQUAL(part_id, part->part_id))
         {  if(content_type) *content_type = part->content_type;
            if(data) *data = part->data;
            if(datalen) *datalen = part->datalen;
            found = TRUE;
            break;
         }
      }
      else if(CidPartIdsMatch(part->part_id, part_id))
      {  if(content_type) *content_type = part->content_type;
         if(data) *data = part->data;
         if(datalen) *datalen = part->datalen;
         found = TRUE;
         break;
      }
   }

   ReleaseSemaphore(&cid_sema);
   return found;
}

/* Unregister all parts for a referer URL */
void Unregistercidparts(UBYTE *referer_url)
{
   struct CidPart *part;
   struct CidPart *next;

   if(!referer_url) return;

   ObtainSemaphore(&cid_sema);

   for(part = cid_parts.first; part; part = next)
   {
      next = part->next;
      if(part->referer_url && STRIEQUAL(referer_url, part->referer_url))
      {  REMOVE(part);
         CidFreePart(part);
      }
   }

   ReleaseSemaphore(&cid_sema);
}

/* Cleanup CID registry */
void Cleanupcidregistry(void)
{
   struct CidPart *part;
   struct CidPart *next;

   ObtainSemaphore(&cid_sema);

   for(part = cid_parts.first; part; part = next)
   {
      next = part->next;
      REMOVE(part);
      CidFreePart(part);
   }

   ReleaseSemaphore(&cid_sema);
}
