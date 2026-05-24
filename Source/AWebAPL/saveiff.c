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

/* saveiff.c - AWeb save as IFF */

#include "aweb.h"
#include "window.h"
#include "winhis.h"
#include "url.h"
#include "application.h"
#include "printwin.h"
#include "arexx.h"
#include "task.h"
#include "filereq.h"
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/soundclass.h>
#include <devices/audio.h>
#include <devices/clipboard.h>
#include <devices/printer.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/stdio.h>
#include <dos/rdargs.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/semaphores.h>
#include <graphics/gfxmacros.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/displayinfo.h>
#include <graphics/scale.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <intuition/imageclass.h>
#include <intuition/pointerclass.h>
#include <intuition/sghooks.h>
#include <intuition/intuition.h>
#include <gadgets/colorwheel.h>
#include <gadgets/gradientslider.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <libraries/iffparse.h>
#include <libraries/locale.h>
#include <rexx/rxslib.h>
#include <rexx/storage.h>
#include <utility/date.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <libraries/Picasso96.h>
#include <reaction/reaction.h>
#include <reaction/reaction_author.h>

#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/iffparse.h>
#include <proto/icon.h>
#include <proto/Picasso96.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>

#include <ezlists.h>

#include <proto/alib.h>
#include <proto/asl.h>
#include <proto/colorwheel.h>
#include <proto/datatypes.h>
#include <proto/diskfont.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/iffparse.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/layers.h>
#include <proto/locale.h>
#include <proto/timer.h>
#include <proto/utility.h>
#include <proto/wb.h>
#include <proto/Picasso96.h>


/*------------------------------------------------------------------------*/

#ifndef NETDEMO

#define IFFWINH   128
#define IOBUFLEN  8192

struct Saveiff
{  struct Aobject object;
   void *freq;
   UBYTE *name;
   void *prwin;
   void *whis;
   long w,h,totalh;        /* window dimensions */
   struct ViewPort *vp;
   struct RastPort *rp;
   short type;
   USHORT flags;
   void *task;
   long fh;
   struct IFFHandle *iff;
   long y;                 /* current y */
   UBYTE *rbuf;            /* bitmap read buffer */
   UBYTE *wbuf;            /* iff write buffer */
   struct RastPort temprp; /* temp rp for 8-bit cybergfx */
   long w16;               /* width rounded up to multiple of 16 */
   UBYTE *iobuf;
   long iolength;
   void *progressreq;
   struct Arexxcmd *wait;  /* Reply this when finished */
};

#define SIFT_STANDARD   1
#define SIFT_CYBER8     2
#define SIFT_CYBERDEEP  3

#define SIFF_P96        0x0001   /* Picasso96 library opened */
#define SIFF_NOICON     0x0002   /* Don't save icon */

#define AOSIF_Dummy        AOBJ_DUMMYTAG(AOTP_SAVEIFF)

#define AOSIF_Name         (AOSIF_Dummy+1)
   /* (UBYTE *) Name to save under */

#define AOSIF_Go           (AOSIF_Dummy+2)
   /* (BOOL) Start the decoding process */

#define AOSIF_Wait         (AOSIF_Dummy+3)
   /* (struct Arexxcmd *) To reply when finished */

#define AOSIF_Noicon       (AOSIF_Dummy+4)
   /* (BOOL) Don't save icons */

/* From task to main: */
#define AOSIF_Next         (AOSIF_Dummy+128)
   /* (BOOL) Prepare next strip */

#define AOSIF_Ready        (AOSIF_Dummy+129)
   /* (BOOL) Task is ready */



/*------------------------------------------------------------------------*/

static BOOL Closeio(struct Saveiff *sif)
{  BOOL ok=TRUE;
   if(sif->iolength)
   {  ok=(WriteChunkBytes(sif->iff,sif->iobuf,sif->iolength)==sif->iolength);
   }
   return ok;
}

static BOOL Saveio(struct Saveiff *sif,UBYTE *buffer,long len)
{  BOOL ok=TRUE;
   while(ok && sif->iolength+len>IOBUFLEN)
   {  memmove(sif->iobuf+sif->iolength,buffer,IOBUFLEN-sif->iolength);
      ok=(WriteChunkBytes(sif->iff,sif->iobuf,IOBUFLEN)==IOBUFLEN);
      buffer+=IOBUFLEN-sif->iolength;
      len-=IOBUFLEN-sif->iolength;
      sif->iolength=0;
   }
   if(ok && len)
   {  memmove(sif->iobuf+sif->iolength,buffer,len);
      sif->iolength+=len;
   }

   return ok;
}

