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

/* url.c aweb url object */

#include "aweb.h"
#include "url.h"
#include "link.h"
#include "source.h"
#include "cache.h"
#include "fetch.h"
#include "window.h"
#include "xhrjs.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <string.h>

struct Url
{  struct Aobject object;
   UBYTE *url;                /* URL string */
   ULONG postnr;              /* POST id */
   USHORT flags;
   struct Url *movedto;       /* This URL is moved to (movedto) */
   struct Aobject *source;    /* Object source interpreter */
   struct Aobject *cache;     /* Object is in cache */
   struct Aobject *fetch;     /* Normal, new fetch */
   struct Aobject *ocache;    /* Old cache object before reload */
   struct Aobject *rfetch;    /* Reload from cache */
   struct Aobject *vfetch;    /* Verifying fetch */
   struct Aobject *vcache;    /* Preliminary cache for verified object */
   struct Aobject *vsource;   /* Preliminary source for verified object */
   struct Aobject *ssource;   /* Saveas source */
   struct Aobject *dsource;   /* Docext source */
   LIST(Child) childs;        /* Childs pointing to this URL */
   long loadnr;               /* Load sequence number */
};

#define URLF_VISITED    0x0001   /* Url is visited before */
#define URLF_TEMPMOVED  0x0002   /* Url was temporary moved, otherwise permanently */
#define URLF_CACHEABLE  0x0004   /* Url is cacheable */
#define URLF_VERIFIED   0x0008   /* Url has been validated in this session */
#define URLF_VOLATILE   0x0010   /* Never use existing copy */
#define URLF_DEXFETCH   0x0020   /* Current fetch is for docext, don't pass to source */
#define URLF_WASDUP     0x0040   /* Url was moved because it was duplicate */
#define URLF_VIEWSOURCE 0x0080   /* view-source: URL - render as plain text */
#define URLF_FETCHERROR 0x0100   /* Fetch failed: do not write body to url->cache (see Srcupdatefetch) */

struct Child
{  NODE(Child);
   struct Aobject *object;
   ULONG relation;
};

static UBYTE absurl[4096];

#define NRLISTS 64
#define HASHMASK  (NRLISTS-1)

static LIST(Url) urls[NRLISTS];
static long lastpostnr=0;
static long loadnr=0;
static long dupchecklist;

#define UQID_DUPCHECK   1

static struct Url *emptyurl;

static struct TagItem mapwindowtags[]=
{  AOURL_Status,AOWIN_Status,
   AOURL_Contentlength,AOWIN_Total,
   AOURL_Datatotal,AOWIN_Read,
   TAG_END
};

/*---------------------------------------------------------------------*/

/* This field is tested against to find beginning of fragemtn while building
 * absurl (is single-threaded anyway). For news: URLs, it is set to '\0',
 * for other URLs it is set to '#'. This is because news: urls can have
 * a # in their message ID and the URL shouldn't be cut off there. */
static UBYTE fraghash='#';

static long Urlschemelength(UBYTE *ptr)
{  UBYTE *p=ptr;
   if(*p==':') return 0;
   while(*p && isalnum(*p) || *p=='+' || *p=='.' || *p=='-') p++;
   if(*p==':') return p-ptr+1;
   else return 0;
}

static long Urlhostlength(UBYTE *ptr)
{  UBYTE *p=ptr;
   if(p[0]=='/' && p[1]=='/')
   {  p+=2;
      while(*p && *p!='/' && *p!=fraghash) p++;
      if(*p=='/' || *p=='\0') return p-ptr;
   }
   return 0;
}

static long Urlpathlength(UBYTE *ptr)
{  UBYTE *p=ptr;
   while(*p && *p!=';' && *p!='?' && *p!=fraghash) p++;
   return p-ptr;
}

static long Urlpathseglength(UBYTE *ptr)
{  UBYTE *p=ptr;
   while(*p && *p!='/' && *p!=';' && *p!='?' && *p!=fraghash) p++;
   if(*p=='/') return p-ptr+1;
   else return p-ptr;
}

static long Urlparamlength(UBYTE *ptr)
{  UBYTE *p=ptr;
   if(*p!=';') return 0;
   while(*p && *p!='?' && *p!=fraghash) p++;
   return p-ptr;
}

static long Urlquerylength(UBYTE *ptr)
{  UBYTE *p=ptr;
   if(*p!='?') return 0;
   while(*p && *p!=fraghash) p++;
   return p-ptr;
}

static long Urllength(UBYTE *ptr)
{  UBYTE *p=ptr;
   while(*p && *p!=fraghash) p++;
   return p-ptr;
}

static BOOL Removelastseg(UBYTE *ptr,BOOL localhost)
{  UBYTE *p=ptr+strlen(ptr)-1;
   if(p<ptr) return FALSE;
   if(*p=='/') p--;
   while(p>=ptr && *p!='/' && !(localhost && *p==':')) p--;
   p[1]='\0';
   return TRUE;
}

static void Urlcat(UBYTE *a,UBYTE *b,long len)
{  long al=strlen(a);
   if(al+len>4095) len=4095-al;
   if(len>0) strncpy(a+al,b,len);
   a[al+len]='\0';
}

static void Buildabsurl(UBYTE *base,UBYTE *rel)
{  long l,m;
   UBYTE *pathp;
   BOOL usebaseparam=FALSE,localhost=FALSE,hadhost=FALSE;
   memset(absurl,0,sizeof(absurl));
   /* remove leading spaces */
   while(*base==' ') base++;
   while(*rel==' ') rel++;
   /* if base is empty, use relurl */
   if(!*base)
   {  fraghash=(STRNIEQUAL(rel,"NEWS:",5))?'\0':'#';
      Urlcat(absurl,rel,Urllength(rel));
      return;
   }
   /* if rel starts with scheme != base scheme, it's absurl */
   /* if rel starts with "x-aweb:", it's absurl */
   if(l=Urlschemelength(rel))
   {  if(!STRNIEQUAL(rel,"x-aweb:",7) && STRNIEQUAL(rel,base,l)) rel+=l;
      else
      {  fraghash=(STRNIEQUAL(rel,"NEWS:",5))?'\0':'#';
         Urlcat(absurl,rel,Urllength(rel));
         return;
      }
   }
   /* inherit the scheme of base */
   l=Urlschemelength(base);
   if(l) Urlcat(absurl,base,l);
   base+=l;
   /* if rel starts with host, use all of it */
   if(Urlhostlength(rel))
   {  fraghash=(STRNIEQUAL(rel,"NEWS:",5))?'\0':'#';
      Urlcat(absurl,rel,Urllength(rel));
      return;
   }
   /* inherit host of base */
   l=Urlhostlength(base);
   if(l) 
   {  Urlcat(absurl,base,l);
      hadhost=TRUE;
   }
   localhost=STRNIEQUAL(absurl,"FILE:",5);
   fraghash=(STRNIEQUAL(absurl,"NEWS:",5))?'\0':'#';
   base+=l;
   if(*base=='/') base++;
   /* if rel starts with slash, it's absurl path */
   if(*rel=='/')
   {  /* In strict mode, just copy the remaining path. */
      if(prefs.browser.htmlmode==HTML_STRICT)
      {  Urlcat(absurl,rel,Urllength(rel));
         return;
      }
      /* In tolerant modes, go through the ./ and ../ logic below.
       * Don't inherit the base's path though. */
   }
   /* use base path for a start if there was a host */
   /* add an initial slash unless ths URL is of the form:
      "x-aweb:token" where the token is interpreted as part of the path.
      DO add a slash in case of "x-aweb://token" */
   if(STRNIEQUAL(absurl,"X-AWEB:",7))
   {  if(absurl[7]=='/') Urlcat(absurl,"/",1);
   }
   else if(hadhost) Urlcat(absurl,"/",1);
   pathp=absurl+strlen(absurl); /* pathp is start of path after "/" */
   l=Urlpathlength(base);
   if(*rel=='/')
   {  /* Absolute path, skip the slash */
      rel++;
   }
   else
   {  /* Copy the bases path */
      if(l) Urlcat(absurl,base,l);
   }
   base+=l;
   /* if rel path is not empty, merge it into absurl */
   if(Urlpathlength(rel))
   {  if(absurl[strlen(absurl)-1]!='/') Removelastseg(pathp,localhost);
      while(l=Urlpathseglength(rel))
      {  /* remove "./" or ".(eol)" */
         if((l==2 && STRNEQUAL(rel,"./",2))
         || (l==1 && STREQUAL(rel,".")))
         {  /* remove "." */
            rel+=l;
         }
         else /* remove "../" or "..(eol)" */
            if((l==3 && STRNEQUAL(rel,"../",3))
         || (l==2 && STREQUAL(rel,"..")))
         {  /* remove ".." and last segment, or add ".." */
            if(Removelastseg(pathp,localhost)) rel+=l;
            else
            {  /* Only add extra ../ in strict mode */
               if(prefs.browser.htmlmode==HTML_STRICT)
               {  Urlcat(absurl,"../",3);
                  pathp+=3;
               }
               rel+=l;
            }
         }
         else
         {  /* copy segment */
            Urlcat(absurl,rel,l);
            rel+=l;
         }
      }
   }
   else usebaseparam=TRUE;
   /* add rel's parameters and query */
   l=Urlparamlength(rel);
   m=Urlparamlength(base);
   if(l)
   {  Urlcat(absurl,rel,l);
      usebaseparam=FALSE;
   }
   else if(m && usebaseparam) Urlcat(absurl,base,m);
   rel+=l;
   base+=m;
   l=Urlquerylength(rel);
   m=Urlquerylength(base);
   if(l) Urlcat(absurl,rel,l);
   else if(m && usebaseparam) Urlcat(absurl,base,m);
}

