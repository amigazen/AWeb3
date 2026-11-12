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

/* ilbmsource.c
 *
 * Example AWeb plugin module - sourcedriver
 *
 * This file implements the sourcedriver class for the
 * ILBMExample plugin module.
 *
 * Processing of the source data is done by a subtask.
 * Incoming blocks of data are stored in a linked list,
 * and read by the subtask as needed.
 * The subtask parses the ILBM file and builds a standard
 * BitMap. After each row the colors read are mapped to
 * the screen's ColorMap.
 *
 */

#include "pluginlib.h"
#include "ilbmexample.h"
#include <string.h>
#include <libraries/awebplugin.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <datatypes/pictureclass.h>
#include <clib/awebplugin_protos.h>
#include <clib/exec_protos.h>
#include <clib/alib_protos.h>
#include <clib/graphics_protos.h>
#include <clib/utility_protos.h>
#include <pragmas/awebplugin_pragmas.h>
#include <pragmas/exec_sysbase_pragmas.h>
#include <pragmas/graphics_pragmas.h>
#include <pragmas/utility_pragmas.h>

/*--------------------------------------------------------------------*/
/* General data structures                                            */
/*--------------------------------------------------------------------*/

/* A struct Datablock holds one block of data. Once it has been read
 * from the list, the data can be accessed without semaphore protection
 * because it will never change (but the ln_Succ pointer will!). */
struct Datablock
{  struct Node node;
   UBYTE *data;                     /* The data */
   long length;                     /* Length of the data */
};

/* The object instance data for the source driver */
struct Ilbmsource
{  struct Sourcedriver sourcedriver;/* Or superclass object instance */
   struct Aobject *source;          /* The AOTP_SOURCE object we belong to */
   struct Aobject *task;            /* Our decoder task */
   struct BitMap *bitmap;           /* The bitmap for the image */
   long width,height;               /* Bitmap dimensions */
   long ready;                      /* Last complete row in bitmap */
   struct List data;                /* A linked list of data blocks */
   struct SignalSemaphore sema;     /* Protect the common data */
   USHORT flags;                    /* See below */
   ULONG colorrgb[256];             /* Left aligned RRGGBB00 values */
   UBYTE pen[256];                  /* Mapped to these pens */
   UBYTE allocated[256];            /* This pen is actually allocated */
   struct ColorMap *colormap;       /* The colormap to obtain pens from */
   struct SignalSemaphore *processsema;   /* Protect heavy processing */
};

/* Ilbmsource flags: */
#define ILBMSF_EOF         0x0001   /* EOF was received */
#define ILBMSF_DISPLAYED   0x0002   /* This image is being displayed */
#define ILBMSF_MEMORY      0x0004   /* Our owner knows our memory size */

/*--------------------------------------------------------------------*/
/* The decoder subtask                                                */
/*--------------------------------------------------------------------*/

/* This structure holds vital data for the decoding subprocess */
struct Decoder
{  struct Datablock *current;       /* The current datablock */
   long currentbyte;                /* The current byte in the current datablock */
   struct Ilbmsource *source;       /* Points back to our Ilbmsource */
   struct BitMap *bitmap;           /* Bitmap to be filled */
   long width,height,depth;         /* Bitmap dimensions */
   USHORT flags;                    /* See below */
   struct RastPort rp;              /* Temporary for pixelline8 functions */
   struct RastPort temprp;          /* Temporary for pixelline8 functions */
   UBYTE *chunky;                   /* Row of chunky pixels */
};

/* Decoder flags: */
#define DECOF_STOP         0x0001   /* The decoder process should stop */
#define DECOF_EOF          0x0002   /* No more data */
#define DECOF_BYTERUN1     0x0004   /* ByteRun1 compression */


/* Read the next byte. First check any waiting messages, if the task
 * should stop then set the DECOF_STOP flag.
 * If end of current block reached, skip to next block. If no more
 * blocks available, wait until a new block is added or eof is
 * reached. */