static BOOL Compressio(struct Saveiff *sif,UBYTE *buffer,long len)
{  BOOL ok=TRUE;
   UBYTE *p,*start,*end;
   short n;
   BYTE nbyte;
   p=buffer;
   start=buffer;
   end=buffer+len;
   n=0;
   while(ok && p<end)
   {  if(p<end-3 && p[1]==p[0] && (p[2]==p[0] || p[2]==p[3]))
      {  /* compress this data; first write out uncompressed data */
         if(n>0)
         {  nbyte=n-1;
            if(!(ok=Saveio(sif,&nbyte,1))) break;
            if(!(ok=Saveio(sif,start,n))) break;
         }
         start=p;
         n=0;
         while(p<end && n<128 && *p==*start)
         {  p++;
            n++;
         }
         nbyte=1-n;
         if(!(ok=Saveio(sif,&nbyte,1))) break;
         if(!(ok=Saveio(sif,start,1))) break;
         start=p;
         n=0;
      }
      else
      {  p++;
         n++;
         if(n>=128 || p>=end)
         {  nbyte=n-1;
            if(!(ok=Saveio(sif,&nbyte,1))) break;
            if(!(ok=Saveio(sif,start,n))) break;
            n=0;
            start=p;
         }
      }
   }
   return ok;
}

static BOOL Savehdr(struct Saveiff *sif)
{  struct BitMapHeader bmh={0};
   bmh.bmh_Width=sif->w;
   bmh.bmh_Height=sif->totalh;
   switch(sif->type)
   {  case SIFT_STANDARD:
      case SIFT_CYBER8:
         bmh.bmh_Depth=GetBitMapAttr(sif->rp->BitMap,BMA_DEPTH);
         break;
      case SIFT_CYBERDEEP:
         bmh.bmh_Depth=24;
         break;
   }
   bmh.bmh_Compression=cmpByteRun1;
   bmh.bmh_XAspect=44;
   bmh.bmh_YAspect=44;
   bmh.bmh_PageWidth=sif->w;
   bmh.bmh_PageHeight=sif->totalh;
   if(PushChunk(sif->iff,ID_ILBM,ID_FORM,IFFSIZE_UNKNOWN)) return FALSE;
   if(PushChunk(sif->iff,ID_ILBM,ID_BMHD,sizeof(bmh))) return FALSE;
   if(WriteChunkBytes(sif->iff,&bmh,sizeof(bmh))!=sizeof(bmh)) return FALSE;
   if(PopChunk(sif->iff)) return FALSE;
   if(bmh.bmh_Depth<=8)
   {  long nrgb=3*(1<<bmh.bmh_Depth),i;
      ULONG *buffer;
      UBYTE *cmap;
      BOOL ok=FALSE;
      if(PushChunk(sif->iff,ID_ILBM,ID_CMAP,nrgb)) return FALSE;
      if(buffer=ALLOCTYPE(ULONG,nrgb,MEMF_PUBLIC))
      {  if(cmap=ALLOCTYPE(UBYTE,nrgb,MEMF_PUBLIC))
         {  GetRGB32(sif->vp->ColorMap,0,nrgb/3,buffer);
            for(i=0;i<nrgb;i++)
            {  cmap[i]=buffer[i]>>24;
            }
            ok=(WriteChunkBytes(sif->iff,cmap,nrgb)==nrgb);
            FREE(cmap);
         }
         FREE(buffer);
      }
      if(!ok) return FALSE;
      if(PopChunk(sif->iff)) return FALSE;
   }
   if(PushChunk(sif->iff,ID_ILBM,ID_BODY,IFFSIZE_UNKNOWN)) return FALSE;
   return TRUE;
}

