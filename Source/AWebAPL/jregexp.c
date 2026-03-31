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

/* jregexp.c - AWeb js internal regular expression object */

#include "awebjs.h"
#include "jprotos.h"
#include "regexp.h"

/* PCRE function declarations */
extern int pcre_fullinfo(const struct pcre *, const void *, int, void *);
extern int pcre_exec(const struct pcre *, const void *, const char *, int, int, int, int *, int);
extern struct pcre *pcre_compile(const char *, int, const char **, int *, const unsigned char *);
extern void pcre_free(void *);

/*-----------------------------------------------------------------------*/
/* tools */
/*-----------------------------------------------------------------------*/

/* Find the string value of Nth argument */
static UBYTE *Strargument(struct Jcontext *jc,long n)
{  struct Variable *var;
   for(var=jc->functions.first->local.first;n && var->next;var=var->next,n--);
   if(var->next)
   {  Tostring(&var->val,jc);
      return var->val.value.svalue;
   }
   return "";
}

/*-----------------------------------------------------------------------*/
/* Core Functions */
/*-----------------------------------------------------------------------*/

/* Apply a regular expression object to a string */
/* to be called by Regexexec and externally by String methods */

struct Jobject *Applyregexp(struct Jcontext *jc, struct Jobject *jo, UBYTE *match)
{
    struct Jobject *result = NULL;
    struct Regexp *re = (struct Regexp *)jo->internal;
    int rc;
    int capcnt;
    int ovecsize = 0;
    int *ovector = NULL;
    if(re && jo->type == OBJT_REGEXP )
    {
        BOOL global = (re->flags & REF_GLOBAL?TRUE:FALSE) ;
        int lastindex = (global?re->lastIndex:0);

        /* find out how many capturing substrings */

        rc = pcre_fullinfo(re->compiled,(const void *)NULL,PCRE_INFO_CAPTURECOUNT,&capcnt);

        ovecsize=3*(capcnt +1);
        ovector = ALLOCTYPE(int,ovecsize,0,jc->pool);

        if((lastindex > strlen(match)) || (ovector == NULL))
        {
            rc = -1;
        }
        else
        {
            rc = pcre_exec(re->compiled,(const void *)NULL,match,strlen(match),lastindex,0,ovector,ovecsize);
        }

        if (rc >=0)
        {
            struct Variable *var;
            int i;

            if (global) re->lastIndex = ovector[1];
            result = Newarray(jc);
            for(i=0;i<=capcnt;i++)
            {
                if(ovector[i*2] >=0)
                {
                    if((var = Addarrayelt(jc,result)))
                    {
                       Asgstringlen(&var->val,(UBYTE *)(match + ovector[i*2]),ovector[i*2 +1] - ovector[i*2],jc->pool);
                    }
                }
                else
                {
                    if((var = Addarrayelt(jc,result)))
                    {
                        Asgstring(&var->val,"",jc->pool);
                    }
                }
            }
            /* set instance index */
            if((var = Addproperty(result,"index")))
            {
                Asgnumber(&var->val,VNA_VALID,(double)ovector[0]);
                var->hook = Constantvhook;
            }
            /* update global regexp object index */
            /* deprecated by ECMA and javascript 1.5included for MSIE compat */
            if((var = Getownproperty(jc->regexp,"index")))
            {
                Asgnumber(&var->val,VNA_VALID,(double)ovector[0]);
            }

            if((var = Addproperty(result,"input")))
            {

                Asgstring(&var->val,match,jc->pool);
                var->hook = Constantvhook;
            }

            /* lastIndex property of exec resut is MSIE specific */
            /* do it anyway now consider adding option in future */

            if((var = Addproperty(result,"lastIndex")))
            {

                Asgnumber(&var->val,VNA_VALID,(double)re->lastIndex);
                var->hook = Constantvhook;
            }


            /* update global regexp object index */
            /* deprecated by ECMA and javascript 1.5 included for MSIE compat */

            if((var = Getownproperty(jc->regexp,"index")))
            {
                Asgnumber(&var->val,VNA_VALID,(double)ovector[0]);
            }
            if((var = Getownproperty(jc->regexp, "lastIndex")))
            {
                Asgnumber(&var->val,VNA_VALID,(double)re->lastIndex);
            }
            if((var = Getownproperty(jc->regexp,"leftContext")))
            {
                Asgstringlen(&var->val,match,ovector[0],jc->pool);
            }
            if((var = Getownproperty(jc->regexp,"rightContext")))
            {
                Asgstringlen(&var->val,match+ovector[1],strlen(match) - ovector[0],jc->pool);
            }
            if((var = Getownproperty(jc->regexp,"input")))
            {
                Asgstring(&var->val,match,jc->pool);
            }
            if((var = Getownproperty(jc->regexp,"lastMatch")))
            {
                Asgstringlen(&var->val,match+ovector[0],ovector[1] - ovector[0],jc->pool);
            }
            for(i=1;i<=9;i++)
            {
                UBYTE varname[3] = {0};
                int captures = rc;
                sprintf(varname,"$%d",i);

                if((var=Getownproperty(jc->regexp,varname)))
                {
                    if(ovector[i*2] >=0 && i <= capcnt)
                    {
                        captures--;
                        Asgstringlen(&var->val,(UBYTE *)(match + ovector[i*2]),ovector[i*2 +1] - ovector[i*2],jc->pool);
                        if(captures==1)
                        {
                            if((var = Getownproperty(jc->regexp,"lastParen")))
                            {
                                Asgstringlen(&var->val,(UBYTE *)(match + ovector[i*2]),ovector[i*2 +1] - ovector[i*2],jc->pool);
                            }

                        }
                    }
                    else
                    {
                        Asgstring(&var->val,"",jc->pool);
                    }
                }
            }


        }

    }
    else
    {
        Runtimeerror(jc,NTE_TYPE,jc->elt,"Object is not a Regular Expression");
    }
    if(ovector) FREE(ovector);
    return result;
}


