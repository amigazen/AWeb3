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

/* docext.c - AWeb HTML document extension (script, style) object */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include "aweb.h"
#include "source.h"
#include "sourcedriver.h"
#include "copy.h"
#include "url.h"
#include "cache.h"
#include "docprivate.h"

static LIST(Docext) docexts;

/*------------------------------------------------------------------------*/

/* Reference to a waiting document */
struct Docref
{  NODE(Docref);
   struct Document *doc;
   void *url;
};

static LIST(Docref) docrefs;

/* Signal all waiting documents */
static void Signaldocs(struct Docext *dox)
{  struct Docref *dr,*drnext;
   void *url,*durl;
   durl=(void *)Agetattr(dox->url,AOURL_Finalurlptr);
/*
printf("Signal for url %08x=%s\n"
       "             ->%08x=%s\n",
       dox->url,Agetattr(dox->url,AOURL_Url),
       durl,Agetattr(durl,AOURL_Url));
*/
   for(dr=docrefs.first;dr->next;dr=drnext)
   {  drnext=dr->next;
      url=(void *)Agetattr(dr->url,AOURL_Finalurlptr);
/*
printf("       ref url %08x=%s\n"
       "             ->%08x=%s\n",
       dr->url,Agetattr(dr->url,AOURL_Url),
       url,Agetattr(url,AOURL_Url));
*/
      if(url==durl)
      {  REMOVE(dr);
         Asetattrs(dr->doc,AODOC_Docextready,dr->url,TAG_END);
         FREE(dr);
      }
   }
}

static void Addwaitingdoc(struct Document *doc,void *url)
{  struct Docref *dr;
   if(dr=ALLOCSTRUCT(Docref,1,0))
   {  dr->doc=doc;
      dr->url=url;
      ADDTAIL(&docrefs,dr);
   }
}

void Remwaitingdoc(struct Document *doc)
{  struct Docref *dr,*drnext;
   for(dr=docrefs.first;dr->next;dr=drnext)
   {  drnext=dr->next;
      if(dr->doc==doc)
      {  REMOVE(dr);
         FREE(dr);
      }
   }
}

/*------------------------------------------------------------------------*/

static long Setdocext(struct Docext *dox,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            dox->source=(void *)tag->ti_Data;
            break;
      }
   }
   return 0;
}

static struct Docext *Newdocext(struct Amset *ams)
{  struct Docext *dox;
   if(dox=Allocobject(AOTP_DOCEXT,sizeof(struct Docext),ams))
   {  Setdocext(dox,ams);
      dox->url=(void *)Agetattr(dox->source,AOSRC_Url);
      ADDTAIL(&docexts,dox);
   }
   return dox;
}

static long Getdocext(struct Docext *dox,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            PUTATTR(tag,dox->source);
            break;
      }
   }
   return 0;
}

