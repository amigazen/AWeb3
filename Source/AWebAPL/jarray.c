/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
 * Changes Copyright (C) 2025-2026 amigazen project
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

/* jarray.c - AWeb js internal Array object */

#include "awebjs.h"
#include "jprotos.h"
#include <proto/exec.h>
#include <string.h>
#include <ctype.h>

#define CHUNKSIZE   16  /* Array storage is allocated in multiples of this */

struct Array            /* Used as internal object value */
{  long length;            /* current array length of data*/
   struct Variable **array; /* pointer to current array */
   long array_length;       /* current length of storage */
};

struct Tabelt           /* Array of this is used to reverse or sort the array */
{  struct Asortinfo *asi;  /* All tabelts point to the same Asortinfo */
   struct Value val;
   UBYTE *svalue;       /* Result of toString or NULL */
};

struct Asortinfo        /* Common info while sorting */
{  struct Jcontext *jc;
   struct Elementfunc *cfunc; /* Sort compare function or NULL */
   struct Jobject *cthis;     /* (this) for compare function */
};

/*-----------------------------------------------------------------------*/

/* Forward declaration */
static BOOL Arrayohook(struct Objhookdata *h);

/* Convert string to unsigned 32-bit integer */
BOOL Touint32(STRPTR str, ULONG *num)
{
    BOOL success = FALSE;
    STRPTR p = str;
    STRPTR end;
    ULONG result = 0;
    ULONG oldresult = 0;
    long len = 0;
    if(str)
    {
        len = strlen(str);
    }
    if(len > 0)
    {
        end = str + len;
        for(p = str;p<end;p++)
        {
            if(isdigit((int)*p))
            {
                if(result > 429496729)
                {
                    /* overflow */
                    return FALSE;
                }
                oldresult = result;
                result = result * 10 + (*p - '0');
                if(result < oldresult)
                {
                    /* overflow */
                    return FALSE;
                }
            }
            else
            {
                return FALSE;
            }
        }
        if(num)
        {
            *num = result;
        }
        success = TRUE;
    }
    return success;
}

/* Find the numeric value of Nth argument */
static double Numargument(struct Jcontext *jc,long n,BOOL *validp)
{  struct Variable *var;
   double result=0.0;
   BOOL valid=FALSE;
   for(var=jc->functions.first->local.first;n && var->next;var=var->next,n--);
   if(var->next)
   {  Tonumber(&var->val,jc);
      if(var->val.attr==VNA_VALID)
      {  result=var->val.value.nvalue;
         valid=TRUE;
      }
   }
   if(validp) *validp=valid;
   return result;
}

/* Find the string value of Nth argument or NULL */
static UBYTE *Strargument(struct Jcontext *jc,long n)
{  struct Variable *var;
   for(var=jc->functions.first->local.first;n && var->next;var=var->next,n--);
   if(var->next && var->val.type!=VTP_UNDEFINED)
   {  Tostring(&var->val,jc);
      return var->val.value.svalue;
   }
   return NULL;
}

/* Find the arguments array */
static struct Jobject *Findarguments(struct Jcontext *jc)
{  struct Variable *var;
   for(var=jc->functions.first->local.first;var && var->next;var=var->next)
   {  if(var->name && STRIEQUAL(var->name,"arguments")
      && var->val.type==VTP_OBJECT) return var->val.value.obj.ovalue;
   }
   return NULL;
}

/* Make a Tabelt array from an Array object. (asi->jc) must be set. */
struct Tabelt *Maketabelts(struct Asortinfo *asi,struct Jobject *jo,long n)
{  struct Variable *elt;
   struct Tabelt *tabelts;
   long i;
   UBYTE nname[16];
   if(tabelts=ALLOCSTRUCT(Tabelt,n,0,asi->jc->pool))
   {  for(i=0;i<n;i++)
      {  sprintf(nname,"%d",(int)i);
         if(elt=Getproperty(jo,nname))
         {  Asgvalue(&tabelts[i].val,&elt->val);
         }
         tabelts[i].asi=asi;
      }
   }
   return tabelts;
}

/* Assign the Tabelt elements back to the Array object */
void Returntabelts(struct Jobject *jo,struct Tabelt *tabelts,long n)
{  struct Variable *elt;
   UBYTE nname[16];
   long i;
   for(i=0;i<n;i++)
   {  sprintf(nname,"%d",(int)i);
      if(!(elt=Getproperty(jo,nname)))
      {  elt=Addproperty(jo,nname);
      }
      if(elt)
      {  Asgvalue(&elt->val,&tabelts[i].val);
      }
      Clearvalue(&tabelts[i].val);
      if(tabelts[i].svalue) FREE(tabelts[i].svalue);
   }
   FREE(tabelts);
}

