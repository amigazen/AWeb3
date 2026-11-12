/**********************************************************************
 *
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2026 amigazen project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the AWeb Public License as included in this
 * distribution.
 *
 **********************************************************************/

/* anubis.c - Solve Techaro Anubis "fast"/"slow" SHA-256 challenges without JS.
 *
 * Challenge page embeds JSON in <script id="anubis_challenge">. We look for a
 * nonce such that hex(sha256(randomData || decimal_nonce)) has `difficulty`
 * leading '0' hex digits, then navigate to pass-challenge (via clientpull).
 *
 * This is Anubis (techaro.lol), not Cloudflare — the wall HTML looks similar
 * to users ("Making sure you're not a bot!") but the cookies and paths differ.
 */

#include "aweb.h"
#include "anubis.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/*---- Compact SHA-256 (public-domain style, C89) ------------------------*/

typedef struct
{  ULONG state[8];
   ULONG bitlen_hi,bitlen_lo;
   UBYTE data[64];
   ULONG datalen;
} Sha256ctx;

static void Sha256_transform(Sha256ctx *ctx,UBYTE data[64])
{  static const ULONG k[64]={
      0x428a2f98UL,0x71374491UL,0xb5c0fbcfUL,0xe9b5dba5UL,0x3956c25bUL,0x59f111f1UL,0x923f82a4UL,0xab1c5ed5UL,
      0xd807aa98UL,0x12835b01UL,0x243185beUL,0x550c7dc3UL,0x72be5d74UL,0x80deb1feUL,0x9bdc06a7UL,0xc19bf174UL,
      0xe49b69c1UL,0xefbe4786UL,0x0fc19dc6UL,0x240ca1ccUL,0x2de92c6fUL,0x4a7484aaUL,0x5cb0a9dcUL,0x76f988daUL,
      0x983e5152UL,0xa831c66dUL,0xb00327c8UL,0xbf597fc7UL,0xc6e00bf3UL,0xd5a79147UL,0x06ca6351UL,0x14292967UL,
      0x27b70a85UL,0x2e1b2138UL,0x4d2c6dfcUL,0x53380d13UL,0x650a7354UL,0x766a0abbUL,0x81c2c92eUL,0x92722c85UL,
      0xa2bfe8a1UL,0xa81a664bUL,0xc24b8b70UL,0xc76c51a3UL,0xd192e819UL,0xd6990624UL,0xf40e3585UL,0x106aa070UL,
      0x19a4c116UL,0x1e376c08UL,0x2748774cUL,0x34b0bcb5UL,0x391c0cb3UL,0x4ed8aa4aUL,0x5b9cca4fUL,0x682e6ff3UL,
      0x748f82eeUL,0x78a5636fUL,0x84c87814UL,0x8cc70208UL,0x90befffaUL,0xa4506cebUL,0xbef9a3f7UL,0xc67178f2UL
   };
   ULONG a,b,c,d,e,f,g,h,i,j,t1,t2,m[64];

   for(i=0,j=0;i<16;i++,j+=4)
   {  m[i]=((ULONG)data[j]<<24)|((ULONG)data[j+1]<<16)|((ULONG)data[j+2]<<8)|((ULONG)data[j+3]);
   }
   for(;i<64;i++)
   {  ULONG s0,s1;
      s0=((m[i-15]>>7)|(m[i-15]<<25))^((m[i-15]>>18)|(m[i-15]<<14))^(m[i-15]>>3);
      s1=((m[i-2]>>17)|(m[i-2]<<15))^((m[i-2]>>19)|(m[i-2]<<13))^(m[i-2]>>10);
      m[i]=m[i-16]+s0+m[i-7]+s1;
   }

   a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
   e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];

   for(i=0;i<64;i++)
   {  ULONG S1,ch,S0,maj;
      S1=((e>>6)|(e<<26))^((e>>11)|(e<<21))^((e>>25)|(e<<7));
      ch=(e&f)^((~e)&g);
      t1=h+S1+ch+k[i]+m[i];
      S0=((a>>2)|(a<<30))^((a>>13)|(a<<19))^((a>>22)|(a<<10));
      maj=(a&b)^(a&c)^(b&c);
      t2=S0+maj;
      h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
   }

   ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
   ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static void Sha256_init(Sha256ctx *ctx)
{  ctx->datalen=0;
   ctx->bitlen_hi=0;
   ctx->bitlen_lo=0;
   ctx->state[0]=0x6a09e667UL;
   ctx->state[1]=0xbb67ae85UL;
   ctx->state[2]=0x3c6ef372UL;
   ctx->state[3]=0xa54ff53aUL;
   ctx->state[4]=0x510e527fUL;
   ctx->state[5]=0x9b05688cUL;
   ctx->state[6]=0x1f83d9abUL;
   ctx->state[7]=0x5be0cd19UL;
}

