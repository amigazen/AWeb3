/**********************************************************************
 *
 * AWEB4 disk cache — strategy-compliant publish path (cache_strategy.md).
 *
 * Included from cache.c only when AWEB4 is defined. Classic AWCU/AWCR
 * behaviour lives in the #else half of cache.c.
 *
 * Rules enforced here:
 *  - MODE_NEWFILE only on staging (.part); publish via Rename
 *  - Whole-index rewrite to index.tmp then Rename over index; then Flush
 *  - Coalesced >=32 KiB sequential writes; no SetComment; no AWCU append
 *  - Batched volume Flush on idle / every K publishes / exit
 *  - Hash-sharded immutable names; index-first eviction
 *
 **********************************************************************/

#include "timer.h"

/*------------------------------------------------------------------------*/

#define NAMESIZE           256
#define CAC_WRITECHUNK     32768  /* Application coalesce / OpenAsync size */
#define CAC_CHKAFTER       102400 /* Bytes before quota sweep */
#define AWI4_VERSION       1

#define CACFS_UNKNOWN      0
#define CACFS_JOURNALED    1
#define CACFS_FFS          2
#define CACFS_FFS_DC       3

#define CAC_K_DEFAULT      32
#define CAC_K_FFS          16
#define CAC_IDLE_DEFAULT_S 1
#define CAC_IDLE_DEFAULT_U 500000  /* 1.5 s total with seconds */
#define CAC_IDLE_FFS_S     1
#define CAC_IDLE_FFS_U     0

/* Disk type IDs (dos/dos.h + common third-party) */
#define CAC_ID_FFS         0x444F5301L  /* DOS\1 */
#define CAC_ID_FFS_I       0x444F5303L  /* DOS\3 */
#define CAC_ID_OFS_DC      0x444F5304L  /* DOS\4 */
#define CAC_ID_FFS_DC      0x444F5305L  /* DOS\5 */
#define CAC_ID_FFS_LN      0x444F5307L  /* DOS\7 */
#define CAC_ID_FFS_CLN     0x444F5308L  /* DOS\8 */
#define CAC_ID_SFS0        0x53465300L  /* SFS\0 */
#define CAC_ID_SFS2        0x53465302L  /* SFS\2 */
#define CAC_ID_PFS0        0x50465300L  /* PFS\0 */
#define CAC_ID_PFS2        0x50465302L  /* PFS\2 */
#define CAC_ID_PFS3        0x50465303L  /* PFS\3 */

struct Awi4hdr
{  UBYTE label[4];      /* 'AWI4' */
   long version;
   ULONG lastnr;
};

struct Awi4entry
{  ULONG date;
   ULONG expires;
   ULONG cachedate;
   long size;
   ULONG checksum;
   short type;
   short urlsize;       /* url + optional movedto */
   short pathsize;      /* relative path including NUL */
   /* mime NUL, path NUL, url(+moved) as urlsize, etag NUL */
};

#define COTYPE_MOVED       1
#define COTYPE_DEL         3
#define MAX_COTYPE         3

struct Cadel
{  NODE(Cadel);
   UBYTE *name;
};

static UBYTE cachename[NAMESIZE];
static long cachelock;
static BOOL initializing,exitting;
static long sizeadded=0;
static long cachenr=0;
static long publishdirty=0;
static short cachefstype=CACFS_UNKNOWN;
static short cacheflushk=CAC_K_DEFAULT;
static long cacheidles=CAC_IDLE_DEFAULT_S;
static long cacheidleu=CAC_IDLE_DEFAULT_U;
static void *cachectl=NULL;
static void *cacheflushtimer=NULL;
static BOOL indexdirty=FALSE;
static LIST(Cadel) delqueue;
static struct SignalSemaphore cachewritesema;

#define AOCCTL_Dummy       AOBJ_DUMMYTAG(AOTP_CACHECTL)
#define AOCCTL_Flushready  (AOCCTL_Dummy+1)

static struct TagItem cacheflushmap[]=
{  AOTIM_Ready,AOCCTL_Flushready,
   TAG_END
};

static void Deletecache(struct Cache *cac);
static void Publishcacheindex(BOOL dovolumeflush);
static void Docachevolumeflush(void);
static void Schedulecacheflush(void);
static void Rundelqueue(void);
static void Abortstaging(struct Cache *cac);

/*------------------------------------------------------------------------*/

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

/* FNV-1a 32-bit — used twice to build a 16-hex immutable object id. */
static ULONG Fnv1a32(UBYTE *p,long len,ULONG hash)
{  long i;
   for(i=0;i<len;i++)
   {  hash^=(ULONG)p[i];
      hash*=16777619UL;
   }
   return hash;
}

static void Hashcacheid(struct Cache *cac,UBYTE *out16)
{  UBYTE *url;
   UBYTE datebuf[12];
   ULONG h1,h2;
   long n;

   url=(UBYTE *)Agetattr(cac->url,AOURL_Realurl);
   if(!url) url=(UBYTE *)Agetattr(cac->url,AOURL_Url);
   h1=2166136261UL;
   h2=2166136261UL;
   if(url)
   {  n=(long)strlen((char *)url);
      h1=Fnv1a32(url,n,h1);
      h2=Fnv1a32(url,n,h2^0x9e3779b9UL);
   }
   h1=Fnv1a32(cac->etag,(long)strlen((char *)cac->etag)+1,h1);
   sprintf((char *)datebuf,"%08lx",(unsigned long)cac->date);
   h2=Fnv1a32(datebuf,8,h2);
   sprintf((char *)out16,"%08lx%08lx",(unsigned long)h1,(unsigned long)h2);
}

static BOOL Ensuredir(UBYTE *path)
{  long lock;
   if(lock=Lock(path,SHARED_LOCK))
   {  UnLock(lock);
      return TRUE;
   }
   if(lock=CreateDir(path))
   {  UnLock(lock);
      return TRUE;
   }
   return FALSE;
}