/* General qsort compare function */
static int Asortcompare(struct Tabelt *ta,struct Tabelt *tb)
{  struct Value val={0};
   int c;
   if(ta->val.type == VTP_UNDEFINED && tb->val.type == VTP_UNDEFINED)
   {  return 0;
   }
   if(ta->val.type == VTP_UNDEFINED) return(1);
   if(tb->val.type == VTP_UNDEFINED) return(-1);
   if(ta->asi->cfunc)
   {  /* Compare function supplied, use it */
      Callfunctionargs(ta->asi->jc,ta->asi->cfunc,ta->asi->cthis,&ta->val,&tb->val,NULL);
      Tonumber(ta->asi->jc->val,ta->asi->jc);
      c=(int)ta->asi->jc->val->value.nvalue;
   }
   else
   {  /* Convert both elements to string and do string compare */
      if(!ta->svalue)
      {  val.type=0;
         Asgvalue(&val,&ta->val);
         Tostring(&val,ta->asi->jc);
         ta->svalue=Jdupstr(val.value.svalue,-1,ta->asi->jc->pool);
         Clearvalue(&val);
      }
      if(!tb->svalue)
      {  val.type=0;
         Asgvalue(&val,&tb->val);
         Tostring(&val,tb->asi->jc);
         tb->svalue=Jdupstr(val.value.svalue,-1,tb->asi->jc->pool);
         Clearvalue(&val);
      }
      c=strcmp(ta->svalue,tb->svalue);
   }
   return c;
}

/*-----------------------------------------------------------------------*/

/* Do the actual array joining, adapted to be generic */
static void Joinarray(struct Jcontext *jc,UBYTE *sep)
{  struct Jobject *jo=jc->jthis;
   struct Variable *elt;
   struct Variable *length;
   struct Value val={0};
   struct Jbuffer *buf;
   unsigned long n,len,sepl;
   UBYTE nname[16];
   if(!sep) sep=",";
   sepl=strlen(sep);
   val.type=0;
   len = 0;
   if((length = Getproperty(jo,"length")))
   {  Tonumber(&length->val,jc);
      /* Ignore NaN/invalid .length so a corrupt value cannot hang this loop. */
      if(length->val.attr == VNA_VALID)
      {  len = (unsigned long)length->val.value.nvalue;
      }
   }
   if(buf=Newjbuffer(jc->pool))
   {  for(n=0;n<len;n++)
      {  sprintf(nname,"%d",(int)n);
         if((elt=Getproperty(jo,nname)) && elt->val.type != VTP_UNDEFINED)
         {  Asgvalue(&val,&elt->val);
            Tostring(&val,jc);
            if(n>0) Addtojbuffer(buf,sep,sepl);
            Addtojbuffer(buf,val.value.svalue,strlen(val.value.svalue));
         }
         else
         {  if(n>0) Addtojbuffer(buf,sep,sepl);
         }
      }
      Asgstring(RETVAL(jc),buf->buffer?buf->buffer:(UBYTE *)"",jc->pool);
      Freejbuffer(buf);
   }
}

static void Arraytostring(struct Jcontext *jc)
{  struct Jobject *jo = jc->jthis;
   if(jo && jo->internal && Isarray(jo))
   {  Joinarray(jc,",");
   }
   else
   {  /* Note: Runtimeerror function needs to be added to jexe.c */
      /* Runtimeerror(jc,NTE_TYPE,jc->elt,"Array.prototype.toString called on incompatable object"); */
   }
}

/* This function is not generic so we can use Arrayelt etc. */
static void Arraytolocalestring(struct Jcontext *jc)
{  struct Jobject *jo = jc->jthis;
   ULONG len;
   ULONG i;
   struct Jbuffer *buf;
   struct Variable *elt;
   UBYTE *sep = ","; /* Could use group separator from struct Locale */
   if(jo && jo->internal && Isarray(jo))
   {  len = ((struct Array *)jo->internal)->length;
      if((buf = Newjbuffer(jc->pool)))
      {  for(i = 0; i < len; i++)
         {  if((elt = Arrayelt(jo,i)))
            {  struct Value v = {0};
               Asgvalue(&v,&elt->val);
               Toobject(&v,jc);
               if(i> 0) Addtojbuffer(buf,sep,-1);
               if(v.value.obj.ovalue && Callproperty(jc,v.value.obj.ovalue,"toLocaleString") && jc->val->type == VTP_STRING)
               {  Addtojbuffer(buf,jc->val->value.svalue,-1);
               }
               Clearvalue(&v);
            }
            else
            {  if(i > 0) Addtojbuffer(buf,sep,-1);
            }
         }
         Asgstring(RETVAL(jc),buf->buffer?buf->buffer:(UBYTE *)"",jc->pool);
         Freejbuffer(buf);
      }
   }
   /* Note: Runtimeerror function needs to be added */
   /* Runtimeerror(jc,NTE_TYPE,jc->elt,"Array.prototype.toLocaleString called on incompatable object"); */
}

