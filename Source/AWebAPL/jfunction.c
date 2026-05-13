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

/* jfunction.c - AWeb js internal Function object */

#include "awebjs.h"
#include "jprotos.h"

/* Find the arguments array */
static struct Jobject *Findarguments(struct Jcontext *jc)
{  struct Variable *var;
   for(var=jc->functions.first->local.first;var && var->next;var=var->next)
   {  if(var->name && STRIEQUAL(var->name,"arguments")
      && var->val.type==VTP_OBJECT) return var->val.value.obj.ovalue;
   }
   return NULL;
}

/*-----------------------------------------------------------------------*/



static void Functioncall(struct Jcontext *jc)
{
    struct Function *f;
    struct Jobject *jo = jc->jthis;
    struct Jobject *args;
    struct Variable *elt;
    struct Jobject *fthis = NULL;
    struct Elementfunc *func;
    UWORD oldflags;
    struct Jobject *oldthis;

    func = jo->function;
    if((args = Findarguments(jc)))
    {
        if((elt = Arrayelt(args,0)))
        {
            if(elt->val.type == VTP_OBJECT && elt->val.value.obj.ovalue)
            {
                fthis = elt->val.value.obj.ovalue;
            }
        }
    }
    if(fthis == NULL)
    {
        /* null/undefined (or non-object) thisArg -> global object */
        if(jc->fscope) fthis = jc->fscope;
        else fthis = jo;
    }
    if((f = Newfunction(jc,func)))
    {
        struct Variable *var;
        int len = 0;
        int i;

        if(args && Isarray(args))
        {
            if((var = Getproperty(args,"length")))
            {
                Tonumber(&var->val,jc);
                len = (int)var->val.value.nvalue;
            }
            for(i = 1; i < len;i++)
            {
                elt = Arrayelt(args,i);
                if((var = Newvar(NULL,jc)))
                {
                    AddTail((struct List*)&f->local,(struct Node*)var);
                    Asgvalue(&var->val, &elt->val);
                }
                if((var = Addarrayelt(jc,f->arguments)))
                {
                    Asgvalue(&var->val, &elt->val);
                }
            }
        }

        AddHead((struct List *)&jc->functions,(struct Node *)f);

        oldthis    =  jc->jthis;
        jc->jthis  =  fthis;
        oldflags   =  jc->flags;
        jc->flags &= ~EXF_CONSTRUCT;

        Executeelem(jc, (struct Element *)func);

        jc->jthis = oldthis;
        jc->flags = oldflags;
        Remove((struct Node *)f);
        Disposefunction(f);

    }
    Asgvalue(RETVAL(jc),jc->val);
}



static void Functionapply(struct Jcontext *jc)
{
    struct Function *f;
    struct Jobject *jo = jc->jthis;
    struct Jobject *args;
    struct Variable *elt;
    struct Jobject *fthis = NULL;
    struct Jobject *fargs = NULL;
    struct Elementfunc *func;
    UWORD oldflags;
    struct Jobject *oldthis;

    func = jo->function;
    if((args = Findarguments(jc)))
    {
        if((elt = Arrayelt(args,0)))
        {
            if(elt->val.type == VTP_OBJECT && elt->val.value.obj.ovalue)
            {
                fthis = elt->val.value.obj.ovalue;
            }
        }
        if((elt = Arrayelt(args,1)))
        {
            if(elt->val.type == VTP_OBJECT && elt->val.value.obj.ovalue)
            {
                fargs = elt->val.value.obj.ovalue;
            }
        }
    }
    if(fthis == NULL)
    {
        /* null/undefined (or non-object) thisArg -> global object */
        if(jc->fscope) fthis = jc->fscope;
        else fthis = jo;
    }
    if((f = Newfunction(jc,func)))
    {
        struct Variable *var;
        int len = 0;
        int i;

        if(fargs && Isarray(fargs))
        {
            if((var = Getproperty(fargs,"length")))
            {
                Tonumber(&var->val,jc);
                len = (int)var->val.value.nvalue;
            }
            for(i = 0; i < len;i++)
            {
                elt = Arrayelt(fargs,i);
                if((var = Newvar(NULL,jc)))
                {
                    AddTail((struct List*)&f->local,(struct Node*)var);
                    Asgvalue(&var->val, &elt->val);
                }
                if((var = Addarrayelt(jc,f->arguments)))
                {
                    Asgvalue(&var->val, &elt->val);
                }
            }
        }

        AddHead((struct List *)&jc->functions,(struct Node *)f);

        oldthis    =  jc->jthis;
        jc->jthis  =  fthis;
        oldflags   =  jc->flags;
        jc->flags &= ~EXF_CONSTRUCT;

        Executeelem(jc, (struct Element *)func);

        jc->jthis = oldthis;
        jc->flags = oldflags;
        Remove((struct Node *)f);
        Disposefunction(f);

    }
    Asgvalue(RETVAL(jc),jc->val);
}