static long Srcupdatedocext(struct Docext *dox,struct Amsrcupdate *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   long length=0;
   UBYTE *data=NULL;
   BOOL eof=FALSE;
   BOOL notmodified=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOURL_Contentlength:
            Expandbuffer(&dox->buf,tag->ti_Data-dox->buf.length);
            break;
         case AOURL_Data:
            data=(UBYTE *)tag->ti_Data;
            break;
         case AOURL_Datalength:
            length=tag->ti_Data;
            break;
         case AOURL_Notmodified:
            /* 304 Not Modified - server says use cached version. If we already have
             * a cached buffer, use it. If not, this is an error (server says "use cache"
             * but we have no cache). */
            notmodified=TRUE;
            break;
         case AOURL_Reload:
            Freebuffer(&dox->buf);
            /* Clear both EOF and ERROR flags on reload to allow retry */
            dox->flags&=~(DOXF_EOF|DOXF_ERROR|DOXF_LOADING|DOXF_RETRY);
            break;
         case AOURL_Eof:
            if(tag->ti_Data)
            {  dox->flags|=DOXF_EOF;
               dox->flags&=~DOXF_LOADING;  /* Load completed successfully */
               dox->flags&=~DOXF_RETRY;     /* Clear RETRY flag on successful completion */
               /* If ERROR was set earlier but we now have valid data, clear ERROR flag.
                * This handles the case where ERROR callback arrives before data, but then
                * EOF arrives with valid data (race condition in callback ordering). */
               if((dox->flags&DOXF_ERROR) && dox->buf.buffer && dox->buf.length > 1)
               {  if(httpdebug)
                  {  void *urlstr;
                     urlstr=(void *)Agetattr(dox->url,AOURL_Url);
                     AwebLog("fetch", "Srcupdatedocext: EOF received with valid data after ERROR, clearing ERROR flag for URL=%s, length=%ld",
                            urlstr ? (char *)urlstr : "NULL", dox->buf.length);
                  }
                  dox->flags&=~DOXF_ERROR;  /* Clear ERROR since we have valid data */
               }
               eof=TRUE;
            }
            break;
         case AOURL_Error:
            if(tag->ti_Data)
            {  dox->flags|=DOXF_EOF|DOXF_ERROR;
               dox->flags&=~DOXF_LOADING;  /* Load completed with error */
               dox->flags&=~DOXF_RETRY;     /* Clear RETRY flag - will be set if buffer invalid */
               eof=TRUE;
               if(httpdebug)
               {  void *urlstr;
                  UBYTE *status;
                  urlstr=(void *)Agetattr(dox->url,AOURL_Url);
                  status=(UBYTE *)Agetattr(dox->url,AOURL_Status);
                  AwebLog("fetch", "Srcupdatedocext: ERROR received for URL=%s, buffer=%p, length=%ld, status=%s",
                         urlstr ? (char *)urlstr : "NULL", dox->buf.buffer, dox->buf.length,
                         status ? (char *)status : "NULL");
               }
            }
            break;
      }
   }
   if(data)
   {  Addtobuffer(&dox->buf,data,length);
      Asetattrs(dox->source,AOSRC_Memory,dox->buf.size,TAG_END);
   }
   if(eof)
   {  Addtobuffer(&dox->buf,"",1);
      if(httpdebug)
      {  void *urlstr;
         urlstr=(void *)Agetattr(dox->url,AOURL_Url);
         AwebLog("fetch", "Srcupdatedocext: EOF reached, URL=%s, notmodified=%d, buffer=%p, length=%ld, flags=0x%04x",
                urlstr ? (char *)urlstr : "NULL", notmodified ? 1 : 0,
                dox->buf.buffer, dox->buf.length, dox->flags);
      }
      /* Handle 304 Not Modified response - server says use cached version.
       * If we have a cached buffer, it's already valid. If not, this is an error. */
      if(notmodified)
      {  if(dox->buf.buffer && dox->buf.length > 1)
         {  /* We have a cached buffer - 304 means use it, which we already have */
            if(httpdebug)
            {  AwebLog("fetch", "Srcupdatedocext: 304 Not Modified, using existing cached buffer (length=%ld)", dox->buf.length);
            }
            /* Signal waiting documents with the cached buffer */
            Signaldocs(dox);
         }
         else
         {  /* 304 but no cached buffer - server says "use cache" but we have none in Docext buffer.
              * This can happen if the URL cache has the data but Docext buffer was cleared.
              * Don't set LOADING - just set RETRY and signal. This allows the callback to retry
              * immediately, or parsing to resume and Dolink to retry naturally. */
            if(httpdebug)
            {  AwebLog("fetch", "Srcupdatedocext: 304 Not Modified but no cached buffer (buffer=%p, length=%ld), setting RETRY flag (no LOADING)",
                      dox->buf.buffer, dox->buf.length);
            }
            dox->flags|=DOXF_RETRY;
            /* Don't set LOADING - allow callback or natural flow to retry */
            dox->flags&=~DOXF_EOF;
            if(dox->flags&DOXF_ERROR) dox->flags&=~DOXF_ERROR;  /* Clear error for retry */
            Signaldocs(dox);
         }
      }
      else if(!dox->buf.buffer || dox->buf.length <= 1)
      {  /* Buffer is invalid (not a 304 case) - set RETRY flag to indicate retry needed.
           * Do NOT set LOADING here - it will be set in Finddocext when the retry actually starts.
           * Setting LOADING here prevents Finddocext from initiating the retry. */
         dox->flags|=DOXF_RETRY;
         dox->flags&=~DOXF_LOADING;  /* Clear LOADING to allow Finddocext to retry */
         dox->flags&=~DOXF_EOF;  /* Clear EOF since we're retrying */
         if(dox->flags&DOXF_ERROR) dox->flags&=~DOXF_ERROR;  /* Clear error for retry */
         if(httpdebug)
         {  AwebLog("fetch", "Srcupdatedocext: EOF with invalid buffer, setting RETRY flag (not LOADING), signaling to resume parsing");
         }
         /* Signal to resume parsing - Finddocext will see RETRY without LOADING and start retry */
         Signaldocs(dox);
      }
      else
      {  /* Buffer is valid - clear ERROR flag if it was set prematurely
           * (e.g., ERROR callback arrived before data/EOF callbacks) */
         if(dox->flags&DOXF_ERROR)
         {  dox->flags&=~DOXF_ERROR;
         }
         /* Signal waiting documents with valid buffer */
         Signaldocs(dox);
      }
   }
   return 0;
}