struct Jobject *Splitregexp(struct Jcontext *jc, struct Jobject *jo, UBYTE *match, unsigned int limit)
{
    struct Jobject *result = NULL;
    struct Regexp *re = (struct Regexp *)jo->internal;
    int rc;
    int capcnt;
    int ovecsize = 0;
    int *ovector;
    int lastindex = 0;

    /* find out how many capturing substrings */

    rc = pcre_fullinfo(re->compiled,(const void *)NULL,PCRE_INFO_CAPTURECOUNT,&capcnt);

    ovecsize=3*(capcnt +1);
    ovector = ALLOCTYPE(int,ovecsize,0,jc->pool);

    result = Newarray(jc);

    while(limit > 0 && result && ovector)
    {
        struct Variable *var;
        int i;

        rc = pcre_exec(re->compiled,(const void *)NULL,match,strlen(match),lastindex,0,ovector,ovecsize);
        if(rc < 0) break;

    //    if (global) re->lastIndex = ovector[1];

        for(i=0;i<=capcnt;i++)
        {
            if (limit -- == 0) break;

            if(ovector[i*2] >=0)
            {
                if((var = Addarrayelt(jc,result)))
                {
                    if(i == 0)
                    {
                        Asgstringlen(&var->val,(UBYTE *)(match + lastindex),ovector[i*2] - lastindex,jc->pool);
                    }
                    else
                    {
                        Asgstringlen(&var->val,(UBYTE *)(match + ovector[i*2]),ovector[i*2 +1] - ovector[i*2],jc->pool);
                    }
                }
            }
            else
            {
                if((var = Addarrayelt(jc,result)))
                {
                    Asgstring(&var->val,"",jc->pool);
                }
            }
        }
        lastindex = ovector[1];
        if(lastindex == 0) break;

    }

    if(ovector) FREE(ovector);
    return result;
}



/*-----------------------------------------------------------------------*/
/*  Properties */
/*-----------------------------------------------------------------------*/

static BOOL Propertylastindex(struct Varhookdata *vd)
{
    BOOL result = FALSE;
    struct Regexp *re = vd->hookdata;
    if(re)
    {
        switch(vd->code)
        {
            case VHC_SET:
                Tonumber(&vd->value->val,vd->jc);
                if (vd->value->val.attr==VNA_VALID) re->lastIndex = vd->value->val.value.nvalue;
                result=TRUE;
                break;
            case VHC_GET:
                Asgnumber(&vd->value->val,VNA_VALID,(double)re->lastIndex);
                result=TRUE;
                break;
        }
    }
    return result;
}