static void Arrayjoin(struct Jcontext *jc)
{  UBYTE *sep=Strargument(jc,0);
   Joinarray(jc,sep);
}

static void Arrayreverse(struct Jcontext *jc)
{  struct Jobject *jo=jc->jthis;
   struct Tabelt *tabelts;
   struct Tabelt te;
   struct Asortinfo asi={0};
   long length,i;
   if(jo && jo->internal)
   {  length=((struct Array *)jo->internal)->length;
      asi.jc=jc;
      if(tabelts=Maketabelts(&asi,jo,length))
      {  /* reverse the table */
         for(i=0;i<length/2;i++)
         {  te=tabelts[i];
            tabelts[i]=tabelts[length-i-1];
            tabelts[length-i-1]=te;
         }
         Returntabelts(jo,tabelts,length);
      }
      Asgobject(RETVAL(jc),jo);
   }
}

/* add all the elements of array1 to array2 */

static void Addelements(struct Jcontext *jc, struct Jobject *to, struct Jobject *from)
{
    /* we assume that we've been passed valid arrays */
    int length = ((struct Array *)from->internal)->length;
    int i;

    for(i = 0; i < length; i++)
    {
        struct Variable *elt1,*elt2;
        if((elt2 = Addarrayelt(jc,to)))
        {
            if((elt1 = Arrayelt(from,i)))
            {
                Asgvalue(&elt2->val,&elt1->val);
            }
        }
    }


}

static void Arraysort(struct Jcontext *jc)
{  struct Jobject *jo=jc->jthis;
   struct Tabelt *tabelts;
   struct Asortinfo asi={0};
   struct Variable *cvar;
   long length;
   if(jo && jo->internal)
   {  length=((struct Array *)jo->internal)->length;
      asi.jc=jc;
      if((cvar=jc->functions.first->local.first) && cvar->next)
      {  Tofunction(&cvar->val,jc);
         if(cvar->val.value.obj.ovalue)
         {  asi.cfunc=cvar->val.value.obj.ovalue->function;
            asi.cthis=cvar->val.value.obj.fthis;
         }
      }
      if(tabelts=Maketabelts(&asi,jo,length))
      {  /* sort the table */
         qsort(tabelts,length,sizeof(struct Tabelt),Asortcompare);
         Returntabelts(jo,tabelts,length);
      }
      Asgobject(RETVAL(jc),jo);
   }
}

static void Arraypop(struct Jcontext *jc)
{  struct Jobject *jo=jc->jthis;
   struct Variable *length,*elt;
   struct Value result={0};
   UBYTE nname[16];
   long len=0;
   if(jo)
   {  if(length=Getproperty(jo,"length"))
      {  Tonumber(&length->val,jc);
         if(length->val.attr==VNA_VALID)
         {  len=(long)length->val.value.nvalue;
            if(len>0)
            {  sprintf(nname,"%d",(int)(len-1));
               if(elt=Getproperty(jo,nname))
               {  /* Copy out value before Deleteownproperty: dense indices live in
                * a->array[] only, not in the object's property list; REMOVE(elt)
                * would corrupt Exec lists and lock the machine. */
                  Asgvalue(&result,&elt->val);
                  if(Deleteownproperty(jo,nname))
                  {  len--;
                     if(jo->internal && jo->hook==Arrayohook)
                     {  ((struct Array *)jo->internal)->length=len;
                     }
                  }
               }
            }
         }
         Asgnumber(&length->val,VNA_VALID,(double)len);
      }
      Asgvalue(RETVAL(jc),&result);
      Clearvalue(&result);
   }
}

static void Arraypush(struct Jcontext *jc)
{  struct Jobject *jo=jc->jthis;
   struct Jobject *args;
   struct Variable *length,*argslen,*arg,*elt;
   long len=0;
   long i;
   UBYTE nname[16];
   if(jo)
   {  if(length=Getproperty(jo,"length"))
      {  Tonumber(&length->val,jc);
         if(length->val.attr==VNA_VALID)
         {  len=(long)length->val.value.nvalue;
         }
      }
      if(args=Findarguments(jc))
      {  if(argslen=Getproperty(args,"length"))
         {  Tonumber(&argslen->val,jc);
            if(argslen->val.attr==VNA_VALID)
            {  long arglen=(long)argslen->val.value.nvalue;
               for(i=0;i<arglen;i++)
               {  if(arg=Arrayelt(args,i))
                  {  if(jo->internal && jo->hook==Arrayohook)
                     {  if(elt=Addarrayelt(jc,jo))
                        {  Asgvalue(&elt->val,&arg->val);
                        }
                     }
                     else
                     {  sprintf(nname,"%d",len);
                        if(elt=Getproperty(jo,nname))
                        {  Asgvalue(&elt->val,&arg->val);
                        }
                        else if(elt=Addproperty(jo,nname))
                        {  Asgvalue(&elt->val,&arg->val);
                        }
                     }
                     len++;
                  }
               }
            }
         }
      }
      if(!(length=Getproperty(jo,"length")))
      {  length=Addproperty(jo,"length");
      }
      if(length)
      {  Asgnumber(&length->val,VNA_VALID,(double)len);
         if(jo->internal && jo->hook==Arrayohook)
         {  ((struct Array *)jo->internal)->length=len;
         }
      }
      Asgnumber(RETVAL(jc),VNA_VALID,(double)len);
   }
}

