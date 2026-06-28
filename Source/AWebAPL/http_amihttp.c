/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2025-2026 amigazen project
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

/**********************************************************************
 *
 * http_amihttp.c - AWeb4 HTTP/HTTPS fetch via amihttp.library
 *
 * Replaces http.c when building the aweb4 smake target. TLS for HTTP
 * is handled inside amihttp; gemini/mail and other protocols still use
 * amissl.c via awebtcp.c.
 *
 * Opaque-handle rule: only Tier-0/1/2 LVOs, documented hook message
 * structs (HttpHookCertVerify, HttpHookPostStream), and URL helper LVOs
 * (HttpUriHostPart, HttpUriAuthorityPart, …).  Never walk
 * HttpTransactionRespHeaders() or read ParsedUrl / HttpHeader fields.
 *
 * Cookie jar: NewHttpCookieJarTags() with HTCJ_REQUEST_HOOK /
 * HTCJ_RESPONSE_HOOK delegates to Findcookies() / Storecookie() in cookie.c.
 * Response headers: HttpTransactionRespHeaderByIndex() for net monitor,
 * cache metadata, and Cache-Control merge.  
 *
 **********************************************************************/

#ifndef LOCALONLY

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/locale.h>
#include <proto/utility.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <libraries/amihttp.h>
#include <proto/amihttp.h>

#include "aweb.h"
#include "awebtcp.h"
#include "aweblog.h"
#include "awebssl.h"
#include "fetchdriver.h"
#include "form.h"
#include "httpcch.h"
#include "task.h"
#include "tcperr.h"
#include "url.h"

/* Match HTBT_DEFAULT_TIMEOUT (15) and legacy http.c SO_RCVTIMEO per operation. */
#define HTTP4_CONNECT_TIMEOUT  15UL
#define HTTP4_READ_TIMEOUT     15UL

/*
 * Library bases and socket swap state for #pragma libcall (proto/amihttp.h,
 * proto/bsdsocket.h) and amissl.c.  Classic builds define these in http.c;
 * aweb4 links http_amihttp.o instead.
 */
struct Library *HttpBase;
struct Library *SocketBase;
struct SignalSemaphore socketbase_swap_sema;
BOOL socketbase_swap_sema_initialized = FALSE;

#define HTTP4_MAX_REDIRECTS 10
#define HTTP4_HDRLINE_MAX   512

/* User-approved TLS certificates (shared with amissl.c cert prompts). */
struct Certaccept
{
   NODE(Certaccept);
   UBYTE *hostname;
   UBYTE *certname;
};

static LIST(Certaccept) certaccepts;
static struct SignalSemaphore certsema;

struct Http4MpostState
{
   struct Fetchdriver *fd;
   struct Multipartpart *part;
   BPTR fh;
   ULONG seg_off;
   ULONG seg_len;
};

struct Http4TxnCtx
{
   struct Fetchdriver *fd;
   UBYTE content_type[64];
   struct Http_cc_accum cc_accum;
   BOOL got_expires;
   BOOL cc_applied;
   ULONG serverdate;
};

static struct HttpCookieJar *Http4CookieJar;
static struct Hook Http4CertHook;
static struct Hook Http4CookieReqHook;
static struct Hook Http4CookieRespHook;
static struct Hook Http4MpostHook;
static struct Http4MpostState Http4MpostState;

/*
 * Retrieve subprocesses share the browser address space (CreateNewProc).
 * amihttp.library is opened once in Inithttp(); concurrent fetches rely on
 * amihttp internal socket/pool locking (ahb_SocketSema, ahb_PoolSema).
 *
 * Each Httptask opens its own Opentcp() handle and closes it on exit.  Do not
 * enable cross-Httptask keep-alive (HTSA_TASK_SERIAL / shared SocketBase):
 * pooled socket fds are tied to the bsdsocket handle that created them.
 */
static UBYTE Http4_tcp_block[256];
static struct Fetchdriver Http4_tcp_probe;

/* Route trace through AwebLog (requires InitAwebLog + httpdebug). */
static void
Http4_debug(const char *format, ...)
{
   va_list args;
   const char *fmt;

   if (!httpdebug) {
      return;
   }
   fmt = format;
   if (fmt && strncmp(fmt, "DEBUG: ", 7) == 0) {
      fmt += 7;
   }
   va_start(args, format);
   AwebLogV("http4", fmt, args);
   va_end(args);
}

/* HttpJoinUri and HttpUri*Part return amihttp pool memory; never Freemem those. */
static void
Http4_free_pool_mem(APTR p)
{
   struct TagItem tags[2];

   if (p == NULL || HttpBase == NULL) {
      return;
   }
   tags[0].ti_Tag = HTBT_FREE_POOL_MEM;
   tags[0].ti_Data = (ULONG)p;
   tags[1].ti_Tag = TAG_DONE;
   tags[1].ti_Data = 0;
   HttpBaseTagList(tags);
}

static UBYTE *Http4_useragent(void);
static BOOL Http4_ca_bundle_path(UBYTE *buf, ULONG bufsz);
static BOOL Http4_bootstrap_tcp_once(void);
static BOOL Http4_init_library(void);
static void Http4_shutdown_library(void);
static void Http4_status_hint(long code, UBYTE *extra);
static void Http4_apply_cc(struct Fetchdriver *fd, struct Http4TxnCtx *ctx);
static void Http4_emit_cipher(struct HttpTransaction *txn, BOOL ssl);
static void Http4_process_header(struct Fetchdriver *fd, struct Http4TxnCtx *ctx,
   STRPTR name, STRPTR value);
static void Http4_process_headers(struct Fetchdriver *fd, struct HttpTransaction *txn,
   struct Http4TxnCtx *ctx);
static BOOL Http4_add_auth_header(struct HttpTransaction *txn, struct Authorize *auth,
   BOOL proxy);
static BOOL Http4_configure_session(struct HttpSession *session, struct Fetchdriver *fd,
   UBYTE *url, struct Authorize *auth, struct Authorize *prxauth, BOOL use_proxy);
static BOOL Http4_configure_txn(struct HttpTransaction *txn, struct Fetchdriver *fd,
   UBYTE *url, struct Authorize *auth, struct Authorize *prxauth);
static BOOL Http4_read_body(struct Fetchdriver *fd, struct HttpTransaction *txn,
   struct Http4TxnCtx *ctx);
static void Http4_fill_content_type(struct Fetchdriver *fd, struct Http4TxnCtx *ctx);
static BOOL Http4_fetch_once(struct Fetchdriver *fd, struct HttpSession *session,
   UBYTE **url_inout, struct Authorize **auth_inout, struct Authorize **prxauth_inout,
   BOOL *use_proxy, long *status_out);
static ULONG Http4_cert_hook(struct Hook *hook, APTR obj, APTR msg);
static ULONG Http4_cookie_request_hook(struct Hook *hook, APTR obj, APTR msg);
static ULONG Http4_cookie_response_hook(struct Hook *hook, APTR obj, APTR msg);
static ULONG Http4_mpost_hook(struct Hook *hook, APTR obj, APTR msg);

