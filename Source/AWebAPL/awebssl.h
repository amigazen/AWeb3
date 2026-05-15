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

/* awebssl.h - AWeb common definitions for amissl function library */

#include <exec/types.h>

#ifndef AWEBSSL_H
#define AWEBSSL_H

/* SAS/C 64-bit integer workaround for OPENSSL_init_ssl() */
/* OPENSSL_init_ssl() expects uint64_t: high 32-bits in D0, low 32-bits in D1 */
/* Since our constants fit in 32 bits (high bits are 0), we can pass directly */
/* SAS/C should handle the 32-bit to 64-bit conversion automatically */
/* If this causes issues, a wrapper function may be needed (see amissl.c) */
#define OPENSSL_init_ssl_32(opts, settings) \
	OPENSSL_init_ssl((unsigned long)(opts), settings)

struct Assl *Assl_initamissl(struct Library *socketbase);

/* Cleanup function to mirror Assl_initamissl() initialization */
/* Call this at application exit, after all SSL connections are closed */
void Freeamissl(void);

/* Get the AmiSSL master library base from an Assl object */
/* Returns NULL if assl is NULL or library base is not available */
struct Library *Assl_getmasterbase(struct Assl *assl);

/* Close AmiSSL libraries for a given Assl object */
/* This should be called before Assl_cleanup() to properly close libraries */
void Assl_closelibraries(struct Assl *assl);

/* Close AmiSSL libraries for a given library base pointer */
/* This is used at application shutdown to close unique library bases */
void Assl_closelibrarybase(struct Library *library_base);

/* Clear socketbase after bsdsocket.library is closed — avoids UAF in Assl_closessl */
void Assl_detach_socketbase(struct Assl *assl);

/* SSL certificate acceptance function */
BOOL Httpcertaccept(char *hostname, char *certname);

/* SSL connection result codes */
#define ASSLCONNECT_OK     0  /* connection ok */
#define ASSLCONNECT_FAIL   1  /* connection failed */
#define ASSLCONNECT_DENIED 2  /* connection denied by user */

/* Global AmiSSL library bases (defined in amissl.c) */
extern struct Library *AmiSSLMasterBase;
extern struct Library *AmiSSLBase;
extern struct Library *AmiSSLExtBase;

#endif