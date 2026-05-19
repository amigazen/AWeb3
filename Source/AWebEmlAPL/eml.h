/**********************************************************************
 * 
 * This file is part of the AWeb distribution
 *
 * Copyright (C) 2025 amigazen project
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

/* eml.h - EML email plugin header file */

#ifndef EML_H
#define EML_H

#include <libraries/awebplugin.h>
#include <exec/types.h>
#include <exec/lists.h>

/* Base pointers of libraries needed */
extern struct Library *AwebPluginBase;

/* Memory / lifecycle trace (Aprintf). Set to 0 to disable. */
#ifndef EML_DEBUG
#define EML_DEBUG 1
#endif

#if EML_DEBUG
#include <clib/awebplugin_protos.h>
/* SAS/C: format string is one literal; EMLDBGn must match argument count exactly. */
#define EMLDBG0(s)              Aprintf("EML: " s "\n")
#define EMLDBG1(s,a)            Aprintf("EML: " s "\n", (a))
#define EMLDBG2(s,a,b)          Aprintf("EML: " s "\n", (a), (b))
#define EMLDBG3(s,a,b,c)        Aprintf("EML: " s "\n", (a), (b), (c))
#define EMLDBG4(s,a,b,c,d)      Aprintf("EML: " s "\n", (a), (b), (c), (d))
#define EMLDBG5(s,a,b,c,d,e)    Aprintf("EML: " s "\n", (a), (b), (c), (d), (e))
#else
#define EMLDBG0(s)
#define EMLDBG1(s,a)
#define EMLDBG2(s,a,b)
#define EMLDBG3(s,a,b,c)
#define EMLDBG4(s,a,b,c,d)
#define EMLDBG5(s,a,b,c,d,e)
#endif

/* Email header structure */
struct EmailHeader
{  UBYTE *from;
   UBYTE *to;
   UBYTE *cc;
   UBYTE *bcc;
   UBYTE *subject;
   UBYTE *date;
   UBYTE *reply_to;
   UBYTE *message_id;
   long fromlen;
   long tolen;
   long cclen;
   long bcclen;
   long subjectlen;
   long datelen;
   long reply_tolen;
   long message_idlen;
};

/* Email attachment structure */
struct EmailAttachment
{  struct EmailAttachment *next;
   UBYTE *filename;
   UBYTE *content_type;
   UBYTE *content_id;
   UBYTE *content_disposition;
   UBYTE *data;
   long filenamelen;
   long content_typelen;
   long content_idlen;
   long content_dispositionlen;
   long datalen;
   BOOL is_inline;
};

/* Email body part structure */
struct EmailBodyPart
{  struct EmailBodyPart *next;
   UBYTE *content_type;
   UBYTE *charset;
   UBYTE *content_transfer_encoding;
   UBYTE *content_id;
   UBYTE *data;
   long content_typelen;
   long charsetlen;
   long content_transfer_encodinglen;
   long content_idlen;
   long datalen;
   BOOL is_html;
   BOOL is_text;
   BOOL is_multipart;
   UBYTE *subboundary;
   long subboundarylen;
};

/* Email message structure */
struct EmailMessage
{  struct EmailHeader *headers;
   struct EmailBodyPart *body_parts;
   struct EmailBodyPart *html_part;
   struct EmailBodyPart *text_part;
   struct EmailAttachment *attachments;
   long attachment_count;
   BOOL is_multipart;
   UBYTE *boundary;
   long boundarylen;
};

/* Parser state structure */
struct EmlParser
{  UBYTE *buffer;
   long bufsize;
   long buflen;
   UBYTE *current;
   UBYTE *end;
   struct EmailMessage *message;
   BOOL in_headers;
   BOOL headers_complete;
   BOOL in_multipart;
   UBYTE *current_boundary;
   long current_boundarylen;
   struct EmailBodyPart *current_part;
   struct EmailAttachment *current_attachment;
   BOOL in_part_headers;
   BOOL expecting_boundary;
   UBYTE *body_start;
};

/* Inline part data structure (for storing parts accessible via x-aweb: URLs) */
struct EmlPartData
{  struct Node node;
   UBYTE *part_id;        /* Content-ID or generated ID */
   UBYTE *content_type;
   UBYTE *data;
   long datalen;
};

/* Filter user data structure */
struct EmlFilterData
{  BOOL first;
   BOOL is_email;
   struct EmlParser *parser;
   UBYTE *html_buffer;
   long html_bufsize;
   long html_buflen;
   BOOL header_written;
   BOOL footer_written;
   UBYTE *eml_url;            /* Original EML file URL for CID registry */
   BOOL cid_registered;       /* Flag to prevent duplicate CID registration */
   BOOL rendered;             /* RenderEmailToHtml completed for this filter */
};

/* AllocVec duplicate (plugin uses Exec allocator throughout). */
UBYTE *EmlDupString(UBYTE *start, long len);

/* Free Exec AllocVec payload (not AWeb FREE). */
void EmlReleaseExecPayload(UBYTE **data, long *datalen);

/* Function declarations */
void InitEmlParser(struct EmlParser *parser);
void ParseEmlChunk(struct EmlParser *parser, UBYTE *data, long length);
void CleanupEmlParser(struct EmlParser *parser);
void RenderEmailToHtml(struct EmlFilterData *fd, void *handle);
void FreeEmailMessage(struct EmailMessage *message);

/* CID registry functions (from awebplugin.library) */
/* These are accessed via awebplugin.library, not as extern functions */

/* Encoding/Decoding functions */
UBYTE *EncodeBase64(UBYTE *input, long inputlen, long *outputlen);
UBYTE *DecodeBase64(UBYTE *input, long inputlen, long *outputlen);
UBYTE *DecodeQuotedPrintable(UBYTE *input, long inputlen, long *outputlen);
UBYTE *DecodeContent(UBYTE *data, long datalen, UBYTE *encoding, long encodinglen, long *outputlen);

#endif /* EML_H */

