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

/* jcomp.c - AWeb js compiler */

#include "awebjs.h"
#include "jprotos.h"
#include "jbytecode.h"
#include <proto/dos.h>

/*-----------------------------------------------------------------------*/

/* Same flag as jdata.c GC traces (defined in jslib.c). */
extern BOOL httpdebug;

/* GC/disposal breadcrumbs: DOS Write only (see jdata.c). */
static void Jcomp_log_line(UBYTE *s)
{
   BPTR fh;
   LONG n;

   if(!httpdebug)
   {
      return;
   }
   if(!s)
   {
      return;
   }
   fh=Output();
   if(!fh)
   {
      return;
   }
   n=(LONG)strlen((char *)s);
   if(n>0)
   {
      Write(fh,(APTR)s,n);
   }
}

/* Dispose-safety: Elements may be reachable through multiple parents (shared subtree).
 * Disposing the same Element* twice is a double-free/use-after-free. We prevent that
 * by tracking Element* addresses per Jdispose() run.
 *
 * This is a functional fix (not just debug logging): the first visit owns disposal,
 * later visits are ignored. */
#ifndef JCOMP_DISPOSE_SEEN_SIZE
#define JCOMP_DISPOSE_SEEN_SIZE 2048
#endif

struct Jcomp_seen_ent
{
   ULONG ptr;
   ULONG runid;
};

static struct Jcomp_seen_ent jcomp_seen[JCOMP_DISPOSE_SEEN_SIZE];
static ULONG jcomp_dispose_runid;

static void Jcomp_dispose_newrun(void)
{
   jcomp_dispose_runid++;
   if(jcomp_dispose_runid==0UL)
   {
      /* Avoid zero as a sentinel. */
      jcomp_dispose_runid=1UL;
   }
}

static ULONG Jcomp_hash_ptr(ULONG p)
{
   /* Simple pointer hash (alignment-heavy) */
   p ^= (p >> 4);
   p ^= (p >> 9);
   return p;
}

static BOOL Jcomp_seen_or_mark(struct Element *elt)
{
   ULONG p;
   ULONG h;
   ULONG i;

   if(!elt)
   {
      return TRUE;
   }
   p=(ULONG)elt;
   h=Jcomp_hash_ptr(p) & (JCOMP_DISPOSE_SEEN_SIZE-1UL);
   for(i=0;i<JCOMP_DISPOSE_SEEN_SIZE;i++)
   {
      ULONG idx=(h + i) & (JCOMP_DISPOSE_SEEN_SIZE-1UL);
      if(jcomp_seen[idx].runid!=jcomp_dispose_runid)
      {
         jcomp_seen[idx].ptr=p;
         jcomp_seen[idx].runid=jcomp_dispose_runid;
         return FALSE; /* first time */
      }
      if(jcomp_seen[idx].ptr==p)
      {
         return TRUE; /* already seen in this run */
      }
   }
   /* Table full for this run; fail safe: treat as seen to avoid double free. */
   return TRUE;
}

/* Forward declarations */
static void Disposelt(struct Element *elt);

#define JBC_EM_MAXCODE  512
#define JBC_EM_MAXCONST 32

struct Jbcemit
{
   struct Jcontext *jc;
   UBYTE buf[JBC_EM_MAXCODE];
   ULONG len;
   void *cstore[JBC_EM_MAXCONST];
   UBYTE ctypes[JBC_EM_MAXCONST];
   ULONG nconst;
   ULONG sp;
   ULONG maxstack;
};

static BOOL jbc_em_byte(struct Jbcemit *em, UBYTE b)
{
   if(!em)
   {
      return FALSE;
   }
   if(em->len >= JBC_EM_MAXCODE)
   {
      return FALSE;
   }
   em->buf[em->len] = b;
   em->len++;
   return TRUE;
}

static BOOL jbc_em_u16(struct Jbcemit *em, UWORD u)
{
   if(!jbc_em_byte(em, (UBYTE)(u & 0xff)))
   {
      return FALSE;
   }
   return jbc_em_byte(em, (UBYTE)(u >> 8));
}

static BOOL jbc_em_number(struct Jbcemit *em, double n)
{
   double *dp;
   UWORD idx;

   if(!em)
   {
      return FALSE;
   }
   if(em->nconst >= JBC_EM_MAXCONST)
   {
      return FALSE;
   }
   dp = ALLOCTYPE(double, 1, 0, em->jc->pool);
   if(!dp)
   {
      return FALSE;
   }
   *dp = n;
   em->cstore[em->nconst] = dp;
   em->ctypes[em->nconst] = JBCC_NUMBER;
   idx = (UWORD)em->nconst;
   em->nconst++;
   if(!jbc_em_byte(em, JBC_OP_NUMBER))
   {
      return FALSE;
   }
   if(!jbc_em_u16(em, idx))
   {
      return FALSE;
   }
   em->sp++;
   if(em->sp > em->maxstack)
   {
      em->maxstack = em->sp;
   }
   return TRUE;
}

static BOOL jbc_em_string(struct Jbcemit *em, UBYTE *s)
{
   UBYTE *sd;
   UWORD idx;

   if(!em)
   {
      return FALSE;
   }
   if(em->nconst >= JBC_EM_MAXCONST)
   {
      return FALSE;
   }
   sd = Jdupstr(s, -1, em->jc->pool);
   if(!sd)
   {
      return FALSE;
   }
   em->cstore[em->nconst] = sd;
   em->ctypes[em->nconst] = JBCC_STRING;
   idx = (UWORD)em->nconst;
   em->nconst++;
   if(!jbc_em_byte(em, JBC_OP_STRING))
   {
      return FALSE;
   }
   if(!jbc_em_u16(em, idx))
   {
      return FALSE;
   }
   em->sp++;
   if(em->sp > em->maxstack)
   {
      em->maxstack = em->sp;
   }
   return TRUE;
}

static void jbc_free_cstore(struct Jbcemit *em)
{
   ULONG i;

   if(!em)
   {
      return;
   }
   for(i = 0; i < em->nconst; i++)
   {
      if(em->ctypes[i] == JBCC_NUMBER || em->ctypes[i] == JBCC_STRING)
      {
         if(em->cstore[i])
         {
            FREE(em->cstore[i]);
         }
      }
      em->cstore[i] = NULL;
   }
   em->nconst = 0;
}

static BOOL jbc_emit_expr(struct Jbcemit *em, void *expr)
{
   struct Element *e;
   struct Elementint *ei;
   struct Elementfloat *ef;
   struct Elementstring *es;

   if(!em || !expr)
   {
      return FALSE;
   }
   e = (struct Element *)expr;
   switch(e->type)
   {
   case ET_INTEGER:
      ei = (struct Elementint *)e;
      return jbc_em_number(em, ei->ivalue);
   case ET_FLOAT:
      ef = (struct Elementfloat *)e;
      return jbc_em_number(em, ef->fvalue);
   case ET_BOOLEAN:
      ei = (struct Elementint *)e;
      if(ei->ivalue)
      {
         if(!jbc_em_byte(em, JBC_OP_TRUE))
         {
            return FALSE;
         }
      }
      else
      {
         if(!jbc_em_byte(em, JBC_OP_FALSE))
         {
            return FALSE;
         }
      }
      em->sp++;
      if(em->sp > em->maxstack)
      {
         em->maxstack = em->sp;
      }
      return TRUE;
   case ET_STRING:
      es = (struct Elementstring *)e;
      if(!es->svalue)
      {
         return FALSE;
      }
      return jbc_em_string(em, es->svalue);
   case ET_PLUS:
      if(!e->sub1 || !e->sub2)
      {
         return FALSE;
      }
      if(((struct Element *)e->sub1)->type == ET_STRING
      || ((struct Element *)e->sub2)->type == ET_STRING)
      {
         return FALSE;
      }
      if(!jbc_emit_expr(em, e->sub1))
      {
         return FALSE;
      }
      if(!jbc_emit_expr(em, e->sub2))
      {
         return FALSE;
      }
      if(em->sp < 2)
      {
         return FALSE;
      }
      if(!jbc_em_byte(em, JBC_OP_ADD))
      {
         return FALSE;
      }
      em->sp--;
      return TRUE;
   default:
      break;
   }
   return FALSE;
}

static BOOL jbc_finalize(struct Jbcemit *em, struct JBytecodeChunk **out)
{
   struct JBytecodeChunk *ch;
   UBYTE *bc;
   void **cs;
   UBYTE *ct;
   ULONG i;

   if(!em || !out)
   {
      return FALSE;
   }
   *out = NULL;
   ch = ALLOCTYPE(struct JBytecodeChunk, 1, 0, em->jc->pool);
   if(!ch)
   {
      jbc_free_cstore(em);
      return FALSE;
   }
   bc = ALLOCTYPE(UBYTE, em->len, 0, em->jc->pool);
   if(!bc)
   {
      FREE(ch);
      jbc_free_cstore(em);
      return FALSE;
   }
   if(em->len)
   {
      CopyMem(em->buf, bc, em->len);
   }
   ch->code = bc;
   ch->length = em->len;
   ch->maxstack = em->maxstack;
   ch->numconsts = em->nconst;
   if(em->nconst)
   {
      cs = ALLOCTYPE(void *, em->nconst, 0, em->jc->pool);
      ct = ALLOCTYPE(UBYTE, em->nconst, 0, em->jc->pool);
      if(!cs || !ct)
      {
         FREE(bc);
         FREE(cs);
         FREE(ct);
         FREE(ch);
         jbc_free_cstore(em);
         return FALSE;
      }
      for(i = 0; i < em->nconst; i++)
      {
         cs[i] = em->cstore[i];
         ct[i] = em->ctypes[i];
      }
      ch->consts = cs;
      ch->ctypes = ct;
   }
   else
   {
      ch->consts = NULL;
      ch->ctypes = NULL;
   }
   *out = ch;
   return TRUE;
}