static void Arrayshift(struct Jcontext *jc)
{  struct Jobject *jo=jc->jthis;
   struct Variable *length,*var,*shift;
   struct Value result={0};
   long len=0;
   long i;
   UBYTE nname[16];
   if(jo)
   {  if(length=Getproperty(jo,"length"))
      {  Tonumber(&length->val,jc);
         if(length->val.attr==VNA_VALID)
         {  len=(long)length->val.value.nvalue;
            if(len>0)
            {  if(var=Getproperty(jo,"0"))
               {  Asgvalue(&result,&var->val);
               }
               for(i=1;i<len;i++)
               {  sprintf(nname,"%d",i);
                  if(var=Getproperty(jo,nname))
                  {  sprintf(nname,"%d",i-1);
                     if(shift=Getproperty(jo,nname))
                     {  Asgvalue(&shift->val,&var->val);
                     }
                     else if(shift=Addproperty(jo,nname))
                     {  Asgvalue(&shift->val,&var->val);
                     }
                  }
                  else
                  {  sprintf(nname,"%d",i-1);
                     if(shift=Getproperty(jo,nname))
                     {  Clearvalue(&shift->val);
                     }
                  }
               }
               len--;
               sprintf(nname,"%d",(int)len);
               /* Same as Arraypop: dense slots are not on the property list. */
               Deleteownproperty(jo,nname);
               if(jo->internal && jo->hook==Arrayohook)
               {  ((struct Array *)jo->internal)->length=len;
               }
            }
         }
         Asgnumber(&length->val,VNA_VALID,(double)len);
      }
      Asgvalue(RETVAL(jc),&result);
      Clearvalue(&result);
   }
}

static void Arrayunshift(struct Jcontext *jc)
{  struct Jobject *jo=jc->jthis;
   struct Jobject *args;
   struct Variable *length,*argslen,*var,*shift;
   long len=0;
   long arglen=0;
   long i;
   UBYTE nname[16];
   if(jo)
   {  if(length=Getproperty(jo,"length"))
      {  Tonumber(&length->val,jc);
         if(length->val.attr==VNA_VALID)
         {  len=(long)length->val.value.nvalue;
         }
      }
      if(args=Findarguments(jc))
      {  if(argslen=Getproperty(args,"length"))
         {  Tonumber(&argslen->val,jc);
            if(argslen->val.attr==VNA_VALID)
            {  arglen=(long)argslen->val.value.nvalue;
               if(arglen>0)
               {  for(i=len;i>0;i--)
                  {  sprintf(nname,"%d",i-1);
                     if(var=Getproperty(jo,nname))
                     {  sprintf(nname,"%d",i+arglen-1);
                        if(shift=Getproperty(jo,nname))
                        {  Asgvalue(&shift->val,&var->val);
                        }
                        else if(shift=Addproperty(jo,nname))
                        {  Asgvalue(&shift->val,&var->val);
                        }
                     }
                     else
                     {  sprintf(nname,"%d",i+arglen-1);
                        if(shift=Getproperty(jo,nname))
                        {  Clearvalue(&shift->val);
                        }
                     }
                  }
                  len+=arglen;
                  if(jo->internal && jo->hook==Arrayohook)
                  {  ((struct Array *)jo->internal)->length+=arglen;
                  }
                  for(i=0;i<arglen;i++)
                  {  if(var=Arrayelt(args,i))
                     {  sprintf(nname,"%d",i);
                        if(shift=Getproperty(jo,nname))
                        {  Asgvalue(&shift->val,&var->val);
                        }
                        else if(shift=Addproperty(jo,nname))
                        {  Asgvalue(&shift->val,&var->val);
                        }
                     }
                  }
               }
            }
         }
      }
      if(!(length=Getproperty(jo,"length")))
      {  length=Addproperty(jo,"length");
      }
      if(length)
      {  Asgnumber(&length->val,VNA_VALID,(double)len);
         if(jo->internal && jo->hook==Arrayohook)
         {  ((struct Array *)jo->internal)->length=len;
         }
      }
      Asgnumber(RETVAL(jc),VNA_VALID,(double)len);
   }
}