/* Create Cache/<aa>/<bb>/ for a 16-hex id; return relative "aa/bb/<id>". */
static UBYTE *Makerelpath(UBYTE *id16)
{  UBYTE *rel;
   UBYTE dir1[NAMESIZE],dir2[NAMESIZE];
   UBYTE aa[3],bb[3];
   long len;

   if(!id16 || strlen((char *)id16)<4) return NULL;
   len=6+(long)strlen((char *)id16)+1;
   if(!(rel=ALLOCTYPE(UBYTE,len,MEMF_PUBLIC))) return NULL;
   aa[0]=id16[0]; aa[1]=id16[1]; aa[2]='\0';
   bb[0]=id16[2]; bb[1]=id16[3]; bb[2]='\0';
   sprintf((char *)rel,"%s/%s/%s",aa,bb,id16);

   strcpy((char *)dir1,(char *)cachename);
   AddPart(dir1,(char *)aa,NAMESIZE);
   if(!Ensuredir(dir1)) { FREE(rel); return NULL; }
   strcpy((char *)dir2,(char *)dir1);
   AddPart(dir2,(char *)bb,NAMESIZE);
   if(!Ensuredir(dir2)) { FREE(rel); return NULL; }
   return rel;
}

static short Classifydisktype(LONG disktype)
{  if(disktype==CAC_ID_OFS_DC || disktype==CAC_ID_FFS_DC)
      return CACFS_FFS_DC;
   if(disktype==CAC_ID_FFS || disktype==CAC_ID_FFS_I
   || disktype==CAC_ID_FFS_LN || disktype==CAC_ID_FFS_CLN)
      return CACFS_FFS;
   if(disktype==CAC_ID_SFS0 || disktype==CAC_ID_SFS2
   || disktype==CAC_ID_PFS0 || disktype==CAC_ID_PFS2 || disktype==CAC_ID_PFS3)
      return CACFS_JOURNALED;
   /* Other DOS\n: treat as FFS family without dir-cache */
   if((disktype&0xffffff00L)==0x444f5300L) return CACFS_FFS;
   return CACFS_UNKNOWN;
}

static void Applyfstuning(short fstype)
{  cachefstype=fstype;
   if(fstype==CACFS_FFS || fstype==CACFS_FFS_DC)
   {  cacheflushk=CAC_K_FFS;
      cacheidles=CAC_IDLE_FFS_S;
      cacheidleu=CAC_IDLE_FFS_U;
   }
   else
   {  cacheflushk=CAC_K_DEFAULT;
      cacheidles=CAC_IDLE_DEFAULT_S;
      cacheidleu=CAC_IDLE_DEFAULT_U;
   }
}

static BOOL Cacheonbootvolume(void)
{  long syslock;
   BOOL same=FALSE;
   if(!cachelock) return FALSE;
   if(syslock=Lock("SYS:",SHARED_LOCK))
   {  long rel=SameLock(cachelock,syslock);
      /* Same volume is enough: cache churn should not dirty the boot disk. */
      if(rel==LOCK_SAME || rel==LOCK_SAME_VOLUME) same=TRUE;
      UnLock(syslock);
   }
   return same;
}

static void Maybewarnfilesystem(void)
{  UBYTE *msg;
   long result;
   BOOL suboptimal;

#ifndef LOCALONLY
   suboptimal=(cachefstype==CACFS_FFS_DC) || Cacheonbootvolume();
   if(!suboptimal) return;
   if(prefs.network.cachefswarnpath
   && prefs.network.cachepath
   && STREQUAL(prefs.network.cachefswarnpath,prefs.network.cachepath))
      return;

   if(cachefstype==CACFS_FFS_DC && Cacheonbootvolume())
   {  msg="The disk cache is on the boot volume using FFS with directory "
         "cache (DOS\\4/DOS\\5). For reliability and less thrashing, use a "
         "dedicated SFS/PFS3 or non-DC FFS partition.";
   }
   else if(cachefstype==CACFS_FFS_DC)
   {  msg="The disk cache volume uses FFS with directory cache (DOS\\4/DOS\\5). "
         "A non-directory-cache FFS or SFS/PFS3 partition is better for a "
         "high-churn browser cache.";
   }
   else
   {  msg="The disk cache is on the boot/system volume. A dedicated cache "
         "partition avoids keeping the system volume dirty under FFS.";
   }

   result=Syncrequest(AWEBSTR(MSG_REQUEST_TITLE),msg,"_OK|_Don't show again",0);
   if(result==2 && prefs.network.cachepath)
   {  /* FREEANDNULL is a defprefs.c-local macro, not a linkable symbol. */
      if(prefs.network.cachefswarnpath) FREE(prefs.network.cachefswarnpath);
      prefs.network.cachefswarnpath=Dupstr(prefs.network.cachepath,-1);
      Savenetworkprefs(&prefs.network,FALSE,NULL);
   }
#endif
}

static void Detectcachefs(void)
{  __aligned struct InfoData id={0};
   if(cachelock && Info(cachelock,&id))
      Applyfstuning(Classifydisktype(id.id_DiskType));
   else
      Applyfstuning(CACFS_UNKNOWN);
   Maybewarnfilesystem();
}

/*------------------------------------------------------------------------*/

static void Docachevolumeflush(void)
{
#ifndef LOCALONLY
   if(cachelock) Flush(cachelock);
#endif
}

static void Rundelqueue(void)
{  struct Cadel *cd;
#ifndef LOCALONLY
   while(cd=REMHEAD(&delqueue))
   {  if(cd->name)
      {  DeleteFile(cd->name);
         FREE(cd->name);
      }
      FREE(cd);
   }
#endif
}

static void Queuedelete(UBYTE *fullname)
{  struct Cadel *cd;
   if(!fullname) return;
#ifndef LOCALONLY
   if(cd=ALLOCSTRUCT(Cadel,1,MEMF_CLEAR))
   {  if(cd->name=Dupstr(fullname,-1))
      {  ADDTAIL(&delqueue,cd);
      }
      else FREE(cd);
   }
   else
   {  DeleteFile(fullname);
   }
#endif
}