/* Emit a bytecode body for functions that are a single return of a simple expression. */
static void Tryemitfuncbytecode(struct Jcontext *jc, struct Elementfunc *func)
{
   struct Elementlist *body;
   struct Elementnode *enode;
   struct Element *st;
   struct Element *ret;
   struct Jbcemit em;
   struct JBytecodeChunk *ch;
   ULONG nst;
   struct Element *only;

   if(!jc || !func)
   {
      return;
   }
   if(func->bcode)
   {
      return;
   }
   if(!func->body)
   {
      return;
   }
   body = (struct Elementlist *)func->body;
   if(body->type != ET_COMPOUND)
   {
      return;
   }
   nst = 0;
   only = NULL;
   for(enode = body->subs.first; enode && enode->next; enode = enode->next)
   {
      if(enode->sub)
      {
         nst++;
         only = enode->sub;
      }
   }
   em.jc = jc;
   em.len = 0;
   em.nconst = 0;
   em.sp = 0;
   em.maxstack = 0;
   if(nst == 0)
   {
      if(!jbc_em_byte(&em, JBC_OP_UNDEFINED))
      {
         return;
      }
      em.sp++;
      if(em.sp > em.maxstack)
      {
         em.maxstack = em.sp;
      }
      if(!jbc_em_byte(&em, JBC_OP_RETURN))
      {
         return;
      }
      if(em.sp != 1)
      {
         return;
      }
      if(!jbc_finalize(&em, &ch))
      {
         return;
      }
      func->bcode = ch;
      return;
   }
   if(nst != 1)
   {
      return;
   }
   st = only;
   if(!st || st->type != ET_RETURN)
   {
      return;
   }
   ret = st;
   if(ret->sub1)
   {
      if(!jbc_emit_expr(&em, ret->sub1))
      {
         return;
      }
   }
   else
   {
      if(!jbc_em_byte(&em, JBC_OP_UNDEFINED))
      {
         return;
      }
      em.sp++;
      if(em.sp > em.maxstack)
      {
         em.maxstack = em.sp;
      }
   }
   if(em.sp != 1)
   {
      return;
   }
   if(!jbc_em_byte(&em, JBC_OP_RETURN))
   {
      return;
   }
   if(!jbc_finalize(&em, &ch))
   {
      return;
   }
   func->bcode = ch;
}

/* Create an element */
static struct Element *Newelement(struct Jcontext *jc,void *pa,UWORD type,
   void *sub1,void *sub2,void *sub3,void *sub4)
{  struct Element *elt=ALLOCSTRUCT(Element,1,0,jc->pool);
   if(elt)
   {  elt->type=type;
      elt->generation=jc->generation;
      elt->linenr=jc->linenr+Plinenr(pa);
      elt->sub1=sub1;
      elt->sub2=sub2;
      elt->sub3=sub3;
      elt->sub4=sub4;
   }
   return elt;
}

/* Create a node for this element */
static struct Elementnode *Newnode(void *pool,void *elt)
{  struct Elementnode *enode=NULL;
   if(elt)
   {  if(enode=ALLOCSTRUCT(Elementnode,1,0,pool))
      {  enode->sub=elt;
      }
   }
   return enode;
}

/* Skip this token, or any token */
static void Skiptoken(struct Jcontext *jc,void *pa,UWORD id)
{  if(jc->nexttoken->id)
   {  if(!id || id==jc->nexttoken->id)
      {  jc->nexttoken=Nexttoken(pa);
      }
      else if(id)
      {  Errormsg(pa,"%s expected (from Skip token)",Tokenname(id));
      }
   }
}

/*-----------------------------------------------------------------------*/
static void *Expression(struct Jcontext *jc,void *pa);
static void *Assignmentexpression(struct Jcontext *jc,void *pa);
static void *Compoundstatement(struct Jcontext *jc,void *pa);
static void *Statement(struct Jcontext *jc, void *pa);
static void *Element(struct Jcontext *jc, void *pa);
static void *Primaryexpression(struct Jcontext *jc, void *pa);


/* Root elements */

static void *Nextarrayelement(struct Jcontext *jc, void *pa)
{
    if(jc->nexttoken->id == JT_COMMA)
    {
        struct Element *elt = ALLOCSTRUCT(Element,1,0,jc->pool);
        if(!elt)
        {
            Errormsg(pa,"Out of memory");
            return NULL;
        }
        elt->type = ET_EMPTY;
        return elt;
    }
    return Assignmentexpression(jc,pa);
}

static void *Array(struct Jcontext *jc, void *pa)
{
    struct Element *arrayelement;
    struct Elementlist *elist=ALLOCSTRUCT(Elementlist,1,0,jc->pool);
    struct Elementnode *enode;

    if(!elist)
    {
        Errormsg(pa,"Out of memory");
        return NULL;
    }
    elist->type=ET_ARRAY;
    elist->generation=jc->generation;
    elist->linenr=Plinenr(pa);
    NewList((struct List *)&elist->subs);
    Skiptoken(jc,pa,JT_LEFTBRACKET);
    if(jc->nexttoken->id !=JT_RIGHTBRACKET)
    {
        while(arrayelement=Nextarrayelement(jc,pa))
        {
           if(enode=Newnode(jc->pool,arrayelement))
           {  AddTail((struct List *)&elist->subs,(struct Node *)enode);
           }
           if(jc->nexttoken->id==JT_RIGHTBRACKET) break;
           Skiptoken(jc,pa,JT_COMMA);
        }
    }
    Skiptoken(jc,pa,JT_RIGHTBRACKET);
    return elist;
}

static void *Objectlit(struct Jcontext *jc, void *pa)
{
    struct Element *objelement;
    struct Elementlist *elist=ALLOCSTRUCT(Elementlist,1,0,jc->pool);
    struct Elementnode *enode;

    if(!elist)
    {
        Errormsg(pa,"Out of memory");
        return NULL;
    }

    elist->type=ET_OBJECTLIT;
    elist->generation=jc->generation;
    elist->linenr=Plinenr(pa);
    NewList((struct List *)&elist->subs);
    Skiptoken(jc,pa,JT_LEFTBRACE);

    if(jc->nexttoken->id != JT_RIGHTBRACE)
    {
        while(objelement=Assignmentexpression(jc,pa))
        {
           if(enode=Newnode(jc->pool,objelement))
           {  AddTail((struct List *)&elist->subs,(struct Node *)enode);
           }
           Skiptoken(jc,pa,JT_CONDELSE);   /* ":" */
           objelement=Assignmentexpression(jc,pa);
           if(enode=Newnode(jc->pool,objelement))
           {
              AddTail((struct List *)&elist->subs,(struct Node *)enode);
           }

           if(jc->nexttoken->id==JT_RIGHTBRACE) break;
           Skiptoken(jc,pa,JT_COMMA);
        }
    }
    Skiptoken(jc,pa,JT_RIGHTBRACE);
    return elist;
}

static void *Identifier(struct Jcontext *jc,void *pa)
{  struct Elementstring *elt=ALLOCSTRUCT(Elementstring,1,0,jc->pool);
   if(!elt)
   {  Errormsg(pa,"Out of memory");
      return NULL;
   }
   elt->type=ET_IDENTIFIER;
   elt->generation=jc->generation;
   elt->linenr=Plinenr(pa);
   elt->svalue=Jdupstr(jc->nexttoken->svalue,-1,jc->pool);
   if(!elt->svalue)
   {  Errormsg(pa,"Out of memory");
      FREE(elt);
      return NULL;
   }
   return elt;
}

static void *Integer(struct Jcontext *jc,void *pa)
{  struct Elementint *elt=ALLOCSTRUCT(Elementint,1,0,jc->pool);
   if(!elt)
   {  Errormsg(pa,"Out of memory");
      return NULL;
   }
   elt->type=ET_INTEGER;
   elt->generation=jc->generation;
   elt->linenr=Plinenr(pa);
   elt->ivalue=jc->nexttoken->fvalue;
   return elt;
}

static void *Float(struct Jcontext *jc,void *pa)
{  struct Elementfloat *elt=ALLOCSTRUCT(Elementfloat,1,0,jc->pool);
   if(!elt)
   {  Errormsg(pa,"Out of memory");
      return NULL;
   }
   elt->type=ET_FLOAT;
   elt->generation=jc->generation;
   elt->linenr=Plinenr(pa);
   elt->fvalue=jc->nexttoken->fvalue;
   return elt;
}

static void *Boolean(struct Jcontext *jc,void *pa)
{  struct Elementint *elt=ALLOCSTRUCT(Elementint,1,0,jc->pool);
   if(!elt)
   {  Errormsg(pa,"Out of memory");
      return NULL;
   }
   elt->type=ET_BOOLEAN;
   elt->generation=jc->generation;
   elt->linenr=Plinenr(pa);
   elt->ivalue=jc->nexttoken->ivalue;
   return elt;
}

static void *String(struct Jcontext *jc,void *pa)
{  struct Elementstring *elt=ALLOCSTRUCT(Elementstring,1,0,jc->pool);
   if(!elt)
   {  Errormsg(pa,"Out of memory");
      return NULL;
   }
   elt->type=ET_STRING;
   elt->generation=jc->generation;
   elt->linenr=Plinenr(pa);
   elt->svalue=Jdupstr(jc->nexttoken->svalue,-1,jc->pool);
   if(!elt->svalue)
   {  Errormsg(pa,"Out of memory");
      FREE(elt);
      return NULL;
   }
   return elt;
}

static void *Regexp(struct Jcontext *jc,void *pa)
{
    struct Elementregexp *elt=ALLOCSTRUCT(Elementregexp,1,0,jc->pool);
    if(!elt)
    {  Errormsg(pa,"Out of memory");
       return NULL;
    }
    elt->type=ET_REGEXP;
    elt->generation=jc->generation;
    elt->linenr=Plinenr(pa);
    elt->pattern=Jdupstr(jc->nexttoken->svalue,-1,jc->pool);
    elt->flags=Jdupstr(jc->nexttoken->svalue2,-1,jc->pool);
    if(!elt->pattern || (!elt->flags && jc->nexttoken->svalue2))
    {  Errormsg(pa,"Out of memory");
       if(elt->pattern) FREE(elt->pattern);
       if(elt->flags) FREE(elt->flags);
       FREE(elt);
       return NULL;
    }
    return elt;
}

