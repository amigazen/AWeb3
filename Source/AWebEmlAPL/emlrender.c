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

/* emlrender.c - EML email HTML4 renderer */

#include "eml.h"
#include <exec/memory.h>
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <clib/awebplugin_protos.h>
#include <pragmas/awebplugin_pragmas.h>
#include <proto/exec.h>

/* Helper: Escape HTML special characters */
static void EscapeHtml(UBYTE *dest, UBYTE *src, long len, long maxlen)
{  UBYTE *p;
   UBYTE *d;
   long remaining;
   
   if(!dest || !src || len <= 0 || maxlen <= 0) return;
   
   d = dest;
   remaining = maxlen - 1;
   p = src;
   
   while(p < src + len && remaining > 0)
   {  if(*p == '<')
      {  if(remaining >= 4)
         {  strcpy(d, "&lt;");
            d += 4;
            remaining -= 4;
         }
         else
         {  break;
         }
      }
      else if(*p == '>')
      {  if(remaining >= 4)
         {  strcpy(d, "&gt;");
            d += 4;
            remaining -= 4;
         }
         else
         {  break;
         }
      }
      else if(*p == '&')
      {  if(remaining >= 5)
         {  strcpy(d, "&amp;");
            d += 5;
            remaining -= 5;
         }
         else
         {  break;
         }
      }
      else if(*p == '"')
      {  if(remaining >= 6)
         {  strcpy(d, "&quot;");
            d += 6;
            remaining -= 6;
         }
         else
         {  break;
         }
      }
      else
      {  *d++ = *p;
         remaining--;
      }
      p++;
   }
   
   *d = '\0';
}

