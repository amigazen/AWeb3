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

/* jslib.h - AWeb js aweblib module interface */


/* Object types:
 *  Jcontext            = general Jscript context
 *  Jobject             = an object, with methods and properties
 *  Jvar                = a Jscript variable value, can contain several types
 */

extern struct Jcontext *Newjcontext(UBYTE *screenname);
extern void Freejcontext(struct Jcontext *jc);

/* Set debugger on or off */
extern void Jdebug(struct Jcontext *jc,BOOL debug);

/* Set compilation and runtime errors on or off */
extern void Jerrors(struct Jcontext *jc,BOOL comperrors,long runerrors,BOOL watch);

/* Runtime error values */
#define JERRORS_CONTINUE   -1 /* Don't show errors and try to continue script */
#define JERRORS_OFF        0  /* Don't show errors and stop script */
#define JERRORS_ON         1  /* Show errors and stop script */

/* Set new screen name */
extern void Jsetscreen(struct Jcontext *jc,UBYTE *screenname);

/* Compile, run and dispose a program. Returns the boolean return value.
 * (fscope) is a the object to collect functions and global variables in;
 *   probably the same as the first object in (gwtab).
 * (gwtab) is a NULL terminated array of objects to be used as global data scope.
 * (protkey) is for data protection.
 * (userdata) is for your own use. */
extern BOOL Runjprogram(struct Jcontext *jc,struct Jobject *fscope,
   UBYTE *source,struct Jobject *jthis,struct Jobject **gwtab,
   ULONG protkey,ULONG userdata);

/* Set a feedback function. This will be called regularly.
 * Return TRUE to continue, FALSE to stop running. */
typedef BOOL Jfeedback(struct Jcontext *jc);
extern void Jsetfeedback(struct Jcontext *jc,Jfeedback *jf);

/* Set starting line # for next interpreted source */
extern void Jsetlinenumber(struct Jcontext *jc,long linenr);

/* Change the current protection key for the execution context. */
extern void Jcprotect(struct Jcontext *jc,ULONG protkey);

/* Get the original execution result (Runjprogram() returns a BOOL value),
 * or NULL if no value was returned. */
extern struct Jvar *Jgetreturnvalue(struct Jcontext *jc);

/* Get the userdata from the most recent Runjprogram(). */
extern ULONG Jgetuserdata(struct Jcontext *jc);

/* Add, dispose a JS object */
extern struct Jobject *Newjobject(struct Jcontext *jc);
extern void Disposejobject(struct Jobject *jo);

/* Free a JS object. If you won't dispose the object yourself explicitly, but
 * has it assigned somewhere (e.g. as property in an object) You have to free
 * it. */
extern void Freejobject(struct Jobject *jo);

/* Clear a JS object (delete all its properties and methods) except those with
 * names listed in the (NULL terminated) except table. */
extern void Clearjobject(struct Jobject *jo,UBYTE **except);

/* Add a JS array object (dispose like normal object) */
extern struct Jobject *Newjarray(struct Jcontext *jc);

/* Hook function when property is added to object.
 * Returns TRUE if it understands the function, FALSE if default
 * action should be taken. */
typedef BOOL Objhookfunc(struct Objhookdata *data);
struct Objhookdata
{  struct Jcontext *jc;       /* Execution context */
   UWORD code;               /* Function code */
   struct Jobject *jo;        /* Object affected */
   UBYTE *name;               /* Name of property to add */
};

#define OHC_ADDPROPERTY 1  /* Add a property */

/* Hook to call when object is disposed. */
typedef void Objdisposehookfunc(void *internal);

/* Set object details.
 * hook is hook to call when a property is added
 * internal is internal data
 * dispose is function(void *) to dispose internal data,
 * called with internal data in A0 */
extern void Setjobject(struct Jobject *jo,Objhookfunc *hook,void *internal,
   Objdisposehookfunc *dispose);

/* Control if object properties may be obtained by calling the object as if
 * it were a function. */
extern void Jsetobjasfunc(struct Jobject *jo,BOOL asfunc);