/*---------------------------------------------------------------------*/

/* Compute hash from name */
static short Hash(UBYTE *name)
{  UBYTE hash=0,*p;
   if(name)
   {  for(p=name;*p;p++) hash^=*p;
   }
   return (short)(hash&HASHMASK);
}

/* Dispose object and clear pointer */
static void Clearobject(struct Aobject **p)
{  if(*p) Adisposeobject(*p);
   *p=NULL;
}

/* Solve movedto references */
static struct Url *Finalurl(struct Url *url)
{  short n=0;
   while(++n<100 && url->movedto && !(url->flags&URLF_VOLATILE)
   && ((url->flags&URLF_WASDUP) || (url->cache && !Agetattr(url->cache,AOCAC_Expired))))
   {  url=url->movedto;
   }
   return url;
}

/* Idem but don't require a cache object. Don't know why it is there in the
 * above function, but I don't want to try out what terrible things might
 * happen if I remove the test. But we need it without in one occation.
 * Very ugly solution but since this is the last upgrade anyway... */
static struct Url *Finalurl2(struct Url *url)
{  short n=0;
   while(++n<100 && url->movedto && !(url->flags&URLF_VOLATILE)
   && (!url->cache || !Agetattr(url->cache,AOCAC_Expired)))
   {  url=url->movedto;
   }
   return url;
}

/* Check for movedto loop */
static BOOL Movedtoloop(struct Url *url)
{  struct Url *u=url;
   short n=0;
   while(++n<100 && u->movedto)
   {  u=u->movedto;
      if(url==u) break;
   }
   return (BOOL)(url->movedto && u==url);
}

/* Permanently moved URLs should return their new address */
static UBYTE *Urllinkaddress(struct Url *url)
{  short n=0;
   while(url->movedto && !(url->flags&(URLF_TEMPMOVED|URLF_VOLATILE)))
   {  url=url->movedto;
      if(++n>99) break;
   }
   return url->url?url->url:NULLSTRING;
}

/* Find the ETag of this object */
static UBYTE *Urletag(struct Url *url)
{  UBYTE *etag=NULL;
   void *cache;
   if(cache=Agetattr(url,AOURL_Cache))
   {  etag=(UBYTE *)Agetattr(cache,AOCAC_Etag);
   }
   if(!etag)
   {  etag=(UBYTE *)Agetattr(url,AOURL_Etag);
   }
   return etag;
}
/* temporary ones should not */

static UBYTE *Urladdress(struct Url *url)
{  short n=0;
   while(url->movedto && !(url->flags&(URLF_VOLATILE)))
   {  url=url->movedto;
      if(++n>99) break;
   }
   return url->url?url->url:NULLSTRING;
}

/* Returns old or new SOURCE object for this URL. If URL is moved, return a source
 * for the new location. */
static void *Urlsource(struct Url *url)
{  url=Finalurl(url);
   if(!url->source)
   {  url->source=Anewobject(AOTP_SOURCE,
         AOSRC_Url,url,
         TAG_END);
   }
   return url->source;
}

/* Passes these tags to all childs in this relation */
static void Notifychilds(struct Url *url,ULONG relation,...)
{  struct Child *ch,*next;
   for(ch=url->childs.first;ch->next;ch=next)
   {  next=ch->next; /* Window might remove itself */
      if(ch->relation==relation)
      {  Asetattrs(ch->object,TAG_MORE,VARARG(relation));
      }
   }
}

/* Replace the old source object by a new one. If newsource==NULL then create new one. */
static void Replacesource(struct Url *url,void *newsource)
{  void *oldsource;
   if(!newsource)
   {  newsource=Anewobject(AOTP_SOURCE,
         AOSRC_Url,url,
         TAG_END);
   }
   if(newsource)
   {  oldsource=url->source;
      url->source=newsource;
      if(oldsource)
      {  /* Now move all COPYs from the old source to the new one.
          * New drivers will be generated by source object. */
         Asetattrs(oldsource,
            AOSRC_Movetourl,url,
            TAG_END);
         Adisposeobject(oldsource);
      }
   }
}

/* Mark URL as visited, and notify links. */
static void Urlvisited(struct Url *url)
{  if(!(url->flags&URLF_VISITED))
   {  url->flags|=URLF_VISITED;
      Notifychilds(url,AOREL_URL_LINK,
         AOURL_Visited,TRUE,
         TAG_END);
   }
}

/* Send the source a message that re-posting isn't performed */
static void Norepost(struct Url *url)
{  static UBYTE flushedmsg[]="<html><head><title>%s</title></head>\n<body>\n<h1>%s</h1>\n"
      "<b>%s</b>\n<p>%s\n<p>%s\n</body></html>\n";
   UBYTE *head,*msg1,*msg2,*buffer;
   long len;
   head=AWEBSTR(MSG_EPART_FLUSHED_HEAD);
   msg1=AWEBSTR(MSG_EPART_FLUSHED_MSG);
   msg2=AWEBSTR(MSG_EPART_FLUSHED_NORELOAD);
   len=strlen(flushedmsg)+2*strlen(head)+strlen(url->url)+strlen(msg1)+strlen(msg2);
   if(buffer=ALLOCTYPE(UBYTE,len+16,0))
   {  len=sprintf(buffer,flushedmsg,head,head,url->url,msg1,msg2);
      Asrcupdatetags(url->source,NULL,
         AOURL_Error,TRUE,
         AOURL_Data,buffer,
         AOURL_Datalength,len,
         AOURL_Eof,TRUE,
         TAG_END);
      FREE(buffer);
   }
}

/* Initiate the load of this url.
 * If it is a reload, kill everything running and do a plain fetch.
 * Else use this decision table:
 *
 *                1  2   3  4 5  6  7  8
 * Download req   YY NNN NN N NN NN NN NN cdl
 * Document ext   -- NNN NN N NN NN NN YY cdex
 * History req    -- YYY NN N NN NN NN -- chis
 * Ifinmem req    -- --- YY N NN NN NN -- ciim
 * Validate       -- --- -- - NN YY YY -- cval
 * Fast response  -- --- -- - -- NN YY -- cfr
 * Source+driver  -- YNN YN N YN YN YN -- csrc
 * Cache          NY -YN -- N -Y -Y -Y NY ccac
 *
 * Use source     .. X.. XX . X. .. X. .. asrc
 * Save source    XX ... .. . .. .. .. .. assrc
 * Docext source  .. ... .. . .. .. .. XX adsrc
 * New source     .. .XX .. X .X .X .X .. ansrc
 * Rfetch cache   .X .X. .. . .X .. .X .X arf
 * Fetch          X. ..X .. X .. .. .. X. af
 * Vfetch         .. ... .. . .. XX XX .. avf
 *
 */
