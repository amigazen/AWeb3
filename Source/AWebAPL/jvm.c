/**********************************************************************
 *
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2026 amigazen project
 *
 * Minimal stack VM for JBytecodeChunk: literals, numeric +, return.
 * Unsupported opcodes or corrupt chunks fail; caller falls back to AST.
 *
 **********************************************************************/

#include "awebjs.h"
#include "jbytecode.h"
#include "jprotos.h"
#include <math.h>

static int jvm_isinf(double x)
{
   return (x == x) && (x * 2.0 == x) && (x != 0.0);
}

/* ECMA additive numeric + attribute table (see jexe.c Exeplus). */
static UBYTE jvm_attrtab[4][4] =
{
   { VNA_VALID, VNA_NAN, VNA_INFINITY, VNA_NEGINFINITY },
   { VNA_NAN, VNA_NAN, VNA_NAN, VNA_NAN },
   { VNA_INFINITY, VNA_NAN, VNA_INFINITY, VNA_NAN },
   { VNA_NEGINFINITY, VNA_NAN, VNA_NAN, VNA_NEGINFINITY }
};

void Jvmfreechunk(struct JBytecodeChunk *ch)
{
   ULONG i;

   if(!ch)
   {
      return;
   }
   if(ch->consts && ch->ctypes)
   {
      for(i = 0; i < ch->numconsts; i++)
      {
         if(ch->ctypes[i] == JBCC_NUMBER || ch->ctypes[i] == JBCC_STRING)
         {
            if(ch->consts[i])
            {
               FREE(ch->consts[i]);
            }
         }
      }
   }
   FREE(ch->consts);
   FREE(ch->ctypes);
   FREE(ch->code);
   FREE(ch);
}

BOOL Jexecchunk(struct Jcontext *jc, struct JBytecodeChunk *ch)
{
   struct Value stk[32];
   struct Value a;
   struct Value b;
   struct Function *f;
   ULONG sp;
   ULONG pc;
   UBYTE op;
   UWORD idx;
   double *dp;
   UBYTE *sptr;
   double n;
   UBYTE v;
   {
      return FALSE;
   }
   if(ch->maxstack > 31)
   {
      return FALSE;
   }
   f = jc->functions.first;
   if(!f)
   {
      return FALSE;
   }

   for(sp = 0; sp < 32; sp++)
   {
      stk[sp].type = VTP_UNDEFINED;
      stk[sp].attr = 0;
   }
   sp = 0;
   pc = 0;
   while(pc < ch->length)
   {
      op = ch->code[pc];
      pc++;
      switch(op)
      {
      case JBC_OP_NOP:
         break;
      case JBC_OP_UNDEFINED:
         Clearvalue(&stk[sp]);
         sp++;
         break;
      case JBC_OP_NULL:
         Clearvalue(&stk[sp]);
         Asgobject(&stk[sp], NULL);
         sp++;
         break;
      case JBC_OP_TRUE:
         Clearvalue(&stk[sp]);
         Asgboolean(&stk[sp], TRUE);
         sp++;
         break;
      case JBC_OP_FALSE:
         Clearvalue(&stk[sp]);
         Asgboolean(&stk[sp], FALSE);
         sp++;
         break;
      case JBC_OP_NUMBER:
         if(pc + 2 > ch->length)
         {
            goto jbc_fail;
         }
         idx = (UWORD)ch->code[pc] | ((UWORD)ch->code[pc + 1] << 8);
         pc += 2;
         if(idx >= ch->numconsts || !ch->ctypes || ch->ctypes[idx] != JBCC_NUMBER)
         {
            goto jbc_fail;
         }
         dp = (double *)ch->consts[idx];
         if(!dp)
         {
            goto jbc_fail;
         }
         Clearvalue(&stk[sp]);
         Asgnumber(&stk[sp], VNA_VALID, *dp);
         sp++;
         break;
      case JBC_OP_STRING:
         if(pc + 2 > ch->length)
         {
            goto jbc_fail;
         }
         idx = (UWORD)ch->code[pc] | ((UWORD)ch->code[pc + 1] << 8);
         pc += 2;
         if(idx >= ch->numconsts || !ch->ctypes || ch->ctypes[idx] != JBCC_STRING)
         {
            goto jbc_fail;
         }
         sptr = (UBYTE *)ch->consts[idx];
         Clearvalue(&stk[sp]);
         Asgstring(&stk[sp], sptr ? sptr : (UBYTE *)"", jc->pool);
         sp++;
         break;
      case JBC_OP_ADD:
         if(sp < 2)
         {
            goto jbc_fail;
         }
         sp--;
         Asgvalue(&b, &stk[sp]);
         Clearvalue(&stk[sp]);
         sp--;
         Asgvalue(&a, &stk[sp]);
         Tonumber(&a, jc);
         Tonumber(&b, jc);
         n = 0.0;
         v = jvm_attrtab[a.attr][b.attr];
         if(v == VNA_VALID)
         {
            n = a.value.nvalue + b.value.nvalue;
            if(jvm_isinf(n))
            {
               v = (UBYTE)(a.value.nvalue > 0 ? VNA_INFINITY : VNA_NEGINFINITY);
            }
         }
         Clearvalue(&stk[sp]);
         Asgnumber(&stk[sp], v, n);
         Clearvalue(&a);
         Clearvalue(&b);
         sp++;
         break;
      case JBC_OP_RETURN:
         if(sp < 1)
         {
            goto jbc_fail;
         }
         sp--;
         Asgvalue(&f->retval, &stk[sp]);
         Clearvalue(&stk[sp]);
         Asgvalue(jc->val, &f->retval);
         jc->complete = ECO_RETURN;
         pc = ch->length;
         break;
      default:
         goto jbc_fail;
      }
   }
   while(sp > 0)
   {
      sp--;
      Clearvalue(&stk[sp]);
   }
   return TRUE;
jbc_fail:
   while(sp > 0)
   {
      sp--;
      Clearvalue(&stk[sp]);
   }
   return FALSE;
}