#ifndef DEMOVERSION
static BOOL Http4_formwarn(void)
{
   return (BOOL)Syncrequest(AWEBSTR(MSG_FORMWARN_TITLE),
      haiku ? HAIKU16 : AWEBSTR(MSG_FORMWARN_WARNING),
      AWEBSTR(MSG_FORMWARN_BUTTONS), 0);
}
#endif

static UBYTE Http4DefaultUa[128];
static BOOL Http4DefaultUaInit;

static UBYTE *
Http4_useragent(void)
{
#ifndef DEMOVERSION
   ObtainSemaphore(&prefssema);
   if (prefs.network.spoofid && prefs.network.spoofid[0]) {
      static UBYTE buf[128];
      sprintf((char *)buf, "Mozilla/4.0 (compatible; %s)", prefs.network.spoofid);
      ReleaseSemaphore(&prefssema);
      return buf;
   }
   ReleaseSemaphore(&prefssema);
#endif
   if (!Http4DefaultUaInit) {
      /*
       * Modern Chrome-like UA for AWeb4; many sites (e.g. Google) serve a stub
       * page to legacy Mozilla/4.0.  Prefs spoof ID still overrides this.
       */
      sprintf((char *)Http4DefaultUa,
         "Mozilla/5.0 (compatible; AWeb/%s; %s) AppleWebKit/537.36 Chrome/120.0.0.0",
         awebversion, Awebosversion());
      Http4DefaultUaInit = TRUE;
   }
   return Http4DefaultUa;
}

/*
 * Optional PEM trust bundle for amihttp TLS (same role as AGet cacert.pem).
 * When absent, HTHK_CERT_VERIFY + Httpcertaccept() still allow user approval.
 */
static BOOL
Http4_ca_bundle_path(UBYTE *buf, ULONG bufsz)
{
   static const UBYTE *paths[] = {
      (UBYTE *)"AWeb:Certs/cacert.pem",
      (UBYTE *)"AWeb:Libs/cacert.pem",
      (UBYTE *)"Libs:amihttp/cacert.pem",
      NULL
   };
   const UBYTE **p;
   BPTR lock;

   if (buf == NULL || bufsz < 2) {
      return FALSE;
   }
   for (p = paths; *p != NULL; p++) {
      lock = Lock((STRPTR)*p, SHARED_LOCK);
      if (lock != 0) {
         UnLock(lock);
         strncpy((char *)buf, (char *)*p, (size_t)bufsz - 1);
         buf[bufsz - 1] = '\0';
         return TRUE;
      }
   }
   return FALSE;
}

/*
 * Ensure the TCP stack is running.  amihttp.library opens bsdsocket in
 * L_OpenLibs; do not call Opentcp/a_setup here — that second handle breaks
 * amihttp connects.  Probe only; optionally run starttcpcmd via Spawnsync.
 */
static BOOL
Http4_bootstrap_tcp_once(void)
{
   struct Library *probe;
#ifndef DEMOVERSION
   UBYTE *cmd;
   UBYTE *args;
#endif

   probe = OpenLibrary("bsdsocket.library", 4);
   if (probe != NULL) {
      CloseLibrary(probe);
      Http4_debug("bootstrap_tcp: stack up");
      return TRUE;
   }
#ifndef DEMOVERSION
   ObtainSemaphore(&tcpsema);
   cmd = NULL;
   args = NULL;
   ObtainSemaphore(&prefssema);
   if (prefs.network.starttcpcmd && prefs.network.starttcpcmd[0]) {
      cmd = Dupstr(prefs.network.starttcpcmd, -1);
      args = Dupstr(prefs.network.starttcpargs, -1);
   }
   ReleaseSemaphore(&prefssema);
   if (cmd != NULL) {
      Http4_tcp_probe.block = Http4_tcp_block;
      Http4_tcp_probe.blocksize = (long)sizeof(Http4_tcp_block);
      Tcpmessage(&Http4_tcp_probe, TCPMSG_TCPSTART);
      Spawnsync(cmd, args);
      FREE(cmd);
      if (args) {
         FREE(args);
      }
   }
   ReleaseSemaphore(&tcpsema);
   probe = OpenLibrary("bsdsocket.library", 4);
   if (probe != NULL) {
      CloseLibrary(probe);
      Http4_debug("bootstrap_tcp: stack started");
      return TRUE;
   }
#endif
   Http4_debug("bootstrap_tcp: no TCP stack");
   return FALSE;
}

/*
 * One-time amihttp setup in the browser main process.  Shared by all retrieve
 * subprocesses; HttpBaseTagList and the cookie jar must not be torn down per fetch.
 */
static BOOL
Http4_init_library(void)
{
   struct TagItem tags[10];
   static UBYTE ca_path[256];
   LONG n;

   if (HttpBase != NULL) {
      return TRUE;
   }
   HttpBase = OpenLibrary(AMIHTTPNAME, AMIHTTPVERSION);
   if (HttpBase == NULL) {
      Http4_debug("OpenLibrary(%s) failed at startup", AMIHTTPNAME);
      return FALSE;
   }
   Http4_debug("OpenLibrary(%s) ok base=%p", AMIHTTPNAME, HttpBase);
   n = 0;
   tags[n].ti_Tag = HTBT_BREAKMASK;
   tags[n].ti_Data = (ULONG)SIGBREAKF_CTRL_C;
   n++;
   tags[n].ti_Tag = HTBT_DEFAULT_TIMEOUT;
   tags[n].ti_Data = (ULONG)15;
   n++;
   tags[n].ti_Tag = HTBT_MAX_IDLE_CONNECTIONS;
   tags[n].ti_Data = (ULONG)0;
   n++;
   tags[n].ti_Tag = HTBT_IDLE_TIMEOUT;
   tags[n].ti_Data = (ULONG)15;
   n++;
   tags[n].ti_Tag = HTBT_SSL_VERIFY;
   tags[n].ti_Data = (ULONG)HTSSL_VERIFY_PEER;
   n++;
   if (Http4_ca_bundle_path(ca_path, (ULONG)sizeof(ca_path))) {
      tags[n].ti_Tag = HTBT_CA_BUNDLE_PATH;
      tags[n].ti_Data = (ULONG)ca_path;
      n++;
      Http4_debug("CA bundle: %s", (char *)ca_path);
   }
   tags[n].ti_Tag = TAG_DONE;
   tags[n].ti_Data = 0;
   n++;
   if (!HttpBaseTagList(tags)) {
      Http4_debug("HttpBaseTagList failed err=%ld", (long)HttpError());
      CloseLibrary(HttpBase);
      HttpBase = NULL;
      return FALSE;
   }
   Http4MpostHook.h_Entry = (HOOKFUNC)Http4_mpost_hook;
   Http4MpostHook.h_Data = (APTR)&Http4MpostState;
   Http4CookieReqHook.h_Entry = (HOOKFUNC)Http4_cookie_request_hook;
   Http4CookieRespHook.h_Entry = (HOOKFUNC)Http4_cookie_response_hook;
   Http4CookieJar = HttpCookieJarTags(
      HTCJ_REQUEST_HOOK, (ULONG)&Http4CookieReqHook,
      HTCJ_RESPONSE_HOOK, (ULONG)&Http4CookieRespHook,
      TAG_DONE);
   if (Http4CookieJar == NULL) {
      Http4_debug("HttpCookieJarTags failed err=%ld", (long)HttpError());
      CloseLibrary(HttpBase);
      HttpBase = NULL;
      return FALSE;
   }
   return TRUE;
}