static long Loadurl(struct Url *url,struct Aumload *auml)
{  long retval=0;
   BOOL nocache=FALSE,expired;
   ULONG veridate=0;
   BOOL cdl,cdex,chis,ciim,cval,cfr,csrc,ccac;
   BOOL asrc=FALSE,assrc=FALSE,adsrc=FALSE,ansrc=FALSE,arf=FALSE,af=FALSE,avf=FALSE;
   BOOL awin=FALSE;  /* Notify window in case of hi-jack ongoing fetch */
   void *window=(void *)Agetattr(auml->frame,AOBJ_Window);
   if(auml->flags&(AUMLF_RELOAD|AUMLF_CHANNEL))
   {  /* Break any ongoing fetches */
      Clearobject(&url->fetch);
      Clearobject(&url->rfetch);
      Clearobject(&url->vfetch);
      Clearobject(&url->vcache);
      Clearobject(&url->vsource);
      Clearobject(&url->dsource);
      Clearobject(&url->ocache);
      url->ocache=url->cache;
      url->cache=NULL;
      nocache=TRUE;
      af=TRUE;
      if(auml->flags&AUMLF_DOWNLOAD) assrc=TRUE;
      else if(auml->flags&AUMLF_DOCEXT) adsrc=TRUE;
      else ansrc=TRUE;
      url->movedto=NULL;
      url->flags&=~URLF_TEMPMOVED;
      url->flags&=~URLF_FETCHERROR;
   }
   else /* No reload */
   {  /* If we are temporarily moved, reset our movedto url now.
        * This ensures that a second request to the original URL will reload. */
      if(url->flags&URLF_TEMPMOVED)
      {  /* Clear the cache of the previous destination for correct operation */
         if(url->movedto) Clearobject(&url->movedto->cache);
         url->movedto=NULL;
         url->flags&=~URLF_TEMPMOVED;
      }
      /* Find the resulting (movedto) url. */
      url=Finalurl(url);
      /* Only fetch if no fetch is going on */
      if(!url->fetch && !url->rfetch && !url->vfetch)
      {  /* Set the condition flags */
         cdl=BOOLVAL(auml->flags&AUMLF_DOWNLOAD);
         cdex=BOOLVAL(auml->flags&AUMLF_DOCEXT);
         chis=BOOLVAL(auml->flags&AUMLF_HISTORY);
         ciim=BOOLVAL(auml->flags&AUMLF_IFINMEM);
         cval=(auml->flags&AUMLF_VERIFY) ||
            (prefs.network.caverify==CAVERIFY_ALWAYS) ||
            (!(url->flags&URLF_VERIFIED) && prefs.network.caverify!=CAVERIFY_NEVER);
         cfr=prefs.network.fastresponse;
         csrc=url->source && Agetattr(url->source,AOSRC_Driver);
         expired=(url->flags&URLF_VOLATILE) || Agetattr(url->cache,AOCAC_Expired);
         ccac=url->cache && !expired;
         /* If not history, don't use expired or volatile cache or source */
         if(!chis && expired)
         {  csrc=FALSE;
            ccac=FALSE;
            Clearobject(&url->cache);
         }
         /* Infer the action(s) */
         if(cdl)                          /* Column 1 */
         {  assrc=TRUE;
            if(ccac) arf=TRUE;
            else af=TRUE;
         }
         else if(cdex)                    /* Column 8 */
         {  adsrc=TRUE;
            if(ccac) arf=TRUE;
            else af=TRUE;
         }
         else if(chis)                    /* Column 2 */
         {  if(csrc) asrc=TRUE;
            else
            {  ansrc=TRUE;
               if(ccac) arf=TRUE;
               else af=TRUE;
            }
         }
         else if(ciim)                    /* Column 3 */
         {  asrc=TRUE;
         }
         else if(!csrc && !ccac)          /* Column 4 */
         {  ansrc=TRUE;
            af=TRUE;
         }
         else if(!cval)                   /* Column 5 */
         {  if(csrc) asrc=TRUE;
            else
            {  ansrc=TRUE;
               arf=TRUE;
            }
         }
         else if(!cfr)                    /* Column 6 */
         {  if(!csrc) ansrc=TRUE;
            avf=TRUE;
         }
         else                             /* Column 7 */
         {  if(csrc) asrc=TRUE;
            else
            {  ansrc=TRUE;
               arf=TRUE;
            }
            avf=TRUE;
         }
      }
      else
      {  asrc=TRUE;
         awin=TRUE;
      }
   }
   /* Don't verify POST responses. Always use memory cached copy when available. */
   if(avf && url->postnr && url->flags&(URLF_VISITED))
   {  avf=FALSE;
      if(csrc) asrc=TRUE;
   }
   if(!url->fetch && !url->rfetch && !url->vfetch)
   {  Clearobject(&url->ssource);
   }
   if(assrc && !url->ssource)
   {  url->ssource=Anewobject(AOTP_SOURCE,
         AOSRC_Url,url,
         AOSRC_Saveas,TRUE,
         AOSRC_Noicon,BOOLVAL(auml->flags&AUMLF_NOICON),
         TAG_END);
   }
   if(adsrc && !url->dsource)
   {  url->dsource=Anewobject(AOTP_SOURCE,
         AOSRC_Url,url,
         AOSRC_Docext,TRUE,
         TAG_END);
   }
   if(ansrc)
   {  Replacesource(url,NULL);
   }
   if(arf)
   {  url->rfetch=Anewobject(AOTP_FETCH,
         AOFCH_Url,url,
         AOFCH_Cache,TRUE,
         AOFCH_Name,Agetattr(url->cache,AOCAC_Name),
         AOFCH_Imagefetch,BOOLVAL(auml->flags&AUMLF_IMAGE),
         AOFCH_Windowkey,Agetattr(window,AOWIN_Key),
         TAG_END);
      Asetattrs(url->cache,
         AOCAC_Touched,TRUE,
         AOCAC_Sendinfo,url->rfetch,
         TAG_END);
      if(!assrc && !prefs.network.ignoremime)
      {  Asrcupdatetags(url->source,url->rfetch,
            AOURL_Contenttype,Agetattr(url->cache,AOCAC_Contenttype),
            TAG_END);
      }
   }
   if(af)
   {  if(url->postnr && url->flags&(URLF_VISITED))
      {  Norepost(url);
      }
      else
      {  if(!(url->fetch=Anewobject(AOTP_FETCH,
            AOFCH_Url,url,
            AOFCH_Name,url->url,
            AOFCH_Nocache,nocache,
            AOFCH_Referer,auml->referer?((struct Url *)auml->referer)->url:NULL,
            (auml->flags&AUMLF_MULTIPART)?AOFCH_Multipartdata:AOFCH_Postmsg,
               url->postnr?auml->postmsg:NULL,
            AOFCH_Windowkey,Agetattr(window,AOWIN_Key),
            AOFCH_Noproxy,BOOLVAL(auml->flags&AUMLF_NOPROXY),
            AOFCH_Loadflags,auml->flags,
            AOFCH_Imagefetch,BOOLVAL(auml->flags&AUMLF_IMAGE),
            AOFCH_Commands,Agetattr(window,AOWIN_Commands),
            AOFCH_Jframe,auml->frame,
            AOFCH_Formwarn,BOOLVAL(auml->flags&AUMLF_FORMWARN),
            AOFCH_Channel,BOOLVAL(auml->flags&AUMLF_CHANNEL),
            TAG_END)))
         {  Notifychilds(url,AOREL_URL_WINDOW,AOWIN_Activeurl,NULL,TAG_END);
            Inputallwindows(FALSE);
         }
         if(url->fetch)
         {  url->flags&=~URLF_FETCHERROR;
         }
         /* POST (postnr!=0): do not create a disk cache; responses are not safe to reuse. */
         if(url->fetch && (url->flags&URLF_CACHEABLE) && !assrc && !url->postnr)
         {  url->cache=Anewobject(AOTP_CACHE,
               AOCAC_Url,url,
               TAG_END);
         }
         if(auml->flags&AUMLF_CHANNEL)
         {  retval=Agetattr(url->fetch,AOFCH_Channelid);
         }
      }
   }
   if(avf)
   {  UBYTE *etag=NULL;
      if(url->cache)
      {  veridate=Agetattr(url->cache,AOCAC_Lastmodified);
         etag=(UBYTE *)Agetattr(url->cache,AOCAC_Etag);
      }
      else
      {  /* For local files, use date from source. */
         if(url->source &&
            (STRNIEQUAL(url->url,"FILE://LOCALHOST/",17) ||
             STRNIEQUAL(url->url,"FILE:///",8)))
         {  veridate=Agetattr(url->source,AOSRC_Lastmodified);
         }
      }
      url->vfetch=Anewobject(AOTP_FETCH,
         AOFCH_Url,url,
         AOFCH_Name,url->url,
         AOFCH_Ifmodifiedsince,veridate,
         AOFCH_Etag,etag,
         AOFCH_Referer,auml->referer?((struct Url *)auml->referer)->url:NULL,
         AOFCH_Noproxy,BOOLVAL(auml->flags&AUMLF_NOPROXY),
         AOFCH_Loadflags,auml->flags,
         AOFCH_Imagefetch,BOOLVAL(auml->flags&AUMLF_IMAGE),
         AOFCH_Windowkey,Agetattr(window,AOWIN_Key),
         AOFCH_Formwarn,BOOLVAL(auml->flags&AUMLF_FORMWARN),
         TAG_END);
      if(url->vfetch)
      {  url->flags&=~URLF_FETCHERROR;
         if(url->flags&URLF_CACHEABLE)
         {  url->vcache=Anewobject(AOTP_CACHE,
               AOCAC_Url,url,
               TAG_END);
         }
         url->vsource=Anewobject(AOTP_SOURCE,
            AOSRC_Url,url,
            TAG_END);
      }
   }
   if((af || avf) && !assrc)
   {  url->loadnr=++loadnr;
   }
   if(af || arf)
   {  SETFLAG(url->flags,URLF_DEXFETCH,adsrc);
   }
   if(arf || af || avf || awin)
   {  Inputallwindows(TRUE);
   }
   if(asrc)
   {  Asetattrs(url->source,AOSRC_Usedriver,TRUE,TAG_END);
   }
   return retval;
}

