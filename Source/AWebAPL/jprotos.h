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

/* jprotos.h - AWeb js prototypes */

#include <stdarg.h>

/*-----------------------------------------------------------------------*/
/*-- jslib --------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/
/* Jgetjmemsessionstats/JmemSessionStats: see awebjs.h; jslib prototypes in jslib.h */

   /* Duplicate string */
extern UBYTE *Jdupstr(UBYTE *str,long len,void *pool);

   /* Dynamic buffer, text is kept null-terminated */
extern struct Jbuffer *Newjbuffer(void *pool);
extern void Freejbuffer(struct Jbuffer *jb);
extern void Addtojbuffer(struct Jbuffer *jb,UBYTE *text,long length);

   /* Show error. Returns true if all errors are to be ignored cq debugger wanted. */
   /* Set pos to <0 to show a runtime error requester. */
extern BOOL Errorrequester(struct Jcontext *jc,long lnr,UBYTE *line,
   long pos,UBYTE *msg,va_list args);

   /* Call feedback. Returns TRUE if continue, FALSE if break. */
extern BOOL Feedback(struct Jcontext *jc);

   /* Return TRUE if caller owns the active window */
extern BOOL Calleractive(void);

/*-----------------------------------------------------------------------*/
/*-- jarray -------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

extern void Initarray(struct Jcontext *jc, struct Jobject *jscope);

   /* Create a new empty Array object, Useobject() it once. */
extern struct Jobject *Newarray(struct Jcontext *jc);

   /* Find the nth array element, or NULL */
extern struct Variable *Arrayelt(struct Jobject *jo,long n);

   /* Add an element to this array */
extern struct Variable *Addarrayelt(struct Jcontext *jc,struct Jobject *jo);

   /* Tests if this object is an array */
extern BOOL Isarray(struct Jobject *jo);

/*-----------------------------------------------------------------------*/
/*-- jboolean -----------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

extern void Initboolean(struct Jcontext *jc, struct Jobject *jscope);

   /* Create a new Boolean object, Useobject() it once. */
extern struct Jobject *Newboolean(struct Jcontext *jc,BOOL bvalue);

/*-----------------------------------------------------------------------*/
/*-- jcomp --------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

   /* Compile the source and construct an element tree */
extern void Jcompile(struct Jcontext *jc,UBYTE *source);

   /* Compile this source and make it into a function object. */
struct Jobject *Jcompiletofunction(struct Jcontext *jc,UBYTE *source,UBYTE *name);

   /* Decompile the source */
extern struct Jbuffer *Jdecompile(struct Jcontext *jc,struct Element *elt);

   /* Dispose this element */
extern void Jdispose(struct Element *elt);

/*-----------------------------------------------------------------------*/
/*-- jdata --------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

   /* Call this property function */
extern BOOL Callproperty(struct Jcontext *jc,struct Jobject *jo,UBYTE *name);

   /* Value processing */
extern void Clearvalue(struct Value *v);
extern void Asgvalue(struct Value *to,struct Value *from);
extern void Asgnumber(struct Value *to,UBYTE attr,double n);
extern void Asgboolean(struct Value *to,BOOL b);
extern void Asgstring(struct Value *to,UBYTE *s,void *pool);
extern void Asgstringlen(struct Value *to,UBYTE *s,long len,void *pool);
extern void Asgobject(struct Value *to,struct Jobject *jo);
extern void Asgfunction(struct Value *to,struct Jobject *f,struct Jobject *fthis);

   /* WARNING: Tostring() cannot be called for ex->val directly. */
extern void Tostring(struct Value *v,struct Jcontext *jc);
extern void Tonumber(struct Value *v,struct Jcontext *jc);
extern void Toboolean(struct Value *v,struct Jcontext *jc);
extern void Toobject(struct Value *v,struct Jcontext *jc);
extern void Tofunction(struct Value *v,struct Jcontext *jc);

   /* Default toString property function */
extern void Defaulttostring(struct Jcontext *jc);

   /* Variables */
extern struct Variable *Newvar(UBYTE *name,struct Jcontext *jc);
extern void Disposevar(struct Variable *var);

   /* Objects */