static void
Http4_shutdown_library(void)
{
   if (Http4CookieJar != NULL) {
      DisposeHttpCookieJar(Http4CookieJar);
      Http4CookieJar = NULL;
   }
   if (HttpBase != NULL) {
      CloseLibrary(HttpBase);
      HttpBase = NULL;
   }
}

static void
Http4_status_hint(long code, UBYTE *extra)
{
   UBYTE line[STATUSBUFSIZE];
   UBYTE clipped[112];
   long n;

   if (code == 304) {
      strcpy((char *)line, "HTTP 304 Not Modified (using cached copy)");
      Updatetaskattrs(AOURL_Status, line, TAG_END);
      return;
   }
   if (!extra) {
      extra = (UBYTE *)"";
   }
   n = (long)strlen((char *)extra);
   if (n >= (long)sizeof(clipped)) {
      n = (long)sizeof(clipped) - 1;
   }
   strncpy((char *)clipped, (char *)extra, n);
   clipped[n] = '\0';
   if (code == 301 && clipped[0]) {
      sprintf((char *)line, "Redirect 301 to %s", clipped);
   } else if ((code == 302 || code == 307) && clipped[0]) {
      sprintf((char *)line, "Redirect %ld to %s", code, clipped);
   } else if (code == 303 && clipped[0]) {
      sprintf((char *)line, "Redirect 303 to %s", clipped);
   } else if (code == 401) {
      strcpy((char *)line, "HTTP 401 Authorization required");
   } else if (code == 407) {
      strcpy((char *)line, "HTTP 407 Proxy authorization required");
   } else if (code >= 400) {
      sprintf((char *)line, "HTTP error %ld", code);
   } else if (code != 200) {
      sprintf((char *)line, "HTTP %ld", code);
   } else {
      return;
   }
   Updatetaskattrs(AOURL_Status, line, TAG_END);
}

BOOL
Httpcertaccept(char *hostname, char *certname)
{
   char *def = "????";
   UBYTE *msg;
   UBYTE *msgbuf;
   UBYTE *h;
   UBYTE *c;
   struct Certaccept *ca;
   BOOL ok = FALSE;

   h = (UBYTE *)hostname;
   c = (UBYTE *)certname;
   if (!c) {
      c = (UBYTE *)def;
   }
   if (!h) {
      h = (UBYTE *)def;
   }
   ObtainSemaphore(&certsema);
   for (ca = certaccepts.first; ca->next; ca = ca->next) {
      if (STRIEQUAL(ca->hostname, hostname) && STREQUAL(ca->certname, c)) {
         ok = TRUE;
         break;
      }
   }
   if (!ok) {
      msg = AWEBSTR(MSG_SSLWARN_CERT_TEXT);
      if (msgbuf = ALLOCTYPE(UBYTE, strlen((char *)msg) + strlen((char *)h) + strlen((char *)c) + 8, 0)) {
         Lprintf(msgbuf, msg, h, c);
         ok = Syncrequest(AWEBSTR(MSG_SSLWARN_CERT_TITLE), haiku ? HAIKU13 : msgbuf,
            AWEBSTR(MSG_SSLWARN_CERT_BUTTONS), 0);
         FREE(msgbuf);
         if (hostname) {
            if (ok) {
               if (ca = ALLOCSTRUCT(Certaccept, 1, MEMF_PUBLIC | MEMF_CLEAR)) {
                  ca->hostname = Dupstr(hostname, -1);
                  ca->certname = Dupstr(c, -1);
                  ADDTAIL(&certaccepts, ca);
               }
            }
         }
      }
   }
   ReleaseSemaphore(&certsema);
   return ok;
}

static void
Http4_emit_cipher(struct HttpTransaction *txn, BOOL ssl)
{
   UBYTE cipher[128];
   LONG n;

#ifndef DEMOVERSION
   if (!ssl || txn == NULL) {
      return;
   }
   cipher[0] = '\0';
   n = HttpTransactionGetCipher(txn, (STRPTR)cipher, (ULONG)sizeof(cipher));
   if (n > 0 && cipher[0]) {
      Updatetaskattrs(AOURL_Cipher, cipher,
         AOURL_Ssllibrary, (UBYTE *)"amihttp",
         TAG_END);
   }
#endif
}

static void
Http4_apply_cc(struct Fetchdriver *fd, struct Http4TxnCtx *ctx)
{
   ULONG exp;
   ULONG nowtoday;

   if (ctx->cc_applied) {
      return;
   }
   ctx->cc_applied = TRUE;
   nowtoday = Today();
   if (ctx->cc_accum.saw_positive_max_age && ctx->cc_accum.min_max_age > 0) {
      exp = nowtoday + (ULONG)ctx->cc_accum.min_max_age;
      Updatetaskattrs(AOURL_Expires, exp, TAG_END);
   } else if (ctx->cc_accum.must_revalidate && !ctx->got_expires && ctx->serverdate) {
      Updatetaskattrs(AOURL_Expires, nowtoday, TAG_END);
   }
}

