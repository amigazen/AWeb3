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

/* jmath.c - AWeb js internal Math object */

#include "awebjs.h"
#include "jprotos.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

static BOOL seeded=FALSE;

/*-----------------------------------------------------------------------*/

/* Add a constant property */
static void Addmathproperty(struct Jobject *jo,UBYTE *name,double n)
{  struct Variable *var;
   if(var=Addproperty(jo,name))
   {  Asgnumber(&var->val,VNA_VALID,n);
      var->hook=Constantvhook;
      var->flags |= VARF_DONTDELETE;
   }
}

/* Find the numeric value of Nth argument */
static double Argument(struct Jcontext *jc,long n,UBYTE *attr)
{  struct Variable *var;
   for(var=jc->functions.first->local.first;n && var->next;var=var->next,n--);
   if(var->next)
   {  Tonumber(&var->val,jc);
      if(attr)
      {
        *attr = var->val.attr;
      }
      return var->val.value.nvalue;
   }
   return 0;
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

/*-----------------------------------------------------------------------*/

static void Mathabs(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   n=abs(n);
   Asgnumber(RETVAL(jc),attr,n);
}

static void Mathacos(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   if(attr == VNA_INFINITY || attr == VNA_NEGINFINITY || attr == VNA_NAN)
   {
       attr = VNA_NAN;
   }
   else
   if(n == 1.0)
   {

      n = +0.0;
      attr = VNA_VALID;
   }
   else
   if(n>=-1.0 && n<1.0)
   {
       n=acos(n);
   }
   else
   {
       attr = VNA_NAN;
       n=0.0;
   }
   Asgnumber(RETVAL(jc),attr,n);
}

static void Mathasin(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   if(attr == VNA_INFINITY || attr == VNA_NEGINFINITY || attr == VNA_NAN)
   {
       attr = VNA_NAN;
   }
   else
   if(n == 0.0)
   {
      attr = VNA_VALID;
   }
   else
   if(n>=-1.0 && n<=1.0)
   {
       attr = VNA_VALID;
       n=asin(n);
   }
   else
   {
       attr = VNA_NAN;
       n=0.0;
   }
   Asgnumber(RETVAL(jc),attr,n);
}

static void Mathatan(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   if(attr == VNA_VALID)
   {
       n=atan(n);
   }
   else
   if(attr == VNA_INFINITY)
   {
      n = M_PI_2;
      attr = VNA_VALID;
   }
   else
   if(attr == VNA_NEGINFINITY)
   {
     n = - M_PI_2;
     attr = VNA_VALID;
   }
   Asgnumber(RETVAL(jc),attr,n);
}

static void Mathatan2(struct Jcontext *jc)
{
   UBYTE attrx,attry;
   double x=Argument(jc,1,&attrx);
   double y=Argument(jc,0,&attry);
   double n;
   if(attrx == VNA_NAN || attry == VNA_NAN)
   {
      attrx = VNA_NAN;
   }
   else
   if(x==0.0 && y > 0.0)
   {
       n=M_PI_2;
       attrx = VNA_VALID;
   }
   else
   if(x==0.0 && y < 0.0)
   {
       n= - M_PI_2;
       attrx = VNA_VALID;
   }
   if(y==0.0 && x >= 0.0)
   {
      n = 0.0;
      attrx = VNA_VALID;
   }
   else
   if(y==0.0 && x<=0.0)
   {
       n = M_PI;
       attrx = VNA_VALID;
   }
   else
   if(attrx == VNA_INFINITY)
   {
      attrx = VNA_VALID;
      if(attry == VNA_INFINITY)
      {
        n = 0.25 * M_PI;
      }else
      if(attry == VNA_NEGINFINITY)
      {
        n = - 0.25 * M_PI;
      }else
      {
        n = 0.0;
      }
   }
   else
   if(attrx == VNA_NEGINFINITY)
   {
      attrx = VNA_VALID;
      if(attry == VNA_INFINITY)
      {
        n = 0.75 * M_PI;
      }else
      if(attry == VNA_NEGINFINITY)
      {
        n = - 0.75 * M_PI;
      }else
      if(y < 0.0)
      {
        n = -M_PI;
      }
      else
      {
        n = M_PI;
      }
   }
   else
   {
       n=atan2(y,x);
       attrx = VNA_VALID;
   }
   Asgnumber(RETVAL(jc),attrx,n);
}

static void Mathceil(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   if(attr == VNA_VALID)
   {
       n=ceil(n);
   }
   Asgnumber(RETVAL(jc),attr,n);
}

static void Mathcos(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   if(attr == VNA_VALID)
   {
       n=cos(n);
   }
   else
   {
      attr = VNA_NAN;
   }
   Asgnumber(RETVAL(jc),attr,n);
}

static void Mathexp(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   if(attr == VNA_VALID)
   {
       if(n == 0.0)
       {
           n = 1.0;
       }
       else
       {
           n=exp(n);
       }
   }
   else
   if(attr == VNA_INFINITY)
   {
        /* no change */
   }
   else
   if(attr == VNA_NEGINFINITY)
   {
       attr = VNA_VALID;
       n = 0.0;
   }
   Asgnumber(RETVAL(jc),attr,n);
}

static void Mathfloor(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   if(attr == VNA_VALID)
   {
      n=floor(n);
   }
   Asgnumber(RETVAL(jc),attr,n);
}

static void Mathlog(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   if(attr == VNA_VALID)
   {
       if(n == 0.0)
       {
           attr = VNA_NEGINFINITY;

       }
       else
       if(n>0.0)
       {
           n=log(n);
       }
       else
       if(n<0.0)
       {
        attr = VNA_NAN;
       }
   }else
   if(attr == VNA_NEGINFINITY)
   {
      attr = VNA_NAN;
   }
   Asgnumber(RETVAL(jc),attr,n);
}

static void Mathmax(struct Jcontext *jc)
{
    struct Jobject *args;
    UBYTE attr = VNA_NEGINFINITY;
    double n = -1.0;
    if((args = Findarguments(jc)))
    {
        struct Variable *elt;
        int len;
        int i;
        if((elt = Getproperty(args,"length")) && elt->val.type == VTP_NUMBER)
        {
            len = (int)elt->val.value.nvalue;
            for(i =0; i<len; i++)
            {
                elt = Arrayelt(args,i);
                Tonumber(&elt->val,jc);
                if(elt->val.attr == VNA_NAN)
                {
                    attr = VNA_NAN;
                    break;
                }
                else
                if(elt->val.attr == VNA_INFINITY)
                {
                    attr = VNA_INFINITY;
                    break;
                }
                else
                if(i == 0 || n < elt->val.value.nvalue)
                {
                    n = elt->val.value.nvalue;
                    attr = VNA_VALID;
                }

            }

        }
    }
    Asgnumber(RETVAL(jc),attr,n);
}

static void Mathmin(struct Jcontext *jc)
{
    struct Jobject *args;
    UBYTE attr = VNA_INFINITY;
    double n = 1.0;
    if((args = Findarguments(jc)))
    {
        struct Variable *elt;
        int len;
        int i;
        if((elt = Getproperty(args,"length")) && elt->val.type == VTP_NUMBER)
        {
            len = (int)elt->val.value.nvalue;
            for(i =0; i<len; i++)
            {
                elt = Arrayelt(args,i);
                Tonumber(&elt->val,jc);
                if(elt->val.attr == VNA_NAN)
                {
                    attr = VNA_NAN;
                    break;
                }
                else
                if(elt->val.attr == VNA_NEGINFINITY)
                {
                    attr = VNA_NEGINFINITY;
                    break;
                }
                else
                if(i == 0 || n > elt->val.value.nvalue)
                {
                    n = elt->val.value.nvalue;
                    attr = VNA_VALID;
                }

            }

        }
    }
    Asgnumber(RETVAL(jc),attr,n);

}

static void Mathpow(struct Jcontext *jc)
{
   UBYTE attrx,attry;
   double x=Argument(jc,0,&attrx);
   double y=Argument(jc,1,&attry);
   double n = 0.0;
   double intpart;
   double fractpart;

   if(attry == VNA_NAN)
   {
      attrx = VNA_NAN;
   }
   else
   if(y == 0.0)
   {
      attrx = VNA_VALID;
      n = 1.0;
   }
   else
   if( attrx == VNA_NAN)
   {
      /* no action */
   }
   else
   if(attry == VNA_INFINITY)
   {
        if(abs(x) > 1)
        {
            attrx = VNA_INFINITY;
        }
        else
        if(abs(x) == 1)
        {
            attrx = VNA_NAN;
        }
        else
        {
            attrx = VNA_VALID;
            n = 0.0;
        }
   }
   else
   if(attry == VNA_NEGINFINITY)
   {
        if(abs(x) < 1)
        {
            attrx = VNA_INFINITY;
        }
        else
        if(abs(x) == 1)
        {
            attrx = VNA_NAN;
        }
        else
        {
            attrx = VNA_VALID;
            n = 0.0;
        }
   }
   else
   if(attrx == VNA_INFINITY)
   {
       if(y > 0.0)
       {
           attrx = VNA_INFINITY;
       }
       else
       if(y < 0.0)
       {
           n = 0.0;
           attrx = VNA_VALID;
       }
   }
   else
   if(attrx == VNA_NEGINFINITY)
   {
       if(y < 0.0)
       {
            n = 0.0;
            attrx = VNA_VALID;
       }
       else
       if(y > 0.0)
       {
           fractpart = modf(y,&intpart);
           if(fractpart == 0.0 && fmod(intpart,2.0) == 1.0)
           {
               attrx = VNA_NEGINFINITY;
           }
           else
           {
               attrx = VNA_INFINITY;
           }
       }
   }
   else
   if(x == 0.0)
   {
       if(y > 0)
       {
           attrx = VNA_VALID;
           n = 0.0;
       }
       if(y < 0)
       {

       }
   }
   else
   if(x < 0.0 && modf(y,&intpart))
   {
           attrx = VNA_NAN;
   }
   else
   {
       n=pow(x,y);
       attrx = VNA_VALID;
   }
   Asgnumber(RETVAL(jc),attrx,n);
}

static void Mathrandom(struct Jcontext *jc)
{  double n;
   n=drand48();
   Asgnumber(RETVAL(jc),VNA_VALID,n);
}

static void Mathround(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   double i=floor(n + 0.5);

   Asgnumber(RETVAL(jc),attr,i);
}

static void Mathsin(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   if(attr == VNA_NAN || attr == VNA_INFINITY || attr == VNA_NEGINFINITY)
   {
      attr = VNA_NAN;
   }
   else
   {
       n=sin(n);
   }
   Asgnumber(RETVAL(jc),attr,n);
}

static void Mathsqrt(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   if(n < 0.0 || attr == VNA_NEGINFINITY || attr == VNA_NAN)
   {
       attr = VNA_NAN;
   }
   else
   {
      n=sqrt(n);
   }
   Asgnumber(RETVAL(jc),attr,n);
}

static void Mathtan(struct Jcontext *jc)
{
   UBYTE attr;
   double n=Argument(jc,0,&attr);
   if(attr != VNA_VALID)
   {
       attr = VNA_NAN;
   }
   else
   {
       n=tan(n);
   }
   Asgnumber(RETVAL(jc),attr,n);
}

/*-----------------------------------------------------------------------*/

void Initmath(struct Jcontext *jc, struct Jobject *jscope)
{  struct Jobject *jo,*jp;
   struct Variable *var;
   if(!seeded)
   {  srand48((long)fmod(Jmillis(),(double)0x7fffffff));
      seeded=TRUE;
   }
   if(jo=Newobject(jc))
   {
      jo->type=OBJT_MATH;

      if(jscope)
      {  if((var=Addproperty(jscope,"Math")))
         {  Asgobject(&var->val,jo);
            var->flags |= VARF_DONTDELETE;
         }
      }
      else
      {  /* Add to global scope so it can be found by Findvar */
         /* Add to jc->fscope which is used by Jexecute */
         if(jc->fscope)
         {  if((var=Addproperty(jc->fscope,"Math")))
            {  Asgobject(&var->val,jo);
               var->flags |= VARF_DONTDELETE;
            }
         }
      }

      Addmathproperty(jo,"E",exp(1.0));
      Addmathproperty(jo,"LN2",log(2.0));
      Addmathproperty(jo,"LN10",log(10.0));
      Addmathproperty(jo,"LOG2E",1.0/log(2.0));
      Addmathproperty(jo,"LOG10E",1.0/log(10.0));
      Addmathproperty(jo,"PI",PI);
      Addmathproperty(jo,"SQRT1_2",sqrt(0.5));
      Addmathproperty(jo,"SQRT2",sqrt(2.0));


      if(jp=Internalfunction(jc,"abs",(Internfunc *)Mathabs,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }
      if(jp=Internalfunction(jc,"acos",(Internfunc *)Mathacos,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"asin",(Internfunc *)Mathasin,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"atan",(Internfunc *)Mathatan,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"atan2",(Internfunc *)Mathatan2,"xCoord","yCoord",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"ceil",(Internfunc *)Mathceil,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"cos",(Internfunc *)Mathcos,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"exp",(Internfunc *)Mathexp,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"floor",(Internfunc *)Mathfloor,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"log",(Internfunc *)Mathlog,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"max",(Internfunc *)Mathmax,"number1","number2",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"min",(Internfunc *)Mathmin,"number1","number2",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"pow",(Internfunc *)Mathpow,"base","exponent",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"random",(Internfunc *)Mathrandom,NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"round",(Internfunc *)Mathround,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"sin",(Internfunc *)Mathsin,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"sqrt",(Internfunc *)Mathsqrt,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

      if(jp=Internalfunction(jc,"tan",(Internfunc *)Mathtan,"number",NULL))
      {  Addinternalproperty(jc,jo,jp);
      }

   }
}