/* Perform special operations */
static long Specialurl(struct Url *url,struct Aumspecial *aums)
{  switch(aums->type)
   {  case AUMST_SAVESOURCE:
         url=Finalurl(url);
         Asetattrs(url->source,AOSRC_Savesource,TRUE,TAG_END);
         break;
      case AUMST_VIEWSOURCE:
         url=Finalurl(url);
         Asetattrs(url->source,AOSRC_Viewsource,TRUE,TAG_END);
         break;
      case AUMST_CANCELFETCH:
         url=Finalurl(url);
         if(url->fetch) Asetattrs(url->fetch,AOFCH_Cancel,TRUE,TAG_END);
         if(url->rfetch) Asetattrs(url->rfetch,AOFCH_Cancel,TRUE,TAG_END);
         if(url->vfetch) Asetattrs(url->vfetch,AOFCH_Cancel,TRUE,TAG_END);
         break;
      case AUMST_DELETECACHE:
         Clearobject(&url->cache);
         break;
      case AUMST_EDITSOURCE:
         url=Finalurl(url);
         Asetattrs(url->source,AOSRC_Editsource,TRUE,TAG_END);
         break;
      case AUMST_FLUSHSOURCE:
         Asetattrs(url->source,AOSRC_Flush,TRUE,TAG_END);
         Doupdateframes();
         break;
   }
   return 0;
}

/* Find the content type of this object */
static UBYTE *Urlcontenttype(struct Url *url)
{  UBYTE *type=NULL;
   if(url->cache && !prefs.network.ignoremime)
   {  type=(UBYTE *)Agetattr(url->cache,AOCAC_Contenttype);
   }
   if(!type) type=(UBYTE *)Agetattr(url->source,AOURL_Contenttype);
   if(!type) type=Mimetypefromext(url->url);
   /* Check for view-source: URL - force text/plain for text types */
   /* This overrides cached content types when view-source is active */
   if(type && Agetattr((void *)url,AOURL_Viewsource) && STRNIEQUAL(type,"text/",5))
   {  return (UBYTE *)"text/plain";
   }
   return type;
}

/* Move this url. */
static void Moveurl(struct Url *url,UBYTE *newurl,BOOL temp,BOOL seeother,void *fetch)
{  ULONG loadflags=0;
   UBYTE *referer=NULL,*postmsg=NULL;
   struct Multipartdata *mpd=NULL;
   Agetattrs(fetch,
      AOFCH_Referer,&referer,
      seeother?TAG_IGNORE:AOFCH_Postmsg,&postmsg,
      seeother?TAG_IGNORE:AOFCH_Multipartdata,&mpd,
      AOFCH_Loadflags,&loadflags,
      TAG_END);
   if(prefs.browser.htmlmode==HTML_COMPATIBLE) postmsg=NULL;
/*
   /* If the old URL doesn't have a cache object attached any more, there
    * must have been a Pragma: no-cache header. Make the url volatile. */
   if(!url->cache) url->flags|=URLF_VOLATILE;
*/
   /* If the new URL contains a query part, it's probably meant to be
    * retrieved with a GET request. */
   if(postmsg && strchr(newurl,'?')) postmsg=NULL;
   if(url->movedto=Findurl(url->url,newurl,postmsg?-1:0))
   {  if(!Movedtoloop(url))
      {  if(temp)
         {  url->flags|=URLF_TEMPMOVED;
            /* Temporarily moved URLs should not be cached - clear cache */
            Clearobject(&url->cache);
         }
         else url->flags&=~URLF_TEMPMOVED;
         /* Redirects with query strings should always refresh to avoid
          * showing stale cached content when query parameters change */
         if(strchr(newurl,'?'))
         {  Asetattrs(url->movedto,
               AOURL_Volatile,TRUE,
               TAG_END);
         }
         if(url->source)
         {  Asetattrs(url->source,
               AOSRC_Movetourl,url->movedto,
               TAG_END);
            Adisposeobject(url->source);
            url->source=NULL;
         }
         Notifychilds(url,AOREL_URL_WINDOW,AOWIN_Activeurl,url->movedto,TAG_END);
         /* In case of multipart data, the loadflags are already set ok. Just use
          * mpd instead of postmsg */
         /* For redirects, set referer to the original URL (the one that redirected) */
         Auload(url->movedto,loadflags,url,
            mpd?(UBYTE *)mpd:postmsg,NULL);
         if(url->ssource)
         {  Asetattrs(url->ssource,
               AOSRC_Movetourl,url->movedto,
               TAG_END);
            Adisposeobject(url->ssource);
            url->ssource=NULL;
         }
         if(url->dsource)
         {  Asetattrs(url->dsource,
               AOSRC_Movetourl,url->movedto,
               TAG_END);
            Adisposeobject(url->dsource);
            url->dsource=NULL;
         }
      }
   }
}

/* Process verify input */
static void Srcupdatevfetch(struct Url *url,struct Amsrcupdate *ams)
{  struct TagItem *wtags,*tag,*tstate=ams->tags;
   UBYTE *referer;
   ULONG loadflags=0;
   BOOL header=FALSE;
   while(url->vfetch && (tag=NextTagItem(&tstate)))
   {  switch(tag->ti_Tag)
      {  case AOURL_Data:
         case AOURL_Movedto:
         case AOURL_Tempmovedto:
            /* Modified data on a verify. Use it and kill any rfetch. */
            if(url->rfetch)
            {  Notifychilds(url,AOREL_URL_WINDOW,AOWIN_Status,NULL,TAG_END);
               Clearobject(&url->rfetch);
            }
            if(url->cache)
            {  Adisposeobject(url->cache);
            }
            url->fetch=url->vfetch;
            url->cache=url->vcache;
            Replacesource(url,url->vsource);
            url->vfetch=NULL;
            url->vcache=NULL;
            url->vsource=NULL;
            url->flags|=URLF_VERIFIED;
            url->flags&=~URLF_FETCHERROR;
            break;
         case AOURL_Error:
            /* Don't use an error validation response. */
            if(!tag->ti_Data) break;
            url->flags|=URLF_FETCHERROR;
            Adisposeobject(url->vfetch);
            /* Will NOT return to us with AOURL_Terminate set
             * so fall through: */
         case AOURL_Terminate:
            /* Not modified. Dispose vcache, start reload if slow verify.
             * On verify error we fall through from AOURL_Error: keep URLF_FETCHERROR set. */
            url->vfetch=NULL;
            url->flags|=URLF_VERIFIED;
            Clearobject(&url->vcache);
            Clearobject(&url->vsource);
            if(!url->rfetch)
            {  Notifychilds(url,AOREL_URL_WINDOW,AOWIN_Activeurl,NULL,TAG_END);
               Inputallwindows(FALSE);
            }
            else
            {  Notifychilds(url,AOREL_URL_WINDOW,AOWIN_Status,NULL,TAG_END);
            }
            if(!prefs.network.fastresponse)
            {  /* Let load logic decide to use source or cache copy.
                * Pass HISTORY flag so it won't start another verify */
               Agetattrs(ams->fetch,
                  AOFCH_Referer,&referer,
                  AOFCH_Loadflags,&loadflags,
                  TAG_END);
               loadflags&=AUMLF_NOPROXY;
               Auload(url,AUMLF_HISTORY|loadflags,
                  referer?Findurl("",referer,0):NULL,NULL,NULL);
               Changedlayout();
            }
            break;
         case AOURL_Lastmodified:
            Asetattrs(url->vsource,AOSRC_Lastmodified,tag->ti_Data,TAG_END);
            break;
         case AOURL_Header:
            header=TRUE;
            break;
      }
   }
   if(!url->rfetch)
   {  if(wtags=CloneTagItems(ams->tags))
      {  MapTags(wtags,mapwindowtags,MAP_REMOVE_NOT_FOUND);
         Notifychilds(url,AOREL_URL_WINDOW,TAG_MORE,wtags);
         FreeTagItems(wtags);
      }
   }
   if(url->vcache && !(url->flags&URLF_FETCHERROR))
   {  AmethodA(url->vcache,ams);
   }
   if(header)
   {  AmethodA(url->vsource,ams);
   }
}