static void
Http4_process_header(struct Fetchdriver *fd, struct Http4TxnCtx *ctx,
   STRPTR name, STRPTR value)
{
   UBYTE line[HTTP4_HDRLINE_MAX];
   UBYTE *p;
   UBYTE *q;
   UBYTE *type_start;
   UBYTE *cs;
   UBYTE *cs_end;
   UBYTE mimetype[32];
   UBYTE charsetval[24];
   long l;
   long cs_len;
   BOOL have_charset;
   struct Http_cc_accum linecc;

   if (name == NULL || value == NULL) {
      return;
   }
   if ((long)strlen((char *)name) + (long)strlen((char *)value) + 4 >= HTTP4_HDRLINE_MAX) {
      return;
   }
   sprintf((char *)line, "%s: %s", name, value);
   Updatetaskattrs(AOURL_Header, line, TAG_END);

   if (STRNIEQUAL(name, "Date", 4) && name[4] == '\0') {
      ctx->serverdate = Scandate(value);
      fd->serverdate = ctx->serverdate;
      Updatetaskattrs(AOURL_Serverdate, ctx->serverdate, TAG_END);
   } else if (STRNIEQUAL(name, "Last-Modified", 13) && name[13] == '\0') {
      Updatetaskattrs(AOURL_Lastmodified, Scandate(value), TAG_END);
   } else if (STRNIEQUAL(name, "Expires", 7) && name[7] == '\0') {
      ctx->got_expires = TRUE;
      Updatetaskattrs(AOURL_Expires, Scandate(value), TAG_END);
   } else if (STRNIEQUAL(name, "Pragma", 6) && name[6] == '\0') {
      for (p = value; *p && isspace((int)(unsigned char)*p); p++) {
         ;
      }
      if (STRNIEQUAL(p, "no-cache", 8)) {
         Updatetaskattrs(AOURL_Nocache, TRUE, TAG_END);
      }
   } else if (STRNIEQUAL(name, "Cache-Control", 13) && name[13] == '\0') {
      Http_cc_parse((UBYTE *)value, &linecc);
      Http_cc_merge(&ctx->cc_accum, &linecc);
      if (linecc.forbid_disk) {
         Updatetaskattrs(AOURL_Nocache, TRUE, TAG_END);
      }
   } else if (STRNIEQUAL(name, "Content-Length", 14) && name[14] == '\0') {
      long clen;

      clen = 0;
      sscanf(value, " %ld", &clen);
      Updatetaskattrs(AOURL_Contentlength, clen, TAG_END);
   } else if (STRNIEQUAL(name, "Content-Type", 12) && name[12] == '\0') {
      /*
       * Parse type/subtype and optional charset without writing into value
       * (library-owned until DisposeHttpTransaction).  Older code reused p
       * for charset scanning and copied the charset into mimetype by mistake.
       */
      mimetype[0] = '\0';
      charsetval[0] = '\0';
      have_charset = FALSE;
      for (p = value; *p && isspace((int)(unsigned char)*p); p++) {
         ;
      }
      type_start = p;
      for (q = p; *q && !isspace((int)(unsigned char)*q) && *q != ';'; q++) {
         ;
      }
      l = (long)(q - type_start);
      if (l > 31) {
         l = 31;
      }
      if (l > 0) {
         strncpy((char *)mimetype, (char *)type_start, (size_t)l);
         mimetype[l] = '\0';
      }
      if (*q == ';' && STRNIEQUAL(type_start, "text/", 5)) {
         for (cs = q + 1; *cs != '\0'; cs++) {
            while (*cs != '\0' &&
               (*cs == ';' || isspace((int)(unsigned char)*cs))) {
               cs++;
            }
            if (*cs == '\0') {
               break;
            }
            if (!STRNIEQUAL(cs, "charset=", 8)) {
               continue;
            }
            cs += 8;
            while (*cs != '\0' && isspace((int)(unsigned char)*cs)) {
               cs++;
            }
            if (*cs == '"') {
               cs++;
               for (cs_end = cs; *cs_end != '\0' && *cs_end != '"'; cs_end++) {
                  ;
               }
               cs_len = (long)(cs_end - cs);
            } else {
               for (cs_end = cs; *cs_end != '\0' &&
                  !isspace((int)(unsigned char)*cs_end) && *cs_end != ';';
                  cs_end++) {
                  ;
               }
               cs_len = (long)(cs_end - cs);
            }
            if (cs_len > (long)sizeof(charsetval) - 1) {
               cs_len = (long)sizeof(charsetval) - 1;
            }
            if (cs_len > 0) {
               strncpy((char *)charsetval, (char *)cs, (size_t)cs_len);
               charsetval[cs_len] = '\0';
               have_charset = TRUE;
            }
            break;
         }
      }
      if (have_charset && charsetval[0]) {
         l = (long)strlen((char *)mimetype);
         if (l > 0 && l + 10 + (long)strlen((char *)charsetval) < (long)sizeof(mimetype)) {
            strcat((char *)mimetype, "; charset=");
            strcat((char *)mimetype, (char *)charsetval);
         }
      }
      strncpy((char *)ctx->content_type, (char *)mimetype, sizeof(ctx->content_type) - 1);
      ctx->content_type[sizeof(ctx->content_type) - 1] = '\0';
      if (!prefs.network.ignoremime && ctx->content_type[0]) {
         Updatetaskattrs(AOURL_Contenttype, ctx->content_type, TAG_END);
      }
   } else if (STRNIEQUAL(name, "ETag", 4) && name[4] == '\0') {
      for (p = value; *p && isspace((int)(unsigned char)*p); p++) {
         ;
      }
      for (q = p; *q && !isspace((int)(unsigned char)*q) && *q != ';'; q++) {
         ;
      }
      *q = '\0';
      if ((long)(q - p) > 63) {
         p[63] = '\0';
      }
      Updatetaskattrs(AOURL_Etag, p, TAG_END);
      if (fd->etag) {
         FREE(fd->etag);
      }
      fd->etag = Dupstr(p, -1);
   } else if (STRNIEQUAL(name, "Set-Cookie", 10) && name[10] == '\0') {
      /* Storecookie runs from HTCJ_RESPONSE_HOOK during Perform. */
   } else if (STRNIEQUAL(name, "Refresh", 7) && name[7] == '\0') {
      Updatetaskattrs(AOURL_Clientpull, value, TAG_END);
   }
}

static void
Http4_process_headers(struct Fetchdriver *fd, struct HttpTransaction *txn,
   struct Http4TxnCtx *ctx)
{
   ULONG i;
   STRPTR name;
   STRPTR value;

   for (i = 0; HttpTransactionRespHeaderByIndex(txn, i, &name, &value); i++) {
      Http4_process_header(fd, ctx, name, value);
   }
   Http4_apply_cc(fd, ctx);
}

static ULONG
Http4_cookie_request_hook(struct Hook *hook, APTR obj, APTR msg)
{
   struct HttpHookCookieRequest *req;
   UBYTE *cookies;
   UBYTE *val;
   UBYTE *p;
   UBYTE cookie_val[4096];

   (void)hook;
   (void)obj;
   req = (struct HttpHookCookieRequest *)msg;
   if (req == NULL) {
      return 0;
   }
   req->hcr_Value = NULL;
   if (!prefs.network.cookies) {
      return 0;
   }
   cookies = Findcookies(req->hcr_Url, req->hcr_Secure);
   if (cookies == NULL) {
      return 0;
   }
   val = cookies;
   if (STRNIEQUAL(cookies, "Cookie:", 7)) {
      val = cookies + 7;
      while (*val && isspace((int)(unsigned char)*val)) {
         val++;
      }
   }
   p = val + strlen((char *)val) - 1;
   while (p > val && (*p == '\r' || *p == '\n')) {
      *p = '\0';
      p--;
   }
   if (*val) {
      strncpy((char *)cookie_val, (char *)val, sizeof(cookie_val) - 1);
      cookie_val[sizeof(cookie_val) - 1] = '\0';
      req->hcr_Value = (STRPTR)cookie_val;
   }
   FREE(cookies);
   return (req->hcr_Value != NULL) ? 1UL : 0UL;
}

