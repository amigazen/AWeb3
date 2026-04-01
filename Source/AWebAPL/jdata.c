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

/* jdata.c - AWeb js data model */

#include "awebjs.h"
#include "jprotos.h"

struct Array;  /* Forward declaration */

/*-----------------------------------------------------------------------*/

/* Call this property function */
BOOL Callproperty(struct Jcontext *jc,struct Jobject *jo,UBYTE *name)
{  struct Variable *prop;
   if(jo && (prop=Getproperty(jo,name))
   && prop->val.type==VTP_OBJECT && prop->val.value.obj.ovalue && prop->val.value.obj.ovalue->function)
   {
      Keepobject(jo,TRUE);
      Callfunctionbody(jc,prop->val.value.obj.ovalue->function,jo);
      Keepobject(jo,FALSE);

      return TRUE;
   }
   return FALSE;
}

/*-----------------------------------------------------------------------*/

/* Clear out a value */
/* This must always be called when assigning to a previously used value */
/* This must always be used before discarding / disposing of a previously used value */

void Clearvalue(struct Value *v)
{  switch(v->type)
   {  case VTP_STRING:
         if(v->value.svalue)
         {
             FREE(v->value.svalue);
             v->value.svalue = NULL;
         }
         break;
   }
   v->type=VTP_UNDEFINED;
}

/* Assign a value */
void Asgvalue(struct Value *to,struct Value *from)
{
   Clearvalue(to);
   to->type=from->type;
   to->attr=from->attr;
   switch(from->type)
   {  case VTP_NUMBER:
         to->value.nvalue=from->value.nvalue;
         break;
      case VTP_BOOLEAN:
         to->value.bvalue=from->value.bvalue;
         break;
      case VTP_STRING:
         /* Defensive: prevent crashes on invalid/low pointers. */
         if(from->value.svalue && (ULONG)from->value.svalue >= 256)
         {  to->value.svalue=Jdupstr(from->value.svalue,-1,Getpool(from->value.svalue));
         }
         else
         {  to->value.svalue=NULL;
         }
         break;
      case VTP_OBJECT:
         to->value.obj.ovalue=from->value.obj.ovalue;
         to->value.obj.fthis=from->value.obj.fthis;
         break;
   }
}

/* Assign a number */
void Asgnumber(struct Value *to,UBYTE attr,double n)
{  switch(attr)
   {  case VNA_NAN:
         n=0.0;
         break;
      case VNA_INFINITY:
         n=1.0;
         break;
      case VNA_NEGINFINITY:
         n=-1.0;
         break;
   }
   Clearvalue(to);
   to->type=VTP_NUMBER;
   to->attr=attr;
   to->value.nvalue=n;
}

/* Assign a boolean */
void Asgboolean(struct Value *to,BOOL b)
{  Clearvalue(to);
   to->type=VTP_BOOLEAN;
   to->attr=0;
   to->value.bvalue=b;
}

/* Assign a string */
void Asgstring(struct Value *to,UBYTE *s,void *pool)
{  Clearvalue(to);
   to->type=VTP_STRING;
   to->attr=0;
   to->value.svalue=Jdupstr(s,-1,pool);
}

/* Assign a string of given length */
void Asgstringlen(struct Value *to,UBYTE *s,long len,void *pool)
{
    Clearvalue(to);
    to->type=VTP_STRING;
    to->attr=0;
    to->value.svalue=Jdupstr(s,len,pool);
}

/* Assign an object */
void Asgobject(struct Value *to,struct Jobject *jo)
{  Clearvalue(to);
   to->type=VTP_OBJECT;
   to->attr=0;
   to->value.obj.ovalue=jo;
   to->value.obj.fthis=NULL;
}

/* Assign a function */
void Asgfunction(struct Value *to,struct Jobject *f,struct Jobject *fthis)
{  Clearvalue(to);
   to->type=VTP_OBJECT;
   to->attr=0;
   to->value.obj.ovalue=f;
   to->value.obj.fthis=fthis;
}