/*-----------------------------------------------------------------------*/
/*  Methods */
/*-----------------------------------------------------------------------*/

/* Get string value of (jthis) */
static void Regexptostring(struct Jcontext *jc)
{  struct Jobject *jo=jc->jthis;
   UBYTE *p="";
   struct Jbuffer *buf;
   buf = Newjbuffer(jc->pool);
   if(jo && jo->internal)
   {
       Addtojbuffer(buf,"/",1);
       p=((struct Regexp *)jo->internal)->pattern;
       Addtojbuffer(buf,p,strlen(p));
       Addtojbuffer(buf,"/",1);
       if( ((struct Regexp *)jo->internal)->flags & REF_GLOBAL) Addtojbuffer(buf,"g",1);
       if( ((struct Regexp *)jo->internal)->flags & REF_MULTI)  Addtojbuffer(buf,"m",1);
       if( ((struct Regexp *)jo->internal)->flags & REF_NOCASE) Addtojbuffer(buf,"i",1);

   }
   Asgstring(RETVAL(jc),buf->buffer?buf->buffer:(UBYTE *)"",jc->pool);
   Freejbuffer(buf);
}

static void Regexpexec(struct Jcontext *jc)
{
    struct Jobject *jo = jc->jthis;
    UBYTE *match = Strargument(jc,0);

    struct Jobject *result = Applyregexp(jc,jo,match);

    if(!result && jo->internal && jo->type == OBJT_REGEXP) ((struct Regexp*)jo->internal)->lastIndex = 0;
    Asgobject(RETVAL(jc),result);
}

static void Regexptest(struct Jcontext *jc)
{
    struct Jobject *jo = jc->jthis;
    UBYTE *match = Strargument(jc,0);
    BOOL test = FALSE;

    struct Jobject *result = Applyregexp(jc,jo,match);
    if (result)
    {
        test = TRUE;
        /* we don't want the result so remove it then dispose it*/
        Remove((struct Node *)result);
        Disposeobject(result);
    }

    Asgboolean(RETVAL(jc),test);
}

/* recompile with a new regexp (added for MSIE compat) */


static void Regexpcompile(struct Jcontext *jc)
{
    struct Jobject *jo = jc->jthis;
    struct Regexp *re = jo->internal;
    struct Variable *prop;
    struct Variable *arg;
    UBYTE *p,*f;

    if(re)
    {
        if (re->compiled) pcre_free((void *)re->compiled);
        if (re->pattern) FREE(re->pattern);
        re->flags = 0;
    }

    arg=jc->functions.first->local.first;
    if(arg->next && arg->val.type!=VTP_UNDEFINED)
    {
        Tostring(&arg->val,jc);
        p=arg->val.value.svalue;
        arg = arg->next;
        if(arg->next && arg->val.type!=VTP_UNDEFINED)
        {
            Tostring(&arg->val,jc);
            f=arg->val.value.svalue;
        }
        else
        {
            f = "";
        }
    }
    else
    {
       p="";
       f="";
    }

    if(jo)
    {
        if(re)
          {
             int pcre_flags = 0;
             UBYTE *errstr = NULL;
             int   errpos = 0;

             re->pattern=Jdupstr(p,-1,jc->pool);

             if(strchr(f,'g')) re->flags |= REF_GLOBAL;
             if(strchr(f,'m')) re->flags |= REF_MULTI;
             if(strchr(f,'i')) re->flags |= REF_NOCASE;

             if(re->flags & REF_MULTI) pcre_flags |= PCRE_MULTILINE;
             if(re->flags & REF_NOCASE) pcre_flags |= PCRE_CASELESS;
             /* global flag is handled in exec */

             re->compiled = (struct pcre *)pcre_compile(re->pattern,pcre_flags,(char **)&errstr,&errpos,NULL);
             if(!re->compiled) Runtimeerror(jc,NTE_SYNTAX,jc->elt,"Regular Expression /%s/: %s",re->pattern,errstr);

             if((prop = Getownproperty(jo,"global")))
             {
                 BOOL global = FALSE;
                 if(re->flags & REF_GLOBAL) global = TRUE;
                 Asgboolean(&prop->val,global);
                 prop->hook=Constantvhook;
             }
             if((prop = Getownproperty(jo,"ignoreCase")))
             {
                 BOOL ic = FALSE;
                 if(re->flags & REF_NOCASE) ic = TRUE;
                 Asgboolean(&prop->val,ic);
             }
             if((prop = Getownproperty(jo,"multiline")))
             {
                 BOOL multi = FALSE;
                 if(re->flags & REF_MULTI) multi = TRUE;
                 Asgboolean(&prop->val,multi);
             }
             if((prop = Getownproperty(jo,"source")))
             {
                 Asgstring(&prop->val,re->pattern,jc->pool);
             }


         }
      }
      Asgobject(RETVAL(jc),jo);

}
/*-----------------------------------------------------------------------*/




