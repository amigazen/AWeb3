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

/* ilbmcopy.c
 *
 * Example AWeb plugin module - copydriver
 *
 * This file implements the copydriver class for the
 * ILBMExample plugin module.
 *
 * When the sourcedriver object let us know that another
 * row of the bitmap is ready, display it.
 *
 */

#include "pluginlib.h"
#include "ilbmexample.h"
#include <libraries/awebplugin.h>
#include <graphics/gfx.h>
#include <clib/awebplugin_protos.h>
#include <clib/graphics_protos.h>
#include <clib/utility_protos.h>
#include <pragmas/awebplugin_pragmas.h>
#include <pragmas/graphics_pragmas.h>
#include <pragmas/utility_pragmas.h>

/*--------------------------------------------------------------------*/
/* General data structures                                            */
/*--------------------------------------------------------------------*/

struct Ilbmcopy
{  struct Copydriver copydriver;    /* Our superclass object instance */
   struct Aobject *copy;            /* The AOTP_COPY object we belong to */
   struct BitMap *bitmap;           /* Our bitmap */
   long width,height;               /* Dimensions of our bitmap */
   long ready;                      /* Last complete row number in bitmap */
   USHORT flags;                    /* See below */
};

/* Ilbmcopy flags: */
#define ILBMCF_DISPLAYED   0x0001   /* We are allowed to render ourselves
                                     * if we have a context frame */

/*--------------------------------------------------------------------*/
/* Misc functions                                                     */
/*--------------------------------------------------------------------*/

/* Limit coordinate (x), offset (dx), width (w) to a region (minx,maxx) */
static void Clipcopy(long *x,long *dx,long *w,long minx,long maxx)
{  
   if(minx>*x)
   {  (*dx)+=minx-*x;
      (*w)-=minx-*x;
      *x=minx;
   }
   if(maxx<*x+*w)
   {  *w=maxx-*x+1;
   }
}

/* Render these rows of our bitmap. */
static void Renderrows(struct Ilbmcopy *ic,struct Coords *coo,
   long minx,long miny,long maxx,long maxy,long minrow,long maxrow)
{  
   long x,y;      /* Resulting rastport x,y to blit to */
   long dx,dy;    /* Offset within bitmap to blit from */
   long w,h;      /* Width and height of portion to blit */
   
   /* Make sure we have a valid render context */
   coo=Clipcoords(ic->cframe,coo);
   if(coo && coo->rp)
   {  
      /* Initialize our blit parameters to the rastport coordinates of
       * the rows to render */
      x=ic->aox+coo->dx;
      y=ic->aoy+coo->dy+minrow;
      dx=0;
      dy=minrow;
      w=ic->width;
      h=maxrow-minrow+1;

      /* Clip our rendering to
       * - clip coordinates in coo  (rastport relative coordinates)
       * - minx etc                 (frame relative coordinates)
       */
      Clipcopy(&x,&dx,&w,coo->minx,coo->maxx);
      Clipcopy(&y,&dy,&h,coo->miny,coo->maxy);
      Clipcopy(&x,&dx,&w,minx+coo->dx,maxx+coo->dx);
      Clipcopy(&y,&dy,&h,miny+coo->dy,maxy+coo->dy);

      /* Now do the blitting if there is something left to blit */
      if(w>0 && h>0)
      {  BltBitMapRastPort(ic->bitmap,dx,dy,coo->rp,x,y,w,h,0xc0);
      }
   }
   
   /* Release our rendering context */
   Unclipcoords(coo);

   /*===== API 1.6 addition =================================================*/
   /* If a static image rendered itselves when decoding is fully complete,
    * it must notify its owning COPY object to schedule the JavaScript
    * onLoad event when appropriate.
    *
    * Note this must NOT be done for animated GIFs. */
   if(ic->ready==ic->height-1)
   {  Asetattrs(ic->copy,AOCPY_Onimgload,TRUE,TAG_END);
   }

   /* For animated GIF images, the situation is different. The image must
    * notify its owning copy (when decoding is complete) every time the
    * animation renders its last frame.
    * This must NOT happen after receiving every AOM_RENDER message (like
    * static images), but only when the animation playback sequence hits
    * the last frame.
    *
    * Probably this is not the location in the source where it would
    * happen, but anyway here is what has to be set:
   Asetattrs(ic->copy,AOCPY_Onimganim,TRUE,TAG_END);
    */
   /*===== End API 1.6 addition =============================================*/
}