static UBYTE Readbyte(struct Decoder *decoder)
{  
   struct Taskmsg *tm;
   struct TagItem *tag,*tstate;
   struct Datablock *db;
   BOOL wait;
   UBYTE retval=0;

   for(;;)
   {  wait=FALSE;
      while(!(decoder->flags&DECOF_STOP) && (tm=Gettaskmsg()))
      {  if(tm->amsg && tm->amsg->method==AOM_SET)
         {  tstate=((struct Amset *)tm->amsg)->tags;
            while(tag=NextTagItem(&tstate))
            {  switch(tag->ti_Tag)
               {  case AOTSK_Stop:
                     if(tag->ti_Data) decoder->flags|=DECOF_STOP;
                     break;
                  case AOILBM_Data:
                     /* Ignore these now */
                     break;
               }
            }
         }
         Replytaskmsg(tm);
      }
      /* Only continue if we shouldn't stop */
      if(decoder->flags&DECOF_STOP) break;
      if(decoder->current)
      {  if(++decoder->currentbyte>=decoder->current->length)
         {  /* End of block reached */
            ObtainSemaphore(&decoder->source->sema);
            db=(struct Datablock *)decoder->current->ln_Succ;
            if(db->ln_Succ)
            {  decoder->current=db;
               decoder->currentbyte=0;
            }
            else if(decoder->source->flags&ILBMSF_EOF)
            {  decoder->flags|=DECOF_EOF;
            }
            else
            {  /* No more blocks; wait for next block */
               wait=TRUE;
            }
            ReleaseSemaphore(&decoder->source->sema);
         }
      }
      else
      {  /* No current block yet */
         ObtainSemaphore(&decoder->source->sema);
         db=(struct Datablock *)decoder->source->data.lh_Head;
         if(db->ln_Succ)
         {  decoder->current=db;
            decoder->currentbyte=0;
         }
         else if(decoder->source->flags&ILBMSF_EOF)
         {  decoder->flags|=DECOF_EOF;
         }
         else
         {  /* No block yet; wait for next block */
            wait=TRUE;
         }
         ReleaseSemaphore(&decoder->source->sema);
      }
      if(!wait)
      {  if(!(decoder->flags&DECOF_EOF))
         {  retval=decoder->current->data[decoder->currentbyte];
         }
         break;
      }
      Waittask(0);
   }
   return retval;
}

/* Read a number of bytes. Returns TRUE if ok, FALSE of EOF reached before
 * end of block, or task should stop. */
static BOOL Readblock(struct Decoder *decoder,UBYTE *block,long length)
{  
   while(length && !(decoder->flags&(DECOF_STOP|DECOF_EOF)))
   {  *block++=Readbyte(decoder);
      length--;
   }
   return (BOOL)!(decoder->flags&(DECOF_STOP|DECOF_EOF));
}

/* Read the bitmap header and allocate the bitmap */
static BOOL Readbmhd(struct Decoder *decoder)
{  
   ULONG length;
   struct BitMapHeader bmh={0};

   if(!Readblock(decoder,(UBYTE *)&length,4)) return FALSE;
   if(!Readblock(decoder,(UBYTE *)&bmh,length<sizeof(bmh)?length:sizeof(bmh))) return FALSE;
   while(length>sizeof(bmh))
   {  Readbyte(decoder);
      length--;
   }
   decoder->width=bmh.bmh_Width;
   decoder->height=bmh.bmh_Height;
   decoder->depth=bmh.bmh_Depth;
   if(bmh.bmh_Compression==cmpByteRun1) decoder->flags|=DECOF_BYTERUN1;

   /* Always allocate a bitmap of depth 8. Even if our source has less
    * planes, the colours may be remapped to pen numbers up to 255. */
   decoder->bitmap=AllocBitMap(bmh.bmh_Width,bmh.bmh_Height,8,BMF_CLEAR,NULL);
   if(!decoder->bitmap) return FALSE;
   if(!GetBitMapAttr(decoder->bitmap,BMA_FLAGS)&BMF_STANDARD) return FALSE;
   
   /* Save our bitmap and dimensions. */
   ObtainSemaphore(&decoder->source->sema);
   decoder->source->bitmap=decoder->bitmap;
   decoder->source->width=decoder->width;
   decoder->source->height=decoder->height;
   ReleaseSemaphore(&decoder->source->sema);

   return TRUE;
}

/* Read the color map */
static BOOL Readcmap(struct Decoder *decoder)
{  
   ULONG length;
   ULONG rgb=0;
   short i,max;

   if(!Readblock(decoder,(UBYTE *)&length,4)) return FALSE;
   max=1<<decoder->depth;
   rgb=0;
   for(i=0;i<max && 3*i<length;i++)
   {  if(!Readblock(decoder,(UBYTE *)&rgb,3)) return FALSE;
      decoder->source->colorrgb[i]=rgb;
   }
   while(length>3*i)
   {  Readbyte(decoder);
      length--;
   }
}