static ULONG
Http4_cookie_response_hook(struct Hook *hook, APTR obj, APTR msg)
{
   struct HttpHookCookieResponse *resp;
   ULONG sd;

   (void)hook;
   (void)obj;
   resp = (struct HttpHookCookieResponse *)msg;
   if (resp == NULL || resp->hcs_SetCookieLine == NULL) {
      return 0;
   }
   sd = 0;
   if (resp->hcs_DateHeader != NULL && resp->hcs_DateHeader[0] != '\0') {
      sd = Scandate(resp->hcs_DateHeader);
   }
   Storecookie(resp->hcs_Url, resp->hcs_SetCookieLine, sd);
   return 0;
}

static BOOL
Http4_add_auth_header(struct HttpTransaction *txn, struct Authorize *auth, BOOL proxy)
{
   UBYTE buf[256];

   if (auth == NULL || auth->cookie == NULL || auth->cookie[0] == '\0') {
      return TRUE;
   }
   sprintf((char *)buf, "Basic %s", auth->cookie);
   if (proxy) {
      return (BOOL)HttpTransactionAddHeader(txn, (STRPTR)"Proxy-Authorization",
         (STRPTR)buf);
   }
   return (BOOL)HttpTransactionAddHeader(txn, (STRPTR)"Authorization", (STRPTR)buf);
}

static BOOL
Http4_configure_session(struct HttpSession *session, struct Fetchdriver *fd,
   UBYTE *url, struct Authorize *auth, struct Authorize *prxauth, BOOL use_proxy)
{
   struct TagItem tags[12];
   UBYTE *ua;
   LONG n;
   BOOL rv;

   ua = Http4_useragent();
   n = 0;
   tags[n].ti_Tag = HTSA_USERAGENT;
   tags[n].ti_Data = (ULONG)ua;
   n++;
   tags[n].ti_Tag = HTSA_FOLLOW_REDIRECTS;
   tags[n].ti_Data = (ULONG)FALSE;
   n++;
   tags[n].ti_Tag = HTSA_MAX_REDIRECTS;
   tags[n].ti_Data = (ULONG)HTTP4_MAX_REDIRECTS;
   n++;
   tags[n].ti_Tag = HTSA_KEEPALIVE;
   /* One fetch per Httptask; socket handle closes on exit — no cross-task pool. */
   tags[n].ti_Data = (ULONG)FALSE;
   n++;
   tags[n].ti_Tag = HTSA_CONNECT_TIMEOUT;
   tags[n].ti_Data = (ULONG)HTTP4_CONNECT_TIMEOUT;
   n++;
   tags[n].ti_Tag = HTSA_READ_TIMEOUT;
   tags[n].ti_Data = (ULONG)HTTP4_READ_TIMEOUT;
   n++;
   /* Accept-Encoding: only when z.library is open (NewHttpSession default). */
   if (Http4CookieJar != NULL) {
      tags[n].ti_Tag = HTSA_COOKIE_JAR;
      tags[n].ti_Data = (ULONG)Http4CookieJar;
      n++;
   }
   tags[n].ti_Tag = TAG_END;
   tags[n].ti_Data = 0;
   n++;
   rv = (BOOL)SetHttpSessionAttrsA(session, tags);
   if (!rv) {
      return FALSE;
   }
   if (use_proxy && fd->proxy && fd->proxy[0]) {
      Http4_debug("session proxy=%s", fd->proxy);
      n = 0;
      tags[n].ti_Tag = HTSA_PROXY;
      tags[n].ti_Data = (ULONG)fd->proxy;
      n++;
      tags[n].ti_Tag = TAG_END;
      tags[n].ti_Data = 0;
      n++;
      SetHttpSessionAttrsA(session, tags);
   }
   SetHttpSessionHook(session, (ULONG)HTHK_CERT_VERIFY, &Http4CertHook);
   return TRUE;
}

static BOOL
Http4_configure_txn(struct HttpTransaction *txn, struct Fetchdriver *fd,
   UBYTE *url, struct Authorize *auth, struct Authorize *prxauth)
{
   struct TagItem tags[12];
   UBYTE datebuf[32];
   UBYTE ctype[96];
   BOOL ssl;
   LONG n;
   BOOL rv;

   ssl = BOOLVAL(fd->flags & FDVF_SSL);
   if (STRNIEQUAL(url, "https://", 8)) {
      ssl = TRUE;
   }
   n = 0;
   tags[n].ti_Tag = HTTA_URL;
   tags[n].ti_Data = (ULONG)url;
   n++;
   tags[n].ti_Tag = HTTA_USERAGENT;
   tags[n].ti_Data = (ULONG)Http4_useragent();
   n++;
   tags[n].ti_Tag = HTTA_RETRY_AUTH;
   tags[n].ti_Data = (ULONG)TRUE;
   n++;
   tags[n].ti_Tag = HTTA_RETRY_CERT_VERIFY;
   tags[n].ti_Data = (ULONG)TRUE;
   n++;
   tags[n].ti_Tag = TAG_END;
   tags[n].ti_Data = 0;
   n++;
   rv = (BOOL)SetHttpTransactionAttrsA(txn, tags);
   if (!rv) {
      return FALSE;
   }
   if (fd->referer && fd->referer[0]) {
      n = 0;
      tags[n].ti_Tag = HTTA_REFERER;
      tags[n].ti_Data = (ULONG)fd->referer;
      n++;
      tags[n].ti_Tag = TAG_END;
      tags[n].ti_Data = 0;
      n++;
      SetHttpTransactionAttrsA(txn, tags);
   }
   if (fd->flags & FDVF_NOCACHE) {
      n = 0;
      tags[n].ti_Tag = HTTA_NO_CACHE;
      tags[n].ti_Data = (ULONG)TRUE;
      n++;
      tags[n].ti_Tag = TAG_END;
      tags[n].ti_Data = 0;
      n++;
      SetHttpTransactionAttrsA(txn, tags);
   }
   if (fd->validate) {
      Makedate(fd->validate, datebuf);
      n = 0;
      tags[n].ti_Tag = HTTA_IF_MODIFIED_SINCE;
      tags[n].ti_Data = (ULONG)datebuf;
      n++;
      tags[n].ti_Tag = TAG_END;
      tags[n].ti_Data = 0;
      n++;
      SetHttpTransactionAttrsA(txn, tags);
   }
   if (fd->etag && fd->etag[0]) {
      n = 0;
      tags[n].ti_Tag = HTTA_IF_NONE_MATCH;
      tags[n].ti_Data = (ULONG)fd->etag;
      n++;
      tags[n].ti_Tag = TAG_END;
      tags[n].ti_Data = 0;
      n++;
      SetHttpTransactionAttrsA(txn, tags);
   }
   if (fd->postmsg) {
#ifndef DEMOVERSION
      if ((fd->flags & FDVF_FORMWARN) && !ssl && !Http4_formwarn()) {
         return FALSE;
      }
#endif
      n = 0;
      tags[n].ti_Tag = HTTA_METHOD;
      tags[n].ti_Data = (ULONG)"POST";
      n++;
      tags[n].ti_Tag = HTTA_POST_BODY;
      tags[n].ti_Data = (ULONG)fd->postmsg;
      n++;
      tags[n].ti_Tag = HTTA_POST_LENGTH;
      tags[n].ti_Data = (ULONG)strlen((char *)fd->postmsg);
      n++;
      tags[n].ti_Tag = TAG_END;
      tags[n].ti_Data = 0;
      n++;
      SetHttpTransactionAttrsA(txn, tags);
   } else if (fd->multipart) {
#ifndef DEMOVERSION
      if ((fd->flags & FDVF_FORMWARN) && !ssl && !Http4_formwarn()) {
         return FALSE;
      }
#endif
      sprintf((char *)ctype, "multipart/form-data; boundary=%.40s",
         fd->multipart->buf.buffer ? (char *)fd->multipart->buf.buffer : "");
      Http4MpostState.fd = fd;
      Http4MpostState.part = (struct Multipartpart *)fd->multipart->parts.first;
      Http4MpostState.fh = 0;
      Http4MpostState.seg_off = 0;
      Http4MpostState.seg_len = 0;
      n = 0;
      tags[n].ti_Tag = HTTA_METHOD;
      tags[n].ti_Data = (ULONG)"POST";
      n++;
      tags[n].ti_Tag = HTTA_POST_LENGTH;
      tags[n].ti_Data = (ULONG)fd->multipart->length;
      n++;
      tags[n].ti_Tag = HTTA_CONTENT_TYPE;
      tags[n].ti_Data = (ULONG)ctype;
      n++;
      tags[n].ti_Tag = HTTA_POST_STREAM_HOOK;
      tags[n].ti_Data = (ULONG)&Http4MpostHook;
      n++;
      tags[n].ti_Tag = TAG_END;
      tags[n].ti_Data = 0;
      n++;
      SetHttpTransactionAttrsA(txn, tags);
   }
   if (!Http4_add_auth_header(txn, auth, FALSE)) {
      return FALSE;
   }
   if (!Http4_add_auth_header(txn, prxauth, TRUE)) {
      return FALSE;
   }
   return TRUE;
}