static void Arrayconcat(struct Jcontext *jc)
{
    struct Jobject *jo = jc->jthis;
    struct Jobject *newarray = NULL;
    struct Jobject *args;
    struct Variable *elt;
    struct Variable *arg;

    if((newarray = Newarray(jc)))
    {
        if(Isarray(jo))
        {
            Addelements(jc,newarray,jo);
        }
        else
        {
            struct Value v = {0};
            Asgobject(&v,jo);
            Tostring(&v,jc);
            if((elt = Addarrayelt(jc,newarray)))
            {
                Asgvalue(&elt->val,&v);
            }
            Clearvalue(&v);
        }
        /* Now do the same with the args */
        if((args = Findarguments(jc)))
        {
            struct Variable *length;
            if((length = Getproperty(args,"length")) && length->val.type == VTP_NUMBER)
            {
                int l = (int)length->val.value.nvalue;
                int i;
                for(i = 0; i < l ; i++)
                {
                    arg = Arrayelt(args,i);
                    if(arg->val.type == VTP_OBJECT && Isarray(arg->val.value.obj.ovalue))
                    {
                        Addelements(jc,newarray,arg->val.value.obj.ovalue);
                    }
                    else
                    {
                        if((elt = Addarrayelt(jc,newarray)))
                        {
                            Asgvalue(&elt->val,&arg->val);
                        }
                    }
                }
            }
        }

    }
    Asgobject(RETVAL(jc),newarray);
}

static void Arrayslice(struct Jcontext *jc)
{  struct Jobject *jo=jc->jthis;
   struct Jobject *array;
   struct Variable *length,*elt,*newelt;
   long l=0;
   long is,ie;
   BOOL svalid,evalid;
   UBYTE nname[16];
   if(jo)
   {  if(length=Getproperty(jo,"length"))
      {  Tonumber(&length->val,jc);
         if(length->val.attr==VNA_VALID)
         {  l=(long)length->val.value.nvalue;
         }
      }
      if(array=Newarray(jc))
      {  is=(long)Numargument(jc,0,&svalid);
         ie=(long)Numargument(jc,1,&evalid);
         if(svalid)
         {  if(!evalid) ie=l;
            if(is<0) is+=l;
            if(is<0) is=0;
            if(is>l) is=l;
            if(ie<0) ie+=l;
            if(ie<0) ie=0;
            if(ie>l) ie=l;
            while(is<ie)
            {  ((struct Array*)array->internal)->length++;
               sprintf(nname,"%d",(int)is++);
               if(elt=Getproperty(jo,nname))
               {  sprintf(nname,"%d",(int)((struct Array*)array->internal)->length -1);
                  if((newelt = Addproperty(array,nname)))
                  {  Asgvalue(&newelt->val,&elt->val);
                  }
               }
            }
            if((elt = Getproperty(array,"length")))
            {  Asgnumber(&elt->val,VNA_VALID,(double)((struct Array*)array->internal)->length);
            }
            Asgobject(RETVAL(jc),array);
         }
         else
         {  Asgobject(RETVAL(jc),array);
         }
      }
   }
}