static BOOL Savepart(struct Saveiff *sif)
{  long y;
   long plane;
   long rgb,bit,pixel;
   UBYTE rmask,wmask;
   UBYTE *rp,*wp;
   struct BitMap *bm=sif->rp->BitMap;
   BOOL ok=TRUE;
   Asetattrs(sif->prwin,AOBJ_Top,sif->y,TAG_END);
   for(y=0;ok && y<IFFWINH && sif->y<sif->totalh;y++,sif->y++)
   {  if(sif->type==SIFT_STANDARD)
      {  for(plane=0;plane<bm->Depth;plane++)
         {  wp=bm->Planes[plane]+y*bm->BytesPerRow;
            ok=Compressio(sif,wp,sif->w16/8);
         }
      }
      else if(sif->type==SIFT_CYBER8)
      {  ReadPixelLine8(sif->rp,0,y,sif->w,sif->rbuf,&sif->temprp);
         rmask=0x01;
         for(bit=0;ok && bit<8;bit++)
         {  memset(sif->wbuf,0,sif->w16/8);
            wp=sif->wbuf;
            wmask=0x80;
            rp=sif->rbuf;
            for(pixel=0;pixel<sif->w;pixel++)
            {  if(*rp&rmask) *wp|=wmask;
               rp++;
               wmask>>=1;
               if(!wmask)
               {  wp++;
                  wmask=0x80;
               }
            }
            ok=Compressio(sif,sif->wbuf,sif->w16/8);
            rmask<<=1;
         }
      }
      else if(sif->type==SIFT_CYBERDEEP)
      {  /* For deep color, read pixels one by one and convert to RGB */
         for(pixel=0;ok && pixel<sif->w;pixel++)
         {  ULONG color=p96ReadPixel(sif->rp,pixel,y);
            sif->rbuf[pixel*3]=(color>>16)&0xFF;     /* Red */
            sif->rbuf[pixel*3+1]=(color>>8)&0xFF;    /* Green */
            sif->rbuf[pixel*3+2]=color&0xFF;         /* Blue */
         }
         for(rgb=0;ok && rgb<3;rgb++)
         {  rmask=0x01;
            for(bit=0;ok && bit<8;bit++)
            {  memset(sif->wbuf,0,sif->w16/8);
               wp=sif->wbuf;
               wmask=0x80;
               rp=sif->rbuf+rgb;
               for(pixel=0;pixel<sif->w;pixel++)
               {  if(*rp&rmask) *wp|=wmask;
                  rp+=3;
                  wmask>>=1;
                  if(!wmask)
                  {  wp++;
                     wmask=0x80;
                  }
               }
               ok=Compressio(sif,sif->wbuf,sif->w16/8);
               rmask<<=1;
            }
         }
      }
   }
   return ok;
}

static void Saveifftask(struct Saveiff *sif)
{  if(!(sif->fh=Open(sif->name,MODE_NEWFILE))) goto err;
   if(!(sif->iff=AllocIFF())) goto err;
   sif->iff->iff_Stream=sif->fh;
   InitIFFasDOS(sif->iff);
   if(OpenIFF(sif->iff,IFFF_WRITE)) goto err;
   
   if(!Savehdr(sif)) goto err;
   
   while(sif->y<sif->totalh)
   {  if(!Savepart(sif)) goto err;
      if(Checktaskbreak()) goto err;
      Updatetaskattrs(AOSIF_Next,TRUE,TAG_END);
   }
   
   if(!Closeio(sif)) goto err;
   if(PopChunk(sif->iff)) goto err;  /* BODY */
   if(PopChunk(sif->iff)) goto err;  /* FORM */

err:
   if(sif->iff) CloseIFF(sif->iff);
   if(sif->fh) Close(sif->fh);
   if(sif->iff) FreeIFF(sif->iff);
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOSIF_Ready,TRUE,
      TAG_END);
}

/*------------------------------------------------------------------------*/

static BOOL Gosaveiff(struct Saveiff *sif)
{  BOOL ok=FALSE;
   if(sif->name)
   {  sif->progressreq=Openprogressreq(AWEBSTR(MSG_SAVEIFF_PROGRESS));
      sif->task=Anewobject(AOTP_TASK,
         AOTSK_Entry,Saveifftask,
         AOTSK_Name,"AWeb Save as IFF",
         AOTSK_Userdata,sif,
         AOBJ_Target,sif,
         TAG_END);
      if(sif->task)
      {  Asetattrs(sif->task,AOTSK_Start,TRUE,TAG_END);
         ok=TRUE;
      }
   }
   else
   {  UBYTE *path,*file,*name=NULL,*p;
      long l;
      path=(UBYTE *)Agetattr(Aweb(),AOAPP_Savepath);
      file=Urlfilename((UBYTE *)Agetattr((void *)Agetattr(sif->whis,AOWHS_Url),AOURL_Url));
      l=(path?strlen(path):0)+(file?strlen(file):0)+8;
      if(name=ALLOCTYPE(UBYTE,l,0))
      {  if(path) strcpy(name,path);
         if(file) AddPart(name,file,l);
         for(p=name+strlen(name)-1;p>name && !strchr("./:",*p);p--);
         if(p>name && *p=='.')
         {  strcpy(p,".iff");
         }
         else
         {  p=name+strlen(name)-1;
            if(p>name && *p!='/' && *p!=':')
            {  strcat(name,".iff");
            }
         }
      }
      sif->freq=Anewobject(AOTP_FILEREQ,
         AOFRQ_Title,AWEBSTR(MSG_FILE_SAVETITLE),
         AOFRQ_Filename,name,
         AOFRQ_Savemode,TRUE,
         AOFRQ_Savecheck,TRUE,
         AOBJ_Target,sif,
         TAG_END);
      if(sif->freq)
      {  ok=TRUE;
      }
      if(file) FREE(file);
      if(name) FREE(name);
   }
   return ok;
}

