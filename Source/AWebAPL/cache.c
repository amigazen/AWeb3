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

/* cache.c - AWeb cache object */

#include "aweb.h"
#include "cache.h"
#include "url.h"
#include "asyncio.h"
#include "window.h"
#include "arexx.h"
#include <libraries/locale.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

/*------------------------------------------------------------------------*/

#include "caprivate.h"

LIST(Cache) cache;
static long cachenr=0;

#define NAMESIZE  256

static UBYTE cachename[NAMESIZE];
static UBYTE *awcuname;
static long cachelock;
static BOOL initializing,exitting;
static long nradded=0;
static long sizeadded=0;

#define NRCHKPT   100      /* Nr of files to add before saving AWCR again */
#define CHKAFTER  102400   /* Nr of bytes to add before flushing excess */

struct SignalSemaphore cachesema;

long cadisksize;

static void Deletecache(struct Cache *cac);

/*------------------------------------------------------------------------*/

/* Create a full name from filename and optional extension. Dynamic string. */
static UBYTE *Makename(UBYTE *file,UBYTE *ext)
{  long len=strlen(cachename)+strlen(file)+10;
   UBYTE *name;
   if(ext) len+=strlen(ext)+1;
   if(name=ALLOCTYPE(UBYTE,len,MEMF_PUBLIC))
   {  strcpy(name,cachename);
      AddPart(name,file,len);
      if(ext && *ext)
      {  strcat(name,".");
         strcat(name,ext);
      }
   }
   return name;
}

/*------------------------------------------------------------------------*/

struct Creghdr          /* Cache registration header */
{  UBYTE label[4];      /* 'AWCR' */
   long version;        /* CACHEVERSION */
   ULONG lastnr;        /* last nr used */
};

#define CACHEVERSION    4

struct Cregentry        /* Cache registration entry version 3/4 */
{  ULONG nr;            /* nr of file */
   ULONG date;          /* date stamp */
   ULONG expires;       /* expiry date stamp */
   ULONG cachedate;     /* cache date */
   long size;           /* disk size */
   short type;          /* cache object type */
   short urlsize;       /* max size of url+movedurl including both '\0' bytes */
   /* followed by MIME type '\0' terminated (max 32 bytes) */
   /* followed by URL '\0' terminated (urlsize bytes) */
   /* if COTYPE_MOVED followed by movedto URL '\0' terminated */
};

#define COTYPE_MOVED       1     /* used in AWCR */
#define COTYPE_TEMPMOVED   2
#define COTYPE_DEL         3     /* used in AWCU */
#define MAX_COTYPE         3