static void Sha256_update(Sha256ctx *ctx,UBYTE *data,ULONG len)
{  ULONG i;
   for(i=0;i<len;i++)
   {  ctx->data[ctx->datalen++]=data[i];
      if(ctx->datalen==64)
      {  Sha256_transform(ctx,ctx->data);
         /* bit length += 512 */
         ctx->bitlen_lo+=512UL;
         if(ctx->bitlen_lo<512UL) ctx->bitlen_hi++;
         ctx->datalen=0;
      }
   }
}

static void Sha256_final(Sha256ctx *ctx,UBYTE hash[32])
{  ULONG i;
   i=ctx->datalen;

   if(ctx->datalen<56)
   {  ctx->data[i++]=0x80;
      while(i<56) ctx->data[i++]=0x00;
   }
   else
   {  ctx->data[i++]=0x80;
      while(i<64) ctx->data[i++]=0x00;
      Sha256_transform(ctx,ctx->data);
      memset(ctx->data,0,56);
   }

   /* Append length in bits as 64-bit big-endian */
   ctx->bitlen_lo+=ctx->datalen*8UL;
   if(ctx->bitlen_lo<ctx->datalen*8UL) ctx->bitlen_hi++;
   ctx->data[63]=(UBYTE)(ctx->bitlen_lo);
   ctx->data[62]=(UBYTE)(ctx->bitlen_lo>>8);
   ctx->data[61]=(UBYTE)(ctx->bitlen_lo>>16);
   ctx->data[60]=(UBYTE)(ctx->bitlen_lo>>24);
   ctx->data[59]=(UBYTE)(ctx->bitlen_hi);
   ctx->data[58]=(UBYTE)(ctx->bitlen_hi>>8);
   ctx->data[57]=(UBYTE)(ctx->bitlen_hi>>16);
   ctx->data[56]=(UBYTE)(ctx->bitlen_hi>>24);
   Sha256_transform(ctx,ctx->data);

   for(i=0;i<4;i++)
   {  hash[i]   =(UBYTE)((ctx->state[0]>>(24-i*8))&0xff);
      hash[i+4] =(UBYTE)((ctx->state[1]>>(24-i*8))&0xff);
      hash[i+8] =(UBYTE)((ctx->state[2]>>(24-i*8))&0xff);
      hash[i+12]=(UBYTE)((ctx->state[3]>>(24-i*8))&0xff);
      hash[i+16]=(UBYTE)((ctx->state[4]>>(24-i*8))&0xff);
      hash[i+20]=(UBYTE)((ctx->state[5]>>(24-i*8))&0xff);
      hash[i+24]=(UBYTE)((ctx->state[6]>>(24-i*8))&0xff);
      hash[i+28]=(UBYTE)((ctx->state[7]>>(24-i*8))&0xff);
   }
}

static void Sha256_hex(UBYTE *data,ULONG len,UBYTE outhex[65])
{  static const char *hexd="0123456789abcdef";
   Sha256ctx ctx;
   UBYTE hash[32];
   ULONG i;
   Sha256_init(&ctx);
   Sha256_update(&ctx,data,len);
   Sha256_final(&ctx,hash);
   for(i=0;i<32;i++)
   {  outhex[i*2]=(UBYTE)hexd[(hash[i]>>4)&0xf];
      outhex[i*2+1]=(UBYTE)hexd[hash[i]&0xf];
   }
   outhex[64]='\0';
}