/* Dispose Of RegExp object */

static void Destructor(struct Regexp *re)
{
    if(re)
    {
        if (re->pattern) FREE(re->pattern);
        if (re->compiled) pcre_free((void *)re->compiled);
        FREE(re);
    }
}

/* make (jthis) a new RegExp object */
static void Constructor(struct Jcontext *jc)
{
    struct Jobject *jo=jc->jthis;
    struct Regexp *re;
    struct Variable *prop;
    struct Variable *arg;
    UBYTE *p,*f;
    BOOL isregexp = FALSE;

    arg=jc->functions.first->local.first;

    if(arg->val.type == VTP_OBJECT)
    {
        isregexp = (arg->val.value.obj.ovalue->type == OBJT_REGEXP);
    }

    if(isregexp && !(jc->flags & EXF_CONSTRUCT))
    {
        Asgobject(RETVAL(jc),arg->val.value.obj.ovalue);
        return;
    }


    if(arg->next && arg->val.type!=VTP_UNDEFINED)
    {
        Tostring(&arg->val,jc);
        p=arg->val.value.svalue;
        arg = arg->next;
        if(arg->next && arg->val.type!=VTP_UNDEFINED)
        {
            Tostring(&arg->val,jc);
            f=arg->val.value.svalue;
        }
        else
        {
            f = "";
        }
    }
    else
    {
       p="";
       f="";
    }

    if(jc->flags & EXF_CONSTRUCT)
    {  if(jo)
       {  if(re=ALLOCSTRUCT(Regexp,1,0,jc->pool))
          {
             int pcre_flags = 0;
             UBYTE *errstr = NULL;
             int   errpos = 0;

             jo->internal=re;
             jo->dispose=(Objdisposehookfunc *)Destructor;
             jo->type = OBJT_REGEXP;

             re->pattern=Jdupstr(p,-1,jc->pool);

             if(strchr(f,'g')) re->flags |= REF_GLOBAL;
             if(strchr(f,'m')) re->flags |= REF_MULTI;
             if(strchr(f,'i')) re->flags |= REF_NOCASE;


             if(re->flags & REF_MULTI) pcre_flags |= PCRE_MULTILINE;
             if(re->flags & REF_NOCASE) pcre_flags |= PCRE_CASELESS;
             /* global flag is handled in exec */

             re->compiled = (struct pcre *)pcre_compile(re->pattern,pcre_flags,(char **)&errstr,&errpos,NULL);
             if(!re->compiled) Runtimeerror(jc,NTE_SYNTAX,jc->elt,"Regular Expression /%s/: %s",re->pattern,errstr);

             if((prop = Addproperty(jo,"global")))
             {
                 BOOL global = FALSE;
                 if(re->flags & REF_GLOBAL) global = TRUE;
                 Asgboolean(&prop->val,global);
                 prop->hook=Constantvhook;
             }
             if((prop = Addproperty(jo,"ignoreCase")))
             {
                 BOOL ic = FALSE;
                 if(re->flags & REF_NOCASE) ic = TRUE;
                 Asgboolean(&prop->val,ic);
                 prop->hook=Constantvhook;
             }
             if((prop = Addproperty(jo,"multiline")))
             {
                 BOOL multi = FALSE;
                 if(re->flags & REF_MULTI) multi = TRUE;
                 Asgboolean(&prop->val,multi);
                 prop->hook=Constantvhook;
             }
             if((prop = Addproperty(jo,"source")))
             {
                 Asgstring(&prop->val,re->pattern,jc->pool);
                 prop->hook=Constantvhook;
             }
             if((prop = Addproperty(jo,"lastIndex")))
             {
                 prop->hook=(Varhookfunc *)Propertylastindex;
                 prop->hookdata = re;
             }


         }
      }
   }
   else
   {
      /* Not called as constructor (ie called as function ) */
      /* ECMA requires us either to return the first arg if it's a regexp */
      /* or pass both args to the constructor and return the resulting regexp */

      /* If the arg was a regexp it have been caught above */

          struct Jobject *re = Newregexp(jc,p,f);
          Asgobject(RETVAL(jc),re);


   }

}