/* Returns FALSE if earlier version */
static BOOL Readcachereg(UBYTE *name,long lock)
{  void *fh;
   struct FileInfoBlock *fib;
   long filesize=0,readsize=0,nextsize=0;
   struct Creghdr crh;
   struct Cregentry cre;
   UBYTE mimetype[32],*p;
   short i;
   UBYTE *urlbuf=NULL,*ext;
   UBYTE buf[20];
   long urlbuflen=0;
   void *url;
   struct Cache *cac;
   BOOL ok=TRUE;
#ifndef LOCALONLY
   if(fib=AllocDosObject(DOS_FIB,TAG_END))
   {  if(Examine(lock,fib))
      {  filesize=fib->fib_Size;
         nextsize=filesize/10;
      }
      FreeDosObject(DOS_FIB,fib);
   }
   if(fh=OpenAsync(name,MODE_READ,FILEBLOCKSIZE))
   {  if(ReadAsync(fh,&crh,sizeof(crh))!=sizeof(crh)) goto err;
      ok=FALSE;
      if(!STRNEQUAL(crh.label,"AWCR",4)) goto err;
      if(crh.version<3 || crh.version>CACHEVERSION) goto err;
      ok=TRUE;
      cachenr=crh.lastnr;
      readsize=sizeof(crh);
      while(ReadAsync(fh,&cre,sizeof(cre))==sizeof(cre))
      {  readsize+=sizeof(cre);
         if(cre.type<0 || cre.type>MAX_COTYPE) goto err;
         p=mimetype;
         i=0;
         do
         {  if(ReadAsync(fh,p,1)!=1) goto err;
            readsize+=1;
         } while(*p++ && i<32); /* loop until nullbyte read */
         if(i>=32) goto err;
         if(cre.urlsize>urlbuflen)
         {  if(urlbuf) FREE(urlbuf);
            urlbuflen=(cre.urlsize+257)&0xffffff00;   /* round up */
            if(!(urlbuf=ALLOCTYPE(UBYTE,urlbuflen,0))) goto err;
         }
         if(ReadAsync(fh,urlbuf,cre.urlsize)!=cre.urlsize) goto err;
         readsize+=cre.urlsize;
         if(cre.type==COTYPE_DEL)
         {  if(!(url=Findurl("",urlbuf,0))) goto err;
            cac=(struct Cache *)Agetattr(url,AOURL_Cache);
            if(cac && cac->nr==cre.nr)
            {  Auspecial(url,AUMST_DELETECACHE);
            }
         }
         else
         {  if(!(url=Anewobject(AOTP_URL,AOURL_Url,urlbuf,TAG_END))) goto err;
            ext=Urlfileext(urlbuf);
            if(!ext && Isxbm(mimetype)) ext=Dupstr("xbm",3);
            sprintf(buf,"AWCD%02X/%08X",cre.nr&0x3f,cre.nr);
            p=Makename(buf,ext);
            if(ext) FREE(ext);
            if(cre.expires && cre.expires<=Today())
            {  if(p)
               {  DeleteFile(p);
                  FREE(p);
               }
            }
            else
            {  if(!(cac=Anewobject(AOTP_CACHE,
                  AOCAC_Url,url,
                  AOCAC_Number,cre.nr,
                  AOCAC_Cachedate,cre.cachedate,
                  TAG_END))) goto err;
               cac->name=p;
               cac->date=cre.date;
               cac->expires=cre.expires;
               strcpy(cac->mimetype,mimetype);
               cac->disksize=cre.size;
               cadisksize+=cac->disksize;
               Asetattrs(url,
                  AOURL_Cache,cac,
                  AOURL_Visited,TRUE,
                  TAG_END);
               p=urlbuf+strlen(urlbuf)+1;
               if(cre.type==COTYPE_MOVED)
               {  Asetattrs(url,AOURL_Movedto,p,TAG_END);
               }
               else if(cre.type==COTYPE_TEMPMOVED)
               {  /* Don't restore temporary moves from cache - they should not be cached.
                    * Clear any existing temporary move state. */
               }
               /* Read ETag if version >= 4 */
               if(crh.version>=4)
               {  UBYTE etagbuf[64];
                  UBYTE *ep=etagbuf;
                  i=0;
                  do
                  {  if(ReadAsync(fh,ep,1)!=1) goto err;
                     readsize+=1;
                     i++;
                  } while(*ep++ && i<64); /* loop until nullbyte read */
                  if(i>=64) goto err;
                  strcpy(cac->etag,etagbuf);
               }
               else
               {  /* Version 3: no ETag, initialize to empty */
                  cac->etag[0]='\0';
               }
               if(cre.nr>cachenr) cachenr=cre.nr;
            }
         }
         if(readsize>nextsize)
         {  Setloadreqlevel(readsize,filesize);
            nextsize+=filesize/10;
         }
      }
err:
      CloseAsync(fh);
   }
   if(urlbuf) FREE(urlbuf);
#endif
   return ok;
}

static void Createdirectories(void)
{
#ifndef LOCALONLY
   short i;
   UBYTE name[NAMESIZE+16];
   UBYTE *p;
   long lock;
   strcpy(name,cachename);
   AddPart(name,"AWCD",NAMESIZE+14);
   p=name+strlen(name);
   for(i=0;i<=0x3f;i++)
   {  sprintf(p,"%02X",i);
      if(lock=CreateDir(name)) UnLock(lock);
   }
#endif
}

/* Create an empty cache registration */
static void Createlog(UBYTE *name)
{
#ifndef LOCALONLY
   void *fh;
   struct Creghdr crh;
   if(fh=OpenAsync(name,MODE_WRITE,FILEBLOCKSIZE))
   {  strncpy(crh.label,"AWCR",4);
      crh.version=CACHEVERSION;
      crh.lastnr=cachenr;
      WriteAsync(fh,&crh,sizeof(crh));
      CloseAsync(fh);
   }
#endif
}