static void Writeregentry4(void *fh,struct Cache *cac)
{  struct Awi4entry ent;
   UBYTE *url=(UBYTE *)Agetattr(cac->url,AOURL_Realurl);
   UBYTE *movedto=NULL;
   short urlsize,movedsize;
   UBYTE *path;

   if(!url) url=(UBYTE *)Agetattr(cac->url,AOURL_Url);
   if(!url) url=(UBYTE *)"";
   path=cac->relpath;
   if(!path) path=(UBYTE *)"";

   memset(&ent,0,sizeof(ent));
   ent.date=cac->date;
   ent.expires=cac->expires;
   ent.cachedate=cac->cachedate;
   ent.size=cac->disksize;
   ent.checksum=cac->checksum;
   ent.type=0;
   urlsize=(short)(strlen((char *)url)+1);
   movedsize=0;
   if(movedto=(UBYTE *)Agetattr(cac->url,AOURL_Movedto))
   {  ent.type=COTYPE_MOVED;
      movedsize=(short)(strlen((char *)movedto)+1);
   }
   ent.urlsize=urlsize+movedsize;
   ent.pathsize=(short)(strlen((char *)path)+1);

   WriteAsync(fh,&ent,sizeof(ent));
   WriteAsync(fh,cac->mimetype,strlen((char *)cac->mimetype)+1);
   WriteAsync(fh,path,ent.pathsize);
   WriteAsync(fh,url,urlsize);
   if(movedto) WriteAsync(fh,movedto,movedsize);
   WriteAsync(fh,cac->etag,strlen((char *)cac->etag)+1);
}

static void Publishcacheindex(BOOL dovolumeflush)
{  UBYTE *tmpname,*idxname,*bakname;
   void *fh;
   struct Awi4hdr hdr;
   struct Cache *cac,*ucac;
   BOOL ok=FALSE;
   long ilock;

#ifndef LOCALONLY
   ObtainSemaphore(&cachesema);
   tmpname=Makename("index.tmp",NULL);
   idxname=Makename("index",NULL);
   bakname=Makename("index.bak",NULL);
   if(tmpname && idxname && bakname)
   {  if(fh=OpenAsync(tmpname,MODE_WRITE,CAC_WRITECHUNK))
      {  strncpy((char *)hdr.label,"AWI4",4);
         hdr.version=AWI4_VERSION;
         hdr.lastnr=cachenr;
         WriteAsync(fh,&hdr,sizeof(hdr));
         for(cac=cache.first;cac->next;cac=cac->next)
         {  if(!(cac->flags&CACF_PUBLISHED)) continue;
            if(cac->flags&CACF_PENDINGDEL) continue;
            ucac=(struct Cache *)Agetattr(cac->url,AOURL_Cache);
            if(cac==ucac) Writeregentry4(fh,cac);
         }
         if(CloseAsync(fh)>=0)
         {  /* Keep a recoverable name until index.tmp is published:
             * index -> index.bak, then index.tmp -> index, then drop bak. */
            if(ilock=Lock(idxname,SHARED_LOCK))
            {  UnLock(ilock);
               DeleteFile(bakname);
               if(!Rename(idxname,bakname))
               {  DeleteFile(tmpname);
                  goto pubdone;
               }
            }
            if(Rename(tmpname,idxname))
            {  DeleteFile(bakname);
               ok=TRUE;
            }
            else
            {  /* Roll bak back if the new index did not land. */
               Rename(bakname,idxname);
               DeleteFile(tmpname);
            }
         }
         else DeleteFile(tmpname);
      }
   }
pubdone:
   if(tmpname) FREE(tmpname);
   if(idxname) FREE(idxname);
   if(bakname) FREE(bakname);
   if(ok)
   {  indexdirty=FALSE;
      publishdirty=0;
      Rundelqueue();
      if(dovolumeflush) Docachevolumeflush();
   }
   else if(dovolumeflush && exitting)
   {  Docachevolumeflush();
   }
   ReleaseSemaphore(&cachesema);
#else
   (void)dovolumeflush;
#endif
}

static void Schedulecacheflush(void)
{
#ifndef LOCALONLY
   indexdirty=TRUE;
   publishdirty++;
   if(publishdirty>=cacheflushk)
   {  Publishcacheindex(TRUE);
      return;
   }
   if(cacheflushtimer)
   {  Asetattrs(cacheflushtimer,
         AOTIM_Waitseconds,cacheidles,
         AOTIM_Waitmicros,cacheidleu,
         TAG_END);
   }
#endif
}

/*------------------------------------------------------------------------*/

static long Updatecachectl(void *ctl,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   (void)ctl;
   while(tag=NextTagItem(&tstate))
   {  if(tag->ti_Tag==AOCCTL_Flushready && tag->ti_Data)
      {  if(indexdirty || publishdirty>0) Publishcacheindex(TRUE);
      }
   }
   return 0;
}