/*--------------------------------------------------------------------*/
/* Plugin copydriver object dispatcher functions                      */
/*--------------------------------------------------------------------*/

/* Process the AOM_MEASURE method: set our Copydriver's width and height
 * to the bitmap's width and height. */
static ULONG Measurecopy(struct Ilbmcopy *ic,struct Ammeasure *ammeasure)
{  
   if(ic->bitmap)
   {  ic->aow=ic->width;
      ic->aoh=ic->height;
      if(ammeasure->ammr)
      {  ammeasure->ammr->width=ic->aow;
         ammeasure->ammr->minwidth=ic->aow;
      }
   }
   return 0;
}

/* Process the AOM_LAYOUT method. Not much to layout here, just set
 * our width and height again. */
static ULONG Layoutcopy(struct Ilbmcopy *ic,struct Amlayout *amlayout)
{  
   if(ic->bitmap)
   {  ic->aow=ic->width;
      ic->aoh=ic->height;
   }
   return 0;
}

/* Process the AOM_RENDER method. */
static ULONG Rendercopy(struct Ilbmcopy *ic,struct Amrender *amrender)
{  
   struct Coords *coo;
   
   /* Only do something if we have a bitmap */
   if(ic->bitmap)
   {  
      /* Make sure we have a valid render context */
      coo=Clipcoords(ic->cframe,amrender->coords);
      if(coo && coo->rp)
      {  
         /* Clear our background if necessary */
         if(amrender->flags&AMRF_CLEAR)
         {  Erasebg(ic->cframe,coo,amrender->minx,amrender->miny,
               amrender->maxx,amrender->maxy);
         }
         
         /* Now render all our valid rows */
         Renderrows(ic,coo,amrender->minx,amrender->miny,
            amrender->maxx,amrender->maxy,0,ic->ready);
      }
      
      /* Release the rendering context */
      Unclipcoords(coo);
   }
   return 0;
}

/* Process the AOM_SET method. This function is also used to set
 * the attributes for the AOM_NEW method */
static ULONG Setcopy(struct Ilbmcopy *ic,struct Amset *amset)
{  
   struct TagItem *tag,*tstate;
   BOOL newbitmap=FALSE;   /* Remember if we got a bitmap update */
   BOOL dimchanged=FALSE;  /* If our dimenasions have changed */
   long oldready=ic->ready;

   /* Pass a new AOM_SET message to our superclass.
    * Note: we cannot use our amset message because it might actually
    * be an AOM_NEW message.
    */
   Amethodas(AOTP_COPYDRIVER,ic,AOM_SET,amset->tags);

   /* Scan the taglist */
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCDV_Copy:
            /* The AOTP_COPY object we belong to */
            ic->copy=(struct Aobject *)tag->ti_Data;
            break;

         case AOCDV_Sourcedriver:
            /* The Ilbmsource object we belong to.
             * This implementation doesn't need it. */
            break;

         case AOCDV_Displayed:
            /* Remember if we are allowed to render ourselves */
            if(tag->ti_Data) ic->flags|=ILBMCF_DISPLAYED;
            else ic->flags&=~ILBMCF_DISPLAYED;
            break;

         case AOCDV_Width:
         case AOCDV_Height:
            /* AOCDV_Width and AOCDV_Height are the suggested width and
             * height for the embedded object. If possible, the
             * the image should be scaled to these dimensions.
             *
             * This example does not implement scaling.
             */
            break;

         case AOILBM_Bitmap:
            ic->bitmap=(struct BitMap *)tag->ti_Data;
            newbitmap=TRUE;
            if(!ic->bitmap)
            {  /* Bitmap was removed */
               ic->width=0;
               ic->height=0;
               ic->ready=-1;
               dimchanged=TRUE;
            }
            break;

         case AOILBM_Width:
            if(tag->ti_Data!=ic->width) dimchanged=TRUE;
            ic->width=tag->ti_Data;
            break;

         case AOILBM_Height:
            if(tag->ti_Data!=ic->height) dimchanged=TRUE;
            ic->height=tag->ti_Data;
            break;

         case AOILBM_Ready:
            ic->ready=tag->ti_Data;
            newbitmap=TRUE;
            break;
      }
   }

   /* If our dimensions have changed, let our AOTP_COPY object know, and
    * eventually we will receive an AOM_RENDER message. */
   if(dimchanged)
   {  Asetattrs(ic->copy,AOBJ_Changedchild,ic,TAG_END);
   }

   /* If the bitmap was changed but the dimensions stayed the same,
    * and we are allowed to render ourselves, render the new row(s) now. */
   else if(newbitmap && (ic->flags&ILBMCF_DISPLAYED) && ic->cframe)
   {  Renderrows(ic,NULL,0,0,AMRMAX,AMRMAX,oldready+1,ic->ready);
   }

   /* If we are not allowed to render ourselves, let our AOTP_COPY object
    * know when the bitmap is complete. */
   else if(newbitmap && !(ic->flags&ILBMCF_DISPLAYED) && ic->ready==ic->height-1)
   {  Asetattrs(ic->copy,AOBJ_Changedchild,ic,TAG_END);
   }

   /*===== API 1.6 addition =================================================*/
   /* When decoding is completed, the owning COPY object has to be notified
    * that the JavaScript onLoad event might have to be scheduled.
    *
    * Note: this notification must also be done when decoding was completed
    * already before this image copy object was created. If a similar
    * source -> copy notification scheme is used as in this example, this will
    * probably happen automatically. */
   if(newbitmap && ic->ready==ic->height-1)
   {  Asetattrs(ic->copy,AOCPY_Onimgload,TRUE,TAG_END);
   }

   /* More notes for animations (animated GIF) only:
    *
    * When decoding is completed, you must set both AOCPY_Onimgload (because
    * decoding is completed), and AOCPY_Onimganim (because only now the last
    * animation frame is fully displayed). Setting both attributes is safe,
    * provided this is done during the same invokation of this object's
    * dispatcher.
    *
    * However, if that is more convenient for the implementation, it is
    * allowed to set only AOCPY_Onimgload in this situation, and omit
    * setting of AOCPY_Onimganim this first time.
    *
    * When decoding is complete, you MUST set AOCPY_Onimgload regardless of
    * whether you set AOCPY_Onimganim this first time or not.
    */
   /*===== End API 1.6 addition =============================================*/

   return 0;
}

