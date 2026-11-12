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

/* End of one address in a From/To/Cc list. */
static UBYTE *EmlAddrSegEnd(UBYTE *p,UBYTE *end)
{
   while(p<end && *p!=',' && *p!=';') p++;
   return p;
}

/* Write one mailto link; angle brackets shown around the address. */
static void RenderEmailAddrLink(UBYTE *html,long *len,long maxlen,
   UBYTE *email_start,UBYTE *email_end,
   UBYTE *name_start,UBYTE *name_end)
{
   UBYTE escaped[512];
   UBYTE email[256];
   long emaillen;
   long namelen;

   if(!email_start || !email_end || email_end<=email_start) return;
   if(*len>=maxlen-80) return;
   emaillen=email_end-email_start;
   if(emaillen>(long)sizeof(email)-1) emaillen=(long)sizeof(email)-1;
   memcpy(email,email_start,emaillen);
   email[emaillen]='\0';
   if(name_start && name_end && name_end>name_start)
   {
      namelen=name_end-name_start;
      if(namelen>(long)sizeof(escaped)-1) namelen=(long)sizeof(escaped)-1;
      EscapeHtml(escaped,name_start,namelen,sizeof(escaped));
      *len+=sprintf(html+*len,"%s &lt;",escaped);
   }
   EscapeHtml(escaped,email_start,emaillen,sizeof(escaped));
   *len+=sprintf(html+*len,"<A HREF=\"mailto:%s\">%s</A>&gt;",email,escaped);
}

