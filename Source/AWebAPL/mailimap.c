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
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/socket.h>

#ifndef LOCALONLY

extern struct Library *AwebTcpBase;
extern void *AwebPluginBase;

/* One IMAP session at a time. Named system semaphore serializes mail fetch
 * subtasks (same address space as aweblib; library statics are shared).
 * Always Obtain/Release per open/close -- never skip Obtain because a flag is
 * set (that let a second task run concurrent TCP sessions). Exec releases the
 * semaphore if a task dies while holding it. */
#define MAIL_IMAP_SEMA_NAME "AWebMailIMAP"

static BOOL Mail_imap_owns_sema;
static struct SignalSemaphore *Mail_imap_syssema;
static BOOL Mail_draining;

static struct SignalSemaphore *Mail_imap_get_syssema(void)
{
   struct SignalSemaphore *sema;
   struct SignalSemaphore *created;

   Forbid();
   sema=FindSemaphore((STRPTR)MAIL_IMAP_SEMA_NAME);
   if(sema)
   {  Permit();
      return sema;
   }
   Permit();

   created=(struct SignalSemaphore *)AllocMem(
      (ULONG)sizeof(struct SignalSemaphore),MEMF_PUBLIC|MEMF_CLEAR);
   if(!created) return NULL;
   created->ss_Link.ln_Type=NT_SIGNALSEM;
   created->ss_Link.ln_Name=(UBYTE *)MAIL_IMAP_SEMA_NAME;
   created->ss_Link.ln_Pri=0;
   InitSemaphore(created);

   Forbid();
   sema=FindSemaphore((STRPTR)MAIL_IMAP_SEMA_NAME);
   if(!sema)
   {
      AddSemaphore(created);
      sema=created;
   }
   else
   {
      FreeMem(created,(ULONG)sizeof(struct SignalSemaphore));
   }
   Permit();

   return sema;
}

static BOOL Mail_imap_lock(BOOL wait)
{
   if(!Mail_imap_syssema)
      Mail_imap_syssema=Mail_imap_get_syssema();
   if(!Mail_imap_syssema)
   {  Mail_debug("Mailimap_lock: could not create IMAP system semaphore");
      return FALSE;
   }
   if(wait)
   {
      ObtainSemaphore(Mail_imap_syssema);
   }
   else if(!AttemptSemaphore(Mail_imap_syssema))
   {
      Mail_debug("Mailimap_lock: IMAP session busy (another mail fetch active?)");
      return FALSE;
   }
   Mail_imap_owns_sema=TRUE;
   return TRUE;
}

static void Mail_imap_unlock(void)
{
   if(Mail_imap_owns_sema && Mail_imap_syssema)
   {
      ReleaseSemaphore(Mail_imap_syssema);
      Mail_imap_owns_sema=FALSE;
   }
}

