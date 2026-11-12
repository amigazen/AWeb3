/**********************************************************************
 * 
 * This file is part of the AWeb distribution
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

/* AWebGet.c - AWeb networking test tool */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "/AWebAPL/aweb.h"
#include "/AWebAPL/fetchdriver.h"
#include "/AWebAPL/awebtcp.h"
#include "/AWebAPL/tcperr.h"
#include "/AWebAPL/author.h"
#include "/AWebAPL/authorlib.h"
#include "/AWebAPL/awebssl.h"
#include "/AWebAPL/url.h"
#include "/AWebAPL/object.h"

/* Global debug flags */
BOOL httpdebug = TRUE;
BOOL specdebug = TRUE;
BOOL haiku = FALSE;

/* Global preferences structure - simplified for testing */
struct Prefs prefs = {
    .network = {
        .spoofid = "",
        .cookies = 1,  /* TRUE */
        .ignoremime = FALSE,
        .limitproxy = FALSE
    }
};

struct SignalSemaphore prefssema;

/* Global version strings */
UBYTE *aboutversion = "AWebGet 1.0 - AWeb Network Test Tool";
UBYTE *awebversion = "1.0";

/* Global locale info */
struct LocaleInfo localeinfo;

/* Function prototypes */
void Httptask(struct Fetchdriver *fd);
BOOL Inithttp(void);
void Freehttp(void);
BOOL Initauthor(void);
void Freeauthor(void);
struct Library *Tcpopenlib(void);
struct Assl *Tcpopenssl(struct Library *socketbase);
void Tcpmessage(struct Fetchdriver *fd, long msgtype, ...);
void Tcperror(struct Fetchdriver *fd, long errtype, ...);
void Updatetaskattrs(ULONG tag, ...);
void UpdatetaskattrsA(struct TagItem *tags);



/* Test URL structure */
struct TestURL {
    UBYTE *url;
    UBYTE *description;
    BOOL ssl;
    BOOL expect_success;
};

/* Test URLs to try */
static struct TestURL test_urls[] = {
    {"http://www.google.com/", "Google HTTP", FALSE, TRUE},
    {"https://www.google.com/", "Google HTTPS", TRUE, TRUE},
    {"http://httpbin.org/get", "HTTPBin GET", FALSE, TRUE},
    {"http://httpbin.org/post", "HTTPBin POST", FALSE, TRUE},
    {"http://example.com/", "Example.com", FALSE, TRUE},
    {"https://example.com/", "Example.com HTTPS", TRUE, TRUE},
    {"http://invalid-domain-that-does-not-exist-12345.com/", "Invalid Domain", FALSE, FALSE},
    {NULL, NULL, FALSE, FALSE}
};

/* Simplified fetch driver for testing */
struct TestFetchDriver {
    struct Fetchdriver fd;
    UBYTE *name;
    UBYTE *postmsg;
    UBYTE *proxy;
    UBYTE *referer;
    UBYTE block[8192];
    long blocksize;
    ULONG validate;
    USHORT flags;
    struct Prefs *prefs;
    struct SignalSemaphore *prefssema;
    ULONG serverdate;
    BOOL error_occurred;
    BOOL data_received;
    long total_bytes;
    UBYTE *content_type;
    long content_length;
};

/* Test fetch driver initialization */
void InitTestFetchDriver(struct TestFetchDriver *tfd, UBYTE *url, BOOL ssl)
{
    memset(tfd, 0, sizeof(struct TestFetchDriver));
    tfd->fd.name = tfd->name = url;
    tfd->fd.block = tfd->block;
    tfd->fd.blocksize = sizeof(tfd->block);
    tfd->fd.prefs = &prefs;
    tfd->fd.prefssema = &prefssema;
    if (ssl) {
        tfd->fd.flags |= FDVF_SSL;
    }
    tfd->fd.flags |= FDVF_NOCACHE; /* Don't use cache for testing */
}

/* Simplified task attribute update function for testing */
void Updatetaskattrs(ULONG tag, ...)
{
    struct TagItem *tags;
    va_list args;
    
    va_start(args, tag);
    tags = (struct TagItem *)args;
    va_end(args);
    
    UpdatetaskattrsA(tags);
}