/* Write an entry to the cache registration file */
static void Writeregentry(void *fh,struct Cache *cac,BOOL del)
{
#ifndef LOCALONLY
   struct Cregentry cre;
   short urlsize,movedsize;
   UBYTE *url=(UBYTE *)Agetattr(cac->url,AOURL_Realurl);
   UBYTE *movedto=NULL;
   cre.nr=cac->nr;
   cre.date=cac->date;
   cre.expires=cac->expires;
   cre.cachedate=cac->cachedate;
   cre.size=cac->disksize;
   cre.type=0;
   urlsize=strlen(url)+1;
   movedsize=0;
   if(del) cre.type=COTYPE_DEL;
   else if(movedto=(UBYTE *)Agetattr(cac->url,AOURL_Movedto))
   {  cre.type=COTYPE_MOVED;
   }
   /* Don't write temporary moves to cache - they should not be cached */
   /* else if(movedto=(UBYTE *)Agetattr(cac->url,AOURL_Tempmovedto))
   {  cre.type=COTYPE_TEMPMOVED;
   } */
   if(movedto)
   {  movedsize=strlen(movedto)+1;
   }
   cre.urlsize=urlsize+movedsize;
   WriteAsync(fh,&cre,sizeof(cre));
   WriteAsync(fh,cac->mimetype,strlen(cac->mimetype)+1);
   WriteAsync(fh,url,urlsize);
   if(movedto && !del) WriteAsync(fh,movedto,movedsize);
   /* Write ETag (version 4) */
   WriteAsync(fh,cac->etag,strlen(cac->etag)+1);
#endif
}

static void Savecachereg(BOOL nodelete)
{
#ifndef LOCALONLY
   UBYTE *name;
   void *fh;
   struct Creghdr crh;
   struct Cache *cac,*ucac;
   BOOL ok=FALSE;
   if(name=Makename("AWCR",NULL))
   {  if(fh=OpenAsync(name,MODE_WRITE,FILEBLOCKSIZE))
      {  strncpy(crh.label,"AWCR",4);
         crh.version=CACHEVERSION;
         crh.lastnr=cachenr;
         WriteAsync(fh,&crh,sizeof(crh));
         for(cac=cache.first;cac->next;cac=cac->next)
         {  ucac=(struct Cache *)Agetattr(cac->url,AOURL_Cache);
            if(cac==ucac)
            {  Writeregentry(fh,cac,FALSE);
               if(nodelete) cac->flags|=CACF_NODELETE;
            }
         }
         ok=TRUE;
         if(CloseAsync(fh)>=0)
         {  DeleteFile(awcuname);
         }
         else
         {  DeleteFile(name);
         }
      }
      FREE(name);
   }
   if(nodelete && !ok)
   {  for(cac=cache.first;cac->next;cac=cac->next)
      {  ucac=(struct Cache *)Agetattr(cac->url,AOURL_Cache);
         if(cac==ucac)
         {  cac->flags|=CACF_NODELETE;
         }
      }
   }
#endif
}

/* Add an entry to the registration */
static void Addregentry(struct Cache *cac,BOOL del)
{
#ifndef LOCALONLY
   void *fh;
   if(!exitting)
   {  if(fh=OpenAsync(awcuname,MODE_APPEND,FILEBLOCKSIZE))
      {  Writeregentry(fh,cac,del);
         CloseAsync(fh);
      }
      if(++nradded>NRCHKPT)
      {  UBYTE *awcrname=Makename("AWCR",NULL);  
         if(awcrname)
         {  Savecachereg(FALSE);
            Rename(awcrname,awcuname);
            FREE(awcrname);
            nradded=0;
         }
      }
   }
#endif
}

/*------------------------------------------------------------------------*/

/* Create file and write entry in registration. */
static void Opencacfile(struct Cache *cac)
{  UBYTE *urlname=(UBYTE *)Agetattr(cac->url,AOURL_Url);
   UBYTE *ext=Urlfileext(urlname);
   UBYTE buf[20];
   sprintf(buf,"AWCD%02X/%08X",cac->nr&0x3f,cac->nr);
   cac->name=Makename(buf,ext);
   if(ext) FREE(ext);
   cac->fh=OpenAsync(cac->name,MODE_WRITE,FILEBLOCKSIZE);
}

/* Delete the associated file, and write entry in registration. */
static void Deletecache(struct Cache *cac)
{  DeleteFile(cac->name);
   if(!initializing)
   {  /* Don't add a DEL entry when we are reading in the registration */
      Addregentry(cac,TRUE);
   }
}

/* Set the file's comment */
static void Setcomment(struct Cache *cac)
{  UBYTE *urlname,*comment;
   long len;
   if(urlname=(UBYTE *)Agetattr(cac->url,AOURL_Url))
   {  len=strlen(urlname);
      if(comment=Dupstr(urlname,MIN(80,len)))
      {  SetComment(cac->name,comment);
         FREE(comment);
      }
   }
}