/*-----------------------------------------------------------------------*/

/* Make (jthis) a new Function object */
/* Turn it into a function, create an Elementfunc with the ET_FUNCEVAL
 * element for the body string */
static void Constructor(struct Jcontext *jc)
{  struct Jobject *jo=jc->jthis;
   struct Jobject *args;
   struct Variable *var;
   struct Elementfunc *func;
   struct Elementnode *enode;
   struct Elementstring *eid;
   struct Element *elt;
   long n;
   if(!(jc->flags & EXF_CONSTRUCT))
   {
      /* Called as function */
      jo = Newobject(jc);
   }

   if(jo)
   {  if(func=ALLOCSTRUCT(Elementfunc,1,0,jc->pool))
      {  NewList((struct List *)&func->subs);
         func->type=ET_FUNCTION;
         func->name=Jdupstr("anonymous",-1,jc->pool);
         if(args=jc->functions.first->arguments)
         {  /* All but the last element in the arguments array are formal parameters */
            for(n=0;Arrayelt(args,n+1);n++)
            {  var=Arrayelt(args,n);
               Tostring(&var->val,jc);
               if(eid=ALLOCSTRUCT(Elementstring,1,0,jc->pool))
               {  eid->type=ET_IDENTIFIER;
                  eid->svalue=Jdupstr(var->val.value.svalue,-1,jc->pool);
                  if(enode=ALLOCSTRUCT(Elementnode,1,0,jc->pool))
                  {  enode->sub=eid;
                     AddTail((struct List *)&func->subs,(struct Node *)enode);
                  }
               }
            }
            /* The last element is the body string */
            if(var=Arrayelt(args,n))
            {  Tostring(&var->val,jc);
               if(eid=ALLOCSTRUCT(Elementstring,1,0,jc->pool))
               {  eid->type=ET_STRING;
                  eid->svalue=Jdupstr(var->val.value.svalue,-1,jc->pool);
               }
               if(elt=ALLOCSTRUCT(Element,1,0,jc->pool))
               {  elt->type=ET_FUNCEVAL;
                  elt->sub1=eid;
                  func->body=elt;
               }
            }
         }
         jo->function=func;
         jo->type=OBJT_FUNCTION;
      }
   }
   if(!(jc->flags & EXF_CONSTRUCT))
   {
      /* Called as function */
      if(jo)
      {
        Asgobject(RETVAL(jc),jo);
      }
   }

}

/*-----------------------------------------------------------------------*/

void Initfunction(struct Jcontext *jc, struct Jobject *jscope)
{  struct Jobject *jo;
   struct Jobject *f;
   struct Variable *prop;
   if((jo=Internalfunction(jc,"Function",(Internfunc *)Constructor,NULL)))
   {
      Keepobject(jo,TRUE);

      Initconstruct(jc,jo,"Object",jc->object);
      Addprototype(jc,jo,Getprototype(jo->constructor));

      if(!jscope)
      {
         jc->function=jo;
         /* Also add to global scope so it can be found by Findvar */
         /* Add to jc->fscope which is used by Jexecute */
         if(jc->fscope)
         {  if((prop = Addproperty(jc->fscope,"Function")))
            {  Asgobject(&prop->val,jo);
               prop->flags |= VARF_DONTDELETE;
               Keepobject(jo,FALSE);
            }
         }
      }
      else
      if((prop = Addproperty(jscope,"Function")))
      {
          Asgobject(&prop->val,jo);
          prop->flags |= VARF_DONTDELETE;
          Keepobject(jo,FALSE);

      }

      if((f=Internalfunction(jc,"apply",(Internfunc *)Functionapply,"thisArg","argArray",NULL)))
      {
          Addtoprototype(jc,jo,f);
      }
      if((f=Internalfunction(jc,"call",(Internfunc *)Functioncall,"thisArg",NULL)))
      {
          Addtoprototype(jc,jo,f);
      }

   }
}