static void Arraysplice(struct Jcontext *jc)
{  struct Jobject *jo = jc->jthis;
   struct Jobject *array = NULL;
   struct Jobject *args;
   struct Variable *var;
   struct Variable *elt;
   struct Variable *length;
   ULONG arglen = 0;
   ULONG l = 0;
   ULONG k;
   UBYTE nname[16];
   int is, dc;
   is = dc = 0;
   if((args = Findarguments(jc)))
   {  var = Getproperty(args,"length");
      Tonumber(&var->val,jc);
      arglen = (ULONG)var->val.value.nvalue;
   }
   if((array = Newarray(jc)) && arglen >= 2)
   {  var = Arrayelt(args,0);
      Tonumber(&var->val,jc);
      is = (int)var->val.value.nvalue;
      var = Arrayelt(args,1);
      Tonumber(&var->val,jc);
      dc =(int)var->val.value.nvalue;
      if (dc < 0) dc = 0;
      if((length = Getproperty(jo,"length")))
      {  Tonumber(&length->val,jc);
         l = (ULONG)length->val.value.nvalue;
      }
      if(is < 0) is += l;
      if(is < 0) is = 0;
      if ((l - is) < dc) dc = l - is;
      for(k = is; k < is + dc; k++)
      {  if((elt = Addarrayelt(jc,array)))
         {  sprintf(nname,"%d",(int)k);
            if((var = Getproperty(jo,nname)))
            {  Asgvalue(&elt->val,&var->val);
            }
         }
      }
      arglen -= 2;
      if( arglen < dc)
      {  /* we must close and clear the gap */
         for(k = is; k < l - dc + arglen; k++)
         {
            struct Variable *from,*to;
            sprintf(nname,"%d",(int)(k+dc));
            from = Getproperty(jo,nname);
            sprintf(nname,"%d",(int)(k+arglen));
            if(!(to = Getownproperty(jo,nname)))
            {
                to = Addproperty(jo,nname);
            }
            if(from)
            {
                if(to)
                {
                    Asgvalue(&to->val,&from->val);
                }
            }
            else
            if(to)
            {
                Clearvalue(&to->val);
            }
         }
         for(k =l; k > l-dc + arglen;k--)
         {
            sprintf(nname,"%d",(int)k-1);
            Deleteownproperty(jo,nname);
         }
      }
      if(arglen > dc)
      {  /* we must widen the gap */
         for( k = l - dc; k > is ; k--)
         {
            struct Variable *from, *to;
            sprintf(nname,"%d",(int) k + dc -1);
            from = Getproperty(jo,nname);
            sprintf(nname,"%d",(int) k + arglen -1);
            if(!(to = Getownproperty(jo,nname)))
            {
                to = Addproperty(jo,nname);
            }
            if(from)
            {
                if(to)
                {
                    Asgvalue(&to->val,&from->val);
                }
            }
            else
            {
                if(to)
                {
                    Clearvalue(&to->val);
                }
            }

         }
      }
      /* now we should have the right size "gap" to copy the new items into */
      if(arglen)
         {
            for(k = 0; k < arglen; k++)
            {
                sprintf(nname,"%d",(int) k + is);
                if((elt = Arrayelt(args,k + 2)))
                {
                    if(!(var = Getownproperty(jo,nname)))
                    {
                        var = Addproperty(jo,nname);
                    }
                    if(var)
                    {
                        Asgvalue(&var->val,&elt->val);
                    }
                }
            }
         }
        if(!(length = Getownproperty(jo,"length")))
        {
            length = Addproperty(jo,"length");
        }
        if(length)
        {
            Asgnumber(&length->val,VNA_VALID,(double)(l - dc + arglen));
        }
        if(Isarray(jo))
        {
            ((struct Array *)jo->internal)->length = (l -dc + arglen);
        }
    }
    Asgobject(RETVAL(jc),array);
}

/* When "length" is set, set internal and truncate array as necessary */
/* This only applies to when length is set by javascript code, internally ie here */
/* we must take care to do all this manually */
static BOOL Arraylhook(struct Varhookdata *v)
{  struct Jobject *jo = v->hookdata;
   struct Array *a = jo->internal;
   switch(v->code)
   {  case VHC_GET:
         Asgnumber(&v->value->val, VNA_VALID, (double)a->length);
         return TRUE;
         break;
      case VHC_SET:
         {  ULONG oldlen = a->length;
            ULONG newlen;
            UBYTE nname[16];
            Tonumber(&v->value->val,v->jc);
            if(v->value->val.attr == VNA_VALID)
            {  newlen = (ULONG)v->value->val.value.nvalue;
               if(((double)newlen) != v->value->val.value.nvalue)
               {  /* Note: Runtimeerror function needs to be added to jexe.c */
                  /* Runtimeerror(v->jc,NTE_RANGE,v->jc->elt,"Array length must be positive integer"); */
               }
               if(newlen < oldlen)
               {  int i;
                  for(i = newlen; i < oldlen; i++)
                  {  sprintf(nname,"%d",i);
                     Deleteownproperty(jo,nname);
                  }
               }
               a->length = newlen;
               Asgnumber(&v->var->val,VNA_VALID,(double)newlen);
            }
            return TRUE;
         }
         break;
   }
   return FALSE;
}

/* Add a property to an array, increase .length if necessary */
static BOOL Arrayohook(struct Objhookdata *h)
{  BOOL result=FALSE;
   struct Variable *prop;
   struct Array *a=(h->jo)?h->jo->internal:NULL;
   long n;
   switch(h->code)
   {  case OHC_ADDPROPERTY:
         /* Note: Getownproperty needs to be added to jdata.c */
         /* For now, use Getproperty which may search prototype chain */
         if(!(prop=Getproperty(h->jo,h->name)))
         {  prop=Addproperty(h->jo,h->name);
         }
         if(prop && a)
         {  n=atoi(h->name);
            if(n>=a->length)
            {  a->length=n+1;
               if(prop=Getproperty(h->jo,"length"))
               {  Asgnumber(&prop->val,VNA_VALID,(double)a->length);
               }
            }
         }
         result=TRUE;
         break;
   }
   return result;
}

/* Dispose an Array object */
static void Destructor(struct Array *a)
{  if(a)
   {  if(a->array)
      {  int i;
         for(i=0;i<a->array_length;i++)
         {  if(a->array[i])
            {  Disposevar(a->array[i]);
               a->array[i] = NULL;
            }
         }
         FREE(a->array);
      }
      a->array = NULL;
      FREE(a);
   }
}

