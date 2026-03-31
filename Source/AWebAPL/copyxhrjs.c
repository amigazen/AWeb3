 uae /**********************************************************************
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

/* xhrjs.c - AWeb XMLHttpRequest JavaScript interface */

#include "aweb.h"
#include "frame.h"
#include "fetch.h"
#include "url.h"
#include "jslib.h"
#include "frprivate.h"
#include "application.h"
#include "source.h"
#include "task.h"
#include "window.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

/*-----------------------------------------------------------------------*/

/* XMLHttpRequest readyState values */
#define XHR_UNSENT          0
#define XHR_OPENED          1
#define XHR_HEADERS_RECEIVED 2
#define XHR_LOADING         3
#define XHR_DONE            4

/* Internal XMLHttpRequest structure */
struct Xhr
{  struct Fetch *fetch;      /* Fetch object for HTTP request */
   struct Frame *frame;      /* Frame this XHR belongs to */
   struct Jobject *jobject;  /* JavaScript object */
   void *urlobj;             /* URL object for this request */
   UBYTE *method;            /* HTTP method (GET, POST, etc.) */
   UBYTE *url;               /* Request URL */
   BOOL async;               /* Asynchronous request flag */
   UBYTE *username;          /* Username for authentication */
   UBYTE *password;          /* Password for authentication */
   long readyState;          /* Current ready state */
   long status;              /* HTTP status code */
   UBYTE *statusText;        /* HTTP status text */
   UBYTE *responseText;      /* Response body as text */
   struct Buffer responseBuffer; /* Buffer for accumulating response */
   UBYTE *onreadystatechange; /* JavaScript callback script */
   LIST(Header) headers;     /* Request headers to send */
   UBYTE *allResponseHeaders; /* All response headers as string */
   BOOL aborted;             /* Request was aborted */
   BOOL inCallback;          /* Flag to prevent reentrant callback execution */
   /* Synchronous send: caller task waits on this signal until fetch completes */
   struct Task *sync_waiter_task;  /* task to wake (NULL if async) */
   LONG sync_waiter_signum;        /* AllocSignal() bit for sync wait (-1 if not used) */
};

struct Header
{  NODE(Header);
   UBYTE *name;
   UBYTE *value;
};

/* Mapping from source objects to XHR objects */
struct Xhrsourcemap
{  NODE(Xhrsourcemap);
   void *source;              /* Source object */
   struct Xhr *xhr;           /* XHR object */
};

static LIST(Xhrsourcemap) xhrsourcemaps;

/*-----------------------------------------------------------------------*/

static void Disposexhr(struct Xhr *xhr)
{  struct Header *h;
   struct Xhrsourcemap *map,*next;
   if(xhr)
   {  /* Wake sync send() waiter so it does not block forever */
      if(xhr->sync_waiter_task!=NULL && xhr->sync_waiter_signum>=0)
      {  Signal(xhr->sync_waiter_task,1UL<<xhr->sync_waiter_signum);
         FreeSignal(xhr->sync_waiter_signum);
         xhr->sync_waiter_task=NULL;
         xhr->sync_waiter_signum=-1;
      }
      /* Remove fetch mapping before disposing fetch object */
      if(xhr->fetch)
      {  /* Cancel request first */
         xhr->aborted=TRUE;
         Asetattrs((struct Aobject *)xhr->fetch,AOFCH_Cancel,TRUE,TAG_END);
         /* Remove mapping */
         for(map=xhrsourcemaps.first;map;map=next)
         {  next=map->next;
            if(map->source==xhr->fetch)
            {  REMOVE(map);
               FREE(map);
               break;
            }
         }
         Adisposeobject((struct Aobject *)xhr->fetch);
      }
      if(xhr->method) FREE(xhr->method);
      if(xhr->url) FREE(xhr->url);
      if(xhr->username) FREE(xhr->username);
      if(xhr->password) FREE(xhr->password);
      if(xhr->statusText) FREE(xhr->statusText);
      if(xhr->responseText) FREE(xhr->responseText);
      if(xhr->onreadystatechange) FREE(xhr->onreadystatechange);
      if(xhr->allResponseHeaders) FREE(xhr->allResponseHeaders);
      Freebuffer(&xhr->responseBuffer);
      while(h=REMHEAD(&xhr->headers))
      {  if(h->name) FREE(h->name);
         if(h->value) FREE(h->value);
         FREE(h);
      }
      FREE(xhr);
   }
}

