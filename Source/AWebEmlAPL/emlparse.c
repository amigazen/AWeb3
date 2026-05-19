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

/* emlparse.c - EML email MIME parser */

#include "eml.h"
#include <exec/memory.h>
#include <exec/types.h>
#include <string.h>
#include <ctype.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <pragmas/awebplugin_pragmas.h>

/* Forward declarations */
static void ParseMultipartBody(struct EmlParser *parser);
static void EmlParseMultipartRegion(struct EmlParser *parser, UBYTE *start, UBYTE *end,
   UBYTE *boundary, long boundarylen);
static void EmlCommitBodyPart(struct EmlParser *parser, struct EmailBodyPart *part,
   UBYTE *raw_start, UBYTE *raw_end);
static void ParsePartHeader(struct EmlParser *parser, UBYTE *line, UBYTE *end);
static void FreeEmailBodyPart(struct EmailBodyPart *part, UBYTE *why);
static BOOL BodyPartInList(struct EmailBodyPart *list, struct EmailBodyPart *part);
extern UBYTE *DecodeContent(UBYTE *data, long datalen, UBYTE *encoding, long encodinglen, long *outputlen);

#define DupString EmlDupString

/* Extract multipart boundary= from a Content-Type value. */
static BOOL EmlExtractBoundary(UBYTE *value, long valuelen, UBYTE **boundary_out, long *boundary_len_out)
{  UBYTE *boundary;
   UBYTE *p;
   UBYTE *start;
   UBYTE *result;
   long len;

   if(boundary_out) *boundary_out = NULL;
   if(boundary_len_out) *boundary_len_out = 0;
   if(!value || valuelen <= 0) return FALSE;

   boundary = value;
   if(valuelen > 0 && boundary[valuelen - 1] == '\0') valuelen--;
   boundary = (UBYTE *)strstr((char *)value, "boundary=");
   if(!boundary) return FALSE;
   boundary += 9;
   if(boundary >= value + valuelen) return FALSE;

   p = boundary;
   start = boundary;
   if(*p == '"')
   {  p++;
      start = p;
      while(p < value + valuelen && *p && *p != '"') p++;
   }
   else
   {  while(p < value + valuelen && *p && *p != ';' && *p != ' ' && *p != '\r' && *p != '\n') p++;
   }
   len = p - start;
   if(len <= 0) return FALSE;

   result = DupString(start, len);
   if(!result) return FALSE;
   if(boundary_out) *boundary_out = result;
   if(boundary_len_out) *boundary_len_out = len;
   return TRUE;
}

/* Attach a leaf body part to the message (not a multipart container). */
static void EmlLinkBodyPart(struct EmailMessage *message, struct EmailBodyPart *part)
{  struct EmailBodyPart *last;

   if(!message || !part) return;

   if(part->is_html)
   {  message->html_part = part;
   }
   else if(part->is_text)
   {  message->text_part = part;
   }
   else
   {  if(!message->body_parts)
      {  message->body_parts = part;
      }
      else
      {  last = message->body_parts;
         while(last->next) last = last->next;
         last->next = part;
      }
   }
}