/* Make this value a string */
void Tostring(struct Value *v,struct Jcontext *jc)
{  if(!jc)
   {  if(v) v->type=VTP_UNDEFINED;
      return;
   }
   switch(v->type)
   {  case VTP_NUMBER:
         {  UBYTE buffer[32];
            switch(v->attr)
            {  case VNA_NAN:
                  strcpy(buffer,"NaN");
                  break;
               case VNA_INFINITY:
                  strcpy(buffer,"+Infinity");
                  break;
               case VNA_NEGINFINITY:
                  strcpy(buffer,"-Infinity");
                  break;
               default:
                  sprintf(buffer,"%.15lg",v->value.nvalue);
                  break;
            }
            Asgstring(v,buffer,jc->pool);
         }
         break;
      case VTP_BOOLEAN:
         Asgstring(v,v->value.bvalue?"true":"false",jc->pool);
         break;
      case VTP_STRING:
         /* Ensure string pointer is valid - if NULL, set to empty string */
         if(!v->value.svalue)
         {  Asgstring(v,"",jc->pool);
         }
         break;
      case VTP_OBJECT:
         if(v->value.obj.ovalue && v->value.obj.ovalue->function)
         {  struct Jbuffer *jb;
            if(jb=Jdecompile(jc,(struct Element *)v->value.obj.ovalue->function))
            {  Asgstring(v,jb->buffer,jc->pool);
               Freejbuffer(jb);
            }
            else
            {  Asgstring(v,"function",jc->pool);
            }
         }
         else
         {  struct Jobject *oldthis;

            if(Callproperty(jc,v->value.obj.ovalue,"toString") && jc->val->type==VTP_STRING)
            {  Asgstring(v,jc->val->value.svalue,jc->pool);
            }
            else
            {  oldthis=jc->jthis;
               jc->jthis=v->value.obj.ovalue;
               Defaulttostring(jc);
               jc->jthis=oldthis;
               Asgvalue(v,jc->val);

            }
         }
         break;
      default:
         Asgstring(v,"undefined",jc->pool);
         break;
   }
}

/* Make this value a number */
void Tonumber(struct Value *v,struct Jcontext *jc)
{
       switch(v->type)
       {  case VTP_NUMBER:
             break;
          case VTP_BOOLEAN:
             Asgnumber(v,VNA_VALID,v->value.bvalue?1.0:0.0);
             break;
          case VTP_STRING:
             {  double n;
                if(sscanf(v->value.svalue,"%lg",&n))
                {  Asgnumber(v,VNA_VALID,n);
                }
                else
                {  Asgnumber(v,VNA_NAN,0.0);
                }
             }
             break;
          case VTP_OBJECT:
             if(Callproperty(jc,v->value.obj.ovalue,"valueOf") && jc->val->type==VTP_NUMBER)
             {  Asgvalue(v,jc->val);
             }
             else
             {  Asgnumber(v,VNA_NAN,0.0);
             }
             break;
          default:
             Asgnumber(v,VNA_NAN,0.0);
             break;
       }
}

/* Make this value a boolean */
void Toboolean(struct Value *v,struct Jcontext *jc)
{  switch(v->type)
   {  case VTP_NUMBER:
         Asgboolean(v,v->value.nvalue!=0.0);
         break;
      case VTP_BOOLEAN:
         break;
      case VTP_STRING:
         Asgboolean(v,(v->value.svalue && *v->value.svalue) ? TRUE : FALSE);
         break;
      case VTP_OBJECT:
         if(Callproperty(jc,v->value.obj.ovalue,"valueOf") && jc->val->type==VTP_BOOLEAN)
         {  Asgvalue(v,jc->val);
         }
         else
         {  Asgboolean(v,v->value.obj.ovalue?TRUE:FALSE);
         }
         break;
      default:
         Asgboolean(v,FALSE);
         break;
   }
}

/* Make this value an object */
void Toobject(struct Value *v,struct Jcontext *jc)
{  struct Jobject *jo;
   if(!v || !jc)
   {  return;
   }
   switch(v->type)
   {  case VTP_NUMBER:
         Asgobject(v,jo=Newnumber(jc,v->attr,v->value.nvalue));
         break;
      case VTP_BOOLEAN:
         Asgobject(v,jo=Newboolean(jc,v->value.bvalue));
         break;
      case VTP_STRING:
         Asgobject(v,jo=Newstring(jc,v->value.svalue));
         break;
      case VTP_OBJECT:
         break;
      default:
         Asgobject(v,NULL);
         break;
   }
}