static void
Http4_fill_content_type(struct Fetchdriver *fd, struct Http4TxnCtx *ctx)
{
   UBYTE *url;
   UBYTE *slash;
   UBYTE *dot;

   if (ctx == NULL || ctx->content_type[0] != '\0') {
      return;
   }
   if (fd == NULL || fd->name == NULL) {
      return;
   }
   url = fd->name;
   if (!STRNIEQUAL(url, "http://", 7) && !STRNIEQUAL(url, "https://", 8)) {
      return;
   }
   slash = (UBYTE *)strrchr((char *)url, '/');
   if (slash == NULL) {
      slash = url;
   }
   dot = (UBYTE *)strrchr((char *)slash, '.');
   if (dot != NULL && dot[1] != '\0' && strchr((char *)dot, '/') == NULL) {
      return;
   }
   strncpy((char *)ctx->content_type, "text/html", sizeof(ctx->content_type) - 1);
   ctx->content_type[sizeof(ctx->content_type) - 1] = '\0';
}

static BOOL
Http4_read_body(struct Fetchdriver *fd, struct HttpTransaction *txn,
   struct Http4TxnCtx *ctx)
{
   long n;
   long total;

   Http4_fill_content_type(fd, ctx);
   total = 0;
   Tcpmessage(fd, TCPMSG_WAITING,
      BOOLVAL(fd->flags & FDVF_SSL) ? (ULONG)"HTTPS" : (ULONG)"HTTP");
   for (;;) {
      if (Checktaskbreak()) {
         return FALSE;
      }
      n = HttpTransactionReadBody(txn, fd->block, (ULONG)fd->blocksize);
      if (n < 0) {
         Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
         return FALSE;
      }
      if (n == 0) {
         break;
      }
      total += n;
      if (ctx->content_type[0]) {
         Updatetaskattrs(
            AOURL_Data, fd->block,
            AOURL_Datalength, n,
            AOURL_Contenttype, ctx->content_type,
            TAG_END);
      } else {
         Updatetaskattrs(
            AOURL_Data, fd->block,
            AOURL_Datalength, n,
            TAG_END);
      }
   }
   return TRUE;
}

static ULONG
Http4_cert_hook(struct Hook *hook, APTR obj, APTR msg)
{
   struct HttpHookCertVerify *cv;
   struct HttpSslPeerCert cert;
   STRPTR cn;
   STRPTR host;
   BOOL ok;

   cv = (struct HttpHookCertVerify *)msg;
   cn = NULL;
   host = NULL;
   ok = FALSE;
   if (hook != NULL && hook->h_Data != NULL) {
      host = HttpUriHostPart((STRPTR)hook->h_Data);
   }
   if (cv != NULL && cv->hcv_Transaction != NULL) {
      if (HttpTransactionGetPeerCert(cv->hcv_Transaction, &cert)) {
         cn = cert.hpc_CommonName;
         ok = Httpcertaccept((char *)host, cn);
         HttpPeerCertFree(&cert);
      }
   }
   if (ok) {
      if (cv != NULL) {
         cv->hcv_Accept = TRUE;
      }
      return 1;
   }
   return 0;
}

static ULONG
Http4_mpost_hook(struct Hook *hook, APTR obj, APTR msg)
{
   struct HttpHookPostStream *ps;
   struct Http4MpostState *st;
   struct Multipartpart *mpp;
   ULONG got;
   long n;
   UBYTE *src;

   ps = (struct HttpHookPostStream *)msg;
   st = (struct Http4MpostState *)(hook ? hook->h_Data : NULL);
   if (ps == NULL || st == NULL || st->fd == NULL || ps->hps_Buffer == NULL) {
      return 0;
   }
   got = 0;
   while (got < ps->hps_MaxLen) {
      if (st->seg_len > 0) {
         n = (long)(ps->hps_MaxLen - got);
         if ((ULONG)n > st->seg_len) {
            n = (long)st->seg_len;
         }
         src = st->fd->multipart->buf.buffer + st->seg_off;
         memcpy((UBYTE *)ps->hps_Buffer + got, src, (size_t)n);
         got += (ULONG)n;
         st->seg_off += (ULONG)n;
         st->seg_len -= (ULONG)n;
         continue;
      }
      if (st->fh != 0) {
         n = Read(st->fh, (UBYTE *)ps->hps_Buffer + got, (long)(ps->hps_MaxLen - got));
         if (n <= 0) {
            Close(st->fh);
            st->fh = 0;
         } else {
            got += (ULONG)n;
         }
         continue;
      }
      mpp = st->part;
      if (mpp == NULL || mpp->next == NULL) {
         break;
      }
      st->part = (struct Multipartpart *)mpp->next;
      if (mpp->lock) {
         st->fh = OpenFromLock(DupLock(mpp->lock));
         if (st->fh == 0) {
            return 0;
         }
         Updatetaskattrs(AOURL_Netstatus, NWS_UPLOAD, TAG_END);
         Tcpmessage(st->fd, TCPMSG_UPLOAD);
      } else if (mpp->length > 0) {
         st->seg_off = (ULONG)mpp->start;
         st->seg_len = (ULONG)mpp->length;
      }
   }
   return got;
}