/* Get the installed dispose hook function for this object.
 * You can use this to identify the object type. */
extern Objdisposehookfunc *Jdisposehook(struct Jobject *jo);

/* Sets a prototype object. (jo)->prototype will be set to (proto). */
extern void Jsetprototype(struct Jcontext *jc,struct Jobject *jo,struct Jobject *proto);

/* Add a property, or return the existing property value */
extern struct Jvar *Jproperty(struct Jcontext *jc,struct Jobject *jo,UBYTE *name);

/* Add a property only if it doesn't already exist. Returns NULL if property already exists. */
extern struct Jvar *Jaddproperty(struct Jcontext *jc,struct Jobject *jo,UBYTE *name);

/* Get the name of a property, or NULL */
extern UBYTE *Jpname(struct Jvar *jv);

/* Set property protection key. Reading this property is only possible if
 * protection key passed with Runjprogram() matches. */
extern void Jpprotect(struct Jvar *jv,ULONG protkey);

/* Hook function when variable is assigned to.
 * Returns TRUE if it understands the function, FALSE if default
 * action should be taken. */
typedef BOOL Varhookfunc(struct Varhookdata *data);
struct Varhookdata
{  struct Jcontext *jc;       /* Execution context */
   UWORD code;               /* Function code VHC_xxx */
   struct Jvar *var;          /* Variable affected (that has this hook defined) */
   void *hookdata;            /* Private data for hook */
   struct Jvar *value;        /* Value to set variable to (VHC_SET) or get into (VHC_GET) */
   UBYTE *name;               /* Name of variable to get or set */
};
#define VHC_SET         1  /* Set variable to this value */
#define VHC_GET         2  /* Get variable value */

/* Set property details. */
extern void Setjproperty(struct Jvar *jv,Varhookfunc *hook,void *hookdata);

/* Special values for Varhookfunc: */

   /* Use as property hook to make property read-only. */
   #define JPROPHOOK_READONLY    (Varhookfunc *)-1

   /* This is a synonym, use the variable passed as (hookdata) instead. */
   #define JPROPHOOK_SYNONYM     (Varhookfunc *)-2

/* Add a method for an internal function to this object. Returns the created
 * JS object for this function.
 * (code) will be called with 1 argument, the Jcontext handle in A0. */
extern struct Jobject *Addjfunction(struct Jcontext *jc,struct Jobject *jo,UBYTE *name,
   void (*code)(struct Jcontext *),...);
extern struct Jobject *AddjfunctionA(struct Jcontext *jc,struct Jobject *jo,UBYTE *name,
   void (*code)(struct Jcontext *),UBYTE **args);

/* Get a function call argument, or NULL */
extern struct Jvar *Jfargument(struct Jcontext *jc,long n);

/* Convert value to string, object, boolean, number */
extern UBYTE *Jtostring(struct Jcontext *jc,struct Jvar *jv);
extern struct Jobject *Jtoobject(struct Jcontext *jc,struct Jvar *jv);
extern BOOL Jtoboolean(struct Jcontext *jc,struct Jvar *jv);
extern long Jtonumber(struct Jcontext *jc,struct Jvar *jv);

/* Assign a string, object, boolean, number to a value.
 * If (jv)==NULL, the string is assigned to the execution result. */
extern void Jasgstring(struct Jcontext *jc,struct Jvar *jv,UBYTE *string);
extern void Jasgobject(struct Jcontext *jc,struct Jvar *jv,struct Jobject *jo);
extern void Jasgboolean(struct Jcontext *jc,struct Jvar *jv,BOOL bvalue);
extern void Jasgnumber(struct Jcontext *jc,struct Jvar *jv,long nvalue);

/* Get the direct scope object */
extern struct Jobject *Jthis(struct Jcontext *jc);

/* Get the object internal data */
extern void *Jointernal(struct Jobject *jo);

/* Add a new element to an array */
extern struct Jvar *Jnewarrayelt(struct Jcontext *jc,struct Jobject *jarray);

/* Tests if this object is an array */
extern BOOL Jisarray(struct Jcontext *jc,struct Jobject *jo);