/* Make this value a function */
void Tofunction(struct Value *v,struct Jcontext *jc)
{  Toobject(v,jc);
}

/*-----------------------------------------------------------------------*/

/* Default toString property function */
void Defaulttostring(struct Jcontext *jc)
{
        struct Jobject *jo;
   UBYTE *p,*buf;
   if(!jc)
   {  return;
   }
   jo=jc->jthis;
   if(Callproperty(jc,jo,"valueOf") && jc->val && jc->val->type==VTP_STRING)
   {  /* We're done */
   }
   else
   {  if(!jo)
      {  Asgstring(jc->val,"null",jc->pool);
      }
      else
      {  if(jo->constructor && jo->constructor->function
         && jo->constructor->function->name)
         {  p=jo->constructor->function->name;
         }
         else
         {  p="";
         }
         if(buf=ALLOCTYPE(UBYTE,10+strlen(p),0,jc->pool))
         {  sprintf(buf,"[object %s]",p);
            Asgstring(jc->val,buf,jc->pool);
            FREE(buf);
         }
         else
         {  Asgstring(jc->val,"[object]",jc->pool);
         }
      }
   }
   Asgvalue(RETVAL(jc),jc->val);
}

/*-----------------------------------------------------------------------*/

/* Create a new variable */
struct Variable *Newvar(UBYTE *name,struct Jcontext *jc)
{  struct Variable *var;
   if(!jc)
   {  return NULL;
   }
   if(var=ALLOCVAR(jc))
   {
      if(name)
      {  var->name=Jdupstr(name,-1,jc->pool);
      }
      var->val.type=0;
   }
   return var;
}

/* Dispose this variable */
void Disposevar(struct Variable *var)
{  if(var)
   {  if(var->name)
      {
          FREE(var->name);
      }
      Clearvalue(&var->val);
      FREE(var);
   }
}

/*-----------------------------------------------------------------------*/

/* Create a new empty object */
struct Jobject *Newobject(struct Jcontext *jc)
{  struct Jobject *jo = NULL;

   /* test for and run a garbage collect */
   /* Do this before creating our object else it'll get swept away! */
   if(jc->gc <= 0 && (jc->nogc<=0))
   {
       jc->gc = GC_THRESHOLD;
       Garbagecollect(jc);
   }

   if(jo=ALLOCOBJECT(jc))
   {  NewList((struct List *)&jo->properties);
      jo->notdisposed = TRUE;
      jo->jc = jc;
      while(jo->jc->truecontext)
      {
          jo->jc = jo->jc->truecontext;
      }

      if(jc->nogc <= 0)jc->gc--;
      AddTail((struct List *)&jc->objects,(struct Node *)jo);
   }
   //adebug("NEW OBJECT: %08lx\n",jo);
   return jo;
}

/* Dispose an object */
void Disposeobject(struct Jobject *jo)
{  struct Variable *var;
   if(jo)
   {
      while(var=(struct Variable *)RemHead((struct List *)&jo->properties))
      {
          Disposevar(var);
      }
      if(jo->internal && jo->dispose)
      {
                                jo->dispose(jo->internal);
      }
      if(jo->function)
      {  Jdispose((struct Element *)jo->function);
      }

      FREE(jo);
   }
}

/* Clear all properties of this object. If a property contains a reference
 * to another object, clear that too recursively. */
void Clearobject(struct Jobject *jo,UBYTE **except)
{  struct Variable *var,*next;
   UBYTE **p;
   if(jo && !(jo->flags&OBJF_CLEARING))
   {  jo->flags|=OBJF_CLEARING;
      for(var=jo->properties.first;var->next;var=next)
      {  next=var->next;
         if(var->name && except)
         {  for(p=except;*p;p++)
            {  if(STRIEQUAL(*p,var->name)) break;
            }
         }
         else p=NULL;
         if(!p || !*p)
         {  /* not in exception list */
            Remove((struct Node *)var);
/*
            if(var->val.type==VTP_OBJECT && var->val.value.obj.ovalue)
            {  Clearobject(jo,NULL);
            }
*/
            Disposevar(var);
         }
      }
      jo->flags&=~OBJF_CLEARING;
   }
}