void UpdatetaskattrsA(struct TagItem *tags)
{
    struct TagItem *tag;
    
    for (tag = tags; tag->ti_Tag != TAG_END; tag++) {
        switch (tag->ti_Tag) {
            case AOURL_Status:
                printf("Status: %s\n", (UBYTE *)tag->ti_Data);
                break;
            case AOURL_Header:
                printf("Header: %s\n", (UBYTE *)tag->ti_Data);
                break;
            case AOURL_Data:
                printf("Data received: %ld bytes\n", (long)tag->ti_Data);
                break;
            case AOURL_Datalength:
                printf("Data length: %ld\n", (long)tag->ti_Data);
                break;
            case AOURL_Contenttype:
                printf("Content-Type: %s\n", (UBYTE *)tag->ti_Data);
                break;
            case AOURL_Contentlength:
                printf("Content-Length: %ld\n", (long)tag->ti_Data);
                break;
            case AOURL_Error:
                printf("ERROR occurred!\n");
                break;
            case AOURL_Eof:
                printf("End of data\n");
                break;
            case AOURL_Serverdate:
                printf("Server date: %lu\n", (ULONG)tag->ti_Data);
                break;
            case AOURL_Lastmodified:
                printf("Last modified: %lu\n", (ULONG)tag->ti_Data);
                break;
            case AOURL_Expires:
                printf("Expires: %lu\n", (ULONG)tag->ti_Data);
                break;
            case AOURL_Notmodified:
                printf("Not modified (304)\n");
                break;
            case AOURL_Movedto:
                printf("Moved to: %s\n", (UBYTE *)tag->ti_Data);
                break;
            case AOURL_Tempmovedto:
                printf("Temporarily moved to: %s\n", (UBYTE *)tag->ti_Data);
                break;
            case AOURL_Seeother:
                printf("See other: %s\n", (UBYTE *)tag->ti_Data);
                break;
            case AOURL_Cipher:
                printf("SSL Cipher: %s\n", (UBYTE *)tag->ti_Data);
                break;
            case AOURL_Ssllibrary:
                printf("SSL Library: %s\n", (UBYTE *)tag->ti_Data);
                break;
            case AOURL_Nocache:
                printf("No-cache directive\n");
                break;
            case AOURL_Clientpull:
                printf("Client pull: %s\n", (UBYTE *)tag->ti_Data);
                break;
            case AOURL_Foreign:
                printf("Foreign charset detected\n");
                break;
            case AOURL_Contentscripttype:
                printf("Content script type: %s\n", (UBYTE *)tag->ti_Data);
                break;
            case AOURL_Reload:
                printf("Reload requested\n");
                break;
            case AOURL_Serverpush:
                printf("Server push: %s\n", (UBYTE *)tag->ti_Data);
                break;
            case AOURL_Terminate:
                printf("Request terminated\n");
                break;
            default:
                printf("Unknown tag: %lu = %lu\n", tag->ti_Tag, (ULONG)tag->ti_Data);
                break;
        }
    }
}

/* Simplified TCP message function for testing */
void Tcpmessage(struct Fetchdriver *fd, long msgtype, ...)
{
    va_list args;
    UBYTE *arg1 = NULL, *arg2 = NULL;
    
    va_start(args, msgtype);
    arg1 = va_arg(args, UBYTE *);
    arg2 = va_arg(args, UBYTE *);
    va_end(args);
    
    switch (msgtype) {
        case TCPMSG_LOOKUP:
            printf("TCP: Looking up %s\n", arg1 ? arg1 : "unknown");
            break;
        case TCPMSG_CONNECT:
            printf("TCP: Making %s connection to %s\n", 
                   arg1 ? arg1 : "unknown", 
                   arg2 ? arg2 : "unknown");
            break;
        case TCPMSG_WAITING:
            printf("TCP: %s awaiting response\n", arg1 ? arg1 : "unknown");
            break;
        case TCPMSG_TCPSTART:
            printf("TCP: Beginning TCP connection\n");
            break;
        case TCPMSG_LOGIN:
            printf("TCP: Logging in at %s\n", arg1 ? arg1 : "unknown");
            break;
        case TCPMSG_NEWSGROUP:
            printf("TCP: Scanning %s\n", arg1 ? arg1 : "unknown");
            break;
        case TCPMSG_NEWSSCAN:
            printf("TCP: Scanning %s (%s articles)\n", 
                   arg1 ? arg1 : "unknown", 
                   arg2 ? arg2 : "unknown");
            break;
        case TCPMSG_NEWSSORT:
            printf("TCP: Sorting %s\n", arg1 ? arg1 : "unknown");
            break;
        case TCPMSG_NEWSPOST:
            printf("TCP: Posting news\n");
            break;
        case TCPMSG_MAILSEND:
            printf("TCP: Sending mail\n");
            break;
        case TCPMSG_UPLOAD:
            printf("TCP: Uploading file\n");
            break;
        default:
            printf("TCP: Unknown message type %ld\n", msgtype);
            break;
    }
}