/*------------------------------------------------------------------------*/

/* Delete all files beyond limit, last touched first */
static void Flushexcess(void)
{  struct Cache *cac,*next;
   long max=prefs.network.cadisksize*1024;
   ObtainSemaphore(&cachesema);
   for(cac=cache.first;cac->next && cadisksize>max;cac=next)
   {  next=cac->next;
      Auspecial(cac->url,AUMST_DELETECACHE);
   }
   ReleaseSemaphore(&cachesema);
   sizeadded=0;
}

/* Delete all files or all files for this type and/or pattern */
static void Flushtype(USHORT type,UBYTE *pattern)
{  struct Cache *cac,*next;
   long len;
   UBYTE *parsepat=NULL,*url,*p;
   BOOL scheme,sel;
   if(pattern)
   {  len=2*strlen(pattern)+4;
      parsepat=ALLOCTYPE(UBYTE,len,0);
      if(parsepat)
      {  if(ParsePatternNoCase(pattern,parsepat,len)<0)
         {  FREE(parsepat);
            parsepat=NULL;
         }
      }
      scheme=BOOLVAL(strstr(pattern,"://"));
   }
   ObtainSemaphore(&cachesema);
   for(cac=cache.first;cac->next;cac=next)
   {  next=cac->next;
      /* If documents, delete "text/..", if images delete not "text/.." */
      if(type)
      {  sel=(type==CACFT_DOCUMENTS) == BOOLVAL(STRNIEQUAL(cac->mimetype,"TEXT/",5));
      }
      else sel=TRUE;
      if(sel && parsepat)
      {  url=(UBYTE *)Agetattr(cac->url,AOURL_Url);
         if(url)
         {  if(!scheme)
            {  p=strstr(url,"://");
               if(p) p+=3;
               else p=url;
            }
            else p=url;
            sel=MatchPatternNoCase(parsepat,p);
         }
      }
      if(sel)
      {  Auspecial(cac->url,AUMST_DELETECACHE);
      }
   }
   ReleaseSemaphore(&cachesema);
   if(parsepat) FREE(parsepat);
}

/* Send constructed HTTP headers */
static void Sendinfo(struct Cache *cac,void *fetch)
{  UBYTE buf[64];
   UBYTE datebuf[32];
   Asrcupdatetags(cac->url,fetch,AOURL_Fromcache,TRUE,TAG_END);
   if(*cac->mimetype)
   {  sprintf(buf,"Content-Type: %s",cac->mimetype);
      Asrcupdatetags(cac->url,fetch,AOURL_Header,buf,TAG_END);
   }
   sprintf(buf,"Content-Length: %d",cac->disksize);
   Asrcupdatetags(cac->url,fetch,AOURL_Header,buf,TAG_END);
   if(cac->date)
   {  Makedate(cac->date,datebuf);
      sprintf(buf,"Last-Modified: %s",datebuf);
      Asrcupdatetags(cac->url,fetch,AOURL_Header,buf,TAG_END);
   }
   if(cac->expires)
   {  Makedate(cac->expires,datebuf);
      sprintf(buf,"Expires: %s",datebuf);
      Asrcupdatetags(cac->url,fetch,AOURL_Header,buf,TAG_END);
   }
   if(*cac->etag)
   {  sprintf(buf,"ETag: %s",cac->etag);
      Asrcupdatetags(cac->url,fetch,AOURL_Header,buf,TAG_END);
   }
}

/*------------------------------------------------------------------------*/

#ifndef LOCALONLY
struct Cafix
{  NODE(Cafix);
   struct Cache *cac;
   UBYTE name[40];            /* including 'AWCD##/' */
   short directory;
};

struct Cafixdel
{  NODE(Cafixdel);
   UBYTE name[40];
};

static struct Cafix *Newcafix(struct Cache *cac)
{  struct Cafix *cf=ALLOCSTRUCT(Cafix,1,MEMF_CLEAR);
   if(cf)
   {  cf->cac=cac;
      if(cac->name)
      {  strncpy(cf->name,Cfnameshort(cac->name),sizeof(cf->name));
         cf->directory=cac->nr&0x3f;
      }
   }
   return cf;
}