/* delete a property from this object */
BOOL _Generic_Deleteownproperty(struct Jobject *jo, STRPTR name)
{
    struct Variable *var;
    if((var = Getownproperty(jo,name)))
    {
        Remove((struct Node *)var);
        Disposevar(var);
        return TRUE;
    }
    return FALSE;
}

BOOL Deleteownproperty(struct Jobject *jo, STRPTR name)
{
    if(jo)
    {
        switch(jo->type)
        {
            case OBJT_ARRAY:
                return _Array_Deleteownproperty(jo,name);
                break;
            default:
                return _Generic_Deleteownproperty(jo,name);
            break;
        }
    }
    return FALSE;
}

/* Add a property to this object */
struct Variable *_Generic_Addproperty(struct Jobject *jo, STRPTR name)
{  struct Variable *var=NULL;
   if(jo)
   {  if(var=Newvar(name,jo->jc))
      {  AddTail((struct List *)&jo->properties,(struct Node *)var);
      }
   }
   return var;
}

struct Variable *Addproperty(struct Jobject *jo, STRPTR name)
{
    if(jo)
    {
        switch(jo->type)
        {
            case OBJT_ARRAY:
                return _Array_Addproperty(jo,name);
                break;
            default:
                return _Generic_Addproperty(jo,name);
            break;
        }
    }
    return NULL;;
}

/* Find a property in this object */

/* Getownproperty() finds a property that belongs directly to the object */
/* it will call an object specific method if appropriate */


struct Variable *_Generic_Getownproperty(struct Jobject *jo, STRPTR name)
{  struct Variable *var;
   if(jo)
   {  for(var=jo->properties.first;var && var->next;var=var->next)
      {  if(STREQUAL(var->name,name)) return var;
      }
   }
   return NULL;
}

struct Variable* Getownproperty(struct Jobject *jo, STRPTR name)
{
    struct Variable *var = NULL;
    //adebug("jo %08lx name %08lx\n");
    if(jo)
    {
        switch (jo->type)
        {
            case OBJT_ARRAY:
                var = _Array_Getownproperty(jo,name);
                break;
            default:
                var = _Generic_Getownproperty(jo,name);
                break;
        }
    }
    return var;
}

/* The more general getproperty, first seaches the object and then the prototype chain */

struct Variable *Getproperty(struct Jobject *jo, STRPTR name)
{
    struct Variable *var;
    struct Jobject *proto;
    struct Jobject *p1;

   if(jo)
   {
   /*
      for(var=jo->properties.first;var->next;var=var->next)
      {  if(STREQUAL(var->name,name)) return var;
      }
   */
            if((var = Getownproperty(jo,name)))
            {
                return var;
            }

   }
   /* didn't find the property start to search prototype chain */
   if(jo && (proto = jo->prototype))
   {
       p1 = proto;
       while(proto)
       {
            /*
            for(var=proto->properties.first;var->next;var=var->next)
            {
                if(STREQUAL(var->name,name)) return var;
            }
            */

            if((var = Getownproperty(proto,name)))
            {
                return var;
            }
            /* Validate proto is still valid before accessing proto->prototype */
            /* Check that object hasn't been disposed and jc pointer is valid */
            if(!proto->jc || proto->notdisposed != TRUE)
            {
                /* Object appears to be invalid or disposed, stop traversal */
                break;
            }
            proto = proto->prototype;
            if(!proto)
            {
                /* Reached end of prototype chain */
                break;
            }
            if(proto == p1)
            {
                //adebug("panick! circular prototype chain!\n");
                break;
            }
       }
   }
   return NULL;
}

/* Prototype property variable hook. (data) is (struct Jobject *constructor).
 * Change value must add/set it in all objects of this type */