/*-----------------------------------------------------------------------*/

static void *Primaryexpression(struct Jcontext *jc,void *pa)
{  void *elt=NULL;
   switch(jc->nexttoken->id)
   {  case JT_LEFTPAR:
         Skiptoken(jc,pa,0);
         elt=Expression(jc,pa);
         Skiptoken(jc,pa,JT_RIGHTPAR);
         break;
      case JT_LEFTBRACKET:
         elt=Array(jc,pa);
         break;
      case JT_IDENTIFIER:
         elt=Identifier(jc,pa);
         Skiptoken(jc,pa,0);
         break;
      case JT_INTEGERLIT:
         elt=Integer(jc,pa);
         Skiptoken(jc,pa,0);
         break;
      case JT_FLOATLIT:
         elt=Float(jc,pa);
         Skiptoken(jc,pa,0);
         break;
      case JT_BOOLEANLIT:
         elt=Boolean(jc,pa);
         Skiptoken(jc,pa,0);
         break;
      case JT_STRINGLIT:
         elt=String(jc,pa);
         Skiptoken(jc,pa,0);
         break;
      case JT_REGEXPLIT:
         elt=Regexp(jc,pa);
         Skiptoken(jc,pa,0);
         break;
      case JT_LEFTBRACE:
         elt=Objectlit(jc,pa);
         break;
      case JT_NULLLIT:
         elt=Newelement(jc,pa,ET_NULL,NULL,NULL,NULL,NULL);
         Skiptoken(jc,pa,0);
         break;
      case JT_THIS:
         elt=Newelement(jc,pa,ET_THIS,NULL,NULL,NULL,NULL);
         Skiptoken(jc,pa,0);
         break;
         case JT_FUNCTION:
            elt=Element(jc,pa);
            break;

      case JT_RIGHTBRACE:
      case JT_RIGHTPAR:
      case JT_SEMICOLON:
         break;
      default:
         Errormsg(pa,"Expression expected (from primary expression)");
         break;
   }
   return elt;
}

static void *Functioncall(struct Jcontext *jc,void *pa,void *elt)
{  struct Elementlist *elist=ALLOCSTRUCT(Elementlist,1,0,jc->pool);
   struct Elementnode *enode;
   struct Element *arg;
   if(elist)
   {  elist->type=ET_CALL;
      elist->generation=jc->generation;
      elist->linenr=Plinenr(pa);
      NewList((struct List *)&elist->subs);
      if(enode=Newnode(jc->pool,elt))
      {  AddTail((struct List *)&elist->subs,(struct Node *)enode);
      }
      Skiptoken(jc,pa,JT_LEFTPAR);
      while(arg=Assignmentexpression(jc,pa))
      {  if(enode=Newnode(jc->pool,arg))
         {  ADDTAIL(&elist->subs,enode);
         }
         if(jc->nexttoken->id==JT_RIGHTPAR) break;
         Skiptoken(jc,pa,JT_COMMA);
      }
      Skiptoken(jc,pa,JT_RIGHTPAR);
   }
   else
   {  Errormsg(pa,"Out of memory");
   }
   return elist;
}

static void *Memberexpression(struct Jcontext *jc,void *pa)
{  BOOL done=FALSE;
   void *elt=Primaryexpression(jc,pa),*sub2;
   while(!done)
   {  switch(jc->nexttoken->id)
      {  case JT_DOT:
            Skiptoken(jc,pa,0);
            sub2=Primaryexpression(jc,pa);
            elt=Newelement(jc,pa,ET_DOT,elt,sub2,NULL,NULL);
            break;
         case JT_LEFTBRACKET:
            Skiptoken(jc,pa,0);
            sub2=Expression(jc,pa);
            elt=Newelement(jc,pa,ET_INDEX,elt,sub2,NULL,NULL);
            Skiptoken(jc,pa,JT_RIGHTBRACKET);
            break;
         case JT_LEFTPAR:
            elt=Functioncall(jc,pa,elt);
            break;
         default:
            done=TRUE;
            break;
      }
   }
   return elt;
}

static void *Unaryexpression(struct Jcontext *jc,void *pa)
{  void *elt;
   UWORD preop=0;
   switch(jc->nexttoken->id)
   {  case JT_MINUS:    preop=ET_NEGATIVE;break;
      case JT_PLUS:     preop=ET_POSITIVE;break;
      case JT_NOT:      preop=ET_NOT;break;
      case JT_BITNEG:   preop=ET_BITNEG;break;
      case JT_INC:      preop=ET_PREINC;break;
      case JT_DEC:      preop=ET_PREDEC;break;
      case JT_DELETE:   preop=ET_DELETE;break;
      case JT_TYPEOF:   preop=ET_TYPEOF;break;
      case JT_VOID:     preop=ET_VOID;break;
      case JT_NEW:      preop=ET_NEW;break;
#ifdef JSADDRESS
      case JT_ADDRESS:  preop=ET_ADDRESS;break;
#endif
   }
   if(preop)
   {  Skiptoken(jc,pa,0);
      elt=Unaryexpression(jc,pa);
   }
   else
   {  elt=Memberexpression(jc,pa);
   }
   if(!preop)
   {  switch(jc->nexttoken->id)
      {  case JT_INC:   preop=ET_POSTINC;break;
         case JT_DEC:   preop=ET_POSTDEC;break;
      }
      if(preop) Skiptoken(jc,pa,0);
   }
   if(preop)
   {  elt=Newelement(jc,pa,preop,elt,NULL,NULL,NULL);
   }
   return elt;
}