static BOOL
Http4_fetch_once(struct Fetchdriver *fd, struct HttpSession *session,
   UBYTE **url_inout, struct Authorize **auth_inout, struct Authorize **prxauth_inout,
   BOOL *use_proxy, long *status_out)
{
   struct HttpTransaction *txn;
   struct Http4TxnCtx ctx;
   STRPTR loc;
   STRPTR host;
   STRPTR authority;
   long status;
   long err;
   BOOL rv;
   BOOL ssl;

   if (session == NULL) {
      return FALSE;
   }
   txn = NULL;
   host = NULL;
   authority = NULL;
   loc = NULL;
   status = 0;
   ssl = BOOLVAL(fd->flags & FDVF_SSL);
   if (STRNIEQUAL(*url_inout, "https://", 8)) {
      ssl = TRUE;
   }
   memset(&ctx, 0, sizeof(ctx));
   Http_cc_reset_accum(&ctx.cc_accum);

   host = HttpUriHostPart((STRPTR)*url_inout);
   if (host == NULL || host[0] == '\0') {
      Tcperror(fd, TCPERR_XAWEB, *url_inout);
      return FALSE;
   }
   authority = HttpUriAuthorityPart((STRPTR)*url_inout);
   if (*auth_inout == NULL && !prefs.network.limitproxy && authority != NULL) {
      *auth_inout = Guessauthorize((UBYTE *)authority);
   }
   if (*use_proxy && fd->proxy && !prefs.network.limitproxy && *prxauth_inout == NULL) {
      *prxauth_inout = Guessauthorize(fd->proxy);
   }

   Http4CertHook.h_Entry = (HOOKFUNC)Http4_cert_hook;
   Http4CertHook.h_Data = (APTR)*url_inout;

   Http4_debug("fetch_once: url=%s proxy=%s ssl=%d",
      *url_inout ? (char *)*url_inout : "(null)",
      (fd->proxy && fd->proxy[0]) ? (char *)fd->proxy : "(none)",
      ssl ? 1 : 0);

   txn = NewHttpTransaction(session);
   if (txn == NULL) {
      Http4_debug("NewHttpTransaction failed, HttpError=%ld", (long)HttpError());
      Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
      return FALSE;
   }
   if (!Http4_configure_txn(txn, fd, *url_inout, *auth_inout, *prxauth_inout)) {
      Http4_debug("Http4_configure_txn failed (form warn or header)");
      DisposeHttpTransaction(txn);
      return FALSE;
   }

   Updatetaskattrs(AOURL_Netstatus, NWS_LOOKUP, TAG_END);
   Tcpmessage(fd, TCPMSG_LOOKUP, (UBYTE *)host);
   Updatetaskattrs(AOURL_Netstatus, NWS_CONNECT, TAG_END);
   Tcpmessage(fd, TCPMSG_CONNECT,
      ssl ? (ULONG)"HTTPS" : (ULONG)"HTTP", (ULONG)host);
   rv = HttpTransactionPerform(txn);
   status = HttpTransactionGetStatusCode(txn);
   err = HttpTransactionGetLastError(txn);
   *status_out = status;

   Http4_debug("Perform rv=%ld status=%ld err=%ld liberr=%ld (%s)",
      (long)rv, status, err, HttpError(),
      HttpGetErrorString(err) ? (char *)HttpGetErrorString(err) : "");

   if (!rv && status == 0) {
      if (err == ERROR_HTTP_SSL_VERIFY) {
         Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
      } else if (err == ERROR_HTTP_DNS_FAILED) {
         Tcperror(fd, TCPERR_NOHOST, (UBYTE *)host);
      } else if (err == ERROR_HTTP_CONNECT_TIMEOUT) {
         Tcperror(fd, TCPERR_NOCONNECT_TIMEOUT, (UBYTE *)host);
      } else if (err == ERROR_HTTP_CONNECT_FAILED || err == ERROR_HTTP_PROXY_AUTH) {
         Tcperror(fd, TCPERR_NOCONNECT, (UBYTE *)host);
      } else {
         Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
      }
      DisposeHttpTransaction(txn);
      return FALSE;
   }

   Http4_emit_cipher(txn, ssl);
   Http4_process_headers(fd, txn, &ctx);

   if (status == 401 && *auth_inout != NULL) {
      if (!(*auth_inout)->cookie) {
         Authorize(fd, *auth_inout, FALSE);
      }
      if ((*auth_inout)->cookie) {
         DisposeHttpTransaction(txn);
         return TRUE;
      }
      Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
   } else if (status == 407 && *prxauth_inout != NULL) {
      if (!(*prxauth_inout)->cookie) {
         Authorize(fd, *prxauth_inout, TRUE);
      }
      if ((*prxauth_inout)->cookie) {
         DisposeHttpTransaction(txn);
         return TRUE;
      }
      Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
   } else if (status >= 300 && status < 400) {
      loc = HttpTransactionGetRedirectLocation(txn);
      if (loc == NULL) {
         loc = HttpTransactionRespHeader(txn, (STRPTR)"Location");
      }
      Http4_status_hint(status, (UBYTE *)loc);
      if (loc != NULL && loc[0] != '\0') {
         if (status == 301) {
            Updatetaskattrs(AOURL_Movedto, loc, TAG_END);
         } else if (status == 303) {
            Updatetaskattrs(AOURL_Seeother, loc, TAG_END);
         } else {
            Updatetaskattrs(AOURL_Tempmovedto, loc, TAG_END);
         }
         DisposeHttpTransaction(txn);
         return TRUE;
      }
      Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
      DisposeHttpTransaction(txn);
      return FALSE;
   } else if (status == 304) {
      Http4_status_hint(304, NULL);
      Updatetaskattrs(AOURL_Notmodified, TRUE, TAG_END);
   } else if (status >= 200 && status < 300) {
      if (!Http4_read_body(fd, txn, &ctx)) {
         DisposeHttpTransaction(txn);
         return FALSE;
      }
   } else if (status == 401 || status == 407) {
      Http4_status_hint(status, NULL);
      Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
   } else if ((status == 405 || status == 500 || status == 501) && fd->postmsg) {
      Http4_status_hint(status, NULL);
      Updatetaskattrs(AOURL_Postnogood, TRUE, TAG_END);
   } else if (status >= 400) {
      Http4_status_hint(status, NULL);
      Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
   } else if (status != 0) {
      Http4_status_hint(status, NULL);
   }

   DisposeHttpTransaction(txn);
   return TRUE;
}