BOOL Protopropvhook(struct Varhookdata *h)
{  BOOL result=FALSE;
   struct Jobject *jo;
   struct Variable *prop;
   switch(h->code)
   {  case VHC_SET:
         for(jo=h->jc->objects.first;jo->next;jo=jo->next)
         {  if(jo->constructor==h->var->hookdata)
            {  if(!(prop=Getownproperty(jo,h->var->name)))
               {  prop=Addproperty(jo,h->var->name);
               }
               if(prop)
               {  Asgvalue(&prop->val,&h->value->val);
               }
            }
         }
         result=TRUE;
         break;
   }
   return result;
}

/* Prototype object hook.
 * Add property must set the prototype property hook */
BOOL Prototypeohook(struct Objhookdata *h)
{  BOOL result=FALSE;
   struct Variable *prop;
   switch(h->code)
   {  case OHC_ADDPROPERTY:
         if(!(prop=Getownproperty(h->jo,h->name)))
         {  prop=Addproperty(h->jo,h->name);
         }
         if(prop)
         { // prop->hook=Protopropvhook;
            prop->hookdata=h->jo->constructor;
         }
         result=TRUE;
         break;
   }
   return result;
}

/* General hook function for constants (that cannot change their value) */
BOOL Constantvhook(struct Varhookdata *h)
{  BOOL result=FALSE;
   if(h->code==VHC_SET) result=TRUE;
   return result;
}

/*-----------------------------------------------------------------------*/

/* Call a variable hook function */
BOOL Callvhook(struct Variable *var,struct Jcontext *jc,short code,struct Value *val)
{
   BOOL result=FALSE;
   if(var && var->hook)
   {
      struct Varhookdata vh={0};
      struct Variable valvar={0};
      vh.jc=jc;
      vh.code=code;
      vh.var=var;
      vh.hookdata=var->hookdata;
      if(code==VHC_SET) Asgvalue(&valvar.val,val);
      vh.value=&valvar;
      vh.name=var->name;
      result=var->hook(&vh);
      if(result && code==VHC_GET) Asgvalue(val,&valvar.val);
      Clearvalue(&valvar.val);
   }
   return result;
}

/* Call an object hook function */
BOOL Callohook(struct Jobject *jo,struct Jcontext *jc,short code,UBYTE *name)
{
   BOOL result=FALSE;
   if(jo && jo->hook)
   {
      struct Objhookdata oh;
      oh.jc=jc;
      oh.code=code;
      oh.jo=jo;
      oh.name=name;
      result=jo->hook(&oh);
   }
   return result;
}

void Dumpobjects(struct Jcontext *jc)
{  struct Jobject *jo;
   struct Variable *v;
   debug("=== JS object dump ===\n");
   for(jo=jc->objects.first;jo->next;jo=jo->next)
   {  debug("%08lx :",jo);
      if(jo->constructor && jo->constructor->function && jo->constructor->function->name)
      {  debug("[%s] ",jo->constructor->function->name);
      }
      if(jo->function && jo->function->name)
      {  debug("{%s} ",jo->function->name);
      }
      for(v=jo->properties.first;v->next;v=v->next)
      {  if(v->name)
         {  debug("%s,",v->name);
         }
      }
      debug("\n");
   }
}

/*-----------------------------------------------------------------------*/
static int depth = 0;
struct Array            /* Used as internal object value */
{
    long length;            /* current array length of data*/
    struct Variable **array; /* pointer to current array */
    long array_length;       /* current length of storage */
};