static void *Multiplicativeexpression(struct Jcontext *jc,void *pa)
{  void *elt=Unaryexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_MULT:     op=ET_MULT;break;
         case JT_DIV:      op=ET_DIV;break;
         case JT_REM:      op=ET_REM;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Unaryexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Additiveexpression(struct Jcontext *jc,void *pa)
{  void *elt=Multiplicativeexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_PLUS:     op=ET_PLUS;break;
         case JT_MINUS:    op=ET_MINUS;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Multiplicativeexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Shiftexpression(struct Jcontext *jc,void *pa)
{  void *elt=Additiveexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_SHLEFT:   op=ET_SHLEFT;break;
         case JT_SHRIGHT:  op=ET_SHRIGHT;break;
         case JT_USHRIGHT: op=ET_USHRIGHT;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Additiveexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Relationalexpression(struct Jcontext *jc,void *pa)
{  void *elt=Shiftexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_LT:       op=ET_LT;break;
         case JT_GT:       op=ET_GT;break;
         case JT_LE:       op=ET_LE;break;
         case JT_GE:       op=ET_GE;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Shiftexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Equalityexpression(struct Jcontext *jc,void *pa)
{  void *elt=Relationalexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_EQ:       op=ET_EQ;break;
         case JT_NE:       op=ET_NE;break;
         case JT_EXEQ:     op=ET_EXEQ;break;
         case JT_NEXEQ:    op=ET_NEXEQ;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Relationalexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Bitwiseandexpression(struct Jcontext *jc,void *pa)
{  void *elt=Equalityexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_BITAND:   op=ET_BITAND;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Equalityexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Bitwisexorexpression(struct Jcontext *jc,void *pa)
{  void *elt=Bitwiseandexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_BITXOR:   op=ET_BITXOR;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Bitwiseandexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Bitwiseorexpression(struct Jcontext *jc,void *pa)
{  void *elt=Bitwisexorexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_BITOR:    op=ET_BITOR;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Bitwisexorexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Andexpression(struct Jcontext *jc,void *pa)
{  void *elt=Bitwiseorexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_AND:      op=ET_AND;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Bitwiseorexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Orexpression(struct Jcontext *jc,void *pa)
{
   void *elt=Andexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_OR:       op=ET_OR;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Andexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}


static void *Instanceofexpression(struct Jcontext *jc, void *pa)
{
   void *elt=Orexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_INSTANCEOF:       op=ET_INSTANCEOF;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Orexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Inexpression(struct Jcontext *jc, void *pa)
{
   void *elt=Instanceofexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_IN:       op=ET_IN;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Instanceofexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Conditionalexpression(struct Jcontext *jc,void *pa)
{  struct Element *elt=Inexpression(jc,pa);
   if(jc->nexttoken->id==JT_CONDIF)
   {  Skiptoken(jc,pa,0);
      elt=Newelement(jc,pa,ET_COND,elt,Assignmentexpression(jc,pa),NULL,NULL);
      if(elt && jc->nexttoken->id==JT_CONDELSE)
      {  Skiptoken(jc,pa,0);
         elt->sub3=Assignmentexpression(jc,pa);
      }
      else
      {  Errormsg(pa,"':' expected (from Conditional expression)");
      }
   }
   return elt;
}

static void *Labelexpression(struct Jcontext *jc,void *pa)
{
    struct Element *elt=Assignmentexpression(jc,pa);
    if(jc->nexttoken->id==JT_CONDELSE)   /* this is ":" think about changing condelse to colon for clarity */
    {
        Skiptoken(jc,pa,0);
        elt=Newelement(jc,pa,ET_LABEL,elt,Statement(jc,pa),NULL,NULL);
    }
    return elt;
}

static void *Assignmentexpression(struct Jcontext *jc,void *pa)
{  void *elt=Conditionalexpression(jc,pa);
   UWORD op;
   switch(jc->nexttoken->id)
   {  case JT_ASSIGN:   op=ET_ASSIGN;break;
      case JT_APLUS:    op=ET_APLUS;break;
      case JT_AMINUS:   op=ET_AMINUS;break;
      case JT_AMULT:    op=ET_AMULT;break;
      case JT_ADIV:     op=ET_ADIV;break;
      case JT_AREM:     op=ET_AREM;break;
      case JT_ABITAND:  op=ET_ABITAND;break;
      case JT_ABITOR:   op=ET_ABITOR;break;
      case JT_ABITXOR:  op=ET_ABITXOR;break;
      case JT_ASHLEFT:  op=ET_ASHLEFT;break;
      case JT_ASHRIGHT: op=ET_ASHRIGHT;break;
      case JT_AUSHRIGHT:op=ET_AUSHRIGHT;break;
      default:          op=0;break;
   }
   if(op)
   {  Skiptoken(jc,pa,0);
      elt=Newelement(jc,pa,op,elt,Assignmentexpression(jc,pa),NULL,NULL);
   }
   return elt;
}

static void *Expression(struct Jcontext *jc,void *pa)
{  void *elt=Labelexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_COMMA:    op=ET_COMMA;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Assignmentexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Expression2(struct Jcontext *jc,void *pa)
{  void *elt=Assignmentexpression(jc,pa);
   UWORD op;
   do
   {  switch(jc->nexttoken->id)
      {  case JT_COMMA:    op=ET_COMMA;break;
         default:          op=0;break;
      }
      if(op)
      {  Skiptoken(jc,pa,0);
         elt=Newelement(jc,pa,op,elt,Assignmentexpression(jc,pa),NULL,NULL);
      }
   } while(op);
   return elt;
}

static void *Condition(struct Jcontext *jc,void *pa)
{  void *elt=NULL;
   if(jc->nexttoken->id==JT_LEFTPAR)
   {  Skiptoken(jc,pa,0);
      elt=Expression(jc,pa);
      Skiptoken(jc,pa,JT_RIGHTPAR);
   }
   else Errormsg(pa,"'(' expected - condition (from condition)");
   return elt;
}

static void *Varlist(struct Jcontext *jc,void *pa)
{  struct Elementlist *elist;
   struct Elementnode *enode;
   struct Element *elt;
   if(elist=ALLOCSTRUCT(Elementlist,1,0,jc->pool))
   {  elist->type=ET_VARLIST;
      elist->generation=jc->generation;
      elist->linenr=Plinenr(pa);
      NewList((struct List *)&elist->subs);
      if(jc->nexttoken->id!=JT_IDENTIFIER)
      {  Errormsg(pa,"Identifier expected (from Varlist)");
      }
      while(jc->nexttoken->id==JT_IDENTIFIER)
      {  elt=Identifier(jc,pa);
         Skiptoken(jc,pa,0);
         if(jc->nexttoken->id==JT_ASSIGN)
         {  Skiptoken(jc,pa,0);
            elt=Newelement(jc,pa,ET_VAR,elt,Assignmentexpression(jc,pa),NULL,NULL);
         }
         else
         {  elt=Newelement(jc,pa,ET_VAR,elt,NULL,NULL,NULL);
         }
         if(enode=Newnode(jc->pool,elt))
         {  AddTail((struct List *)&elist->subs,(struct Node *)enode);
         }
         if(jc->nexttoken->id!=JT_COMMA) break;
         Skiptoken(jc,pa,JT_COMMA);
      }
   }
   return elist;
}

static void *Variablesorexpression(struct Jcontext *jc,void *pa)
{  void *elt;
   if(jc->nexttoken->id==JT_VAR)
   {  Skiptoken(jc,pa,0);
      elt=Varlist(jc,pa);
   }
   else
   {  elt=Expression(jc,pa);
   }
   return elt;
}

static void *Variablesormemberexpression(struct Jcontext *jc,void *pa)
{  void *elt;
   if(jc->nexttoken->id==JT_VAR)
   {  Skiptoken(jc,pa,0);
      elt=Varlist(jc,pa);
   }
   else
   {  elt=Memberexpression(jc,pa);
   }
   return elt;
}

static void *Switchstatement(struct Jcontext *jc, void *pa)
{
    struct Elementswitch *elt = NULL;
    struct Element *cond;
    struct Elementnode *enode;
    ULONG state;

    Skiptoken(jc,pa,JT_LEFTPAR);
    cond=Expression(jc,pa);
    Skiptoken(jc,pa,JT_RIGHTPAR);
    if (cond)
    {
        elt= ALLOCSTRUCT(Elementswitch,1,0,jc->pool);
        if(!elt)
        {  Errormsg(pa,"Out of memory");
           return NULL;
        }
        elt->type=ET_SWITCH;
        elt->generation=jc->generation;
        elt->linenr=Plinenr(pa);
        elt->cond=cond;
        NewList((struct List *)&elt->subs);
        if(jc->nexttoken->id==JT_LEFTBRACE)
        {
            Skiptoken(jc,pa,0);
            {
                while(jc->nexttoken->id)
                {
                    struct Element *statement;
                    state=Parserstate(pa);
                    statement =  Statement(jc,pa);
                    if(enode=Newnode(jc->pool,statement))
                    {
                        if(statement->type==ET_CASE)
                        {
                            if(((struct Elementcase *)statement)->isdefault)
                            {
                                elt->defaultcase=enode;
                            }
                        }
                        AddTail((struct List *)&elt->subs,(struct Node *)enode);
                    }
                    else if(jc->nexttoken->id==JT_RIGHTBRACE) break;
                    if(state==Parserstate(pa)) Skiptoken(jc,pa,0);
                }
            }
            Skiptoken(jc,pa,JT_RIGHTBRACE);
        }
        else Errormsg(pa,"'{' expected (from switch)");
    }
    return elt;
}

static void *Trystatement(struct Jcontext *jc, void *pa)
{
    struct Elementtry *elt = NULL;
    if((elt=ALLOCSTRUCT(Elementtry,1,0,jc->pool)))
    {
        elt->type=ET_TRY;
        elt->generation=jc->generation;
        elt->linenr=Plinenr(pa);
        elt->try=Statement(jc,pa);
        if(elt && jc->nexttoken->id==JT_CATCH)
        {
            Skiptoken(jc,pa,0);
            Skiptoken(jc,pa,JT_LEFTPAR);
            elt->catchvar = Variablesorexpression(jc,pa);
            Skiptoken(jc,pa,JT_RIGHTPAR);
            elt->catch=Statement(jc,pa);
        }
        if(elt && jc->nexttoken->id==JT_FINALLY)
        {
            Skiptoken(jc,pa,0);
            elt->finally=Statement(jc,pa);
        }
    }
    return elt;
}

static void *Statement(struct Jcontext *jc,void *pa)
{  struct Element *elt=NULL;
   if(!Feedback(jc))
   {  jc->flags|=JCF_IGNORE;
   }
   if(!(jc->flags&JCF_IGNORE))
   {  switch(jc->nexttoken->id)
      {  case JT_IF:
            Skiptoken(jc,pa,0);
            if(elt=Newelement(jc,pa,ET_IF,Condition(jc,pa),Statement(jc,pa),NULL,NULL))
            if(elt && jc->nexttoken->id==JT_ELSE)
            {  Skiptoken(jc,pa,0);
               elt->sub3=Statement(jc,pa);
            }
            break;
         case JT_WHILE:
            Skiptoken(jc,pa,0);
            elt=Newelement(jc,pa,ET_WHILE,Condition(jc,pa),Statement(jc,pa),NULL,NULL);
            break;
         case JT_DO:
            Skiptoken(jc,pa,0);
            elt=Newelement(jc,pa,ET_DO,Statement(jc,pa),NULL,NULL,NULL);
            Skiptoken(jc,pa,JT_WHILE);
            elt->sub2=Condition(jc,pa);
            break;
         case JT_FOR:
            {
                void *savedpa;
                Skiptoken(jc,pa,0);
                Skiptoken(jc,pa,JT_LEFTPAR);
                savedpa = Saveparser(jc,pa);
                elt=Variablesormemberexpression(jc,pa);
                if(elt && jc->nexttoken->id==JT_IN)
                {  Skiptoken(jc,pa,0);
                   elt=Newelement(jc,pa,ET_FORIN,elt,Expression(jc,pa),NULL,NULL);
                   Skiptoken(jc,pa,JT_RIGHTPAR);
                   elt->sub3=Statement(jc,pa);
                   Freesavedparser(jc,savedpa);
                }
                else
                {
                    /* didn't get a for .. in back up and try again */
                    Disposelt(elt);
                    Restoreparser(jc,pa,savedpa);
                    elt=Variablesorexpression(jc,pa);
                    if(jc->nexttoken->id==JT_SEMICOLON)
                    {  Skiptoken(jc,pa,0);
                       elt=Newelement(jc,pa,ET_FOR,elt,Expression(jc,pa),NULL,NULL);
                       Skiptoken(jc,pa,JT_SEMICOLON);
                       elt->sub3=Expression(jc,pa);
                       Skiptoken(jc,pa,JT_RIGHTPAR);
                       elt->sub4=Statement(jc,pa);
                    }
                    else
                    {  Errormsg(pa,"'in' or ';' expected (from statement)");
                    }
                }
            }
            break;

         case JT_TRY:
            Skiptoken(jc,pa,0);
            elt=Trystatement(jc,pa);
            break;

         case JT_SWITCH:
            Skiptoken(jc,pa,0);
            elt=Switchstatement(jc,pa);
            break;

         case JT_CASE:
            Skiptoken(jc,pa,0);
            elt=(struct Element *)ALLOCSTRUCT(Elementcase,1,0,jc->pool);
            elt->type=ET_CASE;
            elt->linenr=Plinenr(pa);
            elt->generation=jc->generation;
            ((struct Elementcase *)elt)->isdefault=FALSE;
            ((struct Elementcase *)elt)->expr = Expression2(jc,pa);
            Skiptoken(jc,pa,JT_CONDELSE);
            break;
         case JT_DEFAULT:
            Skiptoken(jc,pa,0);
            elt=(struct Element *)ALLOCSTRUCT(Elementcase,1,0,jc->pool);
            elt->type=ET_CASE;
            elt->linenr=Plinenr(pa);
            elt->generation=jc->generation;
            ((struct Elementcase *)elt)->isdefault=TRUE;
            Skiptoken(jc,pa,JT_CONDELSE);
            break;
         case JT_BREAK:
            Skiptoken(jc,pa,0);
            elt=Newelement(jc,pa,ET_BREAK,NULL,NULL,NULL,NULL);
            if(jc->nexttoken->id==JT_SEMICOLON)
            {  Skiptoken(jc,pa,0);
            }
            break;
         case JT_CONTINUE:
            Skiptoken(jc,pa,0);
            elt=Newelement(jc,pa,ET_CONTINUE,NULL,NULL,NULL,NULL);
            if(jc->nexttoken->id==JT_SEMICOLON)
            {  Skiptoken(jc,pa,0);
            }
            break;
         case JT_WITH:
            Skiptoken(jc,pa,0);
            elt=Newelement(jc,pa,ET_WITH,Condition(jc,pa),Statement(jc,pa),NULL,NULL);
            break;
         case JT_RETURN:
            Pskipnewline(pa,FALSE);
            Skiptoken(jc,pa,0);
            elt=Newelement(jc,pa,ET_RETURN,NULL,NULL,NULL,NULL);
            if(jc->nexttoken->id==JT_NEWLINE)
            {
                Skiptoken(jc,pa,0);
                Pskipnewline(pa,TRUE);
                break;
            }
            Pskipnewline(pa,TRUE);
            elt->sub1 = Expression(jc,pa);
            if(jc->nexttoken->id==JT_SEMICOLON)
            {  Skiptoken(jc,pa,0);
            }
            break;

         case JT_THROW:
            Skiptoken(jc,pa,0);
            elt=Newelement(jc,pa,ET_THROW,Expression(jc,pa),NULL,NULL,NULL);
            if(jc->nexttoken->id==JT_SEMICOLON)
            {  Skiptoken(jc,pa,0);
            }
            break;

         case JT_LEFTBRACE:
            elt=Compoundstatement(jc,pa);
            break;
#ifdef JSDEBUG
         case JT_DEBUG:
            Skiptoken(jc,pa,0);
            elt=Newelement(jc,pa,ET_DEBUG,Expression(jc,pa),NULL,NULL,NULL);
            if(jc->nexttoken->id==JT_SEMICOLON)
            {  Skiptoken(jc,pa,0);
            }
            break;
#endif
         default:
            elt=Variablesorexpression(jc,pa);
            if(jc->nexttoken->id==JT_SEMICOLON)
            {  Skiptoken(jc,pa,0);
            }
            break;
      }
   }
   return elt;
}

static void *Compoundstatement(struct Jcontext *jc,void *pa)
{  struct Elementlist *elist=NULL;
   struct Elementnode *enode;
   ULONG state;
   
   if(jc->nexttoken->id==JT_LEFTBRACE)
   {  Skiptoken(jc,pa,0);
      if(elist=ALLOCSTRUCT(Elementlist,1,0,jc->pool))
      {  elist->type=ET_COMPOUND;
         elist->generation=jc->generation;
         elist->linenr=Plinenr(pa);
         NewList((struct List *)&elist->subs);
         
         while(jc->nexttoken->id)
         {  state=Parserstate(pa);
            if(enode=Newnode(jc->pool,Statement(jc,pa)))
            {  AddTail((struct List *)&elist->subs,(struct Node *)enode);
            }
            else if(jc->nexttoken->id==JT_RIGHTBRACE) break;
            if(state==Parserstate(pa)) Skiptoken(jc,pa,0);
         }
      }
      Skiptoken(jc,pa,JT_RIGHTBRACE);
   }
   else Errormsg(pa,"'{' expected (from compoundstatement)");
   return elist;
}

static void *Element(struct Jcontext *jc,void *pa)
{  void *elt=NULL;
   struct Elementnode *enode;
   struct Elementfunc *func;
   struct Elementfuncref *funcref;
   struct Variable *fprop;
   struct Jobject *fobj = NULL;
   if(jc->nexttoken->id==JT_FUNCTION)
   {  Skiptoken(jc,pa,0);
      if(func=ALLOCSTRUCT(Elementfunc,1,0,jc->pool))
      {  func->type=ET_FUNCTION;
         func->generation=jc->generation;
         func->linenr=Plinenr(pa);
         NewList((struct List *)&func->subs);
         if(jc->nexttoken->id==JT_IDENTIFIER)
         {  func->name=Jdupstr(jc->nexttoken->svalue,-1,jc->pool);
            Skiptoken(jc,pa,0);
         }
         else
         {
         //Errormsg(pa,"Identifier expected (from element)");
         }
         if(jc->nexttoken->id==JT_LEFTPAR)
         {  Skiptoken(jc,pa,0);
            for(;;)
            {  if(jc->nexttoken->id==JT_IDENTIFIER)
               {  if(enode=Newnode(jc->pool,Identifier(jc,pa)))
                  {  AddTail((struct List *)&func->subs,(struct Node *)enode);
                  }
                  Skiptoken(jc,pa,0);
               }
               if(jc->nexttoken->id==JT_COMMA)
               {  Skiptoken(jc,pa,0);
               }
               else break;
            }
            Skiptoken(jc,pa,JT_RIGHTPAR);
         }
         else Errormsg(pa,"'(' expected - element (from element)");
         func->body=Compoundstatement(jc,pa);

         /* Add function to current scope */
        if(fobj=Newobject(jc))
        {
            Keepobject(fobj,TRUE);
            fobj->function=func;
            fobj->type=OBJT_FUNCTION;
            /* Inherit call/apply and other methods from Function.prototype. */
            if(jc->function)
            {  Initconstruct(jc,fobj,NULL,jc->function);
            }
            if(func->name)
            {
                if((fprop=Getownproperty(jc->fscope,func->name))
                || (fprop=Addproperty(jc->fscope,func->name)))
                {  Asgfunction(&fprop->val,fobj,NULL);
                   fprop->flags |= VARF_DONTDELETE;
                }
            }
            {
                struct Jobject *proto = NULL;
                if(jc->object) proto = Getprototype(jc->object);
                Addprototype(jc,fobj,proto);
            }
        }
         /* Remember current scope with function */
         func->fscope=jc->fscope;
         Tryemitfuncbytecode(jc,func);
      }
      else
      {  Errormsg(pa,"Out of memory");
      }

      /* This will return the function object when "function" is used
       * in an expression of type foo = function name () {body}
       */

      if(fobj)
      {
         if(funcref = ALLOCSTRUCT(Elementfuncref,1,0,jc->pool))
         {
            funcref->type = ET_FUNCREF;
            funcref->generation = jc->generation;
            funcref->linenr = jc->linenr;
            funcref->func = fobj;
            elt = funcref;
         }
         else
         {  Errormsg(pa,"Out of memory");
            elt = NULL;
         }
      }
   }
   else
   {  elt=Statement(jc,pa);
   }
   return elt;
}

/* Create new program, or expand existing one */
static void Compileprogram(struct Jcontext *jc,void *pa)
{  struct Elementlist *plist=NULL;
   struct Elementnode *enode;
   struct Element *elt;
   ULONG state;
   if(!jc->program)
   {        if(plist=ALLOCSTRUCT(Elementlist,1,0,jc->pool))
      {  plist->type=ET_PROGRAM;
         plist->generation=jc->generation;
         plist->linenr=Plinenr(pa);
         NewList((struct List *)&plist->subs);
      }
      jc->program=plist;
   }
   if(plist=jc->program)
   {  while(jc->nexttoken->id && !(jc->flags&JCF_IGNORE))
      {  state=Parserstate(pa);
         if(elt=Element(jc,pa))
         {  if(enode=Newnode(jc->pool,elt))
            {  AddTail((struct List *)&plist->subs,(struct Node *)enode);
            }
         }
         if(state==Parserstate(pa)) Skiptoken(jc,pa,0);
      }
   }
}

/*-----------------------------------------------------------------------*/

struct Decompile
{  struct Jbuffer *jb;
   short indent;
   short parlevel;
   BOOL nosemicolon;      /* If TRUE, no semicolon is needed in a compound statement */
};

static void Decompile(struct Decompile *dc,struct Element *elt);

static void Denewline(struct Decompile *dc)
{  short i;
   Addtojbuffer(dc->jb,"\n",1);
   for(i=0;i<dc->indent;i++)
   {  Addtojbuffer(dc->jb,"  ",2);
   }
}

static short Derpar(struct Decompile *dc,short level)
{  short oldlevel=dc->parlevel;
   if(level<dc->parlevel)
   {  Addtojbuffer(dc->jb,"(",1);
   }
   dc->parlevel=level;
   return oldlevel;
}

static void Delpar(struct Decompile *dc,short level)
{  if(level>dc->parlevel)
   {  Addtojbuffer(dc->jb,")",1);
   }
   dc->parlevel=level;
}

static void Desemicolon(struct Decompile *dc)
{  if(!dc->nosemicolon)
   {  Addtojbuffer(dc->jb,";",1);
   }
   dc->nosemicolon=FALSE;
}

static void Deprogram(struct Decompile *dc,struct Elementlist *elist)
{  struct Elementnode *enode;
   for(enode=elist->subs.first;enode->next;enode=enode->next)
   {  if(enode->sub)
      {  Decompile(dc,enode->sub);
         Desemicolon(dc);
      }
   }
}

static void Decall(struct Decompile *dc,struct Elementlist *elist)
{  struct Elementnode *enode;
   BOOL first;
   if(elist->subs.first->next && elist->subs.first->sub)
   {  Decompile(dc,elist->subs.first->sub);
      Addtojbuffer(dc->jb,"(",1);
      first=TRUE;
      for(enode=elist->subs.first->next;enode && enode->next;enode=enode->next)
      {  if(enode->sub)
         {  if(!first)
            {  Addtojbuffer(dc->jb,", ",-1);
            }
            first=FALSE;
            Decompile(dc,enode->sub);
         }
      }
      Addtojbuffer(dc->jb,")",1);
   }
}

static void Decompound(struct Decompile *dc,struct Elementlist *elist)
{  struct Elementnode *enode;
   Addtojbuffer(dc->jb,"{",1);
   dc->indent++;
   for(enode=elist->subs.first;enode->next;enode=enode->next)
   {  Denewline(dc);
      if(enode->sub) Decompile(dc,enode->sub);
      Desemicolon(dc);
   }
   dc->indent--;
   Denewline(dc);
   Addtojbuffer(dc->jb,"}",1);
   dc->nosemicolon=TRUE;
}

static void Devarlist(struct Decompile *dc,struct Elementlist *elist)
{  struct Elementnode *enode;
   BOOL first=TRUE;
   Addtojbuffer(dc->jb,"var ",-1);
   for(enode=elist->subs.first;enode->next;enode=enode->next)
   {  if(!first)
      {  Addtojbuffer(dc->jb,", ",-1);
      }
      first=FALSE;
      if(enode->sub) Decompile(dc,enode->sub);
   }
}

static void Defunction(struct Decompile *dc,struct Elementfunc *func)
{  struct Elementnode *enode;
   BOOL first;
   Addtojbuffer(dc->jb,"function ",-1);
   if(func->name) Addtojbuffer(dc->jb,func->name,-1);
   Addtojbuffer(dc->jb,"(",1);
   first=TRUE;
   for(enode=func->subs.first;enode->next;enode=enode->next)
   {  if(enode->sub)
      {  if(!first)
         {  Addtojbuffer(dc->jb,", ",-1);
         }
         first=FALSE;
         Decompile(dc,enode->sub);
      }
   }
   Addtojbuffer(dc->jb,") ",-1);
   if(func->body)
   {  Decompile(dc,func->body);
   }
   Denewline(dc);
   dc->nosemicolon=TRUE;
}

static void Debreak(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"break",-1);
}

static void Decontinue(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"continue",-1);
}

static void Dethis(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"this",-1);
}

static void Denull(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"null",-1);
}

static void Deempty(struct Decompile *dc,struct Element *elt)
{
}

static void Denegative(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"-",1);
   Decompile(dc,elt->sub1);
}

static void Denot(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"!",1);
   Decompile(dc,elt->sub1);
}

static void Debitneg(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"~",1);
   Decompile(dc,elt->sub1);
}

static void Depreinc(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"++",2);
   Decompile(dc,elt->sub1);
}

static void Depredec(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"--",2);
   Decompile(dc,elt->sub1);
}

static void Depostinc(struct Decompile *dc,struct Element *elt)
{  Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb,"++",2);
}

static void Depostdec(struct Decompile *dc,struct Element *elt)
{  Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb,"--",2);
}

static void Denew(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"new ",-1);
   Decompile(dc,elt->sub1);
}