static long Setsaveiff(struct Saveiff *sif,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   void *win=NULL;
   BOOL go=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Window:
            win=(void *)tag->ti_Data;
            break;
         case AOSIF_Name:
            sif->name=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOSIF_Wait:
            sif->wait=(struct Arexxcmd *)tag->ti_Data;
            break;
         case AOSIF_Noicon:
            SETFLAG(sif->flags,SIFF_NOICON,tag->ti_Data);
            break;
         case AOSIF_Go:
            go=BOOLVAL(tag->ti_Data);
            break;
      }
   }
   if(win)
   {  void *whis=NULL;
      Agetattrs(win,
         AOWIN_Innerwidth,&sif->w,
         AOWIN_Innerheight,&sif->h,
         AOBJ_Winhis,&whis,
         TAG_END);
      if(whis)
      {  sif->whis=Anewobject(AOTP_WINHIS,
            AOWHS_Copyfrom,whis,
            AOWHS_Key,0,
            AOWHS_History,TRUE,
            TAG_END);
      }
      sif->prwin=Anewobject(AOTP_PRINTWINDOW,
         AOPRW_Width,sif->w,
         AOPRW_Height,IFFWINH,
         AOPRW_Layoutheight,sif->h,
         AOPRW_Turboprint,P96Base!=NULL,
         TAG_END);
      sif->rp=(struct RastPort *)Agetattr(sif->prwin,AOWIN_Rastport);
      Asetattrs(sif->prwin,
         AOPRW_Height,IFFWINH,
         AOBJ_Winhis,sif->whis,
         TAG_END);
      Asetattrs(sif->prwin,AOPRW_Update,TRUE,TAG_END);
      sif->totalh=Agetattr(sif->prwin,AOPRW_Totalheight);
   }
   if(go)
   {  if(!Gosaveiff(sif)) Adisposeobject(sif);
   }
   return 0;
}

static long Updatesaveiff(struct Saveiff *sif,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   BOOL go=FALSE,ready=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSIF_Next:
            Asetattrs(sif->prwin,AOBJ_Top,sif->y,TAG_END);
            if(sif->progressreq)
            {  if(Checkprogressreq(sif->progressreq))
               {  Asetattrs(sif->task,
                     AOTSK_Async,TRUE,
                     AOTSK_Stop,TRUE,
                     TAG_END);
               }
               else
               {  Setprogressreq(sif->progressreq,sif->y,sif->totalh);
               }
            }
            break;
         case AOSIF_Ready:
            ready=TRUE;
            break;
         case AOFRQ_Filename:
            sif->name=Dupstr((UBYTE *)tag->ti_Data,-1);
            sif->freq=NULL;
            if(sif->name) Asetattrs(Aweb(),AOAPP_Savepath,sif->name,TAG_END);
            go=TRUE;
            break;
      }
   }
   if(go)
   {  if(!sif->name || !Gosaveiff(sif))
      {  Adisposeobject(sif);
      }
   }
   if(ready)
   {  UBYTE *url=(UBYTE *)Agetattr((void *)Agetattr(sif->whis,AOWHS_Url),AOURL_Url);
      if(url)
      {  long clen=strlen(url);
         UBYTE *comment=Dupstr(url,MIN(79,clen));
         if(comment)
         {  SetComment(sif->name,comment);
            FREE(comment);
         }
      }
      if(!(sif->flags&SIFF_NOICON) && sif->name && prefs.program.saveicons)
      {  struct DiskObject *dob;
         /* Use GetDiskObjectNew() which queries DefIcons if running,
          * providing file-type-specific default icons */
         if(dob=GetDiskObjectNew(sif->name))
         {  PutDiskObject(sif->name,dob);
            FreeDiskObject(dob);
         }
      }
      Adisposeobject(sif);
   }
   return 0;
}

