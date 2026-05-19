/**********************************************************************
 *
 * mailutil.c - Mail URL parsing and message cache
 *
 * Copyright (C) 2026 amigazen project
 *
 **********************************************************************/

#include "aweblib.h"
#include "fetchdriver.h"
#include "mail.h"
#include <proto/dos.h>

#ifndef LOCALONLY

extern void *AwebPluginBase;

/* Build IMAP host from smtphost: smtp.foo -> imap.foo, else use as-is */
void Mail_imap_host(struct Fetchdriver *fd,UBYTE *buf,long buflen)
{  UBYTE *smtp;
   UBYTE *p;
   if(!buf || buflen<=0) return;
   buf[0]='\0';
   ObtainSemaphore(fd->prefssema);
   smtp=fd->prefs->smtphost;
   if(smtp && *smtp)
   {  if(buflen>0) strncpy(buf,smtp,buflen-1);
      buf[buflen-1]='\0';
      if(STRNIEQUAL(buf,"smtp.",5))
      {  buf[0]='i';
         buf[1]='m';
         buf[2]='a';
         buf[3]='p';
      }
      else if((p=strchr(buf,'.')) && p>buf)
      {  if(STRNIEQUAL(p-4,"smtp",4))
      {  memcpy(p-4,"imap",4);
      }
      }
   }
   ReleaseSemaphore(fd->prefssema);
   Mail_debug("Mail_imap_host smtp->imap: %s",buf);
}

void Mail_encode_folder(UBYTE *folder,UBYTE *out,long outmax)
{  UBYTE *s;
   UBYTE *d;
   UBYTE *end;
   if(!out || outmax<=0) return;
   if(!folder || !*folder)
   {  strcpy(out,"INBOX");
      return;
   }
   s=folder;
   d=out;
   end=out+outmax-1;
   while(*s && d<end)
   {  if(*s=='/' || *s=='%' || *s==' ')
      {  if(d+3>=end) break;
         *d++='%';
         sprintf(d,"%02X",(unsigned int)(unsigned char)*s);
         d+=2;
      }
      else
      {  *d++=*s;
      }
      s++;
   }
   *d='\0';
}

static int Mail_hex(UBYTE c)
{  if(c>='0' && c<='9') return c-'0';
   if(c>='A' && c<='F') return c-'A'+10;
   if(c>='a' && c<='f') return c-'a'+10;
   return -1;
}

void Mail_folder_label(UBYTE *imapname,UBYTE *label,long labelmax)
{  if(!label || labelmax<=0) return;
   if(!imapname || !*imapname)
   {  strncpy(label,"Inbox",labelmax-1);
      label[labelmax-1]='\0';
      return;
   }
   if(STRIEQUAL(imapname,"INBOX"))
   {  strncpy(label,"Inbox",labelmax-1);
   }
   else
   {  strncpy(label,imapname,labelmax-1);
   }
   label[labelmax-1]='\0';
}

BOOL Mail_decode_folder(UBYTE *encoded,UBYTE *out,long outmax)
{  UBYTE *s;
   UBYTE *d;
   UBYTE *end;
   int h1;
   int h2;
   if(!encoded || !out || outmax<=0) return FALSE;
   s=encoded;
   d=out;
   end=out+outmax-1;
   while(*s && d<end)
   {  if(*s=='%')
      {  h1=Mail_hex(s[1]);
         h2=Mail_hex(s[2]);
         if(h1>=0 && h2>=0)
         {  *d++=(UBYTE)((h1<<4)|h2);
            s+=3;
            continue;
         }
      }
      *d++=*s++;
   }
   *d='\0';
   if(!*out) strcpy(out,"INBOX");
   return TRUE;
}

BOOL Mail_parse_url(UBYTE *url,UBYTE *folder,long foldermax,
   unsigned long *uid,BOOL *has_uid)
{  UBYTE *p;
   UBYTE *slash;
   UBYTE *u;
   unsigned long n;
   if(has_uid) *has_uid=FALSE;
   if(uid) *uid=0;
   if(!url) return FALSE;
   if(STRNIEQUAL(url,"mail:",5)) p=url+5;
   else return FALSE;
   if(!*p)
   {  if(folder && foldermax>0) strcpy(folder,"INBOX");
      return TRUE;
   }
   u=strstr(p,"/uid/");
   if(u)
   {  *u='\0';
      Mail_decode_folder(p,folder,foldermax);
      u+=5;
      n=0;
      while(*u>='0' && *u<='9')
      {  n=n*10+(*u-'0');
         u++;
      }
      if(uid) *uid=n;
      if(has_uid) *has_uid=(n>0);
      return TRUE;
   }
   slash=strrchr(p,'/');
   if(slash && slash>p)
   {  n=0;
      u=slash+1;
      while(*u>='0' && *u<='9') { n=n*10+(*u-'0'); u++; }
      if(n>0 && *u=='\0')
      {  *slash='\0';
         Mail_decode_folder(p,folder,foldermax);
         if(uid) *uid=n;
         if(has_uid) *has_uid=TRUE;
         return TRUE;
      }
   }
   Mail_decode_folder(p,folder,foldermax);
   return TRUE;
}

static void Mail_cache_path(struct Fetchdriver *fd,UBYTE *folder,unsigned long uid,
   UBYTE *path,long pathmax)
{  UBYTE enc[128];
   UBYTE *cache;
   ObtainSemaphore(fd->prefssema);
   cache=fd->prefs->cachepath;
   if(!cache || !*cache) cache=(UBYTE *)"AWeb:Cache";
   Mail_encode_folder(folder,enc,sizeof(enc));
   ReleaseSemaphore(fd->prefssema);
   sprintf(path,"%s/%s/%s_%lu.msg",cache,MAIL_CACHE_SUBDIR,enc,uid);
}

BOOL Mail_cache_read(struct Fetchdriver *fd,UBYTE *folder,unsigned long uid,
   UBYTE **data,long *datalen)
{  UBYTE path[256];
   long file;
   long len;
   long size;
   UBYTE *buf;
   *data=NULL;
   *datalen=0;
   Mail_cache_path(fd,folder,uid,path,sizeof(path));
   if(!(file=Open(path,MODE_OLDFILE))) return FALSE;
   size=0;
   len=Seek(file,0,OFFSET_END);
   if(len<=0) { Close(file); return FALSE; }
   Seek(file,0,OFFSET_BEGINNING);
   if(!(buf=ALLOCTYPE(UBYTE,len+1,MEMF_CLEAR))) { Close(file); return FALSE; }
   size=Read(file,buf,len);
   Close(file);
   if(size!=len) { FREE(buf); return FALSE; }
   buf[len]='\0';
   *data=buf;
   *datalen=len;
   return TRUE;
}

BOOL Mail_cache_write(struct Fetchdriver *fd,UBYTE *folder,unsigned long uid,
   UBYTE *data,long datalen)
{  UBYTE path[256];
   UBYTE dir[256];
   long file;
   UBYTE *slash;
   if(!data || datalen<=0) return FALSE;
   Mail_cache_path(fd,folder,uid,path,sizeof(path));
   strncpy(dir,path,sizeof(dir)-1);
   slash=strrchr(dir,'/');
   if(slash) *slash='\0';
   {  long lock;
      if(lock=CreateDir(dir)) UnLock(lock);
   }
   if(!(file=Open(path,MODE_NEWFILE))) return FALSE;
   Write(file,data,datalen);
   Close(file);
   return TRUE;
}

#endif /* !LOCALONLY */