static void Dedelete(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"delete ",-1);
   Decompile(dc,elt->sub1);
}

static void Detypeof(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"typeof ",-1);
   Decompile(dc,elt->sub1);
}

static void Devoid(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"void ",-1);
   Decompile(dc,elt->sub1);
}

static void Dereturn(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"return ",-1);
   Decompile(dc,elt->sub1);
}

static void Dethrow(struct Decompile *dc, struct Element *elt)
{
    Addtojbuffer(dc->jb,"throw ",-1);
    Decompile(dc,elt->sub1);
}
static void Deinternal(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"{",-1);
   dc->indent++;
   Denewline(dc);
   Addtojbuffer(dc->jb,"[internal code]",-1);
   dc->indent--;
   Denewline(dc);
   Addtojbuffer(dc->jb,"}",-1);
   dc->nosemicolon=TRUE;
}

static void Defunceval(struct Decompile *dc, struct Element *elt)
{
    Addtojbuffer(dc->jb,"{",-1);
    dc->indent++;
    Denewline(dc);
    if(((struct Element *)elt->sub1)->type == ET_STRING)
    {
        if(((struct Elementstring *)elt->sub1)->svalue)
        {
             Addtojbuffer(dc->jb,((struct Elementstring *)elt->sub1)->svalue,-1);
        }
    }
    else
    {
        Decompile(dc,elt->sub1);
    }
    dc->indent--;
    Denewline(dc);
    Addtojbuffer(dc->jb,"}",-1);
    dc->nosemicolon=TRUE;
}
static void Decase(struct Decompile *dc, struct Elementcase *elt)
{
    if(elt->isdefault)
    {
        Addtojbuffer(dc->jb,"default ",-1);
    }
    else
    {
        Addtojbuffer(dc->jb,"case ",-1);
        if(elt->expr) Decompile(dc,elt->expr);
    }
    Addtojbuffer(dc->jb,":",-1);

    Denewline(dc);
}