static void Garbagemark(struct Jobject *jo)
{  struct Variable *v;
   depth ++;
   if(jo && !(jo->flags&OBJF_USED))
   {  jo->flags|=OBJF_USED;
             if(jo->notdisposed != TRUE)  //FALSE)
             {
                //adebug("attempt to mark disposed object %08lx %08lx %s\n",jo,jo->notdisposed,(jo->var && jo->var->name)?jo->var->name:"NULL");
                // Dumpjobject(jo);
                //adebug("marking constructor\n");
             }
      else
      {
          for(v=jo->properties.first;v && v->next;v=v->next)
          {

             if(v->val.type==VTP_OBJECT)
             {
                Garbagemark(v->val.value.obj.ovalue);
                if(v->val.value.obj.fthis) Garbagemark(v->val.value.obj.fthis);
             }
          }
          Garbagemark(jo->constructor);
          if(jo->function)
          {  Garbagemark(jo->function->fscope);
          }
          if(jo->type == OBJT_ARRAY)
          {
              if(jo->internal)
              {
                struct Array *a = jo->internal;
                if(a->length && a->array_length)
                {
                    int i;

                    for(i = 0; i< a->length && i < a->array_length;i++)
                    {
                        if(a->array && a->array[i])
                        {
                            if(a->array[i]->val.type == VTP_OBJECT)
                            {
                                Garbagemark(a->array[i]->val.value.obj.ovalue);
                                if(a->array[i]->val.value.obj.fthis)
                                Garbagemark(a->array[i]->val.value.obj.fthis);
                            }
                        }
                    }
                }
              }
          }
      }
   }
   depth--;
}

void Garbagecollect(struct Jcontext *jc)
{  struct Jobject *jo,*jonext;
   struct Function *f;
   struct With *w;
   struct Variable *v;
   struct List *objectlist;
   struct List *jlist;
   struct This *this;
   int scanned = 0;
   objectlist = (struct List *)(&jc->objects);

   jlist = (struct List *)(&jc->thislist);

    for(jo=(struct Jobject *)objectlist->lh_Head;jo && jo->next;jo=(struct Jobject *)jo->next)
    {  jo->flags&=~OBJF_USED;
       scanned ++;
    }

    /* No point garbage collecting if no objects in list */
   if(scanned > 0)
   {
       if(jc->val && jc->val->type == VTP_OBJECT)
       {
          if(jc->val->value.obj.ovalue)Garbagemark(jc->val->value.obj.ovalue);
          if(jc->val->value.obj.fthis)Garbagemark(jc->val->value.obj.fthis);

       }
       if(jc->throwval && jc->throwval->type == VTP_OBJECT)
       {
          if(jc->throwval->value.obj.ovalue)Garbagemark(jc->throwval->value.obj.ovalue);
          if(jc->throwval->value.obj.fthis)Garbagemark(jc->throwval->value.obj.fthis);

       }

       if(jc->jthis)
       {
           jc->jthis->flags &=~OBJF_USED;
           Garbagemark(jc->jthis);
       }
       if(jc->fscope)
       {
           jc->fscope->flags &=~OBJF_USED;
           Garbagemark(jc->fscope);
       }
       for(this=(struct This *)jlist->lh_Head;this && this->next;this=(struct This *)this->next)
       {
           this->this->flags &=~OBJF_USED;
           Garbagemark(this->this);
       }

       for(jo=(struct Jobject *)objectlist->lh_Head;jo && jo->next;jo=(struct Jobject *)jo->next)
       {
           if(jo->keepnr > 0) Garbagemark(jo);
       }

       for(f=jc->functions.first;f->next;f=f->next)
       {

          Garbagemark(f->arguments);

          Garbagemark(f->def);

          Garbagemark(f->fscope);

          if(f->retval.type==VTP_OBJECT)
          {  Garbagemark(f->retval.value.obj.ovalue);
             if(f->retval.value.obj.fthis) Garbagemark(f->retval.value.obj.fthis);
          }
          for(v=f->local.first;v->next;v=v->next)
          {  if(v->val.type==VTP_OBJECT)
             {
                Garbagemark(v->val.value.obj.ovalue);
                if(v->val.value.obj.fthis) Garbagemark(v->val.value.obj.fthis);
             }
          }
          for(w=f->with.first;w->next;w=w->next)
          {  Garbagemark(w->jo);
          }
       }
       for(jo=(struct Jobject *)objectlist->lh_Head;jo && jo->next;jo=jonext)
       {  jonext=(struct Jobject *)jo->next;
          if(!(jo->flags&OBJF_USED))
          {
             Remove((struct Node *)jo);
             Disposeobject(jo);
          }
       }
   }
}

void Keepobject(struct Jobject *jo,BOOL used)
{  if(!jo) return;
   if(used) jo->keepnr++;
   else if(jo->keepnr) jo->keepnr--;
}