/* Decode and commit a finished part; expand nested multipart (e.g. alternative inside mixed). */
static void EmlCommitBodyPart(struct EmlParser *parser, struct EmailBodyPart *part,
   UBYTE *raw_start, UBYTE *raw_end)
{  long partlen;
   UBYTE *partdata;
   UBYTE *decoded;
   long decodedlen;
   struct EmailMessage *message;

   if(!parser || !part) return;
   message = parser->message;
   if(!message) return;

   if(raw_start && raw_end > raw_start)
   {  partlen = raw_end - raw_start;
      partdata = DupString(raw_start, partlen);
      if(partdata)
      {  decoded = DecodeContent(partdata, partlen,
            part->content_transfer_encoding,
            part->content_transfer_encodinglen,
            &decodedlen);
         if(decoded)
         {  if(part->data) FreeVec(part->data);
            part->data = decoded;
            part->datalen = decodedlen;
         }
         FreeVec(partdata);
      }
   }

   if(parser->current_attachment && part->data)
   {  EMLDBG3("ParseMultipart: xfer data to attach part=%lx attach=%lx data=%lx",
         (ULONG)part, (ULONG)parser->current_attachment, (ULONG)part->data);
      parser->current_attachment->data = part->data;
      parser->current_attachment->datalen = part->datalen;
      part->data = NULL;
      part->datalen = 0;
      FreeEmailBodyPart(part, "xfer_to_attach");
      return;
   }

   if(part->is_multipart && part->subboundary && part->subboundarylen > 0
      && part->data && part->datalen > 0)
   {  UBYTE *nested;
      long nestedlen;

      nested = part->data;
      nestedlen = part->datalen;
      EMLDBG2("EmlCommitBodyPart: nested multipart len=%ld boundary=%s",
         nestedlen, part->subboundary);
      part->data = NULL;
      part->datalen = 0;
      parser->current_part = NULL;
      EmlParseMultipartRegion(parser, nested, nested + nestedlen,
         part->subboundary, part->subboundarylen);
      FreeVec(nested);
      FreeEmailBodyPart(part, "nested_container");
      return;
   }

   EmlLinkBodyPart(message, part);
}

/* Helper: Duplicate string with AllocVec (do not use AWeb Dupstr/Freemem here). */
UBYTE *EmlDupString(UBYTE *start, long len)
{  UBYTE *result;
   
   if(!start || len <= 0) return NULL;
   
   result = (UBYTE *)AllocVec(len + 1, MEMF_CLEAR);
   if(result)
   {  memcpy(result, start, len);
      result[len] = '\0';
   }
   return result;
}

void EmlReleaseExecPayload(UBYTE **data, long *datalen)
{  UBYTE *p;

   if(data)
   {  p=*data;
      if(p) FreeVec(p);
      *data=NULL;
   }
   if(datalen) *datalen=0;
}

