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

/* Base64 character to value conversion */
static UBYTE Base64Char(UBYTE c)
{  if(c >= 'A' && c <= 'Z') return c - 'A';
   if(c >= 'a' && c <= 'z') return c - 'a' + 26;
   if(c >= '0' && c <= '9') return c - '0' + 52;
   if(c == '+') return 62;
   if(c == '/') return 63;
   return 64; /* Invalid */
}

/* Base64 encoding table */
static const UBYTE base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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
   
   /* Calculate output length: 4 bytes per 3 input bytes, plus padding */
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

/* Decode base64 encoded data */
UBYTE *DecodeBase64(UBYTE *input, long inputlen, long *outputlen)
{  UBYTE *output;
   UBYTE *out;
   UBYTE *in;
   UBYTE *end;
   UBYTE c1, c2, c3, c4;
   long len;
   
   if(!input || inputlen <= 0)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }
   
   /* Calculate output length (approximately 3/4 of input) */
   len = (inputlen * 3) / 4 + 1;
   output = (UBYTE *)AllocVec(len, MEMF_CLEAR);
   if(!output)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }
   
   out = output;
   in = input;
   end = input + inputlen;
   
   while(in < end - 3)
   {  /* Skip whitespace */
      while(in < end && (*in == ' ' || *in == '\t' || *in == '\r' || *in == '\n'))
      {  in++;
      }
      if(in >= end - 3) break;
      
      c1 = Base64Char(in[0]);
      c2 = Base64Char(in[1]);
      c3 = Base64Char(in[2]);
      c4 = Base64Char(in[3]);
      
      if(c1 >= 64 || c2 >= 64) break;
      
      *out++ = (c1 << 2) | (c2 >> 4);
      
      if(c3 >= 64)
      {  /* Padding found */
         break;
      }
      
      *out++ = ((c2 & 0x0F) << 4) | (c3 >> 2);
      
      if(c4 >= 64)
      {  /* Padding found */
         break;
      }
      
      *out++ = ((c3 & 0x03) << 6) | c4;
      
      in += 4;
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
   
   /* Output length is at most input length */
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
      {  if(in < end - 1)
         {  UBYTE c1, c2;
            c1 = *in++;
            if(c1 == '\r' || c1 == '\n')
            {  /* Soft line break, skip it */
               if(c1 == '\r' && in < end && *in == '\n') in++;
               continue;
            }
            if(in < end)
            {  c2 = *in++;
               if((c1 >= '0' && c1 <= '9') || (c1 >= 'A' && c1 <= 'F') || (c1 >= 'a' && c1 <= 'f'))
               {  if((c2 >= '0' && c2 <= '9') || (c2 >= 'A' && c2 <= 'F') || (c2 >= 'a' && c2 <= 'f'))
                  {  UBYTE val = 0;
                     if(c1 >= '0' && c1 <= '9') val = (c1 - '0') << 4;
                     else if(c1 >= 'A' && c1 <= 'F') val = (c1 - 'A' + 10) << 4;
                     else if(c1 >= 'a' && c1 <= 'f') val = (c1 - 'a' + 10) << 4;
                     
                     if(c2 >= '0' && c2 <= '9') val |= (c2 - '0');
                     else if(c2 >= 'A' && c2 <= 'F') val |= (c2 - 'A' + 10);
                     else if(c2 >= 'a' && c2 <= 'f') val |= (c2 - 'a' + 10);
                     
                     *out++ = val;
                     continue;
                  }
               }
            }
         }
         /* Invalid encoding, output as-is */
         *out++ = '=';
         if(in > input + 1) in -= 2;
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
   {  /* No encoding specified, return copy of data */
      result = (UBYTE *)AllocVec(datalen + 1, MEMF_CLEAR);
      if(result)
      {  memcpy(result, data, datalen);
         if(outputlen) *outputlen = datalen;
      }
      else
      {  if(outputlen) *outputlen = 0;
      }
      return result;
   }
   
   /* Convert encoding to lowercase for comparison */
   enc = (UBYTE *)AllocVec(encodinglen + 1, MEMF_CLEAR);
   if(!enc)
   {  if(outputlen) *outputlen = 0;
      return NULL;
   }
   
   for(i = 0; i < encodinglen; i++)
   {  enc[i] = tolower(encoding[i]);
   }
   enc[encodinglen] = '\0';
   
   EMLDBG2("DecodeContent enc=%s datalen=%ld", enc, datalen);
   if(strstr(enc, "base64"))
   {  result = DecodeBase64(data, datalen, outputlen);
   }
   else if(strstr(enc, "quoted-printable"))
   {  result = DecodeQuotedPrintable(data, datalen, outputlen);
   }
   else if(strstr(enc, "7bit") || strstr(enc, "8bit") || strstr(enc, "binary"))
   {  /* No decoding needed, return copy */
      result = (UBYTE *)AllocVec(datalen + 1, MEMF_CLEAR);
      if(result)
      {  memcpy(result, data, datalen);
         if(outputlen) *outputlen = datalen;
      }
      else
      {  if(outputlen) *outputlen = 0;
      }
   }
   else
   {  /* Unknown encoding, return copy */
      result = (UBYTE *)AllocVec(datalen + 1, MEMF_CLEAR);
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

