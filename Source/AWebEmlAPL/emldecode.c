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

/* emldecode.c - Base64 and quoted-printable decoding */

#include "eml.h"
#include <exec/memory.h>
#include <exec/types.h>
#include <string.h>
#include <ctype.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <pragmas/awebplugin_pragmas.h>

/* Base64 character to value conversion (-1 invalid). */
static long Base64Char(UBYTE c)
{  if(c >= 'A' && c <= 'Z') return (long)(c - 'A');
   if(c >= 'a' && c <= 'z') return (long)(c - 'a' + 26);
   if(c >= '0' && c <= '9') return (long)(c - '0' + 52);
   if(c == '+') return 62L;
   if(c == '/') return 63L;
   return -1L;
}

/* Base64 encoding table */
static const UBYTE base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Trim trailing whitespace on a header value copy used for encoding names. */
static void EmlTrimEnc(UBYTE *enc)
{  long len;
   long i;
   UBYTE c;

   if(!enc) return;
   len = (long)strlen((char *)enc);
   while(len > 0)
   {  c = enc[len - 1];
      if(c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
      enc[len - 1] = '\0';
      len--;
   }
   for(i = 0; enc[i]; i++)
   {  enc[i] = (UBYTE)tolower((int)enc[i]);
   }
}

/* Encode binary data to base64 */
UBYTE *EncodeBase64(UBYTE *input, long inputlen, long *outputlen)
{  UBYTE *output;
   UBYTE *out;
   UBYTE *in;
   UBYTE *end;
   long len;
   UBYTE c1, c2, c3;
   
   if(!input || inputlen <= 0)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }
   
   len = ((inputlen + 2) / 3) * 4 + 1;
   output = (UBYTE *)AllocVec(len, MEMF_CLEAR);
   if(!output)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }
   
   out = output;
   in = input;
   end = input + inputlen;
   
   while(in < end)
   {  c1 = *in++;
      
      *out++ = base64_chars[c1 >> 2];
      
      if(in < end)
      {  c2 = *in++;
         *out++ = base64_chars[((c1 & 0x03) << 4) | (c2 >> 4)];
         
         if(in < end)
         {  c3 = *in++;
            *out++ = base64_chars[((c2 & 0x0F) << 2) | (c3 >> 6)];
            *out++ = base64_chars[c3 & 0x3F];
         }
         else
         {  *out++ = base64_chars[(c2 & 0x0F) << 2];
            *out++ = '=';
         }
      }
      else
      {  *out++ = base64_chars[(c1 & 0x03) << 4];
         *out++ = '=';
         *out++ = '=';
      }
   }
   
   *out = '\0';
   if(outputlen) *outputlen = out - output;
   return output;
}

/* Decode base64 (RFC 2045: ignore whitespace, handle = padding). */
UBYTE *DecodeBase64(UBYTE *input, long inputlen, long *outputlen)
{  UBYTE *output;
   UBYTE *out;
   long outcap;
   long i;
   long state;
   UBYTE quad[4];
   UBYTE c;
   long v;

   if(!input || inputlen <= 0)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }

   outcap = (inputlen * 3) / 4 + 4;
   output = (UBYTE *)AllocVec(outcap, MEMF_CLEAR);
   if(!output)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }

   out = output;
   state = 0;
   for(i = 0; i < inputlen; i++)
   {  c = input[i];
      if(c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
      if(c == '=')
      {
         if(state >= 2)
         {
            *out++ = (UBYTE)((quad[0] << 2) | (quad[1] >> 4));
            if(state >= 3)
            {
               *out++ = (UBYTE)(((quad[1] & 0x0F) << 4) | (quad[2] >> 2));
            }
         }
         break;
      }
      v = Base64Char(c);
      if(v < 0) continue;
      quad[state++] = (UBYTE)v;
      if(state == 4)
      {
         *out++ = (UBYTE)((quad[0] << 2) | (quad[1] >> 4));
         *out++ = (UBYTE)(((quad[1] & 0x0F) << 4) | (quad[2] >> 2));
         *out++ = (UBYTE)(((quad[2] & 0x03) << 6) | quad[3]);
         state = 0;
      }
   }
   if(state >= 2)
   {
      *out++ = (UBYTE)((quad[0] << 2) | (quad[1] >> 4));
      if(state >= 3)
      {
         *out++ = (UBYTE)(((quad[1] & 0x0F) << 4) | (quad[2] >> 2));
      }
   }

   if(outputlen) *outputlen = out - output;
   return output;
}

