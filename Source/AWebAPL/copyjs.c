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

/* copyjs - AWeb copy JS Image interface */

#include "aweb.h"
#include "url.h"
#include "document.h"
#include "copyprivate.h"
#include "jslib.h"

/*-----------------------------------------------------------------------*/

/* Get or set JS property */
static BOOL Propertycomplete(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Copy *cop=vd->hookdata;
   if(cop)
   {  switch(vd->code)
      {  case VHC_SET:
            /* Property is read-only */
            result=TRUE;
            break;
         case VHC_GET:
            Jasgboolean(vd->jc,vd->value,BOOLVAL(cop->flags&CPYF_EOF));
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertysrc(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Copy *cop=vd->hookdata;
   UBYTE *src,*base;
   void *url;
   if(cop)
   {  switch(vd->code)
      {  case VHC_SET:
            if(src=Jtostring(vd->jc,vd->value))
            {  base=(UBYTE *)Agetattr(Getjsdocument(vd->jc),AODOC_Base);
               if((url=Findurl(base,src,0)) && url!=cop->url)
               {  cop->flags&=~(CPYF_ERROR|CPYF_EOF);
                  cop->flags|=CPYF_INITIAL;
                  /* Don't change dimensions once they're known. */
                  if(Agetattr(cop->driver,AOCDV_Ready))
                  {  if(!cop->width) cop->width=cop->driver->aow;
                     if(!cop->height) cop->height=cop->driver->aoh;
                  }
                  Asetattrs(cop,
                     AOCPY_Driver,NULL,
                     AOCPY_Url,url,
                     TAG_END);
               }
            }
            result=TRUE;
            break;
         case VHC_GET:
            if(src=(UBYTE *)Agetattr(cop->url,AOURL_Url))
            {  Jasgstring(vd->jc,vd->value,src);
            }
            result=TRUE;
            break;
      }
   }
   return result;
}

/*-----------------------------------------------------------------------*/

static void Disposejsimage(struct Copy *cop)
{  Adisposeobject(cop);
}

/* Image() constructor. This will be called after creation of a new
 * object to turn it into an Image. */
static void Imageconstructor(struct Jcontext *jc)
{  struct Jobject *jthis=Jthis(jc);
   struct Jvar *jv;
   struct Copy *cop;
   /* Ignore width,height arguments */
   if(cop=Anewobject(AOTP_COPY,
      AOCPY_Embedded,TRUE,
      AOCPY_Defaulttype,"image/x-unknown",
      AOCPY_Trueimage,TRUE,
      AOCPY_Onload,"if(this.onload) this.onload();",
      AOCPY_Onerror,"if(this.onerror) this.onerror();",
      AOCPY_Onabort,"if(this.onabort) this.onabort();",
      AOCPY_Onclick,"if(this.onclick) this.onclick();",
      TAG_END))
   {  cop->flags|=CPYF_JSIMAGE;
      cop->jsframe=Getjsframe(jc);
      cop->jobject=jthis;
      Setjobject(jthis,NULL,cop,Disposejsimage);
      if(jv=Jproperty(jc,jthis,"complete"))
      {  Setjproperty(jv,Propertycomplete,cop);
      }
      if(jv=Jproperty(jc,jthis,"height"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgnumber(jc,jv,cop->height?cop->height:cop->aoh);
      }
      if(jv=Jproperty(jc,jthis,"lowsrc"))
      {  /* No support for lowsrc - just create a read/write property */
         Jasgstring(jc,jv,"");
      }
      if(jv=Jproperty(jc,jthis,"src"))
      {  Setjproperty(jv,Propertysrc,cop);
      }
      if(jv=Jproperty(jc,jthis,"width"))
      {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
         Jasgnumber(jc,jv,cop->width?cop->width:cop->aow);
      }
   }
}

/*-----------------------------------------------------------------------*/

void Addimageconstructor(struct Jcontext *jc,struct Jobject *parent)
{  struct Jobject *jo,*proto;
   struct Jvar *jv;
   if(jo=Addjfunction(jc,parent,"Image",Imageconstructor,"width","height",NULL))
   {  if(proto=Newjobject(jc))
      {  /* Most properties have hooks so can't be set here */
         if(jv=Jproperty(jc,proto,"onload"))
         {  Jasgobject(jc,jv,NULL);
         }
         if(jv=Jproperty(jc,proto,"onerror"))
         {  Jasgobject(jc,jv,NULL);
         }
         if(jv=Jproperty(jc,proto,"onabort"))
         {  Jasgobject(jc,jv,NULL);
         }
         Jsetprototype(jc,jo,proto);
         /* proto is referenced from jo's prototype property; do not Freejobject(proto). */
      }
   }
}

long Jsetupcopy(struct Copy *cop,struct Amjsetup *amj)
{  long result=0;
   struct Jvar *jv;
   struct Jobject *images,*parent;
   if(cop->flags&CPYF_TRUEIMAGE)
   {  if(!cop->jobject)
      {  if(cop->jobject=Newjobject(amj->jc))
         {  Jkeepobject(cop->jobject,TRUE);
            if(cop->name)
            {  if(cop->jform) parent=(void *)Agetattr(cop->jform,AOBJ_Jobject);
               else parent=amj->parent;
               /* This is a case where we use Jaddproperty */
               /* We don't want to overwrite any existing property */
               /* with the same name as this can cause multiple disposal */
               if(jv=Jaddproperty(amj->jc,parent,cop->name))
               {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                  Jasgobject(amj->jc,jv,cop->jobject);
               }
            }
            if(images=Jfindarray(amj->jc,amj->parent,"images"))
            {  if(jv=Jnewarrayelt(amj->jc,images))
               {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                  Jasgobject(amj->jc,jv,cop->jobject);
               }
               if(cop->name)
               {  if(jv=Jproperty(amj->jc,images,cop->name))
                  {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                     Jasgobject(amj->jc,jv,cop->jobject);
                  }
               }
            }
            if(jv=Jproperty(amj->jc,cop->jobject,"border"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgnumber(amj->jc,jv,cop->border);
            }
            if(jv=Jproperty(amj->jc,cop->jobject,"complete"))
            {  Setjproperty(jv,Propertycomplete,cop);
            }
            if(jv=Jproperty(amj->jc,cop->jobject,"height"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgnumber(amj->jc,jv,cop->height?cop->height:cop->aoh);
            }
            if(jv=Jproperty(amj->jc,cop->jobject,"hspace"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgnumber(amj->jc,jv,cop->hspace);
            }
            if(jv=Jproperty(amj->jc,cop->jobject,"lowsrc"))
            {  /* No support for lowsrc - just create a read/write property */
               Jasgstring(amj->jc,jv,"");
            }
            if(jv=Jproperty(amj->jc,cop->jobject,"name"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgstring(amj->jc,jv,cop->name);
            }
            if(jv=Jproperty(amj->jc,cop->jobject,"src"))
            {  Setjproperty(jv,Propertysrc,cop);
            }
            if(jv=Jproperty(amj->jc,cop->jobject,"vspace"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgnumber(amj->jc,jv,cop->vspace);
            }
            if(jv=Jproperty(amj->jc,cop->jobject,"width"))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgnumber(amj->jc,jv,cop->width?cop->width:cop->aow);
            }
         }
      }
   }
   else if(cop->driver)
   {  result=AmethodA(cop->driver,amj);
   }
   return result;
}

long Jonmousecopy(struct Copy *cop,struct Amjonmouse *amj)
{  long result=0;
   switch(amj->event)
   {  case AMJE_ONMOUSEOVER:
         if(cop->onmouseover)
         {  Runjavascript(cop->frame?cop->frame:cop->jsframe,cop->onmouseover,&cop->jobject);
         }
         break;
      case AMJE_ONMOUSEOUT:
         if(cop->onmouseout)
         {  Runjavascript(cop->frame?cop->frame:cop->jsframe,cop->onmouseout,&cop->jobject);
         }
         break;
   }
   return result;
}

void Freejcopy(struct Copy *cop)
{  if(cop->jobject)
   {  Jkeepobject(cop->jobject,FALSE);
      /* If this was created by the JS Image() constructor, we are disposed
       * when the JS object is disposed, so no need to dispose it again in that case */
      if(!(cop->flags&CPYF_JSIMAGE)) Disposejobject(cop->jobject);
      cop->jobject=NULL;
   }
}