/* Read one pixel row for all planes */
static BOOL Readrow(struct Decoder *decoder,long row)
{  
   long d,w,width;
   UBYTE *p,r;
   BYTE c;

   /* Compute row width rounded up to multiple of WORDS */
   width=(decoder->width+15)&~15;
   for(d=0;d<decoder->depth;d++)
   {  p=decoder->bitmap->Planes[d]+row*decoder->bitmap->BytesPerRow;
      if(decoder->flags&DECOF_BYTERUN1)
      {  w=0;
         while(w<width)
         {  c=Readbyte(decoder);
            if(decoder->flags&DECOF_STOP) return FALSE;
            if(c>=0)
            {  c++;
               if(!Readblock(decoder,p,c)) return FALSE;
            }
            else if(c>-127)
            {  c=1-c;
               r=Readbyte(decoder);
               if(decoder->flags&DECOF_STOP) return FALSE;
               memset(p,r,c);
            }
            else
            {  c=0;
            }
            p+=c;
            w+=8*c;
         }
      }
      else
      {  if(!Readblock(decoder,p,width/8)) return FALSE;
      }
   }
   return TRUE;
}

/* Repeat the LSB of c across all 32 bits */
static ULONG Expandrgb(ULONG c)
{  
   c&=0xff;
   return (c<<24)|(c<<16)|(c<<8)|c;
}

/* Read the body, row by row. After each row, convert to a row of
 * chunky bytes; see what colors it needs; allocate colors as
 * necessary; map the pixels to their new pens; and convert back
 * to planar. Then signal our main task that another row is ready. */
static BOOL Readbody(struct Decoder *decoder)
{  
   long row,i;
   ULONG rgb;
   short pen,map;

   /* First skip the chunk length */
   for(i=0;i<4;i++) Readbyte(decoder);
   
   for(row=0;row<decoder->height;row++)
   {  
      /* If there is a process semaphore, obtain it now to prevent all
       * heavy decoding processes from stealing all CPU time.
       * Obtain and release it for each separate row so other processes
       * have a chance too.
       * This obtain can fail in case we are stopped. */
      if(decoder->source->processsema)
      {  if(!Obtaintasksemaphore(decoder->source->processsema)) return FALSE;
      }
      
      /* Read the next row of the image */
      if(!Readrow(decoder,row)) return FALSE;

      /* Convert to chunky pixels. Look up every pixel in our tables.
       * If this colour is already mapped to a pen, use it. Else
       * Obtain a new pen for this colour. After remapping, convert
       * back to planar. */
      ReadPixelLine8(&decoder->rp,0,row,decoder->width,decoder->chunky,
         &decoder->temprp);
      for(i=0;i<decoder->width;i++)
      {  pen=decoder->chunky[i];
         if(decoder->source->allocated[pen])
         {  map=decoder->source->pen[pen];
         }
         else
         {  rgb=decoder->source->colorrgb[pen];
            map=ObtainBestPen(decoder->source->colormap,
               Expandrgb(rgb>>24),Expandrgb(rgb>>16),Expandrgb(rgb>>8),
               OBP_Precision,PRECISION_IMAGE,
               TAG_END);
            decoder->source->pen[pen]=map;
            decoder->source->allocated[pen]=TRUE;
         }
         decoder->chunky[i]=map;
      }
      WritePixelLine8(&decoder->rp,0,row,decoder->width,decoder->chunky,
         &decoder->temprp);

      /* Release the process semaphore */
      if(decoder->source->processsema)
      {  ReleaseSemaphore(decoder->source->processsema);
      }

      /* Let our main task know that another row is ready. Use a
       * synchroneous update to give the main task time to actually
       * render the new line :) */
      Updatetaskattrs(AOILBM_Ready,row,TAG_END);
   }
   return TRUE;
}

/* Main subtask process. Checks for AOTSK_Stop are done in the
 * Readbyte() function that is heavily used throughout the process. */