/* Helper: Extract email address from header field */
static void RenderEmailAddress(UBYTE *field, long fieldlen, UBYTE *html, long *len, long maxlen)
{  UBYTE *p;
   UBYTE *end;
   UBYTE *email_start;
   UBYTE *email_end;
   UBYTE *name_start;
   UBYTE *name_end;
   UBYTE *comma;
   UBYTE escaped[512];
   UBYTE email[256];
   BOOL first;
   
   if(!field || fieldlen <= 0 || !html || !len || maxlen <= 0) return;
   
   p = field;
   end = field + fieldlen;
   first = TRUE;
   
   while(p < end && *len < maxlen - 100)
   {  /* Skip whitespace */
      while(p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
      if(p >= end) break;
      
      if(!first)
      {  if(*len + 2 < maxlen)
         {  html[*len++] = ',';
            html[*len++] = ' ';
         }
      }
      first = FALSE;
      
      name_start = NULL;
      name_end = NULL;
      email_start = NULL;
      email_end = NULL;
      
      /* Check for format: "Name" <email@domain.com> or Name <email@domain.com> */
      if(*p == '"')
      {  name_start = p + 1;
         p++;
         while(p < end && *p != '"') p++;
         if(p < end)
         {  name_end = p;
            p++;
            while(p < end && (*p == ' ' || *p == '\t')) p++;
            if(p < end && *p == '<')
            {  email_start = p + 1;
               p++;
               while(p < end && *p != '>') p++;
               if(p < end) email_end = p;
            }
         }
      }
      else if(*p == '<')
      {  email_start = p + 1;
         p++;
         while(p < end && *p != '>') p++;
         if(p < end) email_end = p;
      }
      else
      {  /* Try to find email address directly */
         UBYTE *at_pos;
         at_pos = NULL;
         email_start = p;
         /* First, scan for @ symbol */
         while(p < end && *p != ',' && *p != ';' && *p != '<' && *p != '>')
         {  if(*p == '@')
            {  at_pos = p;
               break;
            }
            p++;
         }
         if(at_pos)
         {  UBYTE *q;
            /* Look backwards for start */
            q = at_pos;
            while(q > email_start && ((q[-1] >= 'a' && q[-1] <= 'z') ||
                  (q[-1] >= 'A' && q[-1] <= 'Z') ||
                  (q[-1] >= '0' && q[-1] <= '9') ||
                  q[-1] == '.' || q[-1] == '_' || q[-1] == '+' || q[-1] == '-'))
            {  q--;
            }
            email_start = q;
            /* Look forwards for end */
            q = at_pos + 1;
            while(q < end && ((*q >= 'a' && *q <= 'z') ||
                  (*q >= 'A' && *q <= 'Z') ||
                  (*q >= '0' && *q <= '9') ||
                  *q == '.' || *q == '_' || *q == '-' || *q == '@'))
            {  q++;
            }
            email_end = q;
            p = q;
         }
         else
         {  /* No @ found, treat as plain text */
            email_start = NULL;
            email_end = NULL;
            while(p < end && *p != ',' && *p != ';') p++;
         }
      }
      
      if(email_start && email_end && email_end > email_start)
      {  long emaillen;
         emaillen = email_end - email_start;
         if(emaillen > sizeof(email) - 1) emaillen = sizeof(email) - 1;
         memcpy(email, email_start, emaillen);
         email[emaillen] = '\0';
         
         if(name_start && name_end && name_end > name_start)
         {  long namelen;
            namelen = name_end - name_start;
            if(namelen > sizeof(escaped) - 1) namelen = sizeof(escaped) - 1;
            EscapeHtml(escaped, name_start, namelen, sizeof(escaped));
            *len += sprintf(html + *len, "<A HREF=\"mailto:%s\">%s</A>", email, escaped);
         }
         else
         {  EscapeHtml(escaped, email_start, emaillen, sizeof(escaped));
            *len += sprintf(html + *len, "<A HREF=\"mailto:%s\">%s</A>", email, escaped);
         }
      }
      else if(p < end)
      {  /* No email found, just escape and display */
         UBYTE *text_start;
         text_start = p;
         while(p < end && *p != ',' && *p != ';') p++;
         if(p > text_start)
         {  EscapeHtml(escaped, text_start, p - text_start, sizeof(escaped));
            *len += sprintf(html + *len, "%s", escaped);
         }
      }
      
      /* Find next address (comma or semicolon) */
      comma = p;
      while(comma < end && *comma != ',' && *comma != ';') comma++;
      if(comma < end) p = comma + 1;
      else break;
   }
   
   html[*len] = '\0';
}

/* Maximum bytes accepted in one AppendHtml call (guards corrupted len). */
#define EML_APPENDHTML_MAX 65536L

/* Helper: Append to HTML buffer */
static void AppendHtml(struct EmlFilterData *fd, UBYTE *html, long len)
{  UBYTE *newbuffer;
   long newlen;
   
   Aprintf("EML: AppendHtml called: fd=%lx, html=%lx, len=%ld\n", (ULONG)fd, (ULONG)html, len);
   if(!fd || !html || len <= 0 || len > EML_APPENDHTML_MAX)
   {  Aprintf("EML: AppendHtml - Invalid parameters, returning\n");
      return;
   }
   
   Aprintf("EML: AppendHtml - Current buffer: buf=%lx, buflen=%ld, bufsize=%ld\n",
      (ULONG)fd->html_buffer, fd->html_buflen, fd->html_bufsize);
   newlen = fd->html_buflen + len;
   if(newlen >= fd->html_bufsize)
   {  Aprintf("EML: AppendHtml - Need to expand buffer: newlen=%ld\n", newlen);
      newbuffer = (UBYTE *)AllocVec(newlen + 4096, MEMF_CLEAR);
      if(newbuffer)
      {  Aprintf("EML: AppendHtml - Allocated new buffer: %lx, size=%ld\n",
            (ULONG)newbuffer, newlen + 4096);
         if(fd->html_buffer)
         {  Aprintf("EML: AppendHtml - Copying old buffer (%ld bytes)\n", fd->html_buflen);
            memcpy(newbuffer, fd->html_buffer, fd->html_buflen);
            Aprintf("EML: AppendHtml - Freeing old buffer\n");
            FreeVec(fd->html_buffer);
         }
         fd->html_buffer = newbuffer;
         fd->html_bufsize = newlen + 4096;
         Aprintf("EML: AppendHtml - Buffer updated\n");
      }
      else
      {  Aprintf("EML: AppendHtml - ERROR - Failed to allocate new buffer!\n");
         return;
      }
   }
   
   Aprintf("EML: AppendHtml - Copying %ld bytes to buffer at offset %ld\n", len, fd->html_buflen);
   if(fd->html_buffer)
   {  memcpy(fd->html_buffer + fd->html_buflen, html, len);
      fd->html_buflen += len;
      Aprintf("EML: AppendHtml - Complete, new buflen=%ld\n", fd->html_buflen);
   }
   else
   {  Aprintf("EML: AppendHtml - ERROR - html_buffer is NULL after allocation!\n");
   }
}

/* Append a NUL-terminated string to the output buffer. */
static void AppendHtmlStr(struct EmlFilterData *fd, UBYTE *s)
{  long slen;
   if(!fd || !s || !*s) return;
   slen = (long)strlen(s);
   if(slen > 0) AppendHtml(fd, s, slen);
}

/* Append raw MIME bytes in chunks (HTML body parts; no escaping). */
static void AppendHtmlRaw(struct EmlFilterData *fd, UBYTE *data, long datalen)
{  long off;
   long chunk;
   if(!fd || !data || datalen <= 0) return;
   off = 0;
   while(off < datalen)
   {  chunk = datalen - off;
      if(chunk > EML_APPENDHTML_MAX) chunk = EML_APPENDHTML_MAX;
      AppendHtml(fd, data + off, chunk);
      off += chunk;
   }
}

/* Render text/plain into the heap buffer line-by-line (no 8K stack staging). */
static void RenderPlainTextPart(struct EmlFilterData *fd, struct EmailBodyPart *part)
{  UBYTE *p;
   UBYTE *end;
   UBYTE *line;
   UBYTE *lineend;
   UBYTE escaped[2048];
   long elen;
   
   if(!fd || !part || !part->data || part->datalen <= 0) return;
   
   AppendHtmlStr(fd, "<DIV CLASS=\"email-body\">\n<P>");
   p = part->data;
   end = part->data + part->datalen;
   while(p < end)
   {  line = p;
      lineend = p;
      while(lineend < end && *lineend != '\r' && *lineend != '\n') lineend++;
      
      if(lineend > line)
      {  EscapeHtml(escaped, line, lineend - line, sizeof(escaped));
         elen = (long)strlen(escaped);
         if(elen > 0) AppendHtml(fd, escaped, elen);
      }
      
      if(lineend < end)
      {  if(*lineend == '\r') lineend++;
         if(lineend < end && *lineend == '\n') lineend++;
         AppendHtmlStr(fd, "<BR>\n");
      }
      p = lineend;
   }
   AppendHtmlStr(fd, "</P>\n</DIV>\n");
}

/* Log parsed message layout before render / free (debug). */
static void LogMessageLayout(struct EmailMessage *message)
{  struct EmailBodyPart *part;
   struct EmailAttachment *attach;
   long n;
   
   if(!message) return;
   EMLDBG4("Message layout html=%lx text=%lx body=%lx attach=%lx",
      (ULONG)message->html_part, (ULONG)message->text_part,
      (ULONG)message->body_parts, (ULONG)message->attachments);
   if(message->html_part)
   {  EMLDBG2("  html_part data=%lx len=%ld",
         (ULONG)message->html_part->data, message->html_part->datalen);
   }
   if(message->text_part)
   {  EMLDBG2("  text_part data=%lx len=%ld",
         (ULONG)message->text_part->data, message->text_part->datalen);
   }
   n = 0;
   for(part = message->body_parts; part; part = part->next)
   {  EMLDBG4("  body_parts[%ld] part=%lx data=%lx len=%ld",
         n, (ULONG)part, (ULONG)part->data, part->datalen);
      n++;
   }
   n = 0;
   for(attach = message->attachments; attach; attach = attach->next)
   {  EMLDBG4("  attach[%ld] obj=%lx data=%lx len=%ld",
         n, (ULONG)attach, (ULONG)attach->data, attach->datalen);
      n++;
   }
}

/* Render email to HTML */
void RenderEmailToHtml(struct EmlFilterData *fd, void *handle)
{  struct EmailMessage *message;
   struct EmailHeader *header;
   UBYTE html[8192];
   UBYTE escaped[2048];
   long len;
   struct EmailBodyPart *part;
   struct EmailAttachment *attach;
   
   EMLDBG2("RenderEmailToHtml enter fd=%lx handle=%lx", (ULONG)fd, (ULONG)handle);
   if(!fd || !fd->parser || !fd->parser->message)
   {  EMLDBG3("RenderEmailToHtml ABORT fd=%lx parser=%lx message=%lx",
         (ULONG)fd, fd ? (ULONG)fd->parser : 0,
         fd && fd->parser ? (ULONG)fd->parser->message : 0);
      return;
   }
   
   message = fd->parser->message;
   header = message->headers;
   LogMessageLayout(message);
   
   if(!header)
   {  EMLDBG0("RenderEmailToHtml ABORT: message->headers is NULL");
      return;
   }
   
   /* Write HTML header if not already written */
   if(!fd->header_written)
   {  len = sprintf(html, 
         "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\n"
         "<HTML>\n<HEAD>\n<TITLE>");
      
      if(header->subject && header->subjectlen > 0)
      {  EscapeHtml(escaped, header->subject, header->subjectlen, sizeof(escaped));
         len += sprintf(html + len, "%s", escaped);
      }
      else
      {  len += sprintf(html + len, "Email Message");
      }
      
      len += sprintf(html + len, 
         "</TITLE>\n"
         "<STYLE TYPE=\"text/css\">\n"
         "<!--\n"
         "BODY { font-family: Arial, Helvetica, sans-serif; margin: 10px; padding: 10px; background: #F5F5F5; }\n"
         ".email-header { background: #FFFFFF; border: 1px solid #CCCCCC; padding: 15px; margin-bottom: 15px; }\n"
         ".email-header H1 { font-size: 18px; margin: 0 0 10px 0; color: #333333; border-bottom: 2px solid #0066CC; padding-bottom: 5px; }\n"
         ".email-header TABLE { width: 100%%; border-collapse: collapse; }\n"
         ".email-header TD { padding: 5px; vertical-align: top; }\n"
         ".email-header .label { font-weight: bold; color: #666666; width: 100px; }\n"
         ".email-header .value { color: #333333; }\n"
         ".email-header .value A { color: #0066CC; text-decoration: none; }\n"
         ".email-header .value A:hover { text-decoration: underline; }\n"
         ".email-body { background: #FFFFFF; border: 1px solid #CCCCCC; padding: 15px; margin-bottom: 15px; }\n"
         ".email-body P { margin: 10px 0; line-height: 1.6; color: #333333; }\n"
         ".email-attachments { background: #FFFFFF; border: 1px solid #CCCCCC; padding: 15px; }\n"
         ".email-attachments H2 { font-size: 16px; margin: 0 0 10px 0; color: #333333; border-bottom: 1px solid #EEEEEE; padding-bottom: 5px; }\n"
         ".email-attachments UL { list-style: none; padding: 0; margin: 0; }\n"
         ".email-attachments LI { padding: 8px; margin: 5px 0; background: #F9F9F9; border-left: 3px solid #0066CC; }\n"
         ".email-attachments A { color: #0066CC; text-decoration: none; font-weight: bold; }\n"
         ".email-attachments A:hover { text-decoration: underline; }\n"
         "//-->\n"
         "</STYLE>\n"
         "</HEAD>\n"
         "<BODY>\n");
      
      /* Email header section */
      len += sprintf(html + len, "<DIV CLASS=\"email-header\">\n<H1>");
      
      if(header->subject && header->subjectlen > 0)
      {  EscapeHtml(escaped, header->subject, header->subjectlen, sizeof(escaped));
         len += sprintf(html + len, "%s", escaped);
      }
      else
      {  len += sprintf(html + len, "(No Subject)");
      }
      
      len += sprintf(html + len, "</H1>\n<TABLE>\n");
      
      if(header->from && header->fromlen > 0)
      {  long len_before;
         len_before = len;
         len += sprintf(html + len, "<TR><TD CLASS=\"label\">From:</TD><TD CLASS=\"value\">");
         RenderEmailAddress(header->from, header->fromlen, html, &len, sizeof(html) - len);
         /* Fallback: if no email was rendered, just escape and display the text */
         if(len == len_before + (long)strlen("<TR><TD CLASS=\"label\">From:</TD><TD CLASS=\"value\">"))
         {  EscapeHtml(escaped, header->from, header->fromlen, sizeof(escaped));
            len += sprintf(html + len, "%s", escaped);
         }
         len += sprintf(html + len, "</TD></TR>\n");
      }
      
      if(header->to && header->tolen > 0)
      {  long len_before;
         len_before = len;
         len += sprintf(html + len, "<TR><TD CLASS=\"label\">To:</TD><TD CLASS=\"value\">");
         RenderEmailAddress(header->to, header->tolen, html, &len, sizeof(html) - len);
         /* Fallback: if no email was rendered, just escape and display the text */
         if(len == len_before + (long)strlen("<TR><TD CLASS=\"label\">To:</TD><TD CLASS=\"value\">"))
         {  EscapeHtml(escaped, header->to, header->tolen, sizeof(escaped));
            len += sprintf(html + len, "%s", escaped);
         }
         len += sprintf(html + len, "</TD></TR>\n");
      }
      
      if(header->cc && header->cclen > 0)
      {  long len_before;
         len_before = len;
         len += sprintf(html + len, "<TR><TD CLASS=\"label\">Cc:</TD><TD CLASS=\"value\">");
         RenderEmailAddress(header->cc, header->cclen, html, &len, sizeof(html) - len);
         /* Fallback: if no email was rendered, just escape and display the text */
         if(len == len_before + (long)strlen("<TR><TD CLASS=\"label\">Cc:</TD><TD CLASS=\"value\">"))
         {  EscapeHtml(escaped, header->cc, header->cclen, sizeof(escaped));
            len += sprintf(html + len, "%s", escaped);
         }
         len += sprintf(html + len, "</TD></TR>\n");
      }
      
      if(header->date && header->datelen > 0)
      {  EscapeHtml(escaped, header->date, header->datelen, sizeof(escaped));
         len += sprintf(html + len, "<TR><TD CLASS=\"label\">Date:</TD><TD CLASS=\"value\">%s</TD></TR>\n", escaped);
      }
      
      len += sprintf(html + len, "</TABLE>\n</DIV>\n");
      
      AppendHtml(fd, html, len);
      fd->header_written = TRUE;
   }
   
   /* Render email body */
   Aprintf("EML: Checking for HTML body: html_part=%lx, text_part=%lx, body_parts=%lx\n",
      (ULONG)message->html_part, (ULONG)message->text_part, (ULONG)message->body_parts);
   
   /* First, check for HTML attachments that should be rendered inline */
   if(message->attachments)
   {  attach = message->attachments;
      while(attach)
      {  BOOL is_html_attach;
         is_html_attach = FALSE;
         if(attach->content_type && attach->content_typelen > 0)
         {  if(strstr(attach->content_type, "text/html"))
            {  is_html_attach = TRUE;
            }
         }
         
         if(is_html_attach && attach->data && attach->datalen > 0)
         {  Aprintf("EML: Found HTML attachment, rendering inline: data=%lx, datalen=%ld\n",
               (ULONG)attach->data, attach->datalen);
            /* TODO: HTML sanitization; cid: URLs preserved for AWeb CID handler */
            AppendHtmlStr(fd, "<DIV CLASS=\"email-body\">\n");
            AppendHtmlRaw(fd, attach->data, attach->datalen);
            AppendHtmlStr(fd, "</DIV>\n");
            Aprintf("EML: HTML attachment rendered inline, datalen=%ld\n", attach->datalen);
            /* Mark as rendered so it's not listed in attachments */
            attach->is_inline = TRUE; /* Mark so we skip it in attachment list */
         }
         attach = attach->next;
      }
   }
   
   if(message->html_part)
   {  part = message->html_part;
      Aprintf("EML: Rendering HTML body part: data=%lx, datalen=%ld\n", (ULONG)part->data, part->datalen);
      AppendHtmlStr(fd, "<DIV CLASS=\"email-body\">\n");
      if(part->data && part->datalen > 0)
      {  AppendHtmlRaw(fd, part->data, part->datalen);
      }
      AppendHtmlStr(fd, "</DIV>\n");
      Aprintf("EML: HTML body rendered, datalen=%ld\n", part->datalen);
   }
   else if(message->text_part)
   {  Aprintf("EML: Rendering text body part (no HTML part found)\n");
      RenderPlainTextPart(fd, message->text_part);
      Aprintf("EML: Text body rendered, datalen=%ld\n", message->text_part->datalen);
   }
   else if(message->body_parts)
   {  Aprintf("EML: Rendering other body parts (no HTML or text part found)\n");
      part = message->body_parts;
      if(part->content_type && strstr(part->content_type, "text/html")
         && part->data && part->datalen > 0)
      {  AppendHtmlStr(fd, "<DIV CLASS=\"email-body\">\n");
         AppendHtmlRaw(fd, part->data, part->datalen);
         AppendHtmlStr(fd, "</DIV>\n");
      }
      else
      {  RenderPlainTextPart(fd, part);
      }
   }
   
   /* Register parts with Content-ID for CID registry (for cid: URL access) */
   /* Only register once - check if we've already registered */
   Aprintf("EML: Starting CID registration, eml_url=%lx, already_registered=%ld\n", (ULONG)fd->eml_url, (ULONG)fd->cid_registered);
   if(fd->eml_url && !fd->cid_registered)
   {  /* Register inline attachments */
      if(message->attachments)
      {  Aprintf("EML: Processing %ld attachments for CID registration\n", message->attachment_count);
         attach = message->attachments;
         while(attach)
         {  Aprintf("EML: Checking attachment: is_inline=%ld, data=%lx, datalen=%ld\n",
               (ULONG)attach->is_inline, (ULONG)attach->data, attach->datalen);
            /* Register inline attachments for cid: URL access */
            /* Also register image attachments with Content-ID even if not explicitly inline */
            /* Note: Images without Content-ID will use data: URLs instead */
            if(attach->data && attach->datalen > 0)
            {  BOOL is_image;
               is_image = FALSE;
               if(attach->content_type && attach->content_typelen > 0)
               {  if(strstr(attach->content_type, "image/"))
                  {  is_image = TRUE;
                  }
               }
               
               /* Register if explicitly inline OR if it's an image with Content-ID */
               /* Only register with CID registry if we have a Content-ID (for cid: URLs) */
               if((attach->is_inline || is_image) && attach->content_id && attach->content_idlen > 0)
               {  UBYTE *content_id;
                  UBYTE *content_id_clean;
                  UBYTE *content_id_reg;
                  long cid_len;
                  UBYTE *content_type;
                  
                  /* Clean Content-ID (remove angle brackets if present) for registration
                   * This ensures lookups will match (HTML uses cid:image001@example.com, not cid:<image001@example.com>) */
                  content_id = attach->content_id;
                  content_id_clean = attach->content_id;
                  cid_len = attach->content_idlen;
                  Aprintf("EML: Using Content-ID: %.*s\n", (int)attach->content_idlen, attach->content_id);
                  
                  if(*content_id_clean == '<')
                  {  content_id_clean++;
                     cid_len--;
                  }
                  if(cid_len > 0 && content_id_clean[cid_len - 1] == '>')
                  {  cid_len--;
                  }
                  
                  /* Create cleaned copy for registration */
                  if(cid_len > 0)
                  {  content_id_reg = (UBYTE *)AllocVec(cid_len + 1, MEMF_CLEAR);
                     if(content_id_reg)
                     {  memcpy(content_id_reg, content_id_clean, cid_len);
                        content_id_reg[cid_len] = '\0';
                        Aprintf("EML: Cleaned Content-ID for registration: %s (len=%ld)\n", content_id_reg, cid_len);
                     }
                     else
                     {  content_id_reg = NULL;
                        Aprintf("EML: ERROR - Failed to allocate cleaned Content-ID\n");
                     }
                  }
                  else
                  {  content_id_reg = NULL;
                     Aprintf("EML: ERROR - Invalid Content-ID length after cleaning\n");
                  }
                  
                  /* Get content type */
                  if(attach->content_type && attach->content_typelen > 0)
                  {  content_type = attach->content_type;
                     Aprintf("EML: Content-Type: %.*s\n", (int)attach->content_typelen, attach->content_type);
                  }
                  else
                  {  content_type = "application/octet-stream";
                     Aprintf("EML: Using default content type\n");
                  }
                  
                  /* Register with CID registry using cleaned Content-ID */
                  if(content_id_reg)
                  {  Aprintf("EML: Registering CID part: referer=%s, content_id=%s, content_type=%s, data=%lx, datalen=%ld\n",
                        fd->eml_url, content_id_reg, content_type, (ULONG)attach->data, attach->datalen);
                     if(!fd->eml_url || !content_id_reg || !attach->data || attach->datalen <= 0)
                     {  Aprintf("EML: ERROR - Invalid parameters for Registercidpart!\n");
                     }
                     else
                     {  /* Verify strings are null-terminated */
                        if(strlen(fd->eml_url) == 0)
                        {  Aprintf("EML: ERROR - eml_url is empty or not null-terminated!\n");
                        }
                        else if(strlen(content_id_reg) == 0)
                        {  Aprintf("EML: ERROR - content_id is empty or not null-terminated!\n");
                        }
                        else
                        {  Aprintf("EML: Calling Registercidpart...\n");
                           EMLDBG2("Registercidpart attach data=%lx len=%ld",
                              (ULONG)attach->data, attach->datalen);
                           if(Registercidpart(fd->eml_url, content_id_reg, content_type,
                              attach->data, attach->datalen))
                           {  /* Registry copied into AWeb pool; release Exec buffer. */
                              EmlReleaseExecPayload(&attach->data, &attach->datalen);
                              fd->cid_registered = TRUE;
                              EMLDBG0("Registercidpart attach: stored in registry");
                           }
                        }
                     }
                     FreeVec(content_id_reg);
                  }
               }
               else if(attach->is_inline || is_image)
               {  /* Image/inline attachment without Content-ID - will use data: URL, no CID registration needed */
                  Aprintf("EML: Image/inline attachment without Content-ID - will use data: URL\n");
               }
               else
               {  Aprintf("EML: Attachment not inline/image - skipping CID registration\n");
               }
            }
            else
            {  Aprintf("EML: Attachment missing data\n");
            }
            attach = attach->next;
         }
      }
      else
      {  Aprintf("EML: No attachments to register\n");
      }
      
      /* Register body parts with Content-ID (for multipart/related inline images) */
      if(message->body_parts)
      {  Aprintf("EML: Processing body parts for CID registration\n");
         part = message->body_parts;
         while(part)
         {  Aprintf("EML: Body part: content_id=%lx, content_idlen=%ld, data=%lx, datalen=%ld\n",
               (ULONG)part->content_id, part->content_idlen, (ULONG)part->data, part->datalen);
            /* Skip parts with null data pointer - these are likely attachments that own the data */
            if(!part->data || part->datalen <= 0)
            {  Aprintf("EML: Skipping body part - null data pointer or zero length (likely attachment-owned)\n");
               part = part->next;
               continue;
            }
            if(part->content_id && part->content_idlen > 0)
            {  UBYTE *content_id_clean;
               UBYTE *content_id_reg;
               long cid_len;
               UBYTE *content_type;
               
               /* Clean Content-ID (remove angle brackets if present) for registration */
               content_id_clean = part->content_id;
               cid_len = part->content_idlen;
               if(*content_id_clean == '<')
               {  content_id_clean++;
                  cid_len--;
               }
               if(cid_len > 0 && content_id_clean[cid_len - 1] == '>')
               {  cid_len--;
               }
               
               /* Create cleaned copy for registration */
               if(cid_len > 0)
               {  content_id_reg = (UBYTE *)AllocVec(cid_len + 1, MEMF_CLEAR);
                  if(content_id_reg)
                  {  memcpy(content_id_reg, content_id_clean, cid_len);
                     content_id_reg[cid_len] = '\0';
                     Aprintf("EML: Cleaned body part Content-ID for registration: %s (len=%ld)\n", content_id_reg, cid_len);
                  }
                  else
                  {  content_id_reg = NULL;
                     Aprintf("EML: ERROR - Failed to allocate cleaned Content-ID for body part\n");
                  }
               }
               else
               {  content_id_reg = NULL;
                  Aprintf("EML: ERROR - Invalid body part Content-ID length after cleaning\n");
               }
               
               /* Get content type */
               if(part->content_type && part->content_typelen > 0)
               {  content_type = part->content_type;
                  Aprintf("EML: Body part Content-Type: %.*s\n", (int)part->content_typelen, part->content_type);
               }
               else
               {  content_type = "application/octet-stream";
                  Aprintf("EML: Body part using default content type\n");
               }
               
               /* Register with CID registry using cleaned Content-ID */
               if(content_id_reg)
               {  Aprintf("EML: Registering body part CID: %s, content_type=%s, data=%lx, datalen=%ld\n",
                     content_id_reg, content_type, (ULONG)part->data, part->datalen);
                  if(!fd->eml_url || !content_id_reg || !part->data || part->datalen <= 0)
                  {  Aprintf("EML: ERROR - Invalid parameters for body part Registercidpart!\n");
                  }
                  else
                  {  /* Verify strings are null-terminated */
                     if(strlen(fd->eml_url) == 0)
                     {  Aprintf("EML: ERROR - eml_url is empty or not null-terminated!\n");
                     }
                     else if(strlen(content_id_reg) == 0)
                     {  Aprintf("EML: ERROR - body part content_id is empty or not null-terminated!\n");
                     }
                     else
                     {  Aprintf("EML: Calling Registercidpart for body part...\n");
                        EMLDBG2("Registercidpart body part data=%lx len=%ld",
                           (ULONG)part->data, part->datalen);
                        if(Registercidpart(fd->eml_url, content_id_reg, content_type,
                           part->data, part->datalen))
                        {  EmlReleaseExecPayload(&part->data, &part->datalen);
                           fd->cid_registered = TRUE;
                           EMLDBG0("Registercidpart body: stored in registry");
                        }
                     }
                  }
                  FreeVec(content_id_reg);
               }
            }
            part = part->next;
         }
      }
      else
      {  Aprintf("EML: No body parts to register\n");
      }
      
   }
   else if(fd->cid_registered)
   {  Aprintf("EML: CID already registered - skipping\n");
   }
   else
   {  Aprintf("EML: No eml_url - skipping CID registration\n");
   }
   
   /* Render inline images and HTML attachments in the email body */
   if(message->attachments)
   {  attach = message->attachments;
      while(attach)
      {  BOOL is_image;
         BOOL is_html_attach;
         BOOL should_render_inline;
         
         /* Check if this is an image type */
         is_image = FALSE;
         if(attach->content_type && attach->content_typelen > 0)
         {  if(strstr(attach->content_type, "image/"))
            {  is_image = TRUE;
            }
         }
         
         /* Check if this is an HTML attachment (already rendered above) */
         is_html_attach = FALSE;
         if(attach->content_type && attach->content_typelen > 0)
         {  if(strstr(attach->content_type, "text/html"))
            {  is_html_attach = TRUE;
            }
         }
         
         /* Determine if we should render inline:
          * - If explicitly marked as inline, OR
          * - If it's an image (will use cid: URL if Content-ID exists, data: URL otherwise) */
         should_render_inline = attach->is_inline || is_image;
         
         Aprintf("EML: Checking attachment for inline rendering: is_inline=%ld, is_image=%ld, is_html=%ld, should_render_inline=%ld, content_id=%lx, content_idlen=%ld\n",
            (ULONG)attach->is_inline, (ULONG)is_image, (ULONG)is_html_attach, (ULONG)should_render_inline,
            (ULONG)attach->content_id, attach->content_idlen);
         
         /* Render inline images in the email body */
         /* Render inline images in the email body */
         if(should_render_inline && is_image && attach->data && attach->datalen > 0)
         {  /* Inline image - render as IMG tag in email body */
            Aprintf("EML: Processing inline image for email body\n");
            
            len = sprintf(html, "<DIV CLASS=\"email-body\">\n");
            
            if(attach->content_id && attach->content_idlen > 0)
            {  /* Use cid: URL if Content-ID is available */
               UBYTE *content_id_clean;
               long cid_len;
               
               /* Clean Content-ID (remove angle brackets if present) */
               content_id_clean = attach->content_id;
               cid_len = attach->content_idlen;
               Aprintf("EML: Original Content-ID: %.*s (len=%ld)\n", (int)attach->content_idlen, attach->content_id, attach->content_idlen);
               if(*content_id_clean == '<')
               {  content_id_clean++;
                  cid_len--;
                  Aprintf("EML: Removed leading <\n");
               }
               if(cid_len > 0 && content_id_clean[cid_len - 1] == '>')
               {  cid_len--;
                  Aprintf("EML: Removed trailing >\n");
               }
               
               Aprintf("EML: Cleaned Content-ID len=%ld\n", cid_len);
               if(cid_len > 0)
               {  Aprintf("EML: Escaping Content-ID for HTML\n");
                  EscapeHtml(escaped, content_id_clean, cid_len, sizeof(escaped));
                  Aprintf("EML: Escaped Content-ID: %s\n", escaped);
                  len += sprintf(html + len, "<P><IMG SRC=\"cid:%s\" ALT=\"Inline Image\" STYLE=\"max-width: 100%%; height: auto;\"></P>\n", escaped);
                  Aprintf("EML: Inline image HTML written (cid:), new len=%ld\n", len);
               }
               else
               {  Aprintf("EML: ERROR - Invalid Content-ID length after cleaning, falling back to data: URL\n");
                  /* Fall through to data: URL */
                  should_render_inline = FALSE; /* Force data: URL path */
               }
            }
            
            /* If no Content-ID or fallback, use data: URL */
            if(!attach->content_id || attach->content_idlen <= 0 || !should_render_inline)
            {  UBYTE *base64_data;
               long base64_len;
               UBYTE *content_type_str;
               UBYTE *content_type_clean;
               UBYTE *semicolon;
               
               Aprintf("EML: Using data: URL for inline image\n");
               
               /* Encode image data as base64 */
               base64_data = EncodeBase64(attach->data, attach->datalen, &base64_len);
               if(base64_data && base64_len > 0)
               {  /* Get and clean content type (remove parameters like name="...") */
                  if(attach->content_type && attach->content_typelen > 0)
                  {  /* Extract base MIME type (before semicolon) */
                     semicolon = (UBYTE *)strchr(attach->content_type, ';');
                     if(semicolon)
                     {  long clean_len;
                        clean_len = semicolon - attach->content_type;
                        content_type_clean = (UBYTE *)AllocVec(clean_len + 1, MEMF_CLEAR);
                        if(content_type_clean)
                        {  memcpy(content_type_clean, attach->content_type, clean_len);
                           content_type_clean[clean_len] = '\0';
                           content_type_str = content_type_clean;
                        }
                        else
                        {  content_type_str = "image/png"; /* Fallback */
                        }
                     }
                     else
                     {  content_type_str = attach->content_type;
                        content_type_clean = NULL;
                     }
                  }
                  else
                  {  content_type_str = "image/png"; /* Default */
                     content_type_clean = NULL;
                  }
                  
                  Aprintf("EML: Base64 encoded image, len=%ld, content_type=%s\n", base64_len, content_type_str);
                  
                  /* Build data: URL - data:image/png;base64,<data> */
                  if(len + 100 + base64_len < sizeof(html))
                  {  long data_url_len;
                     UBYTE *data_url_start;
                     
                     data_url_start = html + len;
                     data_url_len = sprintf(data_url_start, "<P><IMG SRC=\"data:%s;base64,", content_type_str);
                     
                     /* Append base64 data (need to be careful about buffer size) */
                     if(data_url_len + base64_len + 50 < sizeof(html) - len)
                     {  memcpy(data_url_start + data_url_len, base64_data, base64_len);
                        data_url_len += base64_len;
                        data_url_len += sprintf(data_url_start + data_url_len, "\" ALT=\"Inline Image\" STYLE=\"max-width: 100%%; height: auto;\"></P>\n");
                        len += data_url_len;
                        Aprintf("EML: Inline image HTML written (data:), new len=%ld\n", len);
                     }
                     else
                     {  Aprintf("EML: ERROR - data: URL too large for buffer, skipping\n");
                        len += sprintf(html + len, "<P>Image too large to embed</P>\n");
                     }
                  }
                  else
                  {  Aprintf("EML: ERROR - HTML buffer too small for data: URL\n");
                     len += sprintf(html + len, "<P>Image too large to embed</P>\n");
                  }
                  
                  /* Free allocated content type if we created it */
                  if(content_type_clean) FreeVec(content_type_clean);
                  FreeVec(base64_data);
               }
               else
               {  Aprintf("EML: ERROR - Failed to encode image as base64\n");
                  len += sprintf(html + len, "<P>Failed to encode image</P>\n");
               }
            }
            
            len += sprintf(html + len, "</DIV>\n");
            AppendHtml(fd, html, len);
            Aprintf("EML: Inline image rendered in email body, total len=%ld\n", len);
         }
         
         attach = attach->next;
      }
   }
   
   /* Render attachments list (excluding HTML attachments and inline images) */
   Aprintf("EML: Starting attachment rendering, count=%ld\n", message->attachment_count);
   if(message->attachments && message->attachment_count > 0)
   {  long non_inline_count;
      struct EmailAttachment *attach_check;
      
      /* Count non-inline attachments (excluding HTML and inline images) */
      non_inline_count = 0;
      attach_check = message->attachments;
      while(attach_check)
      {  BOOL is_image_check;
         BOOL is_html_check;
         BOOL should_skip;
         
         is_image_check = FALSE;
         is_html_check = FALSE;
         if(attach_check->content_type && attach_check->content_typelen > 0)
         {  if(strstr(attach_check->content_type, "image/"))
            {  is_image_check = TRUE;
            }
            if(strstr(attach_check->content_type, "text/html"))
            {  is_html_check = TRUE;
            }
         }
         
         should_skip = (attach_check->is_inline || is_image_check || is_html_check);
         if(!should_skip)
         {  non_inline_count++;
         }
         attach_check = attach_check->next;
      }
      
      if(non_inline_count > 0)
      {  Aprintf("EML: Allocating HTML buffer for attachments\n");
         len = sprintf(html, "<DIV CLASS=\"email-attachments\">\n<H2>Attachments (%ld)</H2>\n<UL>\n", non_inline_count);
         Aprintf("EML: HTML header written, len=%ld\n", len);
         
         attach = message->attachments;
         while(attach)
         {  BOOL is_image;
            BOOL is_html_attach;
            BOOL should_skip;
            
            /* Check if this is an image type */
            is_image = FALSE;
            if(attach->content_type && attach->content_typelen > 0)
            {  if(strstr(attach->content_type, "image/"))
               {  is_image = TRUE;
               }
            }
            
            /* Check if this is an HTML attachment */
            is_html_attach = FALSE;
            if(attach->content_type && attach->content_typelen > 0)
            {  if(strstr(attach->content_type, "text/html"))
               {  is_html_attach = TRUE;
               }
            }
            
            /* Skip HTML attachments and inline images (already rendered above) */
            should_skip = (attach->is_inline || is_image || is_html_attach);
            
            Aprintf("EML: Rendering attachment: is_inline=%ld, is_image=%ld, is_html=%ld, should_skip=%ld, filename=%lx, filenamelen=%ld\n",
               (ULONG)attach->is_inline, (ULONG)is_image, (ULONG)is_html_attach, (ULONG)should_skip,
               (ULONG)attach->filename, attach->filenamelen);
            
            if(!should_skip)
            {  if(attach->filename && attach->filenamelen > 0)
               {  long filesize;
                  
                  Aprintf("EML: Processing regular attachment with filename\n");
                  filesize = 0;
                  if(attach->data && attach->datalen > 0)
                  {  filesize = attach->datalen;
                     Aprintf("EML: Attachment data size: %ld\n", filesize);
                  }
                  else
                  {  Aprintf("EML: WARNING - Attachment has no data\n");
                  }
                  
                  Aprintf("EML: Escaping filename: %.*s\n", (int)attach->filenamelen, attach->filename);
                  EscapeHtml(escaped, attach->filename, attach->filenamelen, sizeof(escaped));
                  Aprintf("EML: Escaped filename: %s\n", escaped);
                  
                  if(filesize > 0)
                  {  if(filesize < 1024)
                     {  len += sprintf(html + len, "<LI><A HREF=\"#\">%s</A> (%ld bytes)", escaped, filesize);
                     }
                     else if(filesize < 1024 * 1024)
                     {  len += sprintf(html + len, "<LI><A HREF=\"#\">%s</A> (%ld KB)", escaped, filesize / 1024);
                     }
                     else
                     {  len += sprintf(html + len, "<LI><A HREF=\"#\">%s</A> (%ld MB)", escaped, filesize / (1024 * 1024));
                     }
                  }
                  else
                  {  len += sprintf(html + len, "<LI><A HREF=\"#\">%s</A>", escaped);
                  }
                  
                  if(attach->content_type && attach->content_typelen > 0)
                  {  EscapeHtml(escaped, attach->content_type, attach->content_typelen, sizeof(escaped));
                     len += sprintf(html + len, " <SPAN STYLE=\"color: #666666; font-size: 11px;\">(%s)</SPAN>", escaped);
                  }
                  
                  len += sprintf(html + len, "</LI>\n");
               }
               else if(attach->content_type && attach->content_typelen > 0)
               {  EscapeHtml(escaped, attach->content_type, attach->content_typelen, sizeof(escaped));
                  len += sprintf(html + len, "<LI><A HREF=\"#\">Attachment</A> <SPAN STYLE=\"color: #666666; font-size: 11px;\">(%s)</SPAN></LI>\n", escaped);
               }
            }
            
            attach = attach->next;
            Aprintf("EML: Moving to next attachment\n");
         }
         
         Aprintf("EML: Writing attachment list footer\n");
         len += sprintf(html + len, "</UL>\n</DIV>\n");
         Aprintf("EML: Appending HTML, total len=%ld\n", len);
         AppendHtml(fd, html, len);
         Aprintf("EML: Attachment rendering complete\n");
      }
      else
      {  Aprintf("EML: No non-inline attachments to render\n");
      }
   }
   else
   {  Aprintf("EML: No attachments to render\n");
   }
   
   /* Write footer and flush accumulated HTML in one Writefilter call. */
   if(!fd->footer_written)
   {  len = sprintf(html, "</BODY>\n</HTML>\n");
      AppendHtml(fd, html, len);
      fd->footer_written = TRUE;
   }
   
   if(fd->html_buffer && fd->html_buflen > 0)
   {        EMLDBG2("RenderEmailToHtml: Writefilter len=%ld buf=%lx",
         fd->html_buflen, (ULONG)fd->html_buffer);
      Writefilter(handle, fd->html_buffer, fd->html_buflen);
      fd->html_buflen = 0;
   }
   
   LogMessageLayout(message);
   EMLDBG0("RenderEmailToHtml leave");
}

