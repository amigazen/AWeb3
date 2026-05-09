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

/* field.c - AWeb html document field element object */

#include "aweb.h"
#include "field.h"
#include "form.h"
#include "window.h"
#include "jslib.h"
#include <reaction/reaction.h>
#include <proto/intuition.h>
#include <proto/utility.h>

/*------------------------------------------------------------------------*/

/* Get or set name property (JS) */
static BOOL Propertyname(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Field *fld=vd->hookdata;
   UBYTE *name;
   if(fld)
   {  switch(vd->code)
      {  case VHC_SET:
            if(name=Jtostring(vd->jc,vd->value))
            {  if(fld->name) FREE(fld->name);
               fld->name=Dupstr(name,-1);
            }
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,fld->name);
            result=TRUE;
            break;
      }
   }
   return result;
}

/*------------------------------------------------------------------------*/

static long Setfield(struct Field *fld,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=Amethodas(AOTP_ELEMENT,fld,AOM_SET,ams->tags);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFLD_Form:
            if(fld->form) Aremchild(fld->form,fld,0);
            fld->form=(void *)tag->ti_Data;
            if(fld->form) Aaddchild(fld->form,fld,0);
            break;
         case AOFLD_Name:
            if(fld->name) FREE(fld->name);
            fld->name=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFLD_Value:
            if(fld->value) FREE(fld->value);
            fld->value=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOBJ_Frame:
            if(!tag->ti_Data && fld->jobject)
            {  Disposejobject(fld->jobject);
               fld->jobject=NULL;
            }
            break;
         case AOBJ_Window:
            if(!tag->ti_Data && fld->win)
            {  Asetattrs(fld->win,AOWIN_Goinactive,fld,TAG_END);
            }
            fld->win=(void *)tag->ti_Data;
            break;
      }
   }
   return result;
}

static long Getfield(struct Field *fld,struct Amset *ams)
{  long result;
   struct TagItem *tag,*tstate=ams->tags;
   result=AmethodasA(AOTP_ELEMENT,fld,ams);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFLD_Form:
            PUTATTR(tag,fld->form);
            break;
         case AOFLD_Name:
            PUTATTR(tag,fld->name);
            break;
         case AOFLD_Value:
            PUTATTR(tag,fld->value);
            break;
         case AOFLD_Multivalue:
            PUTATTR(tag,NULL);
            break;
         case AOBJ_Jobject:
            PUTATTR(tag,fld->jobject);
            break;
      }
   }
   return result;
}

static struct Field *Newfield(struct Amset *ams)
{  struct Field *fld;
   if(fld=Allocobject(AOTP_FIELD,sizeof(struct Field),ams))
   {  Setfield(fld,ams);
   }
   return fld;
}

static long Jsetupfield(struct Field *fld,struct Amjsetup *amj)
{  struct Jvar *jv,*jva;
   struct Jobject *jelts,*jo,*jarray;
   UBYTE *name;
   if(!fld->jobject)
   {  if(fld->jobject=Newjobject(amj->jc))
      {  Setjobject(fld->jobject,NULL,fld,NULL);
         Jkeepobject(fld->jobject,TRUE);
         /* If this is an isolated field ie it has no form we must specify KEEPOBJECT */
         /* Else it will get garbage collected, also we add to the parent */
         /* in a similar way to an image, if it's in a form the forms object will */
         /* effectively protect from garbage collection */
         if(!fld->form)
         {  /* This is a case where we use Jaddproperty */
            /* We don't want to overwrite any existing property */
            /* with the same name as this can cause multiple disposal */
            if(fld->name)
            {  if(jv=Jaddproperty(amj->jc,amj->parent,fld->name))
               {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                  Jasgobject(amj->jc,jv,fld->jobject);
               }
            }
         }
         /* When adding this object to its parent, there are 3 possibilities:
          * 1. There is no property with this name yet, add it as a single object.
          * 2. There is a property that is an array, add this object.
          * 3. There is a property (not an array), create an array, and add
          *    both the existing object value and this field's object. */
         if(jv=Jproperty(amj->jc,amj->parent,fld->name))
         {  jo=Jtoobject(amj->jc,jv);
            if(!jo)
            {  /* No property value yet */
               Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgobject(amj->jc,jv,fld->jobject);
            }
            else
            {  if(Jisarray(amj->jc,jo))
               {  /* Already an array */
                  jarray=jo;
               }
               else
               {  /* Single object, turn into array */
                  if(jarray=Newjarray(amj->jc))
                  {  /* First assign the current object to a new array elt,
                      * then replace the current value with the array. */
                     if(jva=Jnewarrayelt(amj->jc,jarray))
                     {  Setjproperty(jva,JPROPHOOK_READONLY,NULL);
                        Jasgobject(amj->jc,jva,jo);
                     }
                     Jasgobject(amj->jc,jv,jarray);
                     /* jarray is now the form property value; Freejobject would UAF the parent. */
                  }
               }
               if(jva=Jnewarrayelt(amj->jc,jarray))
               {  Setjproperty(jva,JPROPHOOK_READONLY,NULL);
                  Jasgobject(amj->jc,jva,fld->jobject);
               }
            }
         }
         if(jv=Jproperty(amj->jc,fld->jobject,"name"))
         {  Setjproperty(jv,Propertyname,fld);
         }
         if(jv=Jproperty(amj->jc,fld->jobject,"form"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgobject(amj->jc,jv,amj->parent);
         }
         if(jelts=(struct Jobject *)Agetattr(fld->form,AOFOR_Jelements))
         {  if(jv=Jnewarrayelt(amj->jc,jelts))
            {  Jasgobject(amj->jc,jv,fld->jobject);
            }
            /* Also add element directly to form under its index number */
            if(name=Jpname(jv))
            {  if(jv=Jproperty(amj->jc,amj->parent,name))
               {  Jasgobject(amj->jc,jv,fld->jobject);
               }
            }
            /* And also add it to the array under its field name */
            if(jv=Jproperty(amj->jc,jelts,fld->name))
            {  Jasgobject(amj->jc,jv,fld->jobject);
            }
         }
      }
   }
   return 0;
}

static void Disposefield(struct Field *fld)
{  if(fld)
   {  if(fld->win) Asetattrs(fld->win,AOWIN_Goinactive,fld,TAG_END);
      if(fld->form) Aremchild(fld->form,fld,0);
      if(fld->name) FREE(fld->name);
      if(fld->value) FREE(fld->value);
      Amethodas(AOTP_ELEMENT,fld,AOM_DISPOSE);
   }
}

static long Dispatch(struct Field *fld,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newfield((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setfield(fld,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getfield(fld,(struct Amset *)amsg);
         break;
      case AOM_JSETUP:
         result=Jsetupfield(fld,(struct Amjsetup *)amsg);
         break;
      case AOM_DISPOSE:
         Disposefield(fld);
         break;
      case AOM_DEINSTALL:
         break;
      default:
         result=AmethodasA(AOTP_ELEMENT,fld,amsg);
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installfield(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_FIELD,Dispatch)) return FALSE;
   return TRUE;
}
