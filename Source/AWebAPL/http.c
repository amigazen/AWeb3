/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Original Copyright (C) 2002 Yvon Rozijn
 * Rewrite Copyright (C) 2025 amigazen project
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

/* http.c - aweb http protocol client */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/filio.h>  /* For FIONBIO */
#include <sys/errno.h>  /* For errno and error codes */
#include <netdb.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/socket.h>
#include <proto/bsdsocket.h>
#include <proto/timer.h>  /* For GetSysTime() */
#include <libraries/bsdsocket.h>  /* For SBTC_ERRNO and SocketBaseTags() */
#include "aweb.h"
#include "tcperr.h"
#include "fetchdriver.h"
#include "application.h"
#include "task.h"
#include "form.h"
#include "awebtcp.h"
#include "awebssl.h"
#include <dos/dosextens.h>
#include <proto/amisslmaster.h>

#include "/zlib/zconf.h"
#include "/zlib/zlib.h"

/* Socket option constants if not already defined */
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 0x1006
#endif
#ifndef SO_SNDTIMEO
#define SO_SNDTIMEO 0x1005
#endif

/* Define SocketBase for setsockopt() from proto/bsdsocket.h */
/* The proto header declares it as extern, so we need to provide the actual definition */
struct Library *SocketBase;

/* Shared debug logging semaphore - defined here, used by http.c and amissl.c */
/* This must be non-static so it can be shared between compilation units */
struct SignalSemaphore debug_log_sema;
BOOL debug_log_sema_initialized = FALSE;

#ifndef LOCALONLY

#include <libraries/locale.h>

struct Httpinfo
{  long status;               /* Response status */
   USHORT flags;
   struct Authorize *prxauth; /* Proxy authorization */
   struct Authorize *auth;    /* Normal authorization */
   UBYTE *connect;            /* Connect to this host or proxy */
   long port;                 /* .. using this port. -1 means use default (80/443) */
   UBYTE *tunnel;             /* Host and port to tunnel to */
   UBYTE *hostport;           /* Host and port for use in Host: header */
   UBYTE *hostname;           /* Host name to match authorization for */
   UBYTE *abspath;            /* Abs path, or full url, to use in GET request */
   UBYTE *boundary;           /* Multipart boundary including leading "--" */
   struct Fetchdriver *fd;
   struct Library *socketbase;
   long sock;
   struct Assl *assl;         /* AwebSSL context */
   long blocklength;          /* Length of data in block */
   long nextscanpos;          /* Block position to scan */
   long linelength;           /* Length of current header line */
   long readheaders;          /* Number of header bytes read */
   ULONG movedto;             /* AOURL_ tag if 301 302 303 307 status */
   UBYTE *movedtourl;         /* URL string moved to */
   UBYTE parttype[32];        /* Content-type for this part */
   long partlength;           /* Content-length for this part */
   UBYTE *userid;             /* Userid from URL */
   UBYTE *passwd;             /* Password from URL */
   BOOL connection_reused;   /* TRUE if connection was reused from pool */
   long bytes_received;      /* Bytes received so far (for Range request retry) */
   BOOL server_supports_range; /* TRUE if server supports Range requests (Accept-Ranges: bytes) */
   long full_file_size;      /* Full file size from Content-Range header (for 206 responses) */
};

#define HTTPIF_AUTH        0x0001   /* Tried with a known to be valid auth */
#define HTTPIF_PRXAUTH     0x0002   /* Tried with a known to be valid prxauth */
#define HTTPIF_HEADERS     0x0004   /* Doing headers, issue bytes read messages */
#define HTTPIF_SSL         0x0008   /* Use secure transfer */
#define HTTPIF_RETRYNOSSL  0x0010   /* Retry with no secure transfer */
#define HTTPIF_NOSSLREQ    0x0020   /* Don't put on another SSL requester */
#define HTTPIF_SSLTUNNEL   0x0040   /* Tunnel SSL request through proxy */
#define HTTPIF_KEEPALIVE   0x1000   /* Connection supports keep-alive (server indicated) */
#define HTTPIF_KEEPALIVE_REQ 0x2000 /* Client requested keep-alive */
#define HTTPIF_TUNNELOK    0x0080   /* Tunnel response was ok */
#define HTTPIF_GZIPENCODED 0x0100   /* response is gzip encoded */
#define HTTPIF_GZIPDECODING 0x0200  /* decoding gziped response has begun */
#define HTTPIF_DATA_PROCESSED 0x0400 /* data has already been processed to prevent duplication */
#define HTTPIF_CHUNKED 0x0800        /* response uses chunked transfer encoding */
#define HTTPIF_RANGE_REQUEST 0x4000  /* Using Range request to resume partial download */

static UBYTE *httprequest="GET %.7000s HTTP/1.1\r\n";

static UBYTE *httppostrequest="POST %.7000s HTTP/1.1\r\n";

static UBYTE *useragent="User-Agent: Mozilla/3.0 (compatible; Amiga-AWeb/3.6; AmigaOS 3.2)\r\n";

#ifndef DEMOVERSION
/* Full replacement User-Agent from prefs (chooser supplies a complete token, no suffix). */
static UBYTE *useragentspoof="User-Agent: %s\r\n";
#endif

/* Browser-like negotiation: single-wildcard Accept with q=1 only is a common bot and WAF signal; gzip kept for bandwidth. */
static UBYTE *fixedheaders=
   "Accept: text/html, application/xhtml+xml, application/xml;q=0.9, image/png, image/*;q=0.8, */*;q=0.5\r\n"
   "Accept-Charset: iso-8859-1, utf-8;q=0.8, *;q=0.7\r\n"
   "Accept-Encoding: gzip\r\n";

/* HTTP/1.1 specific headers */
static UBYTE *connection="Connection: close\r\n";
static UBYTE *connection_keepalive="Connection: keep-alive\r\n";

static UBYTE *host="Host: %s\r\n";

static UBYTE *ifmodifiedsince="If-modified-since: %s\r\n";

static UBYTE *ifnonematch="If-none-match: %s\r\n";

static UBYTE *authorization="Authorization: Basic %s\r\n";

static UBYTE *proxyauthorization="Proxy-Authorization: Basic %s\r\n";

static UBYTE *nocache="Pragma: no-cache\r\n";

static UBYTE *referer="Referer: %s\r\n";

static UBYTE *httppostcontent=
   "Content-Length: %d\r\n"
   "Content-Type: application/x-www-form-urlencoded\r\n";

static UBYTE *httpmultipartcontent=
   "Content-Length: %d\r\n"
   "Content-Type: multipart/form-data; boundary=%s\r\n";

static UBYTE *tunnelrequest="CONNECT %.200s HTTP/1.1\r\n";

/* Unverifyable certificates that the user accepted */
struct Certaccept
{  NODE(Certaccept);
   UBYTE *hostname;
   UBYTE *certname;
};

static LIST(Certaccept) certaccepts;
static struct SignalSemaphore certsema;

/* Connection pool for keep-alive connections */
struct KeepAliveConnection
{  NODE(KeepAliveConnection);
   UBYTE *hostname;           /* Hostname for this connection */
   long port;                 /* Port number */
   BOOL ssl;                  /* SSL enabled flag */
   ULONG owner_task_id;       /* Task that owns this socketbase/sock */
   struct Library *socketbase; /* Socket library base */
   long sock;                 /* Socket descriptor */
   struct Assl *assl;         /* SSL context (NULL if not SSL) */
   ULONG last_used;           /* Timestamp of last use */
   BOOL in_use;               /* Currently in use flag */
};

static LIST(KeepAliveConnection) keepalive_pool;
static struct SignalSemaphore keepalive_sema;
static BOOL keepalive_sema_initialized = FALSE;
/* Protect global SocketBase swaps in multi-tasking environment. */
/* Shared across http.c and amissl.c */
struct SignalSemaphore socketbase_swap_sema;
BOOL socketbase_swap_sema_initialized = FALSE;
/* LIMITS TO PREVENT CLOGGING */
#define KEEPALIVE_TIMEOUT 15  /* Reduced to 15s to free resources faster */
#define MAX_IDLE_CONNECTIONS 8 /* Hard limit on idle connections */

/* Redirect loop protection - track redirects across HTTP requests */
static int redirect_count=0;
/* debug_log_sema is now defined above as a shared global */

/*-----------------------------------------------------------------------*/

/* Helper function to get Task ID for logging */
static ULONG get_task_id(void)
{  struct Task *task;
   task = FindTask(NULL);
   return (ULONG)task;
}

/* Safely set global SocketBase for proto/bsdsocket calls that require it. */
static struct Library *AcquireSocketBaseSwap(struct Library *newbase)
{  struct Library *saved;
   BOOL acquired = FALSE;
   int tries;
   saved = SocketBase;
   if(socketbase_swap_sema_initialized)
   {  tries = 0;
      while(!AttemptSemaphore(&socketbase_swap_sema))
      {  tries++;
         if(tries >= 200)
         {  /* CRITICAL: Never deadlock the browser here.
             * If we can't get the semaphore quickly, proceed without it. */
            break;
         }
         Delay(1);
      }
      if(tries < 200) acquired = TRUE;
   }
   SocketBase = newbase;
   /* Encode whether we acquired the semaphore in the low bit of the returned
    * pointer. Library bases are aligned so bit 0 should be 0. */
   if(acquired)
   {  return (struct Library *)(((ULONG)saved) | 1UL);
   }
   return saved;
}

static void ReleaseSocketBaseSwap(struct Library *saved)
{  BOOL acquired;
   acquired = BOOLVAL(((ULONG)saved) & 1UL);
   saved = (struct Library *)(((ULONG)saved) & ~1UL);
   SocketBase = saved;
   if(acquired && socketbase_swap_sema_initialized)
   {  ReleaseSemaphore(&socketbase_swap_sema);
   }
}

/* Thread-safe debug logging wrapper with Task ID */
static void debug_printf(const char *format, ...)
{  va_list args;
   ULONG task_id;
   
   /* Only output if HTTPDEBUG mode is enabled */
   if(!httpdebug)
   {  return;
   }
   
   task_id = get_task_id();
   
   if(debug_log_sema_initialized)
   {  int tries = 0;
      while(!AttemptSemaphore(&debug_log_sema))
      {  tries++;
         if(tries >= 200)
         {  return; /* Don't wedge if a task died while logging */
         }
         Delay(1);
      }
   }
   
   printf("[TASK:0x%08lX] ", task_id);
   va_start(args, format);
   vprintf(format, args);
   va_end(args);
   
   if(debug_log_sema_initialized)
   {  ReleaseSemaphore(&debug_log_sema);
   }
}


/*-----------------------------------------------------------------------*/

static void Messageread(struct Fetchdriver *fd,long n)
{  UBYTE buf[64];
   strcpy(buf,AWEBSTR(MSG_AWEB_BYTESREAD));
   strcat(buf,": ");
   sprintf(buf+strlen(buf),"%d",n);
   Updatetaskattrs(
      AOURL_Status,buf,
      TAG_END);
}

/* Forward declarations */
static void CleanupKeepAlivePool(void);

/* Normalize hostname for comparison (strips www. prefix if present) */
/* This allows www.example.com and example.com to match for connection pooling */
static UBYTE *NormalizeHostname(UBYTE *hostname)
{  if(!hostname) return NULL;
   
   /* Check if hostname starts with "www." (case-insensitive) */
   if(strlen((char *)hostname) > 4 && 
      (hostname[0] == 'w' || hostname[0] == 'W') &&
      (hostname[1] == 'w' || hostname[1] == 'W') &&
      (hostname[2] == 'w' || hostname[2] == 'W') &&
      hostname[3] == '.')
   {  /* Return pointer to hostname without www. prefix */
      return hostname + 4;
   }
   
   return hostname;
}

/* Compare two hostnames for connection pooling (handles www. prefix) */
static BOOL HostnameMatches(UBYTE *hostname1, UBYTE *hostname2)
{  UBYTE *norm1;
   UBYTE *norm2;
   
   if(!hostname1 || !hostname2) return FALSE;
   
   norm1 = NormalizeHostname(hostname1);
   norm2 = NormalizeHostname(hostname2);
   
   return (BOOL)STRIEQUAL(norm1, norm2);
}

/* Free a connection node and all its resources */
static void FreeConnectionNode(struct KeepAliveConnection *conn)
{  if(!conn) return;
   
   if(conn->sock >= 0 && conn->socketbase)
   {  if(conn->assl) Assl_closessl(conn->assl);
      a_close(conn->sock, conn->socketbase);
   }
   if(conn->assl)
   {  Assl_cleanup(conn->assl);
      FREE(conn->assl);
   }
   if(conn->socketbase) CloseLibrary(conn->socketbase);
   if(conn->hostname) FREE(conn->hostname);
   FREE(conn);
}

/* Check whether a pooled connection is safe to reuse.
 * Returns TRUE if reusable, FALSE if it should be closed/discarded. */
static BOOL KeepAliveReusable(struct KeepAliveConnection *conn)
{  struct timeval tv;
   fd_set rfds;
   fd_set efds;
   long rc;
   long nfds;
   struct Library *saved_socketbase;
   UBYTE peekbuf[1];
   long n;
   long nb;
   long msgpeek;
   long errno_value;
   
   if(!conn) return FALSE;
   if(conn->sock < 0 || !conn->socketbase) return FALSE;
   
   /* If there is anything readable or an exception pending, do not reuse.
    * For SSL this can include close_notify or protocol data; for plain HTTP it
    * can mean pipelined bytes (we don't carry a read buffer across uses). */
   FD_ZERO(&rfds);
   FD_ZERO(&efds);
   FD_SET((int)conn->sock, &rfds);
   FD_SET((int)conn->sock, &efds);
   tv.tv_sec = 0;
   tv.tv_usec = 0;
   nfds = conn->sock + 1;
   
   saved_socketbase = AcquireSocketBaseSwap(conn->socketbase);
   rc = WaitSelect((int)nfds, &rfds, NULL, &efds, &tv, NULL);
   errno_value = 0;
   if(rc < 0)
   {  errno_value = Errno();
   }
   ReleaseSocketBaseSwap(saved_socketbase);
   
   if(rc < 0)
   {  return FALSE;
   }
   if(rc > 0)
   {  /* Something is pending; don't reuse this connection. */
      return FALSE;
   }
   
   /* Also try a 1-byte MSG_PEEK in NONBLOCKING mode, in case the stack doesn't
    * signal readability reliably. Any data (or EOF) means it's not cleanly reusable.
    * CRITICAL: this must never block. */
   nb = 1;
   msgpeek = MSG_PEEK;
   saved_socketbase = AcquireSocketBaseSwap(conn->socketbase);
   if(IoctlSocket((int)conn->sock, FIONBIO, (char *)&nb) != 0)
   {  debug_printf("DEBUG: KeepAliveReusable: IoctlSocket(FIONBIO=1) failed\n");
   }
   ReleaseSocketBaseSwap(saved_socketbase);
   
   n = a_recv(conn->sock, peekbuf, 1, msgpeek, conn->socketbase);
   
   nb = 0;
   saved_socketbase = AcquireSocketBaseSwap(conn->socketbase);
   if(IoctlSocket((int)conn->sock, FIONBIO, (char *)&nb) != 0)
   {  debug_printf("DEBUG: KeepAliveReusable: IoctlSocket(FIONBIO=0) failed\n");
   }
   ReleaseSocketBaseSwap(saved_socketbase);
   
   if(n == 0) return FALSE; /* EOF */
   if(n > 0) return FALSE;  /* pending data */
   
   return TRUE;
}

/* Get a connection from the keep-alive pool */
static struct KeepAliveConnection *GetKeepAliveConnection(UBYTE *hostname, long port, BOOL ssl)
{  struct KeepAliveConnection *conn;
   struct KeepAliveConnection *next;
   struct KeepAliveConnection *found_conn = NULL;
   struct timeval current_time;
   ULONG current_sec;
   struct KeepAliveConnection *dead_list = NULL;
   ULONG task_id;
   
   if(!keepalive_sema_initialized || !hostname) return NULL;
   task_id = get_task_id();
   
   ObtainSemaphore(&keepalive_sema);
   GetSysTime(&current_time);
   current_sec = current_time.tv_secs;
   
   for(conn = (struct KeepAliveConnection *)keepalive_pool.first; conn->next; conn = next)
   {  next = (struct KeepAliveConnection *)conn->next;
      
      /* CRITICAL: Only reuse connections created by this task.
       * Many Amiga TCP/IP stacks treat SocketBase/library bases as task-affine.
       * Cross-task reuse can cause blocking I/O and apparent deadlocks. */
      if(!conn->in_use && conn->owner_task_id == task_id &&
         conn->port == port && conn->ssl == ssl &&
         conn->hostname && HostnameMatches(conn->hostname, hostname))
      {  ULONG age = current_sec - conn->last_used;
         if(age < KEEPALIVE_TIMEOUT)
         {  if(KeepAliveReusable(conn))
            {  conn->in_use = TRUE;
               conn->last_used = current_sec;
               Remove((struct Node *)conn);
               found_conn = conn;
               break; /* Stop searching, found one */
            }
            else
            {  /* Not safe to reuse; close it. */
               Remove((struct Node *)conn);
               conn->next = dead_list;
               dead_list = conn;
            }
         }
         else
         {  /* Found a timed out connection, mark for deletion */
            Remove((struct Node *)conn);
            conn->next = dead_list;
            dead_list = conn;
         }
      }
   }
   
   ReleaseSemaphore(&keepalive_sema);
   
   /* Cleanup dead connections outside semaphore */
   while(dead_list)
   {  conn = dead_list;
      dead_list = conn->next;
      FreeConnectionNode(conn);
   }
   
   if(found_conn)
   {  debug_printf("DEBUG: GetKeepAliveConnection: Reusing connection for %s:%ld\n", hostname, port);
   }
   
   return found_conn;
}

/* Return a connection to the keep-alive pool */
static void ReturnKeepAliveConnection(struct Httpinfo *hi)
{  struct KeepAliveConnection *conn;
   struct KeepAliveConnection *node, *next_node;
   struct KeepAliveConnection *kill_list = NULL;
   struct timeval current_time;
   ULONG current_sec;
   ULONG age;  /* C89: Declare at start */
   int pool_count = 0;
   ULONG task_id;
   struct timeval tv;
   fd_set rfds;
   fd_set efds;
   long rc;
   long nfds;
   struct Library *saved_socketbase;
   
   if(!keepalive_sema_initialized || !hi || !hi->hostname) return;
   task_id = get_task_id();
   
   /* CRITICAL FIX: Do NOT pool if server requested close */
   /* Check flags AND explicit Connection header parsing result */
   /* If HTTPIF_KEEPALIVE is NOT set, it means server said "close" or didn't say "keep-alive" on HTTP/1.0 */
   if(!(hi->flags & HTTPIF_KEEPALIVE))
   {  debug_printf("DEBUG: ReturnKeepAliveConnection: Server requested close (no KEEPALIVE flag), closing connection\n");
      /* Close it now */
#ifndef DEMOVERSION
      if(hi->assl) Assl_closessl(hi->assl);
#endif
      if(hi->sock >= 0 && hi->socketbase) a_close(hi->sock, hi->socketbase);
      hi->sock = -1;
      /* Clean up SSL struct if needed */
#ifndef DEMOVERSION
      if(hi->assl) { Assl_cleanup(hi->assl); FREE(hi->assl); hi->assl = NULL; }
#endif
      if(hi->socketbase) { CloseLibrary(hi->socketbase); hi->socketbase = NULL; }
      return;
   }
   
   /* Also check if client requested keep-alive */
   if(!(hi->flags & HTTPIF_KEEPALIVE_REQ)) return;
   
   /* Don't pool proxy connections mixed with hostname */
   if(hi->connect && hi->hostname && !STRIEQUAL(hi->connect, hi->hostname)) return;
   
   if(hi->sock < 0 || !hi->socketbase) return;
   
   /* CRITICAL: Do not pool if any bytes are pending on the socket. */
   FD_ZERO(&rfds);
   FD_ZERO(&efds);
   FD_SET((int)hi->sock, &rfds);
   FD_SET((int)hi->sock, &efds);
   tv.tv_sec = 0;
   tv.tv_usec = 0;
   nfds = hi->sock + 1;
   saved_socketbase = AcquireSocketBaseSwap(hi->socketbase);
   rc = WaitSelect((int)nfds, &rfds, NULL, &efds, &tv, NULL);
   ReleaseSocketBaseSwap(saved_socketbase);
   if(rc > 0)
   {  debug_printf("DEBUG: ReturnKeepAliveConnection: Not pooling - socket has pending data/exception\n");
#ifndef DEMOVERSION
      if(hi->assl) Assl_closessl(hi->assl);
#endif
      if(hi->sock >= 0 && hi->socketbase) a_close(hi->sock, hi->socketbase);
      hi->sock = -1;
#ifndef DEMOVERSION
      if(hi->assl) { Assl_cleanup(hi->assl); FREE(hi->assl); hi->assl = NULL; }
#endif
      if(hi->socketbase) { CloseLibrary(hi->socketbase); hi->socketbase = NULL; }
      return;
   }
   
   /* SSL connections can be pooled - the SSL object maintains state and can be reused */
   /* If a reused SSL connection fails, the retry logic will handle it by creating a fresh connection */
   
   conn = ALLOCTYPE(struct KeepAliveConnection, 1, 0);
   if(!conn) return;
   
   GetSysTime(&current_time);
   current_sec = current_time.tv_secs;
   
   conn->hostname = Dupstr(hi->hostname, -1);
   conn->port = (hi->port > 0) ? hi->port : (BOOLVAL(hi->flags & HTTPIF_SSL) ? 443 : 80);
   conn->ssl = BOOLVAL(hi->flags & HTTPIF_SSL);
   conn->owner_task_id = task_id;
   conn->socketbase = hi->socketbase;
   conn->sock = hi->sock;
   conn->assl = hi->assl;
   conn->last_used = current_sec;
   conn->in_use = FALSE;

   ObtainSemaphore(&keepalive_sema);
   
   /* 1. Count existing connections and remove expired/excess ones */
   /* This prevents the pool from growing indefinitely */
   for(node = (struct KeepAliveConnection *)keepalive_pool.first; node->next; node = next_node)
   {  next_node = (struct KeepAliveConnection *)node->next;
      
      age = current_sec - node->last_used;
      
      if(age >= KEEPALIVE_TIMEOUT)
      {  /* Remove expired */
         Remove((struct Node *)node);
         node->next = kill_list;
         kill_list = node;
      }
      else
      {  pool_count++;
      }
   }
   
   /* 2. If still too many, remove oldest (from Tail) */
   while (pool_count >= MAX_IDLE_CONNECTIONS)
   {   node = (struct KeepAliveConnection *)keepalive_pool.last;
       if (node && (struct Node *)node != (struct Node *)&keepalive_pool && node->prev) /* Check if valid node (not list header) */
       {   Remove((struct Node *)node);
           node->next = kill_list;
           kill_list = node;
           pool_count--;
       }
       else break;
   }

   /* 3. Add new connection to Head (LIFO) */
   if(conn->hostname)
   {  AddHead((struct List *)&keepalive_pool, (struct Node *)conn);
      /* Clear pointers in Httpinfo */
      hi->socketbase = NULL;
      hi->sock = -1;
      hi->assl = NULL;
   }
   else
   {  FREE(conn); 
   }
   ReleaseSemaphore(&keepalive_sema);
   
   /* Clean up culled connections outside semaphore to prevent deadlock */
   while(kill_list)
   {  node = kill_list;
      kill_list = node->next;
      FreeConnectionNode(node);
   }
   
   debug_printf("DEBUG: ReturnKeepAliveConnection: Pooled %s:%ld (Pool size: %d)\n", hi->hostname, hi->port, pool_count+1);
}

/* Clean up expired connections from the pool */
static void CleanupKeepAlivePool(void)
{  struct KeepAliveConnection *conn;
   struct KeepAliveConnection *next;
   struct KeepAliveConnection *close_list = NULL;
   struct timeval current_time;
   ULONG current_sec;
   
   if(!keepalive_sema_initialized) return;
   
   ObtainSemaphore(&keepalive_sema);
   GetSysTime(&current_time);
   current_sec = current_time.tv_secs;
   
   for(conn = (struct KeepAliveConnection *)keepalive_pool.first; conn->next; conn = next)
   {  next = (struct KeepAliveConnection *)conn->next;
      
      if(conn->in_use || ((current_sec - conn->last_used) >= KEEPALIVE_TIMEOUT))
      {  Remove((struct Node *)conn);
         conn->next = close_list;
         close_list = conn;
      }
   }
   ReleaseSemaphore(&keepalive_sema);
   
   while(close_list)
   {  conn = close_list;
      close_list = (struct KeepAliveConnection *)conn->next;
      FreeConnectionNode(conn);
   }
}

/* Close all idle (not in-use) keep-alive connections */
/* This is called when navigating to a new page to reset the connection pool */
/* CRITICAL: Only closes connections that have exceeded KEEPALIVE_TIMEOUT */
void CloseIdleKeepAliveConnections(void)
{  struct KeepAliveConnection *conn;
   struct KeepAliveConnection *next;
   struct KeepAliveConnection *close_list = NULL;
   
   if(!keepalive_sema_initialized) return;
   
   ObtainSemaphore(&keepalive_sema);
   for(conn = (struct KeepAliveConnection *)keepalive_pool.first; conn && conn->next; conn = next)
   {  next = (struct KeepAliveConnection *)conn->next;
      if(!conn->in_use)
      {  Remove((struct Node *)conn);
         conn->next = close_list;
         close_list = conn;
      }
   }
   
   ReleaseSemaphore(&keepalive_sema);
   
   while(close_list)
   {  conn = close_list;
      close_list = (struct KeepAliveConnection *)conn->next;
      FreeConnectionNode(conn);
   }
}

static BOOL Makehttpaddr(struct Httpinfo *hi,UBYTE *proxy,UBYTE *url,BOOL ssl)
{  UBYTE *p,*q,*r,*u;
   UBYTE *userid=NULL,*passwd=NULL;
   long l;
   BOOL gotport=FALSE;
   if(u=strchr(url,':')) u++; /* Should always be found */
   else u=url;
   if(u[0]=='/' && u[1]=='/') u+=2;
   if(proxy)
   {  p=strchr(proxy,':');
      hi->connect=Dupstr(proxy,p?p-proxy:-1);
      hi->port=p?atol(p+1):8080;
      p=stpbrk(u,":/");
      if(p && *p==':' && (q=strchr(p,'@')) && (!(r=strchr(p,'/')) || q<r))
      {  /* userid:passwd@host[:port][/path] */
         userid=Dupstr(u,p-u);
         passwd=Dupstr(p+1,q-p-1);
         u=q+1;
         p=stpbrk(u,":/");
      }
      hi->hostname=Dupstr(u,p?p-u:-1);
      gotport=(p && *p==':');
      p=strchr(u,'/');
      hi->hostport=Dupstr(u,p?p-u:-1);
      if(ssl)
      {  /* Will be tunneled. Use abspath like with no proxy */
         if(gotport)
         {  hi->tunnel=Dupstr(hi->hostport,-1);
         }
         else
         {  hi->tunnel=ALLOCTYPE(UBYTE,strlen(hi->hostname)+5,0);
            if(hi->tunnel)
            {  strcpy(hi->tunnel,hi->hostname);
               strcat(hi->tunnel,":443");
            }
         }
         hi->abspath=Dupstr(p?p:(UBYTE *)"/",-1);
         hi->flags|=HTTPIF_SSLTUNNEL;
      }
      else
      {  if(p)
         {  hi->abspath=Dupstr(url,-1);
         }
         else
         {  /* append '/' */
            l=strlen(url);
            if(hi->abspath=Dupstr(url,l+1)) hi->abspath[l]='/';
         }
      }
   }
   else
   {  p=stpbrk(u,":/");
      if(p && *p==':' && (q=strchr(p,'@')) && (!(r=strchr(p,'/')) || q<r))
      {  /* userid:password@host[:port][/path] */
         userid=Dupstr(u,p-u);
         passwd=Dupstr(p+1,q-p-1);
         u=q+1;
         p=stpbrk(u,":/");
      }
      hi->connect=Dupstr(u,p?p-u:-1);
      if(p && *p==':')
      {  hi->port=atol(p+1);
      }
      else
      {  hi->port=-1;
      }
      p=strchr(u,'/');
      hi->hostport=Dupstr(u,p?p-u:-1);
      hi->abspath=Dupstr(p?p:(UBYTE *)"/",-1);
      hi->hostname=Dupstr(hi->connect,-1);
   }
   if(userid && passwd)
   {  if(hi->auth) Freeauthorize(hi->auth);
      if(hi->auth=Newauthorize(hi->hostport,"dummyrealm"))
      {  Setauthorize(hi->auth,userid,passwd);
         hi->flags|=HTTPIF_AUTH;
      }
   }
   if(userid) FREE(userid);
   if(passwd) FREE(passwd);
   return (BOOL)(hi->connect && hi->hostport && hi->abspath && hi->hostname);
}

/* Build a HTTP request. The length is returned.
 * (*request) is either fd->block or a dynamic string if fd->block was too small */
static long Buildrequest(struct Fetchdriver *fd,struct Httpinfo *hi,UBYTE **request)
{  UBYTE *p=fd->block;
   UBYTE *cookies;
   const UBYTE *plang;
   int plen;
   *request=fd->block;
   if(fd->postmsg || fd->multipart)
      p+=sprintf(p,httppostrequest,hi->abspath);
   else p+=sprintf(p,httprequest,hi->abspath);
   ObtainSemaphore(&prefssema);
#ifndef DEMOVERSION
   if(*prefs.spoofid)
   {  p+=sprintf(p,useragentspoof,prefs.spoofid);
   }
   else
#endif
   {  p+=sprintf(p,useragent,awebversion);
   }
   ReleaseSemaphore(&prefssema);
   p+=sprintf(p,fixedheaders);
   /* Accept-Language: use locale.library preferred tag when it looks like ISO 639-1 or xx-yy. */
   plang=NULL;
   plen=0;
   if(locale)
   {  if(locale->loc_PrefLanguages[0])
      {  plang=(const UBYTE *)locale->loc_PrefLanguages[0];
         plen=(int)strlen((const char *)plang);
      }
   }
   if(plang && plen==2
      && isalpha((int)(unsigned char)plang[0]) && isalpha((int)(unsigned char)plang[1]))
   {  if(STRIEQUAL((UBYTE *)plang,(UBYTE *)"en"))
         p+=sprintf(p,"Accept-Language: en-US, en;q=0.9\r\n");
      else
         p+=sprintf(p,"Accept-Language: %.2s, en;q=0.5\r\n",plang);
   }
   else if(plang && plen==5 && plang[2]=='-'
      && isalpha((int)(unsigned char)plang[0]) && isalpha((int)(unsigned char)plang[1])
      && isalpha((int)(unsigned char)plang[3]) && isalpha((int)(unsigned char)plang[4]))
      p+=sprintf(p,"Accept-Language: %.5s, en;q=0.5\r\n",plang);
   else
      p+=sprintf(p,"Accept-Language: en-US, en;q=0.9\r\n");
   /* Add HTTP/1.1 Connection header - use keep-alive by default for HTTP/1.1 */
   /* Only use keep-alive if not using proxy (proxies may not support it well) */
   if(!fd->proxy)
   {  p+=sprintf(p,connection_keepalive);
      hi->flags|=HTTPIF_KEEPALIVE_REQ;
   }
   else
   {  p+=sprintf(p,connection);
   }
   if(hi->hostport)
      p+=sprintf(p,host,hi->hostport);
   /* Skip conditional headers for Range requests - they cause 304 instead of 206 */
   if(!(hi->flags & HTTPIF_RANGE_REQUEST))
   {  if(fd->validate)
      {  UBYTE date[32];
         Makedate(fd->validate,date);
         p+=sprintf(p,ifmodifiedsince,date);
      }
      
      /* If ETag exists verify this else try time */
      if(fd->etag && strlen(fd->etag)>0)
      {  p+=sprintf(p,ifnonematch,fd->etag);
      }
   }
   
   if(hi->auth && hi->auth->cookie)
      p+=sprintf(p,authorization,hi->auth->cookie);
   if(hi->prxauth && hi->prxauth->cookie)
      p+=sprintf(p,proxyauthorization,hi->prxauth->cookie);
   if(fd->flags&FDVF_NOCACHE)
      p+=sprintf(p,nocache);
   if(fd->referer && (p-fd->block)+strlen(fd->referer)<7000)
      p+=sprintf(p,referer,fd->referer);
   if(fd->multipart)
   {  p+=sprintf(p,httpmultipartcontent,
         fd->multipart->length,fd->multipart->buf.buffer);
   }
   else if(fd->postmsg)
   {  p+=sprintf(p,httppostcontent,strlen(fd->postmsg));
   }
   /* Add Range header if resuming a partial download */
   if((hi->flags & HTTPIF_RANGE_REQUEST) && hi->bytes_received > 0 && hi->server_supports_range)
   {  /* Request remaining bytes: bytes=XXXX- */
      /* Note: We don't specify end byte, server will send until end of file */
      p+=sprintf(p, "Range: bytes=%ld-\r\n", hi->bytes_received);
      debug_printf("DEBUG: Buildrequest: Adding Range header: bytes=%ld-\n", hi->bytes_received);
   }
   if(prefs.cookies && (cookies=Findcookies(fd->name,hi->flags&HTTPIF_SSL)))
   {  long len=strlen(cookies);
      if((p-fd->block)+len<7000)
      {  strcpy(p,cookies);
         p+=len;
      }
      else
      {  UBYTE *newreq=ALLOCTYPE(UBYTE,(p-fd->block)+len+16,0);
         if(newreq)
         {  strcpy(newreq,fd->block);
            strcpy(newreq+(p-fd->block),cookies);
            *request=newreq;
            p=newreq+(p-fd->block)+len;
         }
      }
      FREE(cookies);
   }
   p+=sprintf(p,"\r\n");
   return p-*request;
}

/*-----------------------------------------------------------------------*/