/* Decode quoted-printable encoded data */
UBYTE *DecodeQuotedPrintable(UBYTE *input, long inputlen, long *outputlen)
{  UBYTE *output;
   UBYTE *out;
   UBYTE *in;
   UBYTE *end;
   UBYTE c;
   long len;
   
   if(!input || inputlen <= 0)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }
   
   len = inputlen + 1;
   output = (UBYTE *)AllocVec(len, MEMF_CLEAR);
   if(!output)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }
   
   out = output;
   in = input;
   end = input + inputlen;
   
   while(in < end)
   {  c = *in++;

      if(c == '=')
      {  UBYTE c1;
         UBYTE c2;
         if(in >= end)
         {  *out++ = '=';
            continue;
         }
         c1 = *in++;
         if(c1 == '\r' || c1 == '\n')
         {  if(c1 == '\r' && in < end && *in == '\n') in++;
            continue;
         }
         if(in >= end)
         {  *out++ = '=';
            *out++ = c1;
            continue;
         }
         c2 = *in++;
         if(((c1 >= '0' && c1 <= '9') || (c1 >= 'A' && c1 <= 'F') || (c1 >= 'a' && c1 <= 'f'))
         && ((c2 >= '0' && c2 <= '9') || (c2 >= 'A' && c2 <= 'F') || (c2 >= 'a' && c2 <= 'f')))
         {  UBYTE val;
            val = 0;
            if(c1 >= '0' && c1 <= '9') val = (UBYTE)((c1 - '0') << 4);
            else if(c1 >= 'A' && c1 <= 'F') val = (UBYTE)((c1 - 'A' + 10) << 4);
            else val = (UBYTE)((c1 - 'a' + 10) << 4);
            if(c2 >= '0' && c2 <= '9') val |= (UBYTE)(c2 - '0');
            else if(c2 >= 'A' && c2 <= 'F') val |= (UBYTE)(c2 - 'A' + 10);
            else val |= (UBYTE)(c2 - 'a' + 10);
            *out++ = val;
            continue;
         }
         *out++ = '=';
         *out++ = c1;
         *out++ = c2;
         continue;
      }

      *out++ = c;
   }
   
   if(outputlen) *outputlen = out - output;
   return output;
}

/* Decode content based on transfer encoding */
UBYTE *DecodeContent(UBYTE *data, long datalen, UBYTE *encoding, long encodinglen, long *outputlen)
{  UBYTE *result;
   UBYTE *enc;
   long i;
   
   if(!data || datalen <= 0)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }
   
   if(!encoding || encodinglen <= 0)
   {  result = (UBYTE *)AllocVec(datalen + 1, MEMF_CLEAR);
      if(result)
      {  memcpy(result, data, datalen);
         if(outputlen) *outputlen = datalen;
      }
      else
      {  if(outputlen) *outputlen = 0;
      }
      return result;
   }
   
   enc = (UBYTE *)AllocVec(encodinglen + 1, MEMF_CLEAR);
   if(!enc)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }
   
   for(i = 0; i < encodinglen; i++)
   {  enc[i] = encoding[i];
   }
   enc[encodinglen] = '\0';
   EmlTrimEnc(enc);
   
   EMLDBG2("DecodeContent enc=%s datalen=%ld", enc, datalen);
   if(strstr((char *)enc, "base64"))
   {  result = DecodeBase64(data, datalen, outputlen);
   }
   else if(strstr((char *)enc, "quoted-printable"))
   {  result = DecodeQuotedPrintable(data, datalen, outputlen);
   }
   else if(strstr((char *)enc, "7bit") || strstr((char *)enc, "8bit") || strstr((char *)enc, "binary"))
   {  result = (UBYTE *)AllocVec(datalen + 1, MEMF_CLEAR);
      if(result)
      {  memcpy(result, data, datalen);
         if(outputlen) *outputlen = datalen;
      }
      else
      {  if(outputlen) *outputlen = 0;
      }
   }
   else
   {  result = (UBYTE *)AllocVec(datalen + 1, MEMF_CLEAR);
      if(result)
      {  memcpy(result, data, datalen);
         if(outputlen) *outputlen = datalen;
      }
      else
      {  if(outputlen) *outputlen = 0;
      }
   }
   
   FreeVec(enc);
   EMLDBG2("DecodeContent result=%lx outlen=%ld",
      (ULONG)result, outputlen ? *outputlen : 0);
   return result;
}
