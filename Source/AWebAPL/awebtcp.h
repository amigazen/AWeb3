/**********************************************************************
 * 
 * This file is part of the AWeb distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
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

/* awebtcp.h - prototypes and pragmas for AWeb tcp and ssl switch libraries */

#ifndef AWEBTCP_H
#define AWEBTCP_H

#ifndef SYS_TYPES_H
#ifndef u_long
typedef unsigned long u_long;
#endif
#endif

#include <exec/types.h>
#include "awebssl.h"

/* low-level TCP functions */
/* defined in awebamitcp.c and awebinet225.c */

extern struct Library *AwebTcpBase;

int a_setup(struct Library *);
void a_cleanup(struct Library *);
struct hostent *a_gethostbyname(char *,struct Library *);
int a_socket(int,int,int,struct Library *);
int a_close(int,struct Library *);
int a_connect(int,struct hostent *,int,struct Library *);
int a_connect2(int,int,u_long, int,struct Library *);
int a_bind(int,struct sockaddr *,int,struct Library *);
int a_listen(int,int,struct Library *);
int a_accept(int,struct sockaddr *,int *,struct Library *);
int a_shutdown(int,int,struct Library *);
int a_send(int,char *,int,int,struct Library *);
int a_recv(int,char *,int,int,struct Library *);
int a_getsockname(int,struct sockaddr *,int *,struct Library *);

/* high-level SSL functions */
/* defined in amissl.c and miamissl.c */

extern struct Library *AwebSslBase;

/* TRUE if use_sema was acquired and SSL/task-ref cleanup completed; FALSE if
 * aborted (do not FREE the Assl — it still embeds a live SignalSemaphore). */
BOOL Assl_cleanup(struct Assl *);

/* Assl_cleanup() plus FREE(*passl) only on success; always clears *passl. */
void Assl_dispose(struct Assl **passl);

BOOL Assl_openssl(struct Assl *);

void Assl_closessl(struct Assl *);

/* Return values see awebssl.h */
long Assl_connect(struct Assl *,long sock,UBYTE *hostname);

/* Get descriptive text for last error. errbuf should be 128 bytes minimum */
char *Assl_geterror(struct Assl *,char *errbuf);

long Assl_write(struct Assl *,char *,int);

long Assl_read(struct Assl *,char *,int);

char *Assl_getcipher(struct Assl *);

char *Assl_libname(struct Assl *);

/* TRUE if no TLS plaintext or unread record data buffered in OpenSSL (safe to
 * treat connection idle for keep-alive pooling). */

BOOL Assl_idle_for_keepalive(struct Assl *);


/* Management functions */
/* defined in awebtcp.c */

/* Try to open TCP library. */
extern struct Library *Tcpopenlib(void);

/* Try to open SSL library. */
extern struct Assl *Tcpopenssl(struct Library *socketbase);


/* pragmas */

#pragma libcall AwebTcpBase a_setup 1e 801
#pragma libcall AwebTcpBase a_cleanup 24 801
#pragma libcall AwebTcpBase a_gethostbyname 2a 9802
#pragma libcall AwebTcpBase a_socket 30 821004
#pragma libcall AwebTcpBase a_close 36 8002
#pragma libcall AwebTcpBase a_connect 3c 918004
#pragma libcall AwebTcpBase a_connect2 42 8321005
#pragma libcall AwebTcpBase a_bind 48 918004
#pragma libcall AwebTcpBase a_listen 4e 81003
#pragma libcall AwebTcpBase a_accept 54 A98004
#pragma libcall AwebTcpBase a_shutdown 5a 81003
#pragma libcall AwebTcpBase a_send 60 9218005
#pragma libcall AwebTcpBase a_recv 66 9218005
#pragma libcall AwebTcpBase a_getsockname 6c A98004
/* #pragma libcall AwebTcpBase a_setsockopt 72 8321006 */

#pragma libcall AwebSslBase Assl_cleanup 1e 801
#pragma libcall AwebSslBase Assl_openssl 24 801
#pragma libcall AwebSslBase Assl_closessl 2a 801
#pragma libcall AwebSslBase Assl_connect 30 90803
#pragma libcall AwebSslBase Assl_geterror 36 9802
#pragma libcall AwebSslBase Assl_write 3c 09803
#pragma libcall AwebSslBase Assl_read 42 09803
#pragma libcall AwebSslBase Assl_getcipher 48 801
#pragma libcall AwebSslBase Assl_libname 4e 801

#endif
