/**********************************************************************
 *
 * mailimap.c - IMAP receive and SMTP send for AWeb mail client
 *
 * Copyright (C) 2026 amigazen project
 *
 **********************************************************************/

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "aweblib.h"
#include "tcperr.h"
#include "fetchdriver.h"
#include "task.h"
#include "application.h"
#include "awebtcp.h"
#include "awebssl.h"
#include "mail.h"
#include <stdarg.h>
#include <ctype.h>
#include <proto/dos.h>
#include <proto/socket.h>

#ifndef LOCALONLY

extern struct Library *AwebTcpBase;
extern void *AwebPluginBase;

/* One IMAP session at a time; never block forever on ObtainSemaphore. */
static struct SignalSemaphore Mail_imap_sema;
static BOOL Mail_imap_sema_inited;
static BOOL Mail_imap_locked;
static BOOL Mail_draining;

static void Mail_imap_sema_init(void)
{  if(!Mail_imap_sema_inited)
   {  InitSemaphore(&Mail_imap_sema);
      Mail_imap_sema_inited=TRUE;
   }
}

static BOOL Mail_imap_lock(BOOL wait)
{  Mail_imap_sema_init();
   /* Serialize across fetch tasks; wait=TRUE blocks until the session is free. */
   if(wait)
   {  ObtainSemaphore(&Mail_imap_sema);
   }
   else
   {  if(!AttemptSemaphore(&Mail_imap_sema)) return FALSE;
   }
   Mail_imap_locked=TRUE;
   return TRUE;
}

static void Mail_imap_unlock(void)
{  if(Mail_imap_locked)
   {  Mail_imap_locked=FALSE;
      ReleaseSemaphore(&Mail_imap_sema);
   }
}

void Mail_imap_release_stale(void)
{  if(Mail_imap_locked)
   {  Mail_debug("Mail_imap_release_stale: freeing abandoned session lock");
      Mail_imap_unlock();
   }
}

#if MAIL_DEBUG
/* Log client commands only; server lines are too verbose for SELECT/FETCH. */
static void Mail_debug_imap_cmd(UBYTE *line)
{  UBYTE buf[128];
   long n;
   if(!line) return;
   if(strstr(line," LOGIN ") || strstr(line,"AUTHENTICATE PLAIN"))
   {  Mail_debug((UBYTE *)"Mail IMAP C: (auth redacted)");
      return;
   }
   n=strlen(line);
   if(n>100) n=100;
   sprintf((char *)buf,"Mail IMAP C: %.*s",(int)n,(char *)line);
   Mail_debug((UBYTE *)"%s",buf);
}
#endif

/* --- line I/O (IMAP tagged responses and literals) --- */

static UBYTE *Mail_nextline(struct Mailconn *mc)
{  UBYTE *p;
   UBYTE *end;
   long r;
   if(!mc->buffer)
   {  if(!(mc->buffer=ALLOCTYPE(UBYTE,4096,MEMF_CLEAR))) return NULL;
      mc->bufsize=4096;
   }
   if(mc->linelen)
   {  memmove(mc->buffer,mc->buffer+mc->linelen,mc->length-mc->linelen);
      mc->length-=mc->linelen;
      mc->linelen=0;
   }
   for(;;)
   {  end=mc->buffer+mc->length;
      for(p=mc->buffer;p<end && *p!='\n';p++);
      if(p<end)
      {  *p='\0';
         mc->linelen=p-mc->buffer+1;
         if(mc->linelen>1 && mc->buffer[mc->linelen-2]=='\r')
         {  mc->buffer[mc->linelen-2]='\0';
         }
         return mc->buffer;
      }
      if(mc->length>=mc->bufsize-1)
      {  UBYTE *nb;
         if(!(nb=ALLOCTYPE(UBYTE,mc->bufsize+4096,MEMF_CLEAR))) return NULL;
         memcpy(nb,mc->buffer,mc->bufsize);
         FREE(mc->buffer);
         mc->buffer=nb;
         mc->bufsize+=4096;
      }
      if(!Mail_draining && Checktaskbreak()) return NULL;
      r=a_recv(mc->sock,mc->buffer+mc->length,mc->bufsize-mc->length-1,0,mc->socketbase);
      if(r<=0) return NULL;
      mc->length+=r;
   }
}

static BOOL Mail_readbytes(struct Mailconn *mc,long nbytes,UBYTE *dest,long destmax)
{  long got;
   long need;
   long copy;
   UBYTE *p;
   if(nbytes<=0) return TRUE;
   if(nbytes>destmax) return FALSE;
   got=0;
   if(mc->linelen && mc->length>mc->linelen)
   {  p=mc->buffer+mc->linelen;
      copy=mc->length-mc->linelen;
      if(copy>nbytes) copy=nbytes;
      memcpy(dest,p,copy);
      got=copy;
      memmove(mc->buffer,p+copy,mc->length-mc->linelen-copy);
      mc->length-=copy;
   }
   while(got<nbytes)
   {  if(!Mail_draining && Checktaskbreak()) return FALSE;
      need=nbytes-got;
      copy=a_recv(mc->sock,dest+got,need,0,mc->socketbase);
      if(copy<=0) return FALSE;
      got+=copy;
   }
   if(got>=2 && dest[got-2]=='\r' && dest[got-1]=='\n')
   {  got-=2;
   }
   dest[got]='\0';
   return TRUE;
}