static __saveds __asm void Decodetask(register __a0 struct Ilbmsource *is)
{  
   struct Decoder decoderdata={0},*decoder=&decoderdata;
   ULONG hdr[3];
   BOOL bodyfound=FALSE,error=FALSE;
   
   decoder->source=is;
   
   /* First, check if our file is an IFF ILBM file */
      /* (What?? "goto"? Yes, I think the use of goto is valid
          in structured programming to escape from a processing
          block in case of an error.
          See it as C's implementation of exceptions.) */
   if(!Readblock(decoder,(UBYTE *)hdr,12)) goto err;
   if(hdr[0]!=ID_FORM || hdr[2]!=ID_ILBM) goto err;

   /* Read chunks until we found the BODY chunk, EOF was found or
    * we are killed. */
   while(!bodyfound && !(decoder->flags&(DECOF_STOP|DECOF_EOF)))
   {  
      if(!Readblock(decoder,(UBYTE *)hdr,4)) goto err;
      switch(hdr[0])
      {  case ID_BMHD:
            if(!Readbmhd(decoder)) goto err;
            break;
         case ID_CMAP:
            if(!Readcmap(decoder)) goto err;
            break;
         case ID_BODY:
            /* If we have a body but no BMHD was read before,
             * consider it an error. */
            if(!decoder->bitmap) goto err;
            bodyfound=TRUE;
            break;
         default:
            if(!Readblock(decoder,(UBYTE *)hdr,4)) goto err;
            while(hdr[0] && !(decoder->flags&(DECOF_STOP|DECOF_EOF)))
            {  Readbyte(decoder);
               hdr[0]--;
            }
            break;
      }
   }
   if(!bodyfound) goto err;

   /* The colour mapping process needs
    * - a RastPort to access our BitMap
    * - a temporary RastPort plus BitMap for the PixelLine8 functions.
    * allocate these first, then read the BODY chunk and do the
    * actual processing. */
   InitRastPort(&decoder->rp);
   decoder->rp.BitMap=decoder->bitmap;
   InitRastPort(&decoder->temprp);
   if(decoder->temprp.BitMap=AllocBitMap(
      8*(((decoder->width+15)>>4)<<1),1,8,0,decoder->bitmap))
   {  if(decoder->chunky=AllocVec(((decoder->width+15)>>4)<<4,MEMF_PUBLIC))
      {  error=!Readbody(decoder);
         FreeVec(decoder->chunky);
      }
      FreeBitMap(decoder->temprp.BitMap);
   }
   if(error) goto err;
      return;

err:
   /* Some error has occurred. Let our main task know this. */
   Updatetaskattrs(AOILBM_Error,TRUE,TAG_END);
}

/*--------------------------------------------------------------------*/
/* Main task functions                                                */
/*--------------------------------------------------------------------*/

/* Save all our source data blocks in this AOTP_FILE object */
static void Savesource(struct Ilbmsource *is,struct Aobject *file)
{  
   struct Datablock *db;

   for(db=(struct Datablock *)is->data.lh_Head;
      db->ln_Succ;
      db=(struct Datablock *)db->ln_Succ)
   {  Asetattrs(file,
         AOFIL_Data,db->data,
         AOFIL_Datalength,db->length,
         TAG_END);
   }
}

/* Start the decoder task. Only start it if the screen is
 * currenty valid. */
static void Startdecoder(struct Ilbmsource *is)
{  
   if(Agetattr(Aweb(),AOAPP_Screenvalid))
   {  Agetattrs(Aweb(),
         AOAPP_Colormap,&is->colormap,
         AOAPP_Semaphore,&is->processsema,
         TAG_END);
      if(is->task=Anewobject(AOTP_TASK,
         AOTSK_Entry,Decodetask,
         AOTSK_Name,"ILBM example decoder",
         AOTSK_Userdata,is,
         AOBJ_Target,is,
         TAG_END))
      {  /* The task object is created but not yet running.
          * Start it now. */
         Asetattrs(is->task,AOTSK_Start,TRUE,TAG_END);
      }
   }
}

/* Notify our related Ilbmcopy objects that the bitmap has gone.
 * Delete the bitmap, and release all obtained pens. */
static void Releaseimage(struct Ilbmsource *is)
{  
   short i;

   /* Send an AOM_NOTIFY message containing an AOM_SET message to
    * our AOTP_COPY object. That will forward it to our related
    * Ilbmcopy objects. */
   Anotifyset(is->source,AOILBM_Bitmap,NULL,TAG_END);

   /* Dispose our bitmap */
   if(is->bitmap)
   {  FreeBitMap(is->bitmap);
      is->bitmap=NULL;
   }

   /* Release all obtained pens */
   for(i=0;i<256;i++)
   {  if(is->allocated[i])
      {  ReleasePen(is->colormap,is->pen[i]);
         is->allocated[i]=FALSE;
      }
   }
   is->colormap=NULL;

   /* Let our owner know that memory has been released */
   Asetattrs(is->source,AOSRC_Memory,0,TAG_END);
}