static struct Cafixdel *Newcafixdel(UBYTE *name)
{  struct Cafixdel *cd=ALLOCSTRUCT(Cafixdel,1,MEMF_CLEAR);
   if(cd)
   {  strncpy(cd->name,name,39);
   }
   return cd;
}

static BOOL Makecafixlist(LIST(Cafix) *list)
{  struct Cafix *cf;
   struct Cache *cac;
   for(cac=cache.first;cac->next;cac=cac->next)
   {  if(cac->name)
      {  if(cf=Newcafix(cac)) ADDTAIL(list,cf);
         else return FALSE;
      }
   }
   return TRUE;
}

static void Deletedir(UBYTE *name)
{  __aligned struct FileInfoBlock fib={0};
   long lock,oldcd;
   LIST(Cafixdel) dellist;
   struct Cafixdel *cd;
   NEWLIST(&dellist);
   if(lock=Lock(name,SHARED_LOCK))
   {  oldcd=CurrentDir(lock);
      if(Examine(lock,&fib))
      {  while(ExNext(lock,&fib))
         {  if(fib.fib_DirEntryType>0)
            {  Deletedir(fib.fib_FileName);
            }
            if(cd=Newcafixdel(fib.fib_FileName)) ADDTAIL(&dellist,cd);
         }
      }
      while(cd=REMHEAD(&dellist))
      {  DeleteFile(cd->name);
         FREE(cd);
      }
      CurrentDir(oldcd);
      UnLock(lock);
   }
}

static BOOL Fixroot(void *preq)
{  __aligned struct FileInfoBlock fib={0};
   long oldcd=CurrentDir(cachelock);
   LIST(Cafixdel) dellist;
   struct Cafixdel *cd;
   UBYTE *p;
   BOOL ok=FALSE;
   NEWLIST(&dellist);
   if(Examine(cachelock,&fib) && fib.fib_DirEntryType>0)
   {  while(ExNext(cachelock,&fib))
      {  if(fib.fib_DirEntryType<0)          /* plain file */
         {  if(!STREQUAL(fib.fib_FileName,"AWCU") && !STREQUAL(fib.fib_FileName,"AWCK"))
            {  if(cd=Newcafixdel(fib.fib_FileName)) ADDTAIL(&dellist,cd);
            }
         }
         else if(fib.fib_DirEntryType>0)     /* directory */
         {  p=fib.fib_FileName;
            if(!(STRNEQUAL(p,"AWCD",4) && p[4]>='0' && p[4]<='3'
            && isxdigit(p[5]) && p[6]=='\0'))
            {  Deletedir(fib.fib_FileName);
               if(cd=Newcafixdel(fib.fib_FileName)) ADDTAIL(&dellist,cd);
            }
         }
         if(Checkprogressreq(preq)) goto err;
      }
   }
   while(cd=REMHEAD(&dellist))
   {  DeleteFile(cd->name);
      FREE(cd);
   }
   ok=TRUE;
err:
   while(cd=REMHEAD(&dellist)) FREE(cd);
   CurrentDir(oldcd);
   return ok;
}

static BOOL Fixcachedir(void *preq,LIST(Cafix) *list,short dirnum)
{  struct Cafix *cf;
   __aligned struct FileInfoBlock fib={0};
   UBYTE name[8];
   long oldcd=CurrentDir(cachelock);
   long dirlock;
   LIST(Cafixdel) dellist;
   struct Cafixdel *cd;
   BOOL ok=FALSE,fileok;
   NEWLIST(&dellist);
   sprintf(name,"AWCD%02X",dirnum);
   if(dirlock=Lock(name,SHARED_LOCK))
   {  CurrentDir(dirlock);
      if(Examine(dirlock,&fib))
      {  while(ExNext(dirlock,&fib))
         {  if(fib.fib_DirEntryType<0)       /* plain file */
            {  fileok=FALSE;
               for(cf=list->first;cf->next;cf=cf->next)
               {  if(cf->directory==dirnum
                  && STREQUAL(fib.fib_FileName,cf->name+7)) /* skip 'AWCDxx/' */
                  {  REMOVE(cf);
                     FREE(cf);
                     fileok=TRUE;
                     break;
                  }
               }
               if(!fileok)
               {  if(cd=Newcafixdel(fib.fib_FileName)) ADDTAIL(&dellist,cd);
               }
            }
            else if(fib.fib_DirEntryType>0)  /* directory */
            {  Deletedir(fib.fib_FileName);
               if(cd=Newcafixdel(fib.fib_FileName)) ADDTAIL(&dellist,cd);
            }
            if(Checkprogressreq(preq)) goto err;
         }
      }
      while(cd=REMHEAD(&dellist))
      {  DeleteFile(cd->name);
         FREE(cd);
      }
      ok=TRUE;
err:
      while(cd=REMHEAD(&dellist)) FREE(cd);
      UnLock(dirlock);
   }
   else
   {  if(dirlock=CreateDir(name)) UnLock(dirlock);
      ok=TRUE;
   }
   CurrentDir(oldcd);
   return ok;
}

