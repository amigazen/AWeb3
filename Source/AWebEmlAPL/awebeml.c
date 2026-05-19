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

/* awebeml.c - EML email filter plugin main file */

#include "pluginlib.h"
#include "eml.h"
#include <libraries/awebplugin.h>
#include <exec/memory.h>
#include <clib/awebplugin_protos.h>
#include <clib/exec_protos.h>
#include <pragmas/awebplugin_pragmas.h>
#include <pragmas/exec_sysbase_pragmas.h>
#include <string.h>

/* Library bases we use */
extern struct ExecBase *SysBase;
struct Library *AwebPluginBase;

/* Stub function for _XCEXIT to avoid stdio dependency */
void __stdargs _XCEXIT(long x)
{
   /* No-op stub - we don't use stdio */
}

ULONG Initpluginlib(struct EmlBase *base)
{  AwebPluginBase=OpenLibrary("awebplugin.library",2);
   return (ULONG)(AwebPluginBase);
}

void Expungepluginlib(struct EmlBase *base)
{  if(AwebPluginBase) CloseLibrary(AwebPluginBase);
}

__asm __saveds ULONG Initplugin(register __a0 struct Plugininfo *pi)
{  pi->sourcedriver=0;
   pi->copydriver=0;
   return 1;
}

/* Query function. Handle in a safe way. */
#define ISSAFE(s,f) (s->structsize>=((long)&s->f-(long)s+sizeof(s->f)))

__asm __saveds void Queryplugin(register __a0 struct Pluginquery *pq)
{
#ifdef PLUGIN_COMMANDPLUGIN
   if(ISSAFE(pq,command)) pq->command=TRUE;
#endif
#ifdef PLUGIN_FILTERPLUGIN
   if(ISSAFE(pq,filter)) pq->filter=TRUE;
#endif
}

/* Check if content type indicates email */
static BOOL IsEmailContentType(UBYTE *contenttype)
{  if(!contenttype) return FALSE;
   
   if(!strnicmp(contenttype, "message/rfc822", 14)) return TRUE;
   if(!strnicmp(contenttype, "message/rfc822;", 15)) return TRUE;
   
   return FALSE;
}

/* Check if URL extension suggests email */
static BOOL IsEmailUrl(UBYTE *url)
{  UBYTE *ext;
   long len;
   
   if(!url) return FALSE;
   
   len = strlen(url);
   if(len < 4) return FALSE;
   
   ext = url + len - 4;
   if(!strnicmp(ext, ".eml", 4)) return TRUE;
   
   return FALSE;
}

/* Check if data contains email signatures */
static BOOL IsEmailContent(UBYTE *data, long length)
{     UBYTE *p;
   UBYTE *end;
   
   if(!data || length < 10) return FALSE;
   
   end = data + length;
   if(end - data > 512) end = data + 512;
   
   p = data;
   while(p < end - 10)
   {  if(*p == 'F' || *p == 'f')
      {  if(!strnicmp(p, "From:", 5)) return TRUE;
      }
      if(*p == 'T' || *p == 't')
      {  if(!strnicmp(p, "To:", 3)) return TRUE;
         if(!strnicmp(p, "Subject:", 8)) return TRUE;
      }
      if(*p == 'D' || *p == 'd')
      {  if(!strnicmp(p, "Date:", 5)) return TRUE;
      }
      p++;
   }
   
   return FALSE;
}

