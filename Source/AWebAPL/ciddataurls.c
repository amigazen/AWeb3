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

/* ciddataurls.c - cid: and data: URL scheme handlers */

#include "aweb.h"
#include "fetchdriver.h"
#include "cidregistry.h"
#include "tcperr.h"
#include "task.h"
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/dos.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

/* Conditional debug print for the core AWeb build. This file is linked into
 * AWeb and AWebView, so it must not include aweblib.h/awebplugin.h or call
 * plugin library stubs: those headers intentionally declare a different API
 * surface for external aweblib modules. */
#ifndef CID_DEBUG
#define CID_DEBUG 1
#endif

static void Cid_debug(char *fmt,...)
{
#if CID_DEBUG
   va_list args;
   if(!httpdebug || !fmt) return;
   va_start(args,fmt);
   AwebLogV("cid",fmt,args);
   va_end(args);
#else
   (void)fmt;
#endif
}

/* Base64 character to value conversion */
static UBYTE Base64Char(UBYTE c)
{  if(c >= 'A' && c <= 'Z') return (UBYTE)(c - 'A');
   if(c >= 'a' && c <= 'z') return (UBYTE)(c - 'a' + 26);
   if(c >= '0' && c <= '9') return (UBYTE)(c - '0' + 52);
   if(c == '+') return (UBYTE)62;
   if(c == '/') return (UBYTE)63;
   return (UBYTE)64; /* Invalid */
}

/* Decode base64 encoded data (for data: URLs). */
static UBYTE *DecodeBase64Data(UBYTE *input, long inputlen, long *outputlen)
{  UBYTE *output;
   UBYTE *out;
   long outcap;
   long i;
   long state;
   UBYTE quad[4];
   UBYTE c;
   long v;

   if(!input || inputlen <= 0)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }

   outcap = (inputlen * 3) / 4 + 4;
   output = (UBYTE *)Allocmem(outcap, 0);
   if(!output)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }

   out = output;
   state = 0;
   for(i = 0; i < inputlen; i++)
   {
      c = input[i];
      if(c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
      if(c == '=')
      {
         if(state >= 2)
         {
            *out++ = (UBYTE)((quad[0] << 2) | (quad[1] >> 4));
            if(state >= 3)
            {
               *out++ = (UBYTE)(((quad[1] & 0x0F) << 4) | (quad[2] >> 2));
            }
         }
         break;
      }
      v = (long)Base64Char(c);
      if(v >= 64) continue;
      quad[state++] = (UBYTE)v;
      if(state == 4)
      {
         *out++ = (UBYTE)((quad[0] << 2) | (quad[1] >> 4));
         *out++ = (UBYTE)(((quad[1] & 0x0F) << 4) | (quad[2] >> 2));
         *out++ = (UBYTE)(((quad[2] & 0x03) << 6) | quad[3]);
         state = 0;
      }
   }
   if(state >= 2)
   {
      *out++ = (UBYTE)((quad[0] << 2) | (quad[1] >> 4));
      if(state >= 3)
      {
         *out++ = (UBYTE)(((quad[1] & 0x0F) << 4) | (quad[2] >> 2));
      }
   }

   if(outputlen) *outputlen = out - output;
   return output;
}

/* URL decode %XX sequences (simple version for data: URLs) */
static void Urldecode(UBYTE *str)
{  UBYTE *p, *q;
   UBYTE hex[3];
   long val;
   
   if(!str) return;
   
   p = q = str;
   while(*p)
   {  if(*p == '%' && p[1] && p[2])
      {  hex[0] = p[1];
         hex[1] = p[2];
         hex[2] = '\0';
         sscanf(hex, "%lx", &val);
         *q++ = (UBYTE)val;
         p += 3;
      }
      else if(*p == '+')
      {  *q++ = ' ';
         p++;
      }
      else
      {  *q++ = *p++;
      }
   }
   *q = '\0';
}

/*------------------------------------------------------------------------*/

/* Content-ID URL handler (cid: scheme) */
void Cidurltask(struct Fetchdriver *fd)
{  UBYTE *content_id;
   UBYTE *referer;
   UBYTE *content_type = NULL;
   UBYTE *data = NULL;
   long datalen = 0;
   BOOL error = FALSE;
   BOOL eof = TRUE;

   /* Extract Content-ID from URL (skip "cid:" prefix) */
   content_id = fd->name;

   /* Get referer URL to find the source document */
   referer = fd->referer;

   /* Unconditional trace: helps diagnose why inline images don't render.
    * Without referer the CID registry lookup can't succeed; without an id
    * (the part after "cid:") there is nothing to look up. */
   Cid_debug("CID: Cidurltask entry referer=%s name=%s",
      referer ? (char *)referer : "(null)",
      content_id ? (char *)content_id : "(null)");

   if(!content_id || !*content_id)
   {  Cid_debug("CID: Cidurltask error empty id");
      error = TRUE;
   }
   else if(!referer || !*referer)
   {  /* No referer - can't look up part */
      Cid_debug("CID: Cidurltask error empty referer (id=%s)",content_id);
      error = TRUE;
   }
   else
   {  /* Look up part data by Content-ID and referer */
      if(!Findcidpart(referer, content_id, &content_type, &data, &datalen))
      {  error = TRUE;
         Cid_debug("CID: Cidurltask Findcidpart MISS referer=%s id=%s",
            referer,content_id);
         if(httpdebug)
         {  AwebLog("cid","Cidurltask: Findcidpart failed referer=%s id=%s",
               referer?referer:(UBYTE *)"(null)",
               content_id?content_id:(UBYTE *)"(null)");
         }
      }
      else
      {  Cid_debug("CID: Cidurltask Findcidpart HIT referer=%s id=%s type=%s len=%ld",
            referer,content_id,
            content_type ? (char *)content_type : "(null)",
            datalen);
      }
   }

   if(error)
   {  Tcperror(fd, TCPERR_XAWEB, fd->name);
   }
   else
   {  /* Set content type */
      if(content_type)
      {  Updatetaskattrs(AOURL_Contenttype, content_type, TAG_END);
      }

      /* Deliver CID body via the normal AOURL_Data path. imgsource copies into
       * its owned ramdata and decodes with DTST_MEMORY; disk cache is not used
       * (CID URLs are non-cacheable). Registry data stays valid until
       * Unregistercidparts() when the document closes. */
      if(data && datalen > 0)
      {  Cid_debug("CID: Cidurltask delivering %ld bytes type=%s",
            datalen,
            content_type ? (char *)content_type : "(null)");
         Updatetaskattrs(
            AOURL_Data, data,
            AOURL_Datalength, datalen,
            TAG_END);
         /* Do not free data here - owned by the CID registry for document life. */
      }
   }

   Updatetaskattrs(AOTSK_Async, TRUE,
      AOURL_Error, error,
      AOURL_Eof, eof,
      AOURL_Terminate, TRUE,
      TAG_END);
}