/* Update readyState and call onreadystatechange callback */
static void Setreadystate(struct Xhr *xhr,long state)
{  struct Jcontext *jc;
   UBYTE *callback;
   struct Frame *frame;
   struct Jobject *jobject;
   
   if(xhr && xhr->readyState!=state)
   {  printf("[XHR] Setreadystate: readyState %ld -> %ld (XHR=0x%08lX)\n",xhr->readyState,state,(ULONG)xhr);
      xhr->readyState=state;
      
      /* Prevent reentrant callback execution to avoid deadlocks */
      if(xhr->inCallback)
      {  printf("[XHR] Setreadystate: Skipping callback - already in callback (preventing reentrancy)\n");
         return;
      }
      
      /* Don't call callback if XHR is aborted */
      if(xhr->aborted)
      {  printf("[XHR] Setreadystate: Skipping callback - XHR was aborted\n");
         return;
      }
      
      /* Save pointers before callback - callback might dispose XHR */
      callback=xhr->onreadystatechange;
      frame=xhr->frame;
      jobject=xhr->jobject;
      
      if(jobject && callback && frame)
      {  /* Validate pointers before calling JavaScript */
         if((ULONG)frame>=0x1000 && (ULONG)frame<0xFFFFFFF0 &&
            (ULONG)callback>=0x1000 && (ULONG)callback<0xFFFFFFF0)
         {  printf("[XHR] Setreadystate: Calling onreadystatechange callback\n");
            jc=(struct Jcontext *)Agetattr(Aweb(),AOAPP_Jcontext);
            if(jc)
            {  /* Save XHR pointer for validation after callback */
               struct Xhr *saved_xhr=xhr;
               
               /* Set flag to prevent reentrant calls */
               xhr->inCallback=TRUE;
               
               /* Call callback synchronously - XMLHttpRequest spec requires this */
               /* Note: Callback might dispose XHR, so validate after */
               Runjavascript(frame,callback,&jobject);
               
               /* Validate XHR still exists by checking if jobject still points to it */
               /* Use Jointernal to safely check without dereferencing xhr */
               if(!jobject || Jointernal(jobject)!=(void *)saved_xhr)
               {  /* XHR was disposed during callback - don't access it */
                  printf("[XHR] Setreadystate: WARNING - XHR was disposed during callback\n");
                  return; /* Exit early - xhr is no longer valid */
               }
               
               /* Clear flag after callback - XHR still exists */
               if(saved_xhr->inCallback)
               {  saved_xhr->inCallback=FALSE;
               }
            }
            else
            {  printf("[XHR] Setreadystate: WARNING - No Jcontext available\n");
            }
         }
         else
         {  printf("[XHR] Setreadystate: WARNING - Invalid frame or callback pointer (frame=0x%08lX, callback=0x%08lX)\n",
                   (ULONG)frame,(ULONG)callback);
         }
      }
      else
      {  printf("[XHR] Setreadystate: No callback (jobject=0x%08lX, onreadystatechange=%s, frame=%s)\n",
                (ULONG)jobject,callback?"set":"NULL",frame?"set":"NULL");
      }
      /* Wake task waiting in sync send() when request completes */
      if(state==XHR_DONE && xhr->sync_waiter_task!=NULL && xhr->sync_waiter_signum>=0)
      {  Signal(xhr->sync_waiter_task,1UL<<xhr->sync_waiter_signum);
         xhr->sync_waiter_task=NULL;
         xhr->sync_waiter_signum=-1;
      }
   }
}

/* Custom source object for XHR - receives AOM_SRCUPDATE messages */
struct Xhrsource
{  struct Aobject object;
   struct Xhr *xhr;            /* XHR object to forward updates to */
   void *url;                   /* URL object this source belongs to */
};