/* Reset socket receive timeout after successful data receipt */
/* This ensures the timeout counter resets every time data is received */
/* SO_RCVTIMEO is per-operation, but explicitly resetting ensures fresh timeout for next operation */
static void ResetSocketTimeout(struct Httpinfo *hi)
{  struct timeval timeout;
   struct Library *saved_socketbase;
   
   /* Only reset if socket is valid and socketbase is available */
   if(!hi || hi->sock < 0 || !hi->socketbase)
   {  return;
   }
   
   timeout.tv_sec = 15;  /* 15 second timeout per operation - reasonable for modern networks */
   timeout.tv_usec = 0;
   
   /* CRITICAL: Validate SocketBase before using it */
   /* On AmigaOS, if SocketBase is NULL or invalid, calling socket functions causes Guru Meditation */
   if(!hi->socketbase)
   {  debug_printf("DEBUG: ResetSocketTimeout: socketbase is NULL, cannot set timeout\n");
      return;
   }
   
   /* Set global SocketBase for setsockopt() from proto/bsdsocket.h */
   /* Save and restore SocketBase to avoid race conditions */
   saved_socketbase = AcquireSocketBaseSwap(hi->socketbase);
   
   /* CRITICAL: Validate SocketBase is still valid after assignment */
   if(!SocketBase)
   {  debug_printf("DEBUG: ResetSocketTimeout: SocketBase became NULL after assignment\n");
      ReleaseSocketBaseSwap(saved_socketbase); /* Restore before returning */
      return;
   }
   
   /* Reset receive timeout - this ensures next recv() gets a fresh 15-second timeout */
   /* This is important for long data transfers where gaps between packets might occur */
   setsockopt(hi->sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
   
   /* Restore SocketBase */
   ReleaseSocketBaseSwap(saved_socketbase);
}

/* Receive a block through SSL or socket. */
static long Receive(struct Httpinfo *hi,UBYTE *buffer,long length)
{  long result;
#ifndef DEMOVERSION
   if(hi->flags&HTTPIF_SSL)
   {  debug_printf("DEBUG: Receive: Calling Assl_read() - sock=%ld, length=%ld, assl=%p\n",
             hi->sock, length, hi->assl);
      result=Assl_read(hi->assl,buffer,length);
      debug_printf("DEBUG: Receive: Assl_read() returned %ld\n", result);
   }
   else
#endif
   {  debug_printf("DEBUG: Receive: Calling a_recv() - sock=%ld, length=%ld\n", hi->sock, length);
      result=a_recv(hi->sock,buffer,length,0,hi->socketbase);
      debug_printf("DEBUG: Receive: a_recv() returned %ld\n", result);
   }
   return result;
}



/* Read remainder of block. Returns FALSE if eof or error. */
static BOOL Readblock(struct Httpinfo *hi)
{  long n;
   long errno_value;
   struct Library *saved_socketbase;
   UBYTE *hostname_str;
   int tries;
   int intr_tries;
   debug_printf("DEBUG: Readblock() called, current blocklength=%ld\n", hi->blocklength);
   
#ifdef DEVELOPER
   UBYTE *block;
   if(!hi->socketbase)
   {  block=fgets(hi->fd->block+hi->blocklength,hi->fd->blocksize-hi->blocklength,
         (FILE *)hi->sock);
      n=block?strlen(block):0;
      /* for some reason, we get a bogus 'G' in the second console window */
      if(STRNEQUAL(hi->fd->block,"GHTTP/",6))
      {  memmove(hi->fd->block,hi->fd->block+1,n-1);
         n--;
      }
   }
   else
#endif
   /* Attempt receive with limited retries for EAGAIN/EWOULDBLOCK to avoid
    * indefinite blocking or infinite loops on stale keep-alive connections. */
   tries = 0;
   intr_tries = 0;
   for(;;)
   {  n=Receive(hi,hi->fd->block+hi->blocklength,hi->fd->blocksize-hi->blocklength);
      if(n >= 0) break;
      if(Checktaskbreak()) break;
#ifndef DEMOVERSION
      /* Assl_read signals WaitSelect timeouts via C errno; Errno() may stay stale. */
      if((hi->flags & HTTPIF_SSL) && errno == ETIMEDOUT)
         break;
#endif
      errno_value = 0;
      if(hi->socketbase)
      {  saved_socketbase = AcquireSocketBaseSwap(hi->socketbase);
         errno_value = Errno();
         ReleaseSocketBaseSwap(saved_socketbase);
      }
      /* Interrupted system call (EINTR) should be retried, not treated as a
       * network failure. Some stacks signal async events via EINTR during SSL. */
      if(errno_value == EINTR)
      {  intr_tries++;
         if(intr_tries >= 50)
         {  hostname_str = hi->hostname ? hi->hostname : (UBYTE *)"unknown";
            debug_printf("DEBUG: Readblock: EINTR persisted (%ld tries), treating as timeout\n",
                   (long)intr_tries);
            Tcperror(hi->fd, TCPERR_NOCONNECT_TIMEOUT, hostname_str);
            return FALSE;
         }
         Delay(1);
         continue;
      }
      if(errno_value == EAGAIN || errno_value == EWOULDBLOCK)
      {  tries++;
         if(tries >= 10)
         {  hostname_str = hi->hostname ? hi->hostname : (UBYTE *)"unknown";
            debug_printf("DEBUG: Readblock: EAGAIN/EWOULDBLOCK persisted (%ld tries), treating as timeout\n", (long)tries);
            Tcperror(hi->fd, TCPERR_NOCONNECT_TIMEOUT, hostname_str);
            return FALSE;
         }
         Delay(1); /* Yield to other tasks before retrying */
         continue;
      }
      break;
   }
   
   debug_printf("DEBUG: Readblock: Receive returned %ld bytes\n", n);
   
   if(n<0 || Checktaskbreak())
   {  
      /* Check errno to provide more specific error reporting */
      /* Use Errno() function from bsdsocket.library to get error code */
      /* Note: Errno() is deprecated per SDK but still works. Modern way is SocketBaseTags() with SBTC_ERRNO */
      if(n < 0 && !Checktaskbreak())
      {  errno_value = 0;
         if(hi->socketbase)
         {  saved_socketbase = AcquireSocketBaseSwap(hi->socketbase);
            errno_value = Errno(); /* Get error code from bsdsocket.library */
            ReleaseSocketBaseSwap(saved_socketbase);
         }
#ifndef DEMOVERSION
         if((hi->flags & HTTPIF_SSL) && errno == ETIMEDOUT)
            errno_value = ETIMEDOUT;
#endif
         
         /* Report specific error type via Tcperror() for actual errors */
         hostname_str = hi->hostname ? hi->hostname : (UBYTE *)"unknown";
         if(errno_value == ETIMEDOUT)
         {  debug_printf("DEBUG: Readblock: Receive timeout (errno=ETIMEDOUT)\n");
            /* Timeout during receive - report as timeout error */
            Tcperror(hi->fd, TCPERR_NOCONNECT_TIMEOUT, hostname_str);
         }
         else if(errno_value == ECONNRESET)
         {  debug_printf("DEBUG: Readblock: Connection reset by peer (errno=ECONNRESET)\n");
            /* Connection was reset - but don't report error yet */
            /* The main loop will check if all data was received before reporting error */
            /* This handles the case where server closes connection after sending all data */
         }
         else if(errno_value == ECONNREFUSED)
         {  debug_printf("DEBUG: Readblock: Connection refused (errno=ECONNREFUSED)\n");
            /* Connection refused - report as connection error */
            Tcperror(hi->fd, TCPERR_NOCONNECT_REFUSED, hostname_str);
         }
         else if(errno_value == ENETUNREACH)
         {  debug_printf("DEBUG: Readblock: Network unreachable (errno=ENETUNREACH)\n");
            /* Network unreachable - report as network error */
            Tcperror(hi->fd, TCPERR_NOCONNECT_UNREACH, hostname_str);
         }
         else if(errno_value == EHOSTUNREACH)
         {  debug_printf("DEBUG: Readblock: Host unreachable (errno=EHOSTUNREACH)\n");
            /* Host unreachable - report as network error */
            Tcperror(hi->fd, TCPERR_NOCONNECT_HOSTUNREACH, hostname_str);
         }
         else
         {  debug_printf("DEBUG: Readblock: Network error (errno=%ld), returning FALSE\n", errno_value);
            /* Other network error - report generic error */
            Tcperror(hi->fd, TCPERR_NOCONNECT, hostname_str);
         }
      }
      else if(Checktaskbreak())
      {  debug_printf("DEBUG: Readblock: Task break detected, returning FALSE\n");
         /* Task break - don't report error, just return */
      }
      else
      {  debug_printf("DEBUG: Readblock: error or task break, returning FALSE\n");
      }
/* Don't send error, let source driver keep its partial data if it wants to.
      Updatetaskattrs(
         AOURL_Error,TRUE,
         TAG_END);
*/
      return FALSE;
   }
   if(n==0) 
   {  debug_printf("DEBUG: Readblock: no data received (EOF), returning FALSE\n");
      return FALSE;
   }
   
   /* CRITICAL: Prevent buffer overflow from extremely long headers (e.g., GitHub's 3700+ byte Content-Security-Policy) */
   /* Clamp received bytes to available buffer space */
   {  long available_space = hi->fd->blocksize - hi->blocklength;
      if(n > available_space)
      {  debug_printf("DEBUG: Readblock: WARNING - Received %ld bytes but only %ld bytes available, clamping to prevent overflow\n", n, available_space);
         n = available_space;
         if(n <= 0)
         {  debug_printf("DEBUG: Readblock: Buffer full (%ld/%ld), cannot read more data\n", hi->blocklength, hi->fd->blocksize);
            /* Buffer is full - this is an error condition for headers */
            /* Headers should not exceed blocksize (16384 bytes) */
            Updatetaskattrs(
               AOURL_Error,TRUE,
               TAG_END);
            return FALSE;
         }
      }
   }
   
   /* Validate blocklength before incrementing to prevent integer overflow */
   if(hi->blocklength + n > hi->fd->blocksize)
   {  debug_printf("DEBUG: Readblock: ERROR - blocklength (%ld) + n (%ld) exceeds blocksize (%ld), preventing overflow\n", 
          hi->blocklength, n, hi->fd->blocksize);
      Updatetaskattrs(
         AOURL_Error,TRUE,
         TAG_END);
      return FALSE;
   }
   
   debug_printf("DEBUG: Readblock: adding %ld bytes to block, new total=%ld (blocksize=%ld)\n", 
          n, hi->blocklength + n, hi->fd->blocksize);
   
   hi->blocklength+=n;
   
   /* Reset socket timeout after successful data receipt */
   /* This ensures the timeout counter resets every time data is received */
   /* This prevents premature socket closure during long data transfers */
   ResetSocketTimeout(hi);
   
   if(hi->flags&HTTPIF_HEADERS)
   {  Messageread(hi->fd,hi->readheaders+=n);
   }
   return TRUE;
}

/* Remove the first part from the block. */
static void Nextline(struct Httpinfo *hi)
{  long old_blocklength = hi->blocklength;
   if(hi->nextscanpos<hi->blocklength)
   {  memmove(hi->fd->block,hi->fd->block+hi->nextscanpos,hi->blocklength-hi->nextscanpos);
   }
   hi->blocklength-=hi->nextscanpos;
   hi->nextscanpos=0;
   debug_printf("DEBUG: Nextline: consumed %ld bytes, remaining blocklength=%ld\n", 
          old_blocklength - hi->blocklength, hi->blocklength);
}

/* Find a complete line. Read again if no complete line found. */
static BOOL Findline(struct Httpinfo *hi)
{  UBYTE *p=hi->fd->block;
   UBYTE *end;
   for(;;)
   {  end=hi->fd->block+hi->blocklength;
      while(p<end && *p!='\n') p++;
      if(p<end) break;
      if(!Readblock(hi)) return FALSE;
   }
   /* Now we've got a LF. Terminate line here, but if it is preceded by CR ignore that too. */
   *p='\0';
   hi->linelength=p-hi->fd->block;
   hi->nextscanpos=hi->linelength+1;
   if(hi->linelength)
   {  p--;
      if(*p=='\r')
      {  *p='\0';
         hi->linelength--;
      }
   }
   if(httpdebug)
   {  Write(Output(),hi->fd->block,hi->linelength);
      Write(Output(),"\n",1);
   }
   return TRUE;
}

/* Get the authorization details from this buffer */
static struct Authorize *Parseauth(UBYTE *buf,UBYTE *server)
{  UBYTE *p,*q;
   struct Authorize *auth;
   for(p=buf;*p==' ';p++);
   if(!STRNIEQUAL(p,"Basic ",6)) return NULL;
   for(p+=6;*p==' ';p++);
   if(!STRNIEQUAL(p,"realm",5)) return NULL;
   for(p+=5;*p==' ';p++);
   if(*p!='=') return FALSE;
   for(p++;*p==' ';p++);
   if(*p!='"') return FALSE;
   q=p+1;
   for(p++;*p!='"' && *p!='\r' && *p!='\n';p++);
   *p='\0';
   auth=Newauthorize(server,q);
   return auth;
}

/* Read and process headers until end of headers. Read when necessary.
 * Returns FALSE if eof or error, or data should be skipped. */
static BOOL Readheaders(struct Httpinfo *hi)
{     /* Reset encoding flags at start of headers - this is crucial! */
   hi->flags &= ~(HTTPIF_GZIPENCODED | HTTPIF_GZIPDECODING | HTTPIF_CHUNKED);
   
   /* Default assumption based on protocol version */
   /* For HTTP/1.1, Keep-Alive is default. For 1.0, it's not. */
   /* We assume 1.1 default if we requested it, BUT we must clear it if server says close */
   if(hi->flags & HTTPIF_KEEPALIVE_REQ)
   {  hi->flags |= HTTPIF_KEEPALIVE;
      debug_printf("DEBUG: Assuming keep-alive support for HTTP/1.1 (will be cleared if server says 'close')\n");
   }
   else
   {  hi->flags &= ~HTTPIF_KEEPALIVE; /* Default off for HTTP/1.0 unless header found */
   }
   
   for(;;)
   {  if(!Findline(hi)) return FALSE;
      if(hi->linelength==0)
      {  if(hi->status) return FALSE;
         else 
         {  debug_printf("DEBUG: Headers complete, starting data processing\n");
            /* Allow gzip with chunked encoding - we now handle it properly */
            return TRUE;
         }
      }
      Updatetaskattrs(
         AOURL_Header,hi->fd->block,
         TAG_END);
      debug_printf("DEBUG: Processing header: '%s'\n", hi->fd->block);
      
      if(STRNIEQUAL(hi->fd->block,"Date:",5))
      {  hi->fd->serverdate=Scandate(hi->fd->block+5);
         Updatetaskattrs(
            AOURL_Serverdate,hi->fd->serverdate,
            TAG_END);
      }
      else if(STRNIEQUAL(hi->fd->block,"Last-Modified:",14))
      {  ULONG date=Scandate(hi->fd->block+14);
         Updatetaskattrs(
            AOURL_Lastmodified,date,
            TAG_END);
      }
      else if(STRNIEQUAL(hi->fd->block,"Expires:",8))
      {  long expires=Scandate(hi->fd->block+8);
         Updatetaskattrs(
            AOURL_Expires,expires,
            TAG_END);
      }
      else if(STRNIEQUAL(hi->fd->block,"Content-Length:",15))
      {  long i=0;
         sscanf(hi->fd->block+15," %ld",&i);
         hi->partlength = i; /* Store for use in Readdata to track compressed data consumption */
         Updatetaskattrs(
            AOURL_Contentlength,i,
            TAG_END);
      }
      else if(STRNIEQUAL(hi->fd->block,"Content-Type:",13))
      {  UBYTE mimetype[32];
         UBYTE *p,*q,*r;
         UBYTE qq;
         long l;
         BOOL foreign=FALSE;
         BOOL forward=TRUE;
         
         debug_printf("DEBUG: Content-Type header found in Readheaders: '%s'\n", hi->fd->block);
         mimetype[0] = '\0';  /* Initialize empty string */
         if(!prefs.ignoremime)
         {  for(p=hi->fd->block+13;*p && isspace(*p);p++);
            for(q=p;*q && !isspace(*q) && *q!=';';q++);
            qq=*q;
            *q='\0';
            l=q-p;
            if(qq && !hi->boundary)
            {  if(STRIEQUAL(p,"MULTIPART/X-MIXED-REPLACE")
               || STRIEQUAL(p,"MULTIPART/MIXED-REPLACE"))
               {  for(q++;*q && !STRNIEQUAL(q,"BOUNDARY=",9);q++);
                  if(*q)
                  {  q+=9;
                     if(*q=='"')
                     {  q++;
                        for(r=q;*r && *r!='"';r++);
                        *r='\0';
                     }
                     if(hi->boundary=Dupstr(q-2,-1))
                     {  hi->boundary[0]='-';
                        hi->boundary[1]='-';
                     }
                     forward=FALSE;
                  }
               }
            }
            if(qq && STRNIEQUAL(p,"TEXT/",5))
            {  for(q++;*q && !STRNIEQUAL(q,"CHARSET=",8);q++);
               if(*q)
               {  q+=8;
                  while(*q && isspace(*q)) q++;
                  if(*q=='"')
                  {  q++;
                     for(r=q;*r && *r!='"';r++);
                     *r='\0';
                  }
                  else
                  {  for(r=q;*r && !isspace(*r);r++);
                     *r='\0';
                  }
                  if(*q && !STRIEQUAL(q,"ISO-8859-1")) foreign=TRUE;
               }
            }
            if(forward)
            {  if(l>31) p[31]='\0';
               strcpy(mimetype,p);
            }
         }
         if(*mimetype)
         {  debug_printf("DEBUG: Setting Content-Type to: '%s'\n", mimetype);
            /* Store content type in parttype for later use (e.g., gzip processing) */
            strncpy(hi->parttype, mimetype, sizeof(hi->parttype) - 1);
            hi->parttype[sizeof(hi->parttype) - 1] = '\0';
            debug_printf("DEBUG: Stored parttype='%s' (length=%ld)\n", hi->parttype, strlen(hi->parttype));
            Updatetaskattrs(
               AOURL_Contenttype,mimetype,
               AOURL_Foreign,foreign,
               TAG_END);
         }
         else
         {  debug_printf("DEBUG: No mimetype extracted from Content-Type header (forward=%d, prefs.ignoremime=%d)\n", forward, prefs.ignoremime);
            /* Clear parttype if no mimetype */
            hi->parttype[0] = '\0';
         }
      }
      else if(STRNIEQUAL(hi->fd->block,"Content-Encoding:",17))
      {  if(strstr(hi->fd->block+18,"gzip"))
         {  hi->flags|=HTTPIF_GZIPENCODED;
            debug_printf("DEBUG: Detected gzip encoding\n");
            Updatetaskattrs(AOURL_Contentlength,0,TAG_END);
         }
      }
      else if(STRNIEQUAL(hi->fd->block,"Transfer-Encoding:",18))
      {  if(strstr(hi->fd->block+18,"chunked"))
         {  hi->flags|=HTTPIF_CHUNKED;
            debug_printf("DEBUG: Detected chunked transfer encoding\n");
         }
      }
      else if(STRNIEQUAL(hi->fd->block,"Connection:",11))
      {  /* Parse Connection header to detect keep-alive support */
         /* Connection header can be a comma-separated list: "Upgrade, close" or "keep-alive, close" */
         /* We must check ALL tokens, not just the first one */
         UBYTE *p;
         UBYTE *q;
         UBYTE *token_start;
         UBYTE *token_end;
         BOOL found_close = FALSE;
         BOOL found_keepalive = FALSE;
         
         for(p=hi->fd->block+11;*p && isspace(*p);p++);
         
         /* Parse comma-separated tokens */
         while(*p)
         {  /* Skip leading whitespace */
            while(*p && isspace(*p)) p++;
            if(!*p) break;
            
            /* Find start of token */
            token_start = p;
            
            /* Find end of token (comma, whitespace, or end of string) */
            for(q=p;*q && *q!=',' && !isspace(*q);q++);
            token_end = q;
            
            /* Check if this token is "close" (case-insensitive) */
            if((token_end - token_start == 5) && STRNIEQUAL(token_start, "close", 5))
            {  found_close = TRUE;
               debug_printf("DEBUG: Found 'close' in Connection header\n");
            }
            /* Check if this token is "keep-alive" (case-insensitive) */
            else if((token_end - token_start == 10) && STRNIEQUAL(token_start, "keep-alive", 10))
            {  found_keepalive = TRUE;
               debug_printf("DEBUG: Found 'keep-alive' in Connection header\n");
            }
            
            /* Skip to next token (past comma if present) */
            if(*q == ',')
            {  p = q + 1;
            }
            else
            {  break; /* End of string or no more tokens */
            }
         }
         
         /* "close" takes precedence - if server says close, connection must be closed */
         if(found_close)
         {  hi->flags &= ~HTTPIF_KEEPALIVE;
            debug_printf("DEBUG: Server sent Connection: close (clearing keep-alive flag)\n");
         }
         else if(found_keepalive)
         {  hi->flags |= HTTPIF_KEEPALIVE;
            debug_printf("DEBUG: Server sent Connection: keep-alive\n");
         }
      }
      else if(STRNIEQUAL(hi->fd->block,"Accept-Ranges:",14))
      {  /* Parse Accept-Ranges header to detect Range request support */
         UBYTE *p;
         for(p=hi->fd->block+14;*p && isspace(*p);p++);
         
         /* Check if server supports Range requests (Accept-Ranges: bytes) */
         if(STRNIEQUAL(p, "bytes", 5))
         {  hi->server_supports_range = TRUE;
            debug_printf("DEBUG: Server supports Range requests (Accept-Ranges: bytes)\n");
         }
         else
         {  hi->server_supports_range = FALSE;
            debug_printf("DEBUG: Server does not support Range requests (Accept-Ranges: %s)\n", p);
         }
      }
      else if(STRNIEQUAL(hi->fd->block,"Content-Range:",14))
      {  long start;  /* C89: Declare all variables at start */
         long end;
         long total;
         UBYTE *p;
         
         /* Parse Content-Range header for 206 Partial Content responses */
         /* Format: bytes start-end/total or bytes asterisk-slash-total/total */
         for(p=hi->fd->block+14;*p && isspace(*p);p++);
         
         if(STRNIEQUAL(p, "bytes ", 6))
         {  p += 6;
            /* Parse range: start-end/total */
            if(sscanf(p, "%ld-%ld/%ld", &start, &end, &total) == 3)
            {  debug_printf("DEBUG: Content-Range: bytes %ld-%ld/%ld\n", start, end, total);
               /* Store full file size for completion check */
               hi->full_file_size = total;
               /* Update partlength to reflect the actual range size (Content-Length) */
               hi->partlength = end - start + 1;
               /* bytes_received should already be set to start value */
            }
            else if(sscanf(p, "*/%ld", &total) == 1)
            {  /* Unsatisfiable range: asterisk followed by -total/total */
               debug_printf("DEBUG: Content-Range: bytes */%ld (unsatisfiable)\n", total);
            }
         }
      }
      else if(STRNIEQUAL(hi->fd->block,"ETag:",5))
      {  UBYTE *p,*q;
         for(p=hi->fd->block+5;*p && isspace(*p);p++);
         for(q=p;*q && !isspace(*q) && *q!=';';q++);
         *q='\0';
         if(q-p>63) p[63]='\0';
         /* Store ETag in both URL object and fetchdriver for caching */
         Updatetaskattrs(AOURL_Etag,p,TAG_END);
         if(hi->fd->etag) FREE(hi->fd->etag);
         hi->fd->etag=Dupstr(p,-1);
      }
      else if(STRNIEQUAL(hi->fd->block,"Content-Disposition:",20))
      {  UBYTE *p,*q;
         for(p=hi->fd->block+21;*p && isspace(*p);p++);
         for(q=p;*q && !isspace(*q) && *q!=';';q++);
         *q='\0';
         if(STRIEQUAL(p,"attachment"))
         {  p+=11;
            if((q=strstr(p,"filename")))
            {  for(p=q+8;*p && (isspace(*p) || *p=='"' || *p=='=');p++);
               for(q=p;*q && !isspace(*q) && *q!=';' && *q!='"';q++);
               *q='\0';
               Updatetaskattrs(AOURL_Filename,p,TAG_END);
            }
         }
      }
      else if(STRNIEQUAL(hi->fd->block,"Content-script-type:",20))
      {  UBYTE *p,*q;
         for(p=hi->fd->block+20;*p && isspace(*p);p++);
         for(q=p;*q && !isspace(*q) && *q!=';';q++);
         *q='\0';
         Updatetaskattrs(
            AOURL_Contentscripttype,p,
            TAG_END);
      }
      else if(STRNIEQUAL(hi->fd->block,"Pragma:",7))
      {  UBYTE *p,*q;
         for(p=hi->fd->block+7;*p && isspace(*p);p++);
         for(q=p;*q && !isspace(*q) && *q!=';';q++);
         *q='\0';
         if(STRIEQUAL(p,"no-cache"))
         {  Updatetaskattrs(
               AOURL_Nocache,TRUE,
               TAG_END);
         }
      }
      else if(STRNIEQUAL(hi->fd->block,"Cache-Control:",14))
      {  UBYTE *p,*q;
         for(p=hi->fd->block+14;*p && isspace(*p);p++);
         for(q=p;*q && !isspace(*q) && *q!='\r' && *q!='\n';q++);
         *q='\0';
         if(STRIEQUAL(p,"no-cache") || STRIEQUAL(p,"no-store"))
         {  Updatetaskattrs(
               AOURL_Nocache,TRUE,
               TAG_END);
         }
         else if(STRIEQUAL(p,"max-age"))
         {  /* Parse max-age value for caching */
            long maxage;
            maxage = 0;
            if(q=strchr(p,'='))
            {  sscanf(q+1,"%ld",&maxage);
               Updatetaskattrs(AOURL_Maxage,maxage,TAG_END);
            }
         }
      }
      else if(hi->movedto && STRNIEQUAL(hi->fd->block,"Location:",9))
      {  UBYTE *p;
         UBYTE *q;
         for(p=hi->fd->block+9;*p && isspace(*p);p++);
         for(q=p+strlen(p)-1;q>p && isspace(*q);q--);
         if(hi->movedtourl) FREE(hi->movedtourl);
         hi->movedtourl=Dupstr(p,q-p+1);
         debug_printf("DEBUG: Set movedtourl to: %s\n", hi->movedtourl ? (char *)hi->movedtourl : "(NULL)");
      }
      else if(hi->status==401 && STRNIEQUAL(hi->fd->block,"WWW-Authenticate:",17))
      {  struct Authorize *newauth=Parseauth(hi->fd->block+17,hi->hostport);
         if(newauth)
         {  if(hi->auth) Freeauthorize(hi->auth);
            hi->auth=newauth;
         }
      }
      else if(hi->status==407 && STRNIEQUAL(hi->fd->block,"Proxy-Authenticate:",19)
      && hi->fd->proxy)
      {  if(hi->prxauth) Freeauthorize(hi->prxauth);
         hi->prxauth=Parseauth(hi->fd->block+19,hi->fd->proxy);
      }
      else if(STRNIEQUAL(hi->fd->block,"Set-Cookie:",11))
      {  if(prefs.cookies) Storecookie(hi->fd->name,hi->fd->block+11,hi->fd->serverdate);
      }
      else if(STRNIEQUAL(hi->fd->block,"Refresh:",8))
      {  Updatetaskattrs(
            AOURL_Clientpull,hi->fd->block+8,
            TAG_END);
      }
      Nextline(hi);
   }
}

/* Read the HTTP response. Returns TRUE if HTTP, FALSE if plain response. */
static BOOL Readresponse(struct Httpinfo *hi)
{  long stat=0;
   BOOL http=FALSE;
   do
   {  if(!Readblock(hi)) return FALSE;
   } while(hi->blocklength<5);
   if(STRNEQUAL(hi->fd->block,"HTTP/",5))
   {  if(!Findline(hi)) return FALSE;
      hi->movedto=TAG_IGNORE;
      sscanf(hi->fd->block+5,"%*d.%*d %ld",&stat);
      debug_printf("DEBUG: HTTP status code: %ld\n", stat);
      Updatetaskattrs(
         AOURL_Header,hi->fd->block,
         TAG_END);
      if(stat<400)
      {  hi->flags|=HTTPIF_TUNNELOK;
         if(stat==301) hi->movedto=AOURL_Movedto;
         else if(stat==302 || stat==307)
         {  hi->movedto=AOURL_Tempmovedto;
            Updatetaskattrs(AOURL_Nocache,TRUE,TAG_END);
         }
         else if(stat==303) hi->movedto=AOURL_Seeother;
         else if(stat==304)
         {  /* 304 Not Modified - if we're doing a Range request, this means server ignored Range header */
            if(hi->flags & HTTPIF_RANGE_REQUEST)
            {  debug_printf("DEBUG: Readresponse: Got 304 Not Modified with Range request - server ignored Range header\n");
               /* Server ignored Range request, treat as error and fall back to full download */
               hi->flags &= ~HTTPIF_RANGE_REQUEST;
               hi->bytes_received = 0;  /* Reset to start from beginning */
            }
            Updatetaskattrs(
               AOURL_Notmodified,TRUE,
               TAG_END);
         }
         else if(stat==206)
         {  /* 206 Partial Content - Range request successful */
            debug_printf("DEBUG: Readresponse: Received 206 Partial Content (Range request)\n");
            hi->flags |= HTTPIF_RANGE_REQUEST;
            /* Content-Range header will be parsed in Readheaders() */
         }
      }
      else
      {  if(stat==401)
         {  if(hi->flags&HTTPIF_AUTH)
            {  /* Second attempt */
               if(hi->auth) Forgetauthorize(hi->auth);
               Updatetaskattrs(
                  AOURL_Error,TRUE,
                  TAG_END);
            }
            else
            {  hi->status=401;
            }
         }
         else if(stat==407)
         {  if(hi->flags&HTTPIF_PRXAUTH)
            {  /* Second attempt */
               if(hi->prxauth) Forgetauthorize(hi->prxauth);
               Updatetaskattrs(
                  AOURL_Error,TRUE,
                  TAG_END);
            }
            else
            {  hi->status=407;
            }
         }
         else if((stat==405 || stat==500 || stat==501) && hi->fd->postmsg)
         {  Updatetaskattrs(
               AOURL_Postnogood,TRUE,
               TAG_END);
         }
         else
         {  Updatetaskattrs(
               AOURL_Error,TRUE,
               TAG_END);
         }
      }
      http=TRUE;
   }
   return http;
}

/* Read and process part headers until end of headers. Read when necessary.
 * Returns FALSE if eof or error. */
static BOOL Readpartheaders(struct Httpinfo *hi)
{  hi->partlength=0;
   *hi->parttype='\0';
   for(;;)
   {  if(!Findline(hi)) return FALSE;
      if(hi->linelength==0)
      {  if(hi->status) return FALSE;
         else return TRUE;
      }
      Updatetaskattrs(
         AOURL_Header,hi->fd->block,
         TAG_END);
      if(STRNIEQUAL(hi->fd->block,"Content-Length:",15))
      {  sscanf(hi->fd->block+15," %ld",&hi->partlength);
      }
      else if(STRNIEQUAL(hi->fd->block,"Content-Type:",13))
      {  debug_printf("DEBUG: Content-Type header found in Readpartheaders: '%s'\n", hi->fd->block);
         if(!prefs.ignoremime)
         {  UBYTE *p,*q;
            for(p=hi->fd->block+13;*p && isspace(*p);p++);
            q=strchr(p,';');
            if(q) *q='\0';
            if(strlen(p)>31) p[31]='\0';
            strcpy(hi->parttype,p);
            debug_printf("DEBUG: Set parttype to: '%s'\n", hi->parttype);
         }
      }
      Nextline(hi);
   }
}

/* Read data and pass to main task. Returns FALSE if error or connection eof, TRUE if
 * multipart boundary found. */
static BOOL Readdata(struct Httpinfo *hi)
{  UBYTE *bdcopy=NULL;
   long bdlength=0,blocklength=0;
   BOOL result=FALSE,boundary,partial,eof=FALSE;
   BOOL done_after_send=FALSE; /* True if we reached Content-Length exactly */
   long remaining_bytes=0;
   long gzip_buffer_size=INPUTBLOCKSIZE;
   UBYTE *gzipbuffer=NULL;
   long gziplength=0;
   long err=0;
   UWORD gzip_end=0;
   int loop_count=0;
   long move_length;
   z_stream d_stream;
   BOOL d_stream_initialized=FALSE; /* Track if d_stream has been initialized */
   BOOL exit_main_loop=FALSE; /* Flag to exit main loop for gzip processing */
   long compressed_bytes_consumed=0; /* Track how much compressed data we've consumed for Content-Length validation */
   long total_compressed_read=0; /* Track total compressed bytes READ from network (before copying to gzipbuffer) */
   long total_bytes_received=0; /* Track total bytes received for non-gzip transfers with Content-Length */
   long expected_total_size;  /* Expected total size (full file size for Range requests, partlength for regular) */
   
   debug_printf("DEBUG: Readdata: ENTRY - blocklength=%ld, flags=0x%04X, parttype='%s', sock=%ld, partlength=%ld\n",
          hi->blocklength, hi->flags, hi->parttype ? (char *)hi->parttype : "(null)", hi->sock, hi->partlength);
   
   /* Initialize total_bytes_received from hi->bytes_received if using Range request */
   /* Also calculate expected_total_size for proper completion checking */
   if(hi->flags & HTTPIF_RANGE_REQUEST)
   {  total_bytes_received = hi->bytes_received;
      /* For Range requests, use full_file_size if available, otherwise use partlength */
      expected_total_size = (hi->full_file_size > 0) ? hi->full_file_size : hi->partlength;
      debug_printf("DEBUG: Readdata: Resuming with Range request, starting from byte %ld, expected total=%ld\n", hi->bytes_received, expected_total_size);
   }
   else
   {  total_bytes_received = 0;
      hi->bytes_received = 0;  /* Reset for new transfer */
      expected_total_size = hi->partlength; /* For non-range, partlength is the full content length */
   }
   
   /* Note: total_bytes_received starts at 0 (or bytes_received for Range) and is incremented as we process data blocks */
   /* This ensures accurate tracking without double-counting initial blocklength */

   if(hi->boundary)
   {  bdlength=strlen(hi->boundary);
      bdcopy=ALLOCTYPE(UBYTE,bdlength+1,0);
   }
   for(;;)
   {  if(hi->blocklength)
      {  debug_printf("DEBUG: Readdata loop: Processing data block, length=%ld, flags=0x%04X, parttype='%s'\n", 
                hi->blocklength, hi->flags, hi->parttype[0] ? (char *)hi->parttype : "(none)");
         
         // first block of the encoded data
         // allocate buffer and initialize zlib
         if((hi->flags & HTTPIF_GZIPENCODED) && !(hi->flags & HTTPIF_GZIPDECODING))
         {  int i;
            UBYTE *p;
            long gzip_start;
            long search_start;
            long data_after_chunk;
            
            gzip_start = 0;
            search_start = 0;
            
            /* Handle chunked encoding + gzip combination */
            /* When chunked encoding is present, skip chunk headers before looking for gzip magic */
            if(hi->flags & HTTPIF_CHUNKED)
            {  /* Skip chunk size line (e.g., "3259\r\n") */
               p = hi->fd->block;
               while(search_start < hi->blocklength - 1)
               {  if(p[search_start] == '\r' && search_start + 1 < hi->blocklength && p[search_start + 1] == '\n')
                  {  search_start += 2; /* Skip CRLF */
                     break;
                  }
                  search_start++;
               }
               debug_printf("DEBUG: Chunked encoding detected, skipping %ld bytes of chunk header\n", search_start);
            }
            
            /* Find the start of actual gzip data (1F 8B 08) */
            gzip_start = -1; /* Use -1 to indicate not found */
            for(p = hi->fd->block + search_start; p < hi->fd->block + hi->blocklength - 2; p++) {
                if(p[0] == 0x1F && p[1] == 0x8B && p[2] == 0x08) {
                    gzip_start = p - hi->fd->block;
                    break;
                }
            }
            
            /* With chunked+gzip, we need to extract chunks first before looking for gzip magic */
            /* The gzip stream may span multiple chunks, so magic bytes might not be in first chunk */
            if(gzip_start < 0 && (hi->flags & HTTPIF_CHUNKED))
            {  /* Check if we have enough data after chunk header to potentially contain gzip magic */
               data_after_chunk = hi->blocklength - search_start;
               if(data_after_chunk < 10)
               {  debug_printf("DEBUG: Chunked+gzip: Not enough data yet (only %ld bytes after chunk header), waiting for more chunks\n", data_after_chunk);
                  /* Don't start gzip yet - wait for more data */
                  /* Must read more data before continuing, otherwise infinite loop */
                  if(!Readblock(hi))
                  {  /* No more data available - this shouldn't happen if gzip is encoded */
                     debug_printf("DEBUG: Chunked+gzip: No more data available, disabling gzip\n");
                     hi->flags &= ~HTTPIF_GZIPENCODED;
                     hi->flags &= ~HTTPIF_GZIPDECODING;
                     continue;
                  }
                  /* Data read, continue loop to process new data */
                  continue;
               }
               else
               {  /* We have data but no gzip magic yet - this is OK with chunked encoding */
                  /* The gzip stream may start in a later chunk, so we should extract chunks */
                  /* and look for gzip magic in the accumulated data, or trust Content-Encoding header */
                  debug_printf("DEBUG: Chunked+gzip: Have %ld bytes after chunk header but no gzip magic yet - will extract chunks and look for gzip in accumulated data\n", data_after_chunk);
                  /* Don't disable gzip - trust Content-Encoding header and process chunks */
                  /* Set gzip_start to 0 to process all chunks from the beginning */
                  /* The chunk extraction code will handle finding where gzip data actually starts */
                  gzip_start = 0;
                  debug_printf("DEBUG: Chunked+gzip: Setting gzip_start to 0 to process all chunks from beginning\n");
               }
            }
            
            /* If we found gzip magic (at any position, including 0), use it */
            if(gzip_start >= 0)
            {  debug_printf("DEBUG: Found gzip data starting at position %ld, skipping prefix\n", gzip_start);
               
               /* For chunked encoding, extract all chunks and accumulate them */
               if(hi->flags & HTTPIF_CHUNKED)
               {                 /* Extract all chunks from the block and accumulate in gzipbuffer */
                  long chunk_pos;
                  long chunk_size;
                  long total_chunk_data;
                  long initial_gzip_size;
                  
                  /* Start parsing from beginning of block, not from search_start */
                  /* search_start is after the first chunk header, but we need to parse from the start */
                  chunk_pos = 0;
                  total_chunk_data = 0;
                  
                  /* Calculate total size of chunk data from gzip_start onwards */
                  while(chunk_pos < hi->blocklength)
                  {  long chunk_data_start;
                     
                     /* Skip whitespace before chunk size */
                     while(chunk_pos < hi->blocklength && 
                           (hi->fd->block[chunk_pos] == ' ' || hi->fd->block[chunk_pos] == '\t'))
                     {  chunk_pos++;
                     }
                     
                     /* Parse chunk size (hex number) */
                     chunk_size = 0;
                     {  long max_chunk_size = 0x7FFFFFFF; /* Maximum reasonable chunk size (2GB) */
                        long hex_digits = 0;
                        while(chunk_pos < hi->blocklength)
                        {  UBYTE c;
                           long digit;
                           
                           c = hi->fd->block[chunk_pos];
                           if(c >= '0' && c <= '9')
                           {  digit = c - '0';
                           }
                           else if(c >= 'A' && c <= 'F')
                           {  digit = c - 'A' + 10;
                           }
                           else if(c >= 'a' && c <= 'f')
                           {  digit = c - 'a' + 10;
                           }
                           else
                           {  break;
                           }
                           
                           /* Check for overflow before multiplying */
                           if(chunk_size > max_chunk_size / 16)
                           {  debug_printf("DEBUG: Chunk size overflow detected at position %ld, invalid chunk header\n", chunk_pos);
                              /* Invalid chunk header - treat as error */
                              chunk_size = -1;
                              break;
                           }
                           
                           chunk_size = chunk_size * 16 + digit;
                           hex_digits++;
                           chunk_pos++;
                           
                           /* Limit hex digits to prevent excessive parsing */
                           if(hex_digits > 16)
                           {  debug_printf("DEBUG: Chunk size hex number too long (%ld digits), invalid chunk header\n", hex_digits);
                              chunk_size = -1;
                              break;
                           }
                        }
                        
                        /* Validate chunk size */
                        if(chunk_size < 0)
                        {  debug_printf("DEBUG: Invalid chunk size (%ld), aborting chunked transfer\n", chunk_size);
                           /* Error in chunk parsing - abort */
                           if(bdcopy) { FREE(bdcopy); bdcopy = NULL; }
                           if(gzipbuffer) { FREE(gzipbuffer); gzipbuffer = NULL; }
                           if(d_stream_initialized) { inflateEnd(&d_stream); d_stream_initialized = FALSE; }
                           Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
                           return FALSE;
                        }
                        
                        if(chunk_size > max_chunk_size)
                        {  debug_printf("DEBUG: Chunk size too large (%ld > %ld), invalid chunk header\n", chunk_size, max_chunk_size);
                           /* Chunk size unreasonably large - treat as error */
                           if(bdcopy) { FREE(bdcopy); bdcopy = NULL; }
                           if(gzipbuffer) { FREE(gzipbuffer); gzipbuffer = NULL; }
                           if(d_stream_initialized) { inflateEnd(&d_stream); d_stream_initialized = FALSE; }
                           Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
                           return FALSE;
                        }
                        
                        if(hex_digits == 0)
                        {  debug_printf("DEBUG: No hex digits found in chunk size, invalid chunk header\n");
                           /* No valid hex digits - invalid chunk header */
                           if(bdcopy) { FREE(bdcopy); bdcopy = NULL; }
                           if(gzipbuffer) { FREE(gzipbuffer); gzipbuffer = NULL; }
                           if(d_stream_initialized) { inflateEnd(&d_stream); d_stream_initialized = FALSE; }
                           Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
                           return FALSE;
                        }
                     }
                     
                     if(chunk_size == 0)
                     {  /* Last chunk */
                        break;
                     }
                     
                     /* Skip chunk extension (semicolon and anything after) */
                     while(chunk_pos < hi->blocklength && 
                           hi->fd->block[chunk_pos] != '\r' && hi->fd->block[chunk_pos] != '\n')
                     {  chunk_pos++;
                     }
                     
                     /* Validate CRLF after chunk header */
                     if(chunk_pos >= hi->blocklength)
                     {  debug_printf("DEBUG: Chunk header missing CRLF terminator, waiting for more data\n");
                        /* Not enough data for CRLF - wait for more */
                        continue;
                     }
                     
                     /* Skip CRLF after chunk header */
                     if(hi->fd->block[chunk_pos] == '\r')
                     {  chunk_pos++;
                        if(chunk_pos >= hi->blocklength || hi->fd->block[chunk_pos] != '\n')
                        {  debug_printf("DEBUG: Chunk header has CR but missing LF, invalid chunk header\n");
                           /* Invalid chunk header format */
                           if(bdcopy) { FREE(bdcopy); bdcopy = NULL; }
                           if(gzipbuffer) { FREE(gzipbuffer); gzipbuffer = NULL; }
                           if(d_stream_initialized) { inflateEnd(&d_stream); d_stream_initialized = FALSE; }
                           Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
                           return FALSE;
                        }
                        chunk_pos++;
                     }
                     else if(hi->fd->block[chunk_pos] == '\n')
                     {  chunk_pos++;
                     }
                     else
                     {  debug_printf("DEBUG: Chunk header missing CRLF terminator (found 0x%02X), invalid chunk header\n", 
                               (unsigned char)hi->fd->block[chunk_pos]);
                        /* Invalid chunk header format - expected CRLF */
                        if(bdcopy) { FREE(bdcopy); bdcopy = NULL; }
                        if(gzipbuffer) { FREE(gzipbuffer); gzipbuffer = NULL; }
                        if(d_stream_initialized) { inflateEnd(&d_stream); d_stream_initialized = FALSE; }
                        Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
                        return FALSE;
                     }
                     
                     /* Store where chunk data starts */
                     chunk_data_start = chunk_pos;
                     
                     /* Only count chunk data that is at or after gzip_start */
                     if(chunk_data_start <= gzip_start && chunk_data_start + chunk_size > gzip_start)
                     {  /* This chunk contains gzip_start - count from gzip_start */
                        total_chunk_data += (chunk_data_start + chunk_size) - gzip_start;
                     }
                     else if(chunk_data_start > gzip_start)
                     {  /* This chunk is entirely after gzip_start - count all of it */
                        total_chunk_data += chunk_size;
                     }
                     /* Otherwise, this chunk is before gzip_start, skip it */
                     
                     /* Move past chunk data */
                     if(chunk_pos + chunk_size <= hi->blocklength)
                     {  chunk_pos += chunk_size;
                        
                        /* Skip CRLF after chunk data */
                        if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\r')
                        {  chunk_pos++;
                        }
                        if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\n')
                        {  chunk_pos++;
                        }
                     }
                     else
                     {  /* Chunk extends beyond current block - estimate remaining */
                        if(chunk_data_start <= gzip_start)
                        {  /* This chunk contains gzip_start and extends beyond block */
                           total_chunk_data += (hi->blocklength - gzip_start);
                        }
                        else
                        {  /* This chunk is after gzip_start */
                           total_chunk_data += (hi->blocklength - chunk_pos);
                        }
                        break;
                     }
                  }
                  
                  debug_printf("DEBUG: Chunked+gzip: Calculated total_chunk_data=%ld bytes from gzip_start=%ld\n", total_chunk_data, gzip_start);
                  
                  /* Find where gzip data starts in the chunk data */
                  initial_gzip_size = hi->blocklength - gzip_start;
                  if(initial_gzip_size > total_chunk_data)
                  {  initial_gzip_size = total_chunk_data;
                  }
                  
                  /* Allocate buffer for gzip data (use total_chunk_data, but we may need to grow it) */
                  if(total_chunk_data > 0 && total_chunk_data <= gzip_buffer_size)
                  {  gzipbuffer = ALLOCTYPE(UBYTE, total_chunk_data, 0);
                     if(!gzipbuffer)
                     {  debug_printf("DEBUG: Chunked+gzip: Failed to allocate buffer of size %ld\n", total_chunk_data);
                        total_chunk_data = 0;
                     }
                  }
                  else if(total_chunk_data > gzip_buffer_size)
                  {  /* Chunked data is larger than buffer - allocate full size needed */
                     /* Allocate the full total_chunk_data size to avoid truncation */
                     long original_total = total_chunk_data;  /* C89: Declare at start */
                     gzipbuffer = ALLOCTYPE(UBYTE, total_chunk_data, 0);
                     if(!gzipbuffer)
                     {  debug_printf("DEBUG: Chunked+gzip: Failed to allocate buffer of size %ld, trying fallback size %ld\n", total_chunk_data, gzip_buffer_size);
                        /* Fallback to smaller buffer if allocation fails */
                        gzipbuffer = ALLOCTYPE(UBYTE, gzip_buffer_size, 0);
                        if(!gzipbuffer)
                        {  debug_printf("DEBUG: Chunked+gzip: Failed to allocate fallback buffer of size %ld\n", gzip_buffer_size);
                           total_chunk_data = 0;
                        }
                        else
                        {  debug_printf("DEBUG: Chunked+gzip: Using fallback buffer size %ld (original was %ld)\n", gzip_buffer_size, original_total);
                           total_chunk_data = gzip_buffer_size;
                        }
                     }
                     else
                     {  /* Successfully allocated full size - keep total_chunk_data as is */
                        debug_printf("DEBUG: Chunked+gzip: Allocated full buffer size %ld (exceeds default %ld)\n", total_chunk_data, gzip_buffer_size);
                     }
                  }
                  else
                  {  /* Fallback to initial size */
                     if(initial_gzip_size > 0 && initial_gzip_size <= gzip_buffer_size)
                     {  gzipbuffer = ALLOCTYPE(UBYTE, initial_gzip_size, 0);
                        if(!gzipbuffer)
                        {  debug_printf("DEBUG: Chunked+gzip: Failed to allocate initial buffer of size %ld\n", initial_gzip_size);
                           total_chunk_data = 0;
                        }
                        else
                        {  total_chunk_data = initial_gzip_size;
                        }
                     }
                     else if(initial_gzip_size > 0)
                     {  long fallback_size;
                        fallback_size = MIN(initial_gzip_size, gzip_buffer_size);
                        gzipbuffer = ALLOCTYPE(UBYTE, fallback_size, 0);
                        if(!gzipbuffer)
                        {  debug_printf("DEBUG: Chunked+gzip: Failed to allocate fallback buffer of size %ld\n", fallback_size);
                           total_chunk_data = 0;
                        }
                        else
                        {  total_chunk_data = fallback_size;
                        }
                     }
                     else
                     {  debug_printf("DEBUG: Chunked+gzip: No valid size for buffer allocation (total=%ld, initial=%ld)\n", 
                               total_chunk_data, initial_gzip_size);
                        total_chunk_data = 0;
                     }
                  }
                  
                  if(gzipbuffer && total_chunk_data > 0)
                  {  debug_printf("DEBUG: Chunked+gzip: Successfully allocated buffer of size %ld\n", total_chunk_data);
                     /* Update gzip_buffer_size to match actual allocated buffer size */
                     gzip_buffer_size = total_chunk_data;
                  }
                  
                  if(gzipbuffer)
                  {  /* Extract chunk data starting from where gzip magic was found */
                     /* Start from beginning of block and parse chunks until we reach gzip_start */
                     chunk_pos = 0;
                     gziplength = 0;
                     
                     debug_printf("DEBUG: Chunked+gzip: Starting extraction, gzipbuffer=%p, total_chunk_data=%ld, gzip_start=%ld\n", 
                            gzipbuffer, total_chunk_data, gzip_start);
                     
                     /* First, find the chunk that contains gzip_start */
                     while(chunk_pos < gzip_start && chunk_pos < hi->blocklength)
                     {  /* Skip whitespace before chunk size */
                        while(chunk_pos < hi->blocklength && 
                              (hi->fd->block[chunk_pos] == ' ' || hi->fd->block[chunk_pos] == '\t'))
                        {  chunk_pos++;
                        }
                        
                        /* Parse chunk size */
                        chunk_size = 0;
                        while(chunk_pos < hi->blocklength)
                        {  UBYTE c;
                           long digit;
                           
                           c = hi->fd->block[chunk_pos];
                           if(c >= '0' && c <= '9')
                           {  digit = c - '0';
                           }
                           else if(c >= 'A' && c <= 'F')
                           {  digit = c - 'A' + 10;
                           }
                           else if(c >= 'a' && c <= 'f')
                           {  digit = c - 'a' + 10;
                           }
                           else
                           {  break;
                           }
                           chunk_size = chunk_size * 16 + digit;
                           chunk_pos++;
                        }
                        
                        if(chunk_size == 0)
                        {  /* Last chunk - shouldn't happen before gzip_start */
                           break;
                        }
                        
                        /* Skip chunk extension */
                        while(chunk_pos < hi->blocklength && 
                              hi->fd->block[chunk_pos] != '\r' && hi->fd->block[chunk_pos] != '\n')
                        {  chunk_pos++;
                        }
                        
                        /* Skip CRLF after chunk header */
                        if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\r')
                        {  chunk_pos++;
                        }
                        if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\n')
                        {  chunk_pos++;
                        }
                        
                        /* Check if gzip_start is in this chunk's data */
                        if(chunk_pos <= gzip_start && chunk_pos + chunk_size > gzip_start)
                        {  /* Gzip starts in this chunk - copy from gzip_start to end of chunk */
                           long copy_start;
                           long copy_len;
                           long max_copy;
                           long chunk_data_end;
                           long actual_copy;
                           long bytes_in_block;
                           long expected_bytes;
                           
                           copy_start = gzip_start;
                           /* Calculate where this chunk's data ends (before CRLF) */
                           chunk_data_end = chunk_pos + chunk_size;
                           copy_len = chunk_data_end - gzip_start;
                           expected_bytes = chunk_data_end - copy_start;
                           
                           /* Calculate how much we can actually copy (limited by buffer space) */
                           max_copy = total_chunk_data - gziplength;
                           if(copy_len > max_copy)
                           {  copy_len = max_copy;
                           }
                           
                           actual_copy = 0;
                           if(copy_len > 0 && gzipbuffer)
                           {  /* How much of this chunk's data is actually in the current block */
                              bytes_in_block = MIN(chunk_data_end, hi->blocklength) - copy_start;
                              actual_copy = MIN(copy_len, bytes_in_block);
                              
                              if(actual_copy > 0 && gziplength + actual_copy <= total_chunk_data && gzipbuffer)
                              {  memcpy(gzipbuffer + gziplength, hi->fd->block + copy_start, actual_copy);
                                 gziplength += actual_copy;
                                 compressed_bytes_consumed += actual_copy; /* Track compressed data consumed */
                                 debug_printf("DEBUG: Chunked+gzip: Copied %ld bytes from first chunk starting at gzip_start=%ld (chunk_size=%ld, bytes_in_block=%ld, expected=%ld, gziplength now=%ld, max=%ld)\n", 
                                        actual_copy, gzip_start, chunk_size, bytes_in_block, expected_bytes, gziplength, total_chunk_data);
                                 
                                 /* If chunk extends beyond block, we need to continue it in next block */
                                 /* Don't advance past chunk boundary if chunk wasn't fully copied */
                                 if(bytes_in_block < expected_bytes)
                                 {  /* Chunk continues in next block - leave block position at end of data we copied */
                                    debug_printf("DEBUG: Chunked+gzip: First chunk extends beyond block (%ld of %ld bytes), will continue in next block\n",
                                           bytes_in_block, expected_bytes);
                                    /* Break out - the remaining chunk data will be handled when we read more blocks */
                                    break;
                                 }
                              }
                              else
                              {  debug_printf("DEBUG: Chunked+gzip: WARNING - Failed to copy first chunk: actual_copy=%ld, bytes_in_block=%ld, gziplength=%ld, total=%ld, gzipbuffer=%p\n",
                                        actual_copy, bytes_in_block, gziplength, total_chunk_data, gzipbuffer);
                              }
                           }
                           else
                           {  debug_printf("DEBUG: Chunked+gzip: WARNING - First chunk copy conditions failed: copy_len=%ld, gziplength=%ld, total=%ld, gzipbuffer=%p\n",
                                     copy_len, gziplength, total_chunk_data, gzipbuffer);
                           }
                           
                           /* Move to next chunk only if we copied all data from this chunk */
                           if(actual_copy >= expected_bytes)
                           {  chunk_pos = chunk_data_end;
                              
                              /* Skip CRLF after chunk data */
                              if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\r')
                              {  chunk_pos++;
                              }
                              if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\n')
                              {  chunk_pos++;
                              }
                              
                              /* Now extract all remaining chunks */
                              break;
                           }
                           else
                           {  /* Chunk continues in next block - stop extraction for now */
                              break;
                           }
                        }
                        else if(chunk_pos + chunk_size <= gzip_start)
                        {  /* This chunk is before gzip_start - skip it */
                           chunk_pos += chunk_size;
                           
                           /* Skip CRLF after chunk data */
                           if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\r')
                           {  chunk_pos++;
                           }
                           if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\n')
                           {  chunk_pos++;
                           }
                        }
                        else
                        {  /* Should not happen */
                           break;
                        }
                     }
                     
                     /* Now extract all remaining chunks */
                     while(chunk_pos < hi->blocklength && gziplength < total_chunk_data)
                     {  /* Skip whitespace before chunk size */
                        while(chunk_pos < hi->blocklength && 
                              (hi->fd->block[chunk_pos] == ' ' || hi->fd->block[chunk_pos] == '\t'))
                        {  chunk_pos++;
                        }
                        
                        /* Parse chunk size */
                        chunk_size = 0;
                        while(chunk_pos < hi->blocklength)
                        {  UBYTE c;
                           long digit;
                           
                           c = hi->fd->block[chunk_pos];
                           if(c >= '0' && c <= '9')
                           {  digit = c - '0';
                           }
                           else if(c >= 'A' && c <= 'F')
                           {  digit = c - 'A' + 10;
                           }
                           else if(c >= 'a' && c <= 'f')
                           {  digit = c - 'a' + 10;
                           }
                           else
                           {  break;
                           }
                           chunk_size = chunk_size * 16 + digit;
                           chunk_pos++;
                        }
                        
                        if(chunk_size == 0)
                        {  /* Last chunk */
                           break;
                        }
                        
                        /* Skip chunk extension */
                        while(chunk_pos < hi->blocklength && 
                              hi->fd->block[chunk_pos] != '\r' && hi->fd->block[chunk_pos] != '\n')
                        {  chunk_pos++;
                        }
                        
                        /* Skip CRLF after chunk header */
                        if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\r')
                        {  chunk_pos++;
                        }
                        if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\n')
                        {  chunk_pos++;
                        }
                        
                        /* Copy chunk data (may be partial if chunk is larger than buffer) */
                        if(chunk_size > 0 && gzipbuffer)
                        {  long max_copy;
                           long actual_copy;
                           
                           /* Calculate how much we can copy (limited by remaining buffer space) */
                           max_copy = total_chunk_data - gziplength;
                           if(max_copy <= 0)
                           {  /* Buffer is full */
                              debug_printf("DEBUG: Chunked+gzip: Buffer full (gziplength=%ld >= total=%ld), stopping extraction\n", 
                                     gziplength, total_chunk_data);
                              break;
                           }
                           
                           if(chunk_size > max_copy)
                           {  /* Chunk is larger than remaining buffer - copy what we can */
                              actual_copy = MIN(max_copy, hi->blocklength - chunk_pos);
                              if(actual_copy > 0)
                              {  memcpy(gzipbuffer + gziplength, hi->fd->block + chunk_pos, actual_copy);
                                 gziplength += actual_copy;
                                 compressed_bytes_consumed += actual_copy; /* Track compressed data consumed */
                                 debug_printf("DEBUG: Chunked+gzip: Copied %ld bytes from chunk (size=%ld, partial copy, total=%ld, pos=%ld)\n", 
                                        actual_copy, chunk_size, gziplength, chunk_pos);
                              }
                              /* Buffer is now full - stop extraction */
                              break;
                           }
                           else
                           {  /* Chunk fits in remaining buffer */
                              actual_copy = MIN(chunk_size, hi->blocklength - chunk_pos);
                              if(actual_copy > 0 && gziplength + actual_copy <= total_chunk_data && gzipbuffer)
                              {  memcpy(gzipbuffer + gziplength, hi->fd->block + chunk_pos, actual_copy);
                                 gziplength += actual_copy;
                                 compressed_bytes_consumed += actual_copy; /* Track compressed data consumed */
                                 debug_printf("DEBUG: Chunked+gzip: Copied %ld bytes from chunk (size=%ld, total=%ld, pos=%ld)\n", 
                                        actual_copy, chunk_size, gziplength, chunk_pos);
                              }
                              else if(actual_copy < chunk_size)
                              {  /* Chunk extends beyond block - copy what we have */
                                 if(actual_copy > 0 && gziplength + actual_copy <= total_chunk_data && gzipbuffer)
                                 {  memcpy(gzipbuffer + gziplength, hi->fd->block + chunk_pos, actual_copy);
                                    gziplength += actual_copy;
                                    compressed_bytes_consumed += actual_copy; /* Track compressed data consumed */
                                    debug_printf("DEBUG: Chunked+gzip: Copied partial chunk %ld bytes (extends beyond block, size=%ld, total=%ld)\n", 
                                           actual_copy, chunk_size, gziplength);
                                 }
                                 /* Chunk extends beyond block - we'll read more later */
                              }
                           }
                           
                           /* Check if we've filled the buffer */
                           if(gziplength >= total_chunk_data)
                           {  debug_printf("DEBUG: Chunked+gzip: Buffer full (gziplength=%ld >= total=%ld), stopping extraction\n", 
                                     gziplength, total_chunk_data);
                              /* Buffer full - stop extraction, but still need to advance past chunk */
                              /* Check if we copied the full chunk or just part */
                              if(chunk_size > max_copy)
                              {  /* Chunk larger than buffer - we copied actual_copy, buffer is full */
                                 /* Advance by what we copied, keep remaining for next block read */
                                 chunk_pos += actual_copy;
                                 /* Don't skip CRLF - chunk continues in next block */
                              }
                              else
                              {  /* Chunk fits in buffer - check if we copied all of it */
                                 if(actual_copy >= chunk_size)
                                 {  /* Full chunk copied - advance past chunk and CRLF */
                                    chunk_pos += chunk_size;
                                    
                                    /* Skip CRLF after chunk data */
                                    if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\r')
                                    {  chunk_pos++;
                                    }
                                    if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\n')
                                    {  chunk_pos++;
                                    }
                                 }
                                 else
                                 {  /* Partial chunk copied (extends beyond block) - advance by what we copied */
                                    /* Keep remaining chunk data in block for next iteration */
                                    chunk_pos += actual_copy;
                                    /* Don't skip CRLF yet - we haven't reached end of chunk */
                                 }
                              }
                              break;
                           }
                           
                           /* Move past chunk data - CRITICAL: Only advance by what we actually copied */
                           /* If we copied less than chunk_size (chunk extends beyond block), keep remaining data */
                           if(chunk_size > max_copy)
                           {  /* Chunk larger than remaining buffer space - we copied actual_copy */
                              /* Advance by what we copied, but buffer is not full yet so continue */
                              chunk_pos += actual_copy;
                              /* Don't skip CRLF - chunk continues, will be handled in next iteration */
                              /* Note: This case should be rare since we check max_copy above */
                           }
                           else
                           {  /* Chunk fits in remaining buffer - check if we copied all of it */
                              if(actual_copy >= chunk_size)
                              {  /* Full chunk copied - advance past chunk and CRLF */
                                 chunk_pos += chunk_size;
                                 
                                 /* Skip CRLF after chunk data */
                                 if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\r')
                                 {  chunk_pos++;
                                 }
                                 if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\n')
                                 {  chunk_pos++;
                                 }
                              }
                              else
                              {  /* Partial chunk copied (extends beyond block) - advance by what we copied */
                                 /* Keep remaining chunk data in block for next iteration */
                                 chunk_pos += actual_copy;
                                 /* Don't skip CRLF yet - we haven't reached end of chunk */
                              }
                           }
                        }
                        else if(chunk_size > 0)
                        {  debug_printf("DEBUG: Chunked+gzip: WARNING - Skipping chunk copy: chunk_size=%ld, gziplength=%ld, total=%ld, gzipbuffer=%p\n",
                                  chunk_size, gziplength, total_chunk_data, gzipbuffer);
                           /* Skip this chunk since we can't copy it */
                           chunk_pos += chunk_size;
                           
                           /* Skip CRLF after chunk data */
                           if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\r')
                           {  chunk_pos++;
                           }
                           if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\n')
                           {  chunk_pos++;
                           }
                        }
                     }
                     
                     debug_printf("DEBUG: Chunked+gzip: Extracted %ld bytes of chunk data (gzip_start=%ld, total_chunk_data=%ld)\n", gziplength, gzip_start, total_chunk_data);
                  }
                  else
                  {  debug_printf("DEBUG: Failed to allocate gzip buffer for chunked data\n");
                     hi->flags &= ~HTTPIF_GZIPENCODED;
                     hi->flags &= ~HTTPIF_GZIPDECODING;
                     gzip_end = 1;
                     break;
                  }
               }
               else
               {  /* Non-chunked: Only copy the actual gzip data, skip any prefix */
                gziplength = hi->blocklength - gzip_start;
                
                  /* Track total compressed bytes read from network */
                  /* For Content-Length validation, only count actual compressed gzip data, not any prefix */
                  /* gziplength is the actual compressed data size (blocklength minus any prefix) */
                  total_compressed_read = gziplength;
                  
                  /* For non-chunked gzip, allocate full buffer size since we don't know total size */
                  /* This prevents buffer overflow when appending more data later */
                  if(gziplength > 0 && gziplength <= hi->blocklength) {
                      gzipbuffer = ALLOCTYPE(UBYTE, gzip_buffer_size, 0);
                    if(gzipbuffer) {
                        memcpy(gzipbuffer, hi->fd->block + gzip_start, gziplength);
                          compressed_bytes_consumed += gziplength; /* Track compressed data consumed */
                    } else {
                        debug_printf("DEBUG: Failed to allocate gzip buffer, disabling gzip\n");
                        hi->flags &= ~HTTPIF_GZIPENCODED;
                        hi->flags &= ~HTTPIF_GZIPDECODING;
                        gzip_end = 1;
                        break;
                    }
                } else {
                    debug_printf("DEBUG: Invalid gziplength %ld, disabling gzip\n", gziplength);
                    hi->flags &= ~HTTPIF_GZIPENCODED;
                    hi->flags &= ~HTTPIF_GZIPDECODING;
                    gzip_end = 1;
                    break;
                }
               }
            }
            else if(gzip_start < 0)
            {  /* No gzip magic found at all - check if data might be gzip anyway */
               /* For non-chunked, check if it starts with gzip magic */
               if(hi->blocklength >= 3 && hi->fd->block[0] == 0x1F && hi->fd->block[1] == 0x8B && hi->fd->block[2] == 0x08)
               {  /* Gzip magic at start - treat as gzip_start=0 */
                  gzip_start = 0;
                  debug_printf("DEBUG: Found gzip magic at start of block\n");
                  gziplength = hi->blocklength;
                  
                  /* Track total compressed bytes read from network */
                  /* For Content-Length validation, count actual compressed gzip data */
                  total_compressed_read = gziplength;
                  
                  /* For non-chunked gzip, allocate full buffer size since we don't know total size */
                  /* This prevents buffer overflow when appending more data later */
                  if(gziplength > 0 && gziplength <= gzip_buffer_size) {
                      gzipbuffer = ALLOCTYPE(UBYTE, gzip_buffer_size, 0);
                    if(gzipbuffer) {
                        memcpy(gzipbuffer, hi->fd->block, gziplength);
                          compressed_bytes_consumed += gziplength; /* Track compressed data consumed */
                    } else {
                        debug_printf("DEBUG: Failed to allocate gzip buffer, disabling gzip\n");
                        hi->flags &= ~HTTPIF_GZIPENCODED;
                        hi->flags &= ~HTTPIF_GZIPDECODING;
                        gzip_end = 1;
                        break;
                    }
                } else {
                      debug_printf("DEBUG: Invalid gziplength %ld, disabling gzip\n", gziplength);
                    hi->flags &= ~HTTPIF_GZIPENCODED;
                    hi->flags &= ~HTTPIF_GZIPDECODING;
                    gzip_end = 1;
                    break;
                }
               }
               else
               {  /* No gzip magic found anywhere */
                  debug_printf("DEBUG: No gzip magic found in block, disabling gzip\n");
                  hi->flags &= ~HTTPIF_GZIPENCODED;
                  hi->flags &= ~HTTPIF_GZIPDECODING;
                  continue;
               }
            }
            
            /* Verify we have valid gzip magic before starting */
            /* For chunked encoding, gzip magic might be in a later chunk, so we need to check */
            /* accumulated data or trust Content-Encoding header */
            if(!gzipbuffer || gziplength < 3)
            {  if(hi->flags & HTTPIF_CHUNKED)
               {  /* Chunked encoding - might need more chunks to find gzip magic */
                  debug_printf("DEBUG: Chunked+gzip: Only %ld bytes accumulated, need more chunks to find gzip magic\n", gziplength);
                  /* Don't disable gzip yet - wait for more chunks */
                  /* Read more data and try again */
                  if(!Readblock(hi))
                  {  /* No more data - disable gzip */
                     debug_printf("DEBUG: Chunked+gzip: No more data available, disabling gzip\n");
                     if(gzipbuffer) FREE(gzipbuffer);
                     gzipbuffer = NULL;
                     hi->flags &= ~HTTPIF_GZIPENCODED;
                     hi->flags &= ~HTTPIF_GZIPDECODING;
                     continue;
                  }
                  /* More data read, continue loop to process it */
                  continue;
               }
               else
               {  /* Non-chunked - should have gzip magic by now */
                  debug_printf("DEBUG: No valid gzip magic found (gziplength=%ld), disabling gzip\n", gziplength);
                  if(gzipbuffer) FREE(gzipbuffer);
                  gzipbuffer = NULL;
                  hi->flags &= ~HTTPIF_GZIPENCODED;
                  hi->flags &= ~HTTPIF_GZIPDECODING;
                  continue;
               }
            }
            
            /* Check for gzip magic in accumulated data */
            if(gzipbuffer[0] != 0x1F || gzipbuffer[1] != 0x8B || gzipbuffer[2] != 0x08)
            {  if(hi->flags & HTTPIF_CHUNKED)
               {  /* Chunked encoding - gzip magic might be in a later chunk */
                  /* Trust Content-Encoding header and try decompression anyway */
                  /* Or search for gzip magic in accumulated data */
                  long i;
                  BOOL found_magic = FALSE;
                  for(i = 0; i <= gziplength - 3 && i >= 0; i++)
                  {  if(gzipbuffer[i] == 0x1F && gzipbuffer[i+1] == 0x8B && gzipbuffer[i+2] == 0x08)
                     {  debug_printf("DEBUG: Chunked+gzip: Found gzip magic at offset %ld in accumulated data\n", i);
                        /* Move gzip data to start of buffer, skipping prefix */
                        if(i > 0 && (gziplength - i) > 0 && (gziplength - i) <= gzip_buffer_size)
                        {  memmove(gzipbuffer, gzipbuffer + i, gziplength - i);
                           gziplength -= i;
                        }
                        found_magic = TRUE;
                        break;
                     }
                  }
                  if(!found_magic)
                  {  /* No gzip magic found yet - might be in later chunks */
                     debug_printf("DEBUG: Chunked+gzip: No gzip magic in %ld bytes of accumulated data (first 3 bytes: %02X %02X %02X), waiting for more chunks\n",
                            gziplength,
                            gzipbuffer[0], gzipbuffer[1], gzipbuffer[2]);
                     /* Read more data and try again */
                     if(!Readblock(hi))
                     {  /* No more data - trust Content-Encoding and try anyway, or disable */
                        debug_printf("DEBUG: Chunked+gzip: No more data, but Content-Encoding says gzip - trusting header and attempting decompression\n");
                        /* Trust Content-Encoding header and proceed */
                     }
                     else
                     {  /* More data read, continue loop to process it */
                        continue;
                     }
                  }
               }
               else
               {  /* Non-chunked - should have gzip magic at start */
                  debug_printf("DEBUG: No valid gzip magic found (first 3 bytes: %02X %02X %02X), disabling gzip\n",
                         gzipbuffer[0], gzipbuffer[1], gzipbuffer[2]);
                  if(gzipbuffer) FREE(gzipbuffer);
                  gzipbuffer = NULL;
                  hi->flags &= ~HTTPIF_GZIPENCODED;
                  hi->flags &= ~HTTPIF_GZIPDECODING;
                  continue;
               }
            }
            
            hi->flags|=HTTPIF_GZIPDECODING;
            debug_printf("DEBUG: Starting gzip decompression, blocklength=%ld, gziplength=%ld\n", hi->blocklength, gziplength);
            
            /* Initialize zlib for gzip decompression */
            d_stream.zalloc=Z_NULL;
            d_stream.zfree=Z_NULL;
            d_stream.opaque=Z_NULL;
            d_stream.avail_in=0;
            d_stream.next_in=Z_NULL;
            d_stream.avail_out=0;
            d_stream.next_out=Z_NULL;
            
            /* Validate zlib stream structure before initialization */
            if((ULONG)&d_stream < 0x1000 || (ULONG)&d_stream > 0xFFFFFFF0) {
               debug_printf("DEBUG: Invalid zlib stream pointer 0x%08lX\n", (ULONG)&d_stream);
               if(gzipbuffer) FREE(gzipbuffer);
               gzipbuffer = NULL;
               hi->flags &= ~HTTPIF_GZIPENCODED;
               hi->flags &= ~HTTPIF_GZIPDECODING;
               gzip_end = 1;
               break;
            }
            
            err=inflateInit2(&d_stream,16+15); // set zlib to expect 'gzip-header'
            if(err!=Z_OK) {
               debug_printf("DEBUG: zlib Init Fail: %d\n", err);
               if(gzipbuffer) FREE(gzipbuffer);
               gzipbuffer = NULL;
               hi->flags &= ~HTTPIF_GZIPENCODED;
               hi->flags &= ~HTTPIF_GZIPDECODING;
               gzip_end = 1;
               d_stream_initialized = FALSE;
               break;
            } else {
               debug_printf("DEBUG: zlib init successful\n");
               d_stream_initialized = TRUE; /* Mark d_stream as initialized */
            }
            
                     /* Validate gzip buffer allocation to prevent heap corruption */
         if(gzipbuffer && gziplength > 0) {
            /* Check if gzipbuffer pointer is valid */
            if((ULONG)gzipbuffer < 0x1000 || (ULONG)gzipbuffer > 0xFFFFFFF0) {
               debug_printf("DEBUG: Invalid gzipbuffer pointer 0x%08lX\n", (ULONG)gzipbuffer);
               FREE(gzipbuffer);
               gzipbuffer = NULL;
                  inflateEnd(&d_stream);
               hi->flags &= ~HTTPIF_GZIPENCODED;
               hi->flags &= ~HTTPIF_GZIPDECODING;
               gzip_end = 1;
               break;
            }
            
            /* Check if gziplength is reasonable */
            if(gziplength > gzip_buffer_size || gziplength <= 0) {
               debug_printf("DEBUG: Invalid gziplength: %ld (max: %ld)\n", gziplength, gzip_buffer_size);
               FREE(gzipbuffer);
               gzipbuffer = NULL;
                  inflateEnd(&d_stream);
               hi->flags &= ~HTTPIF_GZIPENCODED;
               hi->flags &= ~HTTPIF_GZIPDECODING;
               gzip_end = 1;
               break;
            }
            
            d_stream.next_in=gzipbuffer;
            d_stream.avail_in=gziplength;
            
            /* Validate blocksize before setting output buffer to prevent overflow */
            if(hi->fd->blocksize > 0 && hi->fd->blocksize <= INPUTBLOCKSIZE) {
                d_stream.next_out=hi->fd->block;
                d_stream.avail_out=hi->fd->blocksize;
                debug_printf("DEBUG: Setting zlib output buffer to %ld bytes\n", hi->fd->blocksize);
            } else {
                debug_printf("DEBUG: Invalid blocksize %ld, using safe default\n", hi->fd->blocksize);
                d_stream.next_out=hi->fd->block;
                d_stream.avail_out=INPUTBLOCKSIZE;
                /* Reset corrupted blocksize to prevent memory corruption */
                hi->fd->blocksize = INPUTBLOCKSIZE;
            }
                
                if(httpdebug)
                {  debug_printf("DEBUG: First 16 bytes of gzip data: ");
                   for(i = 0; i < MIN(16, gziplength); i++) {
                       printf("%02X ", gzipbuffer[i]);
                   }
                   printf("\n");
                }
                
                /* Verify this is actually gzip data */
                if(gziplength >= 3 && gzipbuffer[0] == 0x1F && gzipbuffer[1] == 0x8B && gzipbuffer[2] == 0x08) {
                   debug_printf("DEBUG: Valid gzip header confirmed, gziplength=%ld, avail_in=%lu\n", 
                          gziplength, d_stream.avail_in);
                } else {
                   debug_printf("DEBUG: WARNING: Data still doesn't start with gzip magic! Disabling gzip.\n");
                   FREE(gzipbuffer);
                   gzipbuffer = NULL;
                   inflateEnd(&d_stream);
                   hi->flags &= ~HTTPIF_GZIPENCODED;
                   hi->flags &= ~HTTPIF_GZIPDECODING;
                   gzip_end = 1;
                   break;
                }
            } else {
                debug_printf("DEBUG: No gzip data to process\n");
                hi->flags &= ~HTTPIF_GZIPENCODED;
                hi->flags &= ~HTTPIF_GZIPDECODING;
                continue;
            }
            
            /* After copying data to gzipbuffer, clear block to prevent reprocessing */
            /* For chunked encoding, we've already extracted and copied the gzip data */
            /* For non-chunked, we've copied all data to gzipbuffer */
            /* Either way, we don't want to process this block again as regular data */
            hi->blocklength = 0;
            
            /* Set the decoding flag so we can process gzip */
            hi->flags |= HTTPIF_GZIPDECODING;
            debug_printf("DEBUG: Set HTTPIF_GZIPDECODING flag (0x%04X), gzipbuffer=%p, gziplength=%ld, avail_in=%lu, d_stream_initialized=%d\n", 
                   hi->flags, gzipbuffer, gziplength, d_stream.avail_in, d_stream_initialized);
            
            /* Now process the gzip data immediately */
         }

         /* Process gzip data if flag is set */
         if(hi->flags & HTTPIF_GZIPDECODING && gzipbuffer != NULL && d_stream_initialized)
         {  /* Process gzip data immediately */
            debug_printf("DEBUG: Processing gzip data inside main loop, avail_in=%lu\n", d_stream.avail_in);
            
            /* Process ALL gzip data in this loop to avoid duplicate processing */
            /* Process gzip completely in the main loop - this prevents the second loop from running */
            while(!gzip_end && hi->flags & HTTPIF_GZIPDECODING && gzipbuffer != NULL && d_stream_initialized)
            {  long decompressed_len;
               
               /* Only call inflate if we have input */
               if(d_stream.avail_in > 0)
               {  err=inflate(&d_stream,Z_SYNC_FLUSH);
               }
               else
               {  /* No input - need to read more data */
                  err=Z_OK; /* Signal that we need more input */
               }
               
               if(err==Z_BUF_ERROR || err==Z_OK)
               {  /* Output buffer full or OK - process decompressed data */
                  decompressed_len=hi->fd->blocksize-d_stream.avail_out;
                  if(decompressed_len > 0 && decompressed_len <= hi->fd->blocksize)
                  {  UBYTE *decompressed_copy;
                     
                     debug_printf("DEBUG: Processing %ld bytes of decompressed data\n", decompressed_len);
                     
                     /* CRITICAL: Copy decompressed data before passing to Updatetaskattrs()
                      * because Updatetaskattrs() may pass the buffer to another task asynchronously,
                      * and we immediately reuse hi->fd->block for the next decompression cycle.
                      * Without copying, this causes memory corruption when the other task reads
                      * from the buffer while zlib is writing to it. */
                     decompressed_copy = ALLOCTYPE(UBYTE, decompressed_len, 0);
                     if(decompressed_copy != NULL)
                     {  memcpy(decompressed_copy, hi->fd->block, decompressed_len);
                        
                        /* Ensure parttype is null-terminated before calling strlen() to prevent bounds check errors */
                        hi->parttype[sizeof(hi->parttype) - 1] = '\0';
                        if(hi->parttype[0] != '\0' && hi->parttype[0] != 0) {
                           Updatetaskattrs(
                              AOURL_Data,decompressed_copy,
                              AOURL_Datalength,decompressed_len,
                              AOURL_Contenttype,hi->parttype,
                              TAG_END);
                        } else {
                           Updatetaskattrs(
                              AOURL_Data,decompressed_copy,
                              AOURL_Datalength,decompressed_len,
                              TAG_END);
                        }
                        /* Note: We don't free decompressed_copy here - the receiving task will free it
                         * after processing, or it will be freed when the task completes. */
                     }
                     else
                     {  debug_printf("DEBUG: ERROR - Failed to allocate memory for decompressed data copy, skipping chunk\n");
                        /* If allocation fails, we can't safely pass the data, so skip this chunk */
                     }
                     
                     /* Now safe to reuse the buffer for next decompression cycle */
                     d_stream.next_out=hi->fd->block;
                     d_stream.avail_out=hi->fd->blocksize;
                  }
                  if(err==Z_OK && d_stream.avail_in==0)
                  {  /* Need more input - read next block */
                     if(!Readblock(hi))
                     {  debug_printf("DEBUG: No more data for gzip, ending\n");
                        gzip_end=1;
                        break;
                     }
                     /* Update total compressed bytes read from network */
                     total_compressed_read += hi->blocklength;
                     
                     /* Continue loop to process the newly read data */
                     /* For non-chunked, append new block data to gzipbuffer */
                     /* For chunked, we need to extract chunk data and add to gzipbuffer */
                     if(!(hi->flags & HTTPIF_CHUNKED))
                     {  /* Non-chunked: Append all new block data to gzipbuffer */
                        long remaining_space;
                        long bytes_to_append;
                        long used_space;
                        
                        /* Calculate space used in buffer (from start to next_in + remaining) */
                        used_space = (d_stream.next_in - gzipbuffer) + d_stream.avail_in;
                        remaining_space = gzip_buffer_size - used_space;
                        
                        if(remaining_space <= 0 || hi->blocklength > remaining_space)
                        {  debug_printf("DEBUG: Non-chunked gzip: Buffer full or block too large (remaining=%ld, block=%ld), cannot continue\n",
                                  remaining_space, hi->blocklength);
                           gzip_end = 1;
                           break;
                        }
                        
                        bytes_to_append = MIN(hi->blocklength, remaining_space);
                        if(bytes_to_append > 0 && gziplength + bytes_to_append <= gzip_buffer_size)
                        {  /* Move unprocessed data to start if needed */
                           if(d_stream.next_in != gzipbuffer && d_stream.avail_in > 0)
                           {  memmove(gzipbuffer, d_stream.next_in, d_stream.avail_in);
                              gziplength = d_stream.avail_in;
                           }
                           else if(d_stream.avail_in == 0)
                           {  gziplength = 0;
                           }
                           
                           /* Append new data */
                           memcpy(gzipbuffer + gziplength, hi->fd->block, bytes_to_append);
                           gziplength += bytes_to_append;
                           compressed_bytes_consumed += bytes_to_append;
                           
                           /* Update zlib stream */
                           d_stream.next_in = gzipbuffer;
                           d_stream.avail_in = gziplength;
                           
                           debug_printf("DEBUG: Non-chunked gzip: Added %ld bytes from new block (total in buffer=%ld)\n",
                                  bytes_to_append, gziplength);
                           
                           /* Clear block for next read */
                           hi->blocklength = 0;
                           
                           /* Continue loop to process the newly added data */
            continue;
                        }
                     }
                     else if(hi->flags & HTTPIF_CHUNKED)
                     {  /* Extract chunk data from new block and append to gzipbuffer */
                        UBYTE *chunk_p;
                        long chunk_pos;
                        long chunk_size;
                        long new_data_start;
                        long remaining_space;
                        long available_space;
                        long remaining_unprocessed;
                        long used_space;
                        long bytes_to_move;
                        UBYTE first_char;
                        BOOL is_continuation;
                        
                        chunk_p = hi->fd->block;
                        chunk_pos = 0;
                        new_data_start = 0;
                        
                        /* Check if this block is continuing a chunk from previous block */
                        /* If block doesn't start with hex digits (chunk header), it's continuation of previous chunk */
                        is_continuation = FALSE;
                        if(hi->blocklength > 0)
                        {  first_char = hi->fd->block[0];
                           /* Chunk headers start with hex digits (0-9, A-F, a-f) */
                           /* If first char is not hex digit, this is continuation of previous chunk data */
                           if(!((first_char >= '0' && first_char <= '9') ||
                                (first_char >= 'A' && first_char <= 'F') ||
                                (first_char >= 'a' && first_char <= 'f')))
                           {  is_continuation = TRUE;
                              debug_printf("DEBUG: Chunked+gzip: Block starts with non-hex char (0x%02X), treating as continuation of previous chunk\n", first_char);
                           }
                        }
                        
                        if(is_continuation)
                        {  /* This block continues the previous chunk - copy directly to gzipbuffer */
                           /* Calculate available space and copy what we can */
                           remaining_unprocessed = d_stream.avail_in;
                           
                           /* Validate remaining_unprocessed is reasonable */
                           if(remaining_unprocessed < 0 || remaining_unprocessed > gzip_buffer_size)
                           {  debug_printf("DEBUG: Invalid remaining_unprocessed (%ld) in continuation, resetting to 0\n", remaining_unprocessed);
                              remaining_unprocessed = 0;
                           }
                           
                           if(remaining_unprocessed > 0)
                           {  /* Validate next_in pointer before pointer arithmetic */
                              if(gzipbuffer && d_stream.next_in >= gzipbuffer && 
                                 d_stream.next_in < gzipbuffer + gzip_buffer_size)
                              {  used_space = (d_stream.next_in - gzipbuffer) + remaining_unprocessed;
                                 /* Validate used_space is reasonable */
                                 if(used_space < 0 || used_space > gzip_buffer_size)
                                 {  debug_printf("DEBUG: Invalid used_space (%ld) in continuation, resetting\n", used_space);
                                    used_space = remaining_unprocessed; /* Fallback */
                                    if(used_space > gzip_buffer_size) used_space = gzip_buffer_size;
                                 }
                              }
                              else
                              {  debug_printf("DEBUG: Invalid d_stream.next_in pointer (%p) in continuation, resetting\n", d_stream.next_in);
                                 used_space = remaining_unprocessed; /* Fallback */
                                 if(used_space > gzip_buffer_size) used_space = gzip_buffer_size;
                              }
                           }
                           else
                           {  used_space = 0;
                           }
                           
                           remaining_space = gzip_buffer_size - used_space;
                           if(remaining_space <= 0)
                           {  debug_printf("DEBUG: Chunked+gzip: Buffer full, cannot add continuation data\n");
                              gzip_end = 1;
                              break;
                           }
                           
                           /* Move unprocessed data to start if needed */
                           /* gziplength tracks where to append new data (after any unprocessed data) */
                           if(remaining_unprocessed > 0 && d_stream.next_in != gzipbuffer)
                           {  bytes_to_move = remaining_unprocessed;
                              /* Validate all pointers and sizes before memmove */
                              if(bytes_to_move > gzip_buffer_size)
                              {  debug_printf("DEBUG: bytes_to_move (%ld) exceeds buffer size (%ld) in continuation, resetting\n",
                                        bytes_to_move, gzip_buffer_size);
                                 gziplength = 0;
                                 remaining_unprocessed = 0;
                              }
                              else if(bytes_to_move > 0 && gzipbuffer && d_stream.next_in >= gzipbuffer && 
                                      d_stream.next_in < gzipbuffer + gzip_buffer_size &&
                                      (d_stream.next_in + bytes_to_move) <= gzipbuffer + gzip_buffer_size &&
                                      bytes_to_move <= remaining_unprocessed)
                              {  memmove(gzipbuffer, d_stream.next_in, bytes_to_move);
                                 gziplength = bytes_to_move; /* Position after unprocessed data */
                              }
                              else
                              {  debug_printf("DEBUG: Invalid pointer state for memmove in continuation (next_in=%p, gzipbuffer=%p, bytes=%ld, buffer_size=%ld), resetting\n",
                                        d_stream.next_in, gzipbuffer, bytes_to_move, gzip_buffer_size);
                                 gziplength = 0;
                                 remaining_unprocessed = 0;
                              }
                           }
                           else if(remaining_unprocessed == 0)
                           {  /* All data was consumed - start fresh at beginning */
                              gziplength = 0;
                           }
                           else
                           {  /* remaining_unprocessed > 0 but next_in == gzipbuffer - data already at start */
                              gziplength = remaining_unprocessed; /* Position after unprocessed data */
                           }
                           
                           /* Copy continuation data to buffer */
                           available_space = MIN(hi->blocklength, remaining_space);
                           if(gziplength + available_space <= gzip_buffer_size)
                           {  /* Validate pointers before memcpy */
                              if(gzipbuffer && hi->fd && hi->fd->block &&
                                 gziplength >= 0 && gziplength < gzip_buffer_size &&
                                 available_space > 0 && available_space <= (gzip_buffer_size - gziplength) &&
                                 hi->blocklength > 0 && available_space <= hi->blocklength)
                              {  memcpy(gzipbuffer + gziplength, hi->fd->block, available_space);
                                 gziplength += available_space; /* Total data in buffer now */
                                 compressed_bytes_consumed += available_space; /* Track compressed data consumed */
                              }
                              else
                              {  debug_printf("DEBUG: Invalid pointers for memcpy in continuation (gzipbuffer=%p, block=%p, gziplength=%ld, available_space=%ld, blocklength=%ld), aborting\n",
                                        gzipbuffer, hi->fd ? hi->fd->block : NULL, gziplength, available_space, hi->blocklength);
                                 gzip_end = 1;
                                 break;
                              }
                              
                              /* CRITICAL FIX: Set next_in to start of unprocessed data, not start of buffer */
                              /* If we had unprocessed data, it's at the start; new data is after it */
                              if(remaining_unprocessed > 0)
                              {  /* Unprocessed data is at start, new data is after it */
                                 d_stream.next_in = gzipbuffer; /* Start of unprocessed data */
                                 d_stream.avail_in = remaining_unprocessed + available_space; /* Total unprocessed */
                              }
                              else
                              {  /* No unprocessed data - all new data is unprocessed */
                                 d_stream.next_in = gzipbuffer; /* Start of buffer */
                                 d_stream.avail_in = available_space; /* Only new data is unprocessed */
                              }
                              
                              debug_printf("DEBUG: Chunked+gzip: Added %ld bytes of chunk continuation (gziplength=%ld, remaining_unprocessed was=%ld, avail_in=%lu)\n",
                                     available_space, gziplength, remaining_unprocessed, d_stream.avail_in);
                              
                              /* Clear block - we've used all continuation data */
                              hi->blocklength = 0;
                              
                              /* Continue decompression */
                              continue;
                           }
                           else
                           {  debug_printf("DEBUG: Chunked+gzip: Not enough space for continuation (%ld bytes)\n", hi->blocklength);
                              gzip_end = 1;
                              break;
                           }
                        }
                        
                        /* Find start of next chunk data (new chunk header) */
                        while(chunk_pos < hi->blocklength)
                        {  /* Skip whitespace */
                           while(chunk_pos < hi->blocklength && 
                                 (hi->fd->block[chunk_pos] == ' ' || hi->fd->block[chunk_pos] == '\t'))
                           {  chunk_pos++;
                           }
                           
                           /* Parse chunk size */
                           chunk_size = 0;
                           while(chunk_pos < hi->blocklength)
                           {  UBYTE c;
                              long digit;
                              
                              c = hi->fd->block[chunk_pos];
                              if(c >= '0' && c <= '9')
                              {  digit = c - '0';
                              }
                              else if(c >= 'A' && c <= 'F')
                              {  digit = c - 'A' + 10;
                              }
                              else if(c >= 'a' && c <= 'f')
                              {  digit = c - 'a' + 10;
                              }
                              else
                              {  break;
                              }
                              chunk_size = chunk_size * 16 + digit;
                              chunk_pos++;
                           }
                           
                           if(chunk_size == 0)
                           {  /* Last chunk */
                              gzip_end = 1;
                              break;
                           }
                           
                           /* Skip chunk extension */
                           while(chunk_pos < hi->blocklength && 
                                 hi->fd->block[chunk_pos] != '\r' && hi->fd->block[chunk_pos] != '\n')
                           {  chunk_pos++;
                           }
                           
                           /* Skip CRLF after chunk header */
                           if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\r')
                           {  chunk_pos++;
                           }
                           if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\n')
                           {  chunk_pos++;
                           }
                           
                           /* Calculate remaining unprocessed data and available space */
                           /* If avail_in > 0, we have unprocessed data starting at next_in */
                           /* If avail_in == 0, we've consumed all data and can reuse buffer */
                           
                           remaining_unprocessed = d_stream.avail_in;
                           
                           /* Validate remaining_unprocessed is reasonable */
                           if(remaining_unprocessed < 0 || remaining_unprocessed > gzip_buffer_size)
                           {  debug_printf("DEBUG: Invalid remaining_unprocessed (%ld) in chunk extraction, resetting to 0\n", remaining_unprocessed);
                              remaining_unprocessed = 0;
                           }
                           
                           if(remaining_unprocessed > 0)
                           {  /* We have unprocessed data - calculate how much buffer is used */
                              /* Validate next_in pointer before pointer arithmetic */
                              if(gzipbuffer && d_stream.next_in >= gzipbuffer && 
                                 d_stream.next_in < gzipbuffer + gzip_buffer_size)
                              {  /* Used space = data from start to next_in + remaining unprocessed */
                                 used_space = (d_stream.next_in - gzipbuffer) + remaining_unprocessed;
                                 /* Validate used_space is reasonable */
                                 if(used_space < 0 || used_space > gzip_buffer_size)
                                 {  debug_printf("DEBUG: Invalid used_space (%ld) calculated from next_in=%p, gzipbuffer=%p, remaining=%ld\n",
                                           used_space, d_stream.next_in, gzipbuffer, remaining_unprocessed);
                                    used_space = remaining_unprocessed; /* Fallback to just remaining */
                                    if(used_space > gzip_buffer_size) used_space = gzip_buffer_size;
                                 }
                              }
                              else
                              {  debug_printf("DEBUG: Invalid d_stream.next_in pointer (%p) for gzipbuffer (%p), resetting\n",
                                        d_stream.next_in, gzipbuffer);
                                 used_space = remaining_unprocessed; /* Fallback */
                                 if(used_space > gzip_buffer_size) used_space = gzip_buffer_size;
                              }
                           }
                           else
                           {  /* All data consumed - buffer can be reused from start */
                              used_space = 0;
                           }
                           
                           remaining_space = gzip_buffer_size - used_space;
                           if(remaining_space <= 0)
                           {  debug_printf("DEBUG: Chunked+gzip: Gzip buffer full (used=%ld, remaining=%ld), cannot add more chunks\n",
                                     used_space, remaining_space);
                              gzip_end = 1;
                              break;
                           }
                           
                           /* Determine how much chunk data we can copy */
                           available_space = MIN(chunk_size, remaining_space);
                           if(chunk_pos + available_space <= hi->blocklength)
                           {  /* We have the full chunk or space for it */
                              /* If we have unprocessed data, move it to start of buffer first */
                              if(remaining_unprocessed > 0 && d_stream.next_in != gzipbuffer)
                              {  bytes_to_move = remaining_unprocessed;
                                 /* Validate all pointers and sizes before memmove */
                                 if(bytes_to_move > gzip_buffer_size)
                                 {  debug_printf("DEBUG: bytes_to_move (%ld) exceeds buffer size (%ld), resetting\n",
                                           bytes_to_move, gzip_buffer_size);
                                    gziplength = 0;
                                    remaining_unprocessed = 0;
                                 }
                                 else if(bytes_to_move > 0 && gzipbuffer && d_stream.next_in >= gzipbuffer && 
                                         d_stream.next_in < gzipbuffer + gzip_buffer_size &&
                                         (d_stream.next_in + bytes_to_move) <= gzipbuffer + gzip_buffer_size &&
                                         bytes_to_move <= remaining_unprocessed)
                                 {  memmove(gzipbuffer, d_stream.next_in, bytes_to_move);
                                    gziplength = bytes_to_move;
                                    debug_printf("DEBUG: Chunked+gzip: Moved %ld bytes of unprocessed data to start\n", bytes_to_move);
                                 }
                                 else
                                 {  /* Invalid state - reset */
                                    debug_printf("DEBUG: Chunked+gzip: CRITICAL - Invalid buffer state (next_in=%p, gzipbuffer=%p, bytes=%ld, buffer_size=%ld), resetting\n",
                                           d_stream.next_in, gzipbuffer, bytes_to_move, gzip_buffer_size);
                                    gziplength = 0;
                                    remaining_unprocessed = 0;
                                 }
                              }
                              else
                              {  /* No unprocessed data or already at start - start fresh */
                                 if(remaining_unprocessed == 0)
                                 {  gziplength = 0;
                                 }
                                 /* else gziplength already set correctly */
                              }
                              
                              /* Append new chunk data to buffer */
                              if(gziplength + available_space <= gzip_buffer_size)
                              {  /* Validate pointers before memcpy */
                                 if(gzipbuffer && hi->fd && hi->fd->block &&
                                    gziplength >= 0 && gziplength < gzip_buffer_size &&
                                    available_space > 0 && available_space <= (gzip_buffer_size - gziplength) &&
                                    chunk_pos >= 0 && chunk_pos < hi->blocklength &&
                                    (chunk_pos + available_space) <= hi->blocklength)
                                 {  memcpy(gzipbuffer + gziplength, hi->fd->block + chunk_pos, available_space);
                                    gziplength += available_space;
                                    compressed_bytes_consumed += available_space; /* Track compressed data consumed */
                                 }
                                 else
                                 {  debug_printf("DEBUG: Invalid pointers for memcpy in chunk extraction (gzipbuffer=%p, block=%p, gziplength=%ld, available_space=%ld, chunk_pos=%ld, blocklength=%ld), aborting\n",
                                           gzipbuffer, hi->fd ? hi->fd->block : NULL, gziplength, available_space, chunk_pos, hi->blocklength);
                                    gzip_end = 1;
                                    break;
                                 }
                                 
                                 /* Update zlib stream to point to start of all unprocessed data */
                                 d_stream.next_in = gzipbuffer;
                                 d_stream.avail_in = gziplength;
                                 
                                 debug_printf("DEBUG: Chunked+gzip: Added %ld bytes from chunk (unprocessed was=%ld, total=%ld, avail_in=%lu)\n", 
                                        available_space, remaining_unprocessed, gziplength, d_stream.avail_in);
                                 
                                 /* Move past chunk data and CRLF */
                                 /* CRITICAL: Advance by available_space (what we copied), not chunk_size (total chunk size) */
                                 /* If we only copied part of the chunk due to buffer space, we need to advance by that amount */
                                 chunk_pos += available_space;
                                 
                                 /* Check if we've processed the entire chunk */
                                 if(available_space >= chunk_size)
                                 {  /* Full chunk processed - skip CRLF */
                                    if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\r')
                                    {  chunk_pos++;
                                    }
                                    if(chunk_pos < hi->blocklength && hi->fd->block[chunk_pos] == '\n')
                                    {  chunk_pos++;
                                    }
                                    
                                    /* Remove processed chunk from block */
                                    if(chunk_pos < hi->blocklength)
                                    {  long remaining;
                                       remaining = hi->blocklength - chunk_pos;
                                       memmove(hi->fd->block, hi->fd->block + chunk_pos, remaining);
                                       hi->blocklength = remaining;
                                    }
                                    else
                                    {  hi->blocklength = 0;
                                    }
                                 }
                                 else
                                 {  /* Partial chunk copied - keep remaining chunk data in block for next iteration */
                                    /* Don't remove data - we'll continue this chunk in next loop iteration */
                                    /* Just update blocklength to reflect what we've consumed */
                                    if(chunk_pos < hi->blocklength)
                                    {  /* Move unprocessed chunk data to start of block */
                                       long remaining;
                                       remaining = hi->blocklength - chunk_pos;
                                       memmove(hi->fd->block, hi->fd->block + chunk_pos, remaining);
                                       hi->blocklength = remaining;
                                    }
                                    else
                                    {  hi->blocklength = 0;
                                    }
                                 }
                                 
                                 /* Continue decompression */
                                 continue;
                              }
                              else
                              {  debug_printf("DEBUG: Chunked+gzip: Not enough space in buffer (gziplength=%ld, available_space=%ld, buffer_size=%ld)\n",
                                       gziplength, available_space, gzip_buffer_size);
                                 gzip_end = 1;
                                 break;
                              }
                           }
                           else
                           {  /* Need more data for this chunk */
                              debug_printf("DEBUG: Chunked+gzip: Chunk extends beyond block, need more data\n");
                              /* Keep the block and wait for more data */
                              break;
                           }
                        }
                     }
                     else
                     {  /* Non-chunked - all data already in gzipbuffer */
                        gzip_end=1;
                        break;
                     }
                  }
               }
               else if(err==Z_STREAM_END)
               {  /* Stream complete */
                  decompressed_len=hi->fd->blocksize-d_stream.avail_out;
                  if(decompressed_len > 0 && decompressed_len <= hi->fd->blocksize)
                  {  debug_printf("DEBUG: Processing final %ld bytes of decompressed data\n", decompressed_len);
                     /* Ensure parttype is null-terminated before using it to prevent bounds check errors */
                     hi->parttype[sizeof(hi->parttype) - 1] = '\0';
                     if(hi->parttype[0] != '\0' && hi->parttype[0] != 0) {
                        Updatetaskattrs(
                           AOURL_Data,hi->fd->block,
                           AOURL_Datalength,decompressed_len,
                           AOURL_Contenttype,hi->parttype,
                           TAG_END);
                     } else {
                        Updatetaskattrs(
                           AOURL_Data,hi->fd->block,
                           AOURL_Datalength,decompressed_len,
                           TAG_END);
                     }
                  }
                  
                  /* For chunked encoding, zlib may finish before all chunks are processed */
                  /* After Z_STREAM_END, we must NOT try to decompress more data - just discard remaining chunks */
                  /* Exit decompression loop immediately and let chunk-discarding code handle remaining chunks */
                  if(hi->flags & HTTPIF_CHUNKED)
                  {  debug_printf("DEBUG: Z_STREAM_END - gzip stream complete, will discard remaining chunks\n");
                     /* Exit decompression loop - remaining chunks will be discarded below */
                     gzip_end=1;
                     break;
                  }
                  else
                  {  /* Non-chunked or no remaining data - gzip is complete */
                     gzip_end=1;
                     break;
                  }
               }
               else
               {  debug_printf("DEBUG: Gzip error: %d, ending\n", err);
                  gzip_end=1;
                  break;
               }
            }
            
            /* Clean up gzip after processing */
            if(gzip_end)
            {  inflateEnd(&d_stream);
               if(gzipbuffer) FREE(gzipbuffer);
               gzipbuffer = NULL;
               d_stream_initialized = FALSE;
               hi->flags &= ~HTTPIF_GZIPDECODING;
               hi->flags &= ~HTTPIF_GZIPENCODED;
               debug_printf("DEBUG: Gzip processing complete\n");
               
               /* For chunked+gzip, we need to continue processing remaining chunks */
               /* even after gzip finishes, until we reach the final "0\r\n\r\n" chunk */
               if(hi->flags & HTTPIF_CHUNKED)
               {  debug_printf("DEBUG: Chunked+gzip: Gzip complete, discarding remaining chunks until final chunk\n");
                  /* Process remaining data in blocklength, then continue reading chunks */
                  /* until we find the final "0\r\n\r\n" chunk */
                  /* Note: remaining data might be chunk continuation (not starting with hex digits) */
                  while(hi->blocklength > 0 || Readblock(hi))
                  {  UBYTE *chunk_p;
                     long chunk_pos;
                     long chunk_size;
                     long chunk_data_end;
                     BOOL final_chunk_found;
                     UBYTE first_char;
                     BOOL is_chunk_header;
                     
                     if(hi->blocklength <= 0) break;
                     
                     chunk_p = hi->fd->block;
                     chunk_pos = 0;
                     final_chunk_found = FALSE;
                     
                     /* Parse chunks in current block until we find final chunk or run out of data */
                     while(chunk_pos < hi->blocklength)
                     {  /* Check if this looks like a chunk header (starts with hex digit) */
                        /* or continuation data (starts with non-hex) */
                        first_char = chunk_p[chunk_pos];
                        is_chunk_header = FALSE;
                        if((first_char >= '0' && first_char <= '9') ||
                           (first_char >= 'A' && first_char <= 'F') ||
                           (first_char >= 'a' && first_char <= 'f'))
                        {  is_chunk_header = TRUE;
                        }
                        
                        if(!is_chunk_header)
                        {  /* This is continuation data from a previous chunk - skip until we find CRLF */
                           /* which indicates end of chunk data, then we'll see next chunk header */
                           debug_printf("DEBUG: Chunked+gzip: Found continuation data (starts with 0x%02X), skipping to next chunk boundary\n", first_char);
                           /* Skip until we find CRLF (end of chunk data) */
                           while(chunk_pos < hi->blocklength - 1)
                           {  if(chunk_p[chunk_pos] == '\r' && chunk_p[chunk_pos + 1] == '\n')
                              {  chunk_pos += 2; /* Skip CRLF */
                                 break; /* Now we should see next chunk header */
                              }
                              chunk_pos++;
                           }
                           if(chunk_pos >= hi->blocklength - 1)
                           {  /* CRLF not found in this block - need more data */
                              /* Move remaining data to start */
                              if(chunk_pos < hi->blocklength)
                              {  long remaining;
                                 remaining = hi->blocklength - chunk_pos;
                                 memmove(hi->fd->block, chunk_p + chunk_pos, remaining);
                                 hi->blocklength = remaining;
                              }
                              break; /* Exit inner loop to read more */
                           }
                           /* Continue to parse next chunk header */
                           continue;
                        }
                        
                        /* Skip whitespace */
                        while(chunk_pos < hi->blocklength && 
                              (chunk_p[chunk_pos] == ' ' || chunk_p[chunk_pos] == '\t'))
                        {  chunk_pos++;
                        }
                        
                        if(chunk_pos >= hi->blocklength) break;
                        
                        /* Parse chunk size */
                        chunk_size = 0;
                        while(chunk_pos < hi->blocklength)
                        {  UBYTE c;
                           long digit;
                           c = chunk_p[chunk_pos];
                           if(c >= '0' && c <= '9')
                           {  digit = c - '0';
                           }
                           else if(c >= 'A' && c <= 'F')
                           {  digit = c - 'A' + 10;
                           }
                           else if(c >= 'a' && c <= 'f')
                           {  digit = c - 'a' + 10;
                           }
                           else
                           {  break;
                           }
                           chunk_size = chunk_size * 16 + digit;
                           chunk_pos++;
                        }
                        
                        /* Skip chunk extension and CRLF */
                        while(chunk_pos < hi->blocklength && 
                              chunk_p[chunk_pos] != '\r' && chunk_p[chunk_pos] != '\n')
                        {  chunk_pos++;
                        }
                        if(chunk_pos < hi->blocklength && chunk_p[chunk_pos] == '\r')
                        {  chunk_pos++;
                        }
                        if(chunk_pos < hi->blocklength && chunk_p[chunk_pos] == '\n')
                        {  chunk_pos++;
                        }
                        
                        if(chunk_size == 0)
                        {  /* Final chunk found */
                           debug_printf("DEBUG: Chunked+gzip: Final chunk (0) found, all chunks processed\n");
                           final_chunk_found = TRUE;
                           /* Skip final CRLF if present */
                           if(chunk_pos < hi->blocklength && chunk_p[chunk_pos] == '\r')
                           {  chunk_pos++;
                           }
                           if(chunk_pos < hi->blocklength && chunk_p[chunk_pos] == '\n')
                           {  chunk_pos++;
                           }
                           break;
                        }
                        
                        /* Skip chunk data and trailing CRLF */
                        chunk_data_end = chunk_pos + chunk_size;
                        if(chunk_data_end > hi->blocklength)
                        {  /* Chunk extends beyond current block - need more data */
                           debug_printf("DEBUG: Chunked+gzip: Chunk size %ld extends beyond block, need more data\n", chunk_size);
                           /* Move remaining data to start of block */
                           if(chunk_pos < hi->blocklength)
                           {  long remaining;
                              remaining = hi->blocklength - chunk_pos;
                              memmove(hi->fd->block, chunk_p + chunk_pos, remaining);
                              hi->blocklength = remaining;
                           }
                           break; /* Exit inner loop to read more */
                        }
                        
                        chunk_pos = chunk_data_end;
                        /* Skip trailing CRLF after chunk data */
                        if(chunk_pos < hi->blocklength && chunk_p[chunk_pos] == '\r')
                        {  chunk_pos++;
                        }
                        if(chunk_pos < hi->blocklength && chunk_p[chunk_pos] == '\n')
                        {  chunk_pos++;
                        }
                        
                        debug_printf("DEBUG: Chunked+gzip: Discarded chunk of size %ld\n", chunk_size);
                     }
                     
                     if(final_chunk_found)
                     {  /* Final chunk found - we're done */
                        hi->blocklength = 0;
                        break;
                     }
                     
                     /* Move any remaining data to start of block for next iteration */
                     if(chunk_pos < hi->blocklength)
                     {  long remaining;
                        remaining = hi->blocklength - chunk_pos;
                        memmove(hi->fd->block, chunk_p + chunk_pos, remaining);
                        hi->blocklength = remaining;
                     }
                     else
                     {  hi->blocklength = 0;
                     }
                  }
                  
                  debug_printf("DEBUG: Chunked+gzip: Finished discarding remaining chunks\n");
                  /* If Readblock() failed but gzip decompression completed successfully, */
                  /* treat as successful completion (server may have closed connection early) */
                  /* The actual data is already decompressed, so missing chunk boundaries is OK */
                  /* Note: If we exited the loop because Readblock() failed (not because final chunk found), */
                  /* we still treat it as success since gzip decompression already completed */
                  result = TRUE;  /* Always success if gzip decompression completed */
                  break;
               }
               
               /* For non-chunked gzip streams, all data has been decompressed */
               /* Account for any remaining compressed data in current block */
               if(hi->blocklength > 0)
               {  total_compressed_read += hi->blocklength;
                  debug_printf("DEBUG: Accounting for %ld bytes remaining in block after gzip completion\n", hi->blocklength);
               }
               
               /* Clear blocklength to prevent processing remaining compressed data as uncompressed */
               hi->blocklength = 0;
               
               /* For non-chunked responses with Content-Length, we need to read */
               /* exactly that much compressed data from network before exiting */
               /* Note: Chunked encoding and Content-Length are mutually exclusive per HTTP spec */
               if(!(hi->flags & HTTPIF_CHUNKED) && hi->partlength > 0)
               {  /* We have a Content-Length for non-chunked response - need to read that much compressed data total from network */
                  debug_printf("DEBUG: Gzip decompression complete, read %ld/%ld compressed bytes from network so far\n", 
                         total_compressed_read, hi->partlength);
                  while(total_compressed_read < hi->partlength && Readblock(hi))
                  {  long compressed_in_block;
                     compressed_in_block = hi->blocklength;
                     total_compressed_read += compressed_in_block;
                     hi->blocklength = 0; /* Discard consumed compressed data */
                     debug_printf("DEBUG: Read %ld bytes of compressed data from network, total=%ld/%ld\n", 
                            compressed_in_block, total_compressed_read, hi->partlength);
                  }
                  debug_printf("DEBUG: Finished reading compressed data from network: %ld/%ld bytes\n", 
                         total_compressed_read, hi->partlength);
                  
                  /* Validate Content-Length: warn if we didn't read the expected amount */
                  if(total_compressed_read != hi->partlength)
                  {  debug_printf("DEBUG: WARNING - Content-Length mismatch: read %ld bytes, expected %ld bytes\n",
                            total_compressed_read, hi->partlength);
                     /* This could indicate a server error or connection issue, but don't fail the request */
                     /* since decompression already completed successfully */
                  }
               }
               else
               {  /* No Content-Length - read until EOF to consume remaining compressed data */
                  debug_printf("DEBUG: No Content-Length, reading until EOF to consume remaining compressed data\n");
                  while(Readblock(hi))
                  {  /* Discard remaining compressed data - it's already been decompressed */
                     total_compressed_read += hi->blocklength;
                     hi->blocklength = 0;
                  }
               }
               
               /* Exit Readdata - gzip response is complete */
               result = !eof;
               break;
            }
            continue; /* Skip regular data processing */
         }

         /* Process chunked transfer encoding if present (and not already processed for gzip) */
         if((hi->flags & HTTPIF_CHUNKED) && !(hi->flags & HTTPIF_GZIPDECODING))
         {  UBYTE *chunk_p;
            UBYTE *output_p;
            long chunk_pos;
            long chunk_size;
            long output_pos;
            long original_blocklength;
            BOOL final_chunk;
            BOOL need_more_data;
            
            chunk_p = hi->fd->block;
            output_p = hi->fd->block; /* Reuse same buffer for output */
            chunk_pos = 0;
            output_pos = 0;
            original_blocklength = hi->blocklength; /* Save original length before we modify block */
            final_chunk = FALSE;
            need_more_data = FALSE;
            
            /* Parse chunked encoding: <size in hex>\r\n<data>\r\n */
            /* Extract all complete chunks, removing headers */
            /* Note: We read from original_blocklength positions, but write to output_pos positions */
            while(chunk_pos < original_blocklength && !final_chunk && !need_more_data)
            {  long chunk_header_start;
               long chunk_data_start;
               long chunk_data_end;
               
               chunk_header_start = chunk_pos;
               
               /* Skip whitespace before chunk size */
               while(chunk_pos < original_blocklength && 
                     (chunk_p[chunk_pos] == ' ' || chunk_p[chunk_pos] == '\t'))
               {  chunk_pos++;
               }
               
               if(chunk_pos >= original_blocklength)
               {  need_more_data = TRUE;
                  break;
               }
               
               /* Parse chunk size (hexadecimal) */
               chunk_size = 0;
               while(chunk_pos < original_blocklength)
               {  UBYTE c;
                  long digit;
                  c = chunk_p[chunk_pos];
                  if(c >= '0' && c <= '9')
                  {  digit = c - '0';
                  }
                  else if(c >= 'A' && c <= 'F')
                  {  digit = c - 'A' + 10;
                  }
                  else if(c >= 'a' && c <= 'f')
                  {  digit = c - 'a' + 10;
                  }
                  else
                  {  break;
                  }
                  chunk_size = chunk_size * 16 + digit;
                  chunk_pos++;
               }
               
               /* Skip chunk extension and CRLF after chunk size */
               while(chunk_pos < original_blocklength && 
                     chunk_p[chunk_pos] != '\r' && chunk_p[chunk_pos] != '\n')
               {  chunk_pos++;
               }
               if(chunk_pos < original_blocklength && chunk_p[chunk_pos] == '\r')
               {  chunk_pos++;
               }
               if(chunk_pos < original_blocklength && chunk_p[chunk_pos] == '\n')
               {  chunk_pos++;
               }
               
               if(chunk_size == 0)
               {  /* Final chunk (0) - skip trailing CRLF if present */
                  final_chunk = TRUE;
                  if(chunk_pos < original_blocklength && chunk_p[chunk_pos] == '\r')
                  {  chunk_pos++;
                  }
                  if(chunk_pos < original_blocklength && chunk_p[chunk_pos] == '\n')
                  {  chunk_pos++;
                  }
                  break;
               }
               
               /* Calculate where chunk data starts and ends (in original block) */
               chunk_data_start = chunk_pos;
               chunk_data_end = chunk_pos + chunk_size;
               
               if(chunk_data_end > original_blocklength)
               {  /* Chunk extends beyond current block - need more data */
                  /* Move partial chunk header to start of block for next iteration */
                  if(chunk_header_start < original_blocklength)
                  {  long remaining;
                     remaining = original_blocklength - chunk_header_start;
                     if(remaining > 0 && remaining <= hi->fd->blocksize)
                     {  memmove(hi->fd->block, chunk_p + chunk_header_start, remaining);
                        hi->blocklength = remaining;
                     }
                     else
                     {  hi->blocklength = 0;
                     }
                  }
                  else
                  {  hi->blocklength = 0;
                  }
                  need_more_data = TRUE;
                  break;
               }
               
               /* Copy chunk data to output (removing header) */
               /* We copy from original block position (chunk_data_start) to output position (output_pos) */
               /* Since we're removing headers, output_pos should be <= chunk_data_start, so this is safe */
               /* But we need to validate to prevent memory corruption */
               if(chunk_data_start < chunk_data_end && chunk_data_end <= original_blocklength 
                  && output_pos + chunk_size <= hi->fd->blocksize && output_pos >= 0 && chunk_size >= 0
                  && chunk_data_start + chunk_size <= original_blocklength)
               {  /* Validate that output_pos <= chunk_data_start to ensure safe in-place copy */
                  if(output_pos <= chunk_data_start)
                  {  /* Safe to copy - we're reading from later position, writing to earlier position */
                     memmove(output_p + output_pos, chunk_p + chunk_data_start, chunk_size);
                     output_pos += chunk_size;
                  }
                  else
                  {  /* This shouldn't happen - output_pos > chunk_data_start means we've already written past where we're reading */
                     debug_printf("DEBUG: Chunked encoding: ERROR - output_pos %ld > chunk_data_start %ld, unsafe copy!\n",
                            output_pos, chunk_data_start);
                     need_more_data = TRUE;
                     break;
                  }
               }
               else
               {  /* Invalid bounds - this shouldn't happen but handle it */
                  debug_printf("DEBUG: Chunked encoding: WARNING - Invalid bounds (chunk_data_start=%ld, chunk_data_end=%ld, original_blocklength=%ld, output_pos=%ld, chunk_size=%ld, blocksize=%ld)\n",
                         chunk_data_start, chunk_data_end, original_blocklength, output_pos, chunk_size, hi->fd->blocksize);
                  need_more_data = TRUE;
                  break;
               }
               
               /* Skip chunk data and trailing CRLF to find next chunk (in original block) */
               chunk_pos = chunk_data_end;
               if(chunk_pos < original_blocklength && chunk_p[chunk_pos] == '\r')
               {  chunk_pos++;
               }
               if(chunk_pos < original_blocklength && chunk_p[chunk_pos] == '\n')
               {  chunk_pos++;
               }
            }
            
            if(need_more_data && output_pos == 0)
            {  /* Need more data to complete chunk - don't process partial data yet */
               /* blocklength already adjusted above when we moved partial chunk header */
               debug_printf("DEBUG: Chunked encoding: Need more data to complete chunk, continuing loop\n");
               /* Continue main loop to read more data */
               if(!Readblock(hi))
               {  eof = TRUE;
                  break;
               }
               continue; /* Process new data in next iteration */
            }
            
            if(output_pos > 0 || final_chunk)
            {  /* Update block with extracted chunk data (headers removed) */
               hi->blocklength = output_pos;
               blocklength = output_pos;
               debug_printf("DEBUG: Processed chunked encoding: extracted %ld bytes of data (final_chunk=%d, need_more=%d)\n", 
                      output_pos, final_chunk, need_more_data);
               
               /* If final chunk found, clear chunked flag */
               if(final_chunk)
               {  hi->flags &= ~HTTPIF_CHUNKED;
                  debug_printf("DEBUG: Chunked encoding complete (final chunk received)\n");
               }
               
               /* If there's more chunked data in this block, move it for next iteration */
               if(!final_chunk && chunk_pos < original_blocklength)
               {  /* There's more chunked data starting at chunk_pos in the original block */
                  long remaining;
                  remaining = original_blocklength - chunk_pos;
                  if(remaining > 0 && output_pos + remaining <= hi->fd->blocksize && output_pos >= 0)
                  {  /* Move remaining chunked data to start, after the extracted data */
                     memmove(hi->fd->block + output_pos, chunk_p + chunk_pos, remaining);
                     hi->blocklength = output_pos + remaining;
                     /* Send extracted data now, process remaining chunks in next iteration */
                     blocklength = output_pos;
                  }
                  else if(remaining > 0)
                  {  /* Not enough space - this shouldn't happen but handle it */
                     debug_printf("DEBUG: Chunked encoding: WARNING - Not enough space for remaining data (output_pos=%ld, remaining=%ld, blocksize=%ld)\n",
                            output_pos, remaining, hi->fd->blocksize);
                     /* Just send what we have, remaining will be processed in next iteration */
                     blocklength = output_pos;
                  }
               }
            }
            else
            {  /* No data extracted yet - might be empty chunks or need more data */
               if(hi->blocklength == 0)
               {  /* No data left - read more */
                  if(!Readblock(hi))
                  {  eof = TRUE;
                     break;
                  }
                  continue;
               }
               blocklength = hi->blocklength;
            }
         }
         
         /* Boundary detection for multipart data */
         boundary=partial=eof=FALSE;
         if(bdcopy)
         {  /* Look for [CR]LF--<boundary>[--][CR]LF or any possible part thereof. */
            UBYTE *p=hi->fd->block,*end=p+hi->blocklength;
            for(;;)
            {  for(;p<end && *p!='\r' && *p!='\n';p++);
               if(p>=end) break;
               blocklength=p-hi->fd->block;
               if(*p=='\r' && (p>=end-1 || p[1]=='\n'))
               {  p++;  /* Skip CR */
               }
               p++;  /* Skip LF */
               if(p>=end) partial=TRUE;
               else
               {  if(*p=='-')
                  {  /* Create a copy of hi->boundary, with what we have
                      * in the block, copied over. */
                     strcpy(bdcopy,hi->boundary);
                     strncpy(bdcopy,p,MIN(bdlength,end-p));
                     /* If the result is equal to the boundary, we have a (at least
                      * partial possible) boundary. */
                     if(STREQUAL(bdcopy,hi->boundary))
                     {  /* Now check if it's complete and followed by [CR]LF. */
                        p+=bdlength;
                        if(p<end && *p=='-') p++;
                        if(p<end && *p=='-')
                        {  eof=TRUE;
                           p++;
                        }
                        if(p<end && *p=='\r') p++;
                        if(p>=end) partial=TRUE;
                        else if(*p=='\n') boundary=TRUE;
                     }
                  }
               }
               if(boundary || partial) break;
               /* Look further */
               p=hi->fd->block+blocklength+1;
            }
         }
         if(!boundary && !partial) blocklength=hi->blocklength;
         
         /* Validate blocklength to prevent memory corruption */
         if(blocklength < 0 || blocklength > hi->blocklength) {
            debug_printf("DEBUG: Invalid blocklength %ld, resetting to %ld\n", blocklength, hi->blocklength);
            blocklength = hi->blocklength;
         }
         
         /* Validate data before calling Updatetaskattrs to prevent memory corruption */
         if(hi->fd && hi->fd->block && blocklength >= 0 && blocklength <= hi->fd->blocksize) {
            debug_printf("DEBUG: Validating data before Updatetaskattrs: block=%p, length=%ld, parttype='%s' (strlen=%ld)\n", 
                   hi->fd->block, blocklength, 
                   hi->parttype[0] ? (char *)hi->parttype : "(none)",
                   hi->parttype[0] ? strlen(hi->parttype) : 0);
            
            /* Track total bytes received for Content-Length validation (non-gzip transfers) */
            /* Only track if not processing gzip (gzip tracks compressed_bytes_consumed separately) */
            done_after_send = FALSE;
            if(!(hi->flags & HTTPIF_GZIPDECODING) && !(hi->flags & HTTPIF_GZIPENCODED))
            {  /* If Content-Length is known, never pass more than remaining bytes.
                * This avoids "complete by counters, corrupt by data" when we
                * stop reading early (and it also avoids sending stray bytes
                * that don't belong to this body). */
               if(!(hi->flags & HTTPIF_CHUNKED) && hi->partlength > 0 && expected_total_size > 0)
               {  remaining_bytes = expected_total_size - total_bytes_received;
                  if(remaining_bytes < 0) remaining_bytes = 0;
                  if(blocklength > remaining_bytes)
                  {  debug_printf("DEBUG: Content-Length clamp: block=%ld, remaining=%ld, clamping\n",
                           blocklength, remaining_bytes);
                     blocklength = remaining_bytes;
                     done_after_send = TRUE;
                  }
                  else if(blocklength == remaining_bytes)
                  {  done_after_send = TRUE;
                  }
               }
               
               total_bytes_received += blocklength;
               hi->bytes_received = total_bytes_received;  /* Update persistent counter for Range retry */
               debug_printf("DEBUG: Total bytes received so far: %ld/%ld\n", total_bytes_received, expected_total_size > 0 ? expected_total_size : hi->partlength);
            }
            
            /* Include content type if available to prevent defaulting to octet-stream */
            /* Check if parttype is valid by checking first character and length */
            if(hi->parttype[0] != '\0' && hi->parttype[0] != 0 && strlen(hi->parttype) > 0) {
               debug_printf("DEBUG: Sending data with content type: '%s' (length=%ld)\n", hi->parttype, strlen(hi->parttype));
            Updatetaskattrs(
               AOURL_Data,hi->fd->block,
               AOURL_Datalength,blocklength,
                  AOURL_Contenttype,hi->parttype,
               TAG_END);
         } else {
               debug_printf("DEBUG: WARNING: parttype is empty or invalid! Sending data without content type (will default to octet-stream)\n");
               debug_printf("DEBUG: parttype[0]=0x%02X, parttype='%.31s'\n", (unsigned char)hi->parttype[0], hi->parttype);
               Updatetaskattrs(
                  AOURL_Data,hi->fd->block,
                  AOURL_Datalength,blocklength,
                  TAG_END);
            }
         } else {
            debug_printf("DEBUG: Invalid data detected, skipping Updatetaskattrs to prevent memory corruption\n");
            debug_printf("DEBUG: fd=%p, block=%p, blocklength=%ld, blocksize=%ld\n", 
                   hi->fd, hi->fd ? hi->fd->block : NULL, blocklength, hi->fd ? hi->fd->blocksize : 0);
         }
         
         /* If we reached Content-Length exactly, stop now and discard any extra buffered bytes.
          * Leaving extra bytes around without a per-connection read buffer will corrupt the next
          * transfer; and trying to read more can block. */
         if(done_after_send)
         {  debug_printf("DEBUG: Content-Length complete in main loop: Received %ld/%ld bytes, exiting read loop\n",
                   total_bytes_received, expected_total_size);
            hi->blocklength = 0;
            result = TRUE;
            break;
         }
         
         /* Safe memory move with bounds checking */
         if(blocklength < hi->blocklength && blocklength > 0) {
            move_length = hi->blocklength - blocklength;
            if(move_length > 0 && move_length <= hi->fd->blocksize) {
               memmove(hi->fd->block, hi->fd->block + blocklength, move_length);
               hi->blocklength = move_length;
            } else {
               debug_printf("DEBUG: Invalid move_length %ld, resetting blocklength\n", move_length);
               hi->blocklength = 0;
            }
         } else if(blocklength >= hi->blocklength) {
            hi->blocklength = 0;
         }
         
         /* Handle blocklength differently for gzip vs non-gzip data */
         /* This code should NEVER run if gzip decoding is active because we break out earlier */
         if(hi->flags & HTTPIF_GZIPDECODING)
         {  /* This should not happen - we should have broken out earlier */
            debug_printf("DEBUG: ERROR: Regular data processing with gzip flag set! This is a bug.\n");
            /* The gzip loop handles blocklength separately via decompressed_len */
            d_stream.next_out=hi->fd->block;
            d_stream.avail_out=hi->fd->blocksize;
            /* Don't process this data - let gzip loop handle it */
            break;
         }
         else
         {  /* Only subtract blocklength for non-gzip data */
            /* Only subtract if blocklength is valid and >= blocklength */
            if(hi->blocklength > 0 && hi->blocklength >= blocklength)
            {  hi->blocklength-=blocklength;
            }
            else if(hi->blocklength > 0 && hi->blocklength < blocklength)
            {  /* This should not happen, but handle it safely */
               debug_printf("DEBUG: WARNING: blocklength %ld < processed %ld, resetting\n", hi->blocklength, blocklength);
               hi->blocklength = 0;
            }
            /* If blocklength is already 0 (e.g., after gzip cleanup), don't try to subtract */
         }
         if(boundary)
         {  result=!eof;
            break;
         }
      }
      
      debug_printf("DEBUG: Exited main for(;;) loop, now checking gzip processing\n");
      
      /* Gzip processing loop - continues until stream is complete */
      /* This loop should NOT run if gzip was already processed in the main loop */
      /* The inline loop above processes ALL gzip data, so this should only handle edge cases */
      
      /* Clean up flags if resources are already cleaned up BEFORE checking loop condition */
      /* This prevents the loop from trying to run with invalid resources */
      /* Also check if gzip_end was set, meaning gzip processing completed */
      if(hi->flags & HTTPIF_GZIPDECODING && (gzipbuffer == NULL || !d_stream_initialized || gzip_end)) {
         if(gzipbuffer == NULL || !d_stream_initialized) {
            debug_printf("DEBUG: Flag is set but resources cleaned up - clearing flags to prevent crash\n");
         }
         if(gzip_end) {
            debug_printf("DEBUG: Flag is set but gzip_end is set - clearing flags as gzip processing is complete\n");
         }
         hi->flags &= ~HTTPIF_GZIPDECODING;
         hi->flags &= ~HTTPIF_GZIPENCODED;
      }
      
      if(d_stream_initialized) {
         debug_printf("DEBUG: Checking gzip loop condition: flags=0x%04X, HTTPIF_GZIPDECODING=0x%04X, gzip_end=%u, gzipbuffer=%p, d_stream.avail_in=%lu\n",
                hi->flags, HTTPIF_GZIPDECODING, gzip_end, gzipbuffer, d_stream.avail_in);
      } else {
         debug_printf("DEBUG: Checking gzip loop condition: flags=0x%04X, HTTPIF_GZIPDECODING=0x%04X, gzip_end=%u, gzipbuffer=%p, d_stream NOT initialized\n",
                hi->flags, HTTPIF_GZIPDECODING, gzip_end, gzipbuffer);
      }
      if(hi->flags & HTTPIF_GZIPDECODING && !gzip_end && gzipbuffer != NULL && d_stream_initialized) {
         if(d_stream_initialized) {
            debug_printf("DEBUG: Entering gzip processing loop, flags=0x%04X, gzipbuffer=%p, gzip_end=%u, avail_in=%lu\n", 
                   hi->flags, gzipbuffer, gzip_end, d_stream.avail_in);
         } else {
            debug_printf("DEBUG: Entering gzip processing loop, flags=0x%04X, gzipbuffer=%p, gzip_end=%u\n", 
                   hi->flags, gzipbuffer, gzip_end);
         }
      } else {
         debug_printf("DEBUG: NOT entering gzip loop - flags check: %d, gzip_end check: %d, gzipbuffer check: %d, d_stream_initialized: %d\n",
                (hi->flags & HTTPIF_GZIPDECODING) != 0, gzip_end == 0, gzipbuffer != NULL, d_stream_initialized);
         /* Clean up if flag is still set but we're not entering the loop */
         if(hi->flags & HTTPIF_GZIPDECODING) {
            debug_printf("DEBUG: Flag is set but loop not running - cleaning up\n");
            if(gzipbuffer) {
               FREE(gzipbuffer);
               gzipbuffer = NULL;
            }
            if(d_stream_initialized) {
               inflateEnd(&d_stream);
               d_stream_initialized = FALSE;
            }
            hi->flags &= ~HTTPIF_GZIPDECODING;
            hi->flags &= ~HTTPIF_GZIPENCODED;
         }
      }
      while(hi->flags & HTTPIF_GZIPDECODING && !gzip_end && gzipbuffer != NULL && d_stream_initialized)
      {  /* Declare all variables at start of loop for C89 compliance */
         long final_len;
         long error_len;
         long decompressed_len;
         UBYTE *buffer_start;
         UBYTE *buffer_end;
         UBYTE *data_check;
         BOOL data_valid;
         int i;
         
         /* Re-validate all conditions inside loop to prevent crashes */
         if(gzip_end>0 || gzipbuffer == NULL || !d_stream_initialized) {
            debug_printf("DEBUG: Gzip loop: Invalid state detected, exiting (gzip_end=%d, gzipbuffer=%p, d_stream_initialized=%d)\n",
                   gzip_end, gzipbuffer, d_stream_initialized);
            break;
         }
         
         /* Validate d_stream.next_in points to valid memory before using it */
         if(d_stream.next_in != NULL && d_stream.next_in != gzipbuffer && 
            (d_stream.next_in < gzipbuffer || d_stream.next_in >= gzipbuffer + gzip_buffer_size)) {
            debug_printf("DEBUG: d_stream.next_in (%p) is invalid, clearing flags\n", d_stream.next_in);
            hi->flags &= ~HTTPIF_GZIPDECODING;
            hi->flags &= ~HTTPIF_GZIPENCODED;
            break;
         }
         
         /* Process the current gzip data */
         if(d_stream.avail_in > 0 && d_stream.next_in != NULL) {
            err=inflate(&d_stream,Z_SYNC_FLUSH);
         } else {
            err=Z_OK; /* No input to process */
         }
         
         /* Handle zlib return codes */
         if(err==Z_BUF_ERROR) {
            /* Output buffer is full - this is normal, not an error */
            debug_printf("DEBUG: Output buffer full, processing current data and continuing\n");
            /* Process the current decompressed data */
            hi->blocklength=hi->fd->blocksize-d_stream.avail_out;
            debug_printf("DEBUG: Processing %ld bytes of decompressed data\n", hi->blocklength);
            
            /* Validate blocklength before processing to prevent memory corruption */
            if(hi->blocklength < 0 || hi->blocklength > hi->fd->blocksize) {
               debug_printf("DEBUG: Invalid blocklength %ld in Z_BUF_ERROR, resetting to prevent memory corruption\n", hi->blocklength);
               hi->blocklength = 0;
               inflateEnd(&d_stream);
               hi->flags &= ~HTTPIF_GZIPENCODED;
               hi->flags &= ~HTTPIF_GZIPDECODING;
               gzip_end = 1;
               break;
            }
            
            /* Update task attributes with current data */
            if(hi->parttype[0] && strstr(hi->parttype, "text/html")) {
            Updatetaskattrs(
               AOURL_Data,hi->fd->block,
               AOURL_Datalength,hi->blocklength,
                  AOURL_Contenttype, "text/html",
               TAG_END);
            } else {
               Updatetaskattrs(
                  AOURL_Data,hi->fd->block,
                  AOURL_Datalength,hi->blocklength,
                  TAG_END);
            }
            
            /* Reset output buffer for next chunk */
            d_stream.next_out=hi->fd->block;
            d_stream.avail_out=hi->fd->blocksize;
            
            /* Continue decompression - but add safety check to prevent infinite loops */
            /* For Content-Length gzip transfers, rely on Content-Length validation instead of loop limit */
            /* For chunked gzip, use a high limit to prevent infinite loops while allowing large transfers */
            if(!(hi->flags & HTTPIF_CHUNKED) && hi->partlength > 0)
            {  /* Content-Length gzip transfer - track count but don't limit */
               loop_count++; /* Track but don't limit - Content-Length check will handle completion */
            }
            else
            {  /* Chunked or unknown length gzip - use high limit to prevent infinite loops */
               if(++loop_count > 10000) {
                  debug_printf("DEBUG: Too many buffer full cycles (%d), finishing gzip\n", loop_count);
                  gzip_end=1;
                  break;
               }
            }
            continue;
         }
         else if(err==Z_STREAM_END) {
            debug_printf("DEBUG: Gzip decompression completed successfully\n");
            /* Process any remaining decompressed data before exiting */
            if(d_stream.avail_out < hi->fd->blocksize) {
               final_len = hi->fd->blocksize - d_stream.avail_out;
               if(final_len > 0 && final_len <= hi->fd->blocksize) {
                  debug_printf("DEBUG: Processing final %ld bytes from gzip stream\n", final_len);
                  if(hi->parttype[0] && strstr(hi->parttype, "text/html")) {
                     Updatetaskattrs(
                        AOURL_Data, hi->fd->block,
                        AOURL_Datalength, final_len,
                        AOURL_Contenttype, "text/html",
                        TAG_END);
                  } else {
                     Updatetaskattrs(
                        AOURL_Data, hi->fd->block,
                        AOURL_Datalength, final_len,
                        TAG_END);
                  }
                  hi->flags |= HTTPIF_DATA_PROCESSED;
               }
            }
            
            /* When Z_STREAM_END is returned, zlib has finished decompressing */
            /* compressed_bytes_consumed tracks data we've copied to gzipbuffer and processed */
            /* For Content-Length tracking, we need to account for any remaining unprocessed data in the buffer */
            /* Note: d_stream.avail_in at Z_STREAM_END should typically be 0 for a valid gzip stream */
            if(!(hi->flags & HTTPIF_CHUNKED) && hi->partlength > 0)
            {  long remaining_in_buffer;
               remaining_in_buffer = d_stream.avail_in;
               if(remaining_in_buffer > 0)
               {  debug_printf("DEBUG: Z_STREAM_END: %ld bytes remain unprocessed in buffer (this is unusual for gzip)\n", remaining_in_buffer);
                  /* This data is part of what was read from network, but not processed by zlib */
                  /* For Content-Length tracking, we should count it, but it's already counted in compressed_bytes_consumed */
                  /* since we copied it to gzipbuffer. The issue is zlib didn't consume it all. */
               }
               debug_printf("DEBUG: Z_STREAM_END: compressed_bytes_consumed=%ld, Content-Length=%ld, remaining_in_buffer=%ld\n",
                      compressed_bytes_consumed, hi->partlength, remaining_in_buffer);
            }
            
            /* Clean up immediately when stream ends */
            if(d_stream_initialized) {
               inflateEnd(&d_stream);
               d_stream_initialized = FALSE;
            }
            if(gzipbuffer) {
               FREE(gzipbuffer);
               gzipbuffer = NULL;
            }
            hi->flags &= ~HTTPIF_GZIPENCODED;
            hi->flags &= ~HTTPIF_GZIPDECODING;
            gzip_end=1; // Success break!
         }
         else if(err!=Z_OK)
         {  if(err==Z_DATA_ERROR) printf("zlib DATA ERROR - avail_in=%lu, avail_out=%lu\n", d_stream.avail_in, d_stream.avail_out);
            if(err==Z_STREAM_ERROR) printf("zlib STREAM_ERROR - avail_in=%lu, avail_out=%lu\n", d_stream.avail_in, d_stream.avail_out);
            if(err==Z_NEED_DICT) printf("zlib NEED DICT - avail_in=%lu, avail_out=%lu\n", d_stream.avail_in, d_stream.avail_out);
            if(err==Z_MEM_ERROR) printf("zlib MEM ERROR - avail_in=%lu, avail_out=%lu\n", d_stream.avail_in, d_stream.avail_out);
            
            /* On any zlib error, process any valid decompressed data first */
            /* Then safely disable gzip to prevent memory corruption */
            if(d_stream.avail_out < hi->fd->blocksize) {
               /* We might have some valid decompressed data before the error */
               error_len = hi->fd->blocksize - d_stream.avail_out;
               if(error_len > 0 && error_len <= hi->fd->blocksize) {
                  debug_printf("DEBUG: Processing %ld bytes before zlib error\n", error_len);
                  if(hi->parttype[0] && strstr(hi->parttype, "text/html")) {
                     Updatetaskattrs(
                        AOURL_Data, hi->fd->block,
                        AOURL_Datalength, error_len,
                        AOURL_Contenttype, "text/html",
                        TAG_END);
                  } else {
                     Updatetaskattrs(
                        AOURL_Data, hi->fd->block,
                        AOURL_Datalength, error_len,
                        TAG_END);
                  }
                  hi->flags |= HTTPIF_DATA_PROCESSED;
               }
            }
            
         debug_printf("DEBUG: Zlib error detected, safely disabling gzip to prevent crashes\n");
         
         /* Clean up zlib stream immediately */
            if(d_stream_initialized) {
         inflateEnd(&d_stream);
               d_stream_initialized = FALSE;
            }
            
            /* Free gzip buffer */
            if(gzipbuffer) {
               FREE(gzipbuffer);
               gzipbuffer = NULL;
            }
         
         /* Reset all gzip flags */
         hi->flags &= ~HTTPIF_GZIPENCODED;
         hi->flags &= ~HTTPIF_GZIPDECODING;
         
         /* Reset output buffer safely */
         hi->blocklength = 0;
         d_stream.avail_out = hi->fd->blocksize;
         d_stream.next_out = hi->fd->block;
         
         /* Break out of gzip processing to prevent further corruption */
         gzip_end = 1;
         break;
         }
         
         /* If we need more input data, read it */
         if((err==Z_OK || err==Z_BUF_ERROR) && d_stream.avail_in==0 && !gzip_end)
         {  /* Read more compressed data from network */
            if(!Readblock(hi))
            {  debug_printf("DEBUG: No more data for gzip, ending\n");
               gzip_end = 1;
               break;
            }
            /* Update total compressed bytes read from network */
            total_compressed_read += hi->blocklength;
            
            /* For non-chunked, append new block data to gzipbuffer */
            if(!(hi->flags & HTTPIF_CHUNKED))
            {  /* Non-chunked: Append all new block data to gzipbuffer */
               long remaining_space;
               long bytes_to_append;
               long used_space;
               
               /* Calculate space used in buffer */
               used_space = (d_stream.next_in - gzipbuffer) + d_stream.avail_in;
               remaining_space = gzip_buffer_size - used_space;
               
               if(remaining_space <= 0 || hi->blocklength > remaining_space)
               {  debug_printf("DEBUG: Non-chunked gzip: Buffer full (remaining=%ld, block=%ld), ending\n",
                         remaining_space, hi->blocklength);
                  gzip_end = 1;
                  break;
               }
               
               bytes_to_append = MIN(hi->blocklength, remaining_space);
               if(bytes_to_append > 0)
               {  /* Move unprocessed data to start if needed */
                  if(d_stream.next_in != gzipbuffer && d_stream.avail_in > 0)
                  {  memmove(gzipbuffer, d_stream.next_in, d_stream.avail_in);
                     gziplength = d_stream.avail_in;
                  }
                  else if(d_stream.avail_in == 0)
                  {  gziplength = 0;
                  }
                  
                  /* Append new data */
                  if(gziplength + bytes_to_append <= gzip_buffer_size)
                  {  memcpy(gzipbuffer + gziplength, hi->fd->block, bytes_to_append);
                     gziplength += bytes_to_append;
                     compressed_bytes_consumed += bytes_to_append;
                     
                     /* Update zlib stream */
                     d_stream.next_in = gzipbuffer;
                     d_stream.avail_in = gziplength;
                     
                     debug_printf("DEBUG: Non-chunked gzip: Added %ld bytes from new block (total in buffer=%ld)\n",
                            bytes_to_append, gziplength);
                     
                     /* Clear block */
                     hi->blocklength = 0;
                  }
               }
            }
            /* For chunked encoding, we need to extract chunks - this is handled in the chunked+gzip section above */
            
            /* Add timeout protection to prevent infinite hanging */
            /* For Content-Length gzip transfers, rely on Content-Length validation instead of loop limit */
            /* For chunked gzip, use a high limit to prevent infinite loops while allowing large transfers */
            if(!(hi->flags & HTTPIF_CHUNKED) && hi->partlength > 0)
            {  /* Content-Length gzip transfer - track count but don't limit */
               loop_count++; /* Track but don't limit - Content-Length check will handle completion */
            }
            else
            {  /* Chunked or unknown length gzip - use high limit to prevent infinite loops */
               if(++loop_count > 10000) {
                  debug_printf("DEBUG: Too many gzip input cycles (%d), finishing\n", loop_count);
                  /* Clean up when loop limit reached */
                  if(d_stream_initialized) {
                     inflateEnd(&d_stream);
                     d_stream_initialized = FALSE;
                  }
                  if(gzipbuffer) {
                     FREE(gzipbuffer);
                     gzipbuffer = NULL;
                  }
                  hi->flags &= ~HTTPIF_GZIPENCODED;
                  hi->flags &= ~HTTPIF_GZIPDECODING;
                  gzip_end=1;
                  break;
               }
            }
            
            /* For chunked encoding, read into block first, then extract chunk data */
            if(hi->flags & HTTPIF_CHUNKED)
            {  /* Declare all variables at start of block for C89 compliance */
               long chunk_data_start;
               long gzip_data_start;
               long search_pos;
               long chunk_data_len;
               long remaining_unprocessed;
               long available_space;
               UBYTE *p;
               
               /* Read chunk data into block buffer first */
               if(!Readblock(hi))
               {  debug_printf("DEBUG: End of chunked gzip stream\n");
                  /* Clean up when we reach end of stream */
                  if(d_stream_initialized) {
                     inflateEnd(&d_stream);
                     d_stream_initialized = FALSE;
                  }
                  if(gzipbuffer) {
                     FREE(gzipbuffer);
                     gzipbuffer = NULL;
                  }
                  hi->flags &= ~HTTPIF_GZIPENCODED;
                  hi->flags &= ~HTTPIF_GZIPDECODING;
                  gzip_end=1;
                  break;
               }
               
               /* Extract chunk data (skip chunk header) */
               chunk_data_start = 0;
               p = hi->fd->block;
               
               /* Find end of chunk size line */
               {  long chunk_size_start = chunk_data_start;
                  long chunk_size_value = 0;
                  BOOL found_crlf = FALSE;
                  
                  /* Parse chunk size hex number */
                  while(chunk_data_start < hi->blocklength)
                  {  UBYTE c;
                     long digit;
                     
                     c = p[chunk_data_start];
                     if(c >= '0' && c <= '9')
                     {  digit = c - '0';
                     }
                     else if(c >= 'A' && c <= 'F')
                     {  digit = c - 'A' + 10;
                     }
                     else if(c >= 'a' && c <= 'f')
                     {  digit = c - 'a' + 10;
                     }
                     else if(c == '\r' || c == '\n')
                     {  /* Found CRLF - end of chunk header */
                        found_crlf = TRUE;
                        break;
                     }
                     else
                     {  /* Invalid character in chunk size */
                        debug_printf("DEBUG: Invalid character in chunk size header (0x%02X at pos %ld)\n", 
                              (unsigned char)c, chunk_data_start);
                        /* Try to find CRLF anyway */
                        break;
                     }
                     
                     /* Check for overflow */
                     if(chunk_size_value > 0x7FFFFFFF / 16)
                     {  debug_printf("DEBUG: Chunk size overflow in chunked gzip parsing\n");
                        chunk_size_value = -1;
                        break;
                     }
                     
                     chunk_size_value = chunk_size_value * 16 + digit;
                     chunk_data_start++;
                     
                     /* Limit hex digits */
                     if(chunk_data_start - chunk_size_start > 16)
                     {  debug_printf("DEBUG: Chunk size hex number too long in chunked gzip\n");
                        chunk_size_value = -1;
                        break;
                     }
                  }
                  
                  /* Validate chunk size */
                  if(chunk_size_value < 0)
                  {  debug_printf("DEBUG: Invalid chunk size in chunked gzip, aborting\n");
                     if(bdcopy) { FREE(bdcopy); bdcopy = NULL; }
                     if(gzipbuffer) { FREE(gzipbuffer); gzipbuffer = NULL; }
                     if(d_stream_initialized) { inflateEnd(&d_stream); d_stream_initialized = FALSE; }
                     Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
                     return FALSE;
                  }
                  
                  /* Find CRLF after chunk size */
                  if(!found_crlf)
                  {  while(chunk_data_start < hi->blocklength - 1)
                     {  if(p[chunk_data_start] == '\r' && chunk_data_start + 1 < hi->blocklength && p[chunk_data_start + 1] == '\n')
                        {  chunk_data_start += 2; /* Skip CRLF */
                           found_crlf = TRUE;
                           break;
                        }
                        chunk_data_start++;
                     }
                  }
                  else
                  {  /* Skip CRLF */
                     if(chunk_data_start < hi->blocklength && p[chunk_data_start] == '\r')
                     {  chunk_data_start++;
                     }
                     if(chunk_data_start < hi->blocklength && p[chunk_data_start] == '\n')
                     {  chunk_data_start++;
                     }
                  }
                  
                  if(!found_crlf)
                  {  debug_printf("DEBUG: No chunk data found, waiting for more\n");
                     /* Not enough data yet - wait */
                     continue;
                  }
               }
               
               /* Find actual gzip data start (should be immediately after chunk header) */
               gzip_data_start = chunk_data_start;
               if(hi->blocklength - gzip_data_start >= 3)
               {  /* Check if it starts with gzip magic */
                  if(p[gzip_data_start] != 0x1F || p[gzip_data_start + 1] != 0x8B || p[gzip_data_start + 2] != 0x08)
                  {  /* Look for gzip magic in this chunk */
                     search_pos = gzip_data_start;
                     for(; search_pos < hi->blocklength - 2; search_pos++)
                     {  if(p[search_pos] == 0x1F && p[search_pos + 1] == 0x8B && p[search_pos + 2] == 0x08)
                        {  gzip_data_start = search_pos;
                           break;
                        }
                     }
                  }
               }
               
               chunk_data_len = hi->blocklength - gzip_data_start;
               if(chunk_data_len > 0)
               {  /* For chunked encoding, accumulate chunks into a larger buffer */
                  /* Calculate remaining unprocessed data */
                  remaining_unprocessed = d_stream.avail_in;
                  
                  /* Validate remaining_unprocessed is reasonable */
                  if(remaining_unprocessed < 0 || remaining_unprocessed > gzip_buffer_size)
                  {  debug_printf("DEBUG: Invalid remaining_unprocessed (%ld), resetting to 0\n", remaining_unprocessed);
                     remaining_unprocessed = 0;
                  }
                  
                  /* Move remaining unprocessed data to start of buffer to make room */
                  if(d_stream.next_in != gzipbuffer && remaining_unprocessed > 0)
                  {  /* Validate pointers and sizes before memmove to prevent corruption */
                     if(remaining_unprocessed > gzip_buffer_size)
                     {  debug_printf("DEBUG: remaining_unprocessed (%ld) exceeds buffer size (%ld), resetting\n",
                               remaining_unprocessed, gzip_buffer_size);
                        remaining_unprocessed = 0;
                        gziplength = 0;
                     }
                     else if(d_stream.next_in >= gzipbuffer && 
                             d_stream.next_in < gzipbuffer + gzip_buffer_size &&
                             (d_stream.next_in + remaining_unprocessed) <= gzipbuffer + gzip_buffer_size &&
                             remaining_unprocessed > 0)
                     {  memmove(gzipbuffer, d_stream.next_in, remaining_unprocessed);
                        d_stream.next_in = gzipbuffer;
                        d_stream.avail_in = remaining_unprocessed;
                        gziplength = remaining_unprocessed;
                     }
                     else
                     {  debug_printf("DEBUG: Invalid pointer state for memmove (next_in=%p, gzipbuffer=%p, remaining=%ld, buffer_size=%ld), resetting\n",
                               d_stream.next_in, gzipbuffer, remaining_unprocessed, gzip_buffer_size);
                        remaining_unprocessed = 0;
                        gziplength = 0;
                     }
                  }
                  else if(remaining_unprocessed > 0)
                  {  /* Already at start - ensure gziplength is set correctly */
                     gziplength = remaining_unprocessed;
                  }
                  else
                  {  /* All data consumed - reset gziplength for new data */
                     gziplength = 0;
                  }
                  
                  /* Check if we have space for new chunk */
                  available_space = gzip_buffer_size - gziplength;
                  if(chunk_data_len > available_space)
                  {  debug_printf("DEBUG: WARNING: Chunk data (%ld) exceeds available space (%ld), truncating\n", 
                            chunk_data_len, available_space);
                     chunk_data_len = available_space > 0 ? available_space : 0;
                     if(chunk_data_len <= 0)
                     {  debug_printf("DEBUG: No space in gzipbuffer for chunk data\n");
                        gzip_end = 1;
                        break;
                     }
                  }
                  
                  /* Append chunk data to end of gzipbuffer */
                  if(gziplength + chunk_data_len > gzip_buffer_size)
                  {  debug_printf("DEBUG: Buffer overflow! gziplength=%ld, chunk_data_len=%ld, buffer_size=%ld\n",
                            gziplength, chunk_data_len, gzip_buffer_size);
                     gzip_end = 1;
                     break;
                  }
                  
                  /* Validate source and destination pointers before memcpy */
                  if(gzipbuffer && hi->fd->block && 
                     gziplength >= 0 && gziplength < gzip_buffer_size &&
                     chunk_data_len > 0 && chunk_data_len <= available_space &&
                     gzip_data_start >= 0 && gzip_data_start < hi->blocklength &&
                     (gzip_data_start + chunk_data_len) <= hi->blocklength)
                  {  memcpy(gzipbuffer + gziplength, hi->fd->block + gzip_data_start, chunk_data_len);
                     compressed_bytes_consumed += chunk_data_len; /* Track compressed data consumed */
                  }
                  else
                  {  debug_printf("DEBUG: Invalid pointers for memcpy (gzipbuffer=%p, block=%p, gziplength=%ld, chunk_data_len=%ld, gzip_data_start=%ld, blocklength=%ld), aborting\n",
                            gzipbuffer, hi->fd ? hi->fd->block : NULL, gziplength, chunk_data_len, gzip_data_start, hi->blocklength);
                     gzip_end = 1;
                     break;
                  }
                  
                  /* Update zlib stream to include new data */
                  d_stream.next_in = gzipbuffer; /* Point to start */
                  
                  /* Update total gziplength first, then set avail_in to match */
                  gziplength += chunk_data_len;
                  d_stream.avail_in = gziplength; /* Total available now */
                  
                  /* Clear processed chunk data from block */
                  hi->blocklength = 0;
                  
                  debug_printf("DEBUG: Read %ld bytes of chunked gzip data (unprocessed was=%ld), total in buffer: %ld, avail_in: %lu\n", 
                         chunk_data_len, remaining_unprocessed, gziplength, d_stream.avail_in);
               }
               else
               {  /* No data in this chunk - might be end chunk */
                  debug_printf("DEBUG: Empty chunk, checking for end\n");
                  gzip_end=1;
                  break;
               }
            }
            else
            {  /* Non-chunked: read directly into gzipbuffer */
            gziplength=Receive(hi,gzipbuffer,gzip_buffer_size);
            if(gziplength<=0)
            {  
               if(gziplength < 0) {
                  debug_printf("DEBUG: Network error during gzip processing: %ld\n", gziplength);
               } else {
                  debug_printf("DEBUG: End of gzip data stream\n");
               }
               gzip_end=1; // Finished or Error
            }
            else
            {  d_stream.next_in=gzipbuffer;
               d_stream.avail_in=gziplength;
            }
            }
         }
         
         /* Only calculate blocklength if we have decompressed data */
         /* Don't reset blocklength here - it may still contain valid data */
         if(d_stream.avail_out < hi->fd->blocksize) {
            /* We have decompressed some data */
            decompressed_len = hi->fd->blocksize - d_stream.avail_out;
            
            debug_printf("DEBUG: Gzip decompressed %ld bytes\n", decompressed_len);
         
            /* Validate decompressed length immediately to prevent memory corruption */
            if(decompressed_len < 0 || decompressed_len > hi->fd->blocksize) {
               debug_printf("DEBUG: Invalid decompressed length %ld calculated, aborting\n", decompressed_len);
            inflateEnd(&d_stream);
            hi->flags &= ~HTTPIF_GZIPENCODED;
            hi->flags &= ~HTTPIF_GZIPDECODING;
               hi->blocklength = 0;
            gzip_end = 1;
            break;
         }
         
                  /* Process decompressed data immediately to ensure it's not lost */
            if(hi->fd && hi->fd->block && decompressed_len > 0) {
               debug_printf("DEBUG: Processing %ld bytes of decompressed data immediately\n", decompressed_len);
            
            /* Ensure proper content type is set for HTML parsing */
            /* Check if we have HTML content type from headers */
            if(hi->parttype[0] && strstr(hi->parttype, "text/html")) {
               debug_printf("DEBUG: Content type is HTML, ensuring proper parsing\n");
                  Updatetaskattrs(
                     AOURL_Data, hi->fd->block,
                     AOURL_Datalength, decompressed_len,
                     AOURL_Contenttype, "text/html",
                     TAG_END);
               } else {
                  debug_printf("DEBUG: Content type: %s, processing as-is\n", hi->parttype[0] ? (char *)hi->parttype : "unknown");
                  Updatetaskattrs(
                     AOURL_Data, hi->fd->block,
                     AOURL_Datalength, decompressed_len,
                     TAG_END);
            }
            
            /* Mark this data as already processed to prevent duplication */
            hi->flags |= HTTPIF_DATA_PROCESSED;
               
               /* Reset output buffer for next decompression cycle */
               d_stream.next_out = hi->fd->block;
               d_stream.avail_out = hi->fd->blocksize;
               /* DON'T reset blocklength here - it's managed separately */
            }
            
            /* Continue processing - only exit when gzip_end is set (Z_STREAM_END) */
            if(gzip_end) {
               debug_printf("DEBUG: Gzip stream complete, exiting processing loop\n");
            break;
            }
         }
         
         /* Memory bounds validation with actual canary-like protection */
         if(hi->blocklength < 0 || hi->blocklength > hi->fd->blocksize) {
            debug_printf("DEBUG: Invalid blocklength %ld after decompression, aborting\n", hi->blocklength);
            inflateEnd(&d_stream);
            hi->flags &= ~HTTPIF_GZIPENCODED;
            hi->flags &= ~HTTPIF_GZIPDECODING;
            hi->blocklength = 0;
            gzip_end = 1;
            break;
         }
         
         /* Buffer overflow protection - validate the actual buffer */
         if(hi->fd->block && hi->blocklength > 0) {
            buffer_start = hi->fd->block;
            buffer_end = buffer_start + hi->blocklength;
            
            /* Check if buffer pointers are valid memory addresses */
            if((ULONG)buffer_start < 0x1000 || (ULONG)buffer_start > 0xFFFFFFF0) {
               debug_printf("DEBUG: Invalid buffer start address 0x%08lX\n", (ULONG)buffer_start);
               hi->blocklength = 0;
               gzip_end = 1;
               break;
            }
            
            /* Check if buffer end is within valid range */
            if((ULONG)buffer_end < 0x1000 || (ULONG)buffer_end > 0xFFFFFFF0) {
               debug_printf("DEBUG: Invalid buffer end address 0x%08lX\n", (ULONG)buffer_end);
               hi->blocklength = 0;
               gzip_end = 1;
               break;
            }
            
            /* Verify buffer doesn't wrap around */
            if((ULONG)buffer_end <= (ULONG)buffer_start) {
               debug_printf("DEBUG: Buffer wrap-around detected\n");
               hi->blocklength = 0;
               gzip_end = 1;
               break;
            }
            
            /* Heap corruption detection - check memory allocation integrity */
            if(hi->fd->blocksize > 0) {
               /* Check if blocksize is reasonable (not corrupted) */
               if(hi->fd->blocksize > 1024*1024) { /* 1MB max */
                  debug_printf("DEBUG: Corrupted blocksize detected: %ld\n", hi->fd->blocksize);
                  hi->blocklength = 0;
                  gzip_end = 1;
                  break;
               }
               
               /* Only validate that block pointer is a reasonable memory address */
               /* Don't check if it's within filedata structure - it's a separate buffer! */
               if((ULONG)buffer_start < 0x1000 || (ULONG)buffer_start > 0xFFFFFFF0) {
                  debug_printf("DEBUG: Block pointer is invalid memory address: 0x%08lX\n", (ULONG)buffer_start);
                  hi->blocklength = 0;
                  gzip_end = 1;
                  break;
               }
            }
         }
         
         /* Safety check - ensure blocklength is valid to prevent memory corruption */
         if(hi->blocklength < 0 || hi->blocklength > hi->fd->blocksize) {
            debug_printf("DEBUG: Invalid blocklength %ld, resetting to 0 to prevent crash\n", hi->blocklength);
            hi->blocklength = 0;
            
            /* If we get invalid data, disable gzip to prevent further corruption */
            debug_printf("DEBUG: Disabling gzip due to invalid decompressed data\n");
            hi->flags &= ~HTTPIF_GZIPENCODED;
            hi->flags &= ~HTTPIF_GZIPDECODING;
            gzip_end = 1;
            break;
         }
         
         /* If we have no more input and no more output, we're done */
         if(d_stream.avail_in==0 && d_stream.avail_out==hi->fd->blocksize && !gzip_end) {
            debug_printf("DEBUG: No more input data, gzip decompression complete\n");
            gzip_end=1;
         }
         
         /* Validate decompressed data to prevent memory corruption */
         if(hi->blocklength > 0) {
            data_check = hi->fd->block;
            data_valid = TRUE;
            i = 0;
            
            /* Buffer pointer validation FIRST */
            if((ULONG)data_check < 0x1000 || (ULONG)data_check > 0xFFFFFFF0) {
               debug_printf("DEBUG: Invalid buffer pointer 0x%08lX, disabling gzip\n", (ULONG)data_check);
               hi->flags &= ~HTTPIF_GZIPENCODED;
               hi->flags &= ~HTTPIF_GZIPDECODING;
               hi->blocklength = 0;
               gzip_end = 1;
               break;
            }
            
            /* Check first few bytes for reasonable data */
            for(i = 0; i < hi->blocklength && i < 16; i++) {
               if(data_check[i] < 0x20 && data_check[i] != 0x09 && data_check[i] != 0x0A && data_check[i] != 0x0D) {
                  if(data_check[i] != 0x00) { /* Allow null bytes */
                     data_valid = FALSE;
                     break;
                  }
               }
            }
            
            if(!data_valid) {
               debug_printf("DEBUG: Decompressed data contains invalid characters, disabling gzip\n");
               hi->flags &= ~HTTPIF_GZIPENCODED;
               hi->flags &= ~HTTPIF_GZIPDECODING;
               hi->blocklength = 0;
               gzip_end = 1;
               break;
            }
         }
      } /* End of gzip processing while loop */
      
      /* Clean up gzip resources after processing */
      if(hi->flags & HTTPIF_GZIPDECODING) {
         debug_printf("DEBUG: Cleaning up gzip resources, flags before cleanup=0x%04X, d_stream_initialized=%d\n", 
                hi->flags, d_stream_initialized);
         /* Gzip processing completed - clean up */
         if(d_stream_initialized) {
            inflateEnd(&d_stream);
            d_stream_initialized = FALSE;
         }
         if(gzipbuffer) {
            FREE(gzipbuffer);
            gzipbuffer = NULL;
         }
         hi->flags &= ~HTTPIF_GZIPDECODING;
         hi->flags &= ~HTTPIF_GZIPENCODED;
         debug_printf("DEBUG: Gzip cleanup complete, flags after cleanup=0x%04X\n", hi->flags);
         
         /* Account for any remaining compressed data in current block */
         if(hi->blocklength > 0)
         {  total_compressed_read += hi->blocklength;
            debug_printf("DEBUG: Accounting for %ld bytes remaining in block after gzip cleanup\n", hi->blocklength);
         }
         
         /* Reset blocklength to 0 after gzip processing is complete */
         /* This is safe because all gzip data has been processed and sent via Updatetaskattrs */
         hi->blocklength = 0;
         
         /* For non-chunked gzip streams, all data has been decompressed */
         /* For Content-Length responses, read exactly that much compressed data from network */
         if(!(hi->flags & HTTPIF_CHUNKED))
         {  if(hi->partlength > 0)
            {  debug_printf("DEBUG: Non-chunked gzip complete, read %ld/%ld compressed bytes from network, reading remaining\n", 
                      total_compressed_read, hi->partlength);
               while(total_compressed_read < hi->partlength && Readblock(hi))
               {  long compressed_in_block;
                  compressed_in_block = hi->blocklength;
                  total_compressed_read += compressed_in_block;
                  hi->blocklength = 0; /* Discard consumed compressed data */
                  debug_printf("DEBUG: Read %ld bytes from network, total=%ld/%ld\n", 
                         compressed_in_block, total_compressed_read, hi->partlength);
               }
               debug_printf("DEBUG: Finished reading compressed data from network: %ld/%ld bytes\n", 
                      total_compressed_read, hi->partlength);
               
               /* Validate Content-Length: warn if we didn't read the expected amount */
               if(total_compressed_read != hi->partlength)
               {  debug_printf("DEBUG: WARNING - Content-Length mismatch: read %ld bytes, expected %ld bytes\n",
                         total_compressed_read, hi->partlength);
                  /* This could indicate a server error or connection issue, but don't fail the request */
                  /* since decompression already completed successfully */
               }
            }
            else
            {  debug_printf("DEBUG: Non-chunked gzip complete, no Content-Length, reading until EOF\n");
               while(Readblock(hi))
               {  /* Discard remaining compressed data - it's already been decompressed */
                  total_compressed_read += hi->blocklength;
                  hi->blocklength = 0;
               }
            }
            /* Exit Readdata - gzip response is complete */
            result = !eof;
            if(bdcopy) FREE(bdcopy);
            return result;
         }
      } else if(gzipbuffer != NULL) {
         debug_printf("DEBUG: WARNING: gzipbuffer not NULL but flag not set - possible memory leak!\n");
            FREE(gzipbuffer);
            gzipbuffer = NULL;
         }
      /* Clean up d_stream if it was initialized but flag is not set */
      if(d_stream_initialized && !(hi->flags & HTTPIF_GZIPDECODING)) {
         debug_printf("DEBUG: Cleaning up orphaned d_stream\n");
         inflateEnd(&d_stream);
         d_stream_initialized = FALSE;
      }
      
      /* Validate blocklength before continuing */
      /* Only validate if we're not in a gzip state transition */
      if(!(hi->flags & HTTPIF_GZIPDECODING) && (hi->blocklength < 0 || hi->blocklength > hi->fd->blocksize)) {
         debug_printf("DEBUG: Invalid blocklength %ld after gzip processing, resetting to prevent corruption\n", hi->blocklength);
         hi->blocklength = 0;
      }
      
      if(hi->flags & HTTPIF_GZIPDECODING)
      {  
         /* This shouldn't happen - gzip should be cleaned up above */
         debug_printf("DEBUG: WARNING: Gzip still active after loop exit, forcing cleanup\n");
         inflateEnd(&d_stream);
         hi->flags &= ~HTTPIF_GZIPENCODED;
         hi->flags &= ~HTTPIF_GZIPDECODING;
         hi->blocklength = 0;
      }
      else
      {  debug_printf("DEBUG: No data to process, blocklength=%ld, calling Readblock\n", hi->blocklength);
         
         /* Validate blocklength before proceeding to prevent memory corruption */
         if(hi->blocklength < 0 || hi->blocklength > hi->fd->blocksize) {
            debug_printf("DEBUG: Invalid blocklength %ld detected in main loop, resetting to prevent OS crash\n", hi->blocklength);
            hi->blocklength = 0;
            
            /* Disable gzip if it's causing corruption */
            if(hi->flags & (HTTPIF_GZIPENCODED | HTTPIF_GZIPDECODING)) {
               debug_printf("DEBUG: Disabling corrupted gzip processing\n");
               hi->flags &= ~HTTPIF_GZIPENCODED;
               hi->flags &= ~HTTPIF_GZIPDECODING;
               
               /* Force cleanup of any remaining gzip resources */
               if(gzipbuffer) {
                  FREE(gzipbuffer);
                  gzipbuffer = NULL;
               }
               inflateEnd(&d_stream);
            }
            
            /* Reset corrupted buffer to prevent OS crash */
            if(hi->fd && hi->fd->block) {
               hi->fd->block[0] = '\0'; /* Safe reset */
            }
            
            break;
         }
         
         /* Check if data was already processed to prevent duplication */
         if(hi->flags & HTTPIF_DATA_PROCESSED) {
            debug_printf("DEBUG: Data already processed, skipping Readblock to prevent duplication\n");
            hi->flags &= ~HTTPIF_DATA_PROCESSED; /* Clear the flag for next iteration */
            break;
         }
         
         /* Add timeout protection to prevent infinite hanging */
         /* For Content-Length transfers, don't limit by loop count - rely on Content-Length check */
         /* For chunked/unknown transfers, use a high limit to prevent infinite loops */
         if(!(hi->flags & HTTPIF_CHUNKED) && hi->partlength > 0)
         {  /* Content-Length transfer - no loop limit, Content-Length check will handle completion */
            loop_count++; /* Track but don't limit */
         }
         else
         {  /* Chunked or unknown length - use high limit to prevent infinite loops */
            if(++loop_count > 10000) {
               debug_printf("DEBUG: Too many main loop cycles (%d), finishing\n", loop_count);
               break;
            }
         }
         
         if(!Readblock(hi)) {
            debug_printf("DEBUG: Readblock failed, checking for network errors\n");
            /* Check if this is a network error vs normal EOF */
            if(hi->blocklength > 0) {
               debug_printf("DEBUG: Have %ld bytes of data, processing what we have\n", hi->blocklength);
               
               /* Track bytes received for Content-Length validation (non-gzip transfers) */
               if(!(hi->flags & HTTPIF_GZIPDECODING) && !(hi->flags & HTTPIF_GZIPENCODED))
               {  total_bytes_received += hi->blocklength;
                  hi->bytes_received = total_bytes_received;  /* Update persistent counter for Range retry */
                  debug_printf("DEBUG: Total bytes received (including final block): %ld/%ld\n", total_bytes_received, expected_total_size > 0 ? expected_total_size : hi->partlength);
               }
               
               /* Process the data we already have before breaking */
               /* Include content type if available */
               if(hi->parttype[0] != '\0' && hi->parttype[0] != 0 && strlen(hi->parttype) > 0) {
                  debug_printf("DEBUG: Sending final data with content type: '%s' (length=%ld)\n", hi->parttype, strlen(hi->parttype));
                  Updatetaskattrs(
                     AOURL_Data, hi->fd->block,
                     AOURL_Datalength, hi->blocklength,
                     AOURL_Contenttype, hi->parttype,
                     TAG_END);
               } else {
                  debug_printf("DEBUG: WARNING: parttype empty for final data, sending without content type\n");
               Updatetaskattrs(
                  AOURL_Data, hi->fd->block,
                  AOURL_Datalength, hi->blocklength,
                  TAG_END);
               }
            }
            
            /* Validate Content-Length for non-gzip transfers */
            /* If we have a Content-Length but didn't receive all bytes, report error */
            if(!(hi->flags & HTTPIF_GZIPDECODING) && !(hi->flags & HTTPIF_GZIPENCODED) && 
               !(hi->flags & HTTPIF_CHUNKED) && hi->partlength > 0)
            {  /* For Range requests (206), compare against full file size, not range size */
               long expected_size = (hi->flags & HTTPIF_RANGE_REQUEST && hi->full_file_size > 0) ? 
                                    hi->full_file_size : hi->partlength;
               if(total_bytes_received < expected_size)
               {  debug_printf("DEBUG: ERROR: Transfer incomplete! Received %ld/%ld bytes (missing %ld bytes)\n", 
                         total_bytes_received, expected_size, expected_size - total_bytes_received);
                  /* Store bytes_received for potential Range request retry */
                  hi->bytes_received = total_bytes_received;
                  
                  /* Check if this was due to connection reset - report appropriate error */
                  {  long errno_value;
                     UBYTE *hostname_str;
                     if(hi->socketbase)
                     {  struct Library *saved_socketbase;
                        saved_socketbase = AcquireSocketBaseSwap(hi->socketbase);
                        errno_value = Errno();
                        ReleaseSocketBaseSwap(saved_socketbase);
                     }
                     else
                     {  errno_value = 0;
                     }
                     hostname_str = hi->hostname ? hi->hostname : (UBYTE *)"unknown";
                     if(errno_value == ECONNRESET)
                     {  /* If server supports Range requests, set flag for retry */
                        if(hi->server_supports_range && total_bytes_received > 0)
                        {  debug_printf("DEBUG: Server supports Range requests, will retry with Range header\n");
                           /* Don't report error yet - let retry mechanism handle it */
                        }
                        else
                        {  Tcperror(hi->fd, TCPERR_NOCONNECT_RESET, hostname_str);
                        }
                     }
                     else
                     {  Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
                     }
                  }
               }
               else if(total_bytes_received > expected_size)
               {  debug_printf("DEBUG: WARNING: Received more bytes than expected! Received %ld/%ld bytes (extra %ld bytes)\n", 
                         total_bytes_received, expected_size, total_bytes_received - expected_size);
               }
               else
               {  debug_printf("DEBUG: Transfer complete: Received exactly %ld/%ld bytes\n", total_bytes_received, expected_size);
                  /* Transfer complete - connection reset is expected when server closes connection */
                  /* Don't report error for ECONNRESET when all data was successfully received */
               }
            }
            else
            {  /* For chunked or unknown length transfers, check if connection reset occurred */
               /* and report error if it did (since we can't verify completeness) */
               {  long errno_value;
                  UBYTE *hostname_str;
                  if(hi->socketbase)
                  {  struct Library *saved_socketbase;
                     saved_socketbase = AcquireSocketBaseSwap(hi->socketbase);
                     errno_value = Errno();
                     ReleaseSocketBaseSwap(saved_socketbase);
                  }
                  else
                  {  errno_value = 0;
                  }
                  if(errno_value == ECONNRESET)
                  {  hostname_str = hi->hostname ? hi->hostname : (UBYTE *)"unknown";
                     Tcperror(hi->fd, TCPERR_NOCONNECT_RESET, hostname_str);
                  }
               }
            }
            
            break;
         }
         debug_printf("DEBUG: Readblock returned, new blocklength=%ld\n", hi->blocklength);
         
         /* Check if Content-Length is complete for non-gzip transfers */
         /* Continue reading until Content-Length is met or error occurs */
         if(!(hi->flags & HTTPIF_GZIPDECODING) && !(hi->flags & HTTPIF_GZIPENCODED) && 
            !(hi->flags & HTTPIF_CHUNKED) && hi->partlength > 0)
         {  /* For Range requests (206), compare against full file size, not range size */
            long expected_size = (hi->flags & HTTPIF_RANGE_REQUEST && hi->full_file_size > 0) ? 
                                 hi->full_file_size : hi->partlength;
            if(total_bytes_received >= expected_size)
            {  debug_printf("DEBUG: Content-Length complete: Received %ld/%ld bytes\n", total_bytes_received, expected_size);
               /* Content-Length met - exit loop */
               break;
            }
         }
      }
      
      /* Check if we should exit main loop for gzip processing */
      if(exit_main_loop) {
         debug_printf("DEBUG: Exit flag set, breaking out of main loop for gzip processing\n");
         break;
      }
   }
   
   debug_printf("DEBUG: Main loop ended, flags=0x%04X, gzipbuffer=%p, d_stream_initialized=%d, exit_main_loop=%d\n", 
          hi->flags, gzipbuffer, d_stream_initialized, exit_main_loop);
   
   if(bdcopy) FREE(bdcopy);
   
   /* Always clean up gzip resources to prevent memory corruption */
   if(gzipbuffer) 
   {  FREE(gzipbuffer);
      gzipbuffer = NULL;
   }
   
   /* Always clean up zlib stream to prevent memory corruption */
   if(d_stream_initialized)
   {  inflateEnd(&d_stream);
      d_stream_initialized = FALSE;
      debug_printf("DEBUG: Zlib stream cleaned up\n");
   }
   
   /* CRITICAL: Force socket closure if keep-alive NOT active */
   /* This prevents pooling dead connections that the server has closed */
   if(hi->sock >= 0)
   {  if (!((hi->flags & HTTPIF_KEEPALIVE) && (hi->flags & HTTPIF_KEEPALIVE_REQ)))
      {  debug_printf("DEBUG: Readdata cleanup: Closing non-keepalive socket\n");
         /* Close socket first, then clean up SSL */
         if(hi->socketbase) a_close(hi->sock, hi->socketbase);
         hi->sock = -1;
         
         /* Also free the library/assl references now since we won't pool */
         /* NOTE: Assl_cleanup() will call Assl_closessl() internally, so don't call it twice */
#ifndef DEMOVERSION
         if(hi->assl)
         {  /* Assl_cleanup() handles SSL shutdown and cleanup internally */
            Assl_cleanup(hi->assl);
            FREE(hi->assl);
            hi->assl = NULL;
         }
#endif
         if(hi->socketbase)
         {  CloseLibrary(hi->socketbase);
            hi->socketbase = NULL;
         }
      }
      else
      {  debug_printf("DEBUG: Readdata cleanup: Keeping keep-alive socket for pooling (sock=%ld)\n", hi->sock);
      }
   }
   
   /* Final memory corruption detection and prevention */
   if(hi->fd && hi->fd->block) {
      if(hi->blocklength < 0 || hi->blocklength > hi->fd->blocksize) {
         debug_printf("DEBUG: Final safety check - invalid blocklength %ld, resetting to 0\n", hi->blocklength);
         hi->blocklength = 0;
         
         /* Reset buffer to prevent memory corruption from spreading */
         if(hi->fd->blocksize > 0 && hi->fd->blocksize <= INPUTBLOCKSIZE) {
            hi->fd->block[0] = '\0'; /* Safe reset */
            debug_printf("DEBUG: Buffer reset to prevent OS corruption\n");
         }
         
         /* Disable gzip if it's causing corruption */
         if(hi->flags & (HTTPIF_GZIPENCODED | HTTPIF_GZIPDECODING)) {
            debug_printf("DEBUG: Final cleanup - disabling corrupted gzip processing\n");
            hi->flags &= ~HTTPIF_GZIPENCODED;
            hi->flags &= ~HTTPIF_GZIPDECODING;
         }
      }
   } else {
      debug_printf("DEBUG: Invalid fd or block pointer detected, resetting to prevent OS crash\n");
      hi->blocklength = 0;
   }
   
   /* Final validation before return to prevent memory corruption */
   if(hi->fd && (ULONG)hi->fd < 0x1000 || (ULONG)hi->fd > 0xFFFFFFF0) {
      debug_printf("DEBUG: Corrupted fd pointer detected! fd=0x%08lX\n", (ULONG)hi->fd);
      hi->fd = NULL;
      hi->blocklength = 0;
   }
   
   /* Final blocklength validation to prevent OS crash */
   if(hi->blocklength < 0) {
      debug_printf("DEBUG: Final blocklength validation failed: %ld, forcing reset to prevent OS crash\n", hi->blocklength);
      hi->blocklength = 0;
   }
   
   /* Final validation before return to prevent memory corruption */
   if(hi->fd && (ULONG)hi->fd < 0x1000 || (ULONG)hi->fd > 0xFFFFFFF0) {
      debug_printf("DEBUG: Corrupted fd pointer detected! fd=0x%08lX\n", (ULONG)hi->fd);
      hi->fd = NULL;
      hi->blocklength = 0;
   }
   
   return result;
}

/* Process the plain or HTTP or multipart response. */
static void Httpresponse(struct Httpinfo *hi,BOOL readfirst)
{  BOOL first=TRUE;
   debug_printf("DEBUG: Httpresponse: processing URL, flags=0x%04X\n", hi->flags);
   if(!readfirst || Readresponse(hi))
   {  Nextline(hi);
      hi->flags|=HTTPIF_HEADERS;
      if(Readheaders(hi))
      {  debug_printf("DEBUG: Readheaders returned TRUE, processing response\n");
         debug_printf("DEBUG: movedto=%lu, movedtourl=%p\n", hi->movedto, hi->movedtourl);
         if(hi->movedto && hi->movedtourl)
         {  redirect_count++; /* Increment redirect counter for loop protection */
            debug_printf("DEBUG: Processing redirect to: %s (redirect_count=%d)\n", hi->movedtourl, redirect_count);
            /* For redirects, consume any remaining body data before processing redirect */
            Nextline(hi);
            debug_printf("DEBUG: Nextline called for redirect body consumption, blocklength=%ld\n", hi->blocklength);
            /* If there's body data, we should read it, but for redirects we can skip */
            Updatetaskattrs(hi->movedto,hi->movedtourl,TAG_END);
            debug_printf("DEBUG: Updatetaskattrs for redirect completed\n");
            return; /* Exit after redirect */
         }
         else
         {              debug_printf("DEBUG: No redirect, calling Nextline before Readdata\n");
            Nextline(hi);
            debug_printf("DEBUG: Nextline completed, calling Readdata\n");
            debug_printf("DEBUG: After Nextline - blocklength=%ld, flags=0x%04X\n", hi->blocklength, hi->flags);
            if(hi->boundary)
            {  debug_printf("DEBUG: Httpresponse: Multipart boundary detected, processing parts\n");
               for(;;)
               {  if(!Findline(hi)) return;
                  if(STREQUAL(hi->fd->block,hi->boundary)) break;
                  Nextline(hi);
               }
               Nextline(hi);  /* Skip boundary */
               for(;;)
               {  if(!Readpartheaders(hi)) break;
                  Nextline(hi);
                  if(!first)
                  {  Updatetaskattrs(
                        AOURL_Reload,TRUE,
                        TAG_END);
                  }
                  if(*hi->parttype || hi->partlength)
                  {  Updatetaskattrs(
                        *hi->parttype?AOURL_Contenttype:TAG_IGNORE,hi->parttype,
                        hi->partlength?AOURL_Contentlength:TAG_IGNORE,hi->partlength,
                        TAG_END);
                  }
                  if(!Readdata(hi)) break;
                  Updatetaskattrs(
                     AOURL_Eof,TRUE,
                     AOURL_Serverpush,hi->fd->fetch,
                     TAG_END);
                  if(!Findline(hi)) break;
                  Nextline(hi);  /* Skip boundary */
                  first=FALSE;
               }
            }
            else
            {              debug_printf("DEBUG: No boundary, calling Readdata directly\n");
               
               /* Add memory corruption protection before calling Readdata */
               if(hi->fd && hi->fd->block && hi->fd->blocksize > 0 && hi->fd->blocksize <= INPUTBLOCKSIZE) {
                  debug_printf("DEBUG: Memory validation passed, calling Readdata\n");
                  debug_printf("DEBUG: Httpresponse: About to call Readdata() - this is the main data read\n");
                  Readdata(hi);
                  debug_printf("DEBUG: Httpresponse: Readdata() returned\n");
                  debug_printf("DEBUG: Readdata() completed\n");
                  
                  /* Validate memory integrity after Readdata */
                  if(hi->fd && hi->fd->block) {
                     if(hi->blocklength < 0 || hi->blocklength > hi->fd->blocksize) {
                        debug_printf("DEBUG: Memory corruption detected after Readdata! blocklength=%ld\n", hi->blocklength);
                        debug_printf("DEBUG: Resetting to prevent OS crash\n");
                        hi->blocklength = 0;
                        hi->fd->block[0] = '\0'; /* Safe reset */
                     }
                  }
               } else {
                  debug_printf("DEBUG: Invalid memory state detected, skipping Readdata to prevent OS crash\n");
                  debug_printf("DEBUG: fd=%p, block=%p, blocksize=%ld\n", hi->fd, hi->fd ? hi->fd->block : NULL, hi->fd ? hi->fd->blocksize : 0);
                  hi->blocklength = 0;
               }
            }
         }
      }
   }
   else
   {  Readdata(hi);
   }
}

/* Send a message */
static long Send(struct Httpinfo *hi,UBYTE *request,long reqlen)
{  long result=-1;
#ifndef DEMOVERSION
   if(hi->flags&HTTPIF_SSL)
   {  result=Assl_write(hi->assl,request,reqlen);
   }
   else
   {  debug_printf("DEBUG: Sending HTTP request: '%.*s'\n", (int)reqlen, request);
      result=a_send(hi->sock,request,reqlen,0,hi->socketbase);
   }
   return result;
}

#ifndef DEMOVERSION
/* Warning: Cannot make SSL connection. Retries TRUE if use unsecure link. */
/* COMMENTED OUT: Modern browser behavior - fail connection instead of prompting for unsecure fallback */
/*
static BOOL Securerequest(struct Httpinfo *hi,UBYTE *reason)
{  UBYTE *msg,*msgbuf;
   BOOL ok=FALSE;
   msg=AWEBSTR(MSG_SSLWARN_SSL_TEXT);
   if(msgbuf=ALLOCTYPE(UBYTE,strlen(msg)+strlen(hi->hostname)+strlen(reason)+8,0))
   {  Lprintf(msgbuf,msg,hi->hostname,reason);
      ok=Syncrequest(AWEBSTR(MSG_SSLWARN_SSL_TITLE),haiku?HAIKU11:msgbuf,
         AWEBSTR(MSG_SSLWARN_SSL_BUTTONS),0);
      FREE(msgbuf);
   }
   return ok;
}
*/
#endif

BOOL Httpcertaccept(char *hostname,char *certname)
{  char *def="????";
   UBYTE *msg,*msgbuf,*h,*c;
   struct Certaccept *ca;
   BOOL ok=FALSE;
   h=hostname;
   c=certname;
   if(!c) c=def;
   if(!h) h=def;
   ObtainSemaphore(&certsema);
   for(ca=certaccepts.first;ca->next;ca=ca->next)
   {  if(STRIEQUAL(ca->hostname,hostname) && STREQUAL(ca->certname,c))
      {  ok=TRUE;
         break;
      }
   }
   if(!ok)
   {  msg=AWEBSTR(MSG_SSLWARN_CERT_TEXT);
      if(msgbuf=ALLOCTYPE(UBYTE,strlen(msg)+strlen(h)+strlen(c)+8,0))
      {  Lprintf(msgbuf,msg,h,c);
         ok=Syncrequest(AWEBSTR(MSG_SSLWARN_CERT_TITLE),haiku?HAIKU13:msgbuf,
            AWEBSTR(MSG_SSLWARN_CERT_BUTTONS),0);
         FREE(msgbuf);
         if(hostname)
         {  if(ok)
            {  if(ca=ALLOCSTRUCT(Certaccept,1,MEMF_PUBLIC|MEMF_CLEAR))
               {  ca->hostname=Dupstr(hostname,-1);
                  ca->certname=Dupstr(c,-1);
                  ADDTAIL(&certaccepts,ca);
               }
            }
         }
      }
   }
   ReleaseSemaphore(&certsema);
   return ok;
}

/* Open the tcp stack, and optionally the SSL library */
static BOOL Openlibraries(struct Httpinfo *hi)
{  BOOL result=FALSE;
   struct KeepAliveConnection *pooled_conn;
   long port;
   
   debug_printf("DEBUG: Openlibraries: ENTRY - flags=0x%04X, SSL=%s\n", 
          hi->flags, (hi->flags&HTTPIF_SSL) ? "YES" : "NO");
   
   /* Check for pooled connection BEFORE creating new libraries */
   /* This allows us to reuse existing socketbase, Assl, and socket from the pool */
   /* A proxy connection is identified if hi->connect (proxy host) is different from hi->hostname (destination host) */
   /* SSL connections CAN be pooled - the SSL object maintains state and can be reused */
   /* If a reused SSL connection fails, the retry logic will handle it */
   debug_printf("DEBUG: Openlibraries: Checking for pooled connection (connect=%p, hostname=%p, SSL=%d)\n",
               hi->connect, hi->hostname, BOOLVAL(hi->flags&HTTPIF_SSL));
   if(hi->hostname && 
      (!hi->connect || (hi->connect && STRIEQUAL(hi->connect, hi->hostname))))
   {  /* Direct connection (not a proxy) - can reuse pooled connection */
      port = (hi->port > 0) ? hi->port : (BOOLVAL(hi->flags & HTTPIF_SSL) ? 443 : 80);
      debug_printf("DEBUG: Openlibraries: Calling GetKeepAliveConnection for %s:%ld (SSL=%d)\n",
                  hi->hostname, port, BOOLVAL(hi->flags&HTTPIF_SSL));
      pooled_conn = GetKeepAliveConnection(hi->hostname, port, BOOLVAL(hi->flags&HTTPIF_SSL));
      if(pooled_conn)
      {  /* Reuse pooled connection - use its socketbase, Assl (if SSL), and socket */
         hi->socketbase = pooled_conn->socketbase;
         hi->assl = pooled_conn->assl;
         hi->sock = pooled_conn->sock;
         hi->connection_reused = TRUE;
         debug_printf("DEBUG: Openlibraries: Reusing pooled %s connection (socketbase=%p, assl=%p, sock=%ld)\n",
                     (hi->flags&HTTPIF_SSL) ? "SSL" : "HTTP",
                     hi->socketbase, hi->assl, hi->sock);
         /* Free the pool entry - connection is now in use */
         FREE(pooled_conn->hostname);
         FREE(pooled_conn);
         return TRUE; /* Success - we have everything we need from the pool */
      }
      else
      {  debug_printf("DEBUG: Openlibraries: No pooled %s connection found for %s:%ld\n",
                     (hi->flags&HTTPIF_SSL) ? "SSL" : "HTTP",
                     hi->hostname, port);
      }
   }
   else
   {  debug_printf("DEBUG: Openlibraries: Skipping pool check (proxy=%s, direct=%s, SSL=%d)\n",
                  hi->connect ? (char *)hi->connect : "(none)",
                  hi->hostname ? (char *)hi->hostname : "(none)",
                  BOOLVAL(hi->flags&HTTPIF_SSL));
   }
   
   debug_printf("DEBUG: Openlibraries: Calling Opentcp()\n");
   Opentcp(&hi->socketbase,hi->fd,!hi->fd->validate);
   if(!hi->socketbase)
   {  debug_printf("DEBUG: Openlibraries: Opentcp() failed - bsdsocket.library missing\n");
      /* Show GUI error if bsdsocket.library is missing */
      Lowlevelreq("AWeb requires bsdsocket.library for network access.\nPlease install bsdsocket.library and try again.");
      return FALSE;
   }
   debug_printf("DEBUG: Openlibraries: Opentcp() succeeded, socketbase=%p\n", hi->socketbase);
   result=TRUE;
   if(hi->flags&HTTPIF_SSL)
   {  debug_printf("DEBUG: Openlibraries: SSL flag set, initializing SSL libraries\n");
#ifndef DEMOVERSION
      /* Each HTTPS connection must have its own dedicated Assl object */
      /* If hi->assl already exists, clean it up first to prevent reuse */
      if(hi->assl)
      {  debug_printf("DEBUG: Openlibraries: Existing Assl object found, cleaning up first\n");
         Assl_closessl(hi->assl);
         Assl_cleanup(hi->assl);
         /* Free the struct after Assl_cleanup() has cleaned it up */
         /* Assl_cleanup() no longer frees the struct to prevent use-after-free crashes */
         FREE(hi->assl);
         hi->assl = NULL;
         debug_printf("DEBUG: Openlibraries: Existing Assl object cleaned up\n");
      }
      debug_printf("DEBUG: Openlibraries: Calling Tcpopenssl() to initialize SSL\n");
      if(hi->assl=Tcpopenssl(hi->socketbase))
      {  debug_printf("DEBUG: Openlibraries: Tcpopenssl() succeeded, assl=%p\n", hi->assl);
         /* Verify that amisslmaster.library was successfully opened by Assl_initamissl() */
         /* No need to open/close it again - just check the global base pointer */
         if(!AmiSSLMasterBase)
         {  debug_printf("DEBUG: Openlibraries: ERROR - AmiSSLMasterBase is NULL after Tcpopenssl() succeeded\n");
            Lowlevelreq("AWeb requires amisslmaster.library for SSL/TLS connections.\nPlease install AmiSSL 5.20 or newer and try again.");
            Assl_cleanup(hi->assl);
            /* Free the struct after Assl_cleanup() has cleaned it up */
            FREE(hi->assl);
            hi->assl = NULL;
            result = FALSE;
            debug_printf("DEBUG: Openlibraries: SSL initialization failed - amisslmaster.library not initialized\n");
         }
         else
         {  debug_printf("DEBUG: Openlibraries: SSL libraries initialized successfully (AmiSSLMasterBase=%p)\n", AmiSSLMasterBase);
         }
      }
      else
      {  debug_printf("DEBUG: Openlibraries: Tcpopenssl() failed - SSL not available\n");
         /* No SSL available */
         Lowlevelreq("AWeb requires amissl.library (AmiSSL 5.20+) for SSL/TLS connections.\nPlease install AmiSSL and try again.");
         /* Modern browser behavior: fail connection instead of allowing unsecure fallback */
         /* if(Securerequest(hi,haiku?HAIKU12:AWEBSTR(MSG_SSLWARN_SSL_NO_SSL2)))
         {  debug_printf("DEBUG: Openlibraries: User chose to retry without SSL\n");
            hi->flags&=~HTTPIF_SSL;
         }
         else
         {  debug_printf("DEBUG: Openlibraries: User cancelled, SSL required\n");
            result=FALSE;
         }
         */
         debug_printf("DEBUG: Openlibraries: SSL required but not available - failing connection\n");
         result=FALSE;
      }
#else
      debug_printf("DEBUG: Openlibraries: DEMOVERSION - disabling SSL\n");
      hi->flags&=~HTTPIF_SSL;
#endif
   }
   else
   {  debug_printf("DEBUG: Openlibraries: No SSL flag, skipping SSL initialization\n");
   }
   debug_printf("DEBUG: Openlibraries: EXIT - result=%d, flags=0x%04X, assl=%p\n", 
          result, hi->flags, hi->assl);
   return result;
}

/* Create SSL context, SSL and socket */
static long Opensocket(struct Httpinfo *hi,struct hostent *hent)
{  long sock;
   struct KeepAliveConnection *pooled_conn;
   long port;
   
   /* 1. Check for reused connection */
   if(hi->connection_reused)
   {  debug_printf("DEBUG: Opensocket: Using pooled connection (sock=%ld)\n", hi->sock);
      sock = hi->sock;
      /* We DO NOT return immediately - we fall through to apply setsockopt */
   }
   else
   {  /* Standard new connection logic */
      /* Check keep-alive pool first (only for non-proxy connections) */
      /* Note: This is a fallback for HTTP connections - SSL connections are handled in Openlibraries() */
      if(!hi->connect && hi->hostname && !(hi->flags & HTTPIF_SSL))
      {  port = (hi->port > 0) ? hi->port : 80;
         pooled_conn = GetKeepAliveConnection(hi->hostname, port, FALSE);
         if(pooled_conn)
         {  /* Reuse pooled connection */
            hi->socketbase = pooled_conn->socketbase;
            hi->sock = pooled_conn->sock;
            hi->assl = pooled_conn->assl;
            hi->connection_reused = TRUE;
            sock = hi->sock;
            debug_printf("DEBUG: Opensocket: Reusing pooled HTTP connection (sock=%ld)\n",
                        hi->sock);
            /* Free the pool entry - connection is now in use */
            FREE(pooled_conn->hostname);
            FREE(pooled_conn);
         }
      }
      
      /* NOTE: Do not synchronously clean up the keep-alive pool here.
       * Cleanup can involve SSL shutdown/free and can block on some stacks,
       * which would wedge unrelated fetch tasks when they start a new socket.
       * Pool cleanup must be handled out-of-band. */
      
      /* If we didn't get a pooled connection, create a new one */
      if(!hi->connection_reused)
      {  /* Validate socketbase is still valid before using it */
         /* Another task might have closed the library, invalidating our handle */
         if(!hi->socketbase)
         {  debug_printf("DEBUG: Opensocket: socketbase is NULL, cleaning up SSL and returning -1\n");
#ifndef DEMOVERSION
            /* Clean up SSL resources that were allocated in Openlibraries() */
            if(hi->assl)
            {  Assl_closessl(hi->assl);
               Assl_cleanup(hi->assl);
               FREE(hi->assl);
               hi->assl = NULL;
            }
#endif
            return -1;
         }
         
#ifndef DEMOVERSION
         if(hi->flags&HTTPIF_SSL)
         {  /* Validate socketbase again before SSL operations */
            if(!hi->socketbase)
            {  debug_printf("DEBUG: Opensocket: socketbase became NULL before SSL init, cleaning up SSL and returning -1\n");
               /* Clean up SSL resources that were allocated in Openlibraries() */
               if(hi->assl)
               {  Assl_closessl(hi->assl);
                  Assl_cleanup(hi->assl);
                  FREE(hi->assl);
                  hi->assl = NULL;
               }
               return -1;
            }
            debug_printf("DEBUG: Opensocket: Calling Assl_openssl() before creating socket\n");
            if(!Assl_openssl(hi->assl))
            {  debug_printf("DEBUG: Opensocket: Assl_openssl() failed, cleaning up SSL and returning -1\n");
               Assl_closessl(hi->assl);
               Assl_cleanup(hi->assl);
               FREE(hi->assl);
               hi->assl = NULL;
               return -1;
            }
            debug_printf("DEBUG: Opensocket: Assl_openssl() succeeded\n");
            
            /* Validate socketbase is still valid after SSL init */
            /* Another task might have closed the library during SSL initialization */
            if(!hi->socketbase)
            {  debug_printf("DEBUG: Opensocket: socketbase became NULL during SSL init, cleaning up SSL and returning -1\n");
               Assl_closessl(hi->assl);
               Assl_cleanup(hi->assl);
               FREE(hi->assl);
               hi->assl = NULL;
               return -1;
            }
         }
#endif
         /* Final validation before creating socket */
         if(!hi->socketbase)
         {  debug_printf("DEBUG: Opensocket: socketbase is NULL before socket creation, cleaning up SSL and returning -1\n");
#ifndef DEMOVERSION
            /* Clean up SSL resources that were allocated in Openlibraries() */
            if(hi->assl)
            {  Assl_closessl(hi->assl);
               Assl_cleanup(hi->assl);
               FREE(hi->assl);
               hi->assl = NULL;
            }
#endif
            return -1;
         }
         sock=a_socket(hent->h_addrtype,SOCK_STREAM,0,hi->socketbase);
         debug_printf("DEBUG: Opensocket: Created socket %ld\n", sock);
         if(sock<0)
         {  debug_printf("DEBUG: Opensocket: Socket creation failed, cleaning up SSL\n");
            if(hi->assl)
            {  Assl_closessl(hi->assl);
               Assl_cleanup(hi->assl);
               FREE(hi->assl);
               hi->assl = NULL;
            }
            return -1;
         }
      }
   }
   
   /* NOTE: Timeouts are NOT applied here - they are applied in Connect() */
   /* AFTER the SSL handshake completes. This prevents the receive timeout */
   /* from interfering with the handshake process. */
   
   return sock;
}

/* Connect and make SSL connection. Returns TRUE if success. */
static BOOL Connect(struct Httpinfo *hi,struct hostent *hent)
{  BOOL ok=FALSE;
   long result;
   UBYTE *hostname_str;
   struct timeval timeout;  /* C89: Declare at start */
   struct Library *saved_socketbase_connect;  /* C89: Declare at start */
   
   if(hi->port==-1)
   {  if(hi->flags&HTTPIF_SSL) hi->port=443;
      else hi->port=80;
   }
   debug_printf("DEBUG: Attempting to connect to %s:%ld (SSL=%s)\n", 
          hent->h_name, hi->port, (hi->flags&HTTPIF_SSL) ? "YES" : "NO");
   
   /* CRITICAL: Validate SocketBase before using socket functions */
   if(!hi->socketbase)
   {  debug_printf("DEBUG: Connect: socketbase is NULL, cannot connect\n");
      return FALSE;
   }
   
   /* Attempt TCP connection */
   if(!a_connect(hi->sock,hent,hi->port,hi->socketbase))
   {  /* TCP connect succeeded - a_connect returns 0 on success */
      debug_printf("DEBUG: TCP connect succeeded\n");
      ok=TRUE;
#ifndef DEMOVERSION
      /* For SSL connections, proceed with SSL handshake */
      if(hi->flags&HTTPIF_SSL)
      {  hostname_str = hi->hostname ? hi->hostname : (UBYTE *)"(NULL)";
         debug_printf("DEBUG: Starting SSL connection to '%s'\n", hostname_str);
         
         /* Check for exit signal before starting blocking SSL handshake */
         if(Checktaskbreak())
         {  debug_printf("DEBUG: Connect: Exit signal detected, aborting SSL connection\n");
            /* Close socket to interrupt any blocking SSL operations */
            if(hi->sock >= 0 && hi->socketbase)
            {  debug_printf("DEBUG: Connect: Closing socket to interrupt SSL operations\n");
               a_close(hi->sock, hi->socketbase);
               hi->sock = -1;
            }
            /* Clean up SSL resources */
            if(hi->assl)
            {  Assl_closessl(hi->assl);
               Assl_cleanup(hi->assl);
               FREE(hi->assl);
               hi->assl = NULL;
            }
            ok=FALSE;
         }
         /* Ensure SSL resources are valid before attempting connection */
         else if(hi->assl && hi->sock >= 0)
         {  /* Assl_connect() now handles SocketBase internally via per-connection socketbase */
            /* No need to set global SocketBase here - it's stored in the Assl struct to avoid race conditions */
            result=Assl_connect(hi->assl,hi->sock,hi->hostname);
            debug_printf("DEBUG: SSL connect result: %ld (ASSLCONNECT_OK=%d)\n", result, ASSLCONNECT_OK);
            ok=(result==ASSLCONNECT_OK);
            
            /* CRITICAL: If SSL_connect() fails on a reused connection, treat it as stale */
            /* The socket might have been closed by the server, or SSL state is invalid */
            if(!ok && hi->connection_reused)
            {  debug_printf("DEBUG: Connect: SSL_connect() failed on reused connection - treating as stale\n");
               /* Close the stale connection */
               Assl_closessl(hi->assl);
               if(hi->sock >= 0 && hi->socketbase)
               {  a_close(hi->sock, hi->socketbase);
               }
               hi->sock = -1;
               hi->connection_reused = FALSE;
               /* Return FALSE so caller can retry with fresh connection */
               ok = FALSE;
            }
            else if(result==ASSLCONNECT_DENIED) 
            {  hi->flags|=HTTPIF_NOSSLREQ;
            }
            else if(!ok && !(hi->flags&HTTPIF_NOSSLREQ))
            {  UBYTE errbuf[128],*p;
               p=Assl_geterror(hi->assl,errbuf);
               /* Modern browser behavior: fail connection instead of allowing unsecure fallback */
               /* if(Securerequest(hi,p))
               {  hi->flags|=HTTPIF_RETRYNOSSL;
               }
               */
               debug_printf("DEBUG: SSL connect failed: %s - failing connection (no unsecure fallback)\n", p);
               /* SSL connect failed - clean up SSL resources */
               if(hi->assl)
               {  Assl_closessl(hi->assl);
                  Assl_cleanup(hi->assl);
                  FREE(hi->assl);
                  hi->assl = NULL;
               }
            }
         }
         else
         {  debug_printf("DEBUG: SSL connection aborted - invalid SSL resources (assl=%p, sock=%ld)\n",
                   hi->assl, hi->sock);
            ok=FALSE;
            hi->flags|=HTTPIF_NOSSLREQ;
            /* Clean up SSL resources if they exist but are invalid */
            if(hi->assl)
            {  Assl_closessl(hi->assl);
               Assl_cleanup(hi->assl);
               FREE(hi->assl);
               hi->assl = NULL;
            }
         }
      }
#endif
      
      /* CRITICAL: Apply timeouts AFTER connection (and SSL handshake) is established */
      /* This prevents SO_RCVTIMEO from interfering with the SSL handshake process */
      /* The handshake uses the standard TCP connect timeout, not the data receive timeout */
      if(ok && hi->sock >= 0)
      {  /* CRITICAL: Validate SocketBase before using socket functions */
         if(!hi->socketbase)
         {  debug_printf("DEBUG: Connect: socketbase is NULL, cannot set timeouts - cleaning up SSL\n");
#ifndef DEMOVERSION
            if(hi->assl)
            {  Assl_closessl(hi->assl);
               Assl_cleanup(hi->assl);
               FREE(hi->assl);
               hi->assl = NULL;
            }
#endif
            return FALSE;
         }
         
         timeout.tv_sec = 15;  /* 15 second timeout per operation */
         timeout.tv_usec = 0;
         
         /* Set global SocketBase for setsockopt() from proto/bsdsocket.h */
         saved_socketbase_connect = AcquireSocketBaseSwap(hi->socketbase);
         
         /* CRITICAL: Validate SocketBase is still valid after assignment */
         if(!SocketBase)
         {  debug_printf("DEBUG: Connect: SocketBase became NULL after assignment - cleaning up SSL\n");
            ReleaseSocketBaseSwap(saved_socketbase_connect); /* Restore before returning */
#ifndef DEMOVERSION
            if(hi->assl)
            {  Assl_closessl(hi->assl);
               Assl_cleanup(hi->assl);
               FREE(hi->assl);
               hi->assl = NULL;
            }
#endif
            return FALSE;
         }
         
         /* Set receive timeout - This prevents operations from hanging indefinitely */
         /* SO_RCVTIMEO is per-operation, so each recv() gets a fresh 15-second timeout */
         /* Since timeout resets after each successful receive, long transfers can complete */
         if(setsockopt(hi->sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
         {  /* Timeout setting failed, but continue */
         }
         
         /* Set send timeout - This prevents operations from hanging indefinitely */
         /* SO_SNDTIMEO is per-operation, so each send() gets a fresh 15-second timeout */
         if(setsockopt(hi->sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
         {  /* Timeout setting failed, but continue */
         }
         
         /* Restore SocketBase immediately after setsockopt() */
         ReleaseSocketBaseSwap(saved_socketbase_connect);
         
         debug_printf("DEBUG: Connect: Applied socket timeouts after connection established\n");
      }
   }
   else
   {  /* TCP connect failed - a_connect returns non-zero on failure */
      long errno_value;
      /* Use Errno() function from bsdsocket.library to get error code */
      /* Note: Errno() is deprecated per SDK but still works. Modern way is SocketBaseTags() with SBTC_ERRNO */
      if(hi->socketbase)
      {  struct Library *saved_socketbase = AcquireSocketBaseSwap(hi->socketbase);
         errno_value = Errno(); /* Get error code from bsdsocket.library */
         ReleaseSocketBaseSwap(saved_socketbase);
      }
      else
      {  errno_value = 0; /* No socketbase - can't get errno */
      }
      debug_printf("DEBUG: TCP connect failed (errno=%ld)\n", errno_value);
      
      /* Provide more specific error reporting based on errno */
      if(errno_value == ETIMEDOUT)
      {  debug_printf("DEBUG: Connect: Connection timeout\n");
         hi->status = -1; /* Use negative status to indicate timeout */
      }
      else if(errno_value == ECONNREFUSED)
      {  debug_printf("DEBUG: Connect: Connection refused by server\n");
         hi->status = -2; /* Use negative status to indicate connection refused */
      }
      else if(errno_value == ECONNRESET)
      {  debug_printf("DEBUG: Connect: Connection reset by peer\n");
         hi->status = -3; /* Use negative status to indicate connection reset */
      }
      else if(errno_value == ENETUNREACH)
      {  debug_printf("DEBUG: Connect: Network unreachable\n");
         hi->status = -4; /* Use negative status to indicate network unreachable */
      }
      else if(errno_value == EHOSTUNREACH)
      {  debug_printf("DEBUG: Connect: Host unreachable\n");
         hi->status = -5; /* Use negative status to indicate host unreachable */
      }
      else
      {  debug_printf("DEBUG: Connect: Connection failed with error %ld\n", errno_value);
         hi->status = -6; /* Use negative status to indicate other connection error */
      }
      
      /* TCP connect failed - cannot proceed with SSL or HTTP */
      /* Set ok=FALSE to indicate connection failure */
      ok=FALSE;
#ifndef DEMOVERSION
      if(hi->flags&HTTPIF_SSL)
      {  /* Only proceed if this is an SSL tunnel - tunnel setup uses separate connection */
         if(hi->flags&HTTPIF_SSLTUNNEL)
         {  UBYTE *creq;
            UBYTE *p;
            long creqlen;
            
            creqlen = strlen(tunnelrequest) + strlen(hi->tunnel);
            if(hi->prxauth && hi->prxauth->cookie)
            {  creqlen+=strlen(proxyauthorization)+strlen(hi->prxauth->cookie);
            }
            creqlen+=16;
            if(creq=ALLOCTYPE(UBYTE,creqlen,0))
            {  p=creq;
               p+=sprintf(p,tunnelrequest,hi->tunnel);
               if(hi->prxauth && hi->prxauth->cookie)
                  p+=sprintf(p,proxyauthorization,hi->prxauth->cookie);
               p+=sprintf(p,"\r\n");
               creqlen=p-creq;
#ifdef BETAKEYFILE
               if(httpdebug)
               {  Write(Output(),"\n",1);
                  Write(Output(),creq,creqlen);
               }
#endif
               /* Temporarily turn off SSL since we don't have a SSL connection yet */
               hi->flags&=~HTTPIF_SSL;
               if(Send(hi,creq,creqlen)==creqlen)
               {  
                  if(Readresponse(hi))
                  {  
                     Nextline(hi);
                     if(Readheaders(hi))
                     {  
                        Nextline(hi);
                        if(hi->flags&HTTPIF_TUNNELOK)
                        {  
                           ok=TRUE;
                        }
                     }
                  }
               }
               hi->flags|=HTTPIF_SSL;
               FREE(creq);
            }
         
            /* If tunnel setup succeeded, proceed with SSL connection */
         if(ok)
            {  hostname_str = hi->hostname ? hi->hostname : (UBYTE *)"(NULL)";
               debug_printf("DEBUG: Starting SSL connection to '%s'\n", hostname_str);
               
               /* Ensure SSL resources are valid before attempting connection */
               if(hi->assl && hi->sock >= 0)
               {  result=Assl_connect(hi->assl,hi->sock,hi->hostname);
                  debug_printf("DEBUG: SSL connect result: %ld (ASSLCONNECT_OK=%d)\n", result, ASSLCONNECT_OK);
            ok=(result==ASSLCONNECT_OK);
            if(result==ASSLCONNECT_DENIED) hi->flags|=HTTPIF_NOSSLREQ;
            if(!ok && !(hi->flags&HTTPIF_NOSSLREQ))
            {  UBYTE errbuf[128],*p;
               p=Assl_geterror(hi->assl,errbuf);
               /* Modern browser behavior: fail connection instead of allowing unsecure fallback */
               /* if(Securerequest(hi,p))
               {  hi->flags|=HTTPIF_RETRYNOSSL;
               }
               */
               debug_printf("DEBUG: SSL connect failed after tunnel: %s - failing connection (no unsecure fallback)\n", p);
               /* SSL connect failed after tunnel - clean up SSL resources */
               if(hi->assl)
               {  Assl_closessl(hi->assl);
                  Assl_cleanup(hi->assl);
                  FREE(hi->assl);
                  hi->assl = NULL;
               }
            }
               }
               else
               {  debug_printf("DEBUG: SSL connection aborted - invalid SSL resources (assl=%p, sock=%ld)\n",
                         hi->assl, hi->sock);
                  ok=FALSE;
                  hi->flags|=HTTPIF_NOSSLREQ;
                  /* Clean up SSL resources if they exist but are invalid */
                  if(hi->assl)
                  {  Assl_closessl(hi->assl);
                     Assl_cleanup(hi->assl);
                     FREE(hi->assl);
                     hi->assl = NULL;
                  }
               }
            }
         }
         else
         {  /* For direct SSL connections, TCP connect MUST succeed first */
            /* Cannot proceed with SSL if TCP connection failed */
            debug_printf("DEBUG: TCP connect failed for SSL connection - cleaning up SSL resources\n");
            /* Clean up SSL resources that were initialized but never used */
            if(hi->assl)
            {  Assl_closessl(hi->assl);
               Assl_cleanup(hi->assl);
               FREE(hi->assl);
               hi->assl = NULL;
            }
            ok=FALSE;
         }
      }
      else
#endif
      {  /* For non-SSL connections, TCP connect failure means connection failed */
         ok=FALSE;
      }
   }
   return ok;
}

/* Send multipart form data. */
static BOOL Sendmultipartdata(struct Httpinfo *hi,struct Fetchdriver *fd,FILE *fp)
{  struct Multipartpart *mpp;
   long lock,fh,l;
   BOOL ok=TRUE;
   for(mpp=fd->multipart->parts.first;ok && mpp->next;mpp=mpp->next)
   {  if(mpp->lock)
      {  Updatetaskattrs(AOURL_Netstatus,NWS_UPLOAD,TAG_END);
         Tcpmessage(fd,TCPMSG_UPLOAD);
         /* We can't just use the mpp->lock because we might need to send the
          * message again after a 301/302 status. */
         if(lock=DupLock(mpp->lock))
         {  if(fh=OpenFromLock(lock))
            {  while(ok && (l=Read(fh,fd->block,fd->blocksize)))
               {  
#ifdef DEVELOPER
                  if(httpdebug) Write(Output(),fd->block,l);
                  if(fp) fwrite(fd->block,l,1,fp);
                  else
#endif
                  ok=(Send(hi,fd->block,l)==l);
               }
               Close(fh);
            }
            else UnLock(lock);
         }
      }
      else
      {  
#ifdef DEVELOPER
         if(httpdebug) Write(Output(),fd->multipart->buf.buffer+mpp->start,mpp->length);
         if(fp) fwrite(fd->multipart->buf.buffer+mpp->start,mpp->length,1,fp);
         else
#endif
         ok=(Send(hi,fd->multipart->buf.buffer+mpp->start,mpp->length)==mpp->length);
      }
   }
#ifdef DEVELOPER
   if(httpdebug) Write(Output(),"\n",1);
#endif
   return ok;
}

#ifndef DEMOVERSION
static BOOL Formwarnrequest(void)
{  return (BOOL)Syncrequest(AWEBSTR(MSG_FORMWARN_TITLE),
      haiku?HAIKU16:AWEBSTR(MSG_FORMWARN_WARNING),AWEBSTR(MSG_FORMWARN_BUTTONS),0);
}
#endif

static void Httpretrieve(struct Httpinfo *hi,struct Fetchdriver *fd)
{  struct hostent *hent;
   long reqlen,msglen,result;
   UBYTE *request = fd->block;  /* Initialize to fd->block - Buildrequest() may allocate new buffer */
   UBYTE *p,*q;
   BOOL error=FALSE;
   int retry_count;  /* C89: Declare at start */
   BOOL try_again;   /* C89: Declare at start */
   long sent;        /* C89: Declare at start */
   
   debug_printf("DEBUG: Httpretrieve: ENTRY - URL=%s, SSL=%d\n",
          fd ? (char *)fd->name : "(null)", fd ? BOOLVAL(fd->flags&FDVF_SSL) : 0);
   
   hi->blocklength=0;
   hi->nextscanpos=0;
   if(fd->flags&FDVF_SSL) hi->flags|=HTTPIF_SSL;
   hi->fd=fd;
   /* Initialize Range request support fields */
   if(!(hi->flags & HTTPIF_RANGE_REQUEST))
   {  hi->bytes_received = 0;  /* Only reset if not using Range request */
   }
   hi->server_supports_range = FALSE;  /* Will be set from Accept-Ranges header */
   hi->full_file_size = 0;  /* Will be set from Content-Range header for 206 responses */
   debug_printf("DEBUG: Httpretrieve: Initialized - flags=0x%04X, blocklength=%ld, bytes_received=%ld\n",
          hi->flags, hi->blocklength, hi->bytes_received);
#ifdef DEVELOPER
   if(STRNEQUAL(fd->name,"&&&&",4)
   ||STRNIEQUAL(fd->name,"http://&&&&",11)
   ||STRNIEQUAL(fd->name,"https://&&&&",12)
   ||STRNIEQUAL(fd->name,"ftp://&&&&",10))
   {  UBYTE name[64]="CON:20/200/600/200/HTTP/screen ";
      FILE *f;
      strcat(name,(UBYTE *)Agetattr(Aweb(),AOAPP_Screenname));
      if(
#ifndef DEMOVERSION
         (!(hi->fd->flags&FDVF_FORMWARN) || (hi->flags&HTTPIF_SSL) || Formwarnrequest())
      && 
#endif
         (f=fopen(name,"r+")))
      {  fprintf(f,"[%s %d%s]\n",hi->connect,hi->port,
            (hi->flags&HTTPIF_SSL)?" SECURE":"");
         reqlen=Buildrequest(fd,hi,&request);
         fwrite(request,reqlen,1,f);
         if(fd->multipart) Sendmultipartdata(hi,fd,f);
         else if(fd->postmsg)
         {  fwrite(fd->postmsg,strlen(fd->postmsg),1,f);
            fwrite("\n",1,1,f);
         }
         fflush(f);
         if(request!=fd->block) FREE(request);
         if(hi->flags&HTTPIF_SSL)
         {  Updatetaskattrs(AOURL_Cipher,"AWEB-DEBUG",TAG_END);
         }
         Updatetaskattrs(AOURL_Netstatus,NWS_WAIT,TAG_END);
         Tcpmessage(fd,TCPMSG_WAITING,hi->flags&HTTPIF_SSL?"HTTPS":"HTTP");
         hi->socketbase=NULL;
         hi->sock=(long)f;
         Httpresponse(hi,TRUE);
         fclose(f);
      }
   }
   else
   {
#endif
   /* Retry loop for stale keep-alive connections (RFC 7230) */
   retry_count = 0;
   do
   {  try_again = FALSE; /* Default to single run */
      
      debug_printf("DEBUG: Httpretrieve: Calling Openlibraries() (retry_count=%d)\n", retry_count);
      result=Openlibraries(hi);
      debug_printf("DEBUG: Httpretrieve: Openlibraries() returned %ld\n", result);
      
#ifndef DEMOVERSION
      if(result && (hi->fd->flags&FDVF_FORMWARN) && !(hi->flags&HTTPIF_SSL))
      {  debug_printf("DEBUG: Httpretrieve: Calling Formwarnrequest()\n");
         result=Formwarnrequest();
         debug_printf("DEBUG: Httpretrieve: Formwarnrequest() returned %ld\n", result);
      }
#endif
      
      if(result)
   {  /* If connection was reused from pool, skip DNS lookup, Opensocket(), and Connect() */
      /* The connection is already established and ready to use */
      if(hi->connection_reused)
      {  debug_printf("DEBUG: Httpretrieve: Using pooled %s connection - skipping DNS, Opensocket(), and Connect()\n",
                     (hi->flags&HTTPIF_SSL) ? "SSL" : "HTTP");
         result = TRUE; /* Connection already established */
         
         /* Apply timeouts to reused connection (refresh them) */
         /* This ensures reused connections have fresh timeouts */
         if(hi->sock >= 0 && hi->socketbase)
         {  struct timeval timeout;
            struct Library *saved_socketbase;
            
            /* CRITICAL: Validate SocketBase before using socket functions */
            if(!hi->socketbase)
            {  debug_printf("DEBUG: Httpretrieve: socketbase is NULL, cannot set timeouts on reused connection\n");
               result = FALSE;
               error = TRUE;
               break;
            }
            
            timeout.tv_sec = 15;  /* 15 second timeout per operation */
            timeout.tv_usec = 0;
            
            saved_socketbase = AcquireSocketBaseSwap(hi->socketbase);
            
            /* CRITICAL: Validate SocketBase is still valid after assignment */
            if(!SocketBase)
            {  debug_printf("DEBUG: Httpretrieve: SocketBase became NULL after assignment\n");
               ReleaseSocketBaseSwap(saved_socketbase); /* Restore before continuing */
               result = FALSE;
               error = TRUE;
               break;
            }
            
            /* Set receive and send timeouts */
            setsockopt(hi->sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
            setsockopt(hi->sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
            
            ReleaseSocketBaseSwap(saved_socketbase);
            debug_printf("DEBUG: Httpretrieve: Applied timeouts to reused %s connection\n",
                        (hi->flags&HTTPIF_SSL) ? "SSL" : "HTTP");
         }
      }
      else
      {  /* New connection - need DNS lookup, Opensocket(), and Connect() */
         debug_printf("DEBUG: Httpretrieve: Libraries opened, starting DNS lookup for '%s'\n",
                     hi->connect ? (char *)hi->connect : "(null)");
         Updatetaskattrs(AOURL_Netstatus,NWS_LOOKUP,TAG_END);
         Tcpmessage(fd,TCPMSG_LOOKUP,hi->connect);
         
         debug_printf("DEBUG: Httpretrieve: Calling Lookup() for '%s'\n", hi->connect ? (char *)hi->connect : "(null)");
         if(hent=Lookup(hi->connect,hi->socketbase))
         {  debug_printf("DEBUG: Httpretrieve: Lookup() succeeded, hostname='%s'\n",
                   hent->h_name ? (char *)hent->h_name : "(null)");
            
            debug_printf("DEBUG: Httpretrieve: Calling Opensocket()\n");
            if((hi->sock=Opensocket(hi,hent))>=0)
            {  debug_printf("DEBUG: Httpretrieve: Opensocket() succeeded, sock=%ld\n", hi->sock);
               Updatetaskattrs(AOURL_Netstatus,NWS_CONNECT,TAG_END);
               Tcpmessage(fd,TCPMSG_CONNECT,
                  hi->flags&HTTPIF_SSL?"HTTPS":"HTTP",hent->h_name);
               
               /* Check for exit signal before starting blocking connection */
               if(Checktaskbreak())
                  {  debug_printf("DEBUG: Httpretrieve: Exit signal detected, aborting connection\n");
                     /* Close socket to interrupt any blocking operations */
                     if(hi->sock >= 0 && hi->socketbase)
                     {  debug_printf("DEBUG: Httpretrieve: Closing socket to interrupt operations\n");
                        a_close(hi->sock, hi->socketbase);
                        hi->sock = -1;
                     }
                     /* Clean up SSL resources */
#ifndef DEMOVERSION
                     if(hi->assl)
                     {  Assl_closessl(hi->assl);
                        Assl_cleanup(hi->assl);
                        FREE(hi->assl);
                        hi->assl = NULL;
                     }
#endif
                     result = FALSE;
                     error = TRUE;
                  }
                  else
                  {  result = Connect(hi,hent);
                     
                     /* CRITICAL: If Connect() failed and we haven't retried yet, retry with fresh connection */
                     /* This handles stale reused connections (SSL_connect() fails) and transient network errors */
                     if(!result && Checktaskbreak())
                     {  debug_printf("DEBUG: Httpretrieve: Connect() failed due to exit signal, not retrying\n");
                        error = TRUE;
                        try_again = FALSE;
                     }
                     else if(!result && retry_count == 0)
                     {  debug_printf("DEBUG: Httpretrieve: Connect() failed - retrying with fresh connection (retry_count=%d)\n", retry_count);
                        
                        /* Ensure cleanup is complete */
                        if(hi->connection_reused)
                        {  /* Connection was reused but Connect() failed - cleanup already done in Connect() */
                           hi->connection_reused = FALSE;
                        }
                        else
                        {  /* Fresh connection failed - clean up here */
#ifndef DEMOVERSION
                           if(hi->assl)
                           {  Assl_closessl(hi->assl);
                              Assl_cleanup(hi->assl);
                              FREE(hi->assl);
                              hi->assl = NULL;
                           }
#endif
                           if(hi->sock >= 0 && hi->socketbase)
                           {  a_close(hi->sock, hi->socketbase);
                           }
                           hi->sock = -1;
                        }
                        
                        /* Free request buffer if allocated */
                        if(request != fd->block) FREE(request);
                        
                        retry_count++;
                        try_again = TRUE;
                        continue; /* Jump to start of do-while */
                     }
                  }
               }
               
               if(result)
               {  debug_printf("DEBUG: Httpretrieve: Connect() succeeded\n");
#ifndef DEMOVERSION
               if(hi->flags&HTTPIF_SSL)
               {  debug_printf("DEBUG: Httpretrieve: SSL connection established, getting cipher info\n");
                  p=Assl_getcipher(hi->assl);
                  q=Assl_libname(hi->assl);
                  if(p || q)
                  {  debug_printf("DEBUG: Httpretrieve: Cipher=%s, Library=%s\n",
                            p ? (char *)p : "(null)", q ? (char *)q : "(null)");
                     Updatetaskattrs(AOURL_Cipher,p,
                        AOURL_Ssllibrary,q,
                        TAG_END);
                  }
               }
#endif
               
               debug_printf("DEBUG: Httpretrieve: Building HTTP request\n");
               reqlen=Buildrequest(fd,hi,&request);
               debug_printf("DEBUG: Httpretrieve: Request built, length=%ld, calling Send()\n", reqlen);
               
               /* Send Request */
               sent = Send(hi,request,reqlen);
               
               /* FIX: Detect Stale Connection on Send */
               if(sent != reqlen && hi->connection_reused && retry_count == 0)
               {  debug_printf("DEBUG: Keep-Alive Send failed (sent=%ld, expected=%ld). Retrying with fresh connection.\n",
                              sent, reqlen);
                  
                  /* 1. Clean up bad connection */
#ifndef DEMOVERSION
                  if(hi->assl)
                  {  Assl_closessl(hi->assl);
                  }
#endif
                  if(hi->sock >= 0 && hi->socketbase)
                  {  a_close(hi->sock, hi->socketbase);
                  }
                  hi->sock = -1;
                  
                  /* 2. Mark as not reused so Openlibraries creates fresh */
                  hi->connection_reused = FALSE;
                  
                  /* 3. Free request buffer if allocated */
                  if(request != fd->block) FREE(request);
                  
                  /* 4. Trigger retry loop */
                  retry_count++;
                  try_again = TRUE;
                  continue; /* Jump to start of do-while */
               }
               
               result = (sent == reqlen);
               debug_printf("DEBUG: Httpretrieve: Send() returned, result=%ld (expected %ld)\n", result, reqlen);
#ifdef BETAKEYFILE
               if(httpdebug)
               {  Write(Output(),"\n",1);
                  Write(Output(),request,reqlen);
               }
#endif
               if(result)
               {  if(fd->multipart)
                  {  debug_printf("DEBUG: Httpretrieve: Sending multipart data\n");
                     result=Sendmultipartdata(hi,fd,NULL);
                     debug_printf("DEBUG: Httpretrieve: Sendmultipartdata() returned %ld\n", result);
                  }
                  else if(fd->postmsg)
                  {  msglen=strlen(fd->postmsg);
                     debug_printf("DEBUG: Httpretrieve: Sending POST message, length=%ld\n", msglen);
                     result=(Send(hi,fd->postmsg,msglen)==msglen);
                     debug_printf("DEBUG: Httpretrieve: POST Send() returned, result=%ld (expected %ld)\n",
                            result, msglen);
#ifdef BETAKEYFILE
                     if(httpdebug)
                     {  Write(Output(),fd->postmsg,msglen);
                        Write(Output(),"\n\n",2);
                     }
#endif
                  }
               }
               if(request!=fd->block) FREE(request);
               
               if(result)
               {  debug_printf("DEBUG: Httpretrieve: Request sent successfully, calling Httpresponse()\n");
                  Updatetaskattrs(AOURL_Netstatus,NWS_WAIT,TAG_END);
                  Tcpmessage(fd,TCPMSG_WAITING,hi->flags&HTTPIF_SSL?"HTTPS":"HTTP");
                  
                  /* Check for exit signal before starting blocking HTTP response */
                  if(Checktaskbreak())
                  {  debug_printf("DEBUG: Httpretrieve: Exit signal detected, aborting HTTP response\n");
                     /* Close socket first to interrupt any blocking operations */
                     if(hi->sock >= 0 && hi->socketbase)
                     {  debug_printf("DEBUG: Httpretrieve: Closing socket due to exit\n");
                        a_close(hi->sock, hi->socketbase);
                        hi->sock = -1;
                     }
                     /* SSL cleanup will happen at end of Httpretrieve() via Assl_cleanup() */
                     error=TRUE;
                  }
                  else
                  {  debug_printf("DEBUG: Httpretrieve: About to call Httpresponse() - this may take a while\n");
                     
                     /* FIX: Detect Stale Connection on Immediate Receive */
                     /* Reset status before reading to detect connection failures */
                     hi->status = 0;
                     Httpresponse(hi,TRUE);
                     debug_printf("DEBUG: Httpretrieve: Httpresponse() returned - status=%ld, blocklength=%ld\n",
                                 hi->status, hi->blocklength);
                     
                     /* Check for incomplete transfer and retry with Range request if supported */
                     if(hi->status == 0 && hi->bytes_received > 0 && hi->server_supports_range && 
                        hi->partlength > 0 && hi->bytes_received < hi->partlength && retry_count == 0)
                     {  debug_printf("DEBUG: Incomplete transfer detected (%ld/%ld bytes). Retrying with Range request.\n",
                                   hi->bytes_received, hi->partlength);
                        
                        /* Cleanup current connection */
#ifndef DEMOVERSION
                        if(hi->assl)
                        {  Assl_closessl(hi->assl);
                           Assl_cleanup(hi->assl);
                           FREE(hi->assl);
                           hi->assl = NULL;
                        }
#endif
                        if(hi->sock >= 0 && hi->socketbase)
                        {  a_close(hi->sock, hi->socketbase);
                        }
                        hi->sock = -1;
                        hi->connection_reused = FALSE;
                        
                        /* Set Range request flag for retry */
                        hi->flags |= HTTPIF_RANGE_REQUEST;
                        
                        /* Clear error flag to allow retry */
                        Updatetaskattrs(AOURL_Error, FALSE, TAG_END);
                        
                        /* Free request buffer if allocated */
                        if(request != fd->block) FREE(request);
                        
                        /* Trigger retry loop */
                        retry_count++;
                        try_again = TRUE;
                        continue; /* Jump to start of do-while */
                     }
                     
                     /* If status is 0 (no headers read) or specific socket error */
                     /* AND we reused a connection AND we haven't retried yet... */
                     if(hi->status <= 0 && hi->connection_reused && retry_count == 0)
                     {  debug_printf("DEBUG: Keep-Alive Receive failed (Server closed). Retrying fresh.\n");
                        
                        /* Cleanup and Retry logic */
#ifndef DEMOVERSION
                        if(hi->assl)
                        {  Assl_closessl(hi->assl);
                        }
#endif
                        if(hi->sock >= 0 && hi->socketbase)
                        {  a_close(hi->sock, hi->socketbase);
                        }
                        hi->sock = -1;
                        hi->connection_reused = FALSE;
                        
                        /* Clear any error flags set by the failed attempt */
                        Updatetaskattrs(AOURL_Error, FALSE, TAG_END);
                        
                        retry_count++;
                        try_again = TRUE;
                        continue; /* Jump to start of do-while */
                     }
                  }
               }
               else
               {  /* Send failed on a NON-reused connection */
                  debug_printf("DEBUG: Httpretrieve: Request send failed, setting error\n");
                  error=TRUE;
               }
            }
            else
            {  debug_printf("DEBUG: Httpretrieve: Connect() failed, status=%ld\n", hi->status);
               if(!(hi->flags&HTTPIF_RETRYNOSSL) && hi->status!=407)
               {  /* Provide more specific error reporting based on connection failure type */
                  if(hi->status < 0)
                  {  /* Negative status indicates specific connection error type */
                     switch(hi->status)
                     {  case -1:  /* ETIMEDOUT */
                           debug_printf("DEBUG: Httpretrieve: Connection timeout - server did not respond\n");
                           break;
                        case -2:  /* ECONNREFUSED */
                           debug_printf("DEBUG: Httpretrieve: Connection refused - server rejected connection\n");
                           break;
                        case -3:  /* ECONNRESET */
                           debug_printf("DEBUG: Httpretrieve: Connection reset - server closed connection\n");
                           break;
                        case -4:  /* ENETUNREACH */
                           debug_printf("DEBUG: Httpretrieve: Network unreachable - no route to network\n");
                           break;
                        case -5:  /* EHOSTUNREACH */
                           debug_printf("DEBUG: Httpretrieve: Host unreachable - no route to host\n");
                           break;
                        default:
                           debug_printf("DEBUG: Httpretrieve: Connection failed with error code %ld\n", hi->status);
                           break;
                     }
                  }
                  debug_printf("DEBUG: Httpretrieve: Reporting connection error\n");
                  /* Report specific error type based on hi->status */
                  {  UBYTE *hostname_str;
                     hostname_str = (hi->flags&HTTPIF_SSLTUNNEL)?hi->hostport:(UBYTE *)hent->h_name;
                     if(hi->status == -1)  /* ETIMEDOUT */
                     {  Tcperror(fd,TCPERR_NOCONNECT_TIMEOUT, hostname_str);
                     }
                     else if(hi->status == -2)  /* ECONNREFUSED */
                     {  Tcperror(fd,TCPERR_NOCONNECT_REFUSED, hostname_str);
                     }
                     else if(hi->status == -3)  /* ECONNRESET */
                     {  Tcperror(fd,TCPERR_NOCONNECT_RESET, hostname_str);
                     }
                     else if(hi->status == -4)  /* ENETUNREACH */
                     {  Tcperror(fd,TCPERR_NOCONNECT_UNREACH, hostname_str);
                     }
                     else if(hi->status == -5)  /* EHOSTUNREACH */
                     {  Tcperror(fd,TCPERR_NOCONNECT_HOSTUNREACH, hostname_str);
                     }
                     else
                     {  /* Generic connection error */
                        Tcperror(fd,TCPERR_NOCONNECT, hostname_str);
                     }
                  }
            }
            }
            
            /* Cleanup logic (Keep-Alive pool return vs Close) */
            /* Only cleanup if we're not retrying */
            if(!try_again)
            {  debug_printf("DEBUG: Httpretrieve: Cleaning up connection\n");
               /* Return connection to pool if keep-alive is supported.
                * CRITICAL: Only pool the connection if there is no extra unread
                * data buffered. If hi->blocklength > 0 here, we have bytes that
                * belong to the next response / TLS close_notify / protocol noise.
                * Reusing such a connection can corrupt the next transfer and can
                * also lead to tasks blocking on exit. */
               if((hi->flags & HTTPIF_KEEPALIVE) && (hi->flags & HTTPIF_KEEPALIVE_REQ) && !error && hi->blocklength == 0)
               {  /* Return connection to pool for reuse */
                  debug_printf("DEBUG: Httpretrieve: Keep-alive enabled, returning connection to pool (sock=%ld)\n", hi->sock);
                  ReturnKeepAliveConnection(hi);
                  /* Clear connection_reused flag after returning to pool */
                  /* ReturnKeepAliveConnection() already cleared hi->assl and hi->socketbase */
                  hi->connection_reused = FALSE;
               }
               else
               {  /* Close connection normally */
                  if((hi->flags & HTTPIF_KEEPALIVE) && (hi->flags & HTTPIF_KEEPALIVE_REQ) && !error && hi->blocklength > 0)
                  {  debug_printf("DEBUG: Httpretrieve: Not pooling keep-alive connection: %ld buffered bytes remain\n",
                            hi->blocklength);
                  }
                  /* Clear connection_reused flag since we're closing the connection */
                  hi->connection_reused = FALSE;
                  
                  /* Close SSL connection BEFORE closing socket */
                  /* SSL shutdown needs the socket to still be open */
#ifndef DEMOVERSION
                  if(hi->assl)
                  {  debug_printf("DEBUG: Httpretrieve: Closing SSL connection\n");
                     Assl_closessl(hi->assl);
                     /* DON'T set hi->assl to NULL here - Assl_cleanup() will handle it */
                     /* Assl_closessl() only closes the connection, doesn't free the Assl structure */
                  }
#endif
                  /* Now safe to close socket - SSL has been properly shut down */
                  if(hi->sock >= 0)
                  {  debug_printf("DEBUG: Httpretrieve: Closing socket %ld\n", hi->sock);
                     a_close(hi->sock,hi->socketbase);
                     hi->sock = -1;
                     debug_printf("DEBUG: Httpretrieve: Socket closed\n");
                  }
               }
            }
            else
            {  debug_printf("DEBUG: Httpretrieve: Opensocket() failed, sock=%ld, setting error\n", hi->sock);
               error=TRUE;
            }
         }
         else
         {  debug_printf("DEBUG: Httpretrieve: Lookup() failed for '%s', reporting no host error\n",
                   hi->connect ? (char *)hi->connect : "(null)");
            Tcperror(fd,TCPERR_NOHOST,hi->hostname);
         }
      }
      
      /* Only call a_cleanup() if connection was NOT reused AND socketbase is still valid */
      /* Reused connections keep socketbase in use by the pool */
      /* ReturnKeepAliveConnection() clears socketbase to NULL, so check for NULL too */
      if(!try_again && !hi->connection_reused && hi->socketbase)
      {  debug_printf("DEBUG: Httpretrieve: Calling a_cleanup()\n");
         a_cleanup(hi->socketbase);
         debug_printf("DEBUG: Httpretrieve: a_cleanup() completed\n");
      }
      else if(!try_again && hi->connection_reused)
      {  debug_printf("DEBUG: Httpretrieve: Skipping a_cleanup() - connection was reused and will be returned to pool\n");
      }
      else if(!try_again && !hi->socketbase)
      {  debug_printf("DEBUG: Httpretrieve: Skipping a_cleanup() - socketbase was returned to pool (NULL)\n");
      }
   }
   else
   {  debug_printf("DEBUG: Httpretrieve: Openlibraries() or Formwarnrequest() failed, reporting no lib error\n");
      Tcperror(fd,TCPERR_NOLIB);
   }
   
   } while(try_again); /* Retry loop for stale keep-alive connections */
   
   if(error)
   {  Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
   }
#ifndef DEMOVERSION
   /* Clean up Assl structure */
   /* Must clean up Assl BEFORE closing socketbase library */
   /* This ensures SSL operations are fully complete before library is closed */
   /* Note: If keep-alive is enabled, ReturnKeepAliveConnection() already cleared hi->assl */
   /* Also skip cleanup if connection was reused - it's still in use by the pool */
   if(hi->assl)
   {  if(hi->connection_reused)
      {  debug_printf("DEBUG: Httpretrieve: Skipping Assl cleanup - connection was reused and will be returned to pool\n");
      }
      else
      {  debug_printf("DEBUG: Httpretrieve: Cleaning up Assl structure\n");
         /* Assl_closessl() should have already freed SSL resources */
         /* Assl_cleanup() will null out library bases to mark the object as dead */
         Assl_cleanup(hi->assl);
         /* Free the struct after Assl_cleanup() has cleaned it up */
         /* Assl_cleanup() no longer frees the struct to prevent use-after-free crashes */
         FREE(hi->assl);
         hi->assl=NULL;
         debug_printf("DEBUG: Httpretrieve: Assl structure cleaned up\n");
      }
   }
#endif
   
   /* Only close socketbase library after all SSL operations are complete */
   /* This ensures no concurrent SSL operations are using the library when we close it */
   /* Must wait until Assl is fully cleaned up before closing socketbase */
   /* Note: If keep-alive is enabled, ReturnKeepAliveConnection() already cleared hi->socketbase */
   /* Also skip cleanup if connection was reused - it's still in use by the pool */
   if(hi->socketbase)
   {  if(hi->connection_reused)
      {  debug_printf("DEBUG: Httpretrieve: Skipping socketbase cleanup - connection was reused and will be returned to pool\n");
      }
      else
      {  debug_printf("DEBUG: Httpretrieve: Closing socketbase library\n");
         CloseLibrary(hi->socketbase);
         hi->socketbase=NULL;
         debug_printf("DEBUG: Httpretrieve: Socketbase library closed\n");
      }
   }
#ifdef DEVELOPER
   }
#endif
   
   if(error)
   {  debug_printf("DEBUG: Httpretrieve: Setting error flag\n");
      Updatetaskattrs(
         AOURL_Error,TRUE,
         TAG_END);
   }
   
   debug_printf("DEBUG: Httpretrieve: EXIT - error=%d, status=%ld\n", error, hi->status);
}

/*-----------------------------------------------------------------------*/

void Httptask(struct Fetchdriver *fd)
{  struct Httpinfo hi={0};
   int loop_count=0;
   
   debug_printf("DEBUG: Httptask: ENTRY - URL=%s, proxy=%s, SSL=%d\n",
          fd ? (char *)fd->name : "(null)", fd && fd->proxy ? (char *)fd->proxy : "(none)",
          fd ? BOOLVAL(fd->flags&FDVF_SSL) : 0);
   
   if(Makehttpaddr(&hi,fd->proxy,fd->name,BOOLVAL(fd->flags&FDVF_SSL)))
   {  debug_printf("DEBUG: Httptask: Makehttpaddr succeeded\n");
      if(!prefs.limitproxy && !hi.auth) hi.auth=Guessauthorize(hi.hostport);
      if(fd->proxy && !prefs.limitproxy) hi.prxauth=Guessauthorize(fd->proxy);
      debug_printf("DEBUG: Httptask: Auth setup complete (auth=%p, prxauth=%p)\n", hi.auth, hi.prxauth);
      
      for(;;)
      {  loop_count++;
         debug_printf("DEBUG: Httptask: Loop iteration %d (redirect_count=%d)\n", loop_count, redirect_count);
         
         /* Protect against redirect loops - limit to 10 redirects */
         if(redirect_count >= 10)
         {  debug_printf("DEBUG: Httptask: Redirect loop detected (%d redirects), breaking to prevent infinite loop\n", redirect_count);
            Updatetaskattrs(AOURL_Error, TRUE, TAG_END);
            redirect_count=0; /* Reset counter after error */
            break;
         }
         
         if(fd->proxy && hi.auth && prefs.limitproxy)
         {  debug_printf("DEBUG: Httptask: Handling proxy auth limitproxy case\n");
            if(hi.connect) FREE(hi.connect);
            if(hi.tunnel) FREE(hi.tunnel);hi.tunnel=NULL;
            if(hi.hostport) FREE(hi.hostport);
            if(hi.abspath) FREE(hi.abspath);
            if(hi.hostname) FREE(hi.hostname);
            if(!Makehttpaddr(&hi,NULL,fd->name,BOOLVAL(fd->flags&FDVF_SSL)))
            {  debug_printf("DEBUG: Httptask: Makehttpaddr failed, breaking loop\n");
               break;
         }
            debug_printf("DEBUG: Httptask: Makehttpaddr succeeded after limitproxy reset\n");
         }
         
         hi.status=0;
         /* CRITICAL: Clean up any existing Assl before next iteration to prevent SSL context reuse */
         /* This prevents wild free defects when redirects cause multiple connections */
         if(hi.assl)
         {  debug_printf("DEBUG: Httptask: Cleaning up existing Assl before redirect iteration\n");
            Assl_closessl(hi.assl);
            Assl_cleanup(hi.assl);
            FREE(hi.assl);
            hi.assl = NULL;
         }
         /* CRITICAL: Reset socket and socketbase to prevent reuse */
         hi.sock = -1;
         hi.socketbase = NULL;
         debug_printf("DEBUG: Httptask: Calling Httpretrieve() - status reset to 0\n");
         Httpretrieve(&hi,fd);
         debug_printf("DEBUG: Httptask: Httpretrieve() returned - status=%ld, flags=0x%04X\n",
                hi.status, hi.flags);
         
         if(hi.flags&HTTPIF_RETRYNOSSL)
         {  UBYTE *url,*p;
            debug_printf("DEBUG: Httptask: RETRYNOSSL flag set, downgrading to HTTP\n");
            url=ALLOCTYPE(UBYTE,strlen(fd->name)+6,0);
            strcpy(url,"http");
            if(p=strchr(fd->name,':')) strcat(url,p);
            Updatetaskattrs(AOURL_Tempmovedto,url,TAG_END);
            FREE(url);
            debug_printf("DEBUG: Httptask: Breaking loop after RETRYNOSSL\n");
            break;
         }
         
         if(hi.status==401 && !(hi.flags&HTTPIF_AUTH) && hi.auth)
         {  debug_printf("DEBUG: Httptask: Status 401, attempting authentication\n");
            hi.flags|=HTTPIF_AUTH;
            Updatetaskattrs(
               AOURL_Contentlength,0,
               AOURL_Contenttype,"",
               TAG_END);
            if(!hi.auth->cookie) Authorize(fd,hi.auth,FALSE);
            if(hi.auth->cookie)
            {  debug_printf("DEBUG: Httptask: Auth cookie obtained, continuing loop\n");
               continue;
            }
            debug_printf("DEBUG: Httptask: Auth failed, setting error\n");
            Updatetaskattrs(AOURL_Error,TRUE,TAG_END);
         }
         
         if(hi.status==407 && !(hi.flags&HTTPIF_PRXAUTH) && hi.prxauth)
         {  debug_printf("DEBUG: Httptask: Status 407, attempting proxy authentication\n");
            hi.flags|=HTTPIF_PRXAUTH;
            Updatetaskattrs(
               AOURL_Contentlength,0,
               AOURL_Contenttype,"",
               TAG_END);
            if(!hi.prxauth->cookie) Authorize(fd,hi.prxauth,TRUE);
            if(hi.prxauth->cookie)
            {  debug_printf("DEBUG: Httptask: Proxy auth cookie obtained, continuing loop\n");
               continue;
            }
            debug_printf("DEBUG: Httptask: Proxy auth failed, setting error\n");
            Updatetaskattrs(AOURL_Error,TRUE,TAG_END);
         }
         
         /* Successful non-redirect response - reset redirect counter */
         if(hi.status >= 200 && hi.status < 300 && !hi.movedto)
         {  redirect_count=0; /* Reset redirect counter on successful completion */
            debug_printf("DEBUG: Httptask: Successful response (status=%ld), resetting redirect_count\n", hi.status);
            /* Clear Range request flag after successful completion */
            if(hi.flags & HTTPIF_RANGE_REQUEST)
            {  debug_printf("DEBUG: Httptask: Range request completed successfully, clearing flag\n");
               hi.flags &= ~HTTPIF_RANGE_REQUEST;
               hi.bytes_received = 0;  /* Reset for next transfer */
            }
         }
         
         debug_printf("DEBUG: Httptask: Breaking loop normally (status=%ld)\n", hi.status);
         break;
      }
      debug_printf("DEBUG: Httptask: Loop completed after %d iterations\n", loop_count);
   }
   else
   {  debug_printf("DEBUG: Httptask: Makehttpaddr failed, setting error\n");
      Updatetaskattrs(AOURL_Error,TRUE,TAG_END);
   }
   debug_printf("DEBUG: Httptask: EXIT\n");
   if(hi.connect) FREE(hi.connect);
   if(hi.tunnel) FREE(hi.tunnel);
   if(hi.hostport) FREE(hi.hostport);
   if(hi.abspath) FREE(hi.abspath);
   if(hi.hostname) FREE(hi.hostname);
   if(hi.boundary) FREE(hi.boundary); /* Fix memory leak of boundary element as reported in #33 */
   if(hi.auth) Freeauthorize(hi.auth);
   if(hi.prxauth) Freeauthorize(hi.prxauth);
   if(hi.movedtourl) FREE(hi.movedtourl);
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOURL_Eof,TRUE,
      AOURL_Terminate,TRUE,
      TAG_END);
}

#else /* LOCALONLY */

#include "aweb.h"

/* Stub functions for LOCALONLY build */
BOOL Inithttp(void)
{  return 1;
}

void Freehttp(void)
{
}

void CloseIdleKeepAliveConnections(void)
{
}

#endif /* LOCALONLY */

/*-----------------------------------------------------------------------*/

/* Enhanced multipart boundary detection */
static BOOL Findmultipartboundary(struct Httpinfo *hi, UBYTE *data, long length)
{  UBYTE *p = data;
   UBYTE *end = data + length;
   UBYTE *boundary = hi->boundary;
   long blen;
   
   if(!boundary) return FALSE;
   blen = strlen(boundary);
   
   while(p < end - blen)
   {  if(*p == '\r' || *p == '\n')
      {  p++;
         if(p < end && (*p == '\r' || *p == '\n')) p++;
         if(p < end && *p == '-' && p[1] == '-')
         {  if(STREQUAL(p + 2, boundary))
            {  return TRUE;
            }
         }
      }
      p++;
   }
   return FALSE;
}

/*-----------------------------------------------------------------------*/

BOOL Inithttp(void)
{  
#ifndef LOCALONLY
   InitSemaphore(&certsema);
   NEWLIST(&certaccepts);
   InitSemaphore(&debug_log_sema);
   debug_log_sema_initialized = TRUE;
   InitSemaphore(&keepalive_sema);
   keepalive_sema_initialized = TRUE;
   NEWLIST(&keepalive_pool);
   InitSemaphore(&socketbase_swap_sema);
   socketbase_swap_sema_initialized = TRUE;
#endif
   return TRUE;
}

void Freehttp(void)
{  
#ifndef LOCALONLY
   struct Certaccept *ca;
   struct KeepAliveConnection *conn;
   
   if(certaccepts.first)
   {  while(ca=REMHEAD(&certaccepts))
      {  if(ca->hostname) FREE(ca->hostname);
         if(ca->certname) FREE(ca->certname);
         FREE(ca);
      }
   }
   
   /* Clean up all keep-alive connections */
   if(keepalive_sema_initialized)
   {  ObtainSemaphore(&keepalive_sema);
      while(conn = (struct KeepAliveConnection *)REMHEAD(&keepalive_pool))
      {  if(conn->sock >= 0 && conn->socketbase)
         {  if(conn->assl) Assl_closessl(conn->assl);
            a_close(conn->sock, conn->socketbase);
         }
         if(conn->assl)
         {  Assl_cleanup(conn->assl);
            FREE(conn->assl);
         }
         if(conn->socketbase) CloseLibrary(conn->socketbase);
         if(conn->hostname) FREE(conn->hostname);
         FREE(conn);
      }
      ReleaseSemaphore(&keepalive_sema);
   }
#endif
}