/* Update from normal fetch or cache reload. Only forward to cache if (cache) is TRUE. */
static void Srcupdatefetch(struct Url *url,struct Amsrcupdate *ams,BOOL cache)
{  struct TagItem *wtags,*tag,*tstate=ams->tags;
   BOOL terminate=FALSE;
   /* Check if this is an XHR fetch - if so, let XHR handler process it first */
   /* We'll call Xhrsrcupdate which will check the mapping and process if it's an XHR */
   if(ams->fetch)
   {  /* Xhrsrcupdate will check if this fetch is in our mapping */
      /* It returns 1 if it processed it (XHR), 0 if not */
      /* Even if it's an XHR, we continue with normal processing for cache, etc. */
      Xhrsrcupdate(url,ams);
      /* Continue with normal processing regardless - XHR just extracts its data */
   }
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOURL_Movedto:
            Moveurl(url,(UBYTE *)tag->ti_Data,FALSE,FALSE,ams->fetch);
            Urlvisited(url);
            break;
         case AOURL_Tempmovedto:
            Moveurl(url,(UBYTE *)tag->ti_Data,TRUE,FALSE,ams->fetch);
            Urlvisited(url);
            break;
         case AOURL_Seeother:
            Moveurl(url,(UBYTE *)tag->ti_Data,TRUE,TRUE,ams->fetch);
            Urlvisited(url);
            break;
         case AOURL_Terminate:
            terminate=TRUE;
            url->flags|=URLF_VERIFIED;
            Clearobject(&url->ocache);
            break;
         case AOURL_Error:
            if(tag->ti_Data && cache)
            {  /* Don't cache error response body: later Data/Eof srcupdates must not
                * touch url->cache (e.g. restored ocache after reload, or combined
                * Error+Data in one task message). */
               url->flags|=URLF_FETCHERROR;
               Clearobject(&url->cache);
               /* Restore old cache if it exists */
               url->cache=url->ocache;
               url->ocache=NULL;
            }
            else if(!tag->ti_Data)
            {  /* HTTP retry clears error (e.g. fresh connection after keep-alive fail) */
               url->flags&=~URLF_FETCHERROR;
            }
            break;
         case AOURL_Data:
            Urlvisited(url);
            break;
         case AOURL_Reload:
            url->flags&=~URLF_FETCHERROR;
            Clearobject(&url->cache);
            if(url->ssource)
            {  Adisposeobject(url->ssource);
               url->ssource=Anewobject(AOTP_SOURCE,
                  AOSRC_Url,url,
                  AOSRC_Saveas,TRUE,
                  TAG_END);
            }
            else if(url->dsource)
            {  Adisposeobject(url->dsource);
               url->dsource=Anewobject(AOTP_SOURCE,
                  AOSRC_Url,url,
                  AOSRC_Docext,TRUE,
                  TAG_END);
            }
            else
            {  Replacesource(url,NULL);
            }
            if(url->flags&URLF_CACHEABLE)
            {  url->cache=Anewobject(AOTP_CACHE,AOCAC_Url,url,TAG_END);
            }
            break;
         case AOURL_Nocache:
            if(tag->ti_Data)
            {  Clearobject(&url->cache);
            }
            break;
      }
   }
   if(wtags=CloneTagItems(ams->tags))
   {  MapTags(wtags,mapwindowtags,MAP_REMOVE_NOT_FOUND);
      Notifychilds(url,AOREL_URL_WINDOW,TAG_MORE,wtags);
      FreeTagItems(wtags);
   }
   /* Update our source; create a new one if none exists, except if we are moved */
   if(url->ssource)
   {  AmethodA(url->ssource,ams);
   }
   if(url->dsource)
   {  AmethodA(url->dsource,ams);
   }
   if(!url->ssource)
   {  if(cache && url->cache && !(url->flags&URLF_FETCHERROR))
      {  AmethodA(url->cache,ams);
      }
      if(!(url->flags&URLF_DEXFETCH))
      {  if(url->source || !url->movedto)
         {  AmethodA(Urlsource(url),ams);
         }
         Changedlayout();
      }
   }
   if(terminate)
   {  url->flags&=~URLF_FETCHERROR;
      url->fetch=NULL;
      url->rfetch=NULL;
      if(!url->vfetch)
      {  Notifychilds(url,AOREL_URL_WINDOW,AOWIN_Activeurl,NULL,TAG_END);
         Inputallwindows(FALSE);
      }
      else
      {  Notifychilds(url,AOREL_URL_WINDOW,AOWIN_Status,NULL,TAG_END);
      }
   }
}

/* Check for duplicate URL in the same list. Then queue a check for the next
 * url in this list, or for the first url in the next list. */
static void Dupcheck(struct Url *url)
{  struct Url *u;
   for(u=url->next;u->next;u=u->next)
   {  if(STRIEQUAL(url->url,u->url) && url->postnr==u->postnr)
      {  Clearobject(&u->cache);
         u->movedto=url;
         u->flags&=~URLF_TEMPMOVED;
         u->flags|=URLF_WASDUP;
      }
   }
   u=url->next;
   if(!u->next)
   {  for(dupchecklist++;dupchecklist<NRLISTS;dupchecklist++)
      {  if(!ISEMPTY(&urls[dupchecklist]))
         {  u=urls[dupchecklist].first;
            break;
         }
      }
   }
   if(u->next) Queuesetmsg(u,UQID_DUPCHECK);
}

/*---------------------------------------------------------------------*/

static long Seturl(struct Url *url,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOURL_Url:
            if(!url->url) url->url=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOURL_Postnr:
            url->postnr=tag->ti_Data;
            break;
         case AOURL_Cacheable:
            if(tag->ti_Data) url->flags|=URLF_CACHEABLE;
            else url->flags&=~URLF_CACHEABLE;
            break;
         case AOURL_Movedto:  /* Set by cache when reading in registration */
            url->movedto=Findurl("",(UBYTE *)tag->ti_Data,0);
            url->flags&=~URLF_TEMPMOVED;
            break;
         case AOURL_Tempmovedto:  /* Set by cache when reading in registration */
            url->movedto=Findurl("",(UBYTE *)tag->ti_Data,0);
            url->flags|=URLF_TEMPMOVED;
            break;
         case AOURL_Cache:
            if((url->flags&URLF_CACHEABLE) && !url->cache)
            {  url->cache=(void *)tag->ti_Data;
            }
            break;
         case AOURL_Visited:
            if(tag->ti_Data) url->flags|=URLF_VISITED;
            else url->flags&=~URLF_VISITED;
            break;
         case AOURL_Cachefetch:
            if(!tag->ti_Data && !url->rfetch)
            {  Clearobject(&url->cache);
            }
            break;
         case AOURL_Volatile:
            SETFLAG(url->flags,URLF_VOLATILE,tag->ti_Data);
            break;
         case AOURL_Viewsource:
            if(tag->ti_Data) url->flags|=URLF_VIEWSOURCE;
            else url->flags&=~URLF_VIEWSOURCE;
            break;
         case AOBJ_Queueid:
            if(tag->ti_Data==UQID_DUPCHECK)
            {  Dupcheck(url);
            }
            break;
      }
   }
   if(url->postnr)
   {  if(url->postnr<0) url->postnr=++lastpostnr;
      url->flags&=~URLF_CACHEABLE;
   }
   if(!(url->flags&URLF_CACHEABLE))
   {  Clearobject(&url->cache);
   }
   return 0;
}

static long Geturl(struct Url *url,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOURL_Url:
            PUTATTR(tag,Urladdress(url));
            break;
         case AOURL_Postnr:
            PUTATTR(tag,url->postnr);
            break;
         case AOURL_Visited:
            PUTATTR(tag,BOOLVAL(url->flags&URLF_VISITED));
            break;
         case AOURL_Source:
            PUTATTR(tag,Urlsource(url));
            break;
         case AOURL_Contenttype:
            PUTATTR(tag,Urlcontenttype(url));
            break;
         case AOURL_Cacheable:
            PUTATTR(tag,BOOLVAL(url->flags&URLF_CACHEABLE));
            break;
         case AOURL_Movedto:
            if(url->movedto && !(url->flags&URLF_TEMPMOVED))
            {  PUTATTR(tag,url->movedto->url);
            }
            break;
         case AOURL_Tempmovedto:
            if(url->movedto && (url->flags&URLF_TEMPMOVED))
            {  PUTATTR(tag,url->movedto->url);
            }
            break;
         case AOURL_Linkurl:
            PUTATTR(tag,Urllinkaddress(url));
            break;
         case AOURL_Etag:
            PUTATTR(tag,Urletag(url));
            break;
         case AOURL_Realurl:
            PUTATTR(tag,url->url);
            break;
         case AOURL_Loadnr:
            PUTATTR(tag,url->loadnr);
            break;
         case AOURL_Cache:
            PUTATTR(tag,url->cache);
            break;
         case AOURL_Isinmem:
            PUTATTR(tag,url->source && Agetattr(url->source,AOSRC_Driver));
            break;
         case AOURL_Sourcevalid:
            /* If no cache, it's not expired so it's valid. */
            /* Why the (!url->fetch)?? Why wouldn't a source be valid when
             * a fetch is still going on? Either it is the new source, which
             * will be valid, or it is still the old one, which will replace
             * itself when new data arrives. Note this attr is only gotten in
             * frame.c:Setnewwinhis()
             * Leaving the !url->fetch in causes loops when location is set
             * in a script while the fetch is still going on. */
            PUTATTR(tag,/**!url->fetch &&**/ !Agetattr(url->cache,AOCAC_Expired));
            break;
         case AOURL_Saveassource:
            PUTATTR(tag,url->ssource);
            break;
         case AOURL_Input:
            {  struct Url *u=Finalurl(url);
               PUTATTR(tag,u->fetch || u->rfetch || u->vfetch);
            }
            break;
         case AOURL_Docextsource:
            PUTATTR(tag,url->dsource);
            break;
         case AOURL_Finalurl:
            PUTATTR(tag,Finalurl(url)->url);
            break;
         case AOURL_Finalurlptr:
            PUTATTR(tag,Finalurl2(url));
            break;
         case AOURL_Viewsource:
            PUTATTR(tag,BOOLVAL(url->flags&URLF_VIEWSOURCE));
            break;
      }
   }
   return 0;
}