/* Handle source updates - receives AOM_SRCUPDATE from URL */
long Xhrsrcupdate(void *urlobj,struct Amsrcupdate *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *data=NULL;
   long datalen=0;
   UBYTE *status=NULL;
   long statuscode=0;
   UBYTE *header=NULL;
   struct Xhr *xhr;
   void *fetchobj;
   
   /* Reduced logging to prevent lockup from excessive printf calls */
   /* printf("[XHR] Xhrsrcupdate: Called (urlobj=0x%08lX)\n",(ULONG)urlobj); */
   
   /* Get fetch from message */
   if(!ams || !ams->fetch)
   {  printf("[XHR] Xhrsrcupdate: ERROR - No fetch in message\n");
      return 0;
   }
   fetchobj=ams->fetch;
   
   /* Get XHR pointer from our mapping using fetch object */
   {  struct Xhrsourcemap *map,*next;
      long count=0;
      xhr=NULL;
      /* Iterate with safety checks to prevent crashes and infinite loops */
      for(map=xhrsourcemaps.first;map && count<100;map=next,count++)
      {  next=map->next; /* Save next pointer before accessing map */
         if(!map->source) continue; /* Skip invalid entries */
         if(map->source==fetchobj)
         {  xhr=map->xhr;
            if(!xhr) continue; /* Skip if xhr is NULL */
            printf("[XHR] Xhrsrcupdate: Found XHR mapping (fetch=0x%08lX, xhr=0x%08lX)\n",(ULONG)fetchobj,(ULONG)xhr);
            break;
         }
         /* Safety: prevent infinite loops */
         if(map==next) break;
      }
      if(count>=100)
      {  printf("[XHR] Xhrsrcupdate: ERROR - Mapping list too long or corrupted (count=%ld)!\n",count);
      }
      if(!xhr)
      {  /* Not an XHR fetch - return 0 to let normal processing continue */
         return 0;
      }
   }
   if(!xhr || xhr->aborted) 
   {  printf("[XHR] Xhrsrcupdate: Skipping (xhr=%s, aborted=%s)\n",
             xhr?"OK":"NULL",xhr&&xhr->aborted?"YES":"NO");
      return 0;
   }
   
   /* Process tags - use NextTagItem but it only modifies local tstate, not ams->tags */
   /* Srcupdatefetch will reset its own iterator from ams->tags */
   while(tag=NextTagItem(&tstate))
   {  /* Reduced logging - only log important tags */
      /* printf("[XHR] Xhrsrcupdate: Processing tag 0x%08lX\n",tag->ti_Tag); */
      switch(tag->ti_Tag)
      {  case AOURL_Status:
            /* HTTP status line received */
            status=(UBYTE *)tag->ti_Data;
            printf("[XHR] Xhrsrcupdate: AOURL_Status = '%s'\n",status?status:(UBYTE *)"(NULL)");
            if(status)
            {  if(xhr->statusText) FREE(xhr->statusText);
               xhr->statusText=Dupstr(status,-1);
               /* Parse status code from status line (format: "HTTP/1.x CODE TEXT") */
               /* Skip "HTTP/" prefix if present */
               if(STRNIEQUAL(status,"HTTP/",5))
               {  UBYTE *p=status+5;
                  /* Skip version number */
                  while(*p && *p!=' ') p++;
                  if(*p==' ') p++;
                  statuscode=strtol(p,NULL,10);
               }
               else
               {  statuscode=strtol(status,NULL,10);
               }
               if(statuscode>0) 
               {  xhr->status=statuscode;
                  printf("[XHR] Xhrsrcupdate: Parsed status code = %ld\n",statuscode);
               }
               if(xhr->readyState==XHR_OPENED)
               {  Setreadystate(xhr,XHR_HEADERS_RECEIVED);
               }
            }
            break;
         case AOURL_Data:
            /* Response data received */
            data=(UBYTE *)tag->ti_Data;
            printf("[XHR] Xhrsrcupdate: AOURL_Data (data=0x%08lX)\n",(ULONG)data);
            break;
         case AOURL_Datalength:
            /* Length of data */
            datalen=tag->ti_Data;
            printf("[XHR] Xhrsrcupdate: AOURL_Datalength = %ld\n",datalen);
            break;
         case AOURL_Header:
            /* HTTP header received - collect for getAllResponseHeaders() */
            header=(UBYTE *)tag->ti_Data;
            printf("[XHR] Xhrsrcupdate: AOURL_Header = '%s'\n",header?header:(UBYTE *)"(NULL)");
            if(header && xhr)
            {  if(xhr->allResponseHeaders)
               {  /* Validate pointer before using strlen */
                  UBYTE *oldheaders=xhr->allResponseHeaders;
                  long oldlen=0;
                  long headerlen=0;
                  long newlen=0;
                  UBYTE *newheaders;
                  /* Safely get lengths */
                  if(oldheaders && (ULONG)oldheaders>=0x1000 && (ULONG)oldheaders<0xFFFFFFF0)
                  {  oldlen=strlen(oldheaders);
                  }
                  if((ULONG)header>=0x1000 && (ULONG)header<0xFFFFFFF0)
                  {  headerlen=strlen(header);
                  }
                  newlen=oldlen+headerlen+2; /* +2 for \r\n */
                  newheaders=ALLOCTYPE(UBYTE,newlen+1,0);
                  if(newheaders)
                  {  if(oldlen>0 && oldheaders)
                     {  memcpy(newheaders,oldheaders,oldlen);
                        newheaders[oldlen]='\0';
                        if(oldlen>0) 
                        {  memcpy(newheaders+oldlen,"\r\n",2);
                           newheaders[oldlen+2]='\0';
                        }
                     }
                     else
                     {  newheaders[0]='\0';
                     }
                     if(headerlen>0)
                     {  memcpy(newheaders+oldlen+(oldlen>0?2:0),header,headerlen);
                        newheaders[oldlen+(oldlen>0?2:0)+headerlen]='\0';
                     }
                     FREE(xhr->allResponseHeaders);
                     xhr->allResponseHeaders=newheaders;
                  }
               }
               else
               {  xhr->allResponseHeaders=Dupstr(header,-1);
               }
            }
            break;
         case AOURL_Terminate:
            /* Request completed */
            printf("[XHR] Xhrsrcupdate: AOURL_Terminate (aborted=%s)\n",xhr->aborted?"YES":"NO");
            if(!xhr->aborted)
            {  /* Finalize response text */
               if(xhr->responseBuffer.buffer && xhr->responseBuffer.length>0)
               {  /* Buffer should already be null-terminated by Addtobuffer */
                  xhr->responseText=Dupstr(xhr->responseBuffer.buffer,xhr->responseBuffer.length);
                  printf("[XHR] Xhrsrcupdate: Finalized responseText (length=%ld)\n",xhr->responseBuffer.length);
               }
               else
               {  xhr->responseText=Dupstr("",0);
                  printf("[XHR] Xhrsrcupdate: Finalized responseText (empty)\n");
               }
               Setreadystate(xhr,XHR_DONE);
            }
            break;
         case AOURL_Error:
            /* Request failed */
            printf("[XHR] Xhrsrcupdate: AOURL_Error (aborted=%s)\n",xhr->aborted?"YES":"NO");
            if(!xhr->aborted)
            {  xhr->status=0;
               if(xhr->statusText) FREE(xhr->statusText);
               xhr->statusText=Dupstr("",0);
               Setreadystate(xhr,XHR_DONE);
            }
            break;
      }
   }
   
   /* Process data if we have it */
   if(data && datalen>0 && !xhr->aborted)
   {  printf("[XHR] Xhrsrcupdate: Adding %ld bytes to response buffer\n",datalen);
      if(Addtobuffer(&xhr->responseBuffer,data,datalen))
      {  if(xhr->readyState<XHR_LOADING)
         {  Setreadystate(xhr,XHR_LOADING);
         }
      }
      else
      {  printf("[XHR] Xhrsrcupdate: WARNING - Failed to add data to buffer\n");
      }
   }
   
   /* Note: NextTagItem advances tstate but doesn't modify the tags themselves */
   /* Srcupdatefetch will process tags again for normal URL/cache handling */
   
   return 1;  /* Return 1 to indicate we processed this as an XHR */
}