void
Httptask(struct Fetchdriver *fd)
{
   UBYTE *url;
   struct HttpSession *session;
   struct Authorize *auth;
   struct Authorize *prxauth;
   struct Library *task_sock;
   struct TagItem sock_tags[4];
   long status;
   BOOL use_proxy;
   BOOL again;
   BOOL limitproxy_reset;

   task_sock = NULL;
   session = NULL;

   if (fd == NULL || fd->name == NULL) {
      Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
      Updatetaskattrs(AOTSK_Async, TRUE, AOURL_Eof, TRUE, AOURL_Terminate, TRUE, TAG_END);
      return;
   }

   Http4_debug("Httptask: ENTRY url=%s proxy=%s ssl=%d task=%p",
      fd->name ? (char *)fd->name : "(null)",
      (fd->proxy && fd->proxy[0]) ? (char *)fd->proxy : "(none)",
      BOOLVAL(fd->flags & FDVF_SSL) ? 1 : 0,
      FindTask(NULL));

   if (HttpBase == NULL) {
      Http4_debug("Httptask: amihttp not initialized");
      Tcperror(fd, TCPERR_NOLIB);
      Updatetaskattrs(AOTSK_Async, TRUE,
         AOURL_Eof, TRUE,
         AOURL_Terminate, TRUE,
         TAG_END);
      return;
   }

   /* Per-task TCP stack (same as classic http.c Openlibraries). */
   if (Opentcp(&task_sock, fd, TRUE) == NULL) {
      Http4_debug("Httptask: Opentcp failed");
      Tcperror(fd, TCPERR_NOLIB);
      Updatetaskattrs(AOTSK_Async, TRUE,
         AOURL_Eof, TRUE,
         AOURL_Terminate, TRUE,
         TAG_END);
      return;
   }
   SocketBase = task_sock;
   sock_tags[0].ti_Tag = HTBT_TASK_SOCKETBASE;
   sock_tags[0].ti_Data = (ULONG)task_sock;
   sock_tags[1].ti_Tag = TAG_DONE;
   if (!HttpBaseTagList(sock_tags)) {
      Http4_debug("Httptask: HTBT_TASK_SOCKETBASE failed err=%ld", (long)HttpError());
      a_cleanup(task_sock);
      CloseLibrary(task_sock);
      Tcperror(fd, TCPERR_NOLIB);
      Updatetaskattrs(AOTSK_Async, TRUE,
         AOURL_Eof, TRUE,
         AOURL_Terminate, TRUE,
         TAG_END);
      return;
   }
   SetHttpError(0);

   url = Dupstr(fd->name, -1);
   auth = NULL;
   prxauth = NULL;
   use_proxy = (BOOL)(fd->proxy != NULL && fd->proxy[0] != '\0');
   limitproxy_reset = FALSE;

   if (url == NULL) {
      Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
   } else {
      session = NewHttpSession();
      if (session == NULL) {
         Http4_debug("Httptask: NewHttpSession failed err=%ld", (long)HttpError());
         Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
      } else if (!Http4_configure_session(session, fd, url, auth, prxauth, use_proxy)) {
         Http4_debug("Httptask: Http4_configure_session failed");
         Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
         DisposeHttpSession(session);
         session = NULL;
      } else {
         for (;;) {
            if (Checktaskbreak()) {
               break;
            }
            if (use_proxy && auth && prefs.network.limitproxy && !limitproxy_reset) {
               use_proxy = FALSE;
               limitproxy_reset = TRUE;
               if (!Http4_configure_session(session, fd, url, auth, prxauth, use_proxy)) {
                  Http4_debug("Httptask: reconfigure session (no proxy) failed");
                  break;
               }
            }
            again = FALSE;
            status = 0;
            if (!Http4_fetch_once(fd, session, &url, &auth, &prxauth, &use_proxy, &status)) {
               Http4_debug("Httptask: fetch_once failed status=%ld", status);
               break;
            }
            if (status == 401 && auth && auth->cookie) {
               again = TRUE;
            } else if (status == 407 && prxauth && prxauth->cookie) {
               again = TRUE;
            } else {
               break;
            }
            if (!again) {
               break;
            }
         }
         DisposeHttpSession(session);
         session = NULL;
      }
   }

   if (auth) {
      Freeauthorize(auth);
   }
   if (prxauth) {
      Freeauthorize(prxauth);
   }

   /* Release per-task TLS and TCP before this subprocess exits. */
   sock_tags[0].ti_Tag = HTBT_TASK_SSL_RELEASE;
   sock_tags[0].ti_Data = 0;
   sock_tags[1].ti_Tag = HTBT_TASK_SOCKET_RELEASE;
   sock_tags[1].ti_Data = 0;
   sock_tags[2].ti_Tag = TAG_DONE;
   HttpBaseTagList(sock_tags);
   if (task_sock != NULL) {
      a_cleanup(task_sock);
      CloseLibrary(task_sock);
      task_sock = NULL;
   }

   if (url) {
      FREE(url);
   }

   Http4_debug("Httptask: EXIT url=%s task=%p",
      fd->name ? (char *)fd->name : "(null)", FindTask(NULL));

   Updatetaskattrs(AOTSK_Async, TRUE,
      AOURL_Eof, TRUE,
      AOURL_Terminate, TRUE,
      TAG_END);
}

BOOL
Inithttp(void)
{
   InitAwebLog();
   InitSemaphore(&certsema);
   NEWLIST(&certaccepts);
   InitSemaphore(&socketbase_swap_sema);
   socketbase_swap_sema_initialized = TRUE;
   if (!Http4_bootstrap_tcp_once()) {
      return FALSE;
   }
   if (!Http4_init_library()) {
      return FALSE;
   }
   Http4_debug("Inithttp: %s ready base=%p id=%s", AMIHTTPNAME, HttpBase,
      HttpBase && HttpBase->lib_IdString ? (char *)HttpBase->lib_IdString : "?");
   return TRUE;
}

void
Freehttp(void)
{
   struct Certaccept *ca;

   if (certaccepts.first) {
      while (ca = REMHEAD(&certaccepts)) {
         if (ca->hostname) {
            FREE(ca->hostname);
         }
         if (ca->certname) {
            FREE(ca->certname);
         }
         FREE(ca);
      }
   }
   Http4_shutdown_library();
}

void
CloseIdleKeepAliveConnections(void)
{
   struct TagItem tags[2];

   if (HttpBase == NULL) {
      return;
   }
   tags[0].ti_Tag = HTBT_POOL_FLUSH;
   tags[0].ti_Data = 0;
   tags[1].ti_Tag = TAG_DONE;
   HttpBaseTagList(tags);
}

#else /* LOCALONLY */

#include "aweb.h"

BOOL
Inithttp(void)
{
   return 1;
}

void
Freehttp(void)
{
}

void
CloseIdleKeepAliveConnections(void)
{
}

void
Httptask(struct Fetchdriver *fd)
{
}

#endif /* LOCALONLY */