extern struct Jobject *Newobject(struct Jcontext *jc);
extern void Disposeobject(struct Jobject *jo);
extern void Clearobject(struct Jobject *jo,UBYTE **except);
extern struct Variable *Addproperty(struct Jobject *jo,UBYTE *name);
extern struct Variable *Findproperty(struct Jobject *jo,UBYTE *name);
extern struct Variable *Getownproperty(struct Jobject *jo,UBYTE *name);
extern struct Variable *Getproperty(struct Jobject *jo,UBYTE *name);
extern BOOL Deleteownproperty(struct Jobject *jo,UBYTE *name);
extern BOOL _Generic_Deleteownproperty(struct Jobject *jo,UBYTE *name);
extern struct Variable *_Generic_Addproperty(struct Jobject *jo,UBYTE *name);
extern struct Variable *_Generic_Getownproperty(struct Jobject *jo,UBYTE *name);
extern BOOL _Array_Deleteownproperty(struct Jobject *jo,UBYTE *name);
extern struct Variable *_Array_Addproperty(struct Jobject *jo,UBYTE *name);
extern struct Variable *_Array_Getownproperty(struct Jobject *jo,UBYTE *name);

   /* Convert string to unsigned 32-bit integer */
extern BOOL Touint32(STRPTR str, ULONG *num);

   /* Objhook for .prototype */
extern BOOL Prototypeohook(struct Objhookdata *h);

   /* Variable hook for .prototype properties */
extern BOOL Protopropvhook(struct Varhookdata *h);

   /* General hook function for constants (that cannot change their value) */
extern BOOL Constantvhook(struct Varhookdata *h);

   /* Hook call functions */
extern BOOL Callvhook(struct Variable *var,struct Jcontext *jc,short code,struct Value *val);
extern BOOL Callohook(struct Jobject *jo,struct Jcontext *jc,short code,UBYTE *name);

   /* Garbage collector */
extern void Keepobject(struct Jobject *jo,BOOL used);
extern void Garbagecollect(struct Jcontext *jc);

extern void Dumpobjects(struct Jcontext *jc);

/*-----------------------------------------------------------------------*/
/*-- jdate --------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

extern void Initdate(struct Jcontext *jc, struct Jobject *jscope);

/*-----------------------------------------------------------------------*/
/*-- jerror -------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

extern void Initerror(struct Jcontext *jc, struct Jobject *jscope);

extern struct Jobject *Newerror(struct Jcontext *jc,UBYTE *message);
extern struct Jobject *Newnativeerror(struct Jcontext *jc, STRPTR type, UBYTE *message);

   /* Wall-clock milliseconds since 1970-01-01 (JS Date/Math); not awebplugin Today(). */
extern double Jmillis(void);

/*-----------------------------------------------------------------------*/
/*-- jdebug -------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

   /* Start, stop the debugger */
extern void Startdebugger(struct Jcontext *jc);
extern void Stopdebugger(struct Jcontext *jc);

   /* Debug halt */
extern void Setdebugger(struct Jcontext *jc,struct Element *elt);

/*-----------------------------------------------------------------------*/
/*-- jexe ---------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

   /* Jexecute a program */
extern void Jexecute(struct Jcontext *jc,struct Jobject *jthis,struct Jobject **gwtab);
extern void Runtimeerror(struct Jcontext *jc,STRPTR type,struct Element *elt,UBYTE *msg,...);
extern void Dumpjscallstack(struct Jcontext *jc,long fh,struct Element *errsite);

struct JBytecodeChunk;
extern BOOL Jexecchunk(struct Jcontext *jc, struct JBytecodeChunk *ch);
extern void Jvmfreechunk(struct JBytecodeChunk *ch);

   /* Create a function object for this internal function. */
   /* Varargs are argument names (UBYTE *) terminated by NULL. */
extern struct Jobject *Internalfunction(struct Jcontext *jc,UBYTE *name,
   void (*code)(void *),...);
extern struct Jobject *InternalfunctionA(struct Jcontext *jc,UBYTE *name,
   void (*code)(void *),UBYTE **args);

   /* Adds a function object to global variable list */
extern void Addglobalfunction(struct Jcontext *jc,struct Jobject *f);

   /* Adds a function object to an object's properties */
extern struct Variable *Addinternalproperty(struct Jcontext *jc,
   struct Jobject *jo,struct Jobject *f);

   /* Adds the .prototype property to a function object */
extern void Addprototype(struct Jcontext *jc,struct Jobject *jo, struct Jobject *prototype);

   /* Get prototype object from a function object */
extern struct Jobject *Getprototype(struct Jobject *jo);

   /* Add a function object to the object's prototype */
extern void Addtoprototype(struct Jcontext *jc,struct Jobject *jo,struct Jobject *f);

   /* Call this function without parameters with this object as "this" */
extern void Callfunctionbody(struct Jcontext *jc,struct Elementfunc *func,
   struct Jobject *jthis);

   /* Call this function with supplied arguments (must be struct Value *, NULL terminated) */
extern void Callfunctionargs(struct Jcontext *jc,struct Elementfunc *func,
   struct Jobject *jthis,...);

   /* Execute an element */