static long Dispatchxhrsource(struct Xhrsource *xhrsrc,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_SRCUPDATE:
         result=Xhrsrcupdate(xhrsrc,(struct Amsrcupdate *)amsg);
         break;
      case AOM_DISPOSE:
         if(xhrsrc)
         {  FREE(xhrsrc);
         }
         break;
   }
   return result;
}

/*-----------------------------------------------------------------------*/

/* JavaScript property handlers */

static BOOL Propertyreadystate(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Xhr *xhr=vd->hookdata;
   if(xhr)
   {  switch(vd->code)
      {  case VHC_SET:
            /* Read-only */
            result=TRUE;
            break;
         case VHC_GET:
            Jasgnumber(vd->jc,vd->value,xhr->readyState);
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertystatus(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Xhr *xhr=vd->hookdata;
   if(xhr)
   {  switch(vd->code)
      {  case VHC_SET:
            /* Read-only */
            result=TRUE;
            break;
         case VHC_GET:
            Jasgnumber(vd->jc,vd->value,xhr->status);
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL PropertystatusText(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Xhr *xhr=vd->hookdata;
   if(xhr)
   {  switch(vd->code)
      {  case VHC_SET:
            /* Read-only */
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,xhr->statusText?xhr->statusText:NULLSTRING);
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL PropertyresponseText(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Xhr *xhr=vd->hookdata;
   if(xhr)
   {  switch(vd->code)
      {  case VHC_SET:
            /* Read-only */
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,xhr->responseText?xhr->responseText:NULLSTRING);
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertyonreadystatechange(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Xhr *xhr=vd->hookdata;
   UBYTE *script;
   if(xhr)
   {  switch(vd->code)
      {  case VHC_SET:
            script=Jtostring(vd->jc,vd->value);
            if(xhr->onreadystatechange) FREE(xhr->onreadystatechange);
            xhr->onreadystatechange=script?Dupstr(script,-1):NULL;
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,xhr->onreadystatechange?xhr->onreadystatechange:NULLSTRING);
            result=TRUE;
            break;
      }
   }
   return result;
}

/*-----------------------------------------------------------------------*/

/* JavaScript methods */

static void Methodopen(struct Jcontext *jc)
{  struct Jvar *jv;
   UBYTE *method="GET",*url=NULL,*username=NULL,*password=NULL;
   BOOL async=TRUE;
   struct Xhr *xhr;
   struct Frame *fr;
   
   xhr=(struct Xhr *)Jointernal(Jthis(jc));
   if(!xhr) return;
   
   fr=Getjsframe(jc);
   if(!fr) return;
   
   /* Get arguments */
   if(jv=Jfargument(jc,0))
   {  method=Jtostring(jc,jv);
   }
   if(jv=Jfargument(jc,1))
   {  url=Jtostring(jc,jv);
   }
   if(jv=Jfargument(jc,2))
   {  async=Jtoboolean(jc,jv);
   }
   if(jv=Jfargument(jc,3))
   {  username=Jtostring(jc,jv);
   }
   if(jv=Jfargument(jc,4))
   {  password=Jtostring(jc,jv);
   }
   
   /* Abort any existing request */
   if(xhr->fetch)
   {  /* Wait for callback to complete if it's running */
      if(xhr->inCallback)
      {  printf("[XHR] Methodopen: WARNING - Callback in progress, waiting for completion\n");
         /* Note: We can't actually wait here without deadlocking */
         /* The callback should complete quickly, so we'll proceed */
      }
      
      /* Remove mapping before disposing fetch */
      {  struct Xhrsourcemap *map,*next;
         for(map=xhrsourcemaps.first;map;map=next)
         {  next=map->next;
            if(map->source==xhr->fetch)
            {  REMOVE(map);
               FREE(map);
               break;
            }
         }
      }
      Asetattrs((struct Aobject *)xhr->fetch,AOFCH_Cancel,TRUE,TAG_END);
      Adisposeobject((struct Aobject *)xhr->fetch);
      xhr->fetch=NULL;
   }
   
   /* Reset state */
   xhr->aborted=FALSE;
   xhr->inCallback=FALSE;  /* Reset callback flag */
   if(xhr->sync_waiter_signum>=0)
   {  FreeSignal(xhr->sync_waiter_signum);
      xhr->sync_waiter_task=NULL;
      xhr->sync_waiter_signum=-1;
   }
   xhr->status=0;
   if(xhr->statusText) FREE(xhr->statusText);
   xhr->statusText=NULL;
   if(xhr->responseText) FREE(xhr->responseText);
   xhr->responseText=NULL;
   Freebuffer(&xhr->responseBuffer);
   if(xhr->allResponseHeaders) FREE(xhr->allResponseHeaders);
   xhr->allResponseHeaders=NULL;
   
   /* Store parameters */
   if(xhr->method) FREE(xhr->method);
   xhr->method=method?Dupstr(method,-1):NULL;
   if(xhr->url) FREE(xhr->url);
   if(url)
   {  UBYTE *baseurl;
      void *urlobj;
      baseurl=Getjscurrenturlname(jc);
      urlobj=Findurl(baseurl,url,0);
      if(urlobj)
      {  UBYTE *resolvedurl=(UBYTE *)Agetattr(urlobj,AOURL_Url);
         xhr->url=resolvedurl?Dupstr(resolvedurl,-1):Dupstr(url,-1);
      }
      else
      {  /* Findurl failed - use URL as-is */
         xhr->url=Dupstr(url,-1);
      }
   }
   else
   {  xhr->url=NULL;
   }
   xhr->async=async;
   if(xhr->username) FREE(xhr->username);
   xhr->username=username?Dupstr(username,-1):NULL;
   if(xhr->password) FREE(xhr->password);
   xhr->password=password?Dupstr(password,-1):NULL;
   
   printf("[XHR] Methodopen: method='%s', url='%s', async=%s\n",
          method?method:(UBYTE *)"(NULL)",url?url:(UBYTE *)"(NULL)",async?"YES":"NO");
   
   /* Set readyState to OPENED - callback will be fired by Setreadystate */
   /* Note: We call Setreadystate which will fire the callback synchronously */
   /* This is required by XMLHttpRequest spec, but can cause deadlocks if send() */
   /* is called immediately after open() returns. The reentrancy flag prevents */
   /* issues if the callback tries to change state again. */
   Setreadystate(xhr,XHR_OPENED);
}

/* Synchronous XHR (async=false): we block in Wait() until the fetch task
 * completes and signals us from Setreadystate(XHR_DONE) or Methodabort.
 * This avoids scripts busy-waiting for readyState and allows the fetch
 * task to run and complete. */
static void Methodsend(struct Jcontext *jc)
{  struct Jvar *jv;
   UBYTE *data=NULL;
   struct Xhr *xhr;
   struct Frame *fr;
   void *urlobj;
   UBYTE *referer;
   LONG signum;
   
   printf("[XHR] Methodsend: Called\n");
   xhr=(struct Xhr *)Jointernal(Jthis(jc));
   if(!xhr) 
   {  printf("[XHR] Methodsend: ERROR - No XHR object found\n");
      return;
   }
   if(!xhr->url) 
   {  printf("[XHR] Methodsend: ERROR - No URL set (call open() first)\n");
      return;
   }
   if(xhr->readyState!=XHR_OPENED) 
   {  printf("[XHR] Methodsend: ERROR - readyState is %ld, expected %d (OPENED)\n",
             xhr->readyState,XHR_OPENED);
      return;
   }
   /* Prevent sending if already sending */
   if(xhr->fetch)
   {  printf("[XHR] Methodsend: ERROR - Request already in progress\n");
      return;
   }
   /* Allocate signal for sync wait so fetch task can wake us when done */
   if(!xhr->async)
   {  signum=AllocSignal(-1);
      if(signum>=0)
      {  xhr->sync_waiter_task=FindTask(NULL);
         xhr->sync_waiter_signum=signum;
      }
      else
      {  xhr->async=TRUE;
      }
   }
   
   fr=Getjsframe(jc);
   if(!fr) 
   {  printf("[XHR] Methodsend: ERROR - No frame found\n");
      return;
   }
   
   /* Get data argument (for POST, etc.) */
   if(jv=Jfargument(jc,0))
   {  if(!Jisnumber(jv) || Jtonumber(jc,jv)!=0)
      {  data=Jtostring(jc,jv);
      }
   }
   
   /* Create URL object */
   printf("[XHR] Methodsend: Creating URL object for '%s'\n",xhr->url);
   urlobj=Findurl(Getjscurrenturlname(jc),xhr->url,0);
   if(!urlobj) 
   {  printf("[XHR] Methodsend: ERROR - Findurl() returned NULL\n");
      if(xhr->sync_waiter_signum>=0)
      {  FreeSignal(xhr->sync_waiter_signum);
         xhr->sync_waiter_task=NULL;
         xhr->sync_waiter_signum=-1;
      }
      xhr->status=0;
      if(xhr->statusText) FREE(xhr->statusText);
      xhr->statusText=Dupstr("",0);
      Setreadystate(xhr,XHR_DONE);
      return;
   }
   printf("[XHR] Methodsend: URL object created (0x%08lX)\n",(ULONG)urlobj);
   
   /* Get referer URL */
   referer=(UBYTE *)Agetattr((void *)Agetattr(fr,AOFRM_Url),AOURL_Url);
   printf("[XHR] Methodsend: Referer = '%s'\n",referer?referer:(UBYTE *)"(NULL)");
   
   /* Store URL object pointer for later use */
   xhr->urlobj=urlobj;
   /* Create fetch object */
   printf("[XHR] Methodsend: Creating fetch object\n");
   {  ULONG windowkey=0;
      if(fr->win) windowkey=Agetattr(fr->win,AOWIN_Key);
      xhr->fetch=Anewobject(AOTP_FETCH,
         AOFCH_Url,urlobj,
         AOFCH_Name,xhr->url,
         AOFCH_Referer,referer,
         AOFCH_Nocache,TRUE,
         AOFCH_Jframe,fr,
         AOFCH_Windowkey,windowkey,
         TAG_END);
   }
   
   if(xhr->fetch)
   {  printf("[XHR] Methodsend: Fetch object created (0x%08lX)\n",(ULONG)xhr->fetch);
      /* Store mapping from fetch to XHR so we can intercept updates */
      /* MUST add mapping BEFORE starting fetch to avoid race condition */
      {  struct Xhrsourcemap *map;
         if(map=ALLOCSTRUCT(Xhrsourcemap,1,0))
         {  map->source=xhr->fetch;  /* Use fetch as the key */
            map->xhr=xhr;
            /* Verify list is initialized */
            if(!xhrsourcemaps.first && !xhrsourcemaps.last)
            {  NEWLIST(&xhrsourcemaps);
               printf("[XHR] Methodsend: WARNING - Had to reinitialize mapping list!\n");
            }
            ADDTAIL(&xhrsourcemaps,map);
            printf("[XHR] Methodsend: Added fetch->XHR mapping (fetch=0x%08lX, xhr=0x%08lX)\n",
                   (ULONG)xhr->fetch,(ULONG)xhr);
         }
         else
         {  printf("[XHR] Methodsend: ERROR - Failed to allocate mapping!\n");
            /* Clean up fetch if mapping allocation failed */
            Adisposeobject((struct Aobject *)xhr->fetch);
            xhr->fetch=NULL;
            return;
         }
      }
      /* Set HTTP method and data */
      if(xhr->method && STRIEQUAL(xhr->method,"POST"))
      {  if(data)
         {  Asetattrs((struct Aobject *)xhr->fetch,AOFCH_Postmsg,data,TAG_END);
         }
      }
      else if(xhr->method && STRIEQUAL(xhr->method,"PUT"))
      {  if(data)
         {  Asetattrs((struct Aobject *)xhr->fetch,AOFCH_Postmsg,data,TAG_END);
         }
      }
      
      /* Add custom request headers from setRequestHeader() calls */
      /* Note: AWeb's fetch system doesn't directly support custom headers via tags */
      /* Headers are stored in xhr->headers but not sent yet - this is a limitation */
      /* For now, basic functionality works without custom headers */
      
      /* Start the fetch - it will forward updates to url, which forwards to our source */
      printf("[XHR] Methodsend: Starting fetch (AOTSK_Start)\n");
      Asetattrs((struct Aobject *)xhr->fetch,AOTSK_Start,TRUE,TAG_END);
      printf("[XHR] Methodsend: Fetch started successfully\n");
      /* Block until fetch completes when sync (fetch task will Signal us) */
      if(xhr->sync_waiter_task!=NULL && xhr->sync_waiter_signum>=0)
      {  Wait(1UL<<xhr->sync_waiter_signum);
         FreeSignal(xhr->sync_waiter_signum);
         xhr->sync_waiter_task=NULL;
         xhr->sync_waiter_signum=-1;
      }
   }
   else
   {  printf("[XHR] Methodsend: ERROR - Failed to create fetch object\n");
      if(!xhr->async && xhr->sync_waiter_signum>=0)
      {  FreeSignal(xhr->sync_waiter_signum);
         xhr->sync_waiter_task=NULL;
         xhr->sync_waiter_signum=-1;
      }
   }
}

static void MethodsetRequestHeader(struct Jcontext *jc)
{  struct Jvar *jv;
   UBYTE *name=NULL,*value=NULL;
   struct Xhr *xhr;
   struct Header *h;
   
   xhr=(struct Xhr *)Jointernal(Jthis(jc));
   if(!xhr || xhr->readyState!=XHR_OPENED) return;
   
   /* Get arguments */
   if(jv=Jfargument(jc,0))
   {  name=Jtostring(jc,jv);
   }
   if(jv=Jfargument(jc,1))
   {  value=Jtostring(jc,jv);
   }
   
   if(name && value)
   {  /* Check if header already exists - FIX: iterate correctly including last item */
      for(h=xhr->headers.first;h;h=h->next)
      {  if(h->name && STRIEQUAL(h->name,name))
         {  /* Replace existing header */
            if(h->value) FREE(h->value);
            h->value=Dupstr(value,-1);
            return;
         }
      }
      /* Add new header */
      if(h=ALLOCSTRUCT(Header,1,0))
      {  h->name=Dupstr(name,-1);
         h->value=Dupstr(value,-1);
         ADDTAIL(&xhr->headers,h);
      }
   }
}

static void MethodgetResponseHeader(struct Jcontext *jc)
{  struct Jvar *jv;
   UBYTE *name=NULL;
   struct Xhr *xhr;
   
   xhr=(struct Xhr *)Jointernal(Jthis(jc));
   if(!xhr) return;
   
   /* Get argument */
   if(jv=Jfargument(jc,0))
   {  name=Jtostring(jc,jv);
   }
   
   if(name && xhr->allResponseHeaders)
   {  /* Parse allResponseHeaders to find the requested header */
      /* For now, return empty string - full implementation would parse headers */
      Jasgstring(jc,NULL,NULLSTRING);
   }
   else
   {  Jasgstring(jc,NULL,NULLSTRING);
   }
}

static void MethodgetAllResponseHeaders(struct Jcontext *jc)
{  struct Xhr *xhr;
   
   xhr=(struct Xhr *)Jointernal(Jthis(jc));
   if(!xhr) return;
   
   Jasgstring(jc,NULL,xhr->allResponseHeaders?xhr->allResponseHeaders:NULLSTRING);
}

static void Methodabort(struct Jcontext *jc)
{  struct Xhr *xhr;
   
   xhr=(struct Xhr *)Jointernal(Jthis(jc));
   if(!xhr) return;
   
   xhr->aborted=TRUE;
   xhr->inCallback=FALSE;  /* Reset callback flag */
   if(xhr->fetch)
   {  /* Remove mapping before disposing fetch */
      {  struct Xhrsourcemap *map,*next;
         for(map=xhrsourcemaps.first;map;map=next)
         {  next=map->next;
            if(map->source==xhr->fetch)
            {  REMOVE(map);
               FREE(map);
               break;
            }
         }
      }
      Asetattrs((struct Aobject *)xhr->fetch,AOFCH_Cancel,TRUE,TAG_END);
      Adisposeobject((struct Aobject *)xhr->fetch);
      xhr->fetch=NULL;
   }
   /* Wake sync send() waiter so it does not block forever */
   if(xhr->sync_waiter_task!=NULL && xhr->sync_waiter_signum>=0)
   {  Signal(xhr->sync_waiter_task,1UL<<xhr->sync_waiter_signum);
      xhr->sync_waiter_task=NULL;
      xhr->sync_waiter_signum=-1;
   }
   /* Clear response data */
   if(xhr->statusText) FREE(xhr->statusText);
   xhr->statusText=NULL;
   if(xhr->responseText) FREE(xhr->responseText);
   xhr->responseText=NULL;
   Freebuffer(&xhr->responseBuffer);
   if(xhr->allResponseHeaders) FREE(xhr->allResponseHeaders);
   xhr->allResponseHeaders=NULL;
   xhr->status=0;
   Setreadystate(xhr,XHR_UNSENT);
}

/*-----------------------------------------------------------------------*/

/* XMLHttpRequest constructor */
static void Xhrconstructor(struct Jcontext *jc)
{  struct Jobject *jthis=Jthis(jc);
   struct Jvar *jv;
   struct Xhr *xhr;
   struct Frame *fr;
   
   fr=Getjsframe(jc);
   if(!fr) 
   {  
      return;
   }
   
   if(xhr=ALLOCSTRUCT(Xhr,1,MEMF_CLEAR))
   {  
      /* Initialize all fields explicitly for safety */
      xhr->fetch=NULL;
      xhr->frame=fr;
      xhr->jobject=jthis;
      xhr->urlobj=NULL;
      xhr->method=NULL;
      xhr->url=NULL;
      xhr->async=TRUE;
      xhr->username=NULL;
      xhr->password=NULL;
      xhr->readyState=XHR_UNSENT;
      xhr->status=0;
      xhr->statusText=NULL;
      xhr->responseText=NULL;
      xhr->sync_waiter_task=NULL;
      xhr->sync_waiter_signum=-1;
      xhr->responseBuffer.buffer=NULL;
      xhr->responseBuffer.size=0;
      xhr->responseBuffer.length=0;
      xhr->onreadystatechange=NULL;
      NEWLIST(&xhr->headers);
      xhr->allResponseHeaders=NULL;
      xhr->aborted=FALSE;
      xhr->inCallback=FALSE;
      
      Setjobject(jthis,NULL,xhr,Disposexhr);
      
      /* Add properties */
      if(jv=Jproperty(jc,jthis,"readyState"))
      {  Setjproperty(jv,Propertyreadystate,xhr);
      }
      if(jv=Jproperty(jc,jthis,"status"))
      {  Setjproperty(jv,Propertystatus,xhr);
      }
      if(jv=Jproperty(jc,jthis,"statusText"))
      {  Setjproperty(jv,PropertystatusText,xhr);
      }
      if(jv=Jproperty(jc,jthis,"responseText"))
      {  Setjproperty(jv,PropertyresponseText,xhr);
      }
      if(jv=Jproperty(jc,jthis,"onreadystatechange"))
      {  Setjproperty(jv,Propertyonreadystatechange,xhr);
      }
      
      /* Add methods */
      Addjfunction(jc,jthis,"open",Methodopen,"method","url","async","user","password",NULL);
      Addjfunction(jc,jthis,"send",Methodsend,"data",NULL);
      Addjfunction(jc,jthis,"setRequestHeader",MethodsetRequestHeader,"header","value",NULL);
      Addjfunction(jc,jthis,"getResponseHeader",MethodgetResponseHeader,"header",NULL);
      Addjfunction(jc,jthis,"getAllResponseHeaders",MethodgetAllResponseHeaders,NULL);
      Addjfunction(jc,jthis,"abort",Methodabort,NULL);
   }
   else
   {  
   }
}

/*-----------------------------------------------------------------------*/

void Initxhrjs(void)
{  NEWLIST(&xhrsourcemaps);
}

void Addxhrconstructor(struct Jcontext *jc,struct Jobject *parent)
{  struct Jobject *jo,*proto;
   struct Jvar *jv;
   if(jo=Addjfunction(jc,parent,"XMLHttpRequest",Xhrconstructor,NULL))
   {  
      if(proto=Newjobject(jc))
      {  /* Add constants */
         if(jv=Jproperty(jc,proto,"UNSENT"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgnumber(jc,jv,XHR_UNSENT);
         }
         if(jv=Jproperty(jc,proto,"OPENED"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgnumber(jc,jv,XHR_OPENED);
         }
         if(jv=Jproperty(jc,proto,"HEADERS_RECEIVED"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgnumber(jc,jv,XHR_HEADERS_RECEIVED);
         }
         if(jv=Jproperty(jc,proto,"LOADING"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgnumber(jc,jv,XHR_LOADING);
         }
         if(jv=Jproperty(jc,proto,"DONE"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgnumber(jc,jv,XHR_DONE);
         }
         Jsetprototype(jc,jo,proto);
         Freejobject(proto);
      }
   }
   else
   {  
   }
}