/* Simplified TCP error function for testing */
void Tcperror(struct Fetchdriver *fd, long errtype, ...)
{
    va_list args;
    UBYTE *arg1 = NULL;
    
    va_start(args, errtype);
    arg1 = va_arg(args, UBYTE *);
    va_end(args);
    
    switch (errtype) {
        case TCPERR_NOLIB:
            printf("TCP ERROR: No TCP stack available\n");
            break;
        case TCPERR_NOHOST:
            printf("TCP ERROR: Host %s not found\n", arg1 ? arg1 : "unknown");
            break;
        case TCPERR_NOCONNECT:
            printf("TCP ERROR: Connection failed to %s\n", arg1 ? arg1 : "unknown");
            break;
        case TCPERR_NOFILE:
            printf("TCP ERROR: Local file not found: %s\n", arg1 ? arg1 : "unknown");
            break;
        case TCPERR_XAWEB:
            printf("TCP ERROR: Invalid x-aweb name: %s\n", arg1 ? arg1 : "unknown");
            break;
        case TCPERR_NOLOGIN:
            printf("TCP ERROR: Cannot login at %s\n", arg1 ? arg1 : "unknown");
            break;
        default:
            printf("TCP ERROR: Unknown error type %ld\n", errtype);
            break;
    }
}

/* Test a single URL */
void TestURL(struct TestURL *test_url)
{
    struct TestFetchDriver tfd;
    
    printf("\n=== Testing: %s ===\n", test_url->description);
    printf("URL: %s\n", test_url->url);
    printf("SSL: %s\n", test_url->ssl ? "Yes" : "No");
    printf("Expected success: %s\n", test_url->expect_success ? "Yes" : "No");
    printf("----------------------------------------\n");
    
    InitTestFetchDriver(&tfd, test_url->url, test_url->ssl);
    
    /* Call the HTTP task */
    Httptask(&tfd.fd);
    
    printf("----------------------------------------\n");
    printf("Test completed for: %s\n", test_url->description);
    printf("\n");
}

/* Main function */
int main(int argc, char *argv[])
{
    int i;
    UBYTE *test_url = NULL;
    BOOL test_specific = FALSE;
    
    printf("AWebGet - AWeb Network Test Tool\n");
    printf("================================\n\n");
    
    /* Initialize semaphores */
    InitSemaphore(&prefssema);
    
    /* Initialize HTTP and authorization systems */
    if (!Inithttp()) {
        printf("ERROR: Failed to initialize HTTP system\n");
        return 1;
    }
    
    if (!Initauthor()) {
        printf("ERROR: Failed to initialize authorization system\n");
        Freehttp();
        return 1;
    }
    
    /* Parse command line arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--url") == 0) {
            if (i + 1 < argc) {
                test_url = (UBYTE *)argv[i + 1];
                test_specific = TRUE;
                i++; /* Skip next argument */
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: AWebGet [options]\n");
            printf("Options:\n");
            printf("  -u, --url <url>    Test specific URL\n");
            printf("  -h, --help         Show this help\n");
            printf("\n");
            printf("If no URL is specified, runs built-in test suite.\n");
            return 0;
        }
    }
    
    if (test_specific && test_url) {
        /* Test specific URL */
        struct TestURL single_test = {
            test_url,
            "Custom URL",
            (strncmp(test_url, "https://", 8) == 0),
            TRUE
        };
        TestURL(&single_test);
    } else {
        /* Run built-in test suite */
        printf("Running built-in test suite...\n\n");
        
        for (i = 0; test_urls[i].url != NULL; i++) {
            TestURL(&test_urls[i]);
        }
        
        printf("Test suite completed!\n");
    }
    
    /* Cleanup */
    Freeauthor();
    Freehttp();
    
    printf("AWebGet finished.\n");
    return 0;
}