extern void Executeelem(struct Jcontext *jc,struct Element *elt);

   /* Create a new function from an element */
extern struct Function *Newfunction(struct Jcontext *jc, struct Elementfunc *func);

   /* Dispose a function */
extern void Disposefunction(struct Function *f);

   /* Add .constructor, default .toString() and .prototype properties */
extern void Initconstruct(struct Jcontext *jc,struct Jobject *jo,STRPTR name,struct Jobject *constructor);

   /* Evaluate this string */
extern void Jeval(struct Jcontext *jc,UBYTE *s);

   /* Expand a Jcontext with run-time context */
extern BOOL Newexecute(struct Jcontext *jc);

   /* Free run-time context */
extern void Freeexecute(struct Jcontext *jc);

/*-----------------------------------------------------------------------*/
/*-- jfunction ----------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

extern void Initfunction(struct Jcontext *jc, struct Jobject *jscope);

/*-----------------------------------------------------------------------*/
/*-- jmath --------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

extern void Initmath(struct Jcontext *jc, struct Jobject *jscope);

/*-----------------------------------------------------------------------*/
/*-- jparse -------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

   /* Extract the next token */
extern struct Token *Nexttoken(struct Parser *pa);

   /* Create a new parser */
extern void *Newparser(struct Jcontext *jc,UBYTE *source);
extern void Freeparser(void *parser);

   /* Report error message. Total msg length must not exceed 127 characters. */
extern void Errormsg(struct Parser *pa,UBYTE *msg,...);

   /* Return name of token */
extern UBYTE *Tokenname(USHORT id);

   /* Get state ID, compare to see if parser has advanced. */
extern ULONG Parserstate(struct Parser *pa);

   /* Get current line number */
extern long Plinenr(struct Parser *pa);

   /* Save parser state for backtracking */
extern void *Saveparser(struct Jcontext *jc, struct Parser *pa);

   /* Restore parser state from saved state */
extern void Restoreparser(struct Jcontext *jc, struct Parser *pa, void *saved);

   /* Free saved parser state */
extern void Freesavedparser(struct Jcontext *jc, void *saved);

   /* Skip newlines as whitespace */
extern void Pskipnewline(struct Parser *pa, BOOL skip);

/*-----------------------------------------------------------------------*/
/*-- memory -------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

   /* allocate in private pool. If pool==NULL, allocate unpooled */
extern void *JPallocmem(long size,ULONG flags,void *pool);

   /* free memory, works for all pools and unpooled memory */
extern void JFreemem(void *mem);

   /* get the pool used for a given memory block */
extern void *JGetpool(void *p);

   /* Session stats for one interpreter pool (JPallocmem/JFreemem on jc->pool). */
extern void JmemSessionBind(void *pool);
extern void JmemSessionUnbind(void);
extern void JmemSessionGetStats(ULONG *allocs,ULONG *frees,LONG *outstanding_bytes);
extern void JmemSessionGetStatsEx(
   ULONG *allocs,ULONG *frees,LONG *outstanding_bytes,
   ULONG *total_alloc_bytes,ULONG *total_free_bytes,LONG *peak_outstanding_bytes);
extern void JmemPrintTeardown(struct Jcontext *jc);

/*-----------------------------------------------------------------------*/
/*-- number -------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

extern void Initnumber(struct Jcontext *jc, struct Jobject *jscope);

   /* Create a new Number object, Useobject() it once. */
extern struct Jobject *Newnumber(struct Jcontext *jc,UBYTE attr,double nvalue);

/*-----------------------------------------------------------------------*/
/*-- object -------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

extern void Initobject(struct Jcontext *jc, struct Jobject *jscope);

/*-----------------------------------------------------------------------*/
/*-- string -------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

extern void Initstring(struct Jcontext *jc, struct Jobject *jscope);

   /* Create a new String object, Useobject() it once. */
extern struct Jobject *Newstring(struct Jcontext *jc,UBYTE *svalue);

/*-----------------------------------------------------------------------*/
/*-- regexp -------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

extern void Initregexp(struct Jcontext *jc, struct Jobject *jscope);

   /* Create a new RegExp object, Useobject() it once. */
extern struct Jobject *Newregexp(struct Jcontext *jc,UBYTE *pattern, UBYTE *flags);

   /* Apply a regular expression to a string */
extern struct Jobject *Applyregexp(struct Jcontext *jc, struct Jobject *jo, UBYTE *match);

   /* Split a string using a regular expression */
extern struct Jobject *Splitregexp(struct Jcontext *jc, struct Jobject *jo, UBYTE *match, unsigned int limit);

/*-----------------------------------------------------------------------*/
/*--        -------------------------------------------------------------*/
/*-----------------------------------------------------------------------*/