static void Depositive(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"+",1);
   Decompile(dc,elt->sub1);
}


static void Deplus(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,42);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," + ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deminus(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,42);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," - ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Demult(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,44);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," * ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Dediv(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,44);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," / ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Derem(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,44);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," % ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Debitand(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,28);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," & ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Debitor(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,24);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," | ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Debitxor(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,26);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," ^ ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deshleft(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,40);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," << ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deshright(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,40);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," >> ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deushright(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,40);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," >>> ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deeq(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,30);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," == ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Dene(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,30);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," != ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deexeq(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,30);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," === ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Denexeq(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,30);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," !== ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Delt(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,32);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," < ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Degt(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,32);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," > ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Dele(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,32);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," <= ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Dege(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,32);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," >= ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deand(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,22);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," && ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deor(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,20);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," || ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deassign(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,10);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," = ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deaplus(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,10);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," += ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deaminus(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,10);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," -= ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deamult(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,10);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," *= ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deadiv(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,10);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," /= ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Dearem(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,10);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," %= ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deabitand(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,10);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," &= ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deabitor(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,10);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," |= ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deabitxor(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,10);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," ^= ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deashleft(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,10);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," <<= ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deashright(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,10);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," >>= ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Deaushright(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,10);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," >>>= ",-1);
   Decompile(dc,elt->sub2);
   Delpar(dc,level);
}

static void Decomma(struct Decompile *dc,struct Element *elt)
{  Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," , ",-1);
   Decompile(dc,elt->sub2);
}

static void Dein(struct Decompile *dc, struct Element *elt)
{
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," in ",-1);
   Decompile(dc,elt->sub2);
}

static void Deinstanceof(struct Decompile *dc, struct Element *elt)
{
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," instanceof ",-1);
   Decompile(dc,elt->sub2);
}


static void Dewhile(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"while(",-1);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb,") ",1);
   Decompile(dc,elt->sub2);
}

static void Dedo(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"do",-1);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb,"while(",-1);
   Decompile(dc,elt->sub2);
   Addtojbuffer(dc->jb,") ",1);
}

static void Dewith(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"with(",-1);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb,") ",1);
   Decompile(dc,elt->sub2);
}

static void Dedot(struct Decompile *dc,struct Element *elt)
{  Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb,".",1);
   Decompile(dc,elt->sub2);
}

static void Deindex(struct Decompile *dc,struct Element *elt)
{  Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb,"[",1);
   Decompile(dc,elt->sub2);
   Addtojbuffer(dc->jb,"]",1);
}

static void Devar(struct Decompile *dc,struct Element *elt)
{  Decompile(dc,elt->sub1);
   if(elt->sub2)
   {  Addtojbuffer(dc->jb,"=",1);
      Decompile(dc,elt->sub2);
   }
}

static void Decond(struct Decompile *dc,struct Element *elt)
{  short level=Derpar(dc,5);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," ? ",-1);
   Decompile(dc,elt->sub2);
   Addtojbuffer(dc->jb," : ",-1);
   Decompile(dc,elt->sub3);
   Delpar(dc,level);
}

static void Deif(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"if(",-1);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb,") ",2);
   Decompile(dc,elt->sub2);
   if(elt->sub3)
   {  Desemicolon(dc);
      Denewline(dc);
      Addtojbuffer(dc->jb,"else ",-1);
      Decompile(dc,elt->sub3);
   }
}

static void Deforin(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"for(",-1);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb," in ",-1);
   Decompile(dc,elt->sub2);
   Addtojbuffer(dc->jb,") ",-1);
   Decompile(dc,elt->sub3);
}


static void Deswitch(struct Decompile *dc, struct Elementswitch *elt)
{
    struct Elementnode *enode;
    Addtojbuffer(dc->jb,"switch(",-1);
    Decompile(dc,elt->cond);
    Addtojbuffer(dc->jb,") {",-1);
    dc->indent++;
    for(enode=elt->subs.first;enode->next;enode=enode->next)
    {
        Denewline(dc);
        if(enode->sub) Decompile(dc,enode->sub);
        Desemicolon(dc);
    }
    dc->indent--;
    Denewline(dc);

    Addtojbuffer(dc->jb,"}",-1);
}


static void Detry(struct Decompile *dc, struct Elementtry *elt)
{
    Addtojbuffer(dc->jb,"try ",-1);
    Decompile(dc,elt->try);
    Addtojbuffer(dc->jb,"catch ",-1);
    Decompile(dc,elt->catch);
    Addtojbuffer(dc->jb,"finally ",-1);
    Decompile(dc,elt->finally);

}

static void Defor(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"for(",-1);
   Decompile(dc,elt->sub1);
   Addtojbuffer(dc->jb,";",1);
   Decompile(dc,elt->sub2);
   Addtojbuffer(dc->jb,";",1);
   Decompile(dc,elt->sub3);
   Addtojbuffer(dc->jb,") ",-1);
   Decompile(dc,elt->sub4);
}

static void Deinteger(struct Decompile *dc,struct Elementint *elt)
{  UBYTE buf[24];
   sprintf(buf,"%g",elt->ivalue);
   Addtojbuffer(dc->jb,buf,-1);
}

static void Defloat(struct Decompile *dc,struct Elementfloat *elt)
{  UBYTE buf[24];
   sprintf(buf,"%g",elt->fvalue);
   Addtojbuffer(dc->jb,buf,-1);
}

static void Deboolean(struct Decompile *dc,struct Elementint *elt)
{  Addtojbuffer(dc->jb,elt->ivalue?"true":"false",-1);
}