static void Fixcachereg(LIST(Cafix) *list)
{  struct Cafix *cf;
   while(cf=REMHEAD(list))
   {  Auspecial(cf->cac->url,AUMST_DELETECACHE);
      FREE(cf);
   }
}

static void Dofixcache(short code,void *data)
{  void *preq;
   struct Cafix *cf;
   short i;
   UBYTE *awcrname=Makename("AWCR",NULL);
   if(code && awcrname)
   {  LIST(Cafix) list;
      NEWLIST(&list);
      Busypointer(TRUE);
      ObtainSemaphore(&cachesema);
      if(preq=Openprogressreq(AWEBSTR(MSG_FIXCACHE_PROGRESS)))
      {  if(!Makecafixlist(&list)) goto err;
         if(Checkprogressreq(preq)) goto err;
         Setprogressreq(preq,1,67);
         if(!Fixroot(preq)) goto err;
         Setprogressreq(preq,2,67);
         for(i=0;i<64;i++)
         {  if(!Fixcachedir(preq,&list,i)) goto err;
            Setprogressreq(preq,i+3,67);
         }
         Fixcachereg(&list);
         Savecachereg(FALSE);
         Rename(awcrname,awcuname);
         Setprogressreq(preq,67,67);
err:
         while(cf=REMHEAD(&list)) FREE(cf);
         Closeprogressreq(preq);
      }
      ReleaseSemaphore(&cachesema);
      Busypointer(FALSE);
   }
   if(data) FREE(data);
   if(awcrname) FREE(awcrname);
}
#endif /* !LOCALONLY */

/*------------------------------------------------------------------------*/

static long Setcache(struct Cache *cac,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCAC_Url:
            if(!cac->url) cac->url=(void *)tag->ti_Data;
            break;
         case AOCAC_Name:
            if(!cac->name) cac->name=(UBYTE *)tag->ti_Data;
            break;
         case AOCAC_Number:
            if(!cac->nr) cac->nr=tag->ti_Data;
            break;
         case AOCAC_Cachedate:
            if(!cac->cachedate) cac->cachedate=tag->ti_Data;
            break;
         case AOCAC_Touched:
            if(tag->ti_Data)
            {  ObtainSemaphore(&cachesema);
               REMOVE(cac);
               ADDTAIL(&cache,cac);
               ReleaseSemaphore(&cachesema);
            }
            break;
         case AOCAC_Sendinfo:
            Sendinfo(cac,(void *)tag->ti_Data);
            break;
      }
   }
   return 0;
}

static struct Cache *Newcache(struct Amset *ams)
{  struct Cache *cac;
   if(cac=Allocobject(AOTP_CACHE,sizeof(struct Cache),ams))
   {  Setcache(cac,ams);
      if(!cac->nr) cac->nr=++cachenr;
      if(!cac->cachedate) cac->cachedate=Today();
      ObtainSemaphore(&cachesema);
      ADDTAIL(&cache,cac);
      ReleaseSemaphore(&cachesema);
   }
   return cac;
}

static long Getcache(struct Cache *cac,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCAC_Name:
            PUTATTR(tag,cac->name);
            break;
         case AOCAC_Url:
            PUTATTR(tag,cac->url);
            break;
         case AOCAC_Number:
            PUTATTR(tag,cac->nr);
            break;
         case AOCAC_Cachedate:
            PUTATTR(tag,cac->cachedate);
            break;
         case AOCAC_Lastmodified:
            PUTATTR(tag,cac->date);
            break;
         case AOCAC_Contenttype:
            PUTATTR(tag,*cac->mimetype?cac->mimetype:NULL);
            break;
         case AOCAC_Expired:
            PUTATTR(tag,cac->expires && cac->expires<=Today());
            break;
         case AOCAC_Etag:
            PUTATTR(tag,*cac->etag?cac->etag:NULL);
            break;
      }
   }
   return 0;
}