/* The filter function */
__asm __saveds void Filterplugin(register __a0 struct Pluginfilter *pf)
{
   struct EmlFilterData *fd;
   BOOL should_filter;
   
   EMLDBG4("Filterplugin enter pf=%lx eof=%ld len=%ld userdata=%lx",
      (ULONG)pf, pf ? (ULONG)pf->eof : 0, pf ? pf->length : 0,
      pf ? (ULONG)pf->userdata : 0);
   
   /* See if there is already a userdata for us.
    * If not, allocate and initialize. */
   fd = pf->userdata;
   if(!fd)
   {
      EMLDBG0("Filterplugin: new EmlFilterData");
      fd = (struct EmlFilterData *)AllocVec(sizeof(struct EmlFilterData), MEMF_CLEAR);
      if(!fd)
      {  EMLDBG0("Filterplugin: AllocVec EmlFilterData FAILED");
         return;
      }
      pf->userdata = fd;
      fd->first = TRUE;
      fd->is_email = FALSE;
      fd->parser = NULL;
      fd->html_buffer = NULL;
      fd->html_bufsize = 0;
      fd->html_buflen = 0;
      fd->header_written = FALSE;
      fd->footer_written = FALSE;
      fd->eml_url = NULL;
      fd->rendered = FALSE;
      
      /* Store the EML URL for CID registry */
      if(pf->url)
      {  long urllen;
         urllen = strlen(pf->url);
         fd->eml_url = EmlDupString(pf->url, urllen);
         EMLDBG2("Filterplugin: EmlDupString eml_url=%lx url=%s",
            (ULONG)fd->eml_url, pf->url);
      }
      
      /* Determine if this is an email */
      should_filter = FALSE;
      
      /* Check content type first - specific email types */
      if(IsEmailContentType(pf->contenttype))
      {  should_filter = TRUE;
      }
      
      /* Check URL extension as fallback - but verify content on first chunk */
      if(!should_filter && IsEmailUrl(pf->url))
      {  /* URL suggests email, but verify with content sniffing on first data chunk */
         should_filter = FALSE; /* Will verify on first data chunk */
      }
      
      fd->is_email = should_filter;
      
      /* Only initialize parser if we're certain it's an email (specific content type) */
      if(should_filter)
      {  /* Initialize parser */
         fd->parser = (struct EmlParser *)AllocVec(sizeof(struct EmlParser), MEMF_CLEAR);
         EMLDBG2("Filterplugin: AllocVec parser=%lx is_email=%ld",
            (ULONG)fd->parser, (ULONG)should_filter);
         if(fd->parser)
         {  InitEmlParser(fd->parser);
         }
         else
         {  fd->is_email = FALSE;
         }
      }
      EMLDBG3("Filterplugin: init done is_email=%ld parser=%lx message=%lx",
         (ULONG)fd->is_email, (ULONG)fd->parser,
         fd->parser ? (ULONG)fd->parser->message : 0);
   }
   
   if(pf->data)
   {
      /* Content sniffing on first chunk if not already confirmed as email */
      if(fd->first && !fd->is_email)
      {  BOOL url_suggests_email = IsEmailUrl(pf->url);
         BOOL content_type_suggests_email = FALSE;
         
         if(pf->contenttype)
         {  if(!strnicmp(pf->contenttype, "message/rfc822", 14) ||
               !strnicmp(pf->contenttype, "text/plain", 10))
            {  content_type_suggests_email = TRUE;
            }
         }
         
         /* Do content sniffing if URL suggests email OR content type suggests email */
         if(url_suggests_email || content_type_suggests_email)
         {  if(IsEmailContent(pf->data, pf->length))
            {  fd->is_email = TRUE;
               if(!fd->parser)
               {  fd->parser = (struct EmlParser *)AllocVec(sizeof(struct EmlParser), MEMF_CLEAR);
                  if(fd->parser)
                  {  InitEmlParser(fd->parser);
                  }
                  else
                  {  fd->is_email = FALSE;
                  }
               }
            }
         }
      }
      
      if(fd->is_email && fd->parser)
      {
         /* Before the first data, change the content type
          * and write a proper HTML header */
         if(fd->first)
         {
            Setfiltertype(pf->handle, "text/html");
            fd->first = FALSE;
         }
         
         /* Parse email MIME */
         EMLDBG2("Filterplugin: ParseEmlChunk len=%ld buflen=%ld",
            pf->length, fd->parser->buflen);
         ParseEmlChunk(fd->parser, pf->data, pf->length);
      }
      else
      {  /* Not an email after all, pass through unchanged */
         Writefilter(pf->handle, pf->data, pf->length);
      }
   }
   
   /* EOF: render (even if this call has no pf->data), then tear down. */
   if(pf->eof)
   {
      if(!fd)
      {  EMLDBG0("Filterplugin EOF: no userdata, nothing to clean up");
         EMLDBG0("Filterplugin leave");
         return;
      }
      EMLDBG4("Filterplugin EOF is_email=%ld rendered=%ld parser=%lx message=%lx",
         (ULONG)fd->is_email, (ULONG)fd->rendered, (ULONG)fd->parser,
         fd->parser ? (ULONG)fd->parser->message : 0);
      
      if(fd->is_email && fd->parser && !fd->rendered)
      {  EMLDBG0("Filterplugin EOF: calling RenderEmailToHtml");
         RenderEmailToHtml(fd, pf->handle);
         fd->rendered = TRUE;
         EMLDBG0("Filterplugin EOF: RenderEmailToHtml returned");
      }
      else if(fd->is_email && !fd->parser)
      {  EMLDBG0("Filterplugin EOF: is_email but parser is NULL");
      }
      else if(fd->rendered)
      {  EMLDBG0("Filterplugin EOF: already rendered");
      }
      
      if(fd->parser)
      {  EMLDBG1("Filterplugin EOF: CleanupEmlParser parser=%lx", (ULONG)fd->parser);
         CleanupEmlParser(fd->parser);
         EMLDBG1("Filterplugin EOF: FreeVec parser=%lx", (ULONG)fd->parser);
         FreeVec(fd->parser);
         fd->parser = NULL;
      }
      
      /* Keep CID parts registered until another message loads (browser still resolves cid:). */
      
      if(fd->eml_url)
      {  EMLDBG1("Filterplugin EOF: FreeVec eml_url=%lx", (ULONG)fd->eml_url);
         FreeVec(fd->eml_url);
         fd->eml_url = NULL;
      }
      
      if(fd->html_buffer)
      {  EMLDBG2("Filterplugin EOF: FreeVec html_buffer=%lx len=%ld",
            (ULONG)fd->html_buffer, fd->html_buflen);
         FreeVec(fd->html_buffer);
         fd->html_buffer = NULL;
      }
      
      EMLDBG1("Filterplugin EOF: FreeVec fd=%lx", (ULONG)fd);
      FreeVec(fd);
      pf->userdata = NULL;
      EMLDBG0("Filterplugin EOF: cleanup complete");
   }
   
   EMLDBG0("Filterplugin leave");
}