/* Returns the array property with this name, or NULL if none exists */
extern struct Jobject *Jfindarray(struct Jcontext *jc,struct Jobject *jo,UBYTE *name);

/* Tests if variable contains a number */
extern BOOL Jisnumber(struct Jvar *jv);

/* Keep or free an object and all its referenced objects while garbage collecting */
extern void Jkeepobject(struct Jobject *jo,BOOL used);

/* Do the garbage collection */
extern void Jgarbagecollect(struct Jcontext *jc);

/* Allow or disallow garbage collection */
extern void Jallowgc(struct Jcontext *jc,BOOL allow);

/* Add an anonymous event handler function to this object.
 * If a property with this name already exists, it does nothing. */
extern void Jaddeventhandler(struct Jcontext *jc,struct Jobject *jo,
   UBYTE *name,UBYTE *source);

extern void Jdumpobjects(struct Jcontext *jc);

#pragma libcall AWebJSBase Newjcontext 1e 801
#pragma libcall AWebJSBase Freejcontext 24 801
#pragma libcall AWebJSBase Runjprogram 2a 10CBA9807

#pragma libcall AWebJSBase Newjobject 30 801
#pragma libcall AWebJSBase Disposejobject 36 801

#pragma libcall AWebJSBase AddjfunctionA 3c CBA9805
#pragma tagcall AWebJSBase Addjfunction 3c CBA9805

#pragma libcall AWebJSBase Jfargument 42 0802

#pragma libcall AWebJSBase Jtostring 48 9802
#pragma libcall AWebJSBase Jasgstring 4e A9803
#pragma libcall AWebJSBase Jasgobject 54 A9803

#pragma libcall AWebJSBase Setjobject 5a BA9804

#pragma libcall AWebJSBase Jproperty 60 A9803
#pragma libcall AWebJSBase Jaddproperty 66 A9803
#pragma libcall AWebJSBase Setjproperty 6c A9803

#pragma libcall AWebJSBase Jthis 72 801
#pragma libcall AWebJSBase Jointernal 78 801

#pragma libcall AWebJSBase Jasgboolean 7e 09803
#pragma libcall AWebJSBase Jtoboolean 84 9802

#pragma libcall AWebJSBase Newjarray 8a 801
#pragma libcall AWebJSBase Jnewarrayelt 90 9802

#pragma libcall AWebJSBase Jtoobject 96 9802
#pragma libcall AWebJSBase Jtonumber 9c 9802
#pragma libcall AWebJSBase Jasgnumber a2 09803

#pragma libcall AWebJSBase Jisarray a8 9802
#pragma libcall AWebJSBase Jfindarray ae A9803
#pragma libcall AWebJSBase Jsetprototype b4 A9803

#pragma libcall AWebJSBase Jgetuserdata ba 801
#pragma libcall AWebJSBase Jisnumber c0 801

#pragma libcall AWebJSBase Clearjobject c6 9802
#pragma libcall AWebJSBase Freejobject cc 801

#pragma libcall AWebJSBase Jdumpobjects d2 801
#pragma libcall AWebJSBase Jgetreturnvalue d8 801
#pragma libcall AWebJSBase Jpprotect de 0802
#pragma libcall AWebJSBase Jcprotect e4 0802
#pragma libcall AWebJSBase Jpname ea 801
#pragma libcall AWebJSBase Jdisposehook f0 801
#pragma libcall AWebJSBase Jsetfeedback f6 9802
#pragma libcall AWebJSBase Jdebug fc 0802
#pragma libcall AWebJSBase Jerrors 102 210804

#pragma libcall AWebJSBase Jkeepobject 108 0802
#pragma libcall AWebJSBase Jgarbagecollect 10e 801
#pragma libcall AWebJSBase Jsetlinenumber 114 0802
#pragma libcall AWebJSBase Jsetobjasfunc 11a 0802
#pragma libcall AWebJSBase Jsetscreen 120 9802
#pragma libcall AWebJSBase Jaddeventhandler 126 BA9804
#pragma libcall AWebJSBase Jallowgc 12c 0802