/* Helper: Skip whitespace */
static UBYTE *SkipWhitespace(UBYTE *p, UBYTE *end)
{  while(p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
   {  p++;
   }
   return p;
}

/* Helper: Find end of line */
static UBYTE *FindLineEnd(UBYTE *p, UBYTE *end)
{  while(p < end && *p != '\r' && *p != '\n')
   {  p++;
   }
   return p;
}

/* Helper: Extract header value */
static UBYTE *ExtractHeaderValue(UBYTE *line, UBYTE *end, long *len)
{  UBYTE *colon;
   UBYTE *value;
   
   colon = (UBYTE *)strchr(line, ':');
   if(!colon || colon >= end) return NULL;
   
   value = SkipWhitespace(colon + 1, end);
   if(value >= end)
   {  if(len) *len = 0;
      return NULL;
   }
   
   /* Find end of value, handling continuation lines */
   {  UBYTE *valend;
      valend = FindLineEnd(value, end);
      while(valend < end - 1 && (valend[0] == '\r' || valend[0] == '\n'))
      {  UBYTE *next;
         next = valend;
         if(*next == '\r') next++;
         if(*next == '\n') next++;
         if(next < end && (*next == ' ' || *next == '\t'))
         {  /* Continuation line */
            valend = FindLineEnd(next, end);
         }
         else
         {  break;
         }
      }
      if(len) *len = valend - value;
      return value;
   }
}

/* Parse email header */
static void ParseHeader(struct EmlParser *parser, UBYTE *line, UBYTE *end)
{  struct EmailHeader *header;
   UBYTE *colon;
   UBYTE *name;
   UBYTE *value;
   long namelen;
   long valuelen;
   
   if(!parser || !parser->message || !parser->message->headers) return;
   
   header = parser->message->headers;
   colon = (UBYTE *)strchr(line, ':');
   if(!colon || colon >= end) return;
   
   name = line;
   namelen = colon - name;
   value = ExtractHeaderValue(line, end, &valuelen);
   
   if(namelen == 4 && strnicmp(name, "From", 4) == 0)
   {  if(value && valuelen > 0)
      {  if(header->from) FreeVec(header->from);
         header->from = DupString(value, valuelen);
         if(header->from) header->fromlen = valuelen;
      }
   }
   else if(namelen == 2 && strnicmp(name, "To", 2) == 0)
   {  if(value && valuelen > 0)
      {  if(header->to) FreeVec(header->to);
         header->to = DupString(value, valuelen);
         if(header->to) header->tolen = valuelen;
      }
   }
   else if(namelen == 2 && strnicmp(name, "Cc", 2) == 0)
   {  if(value && valuelen > 0)
      {  if(header->cc) FreeVec(header->cc);
         header->cc = DupString(value, valuelen);
         if(header->cc) header->cclen = valuelen;
      }
   }
   else if(namelen == 3 && strnicmp(name, "Bcc", 3) == 0)
   {  if(value && valuelen > 0)
      {  if(header->bcc) FreeVec(header->bcc);
         header->bcc = DupString(value, valuelen);
         if(header->bcc) header->bcclen = valuelen;
      }
   }
   else if(namelen == 7 && strnicmp(name, "Subject", 7) == 0)
   {  if(value && valuelen > 0)
      {  if(header->subject) FreeVec(header->subject);
         header->subject = DupString(value, valuelen);
         if(header->subject) header->subjectlen = valuelen;
      }
   }
   else if(namelen == 4 && strnicmp(name, "Date", 4) == 0)
   {  if(value && valuelen > 0)
      {  if(header->date) FreeVec(header->date);
         header->date = DupString(value, valuelen);
         if(header->date) header->datelen = valuelen;
      }
   }
   else if(namelen == 8 && strnicmp(name, "Reply-To", 8) == 0)
   {  if(value && valuelen > 0)
      {  if(header->reply_to) FreeVec(header->reply_to);
         header->reply_to = DupString(value, valuelen);
         if(header->reply_to) header->reply_tolen = valuelen;
      }
   }
   else if(namelen == 10 && strnicmp(name, "Message-ID", 10) == 0)
   {  if(value && valuelen > 0)
      {  if(header->message_id) FreeVec(header->message_id);
         header->message_id = DupString(value, valuelen);
         if(header->message_id) header->message_idlen = valuelen;
      }
   }
   else if(namelen == 12 && strnicmp(name, "Content-Type", 12) == 0)
   {  UBYTE *boundary;
      UBYTE *p;
      if(value && valuelen > 0)
      {  /* Check for multipart */
         if(strstr(value, "multipart"))
         {  parser->message->is_multipart = TRUE;
            /* Extract boundary */
            boundary = strstr(value, "boundary=");
            if(boundary)
            {  boundary += 9; /* Skip "boundary=" */
               p = boundary;
               if(*p == '"')
               {  p++;
                  boundary = p;
                  while(*p && *p != '"') p++;
               }
               else
               {  while(*p && *p != ';' && *p != ' ' && *p != '\r' && *p != '\n') p++;
               }
               if(p > boundary)
               {  if(parser->message->boundary) FreeVec(parser->message->boundary);
                  parser->message->boundary = DupString(boundary, p - boundary);
                  if(parser->message->boundary)
                  {  parser->message->boundarylen = p - boundary;
                     parser->in_multipart = TRUE;
                  }
               }
            }
         }
      }
   }
}

/* Find multipart boundary */
static BOOL FindBoundary(UBYTE *data, long datalen, UBYTE *boundary, long boundarylen, UBYTE **boundarypos)
{  UBYTE *p;
   UBYTE *end;
   UBYTE *boundary_start;
   
   if(!data || datalen <= 0 || !boundary || boundarylen <= 0) return FALSE;
   
   p = data;
   end = data + datalen;
   boundary_start = (UBYTE *)"--";
   
   while(p < end - boundarylen - 2)
   {  if(*p == '-' && p[1] == '-')
      {  if(!strnicmp(p + 2, boundary, boundarylen))
         {  if(boundarypos) *boundarypos = p;
            return TRUE;
         }
      }
      p++;
   }
   
   return FALSE;
}

/* Return TRUE if part is already on the body_parts chain. */
static BOOL BodyPartInList(struct EmailBodyPart *list, struct EmailBodyPart *part)
{  while(list)
   {  if(list == part) return TRUE;
      list = list->next;
   }
   return FALSE;
}

/* Allocate and initialize parser */
void InitEmlParser(struct EmlParser *parser)
{  struct EmailMessage *message;
   struct EmailHeader *header;
   
   if(!parser) return;
   EMLDBG1("InitEmlParser parser=%lx", (ULONG)parser);
   
   parser->buffer = NULL;
   parser->bufsize = 0;
   parser->buflen = 0;
   parser->current = NULL;
   parser->end = NULL;
   parser->in_headers = TRUE;
   parser->headers_complete = FALSE;
   parser->in_multipart = FALSE;
   parser->current_boundary = NULL;
   parser->current_boundarylen = 0;
   parser->current_part = NULL;
   parser->current_attachment = NULL;
   parser->in_part_headers = FALSE;
   parser->expecting_boundary = FALSE;
   parser->body_start = NULL;
   
   message = (struct EmailMessage *)AllocVec(sizeof(struct EmailMessage), MEMF_CLEAR);
   EMLDBG1("InitEmlParser AllocVec message=%lx", (ULONG)message);
   if(message)
   {  header = (struct EmailHeader *)AllocVec(sizeof(struct EmailHeader), MEMF_CLEAR);
      EMLDBG1("InitEmlParser AllocVec header=%lx", (ULONG)header);
      if(header)
      {  message->headers = header;
      }
      else
      {  EMLDBG0("InitEmlParser: header alloc failed, freeing message");
         FreeVec(message);
         message = NULL;
      }
   }
   if(message)
   {  message->body_parts = NULL;
      message->html_part = NULL;
      message->text_part = NULL;
      message->attachments = NULL;
      message->attachment_count = 0;
      message->is_multipart = FALSE;
      message->boundary = NULL;
      message->boundarylen = 0;
   }
   parser->message = message;
   EMLDBG2("InitEmlParser done message=%lx headers=%lx",
      (ULONG)message, message ? (ULONG)message->headers : 0);
}

/* Parse a chunk of email data */
void ParseEmlChunk(struct EmlParser *parser, UBYTE *data, long length)
{  UBYTE *p;
   UBYTE *end;
   UBYTE *line;
   UBYTE *lineend;
   
   if(!parser || !data || length <= 0) return;
   if(!parser->message)
   {  EMLDBG0("ParseEmlChunk: parser->message is NULL, skipping");
      return;
   }
   
   /* Append data to buffer */
   {  UBYTE *newbuffer;
      long newlen;
      
      newlen = parser->buflen + length;
      if(newlen >= parser->bufsize)
      {  newbuffer = (UBYTE *)AllocVec(newlen + 4096, MEMF_CLEAR);
         if(newbuffer)
         {  if(parser->buffer)
            {  memcpy(newbuffer, parser->buffer, parser->buflen);
               FreeVec(parser->buffer);
            }
            parser->buffer = newbuffer;
            parser->bufsize = newlen + 4096;
         }
         else
         {  EMLDBG0("ParseEmlChunk: buffer realloc FAILED");
            return;
         }
      }
      
      memcpy(parser->buffer + parser->buflen, data, length);
      parser->buflen += length;
      EMLDBG3("ParseEmlChunk: buffer=%lx buflen=%ld bufsize=%ld",
         (ULONG)parser->buffer, parser->buflen, parser->bufsize);
   }
   
   p = parser->buffer;
   end = parser->buffer + parser->buflen;
   parser->current = p;
   parser->end = end;
   
   /* Parse headers */
   if(parser->in_headers && !parser->headers_complete)
   {  while(p < end)
      {  line = p;
         lineend = FindLineEnd(line, end);
         
         if(lineend == line)
         {  /* Empty line - end of headers */
            parser->in_headers = FALSE;
            parser->headers_complete = TRUE;
            EMLDBG2("ParseEmlChunk: headers complete multipart=%ld boundary=%lx",
               (ULONG)parser->message->is_multipart,
               (ULONG)parser->message->boundary);
            if(lineend < end)
            {  if(*lineend == '\r') lineend++;
               if(lineend < end && *lineend == '\n') lineend++;
            }
            p = lineend;
            break;
         }
         
         ParseHeader(parser, line, lineend);
         
         if(lineend < end)
         {  if(*lineend == '\r') lineend++;
            if(lineend < end && *lineend == '\n') lineend++;
         }
         p = lineend;
      }
   }
   
   /* Parse multipart body parts and attachments */
   if(parser->headers_complete)
   {  if(!parser->body_start)
      {  parser->body_start = p;
      }
      
      if(parser->message->is_multipart && parser->message->boundary)
      {  ParseMultipartBody(parser);
      }
      else if(!parser->message->is_multipart)
      {  /* Simple single-part message - extract body */
         struct EmailMessage *message;
         struct EmailBodyPart *part;
         UBYTE *body_start;
         long bodylen;
         
         message = parser->message;
         body_start = parser->body_start;
         if(body_start && body_start < end)
         {  bodylen = end - body_start;
            
            if(bodylen > 0)
            {  /* Single-part body grows with each chunk: update one part, do not
               * allocate a new struct per chunk (that leaked and duplicated data). */
               part = message->body_parts;
               if(!part)
               {  part = (struct EmailBodyPart *)AllocVec(sizeof(struct EmailBodyPart), MEMF_CLEAR);
                  if(part)
                  {  message->body_parts = part;
                     part->is_text = TRUE;
                     part->is_html = FALSE;
                  }
               }
               if(part)
               {  UBYTE *newdata;
                  newdata = DupString(body_start, bodylen);
                  if(newdata)
                  {  if(part->data)
                     {  EMLDBG2("ParseEmlChunk: replace simple body data old=%lx new=%lx",
                           (ULONG)part->data, (ULONG)newdata);
                        FreeVec(part->data);
                     }
                     part->data = newdata;
                     part->datalen = bodylen;
                     EMLDBG2("ParseEmlChunk: simple body part=%lx datalen=%ld",
                        (ULONG)part, bodylen);
                  }
               }
            }
         }
      }
   }
}

/* Parse part headers */
static void ParsePartHeader(struct EmlParser *parser, UBYTE *line, UBYTE *end)
{  UBYTE *colon;
   UBYTE *name;
   UBYTE *value;
   long namelen;
   long valuelen;
   struct EmailBodyPart *part;
   struct EmailAttachment *attach;
   struct EmailMessage *message;
   
   if(!parser || !parser->current_part || !parser->message) return;
   
   message = parser->message;
   
   part = parser->current_part;
   colon = (UBYTE *)strchr(line, ':');
   if(!colon || colon >= end) return;
   
   name = line;
   namelen = colon - name;
   value = ExtractHeaderValue(line, end, &valuelen);
   
   if(namelen == 12 && strnicmp(name, "Content-Type", 12) == 0)
   {  UBYTE *p;
      UBYTE *semicolon;
      if(value && valuelen > 0)
      {  if(part->content_type) FreeVec(part->content_type);
         part->content_type = DupString(value, valuelen);
         if(part->content_type) part->content_typelen = valuelen;

         part->is_multipart = FALSE;
         if(part->subboundary)
         {  FreeVec(part->subboundary);
            part->subboundary = NULL;
            part->subboundarylen = 0;
         }
         if(strstr(part->content_type, "multipart/"))
         {  part->is_multipart = TRUE;
            EmlExtractBoundary(value, valuelen, &part->subboundary, &part->subboundarylen);
         }
         
         /* Check if HTML or text */
         if(strstr(part->content_type, "text/html"))
         {  part->is_html = TRUE;
            part->is_text = FALSE;
         }
         else if(strstr(part->content_type, "text/plain"))
         {  part->is_text = TRUE;
            part->is_html = FALSE;
         }
         
         /* Extract charset */
         semicolon = strchr(part->content_type, ';');
         if(semicolon)
         {  p = strstr(semicolon, "charset=");
            if(p)
            {  p += 8;
               while(*p == ' ' || *p == '\t') p++;
               if(*p == '"') p++;
               semicolon = p;
               while(*semicolon && *semicolon != ';' && *semicolon != ' ' && *semicolon != '"' && *semicolon != '\r' && *semicolon != '\n') semicolon++;
               if(semicolon > p)
               {  if(part->charset) FreeVec(part->charset);
                  part->charset = DupString(p, semicolon - p);
                  if(part->charset) part->charsetlen = semicolon - p;
               }
            }
         }
      }
   }
   else if(namelen == 25 && strnicmp(name, "Content-Transfer-Encoding", 25) == 0)
   {  if(value && valuelen > 0)
      {  if(part->content_transfer_encoding) FreeVec(part->content_transfer_encoding);
         part->content_transfer_encoding = DupString(value, valuelen);
         if(part->content_transfer_encoding) part->content_transfer_encodinglen = valuelen;
      }
   }
   else if(namelen == 19 && strnicmp(name, "Content-Disposition", 19) == 0)
   {  UBYTE *p;
      UBYTE *filename;
      if(value && valuelen > 0)
      {  /* Check if this is an attachment */
         if(strstr(value, "attachment") || strstr(value, "inline"))
         {  /* Create attachment structure */
            attach = (struct EmailAttachment *)AllocVec(sizeof(struct EmailAttachment), MEMF_CLEAR);
            if(attach)
            {  attach->content_type = DupString(part->content_type, part->content_typelen);
               if(attach->content_type) attach->content_typelen = part->content_typelen;
               attach->is_inline = (strstr(value, "inline") != NULL);
               
               /* Extract filename */
               filename = strstr(value, "filename=");
               if(filename)
               {  filename += 9;
                  while(*filename == ' ' || *filename == '\t') filename++;
                  if(*filename == '"')
                  {  filename++;
                     p = filename;
                     while(*p && *p != '"') p++;
                  }
                  else
                  {  p = filename;
                     while(*p && *p != ';' && *p != ' ' && *p != '\r' && *p != '\n') p++;
                  }
                  if(p > filename)
                  {  attach->filename = DupString(filename, p - filename);
                     if(attach->filename) attach->filenamelen = p - filename;
                  }
               }
               
               attach->content_disposition = DupString(value, valuelen);
               if(attach->content_disposition) attach->content_dispositionlen = valuelen;
               
               /* Copy Content-ID from part to attachment if it exists
                * (Content-ID may have been parsed before Content-Disposition) */
               if(part && part->content_id && part->content_idlen > 0)
               {  attach->content_id = DupString(part->content_id, part->content_idlen);
                  if(attach->content_id) attach->content_idlen = part->content_idlen;
               }
               
               /* Link attachment to message */
               if(!message->attachments)
               {  message->attachments = attach;
               }
               else
               {  struct EmailAttachment *last;
                  last = message->attachments;
                  while(last->next) last = last->next;
                  last->next = attach;
               }
               message->attachment_count++;
               
               /* Mark part as attachment */
               parser->current_attachment = attach;
            }
         }
      }
   }
   else if(namelen == 10 && strnicmp(name, "Content-ID", 10) == 0)
   {  if(value && valuelen > 0)
      {  if(parser->current_attachment)
         {  if(parser->current_attachment->content_id) FreeVec(parser->current_attachment->content_id);
            parser->current_attachment->content_id = DupString(value, valuelen);
            if(parser->current_attachment->content_id) parser->current_attachment->content_idlen = valuelen;
         }
         else if(part)
         {  /* Store Content-ID for body parts too (for cid: URL references) */
            if(part->content_id) FreeVec(part->content_id);
            part->content_id = DupString(value, valuelen);
            if(part->content_id) part->content_idlen = valuelen;
         }
      }
   }
}

/* Parse a MIME multipart region (top-level body or nested multipart/alternative). */
static void EmlParseMultipartRegion(struct EmlParser *parser, UBYTE *start, UBYTE *end,
   UBYTE *boundary, long boundary_rawlen)
{  UBYTE *p;
   UBYTE *boundary_end;
   UBYTE *part_start;
   UBYTE *part_end;
   UBYTE *line;
   UBYTE *lineend;
   UBYTE boundary_str[256];
   long boundarylen;
   BOOL in_part;
   BOOL found_boundary;
   struct EmailBodyPart *finished_part;

   if(!parser || !parser->message || !boundary || boundary_rawlen <= 0) return;
   if(!start || !end || end <= start) return;

   if(boundary_rawlen + 3 > sizeof(boundary_str)) return;
   strcpy(boundary_str, "--");
   strncat(boundary_str, boundary, boundary_rawlen);
   boundarylen = boundary_rawlen + 2;

   p = start;
   in_part = FALSE;
   part_start = NULL;
   finished_part = NULL;

   while(p < end)
   {  found_boundary = FALSE;

      if(p + boundarylen <= end && *p == '-' && p[1] == '-')
      {  if(!strnicmp(p, boundary_str, boundarylen))
         {  found_boundary = TRUE;
            boundary_end = p + boundarylen;

            if(boundary_end + 1 < end && *boundary_end == '-' && boundary_end[1] == '-')
            {  if(in_part && parser->current_part)
               {  part_end = p;
                  finished_part = parser->current_part;
                  EmlCommitBodyPart(parser, finished_part, part_start, part_end);
                  parser->current_part = NULL;
                  parser->current_attachment = NULL;
               }
               break;
            }

            if(in_part && parser->current_part)
            {  part_end = p;
               finished_part = parser->current_part;
               EmlCommitBodyPart(parser, finished_part, part_start, part_end);
               parser->current_part = NULL;
               parser->current_attachment = NULL;
               in_part = FALSE;
            }

            p = boundary_end;
            while(p < end && *p != '\r' && *p != '\n') p++;
            if(p < end && *p == '\r') p++;
            if(p < end && *p == '\n') p++;

            if(p < end)
            {  parser->current_part = (struct EmailBodyPart *)AllocVec(
                  sizeof(struct EmailBodyPart), MEMF_CLEAR);
               if(parser->current_part)
               {  parser->in_part_headers = TRUE;
                  in_part = TRUE;

                  while(p < end)
                  {  line = p;
                     lineend = FindLineEnd(line, end);

                     if(lineend == line)
                     {  parser->in_part_headers = FALSE;
                        if(lineend < end)
                        {  if(*lineend == '\r') lineend++;
                           if(lineend < end && *lineend == '\n') lineend++;
                        }
                        p = lineend;
                        part_start = p;
                        break;
                     }

                     ParsePartHeader(parser, line, lineend);

                     if(lineend < end)
                     {  if(*lineend == '\r') lineend++;
                        if(lineend < end && *lineend == '\n') lineend++;
                     }
                     p = lineend;
                  }
               }
            }
         }
      }

      if(!found_boundary)
      {  p++;
      }
   }

}

/* Parse top-level multipart body using message boundary. */
static void ParseMultipartBody(struct EmlParser *parser)
{
   if(!parser || !parser->message || !parser->message->boundary) return;

   EmlParseMultipartRegion(parser, parser->current, parser->end,
      parser->message->boundary, parser->message->boundarylen);
   parser->current = parser->end;
}

/* Cleanup parser */
void CleanupEmlParser(struct EmlParser *parser)
{  if(!parser) return;
   EMLDBG1("CleanupEmlParser parser=%lx", (ULONG)parser);
   
   if(parser->message)
   {  EMLDBG1("CleanupEmlParser FreeEmailMessage message=%lx", (ULONG)parser->message);
      FreeEmailMessage(parser->message);
      EMLDBG1("CleanupEmlParser FreeVec message struct=%lx", (ULONG)parser->message);
      FreeVec(parser->message);
      parser->message = NULL;
   }
   
   if(parser->buffer)
   {  EMLDBG2("CleanupEmlParser FreeVec buffer=%lx buflen=%ld",
         (ULONG)parser->buffer, parser->buflen);
      FreeVec(parser->buffer);
      parser->buffer = NULL;
   }
   
   parser->bufsize = 0;
   parser->buflen = 0;
   parser->current = NULL;
   parser->end = NULL;
}

/* Free one body part (strings and struct; data may already be owned by CID registry). */
static void FreeEmailBodyPart(struct EmailBodyPart *part, UBYTE *why)
{  if(!part) return;
   EMLDBG4("FreeEmailBodyPart tag=%lx part=%lx data=%lx datalen=%ld",
      (ULONG)why, (ULONG)part, (ULONG)part->data, part->datalen);
   if(part->content_type) FreeVec(part->content_type);
   if(part->charset) FreeVec(part->charset);
   if(part->content_transfer_encoding) FreeVec(part->content_transfer_encoding);
   if(part->content_id) FreeVec(part->content_id);
   if(part->subboundary) FreeVec(part->subboundary);
   if(part->data) FreeVec(part->data);
   FreeVec(part);
}

/* Free email message */
void FreeEmailMessage(struct EmailMessage *message)
{  struct EmailHeader *header;
   struct EmailBodyPart *part;
   struct EmailBodyPart *nextpart;
   struct EmailBodyPart *html_part;
   struct EmailBodyPart *text_part;
   struct EmailAttachment *attach;
   struct EmailAttachment *nextattach;
   
   if(!message) return;
   EMLDBG1("FreeEmailMessage message=%lx", (ULONG)message);
   EMLDBG4("  html_part=%lx text_part=%lx body_parts=%lx attachments=%lx",
      (ULONG)message->html_part, (ULONG)message->text_part,
      (ULONG)message->body_parts, (ULONG)message->attachments);
   
   header = message->headers;
   if(header)
   {  if(header->from) FreeVec(header->from);
      if(header->to) FreeVec(header->to);
      if(header->cc) FreeVec(header->cc);
      if(header->bcc) FreeVec(header->bcc);
      if(header->subject) FreeVec(header->subject);
      if(header->date) FreeVec(header->date);
      if(header->reply_to) FreeVec(header->reply_to);
      if(header->message_id) FreeVec(header->message_id);
      FreeVec(header);
   }
   
   html_part = message->html_part;
   text_part = message->text_part;
   message->html_part = NULL;
   message->text_part = NULL;
   
   if(html_part)
   {  if(BodyPartInList(message->body_parts, html_part))
      {  EMLDBG1("FreeEmailMessage: SKIP html_part=%lx (also on body_parts)",
            (ULONG)html_part);
      }
      else
      {  FreeEmailBodyPart(html_part, "html_part");
      }
   }
   if(text_part)
   {  if(text_part == html_part)
      {  EMLDBG0("FreeEmailMessage: SKIP text_part (same as html_part)");
      }
      else if(BodyPartInList(message->body_parts, text_part))
      {  EMLDBG1("FreeEmailMessage: SKIP text_part=%lx (also on body_parts)",
            (ULONG)text_part);
      }
      else
      {  FreeEmailBodyPart(text_part, "text_part");
      }
   }
   
   part = message->body_parts;
   while(part)
   {  nextpart = part->next;
      FreeEmailBodyPart(part, "body_parts");
      part = nextpart;
   }
   message->body_parts = NULL;
   
   attach = message->attachments;
   while(attach)
   {  nextattach = attach->next;
      EMLDBG3("FreeEmailMessage attach=%lx data=%lx datalen=%ld",
         (ULONG)attach, (ULONG)attach->data, attach->datalen);
      if(attach->filename) FreeVec(attach->filename);
      if(attach->content_type) FreeVec(attach->content_type);
      if(attach->content_id) FreeVec(attach->content_id);
      if(attach->content_disposition) FreeVec(attach->content_disposition);
      if(attach->data) FreeVec(attach->data);
      FreeVec(attach);
      attach = nextattach;
   }
   
   if(message->boundary) FreeVec(message->boundary);
   EMLDBG0("FreeEmailMessage done");
}