/* Delete all source data */
static void Releasedata(struct Ilbmsource *is)
{
   struct Datablock *db;

   /* Delete all datablocks */
   while(db=(struct Datablock *)RemHead(&is->data))
   {  if(db->data) FreeVec(db->data);
      FreeVec(db);
   }
}

/*--------------------------------------------------------------------*/
/* Plugin sourcedriver object dispatcher functions                    */
/*--------------------------------------------------------------------*/

/* Process the AOM_SET method. This function is also used to set
 * the attributes for the AOM_NEW method */
static ULONG Setsource(struct Ilbmsource *is,struct Amset *amset)
{  
   struct TagItem *tag,*tstate;

   /* Pass a new AOM_SET message to our superclass.
    * Note: we cannot use our amset message because it might actually
    * be an AOM_NEW message.
    */
   Amethodas(AOTP_SOURCEDRIVER,is,AOM_SET,amset->tags);

   /* Scan the taglist */
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            /* The AOTP_SOURCE object we belong to */
            is->source=(struct Aobject *)tag->ti_Data;
            break;

         case AOSDV_Savesource:
            /* A request to save our source in a file.
             * tag->ti_Data is the AOTP_FILE object to save the image
             * source into. */
            Savesource(is,(struct Aobject *)tag->ti_Data);
            break;

         case AOSDV_Displayed:
            if(tag->ti_Data)
            {  /* We are becoming displayed. Start our decoder task if
                * we have data but no bitmap yet, and task is not yet
                * running. */
               is->flags|=ILBMSF_DISPLAYED;
               if(is->data.lh_Head->ln_Succ && !is->bitmap && !is->task)
               {  Startdecoder(is);
               }
            }
            else
            {  /* We are not displayed anywhere now. Leave the bitmap
                * intact so we don't need to decode it again when we
                * are becoming displayed again. */
               is->flags&=~ILBMSF_DISPLAYED;
            }
            break;

         case AOAPP_Screenvalid:
            if(tag->ti_Data)
            {  /* The screen has become valid again.
                * If we have data, and we are displayed, and the decoder
                * task isn't running yet, then start the decoder task again. */
               if(is->data.lh_Head->ln_Succ && (is->flags&ILBMSF_DISPLAYED)
               && !is->task)
               {  Startdecoder(is);
               }
            }
            else
            {  /* The screen is about to become invalid.
                * Stop our decoder process, free the bitmap and
                * release all pens. */
               if(is->task)
               {  Adisposeobject(is->task);
                  is->task=NULL;
               }
               Releaseimage(is);
            }
            break;
      }
   }
   return 0;
}

/* Process the AOM_NEW method */
static struct Ilbmsource *Newsource(struct Amset *amset)
{  
   struct Ilbmsource *is;
   
   /* Allocate a new object instance */
   if(is=Allocobject(PluginBase->sourcedriver,sizeof(struct Ilbmsource),amset))
   {  
      /* Initialize our data */
      InitSemaphore(&is->sema);
      NewList(&is->data);
      
      /* Add ourselves to the AOREL_APP_USE_SCREEN relationship since we will
       * use screen resources (the ColorMap for obtaining colours) */
      Aaddchild(Aweb(),is,AOREL_APP_USE_SCREEN);
      
      /* Now set our attributes */
      Setsource(is,amset);
   }
   return is;
}

/* Process the AOM_GET method */
static ULONG Getsource(struct Ilbmsource *is,struct Amset *amset)
{  
   struct TagItem *tag,*tstate;
   
   /* First forward the message to our superclass */
   AmethodasA(AOTP_SOURCEDRIVER,is,amset);
   
   /* Scan the taglist. */
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            PUTATTR(tag,is->source);
            break;

         case AOSDV_Saveable:
            /* We can save ourselves, but only after we have reached EOF */
            PUTATTR(tag,(is->flags&ILBMSF_EOF)?TRUE:FALSE);
            break;
      }
   }
   return 0;
}