/* Parse header address fields; link the addr in &lt;...&gt; (or bare addr). */
static void RenderEmailAddress(UBYTE *field, long fieldlen, UBYTE *html, long *len, long maxlen)
{
   UBYTE *p;
   UBYTE *end;
   UBYTE *segend;
   UBYTE *email_start;
   UBYTE *email_end;
   UBYTE *name_start;
   UBYTE *name_end;
   UBYTE *angle;
   UBYTE *at_pos;
   UBYTE *q;
   UBYTE escaped[512];
   UBYTE *seg_start;
   BOOL first;

   if(!field || fieldlen<=0 || !html || !len || maxlen<=0) return;
   p=field;
   end=field+fieldlen;
   first=TRUE;
   while(p<end && *len<maxlen-100)
   {
      while(p<end && (*p==' ' || *p=='\t' || *p=='\r' || *p=='\n')) p++;
      if(p>=end) break;
      if(!first && *len+2<maxlen)
      {
         html[(*len)++]=',';
         html[(*len)++]=' ';
      }
      first=FALSE;
      seg_start=p;
      segend=EmlAddrSegEnd(p,end);
      name_start=NULL;
      name_end=NULL;
      email_start=NULL;
      email_end=NULL;
      if(*p=='"')
      {
         name_start=p+1;
         p++;
         while(p<segend && *p!='"') p++;
         if(p<segend) name_end=p++;
         while(p<segend && (*p==' ' || *p=='\t')) p++;
         if(p<segend && *p=='<')
         {
            email_start=p+1;
            p++;
            while(p<segend && *p!='>') p++;
            if(p<segend) email_end=p++;
         }
      }
      else
      {
         angle=p;
         while(angle<segend && *angle!='<') angle++;
         if(angle<segend && *angle=='<')
         {
            if(angle>p)
            {
               name_start=p;
               name_end=angle;
               while(name_end>name_start
               && (name_end[-1]==' ' || name_end[-1]=='\t')) name_end--;
            }
            email_start=angle+1;
            q=email_start;
            while(q<segend && *q!='>') q++;
            if(q<segend) email_end=q;
            p=segend;
         }
         else
         {
            at_pos=NULL;
            q=p;
            while(q<segend)
            {
               if(*q=='@') { at_pos=q; break; }
               q++;
            }
            if(at_pos)
            {
               email_start=p;
               q=at_pos;
               while(q>email_start
               && ((q[-1]>='a' && q[-1]<='z')
                  || (q[-1]>='A' && q[-1]<='Z')
                  || (q[-1]>='0' && q[-1]<='9')
                  || q[-1]=='.' || q[-1]=='_' || q[-1]=='+' || q[-1]=='-'))
               {  q--;
               }
               email_start=q;
               q=at_pos+1;
               while(q<segend
               && ((*q>='a' && *q<='z')
                  || (*q>='A' && *q<='Z')
                  || (*q>='0' && *q<='9')
                  || *q=='.' || *q=='_' || *q=='-' || *q=='@'))
               {  q++;
               }
               email_end=q;
               p=segend;
            }
            else
            {
               p=segend;
            }
         }
      }
      if(email_start && email_end && email_end>email_start)
      {
         RenderEmailAddrLink(html,len,maxlen,email_start,email_end,name_start,name_end);
      }
      else if(segend>seg_start)
      {
         EscapeHtml(escaped,seg_start,segend-seg_start,sizeof(escaped));
         *len+=sprintf(html+*len,"%s",escaped);
      }
      if(segend<end && (*segend==',' || *segend==';')) p=segend+1;
      else break;
   }
   html[*len]='\0';
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

/* Prefer largest HTML part (multipart/alternative may have tiny stub + full body). */
static struct EmailBodyPart *EmlFindHtmlBodyPart(struct EmailMessage *message)
{  struct EmailBodyPart *part;
   struct EmailBodyPart *best;
   long bestlen;

   if(!message) return NULL;
   best = NULL;
   bestlen = 0;
   if(message->html_part && message->html_part->data && message->html_part->datalen > 0)
   {
      best = message->html_part;
      bestlen = message->html_part->datalen;
      EMLDBG2("EmlFindHtmlBodyPart: candidate html_part len=%ld part=%lx",
         bestlen, (ULONG)best);
   }
   else if(message->html_part)
   {  EMLDBG2("EmlFindHtmlBodyPart: html_part has no data len=%ld part=%lx",
         message->html_part->datalen, (ULONG)message->html_part);
   }
   part = message->body_parts;
   while(part)
   {  if(part->data && part->datalen > bestlen)
      {  if(part->is_html
      || (part->content_type && strstr(part->content_type, "text/html")))
      {  best = part;
         bestlen = part->datalen;
         EMLDBG2("EmlFindHtmlBodyPart: candidate body_part len=%ld part=%lx",
            bestlen, (ULONG)best);
      }
      }
      part = part->next;
   }
   EMLDBG2("EmlFindHtmlBodyPart: selected part=%lx len=%ld",
      (ULONG)best, bestlen);
   return best;
}

static void EmlCleanContentIdBuf(UBYTE *cid, long cidlen, UBYTE *out, long outmax, long *outlen);

/* True when HTML references a specific Content-ID (after cid: prefix). */
static BOOL EmlHtmlReferencesCid(UBYTE *html, long htmllen, UBYTE *cid, long cidlen)
{
   UBYTE clean[256];
   UBYTE *p;
   UBYTE *end;
   UBYTE *q;
   long clen;
   UBYTE term;

   if(!html || htmllen <= 0 || !cid || cidlen <= 0) return FALSE;
   EmlCleanContentIdBuf(cid, cidlen, clean, sizeof(clean), &clen);
   if(clen <= 0) return FALSE;

   end = html + htmllen;
   p = html;
   while(p < end - 4)
   {
      if((p[0] == 'c' || p[0] == 'C')
      && (p[1] == 'i' || p[1] == 'I')
      && (p[2] == 'd' || p[2] == 'D')
      && p[3] == ':')
      {
         q = p + 4;
         if(q + clen <= end && !strnicmp(q, clean, clen))
         {
            term = '\0';
            if(q + clen < end) term = q[clen];
            if(term == '\0' || term == '"' || term == '\'' || term == '>'
            || term == '?' || term == ';' || term == ' ' || term == '\t'
            || term == '\r' || term == '\n')
            {  return TRUE;
            }
         }
      }
      p++;
   }
   return FALSE;
}

/* HTML already references a registered inline part (do not add a duplicate IMG). */
static BOOL EmlHtmlHasInlineCid(struct EmailMessage *message)
{
   struct EmailBodyPart *html;
   struct EmailAttachment *attach;

   html = EmlFindHtmlBodyPart(message);
   if(!html || !html->data || html->datalen <= 0) return FALSE;
   if(!strstr((char *)html->data, "cid:") && !strstr((char *)html->data, "CID:"))
   {  return FALSE;
   }
   if(!message->attachments) return TRUE;
   attach = message->attachments;
   while(attach)
   {
      if(attach->content_id && attach->content_idlen > 0
      && EmlHtmlReferencesCid(html->data, html->datalen,
         attach->content_id, attach->content_idlen))
      {  return TRUE;
      }
      attach = attach->next;
   }
   return FALSE;
}

static UBYTE *EmlMimeBaseType(UBYTE *content_type, long typelen);

/* Strip angle brackets from Content-ID for cid: matching. */
static void EmlCleanContentIdBuf(UBYTE *cid, long cidlen, UBYTE *out, long outmax, long *outlen)
{
   UBYTE *s;
   long len;

   if(outlen) *outlen = 0;
   if(!cid || cidlen <= 0 || !out || outmax <= 0) return;
   s = cid;
   len = cidlen;
   if(*s == '<')
   {  s++;
      len--;
   }
   if(len > 0 && s[len - 1] == '>')
   {  len--;
   }
   if(len >= outmax) len = outmax - 1;
   if(len > 0) memcpy(out, s, len);
   out[len] = '\0';
   if(outlen) *outlen = len;
}

/* True when p points at the start of an HTML tag name (after '<' or '</'). */
static BOOL EmlTagNameAt(UBYTE *p, UBYTE *end, UBYTE *name)
{
   long n;
   UBYTE c;

   if(!p || !end || !name || p >= end) return FALSE;
   n = (long)strlen((char *)name);
   if(p + n > end) return FALSE;
   if(strnicmp(p, name, n)) return FALSE;
   if(p + n >= end) return TRUE;
   c = p[n];
   if(c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\r' || c == '\n') return TRUE;
   return FALSE;
}

/* Skip past '>' of an opening tag. */
static UBYTE *EmlSkipToTagEnd(UBYTE *p, UBYTE *end)
{
   while(p < end && *p != '>') p++;
   if(p < end) p++;
   return p;
}

/* Find byte after closing tag or end if unclosed (avoids DPM_STYLE swallowing the body). */
static UBYTE *EmlFindCloseTag(UBYTE *p, UBYTE *end, UBYTE *name)
{
   long n;
   UBYTE *q;

   n = (long)strlen((char *)name);
   while(p < end)
   {
      if(p[0] == '<' && p + 2 + n < end && p[1] == '/'
      && !strnicmp(p + 2, name, n))
      {
         q = p + 2 + n;
         while(q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
         if(q < end && *q == '>') return q + 1;
      }
      p++;
   }
   return end;
}

/* Drop embedded <style>/<script> blocks; AWeb hides markup while in DPM_STYLE. */
static UBYTE *EmlSanitizeHtmlBody(UBYTE *data, long datalen, long *outlen)
{
   UBYTE *out;
   UBYTE *p;
   UBYTE *end;
   UBYTE *w;
   UBYTE *seg;
   UBYTE *tagstart;
   long copylen;

   if(outlen) *outlen = 0;
   if(!data || datalen <= 0) return NULL;
   out = (UBYTE *)AllocVec(datalen + 1, MEMF_CLEAR);
   if(!out) return NULL;
   p = data;
   end = data + datalen;
   w = out;
   while(p < end)
   {
      seg = p;
      while(p < end && *p != '<') p++;
      copylen = p - seg;
      if(copylen > 0)
      {
         memcpy(w, seg, copylen);
         w += copylen;
      }
      if(p >= end) break;
      tagstart = p;
      /* HTML comment <!-- ... --> */
      if(p + 4 <= end && p[1] == '!' && p[2] == '-' && p[3] == '-')
      {
         p += 4;
         while(p < end - 2)
         {
            if(p[0] == '-' && p[1] == '-' && p[2] == '>')
            {  p += 3;
               break;
            }
            p++;
         }
         continue;
      }
      /* <?xml ...?> processing instruction */
      if(p + 5 < end && !strnicmp(p, "<?xml", 5))
      {
         p = EmlSkipToTagEnd(tagstart, end);
         continue;
      }
      /* Closing tags </name>: strip the document-wrappers that would
       * prematurely close our enclosing <BODY>/<HTML>, drop the closers of
       * blocks we already stripped (style/script/head/title), and keep
       * everything else verbatim. Without this, e.g. </body></html> at the
       * end of a marketing email shuts the surrounding document and AWeb
       * shows a blank page. */
      if(p + 1 < end && p[1] == '/')
      {
         if(EmlTagNameAt(p + 2, end, (UBYTE *)"html")
         || EmlTagNameAt(p + 2, end, (UBYTE *)"body")
         || EmlTagNameAt(p + 2, end, (UBYTE *)"head")
         || EmlTagNameAt(p + 2, end, (UBYTE *)"style")
         || EmlTagNameAt(p + 2, end, (UBYTE *)"script")
         || EmlTagNameAt(p + 2, end, (UBYTE *)"title"))
         {
            p = EmlSkipToTagEnd(tagstart, end);
            continue;
         }
         *w++ = *p++;
         continue;
      }
      /* Block-form stripping: opening tag + everything up to matching close. */
      if(EmlTagNameAt(p + 1, end, (UBYTE *)"style")
      || EmlTagNameAt(p + 1, end, (UBYTE *)"script")
      || EmlTagNameAt(p + 1, end, (UBYTE *)"head"))
      {
         p = EmlSkipToTagEnd(tagstart, end);
         if(EmlTagNameAt(tagstart + 1, end, (UBYTE *)"style"))
         {  p = EmlFindCloseTag(p, end, (UBYTE *)"style");
         }
         else if(EmlTagNameAt(tagstart + 1, end, (UBYTE *)"script"))
         {  p = EmlFindCloseTag(p, end, (UBYTE *)"script");
         }
         else
         {  p = EmlFindCloseTag(p, end, (UBYTE *)"head");
         }
         continue;
      }
      /* Self-closing / metadata tags we want gone. */
      if(EmlTagNameAt(p + 1, end, (UBYTE *)"link")
      || EmlTagNameAt(p + 1, end, (UBYTE *)"meta")
      || EmlTagNameAt(p + 1, end, (UBYTE *)"title"))
      {
         p = EmlSkipToTagEnd(tagstart, end);
         if(EmlTagNameAt(tagstart + 1, end, (UBYTE *)"title"))
         {  p = EmlFindCloseTag(p, end, (UBYTE *)"title");
         }
         continue;
      }
      /* Document-wrapper opening tags: drop, keep their contents. */
      if(EmlTagNameAt(p + 1, end, (UBYTE *)"html")
      || EmlTagNameAt(p + 1, end, (UBYTE *)"body"))
      {
         p = EmlSkipToTagEnd(tagstart, end);
         continue;
      }
      *w++ = *p++;
   }
   if(outlen) *outlen = w - out;
   return out;
}

/* Render HTML body; keep cid: references (parts registered before this runs). */
static void EmlDumpHtmlSlice(UBYTE *label, UBYTE *data, long datalen, long maxbytes)
{
   UBYTE line[160];
   long i;
   long col;
   long shown;
   UBYTE hi;
   UBYTE lo;
   UBYTE c;

   if(!data || datalen <= 0) return;
   shown = (datalen < maxbytes) ? datalen : maxbytes;
   col = 0;
   for(i = 0; i < shown; i++)
   {
      if(col >= (long)sizeof(line) - 6)
      {  line[col] = '\0';
         EMLDBG2("%s: %s", label, line);
         col = 0;
      }
      c = data[i];
      if(c == '\r') { line[col++] = '\\'; line[col++] = 'r'; }
      else if(c == '\n') { line[col++] = '\\'; line[col++] = 'n'; }
      else if(c >= 32 && c < 127) { line[col++] = c; }
      else
      {  hi = (UBYTE)((c >> 4) & 0xF);
         lo = (UBYTE)(c & 0xF);
         line[col++] = '<';
         line[col++] = (UBYTE)(hi < 10 ? '0' + hi : 'A' + (hi - 10));
         line[col++] = (UBYTE)(lo < 10 ? '0' + lo : 'A' + (lo - 10));
         line[col++] = '>';
      }
   }
   if(col > 0)
   {  line[col] = '\0';
      EMLDBG2("%s: %s", label, line);
   }
}

static void EmlAppendHtmlBodyPart(struct EmlFilterData *fd, struct EmailBodyPart *part,
   struct EmailMessage *message)
{
   UBYTE *safe;
   long safelen;
   long mid;

   (void)message;
   if(!part || !part->data || part->datalen <= 0) return;
   safe = EmlSanitizeHtmlBody(part->data, part->datalen, &safelen);
   EMLDBG4("EmlAppendHtmlBodyPart: part=%lx rawlen=%ld safelen=%ld safe=%lx",
      (ULONG)part, part->datalen, safelen, (ULONG)safe);
   if(safe && safelen > 0)
   {  EmlDumpHtmlSlice((UBYTE *)"HtmlHead", safe, safelen, 256);
      if(safelen > 512)
      {  /* Mid samples make it possible to reconstruct what AWeb is being
          * asked to render without dumping the entire body. Use 1/4, 2/4,
          * and 3/4 offsets so we catch transitions in the marketing HTML
          * that often appears in forwarded Gmail messages. */
         mid = safelen / 4;
         EmlDumpHtmlSlice((UBYTE *)"HtmlQ1", safe + mid, 256, 256);
         EmlDumpHtmlSlice((UBYTE *)"HtmlQ2", safe + (safelen / 2), 256, 256);
         EmlDumpHtmlSlice((UBYTE *)"HtmlQ3", safe + (3 * (safelen / 4)), 256, 256);
      }
      if(safelen > 256)
      {  EmlDumpHtmlSlice((UBYTE *)"HtmlTail", safe + safelen - 256, 256, 256);
      }
   }
   AppendHtmlStr(fd, "<DIV>\n");
   if(safe && safelen > 0)
   {  AppendHtmlRaw(fd, safe, safelen);
      FreeVec(safe);
   }
   else
   {  AppendHtmlRaw(fd, part->data, part->datalen);
      if(safe) FreeVec(safe);
   }
   AppendHtmlStr(fd, "</DIV>\n");
}

/* Plain-text fallback when no HTML. */
static struct EmailBodyPart *EmlFindTextBodyPart(struct EmailMessage *message)
{  struct EmailBodyPart *part;

   if(!message) return NULL;
   if(message->text_part && message->text_part->data && message->text_part->datalen > 0)
   {  EMLDBG2("EmlFindTextBodyPart: selected text_part len=%ld part=%lx",
         message->text_part->datalen, (ULONG)message->text_part);
      return message->text_part;
   }
   else if(message->text_part)
   {  EMLDBG2("EmlFindTextBodyPart: text_part has no data len=%ld part=%lx",
         message->text_part->datalen, (ULONG)message->text_part);
   }

   part = message->body_parts;
   while(part)
   {  if(part->data && part->datalen > 0)
      {  if(part->is_text)
         {  EMLDBG2("EmlFindTextBodyPart: selected body_part len=%ld part=%lx",
               part->datalen, (ULONG)part);
            return part;
         }
         if(part->content_type && strstr(part->content_type, "text/plain"))
         {  EMLDBG2("EmlFindTextBodyPart: selected content-type body_part len=%ld part=%lx",
               part->datalen, (ULONG)part);
            return part;
         }
      }
      part = part->next;
   }
   EMLDBG0("EmlFindTextBodyPart: no text part selected");
   return NULL;
}

/* Charset from HTML or text body part for rendered page (UTF-8 marketing mail). */
static struct EmailBodyPart *EmlCharsetSourcePart(struct EmailMessage *message)
{
   struct EmailBodyPart *part;

   part = EmlFindHtmlBodyPart(message);
   if(part && part->charset && part->charsetlen > 0) return part;
   part = EmlFindTextBodyPart(message);
   if(part && part->charset && part->charsetlen > 0) return part;
   return NULL;
}

static void EmlAppendCharsetMeta(UBYTE *html, long *len, long maxlen, struct EmailMessage *message)
{
   struct EmailBodyPart *part;
   long cslen;

   if(!html || !len || maxlen <= 0) return;
   part = EmlCharsetSourcePart(message);
   if(part && part->charset && part->charsetlen > 0)
   {
      cslen = part->charsetlen;
      if(cslen > 40) cslen = 40;
      if(*len + cslen + 64 < maxlen)
      {
         *len += sprintf(html + *len,
            "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=%.*s\">\n",
            (int)cslen, part->charset);
      }
   }
   else if(*len + 64 < maxlen)
   {
      *len += sprintf(html + *len,
         "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n");
   }
}

/* Base MIME type for CID registry (strip "; name=..." parameters). */
static UBYTE *EmlMimeBaseType(UBYTE *content_type, long typelen)
{  UBYTE *semicolon;
   UBYTE *clean;
   long cleanlen;

   if(!content_type || typelen <= 0) return (UBYTE *)"application/octet-stream";
   semicolon = (UBYTE *)strchr((char *)content_type, ';');
   if(!semicolon) return content_type;
   cleanlen = semicolon - content_type;
   if(cleanlen <= 0) return (UBYTE *)"application/octet-stream";
   clean = (UBYTE *)AllocVec(cleanlen + 1, MEMF_CLEAR);
   if(!clean) return (UBYTE *)"application/octet-stream";
   memcpy(clean, content_type, cleanlen);
   clean[cleanlen] = '\0';
   return clean;
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
      if(message->html_part->content_type)
      {  EMLDBG2("  html_part type=%s charset=%s",
            message->html_part->content_type,
            message->html_part->charset ? message->html_part->charset : (UBYTE *)"(none)");
      }
   }
   if(message->text_part)
   {  EMLDBG2("  text_part data=%lx len=%ld",
         (ULONG)message->text_part->data, message->text_part->datalen);
      if(message->text_part->content_type)
      {  EMLDBG2("  text_part type=%s charset=%s",
            message->text_part->content_type,
            message->text_part->charset ? message->text_part->charset : (UBYTE *)"(none)");
      }
   }
   n = 0;
   for(part = message->body_parts; part; part = part->next)
   {  EMLDBG4("  body_parts[%ld] part=%lx data=%lx len=%ld",
         n, (ULONG)part, (ULONG)part->data, part->datalen);
      if(part->content_type)
      {  EMLDBG3("  body_parts[%ld] type=%s html=%ld",
            n, part->content_type, (ULONG)part->is_html);
      }
      n++;
   }
   n = 0;
   for(attach = message->attachments; attach; attach = attach->next)
   {  EMLDBG4("  attach[%ld] obj=%lx data=%lx len=%ld",
         n, (ULONG)attach, (ULONG)attach->data, attach->datalen);
      if(attach->content_type)
      {  EMLDBG3("  attach[%ld] type=%s inline=%ld",
            n, attach->content_type, (ULONG)attach->is_inline);
      }
      n++;
   }
}

/* Trim seconds and timezone from a Date header for display. */
static void EmlTrimHeaderDate(UBYTE *s, long smax)
{
   UBYTE *p;
   UBYTE *end;
   long len;

   if(!s || smax<=0 || !*s) return;
   len=strlen((char *)s);
   if(len>=smax) len=smax-1;
   end=s+len;
   if(len>=5)
   {
      p=end-5;
      if((*p=='+' || *p=='-') && p[1]>='0' && p[4]>='0')
      {
         if(p>s && p[-1]==' ') p--;
         *p='\0';
         end=p;
         len=end-s;
      }
   }
   p=strrchr((char *)s,' ');
   if(p && p>s)
   {
      UBYTE *z;
      long alpha;
      z=p+1;
      alpha=0;
      while(*z)
      {
         if((*z>='A' && *z<='Z') || (*z>='a' && *z<='z')) alpha++;
         else alpha=0;
         z++;
      }
      if(alpha>=3 && alpha<=5 && !strchr((char *)p+1,':'))
      {  *p='\0';
      }
   }
   p=strrchr((char *)s,':');
   if(p && p>=s+2)
   {
      if(p[1]>='0' && p[1]<='9' && p[2]>='0' && p[2]<='9'
      && (!p[3] || p[3]==' '))
      {
         if(p[-1]>='0' && p[-2]==':') *p='\0';
      }
   }
}

/* Register decoded inline parts for cid: URL resolution (before HTML render). */
static void EmlRegisterCidParts(struct EmlFilterData *fd, struct EmailMessage *message)
{  struct EmailAttachment *attach;
   struct EmailBodyPart *part;
   BOOL is_image;
   BOOL reg_ok;
   UBYTE *content_id_clean;
   UBYTE *content_id_reg;
   long cid_len;
   UBYTE *content_type;
   UBYTE *content_type_alloc;

   if(!fd || !message || !fd->eml_url) return;

   if(message->attachments)
   {  attach = message->attachments;
      while(attach)
      {  if(attach->data && attach->datalen > 0)
         {  is_image = FALSE;
            if(attach->content_type && attach->content_typelen > 0
            && strstr(attach->content_type, "image/"))
            {  is_image = TRUE;
            }
            if((attach->is_inline || is_image)
            && attach->content_id && attach->content_idlen > 0)
            {  content_id_clean = attach->content_id;
               cid_len = attach->content_idlen;
               if(*content_id_clean == '<')
               {  content_id_clean++;
                  cid_len--;
               }
               if(cid_len > 0 && content_id_clean[cid_len - 1] == '>')
               {  cid_len--;
               }
               content_id_reg = NULL;
               if(cid_len > 0)
               {  content_id_reg = (UBYTE *)AllocVec(cid_len + 1, MEMF_CLEAR);
                  if(content_id_reg)
                  {  memcpy(content_id_reg, content_id_clean, cid_len);
                     content_id_reg[cid_len] = '\0';
                  }
               }
               content_type_alloc = NULL;
               if(attach->content_type && attach->content_typelen > 0)
               {  content_type = EmlMimeBaseType(attach->content_type,
                     attach->content_typelen);
                  if(content_type != attach->content_type)
                  {  content_type_alloc = content_type;
                  }
               }
               else
               {  content_type = (UBYTE *)"application/octet-stream";
               }
               reg_ok = FALSE;
               if(content_id_reg)
               {  reg_ok = Registercidpart(fd->eml_url, content_id_reg,
                     content_type, attach->data, attach->datalen);
               }
               /* Trace exactly what we registered, so the Cidurltask lookup
                * traces can be cross-referenced against (eml_url, id) keys.
                * reg=1 means the registry took a copy; reg=0 means either an
                * existing entry blocked us (reload of the same message) or
                * the registry rejected the payload. Either way the existing
                * registry data remains valid for subsequent cid: lookups. */
               EMLDBG6("EmlRegisterCidParts: attach=%lx eml_url=%s id=%s type=%s len=%ld reg=%ld",
                  (ULONG)attach,
                  fd->eml_url ? (char *)fd->eml_url : "(null)",
                  content_id_reg ? (char *)content_id_reg : "(empty)",
                  (char *)content_type,
                  attach->datalen,
                  (ULONG)reg_ok);
               if(reg_ok)
               {  EmlReleaseExecPayload(&attach->data, &attach->datalen);
                  fd->cid_registered = TRUE;
               }
               if(content_type_alloc) FreeVec(content_type_alloc);
               if(content_id_reg) FreeVec(content_id_reg);
            }
         }
         attach = attach->next;
      }
   }

   if(message->body_parts)
   {  part = message->body_parts;
      while(part)
      {  if(part->data && part->datalen > 0
         && part->content_id && part->content_idlen > 0)
         {  content_id_clean = part->content_id;
            cid_len = part->content_idlen;
            if(*content_id_clean == '<')
            {  content_id_clean++;
               cid_len--;
            }
            if(cid_len > 0 && content_id_clean[cid_len - 1] == '>')
            {  cid_len--;
            }
            content_id_reg = NULL;
            if(cid_len > 0)
            {  content_id_reg = (UBYTE *)AllocVec(cid_len + 1, MEMF_CLEAR);
               if(content_id_reg)
               {  memcpy(content_id_reg, content_id_clean, cid_len);
                  content_id_reg[cid_len] = '\0';
               }
            }
            content_type_alloc = NULL;
            if(part->content_type && part->content_typelen > 0)
            {  content_type = EmlMimeBaseType(part->content_type, part->content_typelen);
               if(content_type != part->content_type)
               {  content_type_alloc = content_type;
               }
            }
            else
            {  content_type = (UBYTE *)"application/octet-stream";
            }
            if(content_id_reg
            && Registercidpart(fd->eml_url, content_id_reg, content_type,
               part->data, part->datalen))
            {  EmlReleaseExecPayload(&part->data, &part->datalen);
               fd->cid_registered = TRUE;
            }
            if(content_type_alloc) FreeVec(content_type_alloc);
            if(content_id_reg) FreeVec(content_id_reg);
         }
         part = part->next;
      }
   }
}

/* Render email to HTML */
void RenderEmailToHtml(struct EmlFilterData *fd, void *handle)
{  struct EmailMessage *message;
   struct EmailHeader *header;
   UBYTE html[8192];
   UBYTE escaped[2048];
   UBYTE datebuf[96];
   long len;
   long dlen;
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
      
      len += sprintf(html + len, "</TITLE>\n");
      EmlAppendCharsetMeta(html, &len, sizeof(html), message);
      len += sprintf(html + len,
         "</HEAD>\n"
         "<BODY BGCOLOR=\"#FFFFFF\" TEXT=\"#000000\""
         " TOPMARGIN=0 LEFTMARGIN=0 MARGINWIDTH=0 MARGINHEIGHT=0>\n"
         "<TABLE BORDER=2 WIDTH=\"100%%\" BGCOLOR=\"#FFFFFF\""
         " BORDERCOLOR=\"#888888\" CELLPADDING=2 CELLSPACING=0>\n");

      if(header->subject && header->subjectlen > 0)
      {  EscapeHtml(escaped, header->subject, header->subjectlen, sizeof(escaped));
         len += sprintf(html + len,
            "<TR><TD><FONT SIZE=2><B>Subject:</B></FONT></TD>"
            "<TD><FONT SIZE=2><B>%s</B></FONT></TD></TR>\n", escaped);
      }
      else
      {  len += sprintf(html + len,
            "<TR><TD><FONT SIZE=2><B>Subject:</B></FONT></TD>"
            "<TD><FONT SIZE=2><B>(No Subject)</B></FONT></TD></TR>\n");
      }
      
      if(header->from && header->fromlen > 0)
      {  long len_before;
         len_before = len;
         len += sprintf(html + len, "<TR><TD><FONT SIZE=2><B>From:</B></FONT></TD><TD><FONT SIZE=2>");
         RenderEmailAddress(header->from, header->fromlen, html, &len, sizeof(html) - len);
         /* Fallback: if no email was rendered, just escape and display the text */
         if(len == len_before + (long)strlen("<TR><TD><FONT SIZE=2><B>From:</B></FONT></TD><TD><FONT SIZE=2>"))
         {  EscapeHtml(escaped, header->from, header->fromlen, sizeof(escaped));
            len += sprintf(html + len, "%s", escaped);
         }
         len += sprintf(html + len, "</FONT></TD></TR>\n");
      }
      
      if(header->to && header->tolen > 0)
      {  long len_before;
         len_before = len;
         len += sprintf(html + len, "<TR><TD><FONT SIZE=2><B>To:</B></FONT></TD><TD><FONT SIZE=2>");
         RenderEmailAddress(header->to, header->tolen, html, &len, sizeof(html) - len);
         /* Fallback: if no email was rendered, just escape and display the text */
         if(len == len_before + (long)strlen("<TR><TD><FONT SIZE=2><B>To:</B></FONT></TD><TD><FONT SIZE=2>"))
         {  EscapeHtml(escaped, header->to, header->tolen, sizeof(escaped));
            len += sprintf(html + len, "%s", escaped);
         }
         len += sprintf(html + len, "</FONT></TD></TR>\n");
      }
      
      if(header->cc && header->cclen > 0)
      {  long len_before;
         len_before = len;
         len += sprintf(html + len, "<TR><TD><FONT SIZE=2><B>Cc:</B></FONT></TD><TD><FONT SIZE=2>");
         RenderEmailAddress(header->cc, header->cclen, html, &len, sizeof(html) - len);
         /* Fallback: if no email was rendered, just escape and display the text */
         if(len == len_before + (long)strlen("<TR><TD><FONT SIZE=2><B>Cc:</B></FONT></TD><TD><FONT SIZE=2>"))
         {  EscapeHtml(escaped, header->cc, header->cclen, sizeof(escaped));
            len += sprintf(html + len, "%s", escaped);
         }
         len += sprintf(html + len, "</FONT></TD></TR>\n");
      }
      
      if(header->date && header->datelen > 0)
      {  dlen=header->datelen;
         if(dlen>=(long)sizeof(datebuf)-1) dlen=(long)sizeof(datebuf)-1;
         memcpy(datebuf,header->date,dlen);
         datebuf[dlen]='\0';
         EmlTrimHeaderDate(datebuf,sizeof(datebuf));
         EscapeHtml(escaped, datebuf, strlen((char *)datebuf), sizeof(escaped));
         len += sprintf(html + len,
            "<TR><TD><FONT SIZE=2><B>Date:</B></FONT></TD>"
            "<TD><FONT SIZE=2>%s</FONT></TD></TR>\n", escaped);
      }
      
      len += sprintf(html + len, "</TABLE>\n");
      
      AppendHtml(fd, html, len);
      fd->header_written = TRUE;
   }
   
   /* Register inline MIME before body HTML (cid: lookups use mail: referer). */
   Aprintf("EML: Starting CID registration, eml_url=%lx, already_registered=%ld\n",
      (ULONG)fd->eml_url, (ULONG)fd->cid_registered);
   if(fd->eml_url && !fd->cid_registered)
   {
      EmlRegisterCidParts(fd, message);
   }
   else if(!fd->eml_url)
   {
      Aprintf("EML: No eml_url - skipping CID registration\n");
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
   
   part = EmlFindHtmlBodyPart(message);
   if(part)
   {  Aprintf("EML: Rendering HTML body part: data=%lx, datalen=%ld\n",
         (ULONG)part->data, part->datalen);
      EmlAppendHtmlBodyPart(fd, part, message);
   }
   else
   {  part = EmlFindTextBodyPart(message);
      if(part)
      {  Aprintf("EML: Rendering text body part (no HTML part found)\n");
         RenderPlainTextPart(fd, part);
         Aprintf("EML: Text body rendered, datalen=%ld\n", part->datalen);
      }
      else
      {  Aprintf("EML: No HTML or text body part to render\n");
      }
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
         
         /* Render inline images in the email body (cid: needs only Content-ID after registry). */
         if(should_render_inline && is_image && !is_html_attach
         && !EmlHtmlHasInlineCid(message))
         {  BOOL wrote_image;
            BOOL use_data_url;
            UBYTE *base64_data;
            long base64_len;
            UBYTE *content_type_str;
            UBYTE *content_type_clean;
            UBYTE *semicolon;

            wrote_image = FALSE;
            use_data_url = FALSE;
            len = sprintf(html, "<DIV CLASS=\"email-body\">\n");

            if(attach->content_id && attach->content_idlen > 0)
            {  UBYTE *content_id_clean;
               long cid_len;

               content_id_clean = attach->content_id;
               cid_len = attach->content_idlen;
               if(*content_id_clean == '<')
               {  content_id_clean++;
                  cid_len--;
               }
               if(cid_len > 0 && content_id_clean[cid_len - 1] == '>')
               {  cid_len--;
               }
               if(cid_len > 0)
               {  Aprintf("EML: Processing inline image (cid:) for email body\n");
                  EscapeHtml(escaped, content_id_clean, cid_len, sizeof(escaped));
                  len += sprintf(html + len,
                     "<P><IMG SRC=\"cid:%s\" ALT=\"Inline Image\" STYLE=\"max-width: 100%%; height: auto;\"></P>\n",
                     escaped);
                  wrote_image = TRUE;
                  Aprintf("EML: Inline image HTML written (cid:), new len=%ld\n", len);
               }
               else
               {  use_data_url = TRUE;
               }
            }
            else
            {  use_data_url = TRUE;
            }

            /* data: URL only while Exec payload is still present (before CID release). */
            if(!wrote_image && use_data_url && attach->data && attach->datalen > 0)
            {  Aprintf("EML: Processing inline image (data:) for email body\n");
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
               wrote_image = TRUE;
            }

            if(wrote_image)
            {  len += sprintf(html + len, "</DIV>\n");
               AppendHtml(fd, html, len);
               Aprintf("EML: Inline image rendered in email body, total len=%ld\n", len);
            }
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