static BOOL Mail_readliteral(struct Mailconn *mc,long litlen,UBYTE **out,long *outlen)
{  UBYTE *buf;
   if(litlen<0) return FALSE;
   if(!(buf=ALLOCTYPE(UBYTE,litlen+1,MEMF_CLEAR))) return FALSE;
   if(!Mail_readbytes(mc,litlen,buf,litlen))
   {  FREE(buf);
      return FALSE;
   }
   *out=buf;
   *outlen=litlen;
   return TRUE;
}

static long Mail_literal_size(UBYTE *line)
{  UBYTE *p;
   UBYTE *brace;
   long n;
   if(!line) return -1;
   /* Use the last {n} on the line (Dovecot puts the literal count at the end). */
   brace=NULL;
   for(p=line;*p;p++)
   {  if(*p=='{') brace=p;
   }
   if(!brace) return -1;
   p=brace+1;
   n=0;
   while(*p>='0' && *p<='9')
   {  n=n*10+(*p-'0');
      p++;
   }
   if(*p!='}') return -1;
   return n;
}

static BOOL Mail_sendraw(struct Mailconn *mc,UBYTE *data,long len)
{  if(a_send(mc->sock,data,len,0,mc->socketbase)!=len) return FALSE;
   return TRUE;
}

static BOOL Mail_sendline(struct Mailconn *mc,UBYTE *line)
{  long len;
   len=strlen(line);
   if(!Mail_sendraw(mc,line,len)) return FALSE;
   return Mail_sendraw(mc,(UBYTE *)"\r\n",2);
}

static BOOL Mail_sendcmd(struct Mailconn *mc,UBYTE *fmt,...)
{  va_list args;
   UBYTE cmd[512];
   long len;
   va_start(args,fmt);
   vsprintf(cmd,fmt,args);
   va_end(args);
   mc->tag++;
   len=sprintf(mc->fd->block,"%s%03ld %s",MAIL_TAG_PREFIX,mc->tag,cmd);
#if MAIL_DEBUG
   Mail_debug_imap_cmd(mc->fd->block);
#endif
   return Mail_sendline(mc,mc->fd->block);
}

/* Parse tagged response status (OK/NO/BAD), not atoi (BAD gives 0). */
static long Mail_waittag_status(UBYTE *line,long taglen)
{  UBYTE *p;
   p=line+taglen;
   while(*p==' ') p++;
   if(!strncmp(p,"OK",2)) return 0;
   if(!strncmp(p,"NO",2)) return 400;
   if(!strncmp(p,"BAD",3)) return 500;
   return 999;
}

/* IMAP quoted string for atoms with spaces or special characters */
static BOOL Mail_imap_quote(UBYTE *src,UBYTE *out,long outmax)
{  long i;
   long j;
   BOOL need;
   UBYTE c;
   if(!src || !out || outmax<3) return FALSE;
   need=FALSE;
   for(i=0;src[i];i++)
   {  c=src[i];
      if(c==' ' || c=='(' || c==')' || c=='%' || c=='*' || c=='\\' || c=='"'
         || c<' ' || c>127)
      {  need=TRUE;
         break;
      }
   }
   if(!need)
   {  strncpy(out,src,outmax-1);
      out[outmax-1]='\0';
      return TRUE;
   }
   j=0;
   if(j+1>=outmax) return FALSE;
   out[j++]='"';
   for(i=0;src[i] && j+2<outmax;i++)
   {  c=src[i];
      if(c=='\\' || c=='"')
      {  if(j+2>=outmax) return FALSE;
         out[j++]='\\';
      }
      out[j++]=c;
   }
   if(j+2>outmax) return FALSE;
   out[j++]='"';
   out[j]='\0';
   return TRUE;
}

/* Read until tagged OK/BAD/NO; ignore task break (used after partial FETCH). */
static void Mailimap_drain_tag(struct Mailconn *mc,long tag)
{  UBYTE *line;
   UBYTE tagbuf[16];
   long taglen;
   long lit;
   long lines;
   UBYTE *dummy;
   long dummylen;
   taglen=sprintf(tagbuf,"%s%03ld ",MAIL_TAG_PREFIX,tag);
   Mail_debug("Mailimap_drain_tag: tag %ld",tag);
   Mail_draining=TRUE;
   lines=0;
   for(;;)
   {  line=Mail_nextline(mc);
      if(!line) break;
      if(++lines>5000)
      {  Mail_debug("Mailimap_drain_tag: line limit");
         break;
      }
      if(!strncmp(line,tagbuf,(unsigned long)taglen)) break;
      lit=Mail_literal_size(line);
      if(lit>=0)
      {  if(!Mail_readliteral(mc,lit,&dummy,&dummylen)) break;
         if(dummy) FREE(dummy);
      }
   }
   Mail_draining=FALSE;
   if(mc->buffer)
   {  mc->length=0;
      mc->linelen=0;
   }
}

/* Wait for tagged OK/BAD/NO; consume intermediate lines */
static long Mail_waittag(struct Mailconn *mc,long tag,UBYTE **lastline)
{  UBYTE *line;
   UBYTE tagbuf[16];
   UBYTE *st;
   long taglen;
   long code;
   long lit;
   long lines;
   UBYTE *dummy;
   long dummylen;
   taglen=sprintf(tagbuf,"%s%03ld ",MAIL_TAG_PREFIX,tag);
   lines=0;
   for(;;)
   {  if(Checktaskbreak()) return 999;
      if(++lines>2000)
      {  Mail_debug("Mail_waittag: too many lines waiting for tag %ld",tag);
         return 999;
      }
      line=Mail_nextline(mc);
      if(!line) return 999;
      if(!strncmp(line,tagbuf,(unsigned long)taglen))
      {  if(lastline) *lastline=line;
         code=Mail_waittag_status(line,taglen);
#if MAIL_DEBUG
         st=line+taglen;
         while(*st==' ') st++;
         Mail_debug("Mail_waittag tag=%ld code=%ld status=%.64s",tag,code,st);
#endif
         return code;
      }
      lit=Mail_literal_size(line);
      if(lit>=0)
      {  if(!Mail_readliteral(mc,lit,&dummy,&dummylen))
         {  return 999;
         }
         if(dummy) FREE(dummy);
      }
   }
}