/* Process the AOM_SRCUPDATE method. */
static ULONG Srcupdatesource(struct Ilbmsource *is,struct Amsrcupdate *amsrcupdate)
{  
   struct TagItem *tag,*tstate;
   UBYTE *data=NULL;
   long datalength=0;
   BOOL eof=FALSE;
   
   /* First forward the message to our superclass */
   AmethodasA(AOTP_SOURCEDRIVER,is,amsrcupdate);
   
   /* Scan the taglist */
   tstate=amsrcupdate->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOURL_Data:
            data=(UBYTE *)tag->ti_Data;
            break;
         case AOURL_Datalength:
            datalength=tag->ti_Data;
            break;
         case AOURL_Eof:
            if(tag->ti_Data)
            {  eof=TRUE;
               is->flags|=ILBMSF_EOF;
            }
            break;
      }
   }

   /* If we have new data, add it to our data list.
    * Then start our decoder if it isn't running yet. */
   if(data && datalength)
   {  struct Datablock *db;
      if(db=AllocVec(sizeof(struct Datablock),MEMF_PUBLIC|MEMF_CLEAR))
      {  if(db->data=AllocVec(datalength,MEMF_PUBLIC))
         {  memmove(db->data,data,datalength);
            db->length=datalength;
            ObtainSemaphore(&is->sema);
            AddTail(&is->data,db);
            ReleaseSemaphore(&is->sema);
         }
         else
         {  FreeVec(db);
         }
      }
      if(!is->task)
      {  Startdecoder(is);
      }
   }

   /* If we have new data, or reached EOF, let our decoder task know. */
   if((data && datalength) || eof)
   {  if(is->task)
      {  Asetattrsasync(is->task,AOILBM_Data,TRUE,TAG_END);
      }
   }
   return 0;
}

/* Process the AOM_UPDATE method. This is a message from the decoder task. */
static ULONG Updatesource(struct Ilbmsource *is,struct Amset *amset)
{
   struct TagItem *tag,*tstate;

   /* No need to forward this message to our superclass object
    * since it doesn't support this method. */

      /* Scan the taglist */
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOILBM_Ready:
            /* Another row has been completed. Let our copydriver objects
             * know. */

            is->ready=tag->ti_Data;
            Anotifyset(is->source,
               AOILBM_Bitmap,is->bitmap,
               AOILBM_Width,is->width,
               AOILBM_Height,is->height,
               AOILBM_Ready,is->ready,
               TAG_END);
            
            /* If not told already, let our owner object know how much
             * memory we (roughly) occupy: width*height*depth/8,
             * where depth==8 */
            Asetattrs(is->source,
               AOSRC_Memory,is->width*is->height,
               TAG_END);
            break;

         case AOILBM_Error:
            /* Ignore */
            break;

      }
   }
   
   return 0;
}

/* Process the AOM_ADDCHILD method. If the relation is AOREL_SRC_COPY,
 * a new copydriver was added. If we have a bitmap, let the new
 * child object know. */
static ULONG Addchildsource(struct Ilbmsource *is,struct Amadd *amadd)
{  
   if(amadd->relation==AOREL_SRC_COPY)
   {  if(is->bitmap)
      {  Asetattrs(amadd->child,
            AOILBM_Bitmap,is->bitmap,
            AOILBM_Width,is->width,
            AOILBM_Height,is->height,
            AOILBM_Ready,is->ready,
            TAG_END);
      }
   }
   return 0;
}

/* Process the AOM_DISPOSE method */
static void Disposesource(struct Ilbmsource *is)
{  
   /* If a decoder task is still running, stop it */
   if(is->task)
   {  Adisposeobject(is->task);
   }
   
   /* Delete our image */
   Releaseimage(is);

   /* Delete our source data */
   Releasedata(is);

   /* Remove ourselves from the "use screen" relation */
   Aremchild(Aweb(),is,AOREL_APP_USE_SCREEN);

   /* Pass method to our superclass which will free the object instance
    * memory */
   Amethodas(AOTP_SOURCEDRIVER,is,AOM_DISPOSE);
}

/* The main dispatcher */
__saveds __asm ULONG Dispatchsource(
   register __a0 struct Ilbmsource *is,
   register __a1 struct Amessage *amsg)
{  
   ULONG result=0;

   switch(amsg->method)
   {  case AOM_NEW:
         result=(ULONG)Newsource((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setsource(is,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getsource(is,(struct Amset *)amsg);
         break;
      case AOM_SRCUPDATE:
         result=Srcupdatesource(is,(struct Amsrcupdate *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatesource(is,(struct Amset *)amsg);
         break;
      case AOM_ADDCHILD:
         result=Addchildsource(is,(struct Amadd *)amsg);
         break;
      case AOM_DISPOSE:
         Disposesource(is);
         break;
      default:
         /* Forward any method not understood by us to our superclass
          * object */
         result=AmethodasA(AOTP_SOURCEDRIVER,is,amsg);
         break;
   }
   return result;
}