/*---- JSON / HTML helpers ----------------------------------------------*/

/* Find "key":"value" or "key":number inside a JSON object region. */
static BOOL Anubis_json_string(UBYTE *json,long len,UBYTE *key,UBYTE *out,long outsz)
{  UBYTE pattern[96];
   UBYTE *p,*end,*q;
   long klen,n;

   if(!json || len<=0 || !key || !out || outsz<2) return FALSE;
   klen=(long)strlen((char *)key);
   if(klen<=0 || klen>80) return FALSE;
   sprintf((char *)pattern,"\"%s\"",key);
   end=json+len;
   p=json;
   for(;;)
   {  q=p;
      while(q<end && *q!=pattern[0]) q++;
      if(q>=end) return FALSE;
      if((end-q)>=(klen+2) && STRNEQUAL(q,pattern,klen+2))
      {  p=q+klen+2;
         while(p<end && isspace(*p)) p++;
         if(p<end && *p==':')
         {  p++;
            while(p<end && isspace(*p)) p++;
            if(p<end && *p=='"')
            {  p++;
               n=0;
               while(p<end && *p!='"' && n<outsz-1)
               {  if(*p=='\\' && p+1<end) p++; /* skip simple escapes */
                  out[n++]=*p++;
               }
               out[n]='\0';
               return TRUE;
            }
         }
      }
      p=q+1;
   }
}

static BOOL Anubis_json_long(UBYTE *json,long len,UBYTE *key,long *out)
{  UBYTE pattern[96];
   UBYTE *p,*end,*q;
   long klen;
   long v;
   BOOL neg;

   if(!json || len<=0 || !key || !out) return FALSE;
   klen=(long)strlen((char *)key);
   if(klen<=0 || klen>80) return FALSE;
   sprintf((char *)pattern,"\"%s\"",key);
   end=json+len;
   p=json;
   for(;;)
   {  q=p;
      while(q<end && *q!=pattern[0]) q++;
      if(q>=end) return FALSE;
      if((end-q)>=(klen+2) && STRNEQUAL(q,pattern,klen+2))
      {  p=q+klen+2;
         while(p<end && isspace(*p)) p++;
         if(p<end && *p==':')
         {  p++;
            while(p<end && isspace(*p)) p++;
            neg=FALSE;
            if(p<end && *p=='-') { neg=TRUE; p++; }
            if(p<end && isdigit(*p))
            {  v=0;
               while(p<end && isdigit(*p))
               {  v=v*10+(*p-'0');
                  p++;
               }
               *out=neg?-v:v;
               return TRUE;
            }
         }
      }
      p=q+1;
   }
}

/* Locate the JSON object for script id="anubis_challenge". */
static BOOL Anubis_find_challenge_json(UBYTE *html,long htmllen,UBYTE **start,long *len)
{  UBYTE *p,*end,*obj,*close;
   long depth;

   if(!html || htmllen<=0 || !start || !len) return FALSE;
   end=html+htmllen;
   p=html;
   for(;;)
   {  while(p<end && !STRNIEQUAL(p,"id=",3))
      {  p++;
         if(p>=end) return FALSE;
      }
      if(p>=end) return FALSE;
      p+=3;
      while(p<end && isspace(*p)) p++;
      if(p<end && (*p=='"' || *p=='\''))
      {  UBYTE qch=*p++;
         if(p+16<=end && STRNIEQUAL(p,"anubis_challenge",16) && p[16]==qch)
         {  /* Find the JSON object after this script tag's '>' */
            while(p<end && *p!='>') p++;
            if(p>=end) return FALSE;
            p++;
            while(p<end && isspace(*p)) p++;
            if(p>=end || *p!='{') return FALSE;
            obj=p;
            depth=0;
            while(p<end)
            {  if(*p=='{') depth++;
               else if(*p=='}')
               {  depth--;
                  if(depth==0)
                  {  close=p+1;
                     *start=obj;
                     *len=(long)(close-obj);
                     return TRUE;
                  }
               }
               else if(*p=='"')
               {  p++;
                  while(p<end && *p!='"')
                  {  if(*p=='\\' && p+1<end) p++;
                     p++;
                  }
               }
               p++;
            }
            return FALSE;
         }
      }
      if(p<end) p++;
   }
}