static struct Url *Newurl(struct Amset *ams)
{  struct Url *url=Allocobject(AOTP_URL,sizeof(struct Url),ams);
   if(url)
   {  NEWLIST(&url->childs);
      url->flags|=URLF_CACHEABLE;
      Seturl(url,ams);
      ADDHEAD(&urls[Hash(url->url)],url);
   }
   return url;
}

static long Srcupdateurl(struct Url *url,struct Amsrcupdate *ams)
{  long result=0;
   Busypointer(TRUE);
   if(ams->fetch==url->vfetch)
   {  Srcupdatevfetch(url,ams);
   }
   if(ams->fetch==url->fetch)
   {  Srcupdatefetch(url,ams,TRUE);
   }
   if(ams->fetch==url->rfetch)
   {  Srcupdatefetch(url,ams,FALSE);
   }
   Busypointer(FALSE);
   return result;
}

static long Addchildurl(struct Url *url,struct Amadd *ama)
{  struct Child *ch=ALLOCSTRUCT(Child,1,MEMF_CLEAR);
   if(ch)
   {  ch->object=ama->child;
      ch->relation=ama->relation;
      ADDTAIL(&url->childs,ch);
   }
   return 0;
}

static long Remchildurl(struct Url *url,struct Amadd *ama)
{  struct Child *ch;
   for(ch=url->childs.first;ch->next;ch=ch->next)
   {  if(ch->object==ama->child && ch->relation==ama->relation)
      {  REMOVE(ch);
         FREE(ch);
         break;
      }
   }
   return 0;
}

static void Disposeurl(struct Url *url)
{  struct Child *ch;
   while(ch=REMHEAD(&url->childs))
   {  switch(ch->relation)
      {  case AOREL_URL_LINK:
            Asetattrs(ch->object,AOLNK_Url,NULL,TAG_END);
            break;
         case AOREL_URL_WINDOW:
            Asetattrs(ch->object,AOWIN_Activeurl,NULL,TAG_END);
            break;
      }
      FREE(ch);
   }
   Clearobject(&url->fetch);
   Clearobject(&url->rfetch);
   Clearobject(&url->vfetch);
   Clearobject(&url->source);
   Clearobject(&url->vsource);
   Clearobject(&url->ssource);
   Clearobject(&url->dsource);
   /* Cache objects will be disposed of by cache */
   if(url->url) FREE(url->url);
   Amethodas(AOTP_OBJECT,url,AOM_DISPOSE);
}

static void Deinstall(void)
{  short i;
   void *p;
   for(i=0;i<NRLISTS;i++)
   {  if(urls[i].first)
      {  while(p=REMHEAD(&urls[i])) Disposeurl(p);
      }
   }
}

