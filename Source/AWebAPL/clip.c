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

/* clip.c - AWeb clipboard interface */

#include "aweb.h"
#include <libraries/iffparse.h>
#include <devices/clipboard.h>
#include <proto/iffparse.h>

#define ID_FTXT   MAKE_ID('F','T','X','T')
#define ID_CHRS   MAKE_ID('C','H','R','S')

/*---------------------------------------------------------------------*/


/*---------------------------------------------------------------------*/

void Clipcopy(UBYTE *text,long length)
{  
#ifndef DEMOVERSION
   struct IFFHandle *iff;
   UBYTE *p;
   for(p=text;p<text+length;p++)
   {  if(*p==0xa0) *p=' ';
   }
   iff=AllocIFF();
   if(iff)
   {  iff->iff_Stream=(ULONG)OpenClipboard(PRIMARY_CLIP);
      if(iff->iff_Stream)
      {  InitIFFasClip(iff);
         /* OpenIFF may fail; CloseIFF must still run to run IFFCMD_CLEANUP on
          * the clipboard stream (see iffparse.library/CloseIFF autodocs). */
         if(!OpenIFF(iff,IFFF_WRITE))
         {  if(PushChunk(iff,ID_FTXT,ID_FORM,IFFSIZE_UNKNOWN)) goto clipcopy_done;
            if(PushChunk(iff,ID_FTXT,ID_CHRS,length)) goto clipcopy_done;
            if(WriteChunkBytes(iff,text,length)!=length) goto clipcopy_done;
            if(PopChunk(iff)) goto clipcopy_done;
            if(PopChunk(iff)) goto clipcopy_done;
clipcopy_done:
            ;
         }
         CloseIFF(iff);
         CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
      }
      FreeIFF(iff);
   }
#endif
}

long Clippaste(UBYTE *buf,long length)
{  struct IFFHandle *iff;
   long l;
   LONG e;
   struct ContextNode *cn;
   long left;
   long r;
   long readerr;

   l=0;
   readerr=0;
#ifndef DEMOVERSION
   iff=AllocIFF();
   if(iff)
   {  iff->iff_Stream=(ULONG)OpenClipboard(PRIMARY_CLIP);
      if(iff->iff_Stream)
      {  InitIFFasClip(iff);
         if(!OpenIFF(iff,IFFF_READ))
         {  if(!StopChunk(iff,ID_FTXT,ID_CHRS))
            {  left=length;
               while(left>0 && !readerr)
               {  e=ParseIFF(iff,IFFPARSE_SCAN);
                  if(e==IFFERR_EOC) continue;
                  if(e) break;
                  cn=CurrentChunk(iff);
                  if(cn && cn->cn_Type==ID_FTXT && cn->cn_ID==ID_CHRS)
                  {  r=ReadChunkBytes(iff,buf+length-left,left);
                     if(r<0)
                     {  readerr=1;
                        break;
                     }
                     /* At end of CHRS, ReadChunkBytes can return 0; without a
                      * break, left never decreases and ParseIFF would spin. */
                     if(r==0) break;
                     left-=r;
                  }
               }
               if(!readerr)
               {  l=length-left;
               }
            }
         }
         CloseIFF(iff);
         CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
      }
      FreeIFF(iff);
   }
#endif
   return l;
}