/* Process the AOM_NEW method */
static struct Ilbmcopy *Newcopy(struct Amset *amset)
{  
   struct Ilbmcopy *ic;

   /* Allocate a new object instance */
   if(ic=Allocobject(PluginBase->copydriver,sizeof(struct Ilbmcopy),amset))
   {  
      /* Set last ready row to -1 so rendering will start from row 0 */
      ic->ready=-1;
      
      /* Now set our attributes */
      Setcopy(ic,amset);
   }
   return ic;
}

/* Process the AOM_GET method */
static ULONG Getcopy(struct Ilbmcopy *ic,struct Amset *amset)
{  
   struct TagItem *tag,*tstate;
   
   /* First forward the message to our superclass */
   AmethodasA(AOTP_COPYDRIVER,ic,amset);
   
   /* Scan the taglist. */
   tstate=amset->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCDV_Ready:
            /* If we have a bitmap, we can render something. */
            PUTATTR(tag,ic->bitmap?TRUE:FALSE);
            break;

            /* Support background images only if we have a complete bitmap: */
         case AOCDV_Imagebitmap:
            PUTATTR(tag,(ic->ready==ic->height-1)?ic->bitmap:NULL);
            break;
         case AOCDV_Imagemask:
            PUTATTR(tag,NULL);
            break;
         case AOCDV_Imagewidth:
            PUTATTR(tag,(ic->bitmap && ic->ready==ic->height-1)?ic->width:0);
            break;
         case AOCDV_Imageheight:
            PUTATTR(tag,(ic->bitmap && ic->ready==ic->height-1)?ic->height:0);
            break;
      }
   }
   return 0;
}

/* Process the AOM_DISPOSE method */
static void Disposecopy(struct Ilbmcopy *ic)
{  
   /* Nothing to clean up. Just forward to our superclass object */
   Amethodas(AOTP_COPYDRIVER,ic,AOM_DISPOSE);
}

/* The main dispatcher */
__saveds __asm ULONG Dispatchcopy(
   register __a0 struct Ilbmcopy *ic,
   register __a1 struct Amessage *amsg)
{  
   ULONG result=0;

   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newcopy((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setcopy(ic,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getcopy(ic,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measurecopy(ic,(struct Ammeasure *)amsg);
         break;
      case AOM_LAYOUT:
         result=Layoutcopy(ic,(struct Amlayout *)amsg);
         break;
      case AOM_RENDER:
         result=Rendercopy(ic,(struct Amrender *)amsg);
         break;
      case AOM_DISPOSE:
         Disposecopy(ic);
         break;
      default:
         result=AmethodasA(AOTP_COPYDRIVER,ic,amsg);
         break;
   }
   return result;
}