/* URL already maps to a host file — avoid writing a second copy under Cache:AWCD */
static BOOL Cacheurlislocalfile(struct Cache *cac)
{  UBYTE *u;
   long ulen;

   if(!cac || !cac->url) return FALSE;
   u=(UBYTE *)Agetattr(cac->url,AOURL_Url);
   if(!u) return FALSE;
   if(STRNIEQUAL(u,"FILE:///",8)) return TRUE;
   if(STRNIEQUAL(u,"FILE://LOCALHOST/",17)) return TRUE;
   ulen=(long)strlen((char *)u);
   if(ulen>=16 && STRNIEQUAL(u,"FILE://LOCALHOST",16) && (!u[16] || u[16]=='/'))
      return TRUE;
   return FALSE;
}

static long Srcupdatecache(struct Cache *cac,struct Amsrcupdate *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *data=NULL;
   long length=0;
   BOOL eof=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOURL_Contenttype:
            strncpy(cac->mimetype,(UBYTE *)tag->ti_Data,31);
            break;
         case AOURL_Data:
            data=(UBYTE *)tag->ti_Data;
            break;
         case AOURL_Datalength:
            length=tag->ti_Data;
            break;
         case AOURL_Eof:
            if(tag->ti_Data) eof=TRUE;
            break;
         case AOURL_Lastmodified:
            cac->date=tag->ti_Data;
            break;
         case AOURL_Expires:
            cac->expires=tag->ti_Data;
            break;
         case AOURL_Etag:
            strncpy(cac->etag,(UBYTE *)tag->ti_Data,63);
            cac->etag[63]='\0';
            break;
         case AOURL_Terminate:
            eof=TRUE;
            break;
      }
   }
   if(data || eof)
   {  if(!cac->name && !Cacheurlislocalfile(cac)) Opencacfile(cac);
   }
   if(data)
   {  if(length && cac->fh)
      {  WriteAsync(cac->fh,data,length);
         cac->disksize+=length;
         cadisksize+=length;
         sizeadded+=length;
         if(sizeadded>CHKAFTER) Flushexcess();
      }
   }
   if(eof)
   {  if(cac->fh)
      {  CloseAsync(cac->fh);
         Setcomment(cac);
         cac->fh=NULL;
         Addregentry(cac,FALSE);
         Addcabrobject(cac);
         Flushexcess();
      }
   }
   return 0;
}

static void Disposecache(struct Cache *cac)
{  ObtainSemaphore(&cachesema);
   REMOVE(cac);
   ReleaseSemaphore(&cachesema);
   cadisksize-=cac->disksize;
   Remcabrobject(cac);
   if(cac->fh) CloseAsync(cac->fh);
   if(cac->name)
   {  if(!(cac->flags&CACF_NODELETE)) Deletecache(cac);
      FREE(cac->name);
   }
   Amethodas(AOTP_OBJECT,cac,AOM_DISPOSE);
}

static void Deinstallcache(void)
{  while(cache.first->next) Adisposeobject(cache.first);
   if(awcuname) FREE(awcuname);
   if(cachelock) UnLock(cachelock);
}