static void Destring(struct Decompile *dc,struct Elementstring *elt)
{  UBYTE *p,*q;
   UBYTE buf[6];
   Addtojbuffer(dc->jb,"\"",1);
   for(p=elt->svalue;*p;p++)
   {  switch(*p)
      {  case '"':   q="\\\"";break;
         case '\b':  q="\\b";break;
         case '\t':  q="\\t";break;
         case '\n':  q="\\n";break;
         case '\f':  q="\\f";break;
         case '\r':  q="\\r";break;
         case '\\':  q="\\\\";break;
         default:
            if(!isprint(*p))
            {  sprintf(buf,"\\x%02x",*p);
               q=buf;
            }
            else
            {  Addtojbuffer(dc->jb,p,1);
               q=NULL;
            }
            break;
      }
      if(q) Addtojbuffer(dc->jb,q,-1);
   }
   Addtojbuffer(dc->jb,"\"",1);
}

static void Deidentifier(struct Decompile *dc,struct Elementstring *elt)
{  Addtojbuffer(dc->jb,elt->svalue,-1);
}

static void Deregexp(struct Decompile *dc, struct Elementregexp *elt)
{
    Addtojbuffer(dc->jb,"/",1);
    if(elt->pattern) Addtojbuffer(dc->jb,elt->pattern,-1);
    Addtojbuffer(dc->jb,"/",1);
    if(elt->flags) Addtojbuffer(dc->jb,elt->flags,-1);
}

static void Dearray(struct Decompile *dc,struct Elementlist *elist)
{  struct Elementnode *enode;
   BOOL first;
   Addtojbuffer(dc->jb,"[",1);
   if(elist->subs.first->next && elist->subs.first->sub)
   {
      first=TRUE;
      for(enode=elist->subs.first;enode && enode->next;enode=enode->next)
      {  if(enode->sub)
         {  if(!first)
            {  Addtojbuffer(dc->jb,", ",-1);
            }
            first=FALSE;
            Decompile(dc,enode->sub);
         }
      }
   }
   Addtojbuffer(dc->jb,"]",1);

}

static void Deobject(struct Decompile *dc,struct Elementlist *elist)
{  struct Elementnode *enode;
   BOOL first;
   Addtojbuffer(dc->jb,"{",1);
   if(elist->subs.first->next && elist->subs.first->sub)
   {
      first=TRUE;
      for(enode=elist->subs.first;enode && enode->next;enode=enode->next)
      {  if(enode->sub)
         {
            if(!first)
            {  Addtojbuffer(dc->jb,", ",-1);
            }
            first=FALSE;
            Decompile(dc,enode->sub);
            Addtojbuffer(dc->jb,": ",-1);
            enode=enode->next;
            if(enode->sub)
            {
                Decompile(dc,enode->sub);
            }
         }
      }
   }
   Addtojbuffer(dc->jb,"}",1);

}

static void Delabel(struct Decompile *dc, struct Element *elt)
{
    Decompile(dc,elt->sub1);
    Addtojbuffer(dc->jb,":",-1);
    Decompile(dc,elt->sub2);
}

static void Defuncref(struct Decompile *dc, struct Elementfuncref *elt)
{
   // adebug("\nDEcompiling funcref %08lx function %08lx\n\n",elt,elt->func);

    if(elt->func)
    {
        if(elt->func->function)
        {
            Decompile(dc,(struct Element *)elt->func->function);
        }
    }

}

#ifdef JSDEBUG
static void Dedebug(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"debug ",-1);
   Decompile(dc,elt->sub1);
}
#endif

#ifdef JSADDRESS
static void Deaddress(struct Decompile *dc,struct Element *elt)
{  Addtojbuffer(dc->jb,"address ",-1);
   Decompile(dc,elt->sub1);
}
#endif

typedef void Decompelement(void *,void *);
static Decompelement *decomptab[]=
{
   NULL,
   (Decompelement *)Deprogram,
   (Decompelement *)Decall,
   (Decompelement *)Decompound,
   (Decompelement *)Devarlist,
   (Decompelement *)Detry,
   (Decompelement *)Defunction,
   (Decompelement *)Defuncref,
   (Decompelement *)Debreak,
   (Decompelement *)Decontinue,
   (Decompelement *)Dethis,
   (Decompelement *)Denull,
   (Decompelement *)Deempty,
   (Decompelement *)Denegative,
   (Decompelement *)Depositive,
   (Decompelement *)Denot,
   (Decompelement *)Debitneg,
   (Decompelement *)Depreinc,
   (Decompelement *)Depredec,
   (Decompelement *)Depostinc,
   (Decompelement *)Depostdec,
   (Decompelement *)Denew,
   (Decompelement *)Dedelete,
   (Decompelement *)Detypeof,
   (Decompelement *)Devoid,
   (Decompelement *)Dereturn,
   (Decompelement *)Dethrow,
   (Decompelement *)Deinternal,
   (Decompelement *)Defunceval,
   (Decompelement *)Decase,
   (Decompelement *)Deplus,
   (Decompelement *)Deminus,
   (Decompelement *)Demult,
   (Decompelement *)Dediv,
   (Decompelement *)Derem,
   (Decompelement *)Debitand,
   (Decompelement *)Debitor,
   (Decompelement *)Debitxor,
   (Decompelement *)Deshleft,
   (Decompelement *)Deshright,
   (Decompelement *)Deushright,
   (Decompelement *)Deeq,
   (Decompelement *)Dene,
   (Decompelement *)Deexeq,
   (Decompelement *)Denexeq,
   (Decompelement *)Delt,
   (Decompelement *)Degt,
   (Decompelement *)Dele,
   (Decompelement *)Dege,
   (Decompelement *)Deand,
   (Decompelement *)Deor,
   (Decompelement *)Deassign,
   (Decompelement *)Deaplus,
   (Decompelement *)Deaminus,
   (Decompelement *)Deamult,
   (Decompelement *)Deadiv,
   (Decompelement *)Dearem,
   (Decompelement *)Deabitand,
   (Decompelement *)Deabitor,
   (Decompelement *)Deabitxor,
   (Decompelement *)Deashleft,
   (Decompelement *)Deashright,
   (Decompelement *)Deaushright,
   (Decompelement *)Decomma,
   (Decompelement *)Dein,
   (Decompelement *)Deinstanceof,
   (Decompelement *)Dewhile,
   (Decompelement *)Dedo,
   (Decompelement *)Dewith,
   (Decompelement *)Dedot,
   (Decompelement *)Deindex,
   (Decompelement *)Devar,
   (Decompelement *)Decond,
   (Decompelement *)Deif,
   (Decompelement *)Deforin,
   (Decompelement *)Deswitch,
   (Decompelement *)Defor,
   (Decompelement *)Deinteger,
   (Decompelement *)Defloat,
   (Decompelement *)Deboolean,
   (Decompelement *)Destring,
   (Decompelement *)Deidentifier,
   (Decompelement *)Deregexp,
   (Decompelement *)Dearray,
   (Decompelement *)Deobject,
   (Decompelement *)Delabel,
#ifdef JSDEBUG
   (Decompelement *)Dedebug,
#endif
#ifdef JSADDRESS
   (Decompelement *)Deaddress,
#endif
};

static void Decompile(struct Decompile *dc,struct Element *elt)
{  if(elt && decomptab[elt->type])
   {
       if ((elt->type < sizeof(decomptab)/sizeof(Decompelement *)))
          decomptab[elt->type](dc,elt);

          else
       Addtojbuffer(dc->jb,"Decompilation error ", -1);

   }
}

/*-----------------------------------------------------------------------*/

static void Diselement(struct Element *elt)
{  if(elt->sub1) Disposelt(elt->sub1);
   if(elt->sub2) Disposelt(elt->sub2);
   if(elt->sub3) Disposelt(elt->sub3);
   if(elt->sub4) Disposelt(elt->sub4);
   FREE(elt);
}

static void Disinternal(struct Element *elt)
{  /* Don't free the internal code! */
   FREE(elt);
}

static void Disint(struct Elementint *elt)
{  FREE(elt);
}

static void Disfloat(struct Elementfloat *elt)
{  FREE(elt);
}

static void Disstring(struct Elementstring *elt)
{  if(elt->svalue) FREE(elt->svalue);
   FREE(elt);
}
static void Disregexp(struct Elementregexp *elt)
{
    if(elt->pattern) FREE(elt->pattern);
    if(elt->flags) FREE(elt->flags);
    FREE(elt);
}

static void Dislist(struct Elementlist *elt)
{  struct Elementnode *enode;
   while(enode=(struct Elementnode *)RemHead((struct List *)&elt->subs))
   {  if(enode->sub) Disposelt(enode->sub);
      FREE(enode);
   }
   FREE(elt);
}

static void Disfunc(struct Elementfunc *elt)
{
   struct Elementnode *enode;
   UBYTE dbgt[192];

   if(httpdebug && elt)
   {
      sprintf((char *)dbgt,"[JSGC] DISFUNC_ENTER elt=%08lx bcode=%08lx body=%08lx\n",
         (unsigned long)(ULONG)elt,
         (unsigned long)(ULONG)elt->bcode,
         (unsigned long)(ULONG)elt->body);
      Jcomp_log_line(dbgt);
   }
   if(elt->bcode)
   {
      if(httpdebug)
      {
         sprintf((char *)dbgt,"[JSGC] DISFUNC_BEFORE_JVMFREE elt=%08lx\n",(unsigned long)(ULONG)elt);
         Jcomp_log_line(dbgt);
      }
      Jvmfreechunk(elt->bcode);
      if(httpdebug)
      {
         sprintf((char *)dbgt,"[JSGC] DISFUNC_AFTER_JVMFREE elt=%08lx\n",(unsigned long)(ULONG)elt);
         Jcomp_log_line(dbgt);
      }
      elt->bcode=NULL;
   }
   while(enode=(struct Elementnode *)RemHead((struct List *)&elt->subs))
   {
      if(enode->sub)
      {
         Disposelt(enode->sub);
      }
      FREE(enode);
   }
   if(httpdebug && elt)
   {
      sprintf((char *)dbgt,"[JSGC] DISFUNC_AFTER_SUBS elt=%08lx\n",(unsigned long)(ULONG)elt);
      Jcomp_log_line(dbgt);
   }
   if(elt->body)
   {
      if(httpdebug)
      {
         sprintf((char *)dbgt,"[JSGC] DISFUNC_BEFORE_BODY elt=%08lx body=%08lx\n",
            (unsigned long)(ULONG)elt,(unsigned long)(ULONG)elt->body);
         Jcomp_log_line(dbgt);
      }
      Disposelt(elt->body);
      if(httpdebug)
      {
         sprintf((char *)dbgt,"[JSGC] DISFUNC_AFTER_BODY elt=%08lx\n",(unsigned long)(ULONG)elt);
         Jcomp_log_line(dbgt);
      }
      elt->body=NULL;
   }
   if(elt->name)
   {
      FREE(elt->name);
   }
   elt->fscope=NULL;
   if(httpdebug && elt)
   {
      sprintf((char *)dbgt,"[JSGC] DISFUNC_BEFORE_FREE elt=%08lx\n",(unsigned long)(ULONG)elt);
      Jcomp_log_line(dbgt);
   }
   FREE(elt);
}