/*---------------------------------------------------------------------------------*/


void Initregexp(struct Jcontext *jc, struct Jobject *jscope)
{
    struct Jobject *jo,*f;
    struct Variable *prop;
    struct Variable *syn;
    int i;
    if(jo = Internalfunction(jc,"RegExp",(Internfunc *)Constructor,"pattern","flags",NULL))
    {
        Keepobject(jo,TRUE);

        Initconstruct(jc,jo,"Object",jc->object);
        Addprototype(jc,jo,Getprototype(jo->constructor));

        //Addglobalfunction(jc,jo);
        if(!jscope)
        {
           jc->regexp=jo;
           /* Add to jc->fscope which is used by Jexecute */
           if(jc->fscope)
           {  if((prop = Addproperty(jc->fscope,"RegExp")))
              {  Asgobject(&prop->val,jo);
                 prop->flags |= VARF_DONTDELETE;
                 Keepobject(jo,FALSE);
              }
           }
        }
        else
        {  if((prop = Addproperty(jscope,"RegExp")))
           {  Asgobject(&prop->val,jo);
              prop->flags |= VARF_DONTDELETE;
              Keepobject(jo,FALSE);
           }
           /* Also ensure it's in jc->functions.first->fscope for Findvar */
           if(jc->functions.first && jc->functions.first->fscope && jc->functions.first->fscope != jscope)
           {  if((prop = Addproperty(jc->functions.first->fscope,"RegExp")))
              {  Asgobject(&prop->val,jo);
                 prop->flags |= VARF_DONTDELETE;
              }
           }
        }

        /* setup methods */

        if(f=Internalfunction(jc,"toString",(Internfunc *)Regexptostring,NULL))
        {
            Addtoprototype(jc,jo,f);
        }
        if(f=Internalfunction(jc,"valueOf",(Internfunc *)Regexptostring,NULL))
        {
            Addtoprototype(jc,jo,f);
        }
        if(f=Internalfunction(jc,"test",(Internfunc *)Regexptest,"stringValue",NULL))
        {
            Addtoprototype(jc,jo,f);
        }
        if(f=Internalfunction(jc,"exec",(Internfunc *)Regexpexec,"stringValue",NULL))
        {
            Addtoprototype(jc,jo,f);
        }
        if(f=Internalfunction(jc,"compile",(Internfunc *)Regexpcompile,"pattern","flags",NULL))
        {
            Addtoprototype(jc,jo,f);
        }


        /* set up properties */
        /* these properties are depricated under ECMA and  */
        /* javscript 1.5 but inluded for legacy and MSIE compatabilty */


        if((prop = Addproperty(jo,"index")))
        {
            Asgnumber(&prop->val,VNA_VALID,(double)-1);
            prop->hook = Constantvhook;
        }
        if((prop = Addproperty(jo,"lastIndex")))
        {
            Asgnumber(&prop->val,VNA_VALID,0.0);
            prop->hook = Constantvhook;
        }
        if((prop = Addproperty(jo,"leftContext")))
        {
            Asgstring(&prop->val,"",jc->pool);
            prop->hook = Constantvhook;
            if((syn = Addproperty(jo,"$`")))
            {
                syn->flags = VARF_SYNONYM;
                syn->hookdata = prop;
            }
        }
        if((prop = Addproperty(jo,"rightContext")))
        {
            Asgstring(&prop->val,"",jc->pool);
            prop->hook = Constantvhook;
            if((syn = Addproperty(jo,"$'")))
            {
                syn->flags = VARF_SYNONYM;
                syn->hookdata = prop;
            }
        }
        if((prop = Addproperty(jo,"input")))
        {
            Asgstring(&prop->val,"",jc->pool);
            prop->hook = Constantvhook;
            if((syn = Addproperty(jo,"$_")))
            {
                syn->flags = VARF_SYNONYM;
                syn->hookdata = prop;
            }
        }
        if((prop = Addproperty(jo,"lastMatch")))
        {
            Asgstring(&prop->val,"",jc->pool);
            prop->hook = Constantvhook;
            if((syn = Addproperty(jo,"$&")))
            {
                syn->flags = VARF_SYNONYM;
                syn->hookdata = prop;
            }
        }
        if((prop = Addproperty(jo,"lastParen")))
        {
            Asgstring(&prop->val,"",jc->pool);
            prop->hook = Constantvhook;
            if((syn = Addproperty(jo,"$+")))
            {
                syn->flags = VARF_SYNONYM;
                syn->hookdata = prop;
            }
        }
        for(i=1;i<=9;i++)
        {
            UBYTE varname[3] = {0};
            sprintf(varname,"$%d",i);
            if((prop = Addproperty(jo,varname)))
            {
                Asgstring(&prop->val,"",jc->pool);
                prop->hook = Constantvhook;
            }

        }


    }

}