static long Dispatch(struct Cache *cac,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newcache((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setcache(cac,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getcache(cac,(struct Amset *)amsg);
         break;
      case AOM_SRCUPDATE:
         result=Srcupdatecache(cac,(struct Amsrcupdate *)amsg);
         break;
      case AOM_DISPOSE:
         Disposecache(cac);
         break;
      case AOM_DEINSTALL:
         Deinstallcache();
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installcache(void)
{  NEWLIST(&cache);
   InitSemaphore(&cachesema);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_CACHE,Dispatch)) return FALSE;
   return TRUE;
}

BOOL Initcache(void)
{  UBYTE *awcr;
   long lock;
   BOOL corrupt=FALSE;
   cachelock=Lock(prefs.network.cachepath,SHARED_LOCK);
   if(!cachelock) cachelock=Lock("T:",SHARED_LOCK);
   if(cachelock) NameFromLock(cachelock,cachename,NAMESIZE);
   if(!(awcuname=Makename("AWCU",NULL))) return FALSE;
   if(!(awcr=Makename("AWCR",NULL))) return FALSE;
#ifndef LOCALONLY
   initializing=TRUE;
   if(lock=Lock(awcuname,SHARED_LOCK))
   {  /* old cache log exists - rebuild cache */
      corrupt=!Readcachereg(awcuname,lock);
      UnLock(lock);
      Savecachereg(FALSE);   /* create new reg file and delete AWCU */
      Rename(awcr,awcuname);
   }
   else
   {  if(lock=Lock(awcr,SHARED_LOCK))
      {  corrupt=!Readcachereg(awcr,lock);
         UnLock(lock);
         Rename(awcr,awcuname);
      }
      else
      {  Createdirectories();
         Createlog(awcuname);
      }
   }
   initializing=FALSE;
   if(corrupt)
   {  if(Syncrequest(AWEBSTR(MSG_REQUEST_TITLE),
         AWEBSTR(MSG_FIXCACHE_CORRUPT),AWEBSTR(MSG_FIXCACHE_BUTTONS),0))
      {  Dofixcache(1,NULL);
      }
   }
#endif
   FREE(awcr);
   return TRUE;
}

void Exitcache(void)
{  if(cache.first)
   {  Savecachereg(TRUE);
   }
   /* Prevent ongoing fetches from creating a bogus AWCU */
   exitting=TRUE;
}

UBYTE *Cachename(void)
{  return cachename;
}

void Flushcache(USHORT type)
{  Flushcachepattern(type,NULL);
}

void Flushcachepattern(USHORT type,UBYTE *pattern)
{  switch(type)
   {  case CACFT_DOCUMENTS:
      case CACFT_IMAGES:
         Flushtype(type,pattern);
         break;
      case CACFT_EXCESS:
         Flushexcess();
         break;
      case CACFT_ALL:
         Flushtype(0,pattern);
         break;
   }
}

/* Find cachedir/name part of file name */
UBYTE *Cfnameshort(UBYTE *name)
{  short i=0;
   UBYTE *p;
   for(p=name+strlen(name)-1;p>name;p--)
   {  if(*p=='/' && ++i>1) break;
   }
   if(*p=='/') p++;
   return p;
}

void Fixcache(BOOL force)
{
#ifndef LOCALONLY
   UBYTE *p=haiku?HAIKU20:AWEBSTR(MSG_FIXCACHE_WARNING);
   UBYTE *buf;
   long len=strlen(p)+strlen(cachename)+8;
   if(force)
   {  Dofixcache(TRUE,NULL);
   }
   else
   {  if(buf=ALLOCTYPE(UBYTE,len,0))
      {  sprintf(buf,p,cachename);
         if(!Asyncrequest(AWEBSTR(MSG_REQUEST_TITLE),buf,AWEBSTR(MSG_FIXCACHE_BUTTONS),
            Dofixcache,NULL))
         {  FREE(buf);
         }
      }
   }
#endif
}

void Getcachecontents(struct Arexxcmd *ac,UBYTE *stem,UBYTE *pattern)
{  
#ifndef LOCALONLY
   UBYTE buf[32];
   UBYTE *parsepat=NULL,*url,*p;
   BOOL scheme,sel;
   struct Cache *cac;
   long i,len;
   if(pattern)
   {  len=2*strlen(pattern)+4;
      parsepat=ALLOCTYPE(UBYTE,len,0);
      if(parsepat)
      {  if(ParsePatternNoCase(pattern,parsepat,len)<0)
         {  FREE(parsepat);
            parsepat=NULL;
         }
      }
      scheme=BOOLVAL(strstr(pattern,"://"));
   }
   ObtainSemaphore(&cachesema);
   i=0;
   for(cac=cache.first;cac->next;cac=cac->next)
   {  url=(UBYTE *)Agetattr(cac->url,AOURL_Url);
      if(parsepat)
      {  if(url)
         {  if(!scheme)
            {  p=strstr(url,"://");
               if(p) p+=3;
               else p=url;
            }
            else p=url;
            sel=MatchPatternNoCase(parsepat,p);
         }
      }
      if(sel)
      {  i++;
         Setstemvar(ac,stem,i,"URL",url);
         Setstemvar(ac,stem,i,"TYPE",cac->mimetype);
         sprintf(buf,"%d",cac->disksize);
         Setstemvar(ac,stem,i,"SIZE",buf);
         sprintf(buf,"%d",cac->cachedate/86400);
         Setstemvar(ac,stem,i,"DATE",buf);
         Setstemvar(ac,stem,i,"FILE",Cfnameshort(cac->name));
      }
   }
   ReleaseSemaphore(&cachesema);
   sprintf(buf,"%d",i);
   Setstemvar(ac,stem,0,NULL,buf);
   if(parsepat) FREE(parsepat);
#endif
}