static BOOL Mail_getauth( struct Fetchdriver *fd,UBYTE **user,UBYTE **pass)
{  ObtainSemaphore(fd->prefssema);
   if(fd->prefs->newsauthuser && *fd->prefs->newsauthuser)
   {  *user=fd->prefs->newsauthuser;
      *pass=fd->prefs->newsauthpass?fd->prefs->newsauthpass:(UBYTE *)"";
   }
   else
   {  *user=NULL;
      *pass=NULL;
   }
   ReleaseSemaphore(fd->prefssema);
   if(*user && **user)
   {  Mail_debug("Mail_getauth user=%s",*user);
      return TRUE;
   }
   Mail_debug("Mail_getauth: no News username configured");
   return FALSE;
}

static void Mail_base64encode(UBYTE *in,long inlen,UBYTE *out,long outmax)
{  static UBYTE tab[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   long i;
   long j;
   unsigned long v;
   j=0;
   for(i=0;i<inlen && j+4<outmax;i+=3)
   {  v=(unsigned long)(unsigned char)in[i]<<16;
      if(i+1<inlen) v|=(unsigned long)(unsigned char)in[i+1]<<8;
      if(i+2<inlen) v|=(unsigned long)(unsigned char)in[i+2];
      out[j++]=tab[(v>>18)&63];
      out[j++]=tab[(v>>12)&63];
      out[j++]=(i+1<inlen)?tab[(v>>6)&63]:'=';
      out[j++]=(i+2<inlen)?tab[v&63]:'=';
   }
   out[j]='\0';
}

static BOOL Mail_smtp_auth(struct Mailconn *mc,UBYTE *user,UBYTE *pass)
{  UBYTE plain[384];
   UBYTE b64[520];
   long len;
   long ulen;
   long plen;
   long tag;
   ulen=strlen(user);
   plen=strlen(pass);
   len=1+ulen+1+plen;
   if(len>=(long)sizeof(plain)) return FALSE;
   plain[0]='\0';
   memcpy(plain+1,user,ulen);
   plain[1+ulen]='\0';
   memcpy(plain+1+ulen+1,pass,plen);
   Mail_base64encode(plain,len,b64,sizeof(b64));
   tag=mc->tag+1;
   if(!Mail_sendcmd(mc,"AUTH PLAIN %s",b64)) return FALSE;
   if(Mail_waittag(mc,tag,NULL)>=400) return FALSE;
   return TRUE;
}

/* --- connection --- */

void Mailimap_close(struct Mailconn *mc)
{  if(mc->sock>=0)
   {  /* Close without shutdown/LOGOUT; drain_tag leaves the socket clean. */
      a_close(mc->sock,mc->socketbase);
      mc->sock=-1;
   }
   if(mc->buffer)
   {  FREE(mc->buffer);
      mc->buffer=NULL;
   }
   if(mc->socketbase)
   {  a_cleanup(mc->socketbase);
      CloseLibrary(mc->socketbase);
      mc->socketbase=NULL;
   }
   if(mc->imap_lock_held)
   {  Mail_imap_unlock();
      mc->imap_lock_held=FALSE;
   }
}

BOOL Mailimap_open(struct Mailconn *mc,BOOL optional)
{  UBYTE imaphost[128];
   long port;
   UBYTE *user;
   UBYTE *pass;
   long tag;
   UBYTE *line;
   mc->sock=-1;
   mc->tag=0;
   mc->ssl_active=FALSE;
   mc->imap_lock_held=FALSE;
   if(!Mail_imap_lock(optional?FALSE:TRUE))
   {  Mail_debug("Mailimap_open: IMAP session busy (another fetch active?)");
      return FALSE;
   }
   mc->imap_lock_held=TRUE;
   Mail_imap_host(mc->fd,imaphost,sizeof(imaphost));
   Mail_debug("Mailimap_open host=%s port=%ld",imaphost,(long)MAIL_IMAP_PORT);
   if(!imaphost[0])
   {  Mail_debug("Mailimap_open: empty IMAP host (set Mail host SMTP in prefs)");
      if(!optional) Tcperror(mc->fd,TCPERR_NOHOST,"IMAP");
      Mailimap_close(mc);
      return FALSE;
   }
   /* IMAPS: implicit TLS like GEMINI:// and FTPS:// */
   port=MAIL_IMAP_PORT;
   mc->fd->flags|=FDVF_SSL;
   AwebTcpBase=Opentcp(&mc->socketbase,mc->fd,!optional);
   if(!mc->socketbase)
   {  Mail_debug("Mailimap_open: Opentcp failed");
      Mailimap_close(mc);
      return FALSE;
   }
   strncpy(mc->hostname,imaphost,sizeof(mc->hostname)-1);
   Updatetaskattrs(AOURL_Netstatus,NWS_LOOKUP,TAG_END);
   Tcpmessage(mc->fd,TCPMSG_LOOKUP,imaphost);
   mc->hent=Lookup(imaphost,mc->socketbase);
   if(!mc->hent)
   {  Mail_debug("Mailimap_open: DNS lookup failed for %s",imaphost);
      if(!optional) Tcperror(mc->fd,TCPERR_NOHOST,imaphost);
      Mailimap_close(mc);
      return FALSE;
   }
   if((mc->sock=a_socket(mc->hent->h_addrtype,SOCK_STREAM,0,mc->socketbase))<0)
   {  Mailimap_close(mc);
      return FALSE;
   }
   Updatetaskattrs(AOURL_Netstatus,NWS_CONNECT,TAG_END);
   Tcpmessage(mc->fd,TCPMSG_CONNECT,"IMAP",mc->hent->h_name);
   if(a_connect(mc->sock,mc->hent,port,mc->socketbase))
   {  Mail_debug("Mailimap_open: TCP/SSL connect failed to %s",mc->hent->h_name);
      if(!optional) Tcperror(mc->fd,TCPERR_NOCONNECT,mc->hent->h_name);
      Mailimap_close(mc);
      return FALSE;
   }
   mc->ssl_active=TRUE;
   line=Mail_nextline(mc);
   if(!line || strncmp(line,"* OK",4))
   {  Mail_debug("Mailimap_open: bad greeting: %s",line?line:(UBYTE *)"(null)");
      Mailimap_close(mc);
      return FALSE;
   }
   if(Mail_getauth(mc->fd,&user,&pass))
   {  UBYTE plain[384];
      UBYTE b64[520];
      UBYTE quser[192];
      UBYTE qpass[192];
      long len;
      long ulen;
      long plen;
      long authok;
      authok=FALSE;
      ulen=strlen(user);
      plen=strlen(pass);
      len=1+ulen+1+plen;
      if(len<(long)sizeof(plain))
      {  plain[0]='\0';
         memcpy(plain+1,user,ulen);
         plain[1+ulen]='\0';
         memcpy(plain+1+ulen+1,pass,plen);
         Mail_base64encode(plain,len,b64,sizeof(b64));
         tag=mc->tag+1;
         Mail_sendcmd(mc,"AUTHENTICATE PLAIN %s",b64);
         if(Mail_waittag(mc,tag,NULL)<400) authok=TRUE;
         else Mail_debug("Mailimap_open: AUTH PLAIN failed");
      }
      if(!authok && Mail_imap_quote(user,quser,sizeof(quser))
      && Mail_imap_quote(pass,qpass,sizeof(qpass)))
      {  tag=mc->tag+1;
         Mail_sendcmd(mc,"LOGIN %s %s",quser,qpass);
         if(Mail_waittag(mc,tag,NULL)<400) authok=TRUE;
         else Mail_debug("Mailimap_open: LOGIN failed");
      }
      if(!authok)
      {  if(!optional) Tcperror(mc->fd,TCPERR_NOLOGIN,imaphost,user);
         Mailimap_close(mc);
         return FALSE;
      }
   }
   else
   {  Mail_debug("Mailimap_open: no auth, continuing unauthenticated");
   }
   Mail_debug("Mailimap_open: OK");
   return TRUE;
}

/* True if token is hierarchy delimiter, not a mailbox name. */
static BOOL Mail_list_is_delim(UBYTE *tok,long len)
{  if(len==1 && (tok[0]=='/' || tok[0]=='.')) return TRUE;
   if(len==2 && tok[0]=='/' && tok[1]=='/') return TRUE;
   return FALSE;
}

/* LIST/LSUB: * LIST (flags) "/" "INBOX" — skip delimiter token, take mailbox. */
static BOOL Mail_list_mailbox_name(UBYTE *line,UBYTE *name,long namemax)
{  UBYTE *p;
   UBYTE *q;
   UBYTE *tok;
   UBYTE *last;
   long len;
   long lastlen;
   if(!line || !name || namemax<=0) return FALSE;
   name[0]='\0';
   p=strstr(line,"LIST ");
   if(!p) p=strstr(line,"LSUB ");
   if(!p) return FALSE;
   p+=5;
   q=strrchr(p,')');
   if(q) p=q+1;
   last=NULL;
   lastlen=0;
   while(*p)
   {  while(*p==' ') p++;
      if(!*p || *p=='\r' || *p=='\n') break;
      tok=p;
      if(*p=='"')
      {  p++;
         tok=p;
         while(*p && *p!='"') p++;
         len=p-tok;
         if(*p=='"') p++;
      }
      else
      {  while(*p && *p!=' ' && *p!='\r' && *p!='\n') p++;
         len=p-tok;
      }
      if(len>0 && len<namemax && !Mail_list_is_delim(tok,len))
      {  last=tok;
         lastlen=len;
      }
   }
   if(!last || lastlen<=0) return FALSE;
   memcpy(name,last,lastlen);
   name[lastlen]='\0';
   return TRUE;
}

static long Mailimap_list_cmd(struct Mailconn *mc,struct Mailfolder *folders,
   long maxfolders,UBYTE *cmd)
{  long tag;
   UBYTE *line;
   UBYTE tagbuf[16];
   long count;
   long lit;
   UBYTE *dummy;
   long dummylen;
   long i;
   BOOL dup;
   if(maxfolders<=0) return 0;
   tag=mc->tag+1;
   if(!Mail_sendcmd(mc,"%s",cmd)) return 0;
   count=0;
   sprintf(tagbuf,"%s%03ld ",MAIL_TAG_PREFIX,tag);
   for(;;)
   {  line=Mail_nextline(mc);
      if(!line) break;
      if(!strncmp(line,tagbuf,strlen(tagbuf))) break;
      if((!strncmp(line,"* LIST",6) || !strncmp(line,"* LSUB",6)) && count<maxfolders)
      {  if(strstr(line,"\\Noselect") || strstr(line,"\\NoSelect")) continue;
         if(Mail_list_mailbox_name(line,folders[count].name,sizeof(folders[count].name)))
         {  dup=FALSE;
            for(i=0;i<count;i++)
            {  if(!stricmp(folders[i].name,folders[count].name))
               {  dup=TRUE;
                  break;
               }
            }
            if(!dup) count++;
         }
      }
      lit=Mail_literal_size(line);
      if(lit>=0)
      {  if(!Mail_readliteral(mc,lit,&dummy,&dummylen)) break;
         if(dummy) FREE(dummy);
      }
   }
   if(!line || strncmp(line,tagbuf,strlen(tagbuf)))
   {  Mailimap_drain_tag(mc,tag);
   }
   return count;
}

long Mailimap_list(struct Mailconn *mc,struct Mailfolder *folders,long maxfolders)
{  long count;
   count=Mailimap_list_cmd(mc,folders,maxfolders,"LIST \"\" \"*\"");
   if(count<=0)
   {  count=Mailimap_list_cmd(mc,folders,maxfolders,"LIST \"\" \"%\"");
   }
   if(count<=0)
   {  count=Mailimap_list_cmd(mc,folders,maxfolders,"LSUB \"\" \"*\"");
      if(count>0) Mail_debug("Mailimap_list: LSUB returned %ld folders",count);
   }
   return count;
}

BOOL Mailimap_select(struct Mailconn *mc,UBYTE *mailbox)
{  long tag;
   if(!mailbox || !*mailbox) mailbox=(UBYTE *)"INBOX";
   tag=mc->tag+1;
   Mail_debug("Mailimap_select: %s",mailbox);
   Mail_sendcmd(mc,"SELECT \"%s\"",mailbox);
   if(Mail_waittag(mc,tag,NULL)<400)
   {  Mail_debug("Mailimap_select: OK");
      return TRUE;
   }
   Mail_debug("Mailimap_select: failed");
   return FALSE;
}

long Mailimap_search_all(struct Mailconn *mc,unsigned long *uids,long maxuids)
{  long tag;
   UBYTE *line;
   UBYTE tagbuf[16];
   UBYTE *p;
   long count;
   long n;
   if(maxuids<=0) return 0;
   tag=mc->tag+1;
   Mail_sendcmd(mc,"UID SEARCH ALL");
   count=0;
   sprintf(tagbuf,"%s%03ld ",MAIL_TAG_PREFIX,tag);
   for(;;)
   {  if(Checktaskbreak()) break;
      line=Mail_nextline(mc);
      if(!line) break;
      if(!strncmp(line,tagbuf,strlen(tagbuf))) break;
      if(!strncmp(line,"* SEARCH",8))
      {  p=line+8;
         while(*p && count<maxuids)
         {  while(*p==' ') p++;
            if(!*p) break;
            n=0;
            while(*p>='0' && *p<='9')
            {  n=n*10+(*p-'0');
               p++;
            }
            if(n>0) uids[count++]=(unsigned long)n;
         }
      }
   }
   return count;
}

static void Mail_copy_quoted(UBYTE *dst,long dstmax,UBYTE *src,long srclen)
{  long n;
   if(srclen<0) srclen=strlen(src);
   n=srclen;
   if(n>=dstmax) n=dstmax-1;
   memcpy(dst,src,n);
   dst[n]='\0';
}

/* Parse header fields from a FETCH literal block */
static void Mail_parse_header_literal(UBYTE *hdr,long hdrlen,
   UBYTE *subject,long subjmax,UBYTE *from,long frommax,
   UBYTE *date,long datemax,BOOL *unseen)
{  UBYTE *p;
   UBYTE *end;
   UBYTE *eol;
   UBYTE *hp;
   long linelen;
   if(subject && subjmax>0) subject[0]='\0';
   if(from && frommax>0) from[0]='\0';
   if(date && datemax>0) date[0]='\0';
   if(!hdr || hdrlen<=0) return;
   p=hdr;
   end=hdr+hdrlen;
   while(p<end)
   {  for(eol=p;eol<end && *eol!='\n' && *eol!='\r';eol++);
      linelen=eol-p;
      if(linelen>=8 && STRNIEQUAL(p,"Subject:",8))
      {  hp=p+8;
         while(hp<eol && *hp==' ') hp++;
         if(subject) Mail_copy_quoted(subject,subjmax,hp,eol-hp);
      }
      else if(linelen>=5 && STRNIEQUAL(p,"From:",5))
      {  hp=p+5;
         while(hp<eol && *hp==' ') hp++;
         if(from) Mail_copy_quoted(from,frommax,hp,eol-hp);
      }
      else if(linelen>=5 && STRNIEQUAL(p,"Date:",5))
      {  hp=p+5;
         while(hp<eol && *hp==' ') hp++;
         if(date) Mail_copy_quoted(date,datemax,hp,eol-hp);
      }
      if(eol<end)
      {  p=eol+1;
         if(p<end && *p=='\n' && eol>p && *(p-1)=='\r') p++;
      }
      else break;
   }
   (void)unseen;
}

/* Read one IMAP quoted string; advance *pp past it. */
static BOOL Mail_read_quoted(UBYTE **pp,UBYTE *out,long outmax)
{  UBYTE *p;
   long j;
   p=*pp;
   while(*p==' ') p++;
   if(*p!='"') return FALSE;
   p++;
   j=0;
   while(*p && *p!='"')
   {  if(*p=='\\' && p[1]) p++;
      if(j+1<outmax) out[j++]=*p;
      p++;
   }
   if(*p=='"') p++;
   out[j]='\0';
   *pp=p;
   return TRUE;
}

/* ENVELOPE on one FETCH line (no {literal} blocks; safe for Dovecot batch issues). */
static void Mail_parse_envelope_fetch(UBYTE *line,
   UBYTE *subject,long subjmax,UBYTE *from,long frommax,
   UBYTE *date,long datemax)
{  UBYTE *p;
   UBYTE name[96];
   UBYTE mb[64];
   UBYTE host[64];
   if(subject && subjmax>0) subject[0]='\0';
   if(from && frommax>0) from[0]='\0';
   if(date && datemax>0) date[0]='\0';
   if(!line) return;
   p=strstr(line,"ENVELOPE ");
   if(!p) return;
   p+=9;
   while(*p==' ') p++;
   if(*p!='(') return;
   p++;
   if(date && datemax>0) Mail_read_quoted(&p,date,datemax);
   if(subject && subjmax>0) Mail_read_quoted(&p,subject,subjmax);
   name[0]='\0';
   mb[0]='\0';
   host[0]='\0';
   while(*p==' ') p++;
   if(*p=='(')
   {  p++;
      while(*p==' ') p++;
      if(*p=='(')
      {  p++;
         while(*p==' ') p++;
         if(*p=='"') Mail_read_quoted(&p,name,sizeof(name));
         else if(STRNIEQUAL(p,"NIL",3)) p+=3;
         while(*p==' ') p++;
         if(STRNIEQUAL(p,"NIL",3)) p+=3;
         while(*p==' ') p++;
         if(*p=='"') Mail_read_quoted(&p,mb,sizeof(mb));
         while(*p==' ') p++;
         if(*p=='"') Mail_read_quoted(&p,host,sizeof(host));
      }
   }
   if(from && frommax>0)
   {  if(name[0])
      {  strncpy(from,name,frommax-1);
         from[frommax-1]='\0';
      }
      else if(mb[0])
      {  strncpy(from,mb,frommax-1);
         from[frommax-1]='\0';
      }
      else if(host[0])
      {  strncpy(from,host,frommax-1);
         from[frommax-1]='\0';
      }
   }
}

long Mailimap_fetch_envelope(struct Mailconn *mc,unsigned long uid,
   UBYTE *subject,long subjmax,UBYTE *from,long frommax,
   UBYTE *date,long datemax,BOOL *unseen)
{  long tag;
   UBYTE *line;
   UBYTE tagbuf[16];
   long lit;
   UBYTE *litbuf;
   long litlen;
   if(subject && subjmax>0) subject[0]='\0';
   if(from && frommax>0) from[0]='\0';
   if(date && datemax>0) date[0]='\0';
   if(unseen) *unseen=TRUE;
   tag=mc->tag+1;
   Mail_debug("Mailimap_fetch_envelope: uid %lu",uid);
   Mail_sendcmd(mc,"UID FETCH %lu (FLAGS ENVELOPE)",uid);
   sprintf(tagbuf,"%s%03ld ",MAIL_TAG_PREFIX,tag);
   litbuf=NULL;
   litlen=0;
   for(;;)
   {  line=Mail_nextline(mc);
      if(!line) break;
      if(!strncmp(line,tagbuf,strlen(tagbuf))) break;
      if(strstr(line,"\\Seen") || strstr(line,"\\seen"))
      {  if(unseen) *unseen=FALSE;
      }
      else if(strstr(line,"FLAGS") && !strstr(line,"\\Seen"))
      {  if(unseen) *unseen=TRUE;
      }
      if(strstr(line,"ENVELOPE"))
      {  Mail_parse_envelope_fetch(line,subject,subjmax,from,frommax,date,datemax);
      }
      lit=Mail_literal_size(line);
      if(lit>=0)
      {  if(!Mail_readliteral(mc,lit,&litbuf,&litlen))
         {  Mail_debug("Mailimap_fetch_envelope: skip literal %ld",lit);
            break;
         }
         FREE(litbuf);
         litbuf=NULL;
         continue;
      }
   }
   if(litbuf) FREE(litbuf);
   if(!line || strncmp(line,tagbuf,strlen(tagbuf)))
   {  Mailimap_drain_tag(mc,tag);
   }
   return 0;
}

static unsigned long Mail_fetch_line_uid(UBYTE *line)
{  UBYTE *p;
   UBYTE *q;
   unsigned long uid;
   if(!line) return 0;
   p=line;
   while((p=strstr(p,"UID "))!=NULL)
   {  q=p+4;
      while(*q==' ') q++;
      if(*q>='0' && *q<='9')
      {  uid=0;
         while(*q>='0' && *q<='9')
         {  uid=uid*10+(*q-'0');
            q++;
         }
         return uid;
      }
      p+=4;
   }
   return 0;
}

static long Mail_envelope_index(unsigned long *uids,long nuids,unsigned long uid)
{  long i;
   for(i=0;i<nuids;i++)
   {  if(uids[i]==uid) return i;
   }
   return -1;
}

static BOOL Mail_build_uid_set(UBYTE *buf,long bufmax,unsigned long *uids,long nuids)
{  long i;
   long pos;
   pos=0;
   buf[0]='\0';
   for(i=0;i<nuids;i++)
   {  if(i>0)
      {  if(pos+1>=bufmax) return FALSE;
         buf[pos++]=',';
         buf[pos]='\0';
      }
      if(pos+16>=bufmax) return FALSE;
      pos+=sprintf(buf+pos,"%lu",uids[i]);
   }
   return TRUE;
}

/* One IMAP round-trip for many message headers (avoids per-UID desync/hang). */
long Mailimap_fetch_envelopes(struct Mailconn *mc,unsigned long *uids,long nuids,
   struct Mailuid *items,long itemmax)
{  UBYTE uidset[2048];
   long chunk;
   long start;
   long n;
   long tag;
   UBYTE tagbuf[16];
   UBYTE *line;
   long lit;
   UBYTE *litbuf;
   long litlen;
   unsigned long uid;
   long idx;
   long i;
   if(!uids || !items || nuids<=0 || itemmax<=0) return 0;
   if(nuids>itemmax) nuids=itemmax;
   for(i=0;i<nuids;i++)
   {  items[i].uid=uids[i];
      items[i].subject[0]='\0';
      items[i].from[0]='\0';
      items[i].date[0]='\0';
      items[i].unseen=TRUE;
   }
   chunk=25;
   for(start=0;start<nuids;start+=chunk)
   {  unsigned long last_uid;
      if(Checktaskbreak()) break;
      n=nuids-start;
      if(n>chunk) n=chunk;
      if(!Mail_build_uid_set(uidset,sizeof(uidset),uids+start,n))
      {  Mail_debug("Mailimap_fetch_envelopes: uid set too long");
         break;
      }
      tag=mc->tag+1;
      if(!Mail_sendcmd(mc,"UID FETCH %s (FLAGS BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])",
         uidset))
      {  break;
      }
      sprintf(tagbuf,"%s%03ld ",MAIL_TAG_PREFIX,tag);
      litbuf=NULL;
      litlen=0;
      last_uid=0;
      for(;;)
      {  if(Checktaskbreak()) break;
         line=Mail_nextline(mc);
         if(!line) break;
         if(!strncmp(line,tagbuf,strlen(tagbuf))) break;
         if(!strncmp(line,"* ",2))
         {  uid=Mail_fetch_line_uid(line);
            if(uid) last_uid=uid;
            if(uid)
            {  idx=Mail_envelope_index(uids,nuids,uid);
               if(idx>=0 && strstr(line,"FLAGS"))
               {  if(strstr(line,"\\Seen") || strstr(line,"\\seen"))
                  {  items[idx].unseen=FALSE;
                  }
               }
            }
         }
         lit=Mail_literal_size(line);
         if(lit>=0)
         {  uid=Mail_fetch_line_uid(line);
            if(!uid) uid=last_uid;
            if(Mail_readliteral(mc,lit,&litbuf,&litlen))
            {  if(uid)
               {  idx=Mail_envelope_index(uids,nuids,uid);
                  if(idx>=0)
                  {  Mail_parse_header_literal(litbuf,litlen,
                        items[idx].subject,sizeof(items[idx].subject),
                        items[idx].from,sizeof(items[idx].from),
                        items[idx].date,sizeof(items[idx].date),
                        &items[idx].unseen);
                  }
               }
               FREE(litbuf);
               litbuf=NULL;
            }
            else
            {  Mail_debug("Mailimap_fetch_envelopes: literal read failed (%ld)",lit);
               Mailimap_drain_tag(mc,tag);
               return nuids;
            }
         }
      }
      if(litbuf) FREE(litbuf);
      if(!line || strncmp(line,tagbuf,strlen(tagbuf)))
      {  Mailimap_drain_tag(mc,tag);
      }
   }
   Mail_debug("Mailimap_fetch_envelopes: done nuids=%ld",nuids);
   return nuids;
}

BOOL Mailimap_fetch_body(struct Mailconn *mc,unsigned long uid,
   UBYTE **body,long *bodylen)
{  long tag;
   UBYTE *line;
   UBYTE tagbuf[16];
   long lit;
   UBYTE *buf;
   long len;
   long total;
   UBYTE *nb;
   *body=NULL;
   *bodylen=0;
   tag=mc->tag+1;
   Mail_sendcmd(mc,"UID FETCH %lu (BODY.PEEK[])",uid);
   sprintf(tagbuf,"%s%03ld ",MAIL_TAG_PREFIX,tag);
   total=0;
   buf=NULL;
   for(;;)
   {  line=Mail_nextline(mc);
      if(!line) break;
      if(!strncmp(line,tagbuf,strlen(tagbuf))) break;
      lit=Mail_literal_size(line);
      if(lit>=0)
      {  if(!Mail_readliteral(mc,lit,&nb,&len)) break;
         {  UBYTE *grow;
            grow=ALLOCTYPE(UBYTE,total+len+1,MEMF_CLEAR);
            if(!grow)
            {  FREE(nb);
               break;
            }
            if(buf)
            {  memcpy(grow,buf,total);
               FREE(buf);
            }
            buf=grow;
            memcpy(buf+total,nb,len);
            total+=len;
            buf[total]='\0';
            FREE(nb);
         }
      }
   }
   if(buf && total>0)
   {  *body=buf;
      *bodylen=total;
      return TRUE;
   }
   if(buf) FREE(buf);
   return FALSE;
}

BOOL Mailimap_store_seen(struct Mailconn *mc,unsigned long uid)
{  long tag;
   tag=mc->tag+1;
   Mail_sendcmd(mc,"UID STORE %lu +FLAGS (\\Seen)",uid);
   if(Mail_waittag(mc,tag,NULL)<400) return TRUE;
   return FALSE;
}

/* --- SMTP --- */

static long Smtp_getresponse(struct Fetchdriver *fd,long sock,struct Library *SocketBase)
{  long len;
   len=a_recv(sock,fd->block,fd->blocksize,0,SocketBase);
   if(len<0) return 999;
   fd->block[MIN(len,fd->blocksize-1)]='\0';
   return atoi(fd->block);
}

static long Smtp_sendcommand(struct Fetchdriver *fd,long sock,struct Library *SocketBase,
   UBYTE *fmt,...)
{  va_list args;
   long len;
   va_start(args,fmt);
   vsprintf(fd->block,fmt,args);
   va_end(args);
   strcat(fd->block,"\r\n");
   len=strlen(fd->block);
   if(a_send(sock,fd->block,len,0,SocketBase)!=len) return 999;
   return Smtp_getresponse(fd,sock,SocketBase);
}

short Mailsmtp_send(struct Fetchdriver *fd,UBYTE *rcpt_to,UBYTE *message,long msglen)
{  struct Mailconn mc={0};
   struct Library *SocketBase;
   struct hostent *hent;
   long sock;
   UBYTE name[128];
   UBYTE *p;
   UBYTE *q;
   UBYTE *user;
   UBYTE *pass;
   BOOL error;
   short retval;
   ULONG saved_flags;
   retval=SMR_FAIL;
   error=FALSE;
   saved_flags=fd->flags;
   mc.fd=fd;
   mc.sock=-1;
   fd->flags|=FDVF_SSL;
   AwebTcpBase=Opentcp(&SocketBase,fd,TRUE);
   if(!SocketBase) return SMR_NOCONNECT;
   ObtainSemaphore(fd->prefssema);
   if(!*fd->prefs->smtphost)
   {  ReleaseSemaphore(fd->prefssema);
      a_cleanup(SocketBase);
      CloseLibrary(SocketBase);
      fd->flags=saved_flags;
      return SMR_NOCONNECT;
   }
   Tcpmessage(fd,TCPMSG_LOOKUP,fd->prefs->smtphost);
   hent=Lookup(fd->prefs->smtphost,SocketBase);
   ReleaseSemaphore(fd->prefssema);
   if(!hent)
   {  a_cleanup(SocketBase);
      CloseLibrary(SocketBase);
      fd->flags=saved_flags;
      return SMR_NOCONNECT;
   }
   strncpy(mc.hostname,hent->h_name,sizeof(mc.hostname)-1);
   if((sock=a_socket(hent->h_addrtype,SOCK_STREAM,0,SocketBase))<0)
   {  a_cleanup(SocketBase);
      CloseLibrary(SocketBase);
      fd->flags=saved_flags;
      return SMR_NOCONNECT;
   }
   mc.sock=sock;
   mc.socketbase=SocketBase;
   Updatetaskattrs(AOURL_Netstatus,NWS_CONNECT,TAG_END);
   Tcpmessage(fd,TCPMSG_CONNECT,"SMTP",hent->h_name);
   gethostname(name,sizeof(name));
   /* SMTPS port 465: implicit TLS via a_connect (same as gemini.c / ftp.c FTPS).
    * aweblib modules must not call Setsslsocket/GetTaskSSLContext (main binary). */
   if(a_connect(sock,hent,MAIL_SMTP_SSLPORT,SocketBase)
   || Smtp_getresponse(fd,sock,SocketBase)>=400)
   {  retval=SMR_NOCONNECT;
      goto smtp_done;
   }
   mc.ssl_active=TRUE;
   if(Smtp_sendcommand(fd,sock,SocketBase,"EHLO %s",name)>=400)
   {  retval=SMR_NOCONNECT;
      goto smtp_done;
   }
   if(Mail_getauth(fd,&user,&pass))
   {  mc.fd=fd;
      mc.socketbase=SocketBase;
      mc.tag=0;
      if(!Mail_smtp_auth(&mc,user,pass))
      {  retval=SMR_FAIL;
         goto smtp_done;
      }
   }
   ObtainSemaphore(fd->prefssema);
   strncpy(name,fd->prefs->emailaddr,sizeof(name));
   ReleaseSemaphore(fd->prefssema);
   Tcpmessage(fd,TCPMSG_MAILSEND);
   if(Smtp_sendcommand(fd,sock,SocketBase,"MAIL FROM:<%s>",name)<400)
   {  retval=SMR_FAIL;
      goto smtp_done;
   }
   if(rcpt_to && *rcpt_to)
   {  p=Dupstr(rcpt_to,-1);
      if(p)
      {  while(p)
         {  if(q=strchr(p,',')) *q++='\0';
            while(*p==' ') p++;
            if(*p && Smtp_sendcommand(fd,sock,SocketBase,"RCPT TO:<%s>",p)>=400)
            {  error=TRUE;
               break;
            }
            p=q;
         }
         FREE(p);
      }
   }
   if(!error)
   {  if(Smtp_sendcommand(fd,sock,SocketBase,"DATA")<400)
      {  a_send(sock,message,msglen,0,SocketBase);
         a_send(sock,(UBYTE *)"\r\n.\r\n",5,0,SocketBase);
         if(Smtp_getresponse(fd,sock,SocketBase)<400) retval=SMR_OK;
      }
   }
smtp_done:
   if(sock>=0) a_close(sock,SocketBase);
   a_cleanup(SocketBase);
   CloseLibrary(SocketBase);
   fd->flags=saved_flags;
   return retval;
}

#endif /* !LOCALONLY */