/* Make (jthis) a new Array object */
static void Constructor(struct Jcontext *jc)
{  struct Jobject *jo=jc->jthis,*args;
   struct Array *a = NULL;
   struct Variable *prop,*length,*arg,*narg;
   BOOL constructed=FALSE;
   long n;
   if(!(jc->flags & EXF_CONSTRUCT))
   {  jo = Newarray(jc);
      a = (struct Array *)jo->internal;
   }
   else
   if(jo)
   {  if(a=ALLOCSTRUCT(Array,1,0,jc->pool))
      {  jo->internal=a;
         jo->dispose=Destructor;
         jo->hook=Arrayohook;
         a->length=0;
         a->array_length=0;
         a->array = NULL;
      }
   }
   if(a)
   {  if(args=Findarguments(jc))
      {  if((length=Getproperty(args,"length")) && length->val.type==VTP_NUMBER)
         {  if((long)length->val.value.nvalue==1)
            {  /* Only 1 argument, see if it's numeric */
               if((arg=Arrayelt(args,0)) && arg->val.type==VTP_NUMBER)
               {  a->length=(long)arg->val.value.nvalue;
                  constructed=TRUE;
               }
            }
         }
         if(!constructed)
         {  /* Build new array from arguments array */
            for(n=0;arg=Arrayelt(args,n);n++)
            {  if(narg=Addarrayelt(jc,jo))
               {  Asgvalue(&narg->val,&arg->val);
               }
            }
         }
      }
      if(!(prop = Getproperty(jo,"length")))
      {  if(prop=Addproperty(jo,"length"))
         {  prop->flags|=VARF_HIDDEN;
            prop->hook=Arraylhook;
            prop->hookdata = jo;
         }
      }
      if(prop)
      {  Asgnumber(&prop->val,VNA_VALID,(double)a->length);
      }
   }
   if(!(jc->flags & EXF_CONSTRUCT))
   {  Asgobject(RETVAL(jc),jo);
   }
}

/*-----------------------------------------------------------------------*/

void Initarray(struct Jcontext *jc, struct Jobject *jscope)
{  struct Jobject *jo,*f;
   struct Variable *prop;
   if(jo=Internalfunction(jc,"Array",Constructor,"arrayLength",NULL))
   {  /* keep our object to avoid garbage collection triggered by any object creation below */
      Keepobject(jo,TRUE);
      Initconstruct(jc,jo,"Object",jc->object);
      Addprototype(jc,jo,Getprototype(jo->constructor));
      /* If invoke with NULL this must be added to the jcontext */
      /* This is to allow adding arrays to DOM etc before the frame scope */
      /* Has been created, there may be a better way to do this! */
      if(jscope == NULL)
      {  jc->array=jo;
         /* Also add to global scope so it can be found by Findvar */
         /* Add to jc->fscope which is used by Jexecute */
         if(jc->fscope)
         {  if((prop = Addproperty(jc->fscope,"Array")))
            {  Asgobject(&prop->val,jo);
               prop->flags |= VARF_DONTDELETE;
               /* no longer need to keep as assigned to scope */
               Keepobject(jo,FALSE);
            }
         }
      }
      else
      {  if((prop = Addproperty(jscope,"Array")))
         {  Asgobject(&prop->val,jo);
            prop->flags |= VARF_DONTDELETE;
            /* no longer need to keep as assigned to scope */
            Keepobject(jo,FALSE);
         }
      }
      if(f=Internalfunction(jc,"toString",Arraytostring,NULL))
      {  Addtoprototype(jc,jo,f);
      }
      if(f=Internalfunction(jc,"toLocaleString",Arraytolocalestring,NULL))
      {  Addtoprototype(jc,jo,f);
      }
      if(f=Internalfunction(jc,"join",Arrayjoin,"separator",NULL))
      {  Addtoprototype(jc,jo,f);
      }
      if(f=Internalfunction(jc,"reverse",Arrayreverse,NULL))
      {  Addtoprototype(jc,jo,f);
      }
      if(f=Internalfunction(jc,"sort",Arraysort,"compareFunction",NULL))
      {  Addtoprototype(jc,jo,f);
      }
      if(f=Internalfunction(jc,"concat",Arrayconcat,NULL))
      {  Addtoprototype(jc,jo,f);
      }
      if(f=Internalfunction(jc,"pop",Arraypop,NULL))
      {  Addtoprototype(jc,jo,f);
      }
      if(f=Internalfunction(jc,"push",Arraypush,NULL))
      {  Addtoprototype(jc,jo,f);
      }
      if(f=Internalfunction(jc,"shift",Arrayshift,NULL))
      {  Addtoprototype(jc,jo,f);
      }
      if(f=Internalfunction(jc,"unshift",Arrayunshift,NULL))
      {  Addtoprototype(jc,jo,f);
      }
      if(f=Internalfunction(jc,"slice",Arrayslice,"start","end",NULL))
      {  Addtoprototype(jc,jo,f);
      }
      if(f=Internalfunction(jc,"splice",Arraysplice,"start","deleteCount",NULL))
      {  Addtoprototype(jc,jo,f);
      }
   }
}