/* Create a new RegExp object. */
struct Jobject *Newregexp(struct Jcontext *jc,UBYTE *pattern, UBYTE *flags)
{
   struct Jobject *jo;
   struct Regexp *re;
   struct Variable *prop;
   if(jo=Newobject(jc))
   {  Initconstruct(jc,jo,"RegExp",jc->regexp);
      if(re=ALLOCSTRUCT(Regexp,1,0,jc->pool))
      {
         int pcre_flags = 0;
         UBYTE *errstr = NULL;
         int   errpos = 0;


         jo->internal=re;
         jo->dispose=(Objdisposehookfunc *)Destructor;
         jo->type = OBJT_REGEXP;
         re->pattern=Jdupstr(pattern,-1,jc->pool);
         if(flags)
         {
             if(strchr(flags,'g')) re->flags |= REF_GLOBAL;
             if(strchr(flags,'m')) re->flags |= REF_MULTI;
             if(strchr(flags,'i')) re->flags |= REF_NOCASE;
         }
         if(re->flags & REF_MULTI) pcre_flags |= PCRE_MULTILINE;
         if(re->flags & REF_NOCASE) pcre_flags |= PCRE_CASELESS;
         /* global flag is handled in exec */

         re->compiled = pcre_compile(re->pattern,pcre_flags,&errstr,&errpos,NULL);
             if(!re->compiled) Runtimeerror(jc,NTE_SYNTAX,jc->elt,"Regular Expression /%s/: %s",re->pattern,errstr);

         if((prop = Addproperty(jo,"global")))
         {
             BOOL global = FALSE;
             if(re->flags & REF_GLOBAL) global = TRUE;
             Asgboolean(&prop->val,global);
             prop->hook=Constantvhook;
         }
         if((prop = Addproperty(jo,"ignoreCase")))
         {
             BOOL ic = FALSE;
             if(re->flags & REF_NOCASE) ic = TRUE;
             Asgboolean(&prop->val,ic);
             prop->hook=Constantvhook;
         }
         if((prop = Addproperty(jo,"multiline")))
         {
             BOOL multi = FALSE;
             if(re->flags & REF_MULTI) multi = TRUE;
             Asgboolean(&prop->val,multi);
             prop->hook=Constantvhook;
         }
         if((prop = Addproperty(jo,"source")))
         {
             Asgstring(&prop->val,re->pattern,jc->pool);
             prop->hook=Constantvhook;
         }

         if((prop = Addproperty(jo,"lastIndex")))
         {
             prop->hook=(Varhookfunc *)Propertylastindex;
             prop->hookdata = re;
         }


      }
   }
   return jo;
}