static long Dispatchcachectl(void *ctl,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Allocobject(AOTP_CACHECTL,sizeof(struct Aobject),
            (struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatecachectl(ctl,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Amethodas(AOTP_OBJECT,ctl,AOM_DISPOSE);
         break;
      case AOM_DEINSTALL:
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

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

static void Abortstaging(struct Cache *cac)
{
#ifndef LOCALONLY
   if(cac->fh)
   {  ObtainSemaphore(&cachewritesema);
      CloseAsync(cac->fh);
      cac->fh=NULL;
      ReleaseSemaphore(&cachewritesema);
   }
   if(cac->staging)
   {  DeleteFile(cac->staging);
      FREE(cac->staging);
      cac->staging=NULL;
   }
   if(cac->wbuf)
   {  FREE(cac->wbuf);
      cac->wbuf=NULL;
      cac->wbufused=0;
   }
   cac->flags&=~(CACF_WRITING);
   /* Do not advertise a final path that was never published. */
   if(!(cac->flags&CACF_PUBLISHED) && cac->name)
   {  FREE(cac->name);
      cac->name=NULL;
   }
   /* Unpublished incomplete bodies must not count toward disk quota. */
   if(!(cac->flags&CACF_PUBLISHED) && cac->disksize)
   {  cadisksize-=cac->disksize;
      cac->disksize=0;
   }
#endif
}

static BOOL Opencacstaging(struct Cache *cac)
{  UBYTE id[20];
   UBYTE *rel;
#ifndef LOCALONLY
   if(Cacheurlislocalfile(cac)) return FALSE;
   /* AmigaDOS Rename() cannot move between directories. Stage as
    * aa/bb/<id>.part beside the final aa/bb/<id> so publish is a
    * same-drawer rename. Hash now: ETag/Last-Modified tags in this
    * Srcupdate are applied before Opencacstaging is called. */
   Hashcacheid(cac,id);
   if(!(rel=Makerelpath(id))) return FALSE;
   if(cac->relpath) FREE(cac->relpath);
   cac->relpath=rel;
   if(cac->name) FREE(cac->name);
   if(!(cac->name=Makename(rel,NULL))) return FALSE;
   if(cac->staging) FREE(cac->staging);
   if(!(cac->staging=Makename(rel,"part"))) return FALSE;
   if(!cac->wbuf)
   {  cac->wbuf=ALLOCTYPE(UBYTE,CAC_WRITECHUNK,MEMF_PUBLIC);
      cac->wbufused=0;
   }
   ObtainSemaphore(&cachewritesema);
   cac->fh=OpenAsync(cac->staging,MODE_WRITE,CAC_WRITECHUNK);
   ReleaseSemaphore(&cachewritesema);
   if(!cac->fh)
   {  Abortstaging(cac);
      return FALSE;
   }
   cac->flags|=CACF_WRITING;
   cac->checksum=0;
   cac->disksize=0;
   return TRUE;
#else
   (void)cac;
   return FALSE;
#endif
}

static BOOL Flushwbuf(struct Cache *cac)
{  long n;
#ifndef LOCALONLY
   if(!cac->fh || !cac->wbuf || cac->wbufused<=0) return TRUE;
   ObtainSemaphore(&cachewritesema);
   n=WriteAsync(cac->fh,cac->wbuf,cac->wbufused);
   ReleaseSemaphore(&cachewritesema);
   if(n!=cac->wbufused) return FALSE;
   cac->wbufused=0;
   return TRUE;
#else
   (void)cac;
   return TRUE;
#endif
}

static BOOL Writecachedata(struct Cache *cac,UBYTE *data,long length)
{  long i,space,chunk;
#ifndef LOCALONLY
   if(!cac->wbuf)
   {  cac->wbuf=ALLOCTYPE(UBYTE,CAC_WRITECHUNK,MEMF_PUBLIC);
      if(!cac->wbuf) return FALSE;
      cac->wbufused=0;
   }
   for(i=0;i<length;i++)
      cac->checksum=(cac->checksum*33UL)+(ULONG)data[i];
   while(length>0)
   {  space=CAC_WRITECHUNK-cac->wbufused;
      chunk=length<space?length:space;
      memcpy(cac->wbuf+cac->wbufused,data,chunk);
      cac->wbufused+=chunk;
      data+=chunk;
      length-=chunk;
      cac->disksize+=chunk;
      cadisksize+=chunk;
      sizeadded+=chunk;
      if(cac->wbufused>=CAC_WRITECHUNK)
      {  if(!Flushwbuf(cac)) return FALSE;
      }
   }
   return TRUE;
#else
   (void)cac;(void)data;(void)length;
   return TRUE;
#endif
}

static BOOL Publishstaging(struct Cache *cac)
{
#ifndef LOCALONLY
   if(!cac->staging || !cac->name) return FALSE;
   if(!Flushwbuf(cac)) { Abortstaging(cac); return FALSE; }
   ObtainSemaphore(&cachewritesema);
   if(cac->fh)
   {  if(CloseAsync(cac->fh)<0)
      {  cac->fh=NULL;
         ReleaseSemaphore(&cachewritesema);
         Abortstaging(cac);
         return FALSE;
      }
      cac->fh=NULL;
   }
   ReleaseSemaphore(&cachewritesema);
   if(cac->wbuf) { FREE(cac->wbuf); cac->wbuf=NULL; cac->wbufused=0; }

   /* Same-directory rename only (see Opencacstaging). Replace any prior
    * generation at the final name before publishing. */
   DeleteFile(cac->name);
   if(!Rename(cac->staging,cac->name))
   {  Abortstaging(cac);
      return FALSE;
   }
   FREE(cac->staging);
   cac->staging=NULL;
   cac->flags&=~CACF_WRITING;
   cac->flags|=CACF_PUBLISHED;
   return TRUE;
#else
   (void)cac;
   return FALSE;
#endif
}

/*------------------------------------------------------------------------*/

static void Flushexcess(void);
static void Flushtype(USHORT type,UBYTE *pattern);

static void Deletecache(struct Cache *cac)
{
#ifndef LOCALONLY
   /* Index-first: mark pending, queue file delete for flush window.
    * Never delete a final name that was never published — staging aborts
    * must not remove another object's content-addressed file. */
   if(cac->flags&CACF_PUBLISHED)
   {  cac->flags|=CACF_PENDINGDEL;
      cac->flags&=~CACF_PUBLISHED;
      indexdirty=TRUE;
      if(cac->name) Queuedelete(cac->name);
   }
   else if(cac->staging)
   {  DeleteFile(cac->staging);
      FREE(cac->staging);
      cac->staging=NULL;
   }
#endif
}

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
   Schedulecacheflush();
}

static void Flushtype(USHORT type,UBYTE *pattern)
{  struct Cache *cac,*next;
   long len;
   UBYTE *parsepat=NULL,*url,*p;
   BOOL scheme=FALSE,sel;
   if(pattern)
   {  len=2*strlen((char *)pattern)+4;
      parsepat=ALLOCTYPE(UBYTE,len,0);
      if(parsepat)
      {  if(ParsePatternNoCase(pattern,parsepat,len)<0)
         {  FREE(parsepat);
            parsepat=NULL;
         }
      }
      scheme=BOOLVAL(strstr((char *)pattern,"://"));
   }
   ObtainSemaphore(&cachesema);
   for(cac=cache.first;cac->next;cac=next)
   {  next=cac->next;
      if(type)
      {  sel=(type==CACFT_DOCUMENTS)==BOOLVAL(STRNIEQUAL(cac->mimetype,"TEXT/",5));
      }
      else sel=TRUE;
      if(sel && parsepat)
      {  url=(UBYTE *)Agetattr(cac->url,AOURL_Url);
         if(url)
         {  if(!scheme)
            {  p=(UBYTE *)strstr((char *)url,"://");
               if(p) p+=3;
               else p=url;
            }
            else p=url;
            sel=MatchPatternNoCase(parsepat,p);
         }
      }
      if(sel) Auspecial(cac->url,AUMST_DELETECACHE);
   }
   ReleaseSemaphore(&cachesema);
   if(parsepat) FREE(parsepat);
   Schedulecacheflush();
}

static void Sendinfo(struct Cache *cac,void *fetch)
{  UBYTE buf[64];
   UBYTE datebuf[32];
   Asrcupdatetags(cac->url,fetch,AOURL_Fromcache,TRUE,TAG_END);
   if(*cac->mimetype)
   {  sprintf((char *)buf,"Content-Type: %s",cac->mimetype);
      Asrcupdatetags(cac->url,fetch,AOURL_Header,buf,TAG_END);
   }
   sprintf((char *)buf,"Content-Length: %ld",cac->disksize);
   Asrcupdatetags(cac->url,fetch,AOURL_Header,buf,TAG_END);
   if(cac->date)
   {  Makedate(cac->date,datebuf);
      sprintf((char *)buf,"Last-Modified: %s",datebuf);
      Asrcupdatetags(cac->url,fetch,AOURL_Header,buf,TAG_END);
   }
   if(cac->expires)
   {  Makedate(cac->expires,datebuf);
      sprintf((char *)buf,"Expires: %s",datebuf);
      Asrcupdatetags(cac->url,fetch,AOURL_Header,buf,TAG_END);
   }
   if(*cac->etag)
   {  sprintf((char *)buf,"ETag: %s",cac->etag);
      Asrcupdatetags(cac->url,fetch,AOURL_Header,buf,TAG_END);
   }
}

/*------------------------------------------------------------------------*/

static BOOL Readawi4(UBYTE *name,long lock)
{  void *fh;
   struct FileInfoBlock *fib;
   long filesize=0,readsize=0,nextsize=0;
   struct Awi4hdr hdr;
   struct Awi4entry ent;
   UBYTE mimetype[32],*p;
   short i;
   UBYTE *urlbuf=NULL,*pathbuf=NULL;
   long urlbuflen=0,pathbuflen=0;
   void *url;
   struct Cache *cac;
   BOOL ok=TRUE;
   UBYTE etagbuf[64];
   UBYTE *fullname;
   struct FileInfoBlock *fib2;
#ifndef LOCALONLY
   if(fib=AllocDosObject(DOS_FIB,TAG_END))
   {  if(Examine(lock,fib))
      {  filesize=fib->fib_Size;
         nextsize=filesize/10;
      }
      FreeDosObject(DOS_FIB,fib);
   }
   if(fh=OpenAsync(name,MODE_READ,CAC_WRITECHUNK))
   {  if(ReadAsync(fh,&hdr,sizeof(hdr))!=sizeof(hdr)) goto err;
      ok=FALSE;
      if(!STRNEQUAL(hdr.label,"AWI4",4)) goto err;
      if(hdr.version!=AWI4_VERSION) goto err;
      ok=TRUE;
      cachenr=hdr.lastnr;
      readsize=sizeof(hdr);
      while(ReadAsync(fh,&ent,sizeof(ent))==sizeof(ent))
      {  readsize+=sizeof(ent);
         if(ent.type<0 || ent.type>MAX_COTYPE) goto err;
         p=mimetype;
         i=0;
         do
         {  if(ReadAsync(fh,p,1)!=1) goto err;
            readsize+=1;
            i++;
         } while(*p++ && i<32);
         if(i>=32) goto err;
         if(ent.pathsize>pathbuflen)
         {  if(pathbuf) FREE(pathbuf);
            pathbuflen=(ent.pathsize+257)&~255;
            if(!(pathbuf=ALLOCTYPE(UBYTE,pathbuflen,0))) goto err;
         }
         if(ReadAsync(fh,pathbuf,ent.pathsize)!=ent.pathsize) goto err;
         readsize+=ent.pathsize;
         if(ent.urlsize>urlbuflen)
         {  if(urlbuf) FREE(urlbuf);
            urlbuflen=(ent.urlsize+257)&~255;
            if(!(urlbuf=ALLOCTYPE(UBYTE,urlbuflen,0))) goto err;
         }
         if(ReadAsync(fh,urlbuf,ent.urlsize)!=ent.urlsize) goto err;
         readsize+=ent.urlsize;
         p=etagbuf;
         i=0;
         do
         {  if(ReadAsync(fh,p,1)!=1) goto err;
            readsize+=1;
            i++;
         } while(*p++ && i<64);
         if(i>=64) goto err;

         if(ent.expires && ent.expires<=Today())
         {  fullname=Makename(pathbuf,NULL);
            if(fullname) { DeleteFile(fullname); FREE(fullname); }
         }
         else if(ent.type!=COTYPE_DEL)
         {  fullname=Makename(pathbuf,NULL);
            if(!fullname) goto err;
            /* Validate size before trusting the index entry. */
            if(fib2=AllocDosObject(DOS_FIB,TAG_END))
            {  long flock;
               BOOL good=FALSE;
               if(flock=Lock(fullname,SHARED_LOCK))
               {  if(Examine(flock,fib2) && fib2->fib_Size==ent.size)
                     good=TRUE;
                  UnLock(flock);
               }
               FreeDosObject(DOS_FIB,fib2);
               if(!good)
               {  DeleteFile(fullname);
                  FREE(fullname);
                  fullname=NULL;
               }
            }
            if(fullname)
            {  if(!(url=Anewobject(AOTP_URL,AOURL_Url,urlbuf,TAG_END)))
               {  FREE(fullname); goto err; }
               if(!(cac=Anewobject(AOTP_CACHE,
                  AOCAC_Url,url,
                  AOCAC_Cachedate,ent.cachedate,
                  TAG_END)))
               {  FREE(fullname); goto err; }
               cac->name=fullname;
               cac->relpath=Dupstr(pathbuf,-1);
               cac->date=ent.date;
               cac->expires=ent.expires;
               cac->checksum=ent.checksum;
               strcpy((char *)cac->mimetype,(char *)mimetype);
               strcpy((char *)cac->etag,(char *)etagbuf);
               cac->disksize=ent.size;
               cac->flags|=CACF_PUBLISHED;
               cadisksize+=cac->disksize;
               Asetattrs(url,
                  AOURL_Cache,cac,
                  AOURL_Visited,TRUE,
                  TAG_END);
               if(ent.type==COTYPE_MOVED)
               {  p=urlbuf+strlen((char *)urlbuf)+1;
                  Asetattrs(url,AOURL_Movedto,p,TAG_END);
               }
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
   if(pathbuf) FREE(pathbuf);
#endif
   return ok;
}

/* One-shot import of classic AWCR/AWCU into the in-memory list + AWI4. */
static BOOL Importlegacyreg(UBYTE *name,long lock)
{  void *fh;
   struct Creghdr
   {  UBYTE label[4];
      long version;
      ULONG lastnr;
   } crh;
   struct Cregentry
   {  ULONG nr;
      ULONG date;
      ULONG expires;
      ULONG cachedate;
      long size;
      short type;
      short urlsize;
   } cre;
   UBYTE mimetype[32],*p;
   short i;
   UBYTE *urlbuf=NULL;
   long urlbuflen=0;
   void *url;
   struct Cache *cac;
   UBYTE buf[20];
   UBYTE *ext;
   UBYTE etagbuf[64];
   BOOL ok=TRUE;
#ifndef LOCALONLY
   (void)lock;
   if(fh=OpenAsync(name,MODE_READ,FILEBLOCKSIZE))
   {  if(ReadAsync(fh,&crh,sizeof(crh))!=sizeof(crh)) goto err;
      ok=FALSE;
      if(!STRNEQUAL(crh.label,"AWCR",4)) goto err;
      if(crh.version<3 || crh.version>4) goto err;
      ok=TRUE;
      if(crh.lastnr>cachenr) cachenr=crh.lastnr;
      while(ReadAsync(fh,&cre,sizeof(cre))==sizeof(cre))
      {  if(cre.type<0 || cre.type>MAX_COTYPE) goto err;
         p=mimetype; i=0;
         do
         {  if(ReadAsync(fh,p,1)!=1) goto err;
            i++;
         } while(*p++ && i<32);
         if(i>=32) goto err;
         if(cre.urlsize>urlbuflen)
         {  if(urlbuf) FREE(urlbuf);
            urlbuflen=(cre.urlsize+257)&~255;
            if(!(urlbuf=ALLOCTYPE(UBYTE,urlbuflen,0))) goto err;
         }
         if(ReadAsync(fh,urlbuf,cre.urlsize)!=cre.urlsize) goto err;
         etagbuf[0]='\0';
         if(crh.version>=4)
         {  p=etagbuf; i=0;
            do
            {  if(ReadAsync(fh,p,1)!=1) goto err;
               i++;
            } while(*p++ && i<64);
            if(i>=64) goto err;
         }
         if(cre.type==COTYPE_DEL) continue;
         if(cre.expires && cre.expires<=Today()) continue;
         if(!(url=Anewobject(AOTP_URL,AOURL_Url,urlbuf,TAG_END))) goto err;
         ext=Urlfileext(urlbuf);
         if(!ext && Isxbm(mimetype)) ext=Dupstr("xbm",3);
         sprintf((char *)buf,"AWCD%02X/%08lX",(unsigned)(cre.nr&0x3f),
            (unsigned long)cre.nr);
         p=Makename(buf,ext);
         if(ext) FREE(ext);
         if(!p) continue;
         {  long flock;
            flock=Lock(p,SHARED_LOCK);
            if(!flock) { FREE(p); continue; }
            UnLock(flock);
         }
         if(!(cac=Anewobject(AOTP_CACHE,
            AOCAC_Url,url,
            AOCAC_Number,cre.nr,
            AOCAC_Cachedate,cre.cachedate,
            TAG_END))) { FREE(p); goto err; }
         cac->name=p;
         cac->relpath=Dupstr(buf,-1);
         cac->date=cre.date;
         cac->expires=cre.expires;
         strcpy((char *)cac->mimetype,(char *)mimetype);
         strcpy((char *)cac->etag,(char *)etagbuf);
         cac->disksize=cre.size;
         cac->flags|=CACF_PUBLISHED;
         cadisksize+=cac->disksize;
         Asetattrs(url,AOURL_Cache,cac,AOURL_Visited,TRUE,TAG_END);
         if(cre.type==COTYPE_MOVED)
         {  p=urlbuf+strlen((char *)urlbuf)+1;
            Asetattrs(url,AOURL_Movedto,p,TAG_END);
         }
         if(cre.nr>cachenr) cachenr=cre.nr;
      }
err:
      CloseAsync(fh);
   }
   if(urlbuf) FREE(urlbuf);
#endif
   return ok;
}

static void Deletestagingleftovers(UBYTE *dirname,short depth)
{  __aligned struct FileInfoBlock fib={0};
   long lock,oldcd;
   LIST(Cadel) local;
   struct Cadel *cd;
   long len;
#ifndef LOCALONLY
   NEWLIST(&local);
   if(!(lock=Lock(dirname,SHARED_LOCK))) return;
   oldcd=CurrentDir(lock);
   if(Examine(lock,&fib))
   {  while(ExNext(lock,&fib))
      {  if(fib.fib_DirEntryType>0)
         {  if(depth<4) Deletestagingleftovers(fib.fib_FileName,depth+1);
         }
         else
         {  len=strlen(fib.fib_FileName);
            if((len>5 && STRIEQUAL(fib.fib_FileName+len-5,".part"))
            || STREQUAL(fib.fib_FileName,"index.tmp"))
            {  if(cd=ALLOCSTRUCT(Cadel,1,MEMF_CLEAR))
               {  if(cd->name=Dupstr(fib.fib_FileName,-1)) ADDTAIL(&local,cd);
                  else FREE(cd);
               }
            }
         }
      }
   }
   while(cd=REMHEAD(&local))
   {  DeleteFile(cd->name);
      FREE(cd->name);
      FREE(cd);
   }
   CurrentDir(oldcd);
   UnLock(lock);
   /* Also clear Cache/staging/ drawer leftovers by absolute path when at root. */
   if(depth==0)
   {  UBYTE stagedir[NAMESIZE];
      strcpy((char *)stagedir,(char *)cachename);
      AddPart(stagedir,"staging",NAMESIZE);
      Deletestagingleftovers(stagedir,1);
   }
#else
   (void)dirname;(void)depth;
#endif
}

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

static long Srcupdatecache(struct Cache *cac,struct Amsrcupdate *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *data=NULL;
   long length=0;
   BOOL eof=FALSE,terminate=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOURL_Contenttype:
            strncpy((char *)cac->mimetype,(char *)tag->ti_Data,31);
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
            strncpy((char *)cac->etag,(char *)tag->ti_Data,63);
            cac->etag[63]='\0';
            break;
         case AOURL_Terminate:
            terminate=TRUE;
            break;
      }
   }
   /* Terminate/abort: discard staging only; never publish or index. */
   if(terminate)
   {  Abortstaging(cac);
      return 0;
   }
   if(data || eof)
   {  if(!(cac->flags&CACF_WRITING) && !(cac->flags&CACF_PUBLISHED)
      && !Cacheurlislocalfile(cac))
      {  Opencacstaging(cac);
      }
   }
   if(data && length && (cac->flags&CACF_WRITING))
   {  if(!Writecachedata(cac,data,length))
      {  Abortstaging(cac);
         return 0;
      }
      if(sizeadded>CAC_CHKAFTER) Flushexcess();
   }
   if(eof && (cac->flags&CACF_WRITING))
   {  if(Publishstaging(cac))
      {  Addcabrobject(cac);
         Schedulecacheflush();
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
   Abortstaging(cac);
   if(cac->name)
   {  if(!(cac->flags&CACF_NODELETE)) Deletecache(cac);
      FREE(cac->name);
      cac->name=NULL;
   }
   if(cac->relpath) FREE(cac->relpath);
   Amethodas(AOTP_OBJECT,cac,AOM_DISPOSE);
}

static void Deinstallcache(void)
{  while(cache.first->next) Adisposeobject(cache.first);
   if(cacheflushtimer) { Adisposeobject(cacheflushtimer); cacheflushtimer=NULL; }
   if(cachectl) { Adisposeobject(cachectl); cachectl=NULL; }
   Rundelqueue();
   if(cachelock) { UnLock(cachelock); cachelock=0; }
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
   NEWLIST(&delqueue);
   InitSemaphore(&cachesema);
   InitSemaphore(&cachewritesema);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_CACHE,Dispatch)) return FALSE;
   if(!Amethod(NULL,AOM_INSTALL,AOTP_CACHECTL,Dispatchcachectl)) return FALSE;
   return TRUE;
}

BOOL Initcache(void)
{  UBYTE *idx;
   long lock;
   BOOL haveawi4=FALSE;
#ifndef LOCALONLY
   cachelock=Lock(prefs.network.cachepath,SHARED_LOCK);
   if(!cachelock) cachelock=Lock("T:",SHARED_LOCK);
   if(cachelock) NameFromLock(cachelock,cachename,NAMESIZE);

   Detectcachefs();
   Deletestagingleftovers(cachename,0);

   initializing=TRUE;
   if(idx=Makename("index",NULL))
   {  if(lock=Lock(idx,SHARED_LOCK))
      {  haveawi4=Readawi4(idx,lock);
         UnLock(lock);
      }
      FREE(idx);
   }
   if(!haveawi4)
   {  /* Recover from crash between index->bak and index.tmp->index. */
      UBYTE *live;
      if(idx=Makename("index.bak",NULL))
      {  if(lock=Lock(idx,SHARED_LOCK))
         {  haveawi4=Readawi4(idx,lock);
            UnLock(lock);
            if(haveawi4)
            {  live=Makename("index",NULL);
               if(live)
               {  DeleteFile(live);
                  Rename(idx,live);
                  FREE(live);
               }
            }
         }
         FREE(idx);
      }
   }

   if(!haveawi4)
   {  /* Migrate classic AWCU/AWCR if present. */
      UBYTE *awcu,*awcr;
      awcu=Makename("AWCU",NULL);
      awcr=Makename("AWCR",NULL);
      if(awcu && (lock=Lock(awcu,SHARED_LOCK)))
      {  Importlegacyreg(awcu,lock);
         UnLock(lock);
         DeleteFile(awcu);
      }
      if(awcr && (lock=Lock(awcr,SHARED_LOCK)))
      {  if(!cache.first->next) Importlegacyreg(awcr,lock);
         UnLock(lock);
         DeleteFile(awcr);
      }
      if(awcu) FREE(awcu);
      if(awcr) FREE(awcr);
      /* Persist migrated list as AWI4. */
      if(cache.first->next)
      {  indexdirty=TRUE;
         Publishcacheindex(TRUE);
      }
   }
   initializing=FALSE;

   cachectl=Anewobject(AOTP_CACHECTL,TAG_END);
   if(cachectl)
   {  cacheflushtimer=Anewobject(AOTP_TIMER,
         AOBJ_Target,cachectl,
         AOBJ_Map,cacheflushmap,
         TAG_END);
   }
#endif
   return TRUE;
}

void Exitcache(void)
{  exitting=TRUE;
   if(cacheflushtimer)
   {  Adisposeobject(cacheflushtimer);
      cacheflushtimer=NULL;
   }
   if(cache.first->next || indexdirty || !ISEMPTY(&delqueue))
   {  Publishcacheindex(TRUE);
   }
   else
   {  Docachevolumeflush();
   }
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

UBYTE *Cfnameshort(UBYTE *name)
{  short i=0;
   UBYTE *p;
   for(p=name+strlen((char *)name)-1;p>name;p--)
   {  if(*p=='/' && ++i>1) break;
   }
   if(*p=='/') p++;
   return p;
}

#ifndef LOCALONLY
static BOOL Fixroot4(void *preq)
{  __aligned struct FileInfoBlock fib={0};
   long oldcd=CurrentDir(cachelock);
   LIST(Cadel) dellist;
   struct Cadel *cd;
   BOOL ok=FALSE;
   NEWLIST(&dellist);
   if(Examine(cachelock,&fib) && fib.fib_DirEntryType>0)
   {  while(ExNext(cachelock,&fib))
      {  if(fib.fib_DirEntryType<0)
         {  if(!STREQUAL(fib.fib_FileName,"index")
            && !STREQUAL(fib.fib_FileName,"index.tmp"))
            {  if(cd=ALLOCSTRUCT(Cadel,1,MEMF_CLEAR))
               {  if(cd->name=Dupstr(fib.fib_FileName,-1)) ADDTAIL(&dellist,cd);
                  else FREE(cd);
               }
            }
         }
         if(Checkprogressreq(preq)) goto err;
      }
   }
   while(cd=REMHEAD(&dellist))
   {  /* Only delete leftover staging names at root; keep shard dirs. */
      long len=strlen((char *)cd->name);
      if((len>5 && STRIEQUAL(cd->name+len-5,".part"))
      || STREQUAL(cd->name,"index.tmp")
      || STREQUAL(cd->name,"AWCU")
      || STREQUAL(cd->name,"AWCR")
      || STREQUAL(cd->name,"AWCK"))
         DeleteFile(cd->name);
      FREE(cd->name);
      FREE(cd);
   }
   ok=TRUE;
err:
   while(cd=REMHEAD(&dellist)) { FREE(cd->name); FREE(cd); }
   CurrentDir(oldcd);
   return ok;
}

static void Dofixcache(short code,void *data)
{  void *preq;
   if(code)
   {  Busypointer(TRUE);
      ObtainSemaphore(&cachesema);
      if(preq=Openprogressreq(AWEBSTR(MSG_FIXCACHE_PROGRESS)))
      {  Setprogressreq(preq,1,3);
         Fixroot4(preq);
         Setprogressreq(preq,2,3);
         Deletestagingleftovers(cachename,0);
         Setprogressreq(preq,3,3);
         indexdirty=TRUE;
         Publishcacheindex(TRUE);
         Closeprogressreq(preq);
      }
      ReleaseSemaphore(&cachesema);
      Busypointer(FALSE);
   }
   if(data) FREE(data);
}
#endif

void Fixcache(BOOL force)
{
#ifndef LOCALONLY
   UBYTE *p=haiku?HAIKU20:AWEBSTR(MSG_FIXCACHE_WARNING);
   UBYTE *buf;
   long len=strlen((char *)p)+strlen((char *)cachename)+8;
   if(force)
   {  Dofixcache(TRUE,NULL);
   }
   else
   {  if(buf=ALLOCTYPE(UBYTE,len,0))
      {  sprintf((char *)buf,(char *)p,cachename);
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
   BOOL scheme=FALSE,sel;
   struct Cache *cac;
   long i,len;
   if(pattern)
   {  len=2*strlen((char *)pattern)+4;
      parsepat=ALLOCTYPE(UBYTE,len,0);
      if(parsepat)
      {  if(ParsePatternNoCase(pattern,parsepat,len)<0)
         {  FREE(parsepat);
            parsepat=NULL;
         }
      }
      scheme=BOOLVAL(strstr((char *)pattern,"://"));
   }
   ObtainSemaphore(&cachesema);
   i=0;
   for(cac=cache.first;cac->next;cac=cac->next)
   {  if(!(cac->flags&CACF_PUBLISHED)) continue;
      url=(UBYTE *)Agetattr(cac->url,AOURL_Url);
      sel=TRUE;
      if(parsepat)
      {  if(url)
         {  if(!scheme)
            {  p=(UBYTE *)strstr((char *)url,"://");
               if(p) p+=3;
               else p=url;
            }
            else p=url;
            sel=MatchPatternNoCase(parsepat,p);
         }
         else sel=FALSE;
      }
      if(sel)
      {  i++;
         Setstemvar(ac,stem,i,"URL",url);
         Setstemvar(ac,stem,i,"TYPE",cac->mimetype);
         sprintf((char *)buf,"%ld",cac->disksize);
         Setstemvar(ac,stem,i,"SIZE",buf);
         sprintf((char *)buf,"%ld",cac->cachedate/86400);
         Setstemvar(ac,stem,i,"DATE",buf);
         Setstemvar(ac,stem,i,"FILE",cac->name?Cfnameshort(cac->name):(UBYTE *)"");
      }
   }
   ReleaseSemaphore(&cachesema);
   sprintf((char *)buf,"%ld",i);
   Setstemvar(ac,stem,0,NULL,buf);
   if(parsepat) FREE(parsepat);
#endif
}