/* Create a new empty Array object. */
struct Jobject *Newarray(struct Jcontext *jc)
{  struct Jobject *jo;
   struct Array *a;
   struct Variable *prop;
   if(jo=Newobject(jc))
   {  Initconstruct(jc,jo,"Array",NULL);
      if(a=ALLOCSTRUCT(Array,1,0,jc->pool))
      {  jo->internal=a;
         jo->dispose=(Objdisposehookfunc *)Destructor;
         jo->hook=Arrayohook;
         jo->type=OBJT_ARRAY;
         a->length=0;
         a->array_length=0;
         a->array=NULL;
         if(prop=Addproperty(jo,"length"))
         {  prop->flags|=VARF_HIDDEN;
            Asgnumber(&prop->val,VNA_VALID,0.0);
            prop->hook=Arraylhook;
            prop->hookdata = jo;
         }
      }
   }
   return jo;
}

/* Find the nth array element, or NULL */
struct Variable *Arrayelt(struct Jobject *jo,long n)
{  UBYTE nname[16];
   struct Array *a;
   struct Variable *prop=NULL;
   if(jo && jo->internal && jo->hook==Arrayohook)
   {  a=(struct Array *)jo->internal;
      if(n>=0 && n<a->length)
      {  sprintf(nname,"%d",(int)n);
         prop=Getownproperty(jo,nname);
      }
   }
   return prop;
}

/* Add an element to this array */
struct Variable *Addarrayelt(struct Jcontext *jc,struct Jobject *jo)
{  UBYTE nname[16];
   struct Array *a;
   struct Variable *prop=NULL,*length;
   if(jo && jo->internal && jo->hook==Arrayohook)
   {  a=(struct Array *)jo->internal;
      sprintf(nname,"%d",(int)a->length);
      if(prop=Addproperty(jo,nname))
      {  a->length++;
         if(length=Getownproperty(jo,"length"))
         {  Asgnumber(&length->val,VNA_VALID,(double)a->length);
         }
      }
   }
   return prop;
}

/* Array-specific property functions */

struct Variable *_Array_Addproperty(struct Jobject *jo, STRPTR name)
{
    ULONG index;
    struct Variable * var = NULL;
    if(Touint32(name,&index) && jo->internal)
    {
        struct Array *a = jo->internal;

        if(a->array_length > index)
        {
//            if(a->length <= index) a->length = index;
            if(a->array[index])
            {
                Disposevar(a->array[index]);
            }
            a->array[index] = Newvar(name,jo->jc);
            var = a->array[index];
        }
        else
        {
            /* storage is not long enough */
            /* allocate new copy date and replace */
            long new_length = ((index / CHUNKSIZE) + 1) *CHUNKSIZE;
            struct Variable **new_array = ALLOCTYPE(struct Variable *,new_length,MEMF_CLEAR,jo->jc->pool);
            if(new_array)
            {
                if(a->array)
                {
                    /* copy into new_array and free */
                    CopyMem(a->array,new_array,a->array_length * sizeof(struct Variable *));
                    FREE(a->array);
                }
                a->array = new_array;
                a->array_length = new_length;

          //  if(a->length <= index) a->length = index +1;
            if(a->array[index])
            {
                Disposevar(a->array[index]);
            }
                a->array[index] = Newvar(name,jo->jc);
                var = a->array[index];
            }
        }
    }
    else
    {
        var = _Generic_Addproperty(jo,name);
    }
    return var;
}

struct Variable *_Array_Getownproperty(struct Jobject *jo, STRPTR name)
{
    ULONG index;
    if(Touint32(name,&index) && jo->internal)
    {
        struct Array *a = jo->internal;
        if((index < a->length) && (index < a->array_length))
        {
            return a->array[index];
        }
        else
        {
            return NULL;
        }
    }
    else
    {
        return _Generic_Getownproperty(jo,name);
    }

}

BOOL _Array_Deleteownproperty(struct Jobject *jo, STRPTR name)
{
    ULONG index;
    if(Touint32(name,&index) && jo->internal)
    {
        struct Array *a = jo->internal;
        if((index < a->length) && (index < a->array_length))
        {
            if(a->array[index])
            {
                Disposevar(a->array[index]);
                a->array[index]=0;
                return TRUE;
            }
            else
            return FALSE;
        }
    }
    else
    {
        return _Generic_Deleteownproperty(jo,name);
    }
    return FALSE;
}

/* Tests if this object is an array */
BOOL Isarray(struct Jobject *jo)
{  return (BOOL)(jo && jo->internal && jo->hook==Arrayohook);
}