/*------------------------------------------------------------------------*/

/* Data URI handler (data: scheme) */
void Dataurltask(struct Fetchdriver *fd)
{     UBYTE *data_uri;
   UBYTE *comma;
   UBYTE *content_type = "text/plain";
   UBYTE *content_type_allocated = NULL;
   UBYTE *data = NULL;
   long datalen = 0;
   BOOL base64 = FALSE;
   BOOL error = FALSE;
   BOOL eof = TRUE;
   UBYTE *mediatype_copy = NULL;
   UBYTE *semicolon;
   UBYTE *data_str;
   UBYTE *decoded_data = NULL;
   UBYTE *deliver_data;
   UBYTE *reg_type;
   long mediatype_len;
   long deliver_len;
   
   /* fd->name contains the data: URL without the "data:" prefix (stripped in fetch.c)
    * We need to reconstruct the full data: URL string for the registry key */
   data_uri = fd->name;
   
   if(!data_uri || !*data_uri)
   {  
      error = TRUE;
   }
   else
   {  /* Parse data URI: data:[<mediatype>][;base64],<data> */
      comma = strchr(data_uri, ',');
      if(!comma)
      {  
         error = TRUE;
      }
      else
      {  /* Make a copy of mediatype part for parsing */
         mediatype_len = comma - data_uri;
         if(mediatype_len > 0)
         {  mediatype_copy = (UBYTE *)Allocmem(mediatype_len + 1, 0);
            if(mediatype_copy)
            {  memcpy(mediatype_copy, data_uri, mediatype_len);
               mediatype_copy[mediatype_len] = '\0';
            }
         }
         data_str = comma + 1;  /* Point to data */
         
         if(mediatype_copy)
         {  /* Check for base64 encoding */
            if(strstr(mediatype_copy, ";base64"))
            {  base64 = TRUE;
               
            }
            
            /* Extract content type */
            semicolon = strchr(mediatype_copy, ';');
            if(semicolon)
            {  *semicolon = '\0';
            }
            
            if(*mediatype_copy)
            {  /* Allocate separate copy for content_type */
               content_type_allocated = Dupstr(mediatype_copy, -1);
               if(content_type_allocated)
               {  content_type = content_type_allocated;
               }
               else
               {  
               }
            }
         }
         
         /* Decode data */
         if(base64)
         {  decoded_data = DecodeBase64Data((UBYTE *)data_str, strlen(data_str), &datalen);
            if(!decoded_data || datalen <= 0)
            {  
               error = TRUE;
            }
            else
            {  data = decoded_data;
            }
         }
         else
         {  /* URL decode */
            data = (UBYTE *)Dupstr((UBYTE *)data_str, -1);
            if(data)
            {  Urldecode(data);
               datalen = strlen(data);
            }
            else
            {  error = TRUE;
            }
         }
      }
   }
   
   if(error)
   {  /* Free allocated data on error only */
      if(decoded_data) FREE(decoded_data);
      else if(data && data != content_type_allocated) FREE(data);
      if(content_type_allocated) FREE(content_type_allocated);
      if(mediatype_copy) FREE(mediatype_copy);
      Tcperror(fd, TCPERR_XAWEB, fd->name);
   }
   else
   {  deliver_data = NULL;
      deliver_len = 0;
      reg_type = content_type;

      /* Set content type first */
      Updatetaskattrs(AOURL_Contenttype, content_type, TAG_END);

      if(data && datalen > 0 && fd->referer)
      {
         /* Registry owns the payload; deliver that copy (not freed below). */
         if(Registercidpart(fd->referer, fd->name, content_type, data, datalen))
         {
            if(Findcidpart(fd->referer, fd->name, &reg_type, &deliver_data, &deliver_len))
            {
               Updatetaskattrs(
                  AOURL_Data, deliver_data,
                  AOURL_Datalength, deliver_len,
                  TAG_END);
            }
         }
      }
      else if(data && datalen > 0)
      {
         Updatetaskattrs(
            AOURL_Data, data,
            AOURL_Datalength, datalen,
            TAG_END);
      }

      Updatetaskattrs(AOTSK_Async, TRUE,
         AOURL_Error, error,
         AOURL_Eof, eof,
         AOURL_Terminate, TRUE,
         TAG_END);

      if(content_type_allocated) FREE(content_type_allocated);
      if(mediatype_copy) FREE(mediatype_copy);
      if(decoded_data) FREE(decoded_data);
      else if(data) FREE(data);
   }
   
}