static void Disfuncref(struct Elementfuncref *elt)
{
    /* only free ourself as the we don't own the function object */
    /* but once we are gone we should unkeep the object so it can be disposed */
    if(elt->func)Keepobject(elt->func,FALSE);
    FREE(elt);
}

static void Discase(struct Elementcase *elt)
{
    if(elt->expr)
    {
        Disposelt(elt->expr);
        elt->expr=NULL;
    }
    FREE(elt);
}


static void Disswitch(struct Elementswitch *elt)
{
   struct Elementnode *enode;
   while(enode=(struct Elementnode *)RemHead((struct List *)&elt->subs))
   {  if(enode->sub) Disposelt(enode->sub);
      FREE(enode);
   }
   FREE(elt);
}

static void Distry(struct Elementtry *elt)
{
    if(elt->try)Disposelt(elt->try);
    if(elt->catch)Disposelt(elt->catch);
    if(elt->finally)Disposelt(elt->finally);
    FREE(elt);
}

typedef void Diselementf(void *);
static Diselementf *distab[]=
{
   NULL,
   (Diselementf *)Dislist,       /* program */
   (Diselementf *)Dislist,       /* call */
   (Diselementf *)Dislist,       /* compound */
   (Diselementf *)Dislist,       /* varlist */
   (Diselementf *)Distry,        /* try catch */
   (Diselementf *)Disfunc,       /* function */
   (Diselementf *)Disfuncref,    /* funcref */
   (Diselementf *)Diselement,    /* break */
   (Diselementf *)Diselement,    /* continue */
   (Diselementf *)Diselement,    /* this */
   (Diselementf *)Diselement,    /* null */
   (Diselementf *)Diselement,    /* empty */
   (Diselementf *)Diselement,    /* negative */
   (Diselementf *)Diselement,    /* positive */
   (Diselementf *)Diselement,    /* not */
   (Diselementf *)Diselement,    /* bitneg */
   (Diselementf *)Diselement,    /* preinc */
   (Diselementf *)Diselement,    /* predec */
   (Diselementf *)Diselement,    /* postinc */
   (Diselementf *)Diselement,    /* postdec */
   (Diselementf *)Diselement,    /* new */
   (Diselementf *)Diselement,    /* delete */
   (Diselementf *)Diselement,    /* typeof */
   (Diselementf *)Diselement,    /* void */
   (Diselementf *)Diselement,    /* return */
   (Diselementf *)Diselement,    /* throw */
   (Diselementf *)Disinternal,   /* internal */
   (Diselementf *)Diselement,    /* funceval */
   (Diselementf *)Discase,       /* case */
   (Diselementf *)Diselement,    /* plus */
   (Diselementf *)Diselement,    /* minus */
   (Diselementf *)Diselement,    /* mult */
   (Diselementf *)Diselement,    /* div */
   (Diselementf *)Diselement,    /* rem */
   (Diselementf *)Diselement,    /* bitand */
   (Diselementf *)Diselement,    /* bitor */
   (Diselementf *)Diselement,    /* bitxor */
   (Diselementf *)Diselement,    /* shleft */
   (Diselementf *)Diselement,    /* shright */
   (Diselementf *)Diselement,    /* ushright */
   (Diselementf *)Diselement,    /* eq */
   (Diselementf *)Diselement,    /* ne */
   (Diselementf *)Diselement,    /* exeq */
   (Diselementf *)Diselement,    /* nexeq */
   (Diselementf *)Diselement,    /* lt */
   (Diselementf *)Diselement,    /* gt */
   (Diselementf *)Diselement,    /* le */
   (Diselementf *)Diselement,    /* ge */
   (Diselementf *)Diselement,    /* and */
   (Diselementf *)Diselement,    /* or */
   (Diselementf *)Diselement,    /* assign */
   (Diselementf *)Diselement,    /* aplus */
   (Diselementf *)Diselement,    /* aminus */
   (Diselementf *)Diselement,    /* amult */
   (Diselementf *)Diselement,    /* adiv */
   (Diselementf *)Diselement,    /* arem */
   (Diselementf *)Diselement,    /* abitand */
   (Diselementf *)Diselement,    /* abitor */
   (Diselementf *)Diselement,    /* abitxor */
   (Diselementf *)Diselement,    /* ashleft */
   (Diselementf *)Diselement,    /* ashright */
   (Diselementf *)Diselement,    /* aushright */
   (Diselementf *)Diselement,    /* comma */
   (Diselementf *)Diselement,    /* in */
   (Diselementf *)Diselement,    /* instanceof */
   (Diselementf *)Diselement,    /* while */
   (Diselementf *)Diselement,    /* do */
   (Diselementf *)Diselement,    /* with */
   (Diselementf *)Diselement,    /* dot */
   (Diselementf *)Diselement,    /* index */
   (Diselementf *)Diselement,    /* var */
   (Diselementf *)Diselement,    /* cond */
   (Diselementf *)Diselement,    /* if */
   (Diselementf *)Diselement,    /* forin */
   (Diselementf *)Disswitch,     /* switch */
   (Diselementf *)Diselement,    /* for */
   (Diselementf *)Disint,        /* integer */
   (Diselementf *)Disfloat,      /* float */
   (Diselementf *)Disint,        /* boolean */
   (Diselementf *)Disstring,     /* string */
   (Diselementf *)Disstring,     /* identifier */
   (Diselementf *)Disregexp,     /* regexp */
   (Diselementf *)Dislist,       /* array */
   (Diselementf *)Dislist,       /* object */
   (Diselementf *)Diselement,    /* label */
#ifdef JSDEBUG
   (Diselementf *)Diselement,    /* debug */
#endif
#ifdef JSADDRESS
   (Diselementf *)Diselement,    /* address */
#endif
};

static void Disposelt(struct Element *elt)
{
   ULONG ntab;
   UBYTE buf[160];

   if(!elt)
   {
      return;
   }
   if(Jcomp_seen_or_mark(elt))
   {
      if(httpdebug)
      {
         sprintf((char *)buf,"[JSGC] DISPOSE_REPEAT elt=%08lx\n",(unsigned long)(ULONG)elt);
         Jcomp_log_line(buf);
      }
      return;
   }
   ntab=(ULONG)(sizeof(distab)/sizeof(distab[0]));
   /* Element.type indexes distab; out-of-range reads were undefined and could fault. */
   if((ULONG)elt->type==0UL || (ULONG)elt->type>=ntab)
   {
      if(httpdebug)
      {
         sprintf((char *)buf,"[JSGC] DISPOSE_BADTYPE elt=%08lx type=%lu tab=%lu\n",
            (unsigned long)(ULONG)elt,
            (unsigned long)(ULONG)elt->type,
            (unsigned long)ntab);
         Jcomp_log_line(buf);
      }
      return;
   }
   if(distab[elt->type])
   {
      distab[elt->type](elt);
   }
}

/*-----------------------------------------------------------------------*/

void Jcompile(struct Jcontext *jc,UBYTE *source)
{  void *pa;
   if(!jc || !source)
   {  return;
   }
   if(pa=Newparser(jc,source))
   {  jc->nexttoken=Nexttoken(pa);
      Compileprogram(jc,pa);
      FREE(pa);
   }
}

struct Jobject *Jcompiletofunction(struct Jcontext *jc,UBYTE *source,UBYTE *name)
{  struct Elementfunc *func;
   struct Jobject *fobj=NULL;
   struct Jcontext jc2={0};
   jc2.pool=jc->pool;
   NewList((struct List *)&jc2.objects);
   NewList((struct List *)&jc2.functions);
   jc2.generation=jc->generation;
   Jcompile(&jc2,source);
   if(!(jc2.flags&JCF_ERROR))
   {  if(func=ALLOCSTRUCT(Elementfunc,1,0,jc->pool))
      {  func->type=ET_FUNCTION;
         func->generation=jc->generation;
         func->linenr=0;
         NewList((struct List *)&func->subs);
         func->name=Jdupstr(name,-1,jc->pool);
         func->name[0]=toupper(func->name[0]);
         func->body=jc2.program;
         ((struct Elementlist *)func->body)->type=ET_COMPOUND;
         /* Create function object */
         if(fobj=Newobject(jc))
         {  fobj->function=func;
         }
         /* Remember current scope with function */
         func->fscope=jc->fscope;
         Tryemitfuncbytecode(jc,func);
      }
   }
   if(!fobj)
   {  Jdispose(jc2.program);
   }
   return fobj;
}

struct Jbuffer *Jdecompile(struct Jcontext *jc,struct Element *elt)
{  struct Decompile dc={0};
   if(dc.jb=Newjbuffer(jc->pool))
   {  Decompile(&dc,elt);
   }
   return dc.jb;
}

void Jdispose(struct Element *elt)
{  if(elt)
   {
      Jcomp_dispose_newrun();
      Disposelt(elt);
   }
}