void Mail_imap_release_stale(void)
{
   /* Only drop a lock this process still holds; never touch another task's. */
   if(Mail_imap_owns_sema)
      Mail_imap_unlock();
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

/* Pull the next CRLF-terminated IMAP line into mc->buffer. Returns a pointer
 * to a NUL-terminated line (with the trailing CR/LF replaced by '\0'); the
 * caller may keep using the pointer until the next Mail_nextline or
 * Mail_readbytes call, which consume the line. On error the buffer is freed
 * so the next attempt allocates fresh state and we never accidentally re-use
 * a partial response from a dropped connection. */
static UBYTE *Mail_nextline(struct Mailconn *mc)
{  UBYTE *p;
   UBYTE *end;
   UBYTE *nb;
   long r;
   long newsize;
   if(!mc->buffer)
   {  if(!(mc->buffer=ALLOCTYPE(UBYTE,4096,MEMF_CLEAR))) return NULL;
      mc->bufsize=4096;
      mc->length=0;
      mc->linelen=0;
   }
   if(mc->linelen)
   {  if(mc->length>mc->linelen)
         memmove(mc->buffer,mc->buffer+mc->linelen,mc->length-mc->linelen);
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
      {  /* Grow geometrically once the line crosses 16k so very long Subject
          * lines or fetched literals don't drag us into many small reallocs. */
         newsize=mc->bufsize<16384 ? mc->bufsize+4096 : mc->bufsize*2;
         if(!(nb=ALLOCTYPE(UBYTE,newsize,MEMF_CLEAR)))
         {  /* Out of memory: drop buffer state so we don't keep a half-line
             * around for the next call. */
            FREE(mc->buffer);
            mc->buffer=NULL;
            mc->length=0;
            mc->bufsize=0;
            return NULL;
         }
         if(mc->length>0) memcpy(nb,mc->buffer,mc->length);
         FREE(mc->buffer);
         mc->buffer=nb;
         mc->bufsize=newsize;
      }
      if(!Mail_draining && Checktaskbreak())
      {  /* User cancelled mid-read. Tear down the buffer so callers that
          * react to the NULL by issuing a Mailimap_close don't try to drain
          * a half-line from stale memory. */
         FREE(mc->buffer);
         mc->buffer=NULL;
         mc->length=0;
         mc->bufsize=0;
         mc->linelen=0;
         return NULL;
      }
      r=a_recv(mc->sock,mc->buffer+mc->length,mc->bufsize-mc->length-1,0,mc->socketbase);
      if(r<=0)
      {  /* Socket dropped; keep buffer for any final drain attempt by the
          * caller but make sure subsequent calls return NULL until reset. */
         return NULL;
      }
      mc->length+=r;
   }
}

/* Read exactly nbytes from the IMAP socket. Pull from any bytes that the line
 * reader already buffered past the current terminated line, then top up from
 * the wire. On return the connection state is reset (linelen=0) and mc->length
 * reflects only the bytes that remain unread *after* this literal, so the next
 * Mail_nextline does not re-shift a stale offset. The IMAP literal {N} reply
 * is exactly N bytes and is NOT followed by an implicit CRLF that belongs to
 * the literal itself: any CRLF that follows is part of the surrounding tagged
 * response. Do not strip it here (the previous code corrupted bodies whose
 * payload happened to end with CRLF). */
static BOOL Mail_readbytes(struct Mailconn *mc,long nbytes,UBYTE *dest,long destmax)
{  long got;
   long need;
   long copy;
   long remain;
   UBYTE *p;
   if(nbytes<=0) { if(destmax>0) dest[0]='\0'; return TRUE; }
   /* Caller must reserve destmax >= nbytes; the trailing '\0' goes at dest[got]
    * (which equals nbytes), so the buffer must hold nbytes+1 bytes overall. */
   if(nbytes>destmax) return FALSE;
   got=0;
   if(mc->buffer && mc->linelen)
   {  if(mc->length>mc->linelen)
      {  p=mc->buffer+mc->linelen;
         copy=mc->length-mc->linelen;
         if(copy>nbytes) copy=nbytes;
         memcpy(dest,p,copy);
         got=copy;
         remain=mc->length-mc->linelen-copy;
         if(remain>0) memmove(mc->buffer,p+copy,remain);
         mc->length=remain;
      }
      else
      {  /* No bytes past the terminated line; just drop the consumed line. */
         mc->length=0;
      }
      mc->linelen=0;
   }
   while(got<nbytes)
   {  if(!Mail_draining && Checktaskbreak()) return FALSE;
      need=nbytes-got;
      copy=a_recv(mc->sock,dest+got,need,0,mc->socketbase);
      if(copy<=0) return FALSE;
      got+=copy;
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
      if(!line)
      {  Mail_debug("Mail_waittag: no line (socket dropped) waiting for tag %ld",tag);
         return 999;
      }
      if(!strncmp(line,tagbuf,(unsigned long)taglen))
      {  if(lastline) *lastline=line;
         code=Mail_waittag_status(line,taglen);
#if MAIL_DEBUG
         st=line+taglen;
         while(*st==' ') st++;
         Mail_debug("Mail_waittag tag=%ld code=%ld status=%.128s",tag,code,st);
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

/* Forward declarations so the SMTP AUTH helper can use the SMTP wire helpers
 * (defined further down) without re-ordering the whole file. */
static long Smtp_getresponse(struct Fetchdriver *fd,long sock,
   struct Library *SocketBase);
static long Smtp_sendcommand(struct Fetchdriver *fd,long sock,
   struct Library *SocketBase,UBYTE *fmt,...);

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

/* SMTP AUTH PLAIN. The previous implementation routed the command through
 * Mail_sendcmd / Mail_waittag, which prefix each command with an IMAP tag
 * ("A001 ...") and expect tagged response lines. SMTP servers reject those
 * as syntax errors. Use the SMTP-style helpers instead so the wire format
 * matches RFC 4954 (and the multi-line response handling in Smtp_getresponse
 * picks up the eventual 235/535 status correctly). */
static BOOL Mail_smtp_auth(struct Fetchdriver *fd,long sock,
   struct Library *SocketBase,UBYTE *user,UBYTE *pass)
{  UBYTE plain[384];
   UBYTE b64[520];
   long len;
   long ulen;
   long plen;
   ulen=strlen(user);
   plen=strlen(pass);
   len=1+ulen+1+plen;
   if(len>=(long)sizeof(plain)) return FALSE;
   plain[0]='\0';
   memcpy(plain+1,user,ulen);
   plain[1+ulen]='\0';
   memcpy(plain+1+ulen+1,pass,plen);
   Mail_base64encode(plain,len,b64,sizeof(b64));
   /* SMTP authentication uses 2xx for success, 5xx for permanent failures
    * and 4xx for transient failures. Any code >= 400 means we cannot send. */
   if(Smtp_sendcommand(fd,sock,SocketBase,"AUTH PLAIN %s",b64)>=400) return FALSE;
   return TRUE;
}

/* --- connection --- */

/* Cool-down ticks (50/sec) held while the lock is still owned, so the next
 * fetch task can't reconnect to the IMAP server too quickly. Gmail (and other
 * large providers) throttle rapid reconnects from the same IP: their first
 * defence is to send "* BYE" or drop the TCP connection mid-handshake (we
 * see "bad greeting: (null)"), their second is to accept the greeting and
 * then silently close the socket once AUTH PLAIN/LOGIN is sent. The second
 * pattern shows up in logs as Mail_waittag returning with no tagged status
 * line, followed by AUTH PLAIN failed / LOGIN failed and no server text.
 *
 * 150 ticks == 3 seconds. Tuned to be long enough to let Gmail forget rapid
 * activity in practice, short enough to feel responsive for back-to-back
 * clicks on the message list. Adjust if a quieter server is in use. */
#define MAIL_IMAP_COOLDOWN_TICKS 150

void Mailimap_close(struct Mailconn *mc)
{  BOOL session_was_used;
   long logout_tag;
   session_was_used=(mc->tag>0 && mc->ssl_active);
   if(mc->sock>=0)
   {  /* Send LOGOUT only on a session that has been used (tag>0 implies we
      * passed the greeting and sent at least one command). This is the
      * polite IMAP teardown; without it Gmail and other servers may treat
      * a quick reconnect as abusive and close the new TCP/TLS session
      * immediately, producing "Mailimap_open: bad greeting: (null)". On
      * half-open or failed sessions we skip LOGOUT to avoid blocking the
      * teardown waiting for a response that will never arrive. */
      if(session_was_used)
      {  logout_tag=mc->tag+1;
         if(Mail_sendcmd(mc,"LOGOUT"))
         {  Mailimap_drain_tag(mc,logout_tag);
         }
      }
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
   /* Cool-down between consecutive sessions on the same provider. Only after
    * a session that actually authenticated (session_was_used) -- aborted
    * opens (no greeting, immediate teardown) need no extra delay; the user
    * has already paid the cost of waiting for the failed connect. The Delay
    * is performed while we still hold Mail_imap_lock, so the next fetch
    * task waiting on the lock is automatically gated. */
   if(session_was_used && mc->imap_lock_held)
   {  Mail_debug("Mailimap_close: cool-down %ld ticks before unlock",
         (long)MAIL_IMAP_COOLDOWN_TICKS);
      Delay(MAIL_IMAP_COOLDOWN_TICKS);
   }
   if(mc->imap_lock_held)
   {  Mail_imap_unlock();
      mc->imap_lock_held=FALSE;
   }
}

BOOL Mailimap_open(struct Mailconn *mc,BOOL optional)
{  UBYTE imaphost[128];
   UBYTE plain[384];
   UBYTE b64[520];
   UBYTE quser[192];
   UBYTE qpass[192];
   long port;
   UBYTE *user;
   UBYTE *pass;
   long tag;
   UBYTE *line;
   long attempt;
   long len;
   long ulen;
   long plen;
   long rc;
   UBYTE *resp;
   BOOL greeted;
   BOOL authok;
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
   /* Retry the socketbase+connect+greeting up to twice. On the second pass we
    * Delay() a beat to let the server forget a recent abusive-reconnect, which
    * happens with Gmail when LOGOUT was not sent or after rapid open/close. */
   greeted=FALSE;
   for(attempt=0; attempt<2 && !greeted; attempt++)
   {
      if(attempt>0)
      {  Mail_debug("Mailimap_open: retry attempt %ld (after greeting failure)",attempt);
         /* Brief back-off; AmigaOS Delay() is in ticks (50/sec). */
         Delay(50);
      }
      /* Fresh socketbase on each attempt; the previous one is closed below. */
      Mail_debug("Mailimap_open: Opentcp (attempt %ld)",attempt);
      AwebTcpBase=Opentcp(&mc->socketbase,mc->fd,!optional);
      if(!mc->socketbase)
      {  Mail_debug("Mailimap_open: Opentcp failed (attempt %ld)",attempt);
         Mailimap_close(mc);
         return FALSE;
      }
      strncpy(mc->hostname,imaphost,sizeof(mc->hostname)-1);
      Updatetaskattrs(AOURL_Netstatus,NWS_LOOKUP,TAG_END);
      Tcpmessage(mc->fd,TCPMSG_LOOKUP,imaphost);
      Mail_debug("Mailimap_open: Lookup %s",imaphost);
      mc->hent=Lookup(imaphost,mc->socketbase);
      if(!mc->hent)
      {  Mail_debug("Mailimap_open: DNS lookup failed for %s",imaphost);
         if(!optional) Tcperror(mc->fd,TCPERR_NOHOST,imaphost);
         Mailimap_close(mc);
         return FALSE;
      }
      Mail_debug("Mailimap_open: a_socket");
      if((mc->sock=a_socket(mc->hent->h_addrtype,SOCK_STREAM,0,mc->socketbase))<0)
      {  Mail_debug("Mailimap_open: a_socket failed");
         Mailimap_close(mc);
         return FALSE;
      }
      Updatetaskattrs(AOURL_Netstatus,NWS_CONNECT,TAG_END);
      Tcpmessage(mc->fd,TCPMSG_CONNECT,"IMAP",mc->hent->h_name);
      Mail_debug("Mailimap_open: a_connect %s:%ld",mc->hent->h_name,port);
      if(a_connect(mc->sock,mc->hent,port,mc->socketbase))
      {  Mail_debug("Mailimap_open: TCP/SSL connect failed to %s",mc->hent->h_name);
         /* Connect failure is unlikely to be cured by a quick retry; bail. */
         if(!optional) Tcperror(mc->fd,TCPERR_NOCONNECT,mc->hent->h_name);
         Mailimap_close(mc);
         return FALSE;
      }
      mc->ssl_active=TRUE;
      Mail_debug("Mailimap_open: connected, waiting greeting");
      line=Mail_nextline(mc);
      if(line && !strncmp(line,"* OK",4))
      {  greeted=TRUE;
         break;
      }
      Mail_debug("Mailimap_open: bad greeting: %s (attempt %ld)",
         line?line:(UBYTE *)"(null)",attempt);
      /* Tear down this attempt's socket/socketbase without releasing the lock,
       * then loop to retry. We close the socket directly because Mailimap_close
       * also releases the lock and the LOGOUT it now sends would fail on a
       * half-broken session. */
      if(mc->sock>=0)
      {  a_close(mc->sock,mc->socketbase);
         mc->sock=-1;
      }
      if(mc->buffer)
      {  mc->length=0;
         mc->linelen=0;
      }
      if(mc->socketbase)
      {  a_cleanup(mc->socketbase);
         CloseLibrary(mc->socketbase);
         mc->socketbase=NULL;
      }
      mc->ssl_active=FALSE;
   }
   if(!greeted)
   {  Mail_debug("Mailimap_open: bad greeting after retries");
      Mailimap_close(mc);
      return FALSE;
   }
   if(Mail_getauth(mc->fd,&user,&pass))
   {  authok=FALSE;
      ulen=strlen(user);
      plen=strlen(pass);
      len=1+ulen+1+plen;
      /* Try AUTHENTICATE PLAIN first because IMAP servers that disable LOGIN
       * (LOGINDISABLED capability) typically still accept SASL PLAIN. If our
       * combined credentials fit, b64-encode user\0user\0pass and send. */
      if(len<(long)sizeof(plain))
      {  plain[0]='\0';
         memcpy(plain+1,user,ulen);
         plain[1+ulen]='\0';
         memcpy(plain+1+ulen+1,pass,plen);
         Mail_base64encode(plain,len,b64,sizeof(b64));
         tag=mc->tag+1;
         if(Mail_sendcmd(mc,"AUTHENTICATE PLAIN %s",b64))
         {  resp=NULL;
            rc=Mail_waittag(mc,tag,&resp);
            if(rc<400) authok=TRUE;
            else Mail_debug("Mailimap_open: AUTH PLAIN failed rc=%ld resp=%.160s",
               rc,
               resp ? (char *)resp : "(no response - socket dropped)");
         }
      }
      /* Fallback to plain LOGIN. Some legacy servers don't speak SASL PLAIN,
       * and some require LOGIN despite advertising AUTH=PLAIN. Quote each
       * field so user names with special characters survive intact. */
      if(!authok && Mail_imap_quote(user,quser,sizeof(quser))
      && Mail_imap_quote(pass,qpass,sizeof(qpass)))
      {  tag=mc->tag+1;
         if(Mail_sendcmd(mc,"LOGIN %s %s",quser,qpass))
         {  resp=NULL;
            rc=Mail_waittag(mc,tag,&resp);
            if(rc<400) authok=TRUE;
            else Mail_debug("Mailimap_open: LOGIN failed rc=%ld resp=%.160s",
               rc,
               resp ? (char *)resp : "(no response - socket dropped)");
         }
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
   long taglen;
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
   taglen=sprintf(tagbuf,"%s%03ld ",MAIL_TAG_PREFIX,tag);
   for(;;)
   {  if(Checktaskbreak()) break;
      line=Mail_nextline(mc);
      if(!line) break;
      if(!strncmp(line,tagbuf,(unsigned long)taglen)) break;
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
   if(!line || strncmp(line,tagbuf,(unsigned long)taglen))
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
   UBYTE qmbox[160];
   if(!mailbox || !*mailbox) mailbox=(UBYTE *)"INBOX";
   tag=mc->tag+1;
   Mail_debug("Mailimap_select: %s",mailbox);
   /* Quote the mailbox name so spaces, parentheses and other atom-specials in
    * server-provided folder names (e.g. "[Gmail]/Sent Mail") survive intact. */
   if(!Mail_imap_quote(mailbox,qmbox,sizeof(qmbox)))
   {  Mail_debug("Mailimap_select: mailbox name too long");
      return FALSE;
   }
   /* If Mail_imap_quote returned an unquoted atom, wrap in literal quotes so
    * the command is syntactically valid; the IMAP atom rules are a subset of
    * what we have but unconditional quoting keeps things uniform. */
   if(qmbox[0]!='"')
   {  if(!Mail_sendcmd(mc,"SELECT \"%s\"",qmbox)) return FALSE;
   }
   else
   {  if(!Mail_sendcmd(mc,"SELECT %s",qmbox)) return FALSE;
   }
   if(Mail_waittag(mc,tag,NULL)<400)
   {  Mail_debug("Mailimap_select: OK");
      return TRUE;
   }
   Mail_debug("Mailimap_select: failed");
   return FALSE;
}

long Mailimap_search_all(struct Mailconn *mc,unsigned long *uids,long maxuids)
{  long tag;
   long taglen;
   UBYTE *line;
   UBYTE tagbuf[16];
   UBYTE *p;
   long count;
   unsigned long n;
   BOOL saw_tag;
   if(maxuids<=0) return 0;
   tag=mc->tag+1;
   if(!Mail_sendcmd(mc,"UID SEARCH ALL")) return 0;
   count=0;
   saw_tag=FALSE;
   taglen=sprintf(tagbuf,"%s%03ld ",MAIL_TAG_PREFIX,tag);
   for(;;)
   {  if(Checktaskbreak()) break;
      line=Mail_nextline(mc);
      if(!line) break;
      if(!strncmp(line,tagbuf,(unsigned long)taglen)) { saw_tag=TRUE; break; }
      if(!strncmp(line,"* SEARCH",8))
      {  p=line+8;
         while(*p && count<maxuids)
         {  while(*p==' ') p++;
            if(!*p) break;
            n=0;
            /* IMAP UIDs are 32-bit unsigned; parse without overflow into a long
             * since we want all 32 bits available for sorting/display. */
            while(*p>='0' && *p<='9')
            {  n=n*10+(unsigned long)(*p-'0');
               p++;
            }
            if(n>0) uids[count++]=n;
         }
      }
   }
   /* Always resync. A SEARCH that returned no results still needs the tagged
    * status line consumed; otherwise the next command would read it instead. */
   if(!saw_tag) Mailimap_drain_tag(mc,tag);
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

/* Append a header value into target with whitespace folded to single spaces
 * and trailing whitespace trimmed (used after RFC 5322 unfolding). */
static void Mail_append_header_value(UBYTE *target,long targetmax,
   UBYTE *src,long srclen)
{  long i;
   long pos;
   UBYTE c;
   BOOL prev_space;
   if(!target || targetmax<=0) return;
   pos=0;
   prev_space=TRUE; /* swallow leading spaces */
   for(i=0;i<srclen && pos<targetmax-1;i++)
   {  c=src[i];
      if(c=='\t' || c==' ')
      {  if(!prev_space)
         {  target[pos++]=' ';
            prev_space=TRUE;
         }
      }
      else
      {  target[pos++]=c;
         prev_space=FALSE;
      }
   }
   /* Strip any trailing whitespace we may have just emitted. */
   while(pos>0 && target[pos-1]==' ') pos--;
   target[pos]='\0';
}

/* Parse header fields from a FETCH literal block. Handles RFC 5322 header
 * folding (continuation lines starting with WSP) and tolerates both CRLF and
 * bare LF line terminators. The previous parser had a broken CRLF advance
 * (compared (eol+1)>p, which is always false) and ignored folded headers, so
 * long Subject/From values from many real servers were truncated to the first
 * line and the trailing line break was left in the buffer. */
static void Mail_parse_header_literal(UBYTE *hdr,long hdrlen,
   UBYTE *subject,long subjmax,UBYTE *from,long frommax,
   UBYTE *date,long datemax,BOOL *unseen)
{  UBYTE *p;
   UBYTE *end;
   UBYTE *eol;
   UBYTE *next;
   UBYTE *target;
   long targetmax;
   long namelen;
   if(subject && subjmax>0) subject[0]='\0';
   if(from && frommax>0) from[0]='\0';
   if(date && datemax>0) date[0]='\0';
   if(!hdr || hdrlen<=0) return;
   p=hdr;
   end=hdr+hdrlen;
   while(p<end)
   {  /* Find end of this physical line. */
      eol=p;
      while(eol<end && *eol!='\r' && *eol!='\n') eol++;
      /* Compute start of the next physical line (skip CR, LF, or CRLF). */
      next=eol;
      if(next<end && *next=='\r') next++;
      if(next<end && *next=='\n') next++;
      /* Fold any continuation lines (start with SP/TAB) into this logical
       * header by extending eol forward, treating the CRLF + leading WSP as
       * a single space (per RFC 5322 section 2.2.3). */
      while(next<end && (*next==' ' || *next=='\t'))
      {  eol=next;
         while(eol<end && *eol!='\r' && *eol!='\n') eol++;
         next=eol;
         if(next<end && *next=='\r') next++;
         if(next<end && *next=='\n') next++;
      }
      /* Match the three headers we care about. Names are case-insensitive. */
      target=NULL;
      targetmax=0;
      namelen=0;
      if((eol-p)>=8 && STRNIEQUAL(p,"Subject:",8))
      {  target=subject; targetmax=subjmax; namelen=8;
      }
      else if((eol-p)>=5 && STRNIEQUAL(p,"From:",5))
      {  target=from; targetmax=frommax; namelen=5;
      }
      else if((eol-p)>=5 && STRNIEQUAL(p,"Date:",5))
      {  target=date; targetmax=datemax; namelen=5;
      }
      if(target && targetmax>0)
      {  Mail_append_header_value(target,targetmax,p+namelen,
            (eol-p)-namelen);
      }
      if(next<=p) break;       /* defensive: never loop on the same position */
      p=next;
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
      if(out && j+1<outmax) out[j++]=*p;
      p++;
   }
   if(*p=='"') p++;
   if(out) out[j]='\0';
   *pp=p;
   return TRUE;
}

/* Skip a single ENVELOPE field that may be NIL, "quoted", or {literal} (we
 * don't try to materialise literal-form values on the same line; the batch
 * HEADER.FIELDS path is used when those are needed). Returns FALSE if we
 * can't make sense of the input so the caller can stop parsing rather than
 * spin or lift garbage. */
static BOOL Mail_skip_envelope_value(UBYTE **pp,UBYTE *out,long outmax)
{  UBYTE *p;
   long lit;
   if(out && outmax>0) out[0]='\0';
   p=*pp;
   while(*p==' ') p++;
   if(!*p) { *pp=p; return FALSE; }
   if(STRNIEQUAL(p,"NIL",3) && (p[3]==' ' || p[3]==')' || p[3]=='\0'))
   {  p+=3;
      *pp=p;
      return TRUE;
   }
   if(*p=='"')
   {  return Mail_read_quoted(pp,out,outmax);
   }
   if(*p=='{')
   {  /* Literal continuation: the value is on the next IMAP line which we do
       * not have here. Skip the {n} token so we don't desync, but leave the
       * output empty. */
      p++;
      lit=0;
      while(*p>='0' && *p<='9') { lit=lit*10+(*p-'0'); p++; }
      if(*p=='}') p++;
      (void)lit;
      *pp=p;
      return FALSE;
   }
   return FALSE;
}

/* Pull a single address triple ("personal" "adl" "mailbox" "host") from an
 * ENVELOPE address list. Returns FALSE if the parser hits something it
 * doesn't recognise so we don't keep reading past the structure. */
static BOOL Mail_read_envelope_address(UBYTE **pp,
   UBYTE *name,long namemax,UBYTE *mb,long mbmax,UBYTE *host,long hostmax)
{  UBYTE *p;
   if(name && namemax>0) name[0]='\0';
   if(mb && mbmax>0) mb[0]='\0';
   if(host && hostmax>0) host[0]='\0';
   p=*pp;
   while(*p==' ') p++;
   if(*p!='(') return FALSE;
   p++;
   if(!Mail_skip_envelope_value(&p,name,namemax)) { *pp=p; return FALSE; }
   if(!Mail_skip_envelope_value(&p,NULL,0))       { *pp=p; return FALSE; }
   if(!Mail_skip_envelope_value(&p,mb,mbmax))     { *pp=p; return FALSE; }
   if(!Mail_skip_envelope_value(&p,host,hostmax)) { *pp=p; return FALSE; }
   while(*p==' ') p++;
   if(*p==')') p++;
   *pp=p;
   return TRUE;
}

/* ENVELOPE on one FETCH line (no {literal} blocks; safe for Dovecot batch
 * issues). The previous parser silently dropped responses whose fields used
 * NIL or {literal} and accepted only a hand-rolled quoted-string at fixed
 * offsets, which made the first address in a multi-address From: header
 * (mailing lists, MUAs that include groups) misparse. Walk the structure
 * generically and pick the first non-NIL field for display. */
static void Mail_parse_envelope_fetch(UBYTE *line,
   UBYTE *subject,long subjmax,UBYTE *from,long frommax,
   UBYTE *date,long datemax)
{  UBYTE *p;
   UBYTE name[96];
   UBYTE mb[64];
   UBYTE host[64];
   long ml;
   long hl;
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
   /* envelope = "(" env-date SP env-subject SP env-from SP env-sender SP ... */
   if(date && datemax>0) Mail_skip_envelope_value(&p,date,datemax);
   else                  Mail_skip_envelope_value(&p,NULL,0);
   if(subject && subjmax>0) Mail_skip_envelope_value(&p,subject,subjmax);
   else                     Mail_skip_envelope_value(&p,NULL,0);
   /* env-from is an address-list: "(" 1*address ")" or NIL */
   name[0]='\0'; mb[0]='\0'; host[0]='\0';
   while(*p==' ') p++;
   if(*p=='(')
   {  p++;
      /* Read the first address; stop scanning further addresses for the
       * display name (we just want a single line for the index). */
      Mail_read_envelope_address(&p,name,sizeof(name),mb,sizeof(mb),
         host,sizeof(host));
   }
   else if(STRNIEQUAL(p,"NIL",3))
   {  p+=3;
   }
   if(from && frommax>0)
   {  if(name[0])
      {  strncpy(from,name,frommax-1);
         from[frommax-1]='\0';
      }
      else if(mb[0] && host[0])
      {  /* Build "mailbox@host" so we don't display a bare local part for
          * automatic posts that omit the personal field. */
         ml=strlen(mb);
         hl=strlen(host);
         if(ml+1+hl<frommax)
         {  memcpy(from,mb,ml);
            from[ml]='@';
            memcpy(from+ml+1,host,hl);
            from[ml+1+hl]='\0';
         }
         else
         {  strncpy(from,mb,frommax-1);
            from[frommax-1]='\0';
         }
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

/* Single-UID envelope fetch. Kept for callers that have a single UID in hand;
 * Mailimap_fetch_envelopes is preferred for index listings because it cuts
 * round-trip count and uses BODY.PEEK[HEADER.FIELDS], which avoids the
 * cross-server inconsistencies in the literal layout of (ENVELOPE ...). */
long Mailimap_fetch_envelope(struct Mailconn *mc,unsigned long uid,
   UBYTE *subject,long subjmax,UBYTE *from,long frommax,
   UBYTE *date,long datemax,BOOL *unseen)
{  long tag;
   long taglen;
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
   if(!Mail_sendcmd(mc,"UID FETCH %lu (FLAGS ENVELOPE)",uid)) return 999;
   taglen=sprintf(tagbuf,"%s%03ld ",MAIL_TAG_PREFIX,tag);
   litbuf=NULL;
   litlen=0;
   for(;;)
   {  line=Mail_nextline(mc);
      if(!line) break;
      if(!strncmp(line,tagbuf,(unsigned long)taglen)) break;
      /* FLAGS section appears once per FETCH untagged reply. Pick the seen
       * state from that specific bracketed list, not anywhere on the line. */
      if(strstr(line,"FLAGS"))
      {  if(unseen)
         {  if(strstr(line,"\\Seen") || strstr(line,"\\seen")) *unseen=FALSE;
            else                                               *unseen=TRUE;
         }
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
      }
   }
   if(litbuf) { FREE(litbuf); litbuf=NULL; }
   if(!line || strncmp(line,tagbuf,(unsigned long)taglen))
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

/* One IMAP round-trip for many message headers (avoids per-UID desync/hang).
 * Chunked at MAIL_IMAP_FETCH_CHUNK UIDs per request so a very long pending UID
 * set never blows past the server's command line limit and the progress
 * callback can refresh the UI between chunks. Returns the number of items
 * that were populated successfully (0 on a complete failure). */
#define MAIL_IMAP_FETCH_CHUNK 25
long Mailimap_fetch_envelopes(struct Mailconn *mc,unsigned long *uids,long nuids,
   struct Mailuid *items,long itemmax,
   MailEnvelopeProgress progress,void *udata)
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
   unsigned long last_uid;
   long idx;
   long i;
   long done;
   long taglen;
   BOOL cancel;
   BOOL aborted;
   if(!uids || !items || nuids<=0 || itemmax<=0) return 0;
   if(nuids>itemmax) nuids=itemmax;
   for(i=0;i<nuids;i++)
   {  items[i].uid=uids[i];
      items[i].subject[0]='\0';
      items[i].from[0]='\0';
      items[i].date[0]='\0';
      items[i].unseen=TRUE;
   }
   chunk=MAIL_IMAP_FETCH_CHUNK;
   done=0;
   cancel=FALSE;
   aborted=FALSE;
   /* Initial progress callout so the caller can paint "0 of N" before the
    * first chunk's network round-trip. */
   if(progress) progress(udata,0,nuids,&cancel);
   if(cancel) return 0;
   for(start=0;start<nuids;start+=chunk)
   {  if(Checktaskbreak()) { aborted=TRUE; break; }
      n=nuids-start;
      if(n>chunk) n=chunk;
      if(!Mail_build_uid_set(uidset,sizeof(uidset),uids+start,n))
      {  Mail_debug("Mailimap_fetch_envelopes: uid set too long");
         aborted=TRUE;
         break;
      }
      tag=mc->tag+1;
      if(!Mail_sendcmd(mc,
         "UID FETCH %s (FLAGS BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])",
         uidset))
      {  aborted=TRUE;
         break;
      }
      taglen=sprintf(tagbuf,"%s%03ld ",MAIL_TAG_PREFIX,tag);
      litbuf=NULL;
      litlen=0;
      last_uid=0;
      for(;;)
      {  if(Checktaskbreak()) { aborted=TRUE; break; }
         line=Mail_nextline(mc);
         if(!line) { aborted=TRUE; break; }
         if(!strncmp(line,tagbuf,(unsigned long)taglen)) break;
         if(!strncmp(line,"* ",2))
         {  uid=Mail_fetch_line_uid(line);
            if(uid)
            {  last_uid=uid;
               idx=Mail_envelope_index(uids,nuids,uid);
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
               aborted=TRUE;
               break;
            }
         }
      }
      if(litbuf) { FREE(litbuf); litbuf=NULL; }
      if(aborted) break;
      if(!line || strncmp(line,tagbuf,(unsigned long)taglen))
      {  /* Did not see the tagged status (socket dropped or limit hit); drain
          * whatever is left so the next command has a clean stream. */
         Mailimap_drain_tag(mc,tag);
         aborted=TRUE;
         break;
      }
      done=start+n;
      if(progress)
      {  progress(udata,done,nuids,&cancel);
         if(cancel) { aborted=TRUE; break; }
      }
   }
   Mail_debug("Mailimap_fetch_envelopes: done=%ld nuids=%ld%s",
      done,nuids,aborted?" (aborted)":"");
   /* Return the number of items that received a server reply. Items past the
    * abort point still have UID set (caller may render them with empty cells
    * rather than dropping them entirely). */
   return done;
}

BOOL Mailimap_fetch_body(struct Mailconn *mc,unsigned long uid,
   UBYTE **body,long *bodylen)
{  long tag;
   long taglen;
   UBYTE *line;
   UBYTE tagbuf[16];
   long lit;
   UBYTE *buf;
   long len;
   long total;
   UBYTE *nb;
   UBYTE *grow;
   BOOL saw_tag;
   *body=NULL;
   *bodylen=0;
   tag=mc->tag+1;
   if(!Mail_sendcmd(mc,"UID FETCH %lu (BODY.PEEK[])",uid)) return FALSE;
   taglen=sprintf(tagbuf,"%s%03ld ",MAIL_TAG_PREFIX,tag);
   total=0;
   buf=NULL;
   saw_tag=FALSE;
   for(;;)
   {  line=Mail_nextline(mc);
      if(!line) break;
      if(!strncmp(line,tagbuf,(unsigned long)taglen)) { saw_tag=TRUE; break; }
      lit=Mail_literal_size(line);
      if(lit>=0)
      {  if(!Mail_readliteral(mc,lit,&nb,&len)) break;
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
   /* Keep the IMAP stream synchronised even on partial responses so the next
    * command after us doesn't see a stale untagged line. */
   if(!saw_tag) Mailimap_drain_tag(mc,tag);
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
   if(!Mail_sendcmd(mc,"UID STORE %lu +FLAGS (\\Seen)",uid)) return FALSE;
   if(Mail_waittag(mc,tag,NULL)<400) return TRUE;
   return FALSE;
}

/* --- SMTP --- */

/* SMTP responses are CRLF-terminated lines, each beginning with three digits.
 * Multi-line responses use '-' between the code and text on continuation lines
 * ("250-EXTENSION\r\n250 OK\r\n"). The final line has a space after the code.
 * a_recv() may return any number of bytes in any boundary, so the previous
 * "recv one buffer, atoi the start" approach was unreliable: it could split a
 * code mid-digit, miss continuation lines, or leave bytes from the next
 * response in the kernel queue. Read line-by-line into fd->block, scanning for
 * CRLF, and stop on a final-line ("ddd ") code. */
static long Smtp_getresponse(struct Fetchdriver *fd,long sock,struct Library *SocketBase)
{  long total;
   long room;
   long got;
   long n;
   long scanned;
   UBYTE *buf;
   UBYTE *line;
   UBYTE *eol;
   long code;
   long linecode;
   BOOL last;
   buf=fd->block;
   room=fd->blocksize-1;
   total=0;
   scanned=0;
   code=999;
   last=FALSE;
   while(!last)
   {  /* Try to consume any whole lines already in the buffer. */
      while(scanned<total)
      {  line=buf+scanned;
         eol=NULL;
         for(n=scanned;n<total;n++)
         {  if(buf[n]=='\n') { eol=buf+n; break; }
         }
         if(!eol) break;
         *eol='\0';
         if(eol>line && *(eol-1)=='\r') *(eol-1)='\0';
         if(line[0]>='0' && line[0]<='9'
         && line[1]>='0' && line[1]<='9'
         && line[2]>='0' && line[2]<='9')
         {  linecode=(line[0]-'0')*100+(line[1]-'0')*10+(line[2]-'0');
            code=linecode;
            /* Space after code indicates final line of the response. */
            if(line[3]==' ' || line[3]=='\0') last=TRUE;
         }
         else
         {  /* Malformed line: treat the whole response as a failure but keep
             * draining until a final line so the socket stays in sync. */
            code=999;
         }
         scanned=(eol-buf)+1;
         if(last) break;
      }
      if(last) break;
      if(total>=room)
      {  /* Buffer full without a terminator - shift consumed bytes off the
          * front so we can keep reading. If even one line doesn't fit, abort. */
         if(scanned==0) return 999;
         memmove(buf,buf+scanned,total-scanned);
         total-=scanned;
         scanned=0;
      }
      got=a_recv(sock,buf+total,room-total,0,SocketBase);
      if(got<=0) return 999;
      total+=got;
      buf[total]='\0';
   }
   return code;
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
   UBYTE *q;
   UBYTE *user;
   UBYTE *pass;
   UBYTE *rcpt_orig;
   UBYTE *rcpt_walk;
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
   {  if(!Mail_smtp_auth(fd,sock,SocketBase,user,pass))
      {  retval=SMR_FAIL;
         goto smtp_done;
      }
   }
   ObtainSemaphore(fd->prefssema);
   strncpy(name,fd->prefs->emailaddr,sizeof(name)-1);
   name[sizeof(name)-1]='\0';
   ReleaseSemaphore(fd->prefssema);
   Tcpmessage(fd,TCPMSG_MAILSEND);
   /* MAIL FROM must succeed (response <400); >=400 means the server rejected
    * the envelope sender. The original code had the comparison inverted, which
    * caused a successful MAIL FROM to short-circuit out with SMR_FAIL. */
   if(Smtp_sendcommand(fd,sock,SocketBase,"MAIL FROM:<%s>",name)>=400)
   {  retval=SMR_FAIL;
      goto smtp_done;
   }
   rcpt_orig=NULL;
   rcpt_walk=NULL;
   if(rcpt_to && *rcpt_to)
   {  rcpt_orig=Dupstr(rcpt_to,-1);
      if(rcpt_orig)
      {  rcpt_walk=rcpt_orig;
         q=NULL;
         while(rcpt_walk)
         {  q=strchr(rcpt_walk,',');
            if(q) *q++='\0';
            while(*rcpt_walk==' ') rcpt_walk++;
            if(*rcpt_walk
            && Smtp_sendcommand(fd,sock,SocketBase,"RCPT TO:<%s>",rcpt_walk)>=400)
            {  error=TRUE;
               break;
            }
            rcpt_walk=q;
         }
         /* Free the buffer Dupstr returned, not the walked pointer. */
         FREE(rcpt_orig);
         rcpt_orig=NULL;
      }
      else
      {  error=TRUE;
      }
   }
   if(!error)
   {  if(Smtp_sendcommand(fd,sock,SocketBase,"DATA")<400)
      {  a_send(sock,message,msglen,0,SocketBase);
         a_send(sock,(UBYTE *)"\r\n.\r\n",5,0,SocketBase);
         if(Smtp_getresponse(fd,sock,SocketBase)<400) retval=SMR_Ok;
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