static void Anubis_pct_encode_path(UBYTE *path,UBYTE *out,long outsz)
{  static const char *hexd="0123456789ABCDEF";
   long i,n;
   UBYTE c;

   if(!path || !out || outsz<4)
   {  if(out && outsz>0) out[0]='\0';
      return;
   }
   n=0;
   for(i=0;path[i] && n<outsz-4;i++)
   {  c=path[i];
      if((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9')
      || c=='-' || c=='_' || c=='.' || c=='~')
      {  out[n++]=c;
      }
      else
      {  out[n++]='%';
         out[n++]=hexd[(c>>4)&0xf];
         out[n++]=hexd[c&0xf];
      }
   }
   out[n]='\0';
}

/* Build redir for pass-challenge. Anubis accepts a same-origin path or a
 * full http(s) URL (see validateRedirect); browsers send location.href. */
static void Anubis_redir_from_url(UBYTE *pageurl,UBYTE *redir,long redirsz)
{  UBYTE *p;
   long n;

   if(!redir || redirsz<2) return;
   redir[0]='/';
   redir[1]='\0';
   if(!pageurl || !*pageurl) return;

   /* Prefer the absolute URL (matches Anubis JS window.location.href). */
   p=pageurl;
   if(STRNIEQUAL(p,"http://",7) || STRNIEQUAL(p,"https://",8))
   {  n=0;
      while(*p && *p!='#' && n<redirsz-1)
         redir[n++]=*p++;
      redir[n]='\0';
      if(redir[0]) return;
   }

   p=pageurl;
   while(*p && *p!=':') p++;
   if(!(p[0]==':' && p[1]=='/' && p[2]=='/')) return;
   p+=3;
   while(*p && *p!='/' && *p!='?' && *p!='#') p++;
   if(!*p || *p=='#') return;

   n=0;
   if(*p=='?')
   {  /* Host with query but no path — Anubis wants "/?..." */
      if(n<redirsz-1) redir[n++]='/';
   }
   while(*p && *p!='#' && n<redirsz-1)
   {  redir[n++]=*p++;
   }
   redir[n]='\0';
   if(!redir[0]) { redir[0]='/'; redir[1]='\0'; }
}

static BOOL Anubis_leading_zeros(UBYTE *hex,long difficulty)
{  long i;
   if(difficulty<=0) return TRUE;
   for(i=0;i<difficulty;i++)
   {  if(hex[i]!='0') return FALSE;
   }
   return TRUE;
}

/* Cap iterations so a high difficulty cannot hang a 68k forever.
 * Expected work is ~16^difficulty hashes; eab.abime.net uses difficulty 2. */
#define ANUBIS_MAX_NONCE  20000000L

UBYTE *Anubis_clientpull(UBYTE *html,long htmllen,UBYTE *pageurl)
{  UBYTE *json;
   long jsonlen;
   UBYTE method[32];
   UBYTE id[80];
   UBYTE randomData[320];
   UBYTE prefix[128];
   UBYTE redir[512];
   UBYTE redirenc[1024];
   UBYTE hashhex[65];
   UBYTE noncebuf[32];
   UBYTE *pull;
   long difficulty;
   long nonce;
   long maxnonce;
   long rdlen;
   long prefixlen;
   UBYTE *work;
   long worklen;

   if(!html || htmllen<=0) return NULL;
   /* Cheap reject: must look like Anubis before scanning (buffer may lack NUL). */
   {  UBYTE *hp=html;
      UBYTE *hend=html+htmllen;
      BOOL found=FALSE;
      while(hp+16<=hend)
      {  if(STRNIEQUAL(hp,"anubis_challenge",16)) { found=TRUE; break; }
         hp++;
      }
      if(!found) return NULL;
   }
   if(!Anubis_find_challenge_json(html,htmllen,&json,&jsonlen)) return NULL;

   method[0]='\0';
   if(!Anubis_json_string(json,jsonlen,(UBYTE *)"method",method,sizeof(method)))
      return NULL;
   /* JS PoW methods we can solve natively. Metarefresh uses HTTP Refresh. */
   if(!STRIEQUAL(method,"fast") && !STRIEQUAL(method,"slow"))
      return NULL;

   id[0]='\0';
   randomData[0]='\0';
   difficulty=0;
   if(!Anubis_json_string(json,jsonlen,(UBYTE *)"id",id,sizeof(id))) return NULL;
   if(!Anubis_json_string(json,jsonlen,(UBYTE *)"randomData",randomData,sizeof(randomData)))
      return NULL;
   if(!Anubis_json_long(json,jsonlen,(UBYTE *)"difficulty",&difficulty)) return NULL;
   if(difficulty<0) difficulty=0;
   if(difficulty>8) difficulty=8; /* keep Amiga responsive */

   prefix[0]='\0';
   /* Optional base prefix from a sibling script; usually empty. */
   {  UBYTE *bp,*bend;
      bend=html+htmllen;
      bp=html;
      while(bp<bend)
      {  UBYTE *q=bp;
         while(q<bend && !STRNIEQUAL(q,"anubis_base_prefix",18)) q++;
         if(q>=bend) break;
         bp=q+18;
         while(bp<bend && *bp!='>') bp++;
         if(bp>=bend) break;
         bp++;
         while(bp<bend && isspace(*bp)) bp++;
         if(bp<bend && *bp=='"')
         {  long n=0;
            bp++;
            while(bp<bend && *bp!='"' && n<(long)sizeof(prefix)-1)
               prefix[n++]=*bp++;
            prefix[n]='\0';
         }
         break;
      }
   }

   Anubis_redir_from_url(pageurl,redir,sizeof(redir));
   Anubis_pct_encode_path(redir,redirenc,sizeof(redirenc));

   rdlen=(long)strlen((char *)randomData);
   if(rdlen<=0) return NULL;

   /* Average work is 16^difficulty hashes; budget ~2x expected for d<=5. */
   maxnonce=ANUBIS_MAX_NONCE;
   if(difficulty<=2) maxnonce=100000L;
   else if(difficulty==3) maxnonce=500000L;
   else if(difficulty==4) maxnonce=2000000L;
   else if(difficulty==5) maxnonce=5000000L;
   else if(difficulty==6) maxnonce=20000000L;

   AwebLog("anubis","Solving %s challenge id=%s difficulty=%ld",
      method,id,difficulty);

   work=ALLOCTYPE(UBYTE,rdlen+32,MEMF_PUBLIC);
   if(!work) return NULL;
   memcpy(work,randomData,rdlen);

   for(nonce=0;nonce<=maxnonce;nonce++)
   {  sprintf((char *)noncebuf,"%ld",nonce);
      worklen=rdlen+(long)strlen((char *)noncebuf);
      memcpy(work+rdlen,noncebuf,worklen-rdlen);
      Sha256_hex(work,(ULONG)worklen,hashhex);
      if(Anubis_leading_zeros(hashhex,difficulty))
         break;
   }
   FREE(work);
   if(nonce>maxnonce)
   {  AwebLog("anubis","Gave up after %ld nonces (difficulty %ld)",maxnonce,difficulty);
      return NULL;
   }

   prefixlen=(long)strlen((char *)prefix);
   /* "0; URL=" + prefix + path + query — leave room for a long absolute redir. */
   pull=ALLOCTYPE(UBYTE,prefixlen+(long)strlen((char *)redirenc)+640,MEMF_PUBLIC|MEMF_CLEAR);
   if(!pull) return NULL;
   sprintf((char *)pull,
      "0; URL=%s/.within.website/x/cmd/anubis/api/pass-challenge"
      "?id=%s&response=%s&nonce=%ld&redir=%s&elapsedTime=100",
      prefix[0]? (char *)prefix : "",
      id,hashhex,nonce,redirenc);

   AwebLog("anubis","Solved nonce=%ld pull=%s",nonce,pull);
   return pull;
}