static struct Saveiff *Newsaveiff(struct Amset *ams)
{  struct Saveiff *sif;
   struct Screen *screen;
   if(sif=Allocobject(AOTP_SAVEIFF,sizeof(struct Saveiff),ams))
   {  Aaddchild(Aweb(),sif,AOREL_APP_USE_SCREEN);
      screen=(struct Screen *)Agetattr(Aweb(),AOAPP_Screen);
      if(!screen) goto err;
      if(P96Base=OpenLibrary("Picasso96API.library",0))
      {  sif->flags|=SIFF_P96;
      }
      Setsaveiff(sif,ams);
      sif->vp=&screen->ViewPort;
      sif->w16=(sif->w+15)&~15;
      InitRastPort(&sif->temprp);
      if(GetBitMapAttr(sif->rp->BitMap,BMA_FLAGS)&BMF_STANDARD)
      {  sif->type=SIFT_STANDARD;
      }
      else if(P96Base)
      {  if(p96GetBitMapAttr(sif->rp->BitMap,P96BMA_ISP96))
         {  if(p96GetBitMapAttr(sif->rp->BitMap,P96BMA_DEPTH)>8)
            {  sif->type=SIFT_CYBERDEEP;
               if(!(sif->rbuf=ALLOCTYPE(UBYTE,3*sif->w,0))) goto err;
               if(!(sif->wbuf=ALLOCTYPE(UBYTE,3*sif->w16,MEMF_CLEAR))) goto err;
            }
            else
            {  sif->type=SIFT_CYBER8;
               if(!(sif->temprp.BitMap=AllocBitMap(
                  8*(((sif->w+15)>>4)<<1),1,8,BMF_DISPLAYABLE,sif->rp->BitMap))) goto err;
               if(!(sif->rbuf=ALLOCTYPE(UBYTE,((sif->w+15)>>4)<<4,0))) goto err;
               if(!(sif->wbuf=ALLOCTYPE(UBYTE,sif->w16,MEMF_CLEAR))) goto err;
            }
         }
      }
      if(!(sif->iobuf=ALLOCTYPE(UBYTE,IOBUFLEN,0))) goto err;
   }
   return sif;

err:
   Adisposeobject(sif);
   return NULL;
}

static void Disposesaveiff(struct Saveiff *sif)
{  if(sif->task) Adisposeobject(sif->task);
   Aremchild(Aweb(),sif,AOREL_APP_USE_SCREEN);
   if(sif->progressreq) Closeprogressreq(sif->progressreq);
   if(sif->prwin) Adisposeobject(sif->prwin);
   if(sif->whis) Adisposeobject(sif->whis);
   if(sif->freq) Adisposeobject(sif->freq);
   if(sif->rbuf) FREE(sif->rbuf);
   if(sif->wbuf) FREE(sif->wbuf);
   if(sif->iobuf) FREE(sif->iobuf);
   if(sif->temprp.BitMap) FreeBitMap(sif->temprp.BitMap);
   if(sif->name) FREE(sif->name);
         if(sif->flags&SIFF_P96) CloseLibrary(P96Base);
   if(sif->wait) Replyarexxcmd(sif->wait);
   Amethodas(AOTP_OBJECT,sif,AOM_DISPOSE);
}

static long Dispatch(struct Saveiff *sif,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newsaveiff((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setsaveiff(sif,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatesaveiff(sif,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposesaveiff(sif);
         break;
      case AOM_DEINSTALL:
         break;
      default:
         result=AmethodasA(AOTP_OBJECT,sif,amsg);
   }
   return result;
}

#endif /* !NETDEMO */

/*------------------------------------------------------------------------*/

BOOL Installsaveiff(void)
{
#ifndef NETDEMO
   if(!(Amethod(NULL,AOM_INSTALL,AOTP_SAVEIFF,Dispatch))) return FALSE;
#endif
   return TRUE;
}

void Saveasiff(void *win,UBYTE *name,BOOL noicon,struct Arexxcmd *wait)
{
#ifndef NETDEMO
   void *sif;
   sif=Anewobject(AOTP_SAVEIFF,
      AOBJ_Window,win,
      AOSIF_Name,name,
      AOSIF_Noicon,noicon,
      AOSIF_Wait,wait,
      TAG_END);
   if(sif)
   {  Asetattrs(sif,AOSIF_Go,TRUE,TAG_END);
   }
   else
#endif
   if(wait)
   {  Replyarexxcmd(wait);
   }
}