static long Dispatch(struct Url *url,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newurl((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Seturl(url,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Geturl(url,(struct Amset *)amsg);
         break;
      case AOM_SRCUPDATE:
         result=Srcupdateurl(url,(struct Amsrcupdate *)amsg);
         break;
      case AOM_ADDCHILD:
         result=Addchildurl(url,(struct Amadd *)amsg);
         break;
      case AOM_REMCHILD:
         result=Remchildurl(url,(struct Amadd *)amsg);
         break;
      case AOM_DISPOSE:
         Disposeurl(url);
         break;
      case AOM_DEINSTALL:
         Deinstall();
         break;
      case AUM_LOAD:
         result=Loadurl(url,(struct Aumload *)amsg);
         break;
      case AUM_SPECIAL:
         result=Specialurl(url,(struct Aumspecial *)amsg);
         break;
   }
   return result;
}

/*---------------------------------------------------------------------*/

BOOL Installurl(void)
{  short i;
   for(i=0;i<NRLISTS;i++)
   {  NEWLIST(&urls[i]);
   }
   if(!Amethod(NULL,AOM_INSTALL,AOTP_URL,Dispatch)) return FALSE;
   return TRUE;
}

BOOL Initurl(void)
{  emptyurl=Anewobject(AOTP_URL,
      AOURL_Url,"",
      TAG_END);
   if(!emptyurl) return FALSE;
   Replacesource(emptyurl,NULL);
   Asrcupdatetags(emptyurl->source,NULL,
      AOURL_Contenttype,"text/plain",
      AOURL_Data,"",
      AOURL_Datalength,0,
      TAG_END);
   Asrcupdatetags(emptyurl->source,NULL,
      AOURL_Eof,TRUE,
      AOURL_Terminate,TRUE,
      TAG_END);
   Asetattrs(emptyurl->source,AOSRC_Noflush,TRUE,TAG_END);
   return TRUE;
}

BOOL Initurl2(void)
{  for(dupchecklist=0;dupchecklist<NRLISTS;dupchecklist++)
   {  if(!ISEMPTY(&urls[dupchecklist]))
      {  Queuesetmsg(urls[dupchecklist].first,UQID_DUPCHECK);
         break;
      }
   }
   return TRUE;
}

void *Findurlloadnr(long loadnr)
{  struct Url *url;
   short i;
   for(i=0;i<NRLISTS;i++)
   {  for(url=urls[i].first;url->next;url=url->next)
      {  if(url->loadnr==loadnr) return url;
      }
   }
   return NULL;
}

UBYTE *Makeabsurl(UBYTE *base,UBYTE *url)
{  Buildabsurl(base?base:NULLSTRING,url);
   return Dupstr(absurl,-1);
}

/* Single hex digit for percent-decoding in query strings (RFC 3986 style). */
static int Url_hexdigit(UBYTE c)
{  if(c>='0' && c<='9') return (int)(c-'0');
   if(c>='a' && c<='f') return (int)(c-'a'+10);
   if(c>='A' && c<='F') return (int)(c-'A'+10);
   return -1;
}

/* Signed download URLs often put the real name in filename= / filename%3D in the query while the path ends in an opaque token. */
static UBYTE *Urlfilename_from_query(UBYTE *url)
{  UBYTE *q;
   UBYTE *s;
   UBYTE *end;
   UBYTE decbuf[264];
   long dlen;
   int hi;
   int lo;

   q=(UBYTE *)strchr((char *)url,'?');
   if(!q) return NULL;
   s=(UBYTE *)strstr((char *)q,"filename%3D");
   if(!s) s=(UBYTE *)strstr((char *)q,"filename=");
   if(!s) return NULL;
   if(STRNIEQUAL(s,"filename%3D",11))
      s+=11;
   else if(STRNIEQUAL(s,"filename=",9))
      s+=9;
   else return NULL;
   end=s;
   while(*end && *end!='&' && *end!='#') end++;
   dlen=0;
   while(s<end && dlen<(long)sizeof(decbuf)-1)
   {  if(*s=='%' && s+2<end)
      {  hi=Url_hexdigit(s[1]);
         lo=Url_hexdigit(s[2]);
         if(hi>=0 && lo>=0)
         {  decbuf[dlen++]=(UBYTE)((hi<<4)|lo);
            s+=3;
            continue;
         }
      }
      if(*s=='+')
      {  decbuf[dlen++]=' ';
         s++;
         continue;
      }
      decbuf[dlen++]=*s++;
   }
   decbuf[dlen]='\0';
   if(dlen<1) return NULL;
   return Dupstr(decbuf,-1);
}

UBYTE *Urlfilename(UBYTE *url)
{  UBYTE *p;
   UBYTE *q;
   UBYTE *dflt;
   UBYTE *alt;
   UBYTE *x;
   int hasdot;

   for(p=url;*p && *p!=';' && *p!='?' && *p!='#';p++);
   for(q=p-1;q>=url && *q!='/' && *q!=':';q--);
   if(q>url && *(q-1)!='/') dflt=Dupstr(q+1,p-q-1);
   else dflt=Dupstr("",0);
   hasdot=0;
   if(dflt)
   {  for(x=dflt;*x;x++)
      {  if(*x=='.')
         {  hasdot=1;
            break;
         }
      }
   }
   /* When the last path segment has no extension it is often an opaque id; many CDNs then supply filename= in the query. */
   if(strchr((char *)url,'?') && (!dflt || !*dflt || !hasdot))
   {  alt=Urlfilename_from_query(url);
      if(alt && *alt)
      {  FREE(dflt);
         return alt;
      }
      if(alt) FREE(alt);
   }
   return dflt;
}

UBYTE *Urlfilenamefb(UBYTE *url)
{  UBYTE *p,*q;
   for(p=url;*p && *p!=';' && *p!='?' && *p!='#';p++);
   p--;
   if(p>=url && *p=='/') p--;
   for(q=p;q>=url && *q!='/';q--);
   if(q>url) return Dupstr(q+1,p-q);
   p=strchr(url,':');
   if(p && p[1]) return Dupstr(p+1,strlen(p+1));
   if(!p) p=url+strlen(url);
   return Dupstr(url,p-url);
}

UBYTE *Urlfileext(UBYTE *url)
{  UBYTE *p,*q;
   for(p=url;*p && *p!=':';p++);
   if(p[0]==':' && p[1]=='/' && p[2]=='/')
   {  for(p+=3;*p && *p!='/';p++);
      if(*p)
      {  for(p++;*p && *p!=';' && *p!='?' && *p!='#';p++);
         for(q=p-1;q>=url && *q!='/' && *q!='.';q--);
         if(q>=url && *q=='.') return Dupstr(q+1,p-q-1);
      }
   }
   return NULL;
}

UBYTE *Fragmentpart(UBYTE *url)
{  UBYTE *p;
   /* Don't allow fragments in news: URLs (message ID can contain #) */
   if(!STRNIEQUAL(url,"NEWS:",5))
   {  if(p=strchr(url,'#')) return p+1;
   }
   return NULL;
}

#ifndef LOCALONLY
/* Dos Lock() on the typed string (fragment stripped for the probe only). Used when the bar
 * holds plain text that might be an Amiga path rather than a URL scheme AWeb handles. */
static BOOL Urlbarpathlocks(UBYTE *begin, UBYTE *end)
{  UBYTE *temp;
   UBYTE *hashmark;
   long plen;
   long lock;

   if(!begin || !end || end<=begin) return FALSE;
   plen=(long)(end-begin);
   temp=Dupstr(begin,plen);
   if(!temp) return FALSE;
   hashmark=strchr((char *)temp,'#');
   if(hashmark) *hashmark='\0';
   if(!temp[0])
   {  FREE(temp);
      return FALSE;
   }
   lock=Lock(temp,SHARED_LOCK);
   FREE(temp);
   if(lock)
   {  UnLock(lock);
      return TRUE;
   }
   return FALSE;
}

/* Build file:/// plus trimmed input — same notion as LOCAL/S prepending file:/// for CLI paths. */
static UBYTE *Fixurlname_mkfile(UBYTE *begin, UBYTE *end)
{  UBYTE *fixname;
   long len;

   if(!begin || !end || end<=begin) return NULL;
   len=(long)(end-begin)+8;
   if(fixname=ALLOCTYPE(UBYTE,len+1,MEMF_PUBLIC))
   {  strcpy(fixname,"file:///");
      strncat(fixname,begin,end-begin);
   }
   return fixname;
}

/* Leave input unchanged so loads fall through to Errorschemetask in fetch when nothing handles it. */
static UBYTE *Fixurlname_mkpassthrough(UBYTE *begin, UBYTE *end)
{  if(!begin || !end || end<=begin) return NULL;
   return Dupstr(begin,(long)(end-begin));
}
#endif /* !LOCALONLY */

void *Findurl(UBYTE *base,UBYTE *url,long postnr)
{  struct Url *u;
   UBYTE *absurl=Makeabsurl(base,url);
   UBYTE hash=0;
   hash=Hash(absurl);
   for(u=urls[hash].first;u->next;u=u->next)
   {  if(STREQUAL(u->url,absurl) && u->postnr==postnr)
      {  FREE(absurl);
         return u;
      }
   }
   if(postnr<0) postnr=++lastpostnr;
   else postnr=0;
   u=Anewobject(AOTP_URL,
      AOURL_Url,absurl,
      AOURL_Postnr,postnr,
      TAG_END);
   FREE(absurl);
   return u;
}

UBYTE *Fixurlname(UBYTE *name)
{  UBYTE *p,*begin=NULL,*end=NULL,*fixname;
   short scheme=0;   /* 0=unknown, 1=found, 2=not found */
   long len;
   UBYTE *colon=NULL;
   UBYTE *firstnonalnum=NULL;
   long schemelen=0;
   BOOL knownscheme=FALSE;
   BOOL requires_slashslash=FALSE;
   BOOL allows_noslashslash=FALSE;
   if(!name || !*name) return NULL;
#ifdef LOCALONLY
   /* In LOCALONLY builds, only treat "file:///" as a scheme */
   /* Everything else (including volume names with colons) is a file path */
   if(STRNIEQUAL(name,"file:///",8))
   {  /* Already has file:/// prefix, return a copy */
      if(fixname=Dupstr(name,-1))
      {  return fixname;
      }
      return NULL;
   }
   /* Not a file:/// URL, treat as file path and add prefix */
   for(p=name;*p;p++)
   {  if(!begin && !isspace(*p)) begin=p;
      if(begin)
      {  if(!isspace(*p)) end=p+1;
      }
   }
   if(!begin || !end) return NULL;
   len=end-begin+8;  /* "file:///" (8) + path */
   if(fixname=ALLOCTYPE(UBYTE,len+1,MEMF_PUBLIC))
   {  strcpy(fixname,"file:///");
      strncat(fixname,begin,end-begin);
   }
   return fixname;
#else
   for(p=name;*p;p++)
   {  if(!begin && !isspace(*p)) begin=p;
      if(begin)
      {  if(!isspace(*p)) end=p+1;
         if(!scheme)
         {  if(*p==':') scheme=1;
            else if(*p=='/') scheme=2;
         }
      }
   }
   if(!begin || !end) return NULL;
   /* Determine if this looks like a "scheme:" or an Amiga volume path. */
   for(p=begin;p<end;p++)
   {  if(*p==':')
      {  colon=p;
         break;
      }
      if(!isalnum((unsigned char)*p))
      {  firstnonalnum=p;
         break;
      }
   }
   if(colon && !firstnonalnum) firstnonalnum=colon;

   if(colon)
   {  schemelen=colon-begin;
      /* Identify known schemes. We must not misinterpret "ftp:host" as a volume. */
      if(STRNIEQUAL(begin,"http",4) && schemelen==4) { knownscheme=TRUE; requires_slashslash=TRUE; }
      else if(STRNIEQUAL(begin,"https",5) && schemelen==5) { knownscheme=TRUE; requires_slashslash=TRUE; }
      else if(STRNIEQUAL(begin,"ftp",3) && schemelen==3) { knownscheme=TRUE; requires_slashslash=TRUE; }
      else if(STRNIEQUAL(begin,"ftps",4) && schemelen==4) { knownscheme=TRUE; requires_slashslash=TRUE; }
      else if(STRNIEQUAL(begin,"gopher",6) && schemelen==6) { knownscheme=TRUE; requires_slashslash=TRUE; }
      else if(STRNIEQUAL(begin,"gemini",6) && schemelen==6) { knownscheme=TRUE; requires_slashslash=TRUE; }
      else if(STRNIEQUAL(begin,"spartan",7) && schemelen==7) { knownscheme=TRUE; requires_slashslash=TRUE; }
      else if(STRNIEQUAL(begin,"nntp",4) && schemelen==4) { knownscheme=TRUE; requires_slashslash=TRUE; }
      else if(STRNIEQUAL(begin,"telnet",6) && schemelen==6) { knownscheme=TRUE; requires_slashslash=TRUE; }
      else if(STRNIEQUAL(begin,"file",4) && schemelen==4) { knownscheme=TRUE; /* special validation below */ }
      else if(STRNIEQUAL(begin,"mailto",6) && schemelen==6) { knownscheme=TRUE; allows_noslashslash=TRUE; }
      else if(STRNIEQUAL(begin,"news",4) && schemelen==4) { knownscheme=TRUE; allows_noslashslash=TRUE; }
      else if(STRNIEQUAL(begin,"x-aweb",6) && schemelen==6) { knownscheme=TRUE; allows_noslashslash=TRUE; }
      else if(STRNIEQUAL(begin,"about",5) && schemelen==5) { knownscheme=TRUE; allows_noslashslash=TRUE; }
      else if(STRNIEQUAL(begin,"cid",3) && schemelen==3) { knownscheme=TRUE; allows_noslashslash=TRUE; }
      else if(STRNIEQUAL(begin,"data",4) && schemelen==4) { knownscheme=TRUE; allows_noslashslash=TRUE; }
      else if(STRNIEQUAL(begin,"javascript",10) && schemelen==10) { knownscheme=TRUE; allows_noslashslash=TRUE; }
      else if(STRNIEQUAL(begin,"view-source",11) && schemelen==11) { knownscheme=TRUE; allows_noslashslash=TRUE; }

      if(knownscheme)
      {  /* Validate syntax for schemes that require '//' (and for file:///) */
         if(requires_slashslash)
         {  if(colon+2 >= end) return NULL;
            if(colon[1]!='/' || colon[2]!='/') return NULL;
         }
         else if(!allows_noslashslash)
         {  /* file: scheme - accept only file:/// and file://localhost/ forms */
            if(STRNIEQUAL(begin,"file:///",8))
            {  /* file:/// is valid, including the root volumes view */
            }
            else if(STRNIEQUAL(begin,"file://localhost/",17))
            {  /* ok */
            }
            /* Host with empty path: "file://localhost" (16 chars) = file root like file:///. */
            else if((end-begin)>=16 && STRNIEQUAL(begin,"file://localhost",16)
               && ((end-begin)==16 || begin[16]=='/'))
            {  /* ok */
            }
            else
            {  return NULL;
            }
         }
         /* Known scheme that passes validation: return the trimmed string as-is. */
         len=end-begin;
         if(fixname=ALLOCTYPE(UBYTE,len+1,MEMF_PUBLIC))
         {  *fixname='\0';
            strncat(fixname,begin,end-begin);
         }
         return fixname;
      }

      /* Unknown scheme vs. Amiga volume:
       * If the first non-alphanumeric is ':' and it is NOT immediately followed by '/',
       * treat as a volume path only when Dos can lock it; same test for other unknown strings. */
      if(firstnonalnum==colon && (colon+1)<end && colon[1]!='/')
      {  if(Urlbarpathlocks(begin,end)) return Fixurlname_mkfile(begin,end);
         return Fixurlname_mkpassthrough(begin,end);
      }

      if(Urlbarpathlocks(begin,end)) return Fixurlname_mkfile(begin,end);
      return Fixurlname_mkpassthrough(begin,end);
   }

   /* No ':' — treat as a web address (http://) like historical AWeb; file:// probing only runs when ':' is present above */
   len=end-begin+7;
   if(fixname=ALLOCTYPE(UBYTE,len+1,MEMF_PUBLIC))
   {  strcpy(fixname,"http://");
      strncat(fixname,begin,end-begin);
   }
   return fixname;
#endif
}

UBYTE *Urllocalfilename(UBYTE *url)
{  UBYTE *p;
   if(STRNIEQUAL(url,"FILE://",7))
   {  if(p=strchr(url+7,'/'))
      {  return p+1;
      }
   }
   return NULL;
}

UBYTE *Urladdfragment(UBYTE *url,UBYTE *fragment)
{  UBYTE *p;
   long l=0;
   if(url) l=strlen(url);
   if(fragment) l+=strlen(fragment)+1;
   if(p=ALLOCTYPE(UBYTE,l+1,MEMF_PUBLIC))
   {  if(url) strcpy(p,url);
      else *p='\0';
      if(fragment)
      {  strcat(p,"#");
         strcat(p,fragment);
      }
   }
   return p;
}

long Auload(void *url,ULONG flags,void *referer,UBYTE *postmsg,void *frame)
{  struct Aumload auml={0};
   auml.method=AUM_LOAD;
   auml.flags=flags;
   auml.referer=referer;
   auml.postmsg=postmsg;
   auml.frame=frame;
   return (long)AmethodA(url,&auml);
}

long Auspecial(void *url,ULONG type)
{  struct Aumspecial aums={0};
   aums.method=AUM_SPECIAL;
   aums.type=type;
   return (long)AmethodA(url,&aums);
}

void Getjspart(struct Url *url,USHORT which,UBYTE **start,long *length)
{  UBYTE *p=url->url;
   *start=NULL;
   *length=0;
   if(STRNIEQUAL(p,"x-jsgenerated:",14))
   {  for(p+=14;*p && *p!='/';p++);
      if(*p) p++;
   }
   if(which==UJP_PROTOCOL) *start=p;
   for(;*p && *p!=':';p++);
   if(which==UJP_PROTOCOL)
   {  *length=p-*start;
      return;
   }
   if(*p==':') p++;
   if(p[0]=='/' && p[1]=='/') p+=2;
   if(which==UJP_HOST || which==UJP_HOSTNAME) *start=p;
   for(;*p && *p!='/' && *p!=':';p++);
   if(which==UJP_HOST)
   {  *length=p-*start;
      return;
   }
   if(*p==':') p++;
   if(which==UJP_PORT) *start=p;
   for(;*p && *p!='/';p++);
   if(which==UJP_PORT || which==UJP_HOSTNAME)
   {  *length=p-*start;
      return;
   }
   if(which==UJP_PATHNAME) *start=p;
   for(;*p && *p!='?' && *p!='#';p++);
   if(which==UJP_PATHNAME)
   {  *length=p-*start;
      return;
   }
   if(which==UJP_SEARCH) *start=p;
   for(;*p && *p!='#';p++);
   if(which==UJP_SEARCH)
   {  *length=p-*start;
      return;
   }
}

void *Repjspart(struct Url *url,USHORT which,UBYTE *part)
{  UBYTE *buf,*p,*q,*start;
   long l=strlen(url->url)+strlen(part)+4;
   void *newurl=url;
   if(buf=ALLOCTYPE(UBYTE,l,0))
   {  p=url->url;
      q=buf;
      start=q;
      for(;*p && *p!=':';*q++=*p++);
      if(which==UJP_PROTOCOL)
      {  strcpy(start,part);
         q=start+strlen(start);
      }
      if(*p==':') *q++=*p++;
      if(p[0]=='/' && p[1]=='/')
      {  *q++=*p++;
         *q++=*p++;
      }
      start=q;
      for(;*p && *p!='/' && *p!=':';*q++=*p++);
      if(which==UJP_HOST)
      {  strcpy(start,part);
         q=start+strlen(start);
      }
      if(*p==':') *q++=*p++;
      if(which==UJP_PORT) start=q;
      for(;*p && *p!='/';*q++=*p++);
      if(which==UJP_HOSTNAME || which==UJP_PORT)
      {  strcpy(start,part);
         q=start+strlen(start);
      }
      start=q;
      for(;*p && *p!='?' && *p!='#';*q++=*p++);
      if(which==UJP_PATHNAME)
      {  if(*part!='/') *start++='/';
         strcpy(start,part);
         q=start+strlen(start);
      }
      start=q;
      for(;*p && *p!='#';*q++=*p++);
      if(which==UJP_SEARCH)
      {  if(*part!='?') *start++='?';
         strcpy(start,part);
         q=start+strlen(start);
      }
      *q='\0';
      newurl=Findurl("",buf,0);
      FREE(buf);
   }
   return newurl;
}

void *Emptyurl(void)
{  return emptyurl;
}

BOOL Mailnewsurl(UBYTE *url)
{  if(STRNIEQUAL(url,"mailto:",7))
   {  return TRUE;
   }
   if(STRNIEQUAL(url,"news:",5) && !strchr(url,'@'))
   {  return TRUE;
   }
   return FALSE;
}

BOOL Issamehost(struct Url *url1,struct Url *url2)
{  char *p,*q;
   if(!url1 || !url2) return FALSE;
   p=url1->url;
   q=url2->url;
   if(!p || !q) return FALSE;
   if(STRNIEQUAL(p,"HTTP://",7)) p+=7;
   else if(STRNIEQUAL(p,"HTTPS://",8)) p+=8;
   else if(STRNIEQUAL(p,"FTP://",6)) p+=6;
   else if(STRNIEQUAL(p,"FILE://",7)) p+=7;
   else return FALSE;
   if(STRNIEQUAL(q,"HTTP://",7)) q+=7;
   else if(STRNIEQUAL(q,"HTTPS://",8)) q+=8;
   else if(STRNIEQUAL(q,"FTP://",6)) q+=6;
   else if(STRNIEQUAL(q,"FILE://",7)) q+=7;
   else return FALSE;
   while(*p && *q && *p!=':' && *p!='/' && *q!=':' && *q!='/' && *p==*q)
   {  p++;
      q++;
   }
   if((!*p || *p==':' || *p=='/') && (!*q || *q==':' || *q=='/')) return TRUE;
   return FALSE;
}