/* A new child was added; send it an initial update msg */
static long Addchilddocext(struct Docext *dox,struct Amadd *ama)
{  Asetattrs(ama->child,AODOC_Srcupdate,TRUE,TAG_END);
   return 0;
}

static void Disposedocext(struct Docext *dox)
{  REMOVE(dox);
   Freebuffer(&dox->buf);
   Asetattrs(dox->source,AOSRC_Memory,0,TAG_END);
   Amethodas(AOTP_OBJECT,dox,AOM_DISPOSE);
}

static void Deinstalldocext(void)
{  struct Docref *p;
   while(p=REMHEAD(&docrefs)) FREE(p);
}

static long Dispatch(struct Docext *dox,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newdocext((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setdocext(dox,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getdocext(dox,(struct Amset *)amsg);
         break;
      case AOM_SRCUPDATE:
         result=Srcupdatedocext(dox,(struct Amsrcupdate *)amsg);
         break;
      case AOM_ADDCHILD:
         result=Addchilddocext(dox,(struct Amadd *)amsg);
         break;
      case AOM_DISPOSE:
         Disposedocext(dox);
         break;
      case AOM_DEINSTALL:
         Deinstalldocext();
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installdocext(void)
{  NEWLIST(&docexts);
   NEWLIST(&docrefs);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_DOCEXT,Dispatch)) return FALSE;
   return TRUE;
}

/* Return the source for this document extension. If NULL return, a
 * load for that file was started and the document was added to the
 * wait list.
 * If (UBYTE *)~0 return, the extension is in error. */
UBYTE *Finddocext(struct Document *doc,void *url,BOOL reload)
{  struct Docext *dox;
   struct Docext *found_dox=NULL;
   ULONG loadflags=AUMLF_DOCEXT;
   void *durl;
   void *furl=(void *)Agetattr(url,AOURL_Finalurlptr);
   UBYTE *urlstr;
   void *refererurl;
   void *loadframe;
   extern BOOL httpdebug;
   urlstr = (UBYTE *)Agetattr(url,AOURL_Url);
   if(httpdebug)
   {  AwebLog("fetch", "Finddocext: URL=%s, reload=%d", urlstr ? (char *)urlstr : "NULL", reload ? 1 : 0);
   }
   if(reload)
   {  loadflags|=AUMLF_RELOAD;
      if(httpdebug)
      {  AwebLog("fetch", "Finddocext: Reload requested, forcing fresh load");
      }
   }
   else
   {  for(dox=docexts.first;dox->next;dox=dox->next)
      {  durl=(void *)Agetattr(dox->url,AOURL_Finalurlptr);
         if(durl==furl)
         {  found_dox=dox;
            /* Check if we have valid data first - if EOF is set and buffer is valid,
             * use it even if ERROR flag is set (ERROR might have been set prematurely) */
            if((dox->flags&DOXF_EOF) && !(dox->flags&DOXF_LOADING) && dox->buf.buffer && dox->buf.length > 1)
            {  /* Valid buffer with EOF - clear ERROR flag if set and use the buffer */
               if(dox->flags&DOXF_ERROR)
               {  dox->flags&=~DOXF_ERROR;
               }
               if(httpdebug)
               {  AwebLog("cache", "Finddocext: HIT cached buffer length=%ld bytes",
                         dox->buf.length);
               }
               return dox->buf.buffer;
            }
            if(dox->flags&DOXF_ERROR)
            {  /* ERROR flag set and no valid buffer - retry */
               dox->flags&=~DOXF_ERROR;
               dox->flags&=~DOXF_EOF;
               dox->flags&=~DOXF_LOADING;
               Freebuffer(&dox->buf);
               /* Set LOADING flag immediately to prevent race conditions */
               dox->flags|=DOXF_LOADING;
               /* Break out to start loading - don't fall through */
               break;
            }
            /* Only return cached buffer if it's complete (EOF reached) and valid.
             * After a reload, the buffer is freed and DOXF_EOF is cleared, so
             * we need to reload it instead of returning an invalid buffer.
             * Also check that buffer has meaningful content (more than just null terminator). */
            if((dox->flags&DOXF_EOF) && !(dox->flags&DOXF_ERROR) && dox->buf.buffer && dox->buf.length > 1)
            {  if(httpdebug)
               {  AwebLog("cache", "Finddocext: HIT cached buffer length=%ld bytes",
                         dox->buf.length);
               }
               return dox->buf.buffer;
            }
            /* If EOF is set but buffer is invalid (empty or just null terminator), the previous
             * load completed but failed. Clear EOF, set RETRY flag, and return NULL.
             * The RETRY flag prevents immediate retry from callbacks, but allows retry from
             * natural flow (e.g., Dolink) when RETRY is set and LOADING is not. */
            if((dox->flags&DOXF_EOF) && (!dox->buf.buffer || dox->buf.length <= 1))
            {  /* Clear EOF and set RETRY flag to indicate we need to retry */
               dox->flags&=~DOXF_EOF;
               if(dox->flags&DOXF_ERROR) dox->flags&=~DOXF_ERROR;
               dox->flags|=DOXF_RETRY;
               if(dox->buf.buffer) Freebuffer(&dox->buf);
               if(httpdebug)
               {  AwebLog("fetch", "Finddocext: Load completed with invalid buffer, clearing EOF, setting RETRY flag, returning NULL");
               }
               return NULL;
            }
            /* If RETRY flag is set, we need to retry. If LOADING is also set, wait for current load.
             * If LOADING is not set, we can retry (either from callback or natural flow). */
            if(dox->flags&DOXF_RETRY)
            {  if(dox->flags&DOXF_LOADING)
               {  /* RETRY flag set but LOADING also set - current load in progress, wait for it */
                  struct Docref *dr;
                  void *durl;
                  void *furl=(void *)Agetattr(url,AOURL_Finalurlptr);
                  BOOL already_waiting=FALSE;
                  for(dr=docrefs.first;dr->next;dr=dr->next)
                  {  durl=(void *)Agetattr(dr->url,AOURL_Finalurlptr);
                     if(durl==furl && dr->doc==doc)
                     {  already_waiting=TRUE;
                        break;
                     }
                  }
                  if(!already_waiting)
                  {  if(httpdebug)
                     {  void *urlstr;
                        urlstr=(void *)Agetattr(url,AOURL_Url);
                        AwebLog("fetch", "Finddocext: RETRY flag set but LOADING also set, adding to wait list for URL=%s",
                               urlstr ? (char *)urlstr : "NULL");
                     }
                     Addwaitingdoc(doc,url);
                  }
                  return NULL;
               }
               /* RETRY flag set and no LOADING - retry the load */
               if(httpdebug)
               {  void *urlstr;
                  urlstr=(void *)Agetattr(url,AOURL_Url);
                  AwebLog("fetch", "Finddocext: RETRY flag set (flags=0x%04x), clearing RETRY, starting retry load for URL=%s",
                         dox->flags, urlstr ? (char *)urlstr : "NULL");
               }
               dox->flags&=~DOXF_RETRY;
               dox->flags|=DOXF_LOADING;    /* Set LOADING for the new load */
               if(dox->buf.buffer) Freebuffer(&dox->buf);
               /* Break out to start loading - LOADING is already set */
               break;
            }
            /* If a load is already in progress, don't start another one - just wait for it to complete.
             * But check if this document is already in the wait list to avoid adding it multiple times
             * (which would cause multiple callbacks and infinite loops). */
            if(dox->flags&DOXF_LOADING)
            {  /* Check if this document is already waiting for this URL */
               struct Docref *dr;
               void *durl;
               void *furl=(void *)Agetattr(url,AOURL_Finalurlptr);
               BOOL already_waiting=FALSE;
               for(dr=docrefs.first;dr->next;dr=dr->next)
               {  durl=(void *)Agetattr(dr->url,AOURL_Finalurlptr);
                  if(durl==furl && dr->doc==doc)
                  {  already_waiting=TRUE;
                     break;
                  }
               }
               if(!already_waiting)
               {  if(httpdebug)
                  {  void *urlstr;
                     urlstr=(void *)Agetattr(url,AOURL_Url);
                     AwebLog("fetch", "Finddocext: Cached entry found but load already in progress (flags=0x%04x), adding to wait list for URL=%s",
                            dox->flags, urlstr ? (char *)urlstr : "NULL");
                  }
                  Addwaitingdoc(doc,url);
               }
               else if(httpdebug)
               {  void *urlstr;
                  urlstr=(void *)Agetattr(url,AOURL_Url);
                  AwebLog("fetch", "Finddocext: Cached entry found but load already in progress (flags=0x%04x), already in wait list for URL=%s",
                         dox->flags, urlstr ? (char *)urlstr : "NULL");
               }
               return NULL;
            }
            if(httpdebug)
            {  AwebLog("fetch", "Finddocext: Cached entry found but buffer not ready (EOF=%d, buffer=%p, length=%ld), loading fresh",
                      (dox->flags&DOXF_EOF) ? 1 : 0, dox->buf.buffer, dox->buf.length);
            }
            /* Buffer not ready yet, break out to start loading */
            break;
         }
      }
      /* If we found an entry that needs loading and LOADING flag isn't already set,
       * set it before starting the load. (It may already be set if we cleared ERROR above) */
      if(found_dox && !(found_dox->flags&DOXF_LOADING))
      {  found_dox->flags|=DOXF_LOADING;
      }
   }
   Addwaitingdoc(doc,url);
   if(httpdebug)
   {  void *urlstr;
      urlstr=(void *)Agetattr(url,AOURL_Url);
      AwebLog("fetch", "Finddocext: Calling Auload for URL=%s, loadflags=0x%04lx, found_dox=%p",
             urlstr ? (char *)urlstr : "NULL", loadflags, found_dox);
   }
   refererurl = NULL;
   loadframe = NULL;
   if(doc)
   {  loadframe = doc->frame;
      if(doc->copy)
      {  refererurl = (void *)Agetattr(doc->copy, AOCPY_Url);
      }
   }
   Auload(url, loadflags, refererurl, NULL, loadframe);
   return NULL;
}

