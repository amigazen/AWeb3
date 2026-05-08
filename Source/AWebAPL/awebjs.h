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

/* awebjs.h - AWeb js common definitions */

#ifndef AWEBJS_H
#define AWEBJS_H

#include <exec/types.h>
#include <exec/memory.h>
#include <ezlists.h>

#include <clib/exec_protos.h>
#include <clib/alib_protos.h>
#include <clib/locale_protos.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pragmas/exec_pragmas.h>
#include <pragmas/locale_pragmas.h>

/* #define JSDEBUG */
/* #define JSADDRESS */

// #define debug KPrintF
#define debug KPrintF

#define STRNIEQUAL(a,b,n)  !strnicmp(a,b,n)
#define STRNEQUAL(a,b,n)   !strncmp(a,b,n)
#define STRIEQUAL(a,b)     !stricmp(a,b)
#define STREQUAL(a,b)      !strcmp(a,b)

#ifndef BOOLVAL
#define BOOLVAL(x)         ((x)?TRUE:FALSE)
#endif

#ifndef ALLOCTYPE
#define ALLOCTYPE(t,n,f,p)    (t*)JPallocmem((n)*sizeof(t),(f)|MEMF_PUBLIC,p)
#define ALLOCSTRUCT(s,n,f,p)  ALLOCTYPE(struct s,n,f,p)
#endif
#define FREE(p)               JFreemem(p)

#define ALLOCOBJECT(jc)      (struct Jobject *)JPallocmem(sizeof(struct Jobject),MEMF_PUBLIC,jc->objpool)
#define ALLOCVAR(jc)         (struct Variable *)JPallocmem(sizeof(struct Variable),MEMF_PUBLIC,jc->varpool)

/* get pointer to first vararg after p */
#define VARARG(p)          (void *)((ULONG)&p+sizeof(p))

#ifndef OSNEED
#ifdef NEED35
#define OSNEED(a,b) (b)
#else
#define OSNEED(a,b) (a)
#endif
#endif

extern struct Locale *locale;
/* LocaleBase is provided by proto/locale.h */
extern struct Hook idcmphook;

/*-----------------------------------------------------------------------*/

struct Token
{  UWORD id;
   UBYTE *svalue;
   UBYTE *svalue2;  /* added to handle regexp flags typical regexp literal parses /svalue/svalue2 */
   long ivalue;
   double fvalue;
};

/*-----------------------------------------------------------------------*/

typedef BOOL Jfeedback(struct Jcontext *jc);
#define NUM_ERRORTYPES  6

#include "jatom.h"

struct JPropIdxEnt;
struct JBytecodeChunk;

#define JATOM_NUM_BUCKETS 64
#define JPROP_IDX_BUCKETS 16

/* JPallocmem/JFreemem session counters for the interpreter pool (see jmemory JmemSessionBind). */
struct JmemSessionStats
{
   ULONG jp_allocs;
   ULONG jp_frees;
   LONG outstanding_bytes;
   ULONG total_alloc_bytes;
   ULONG total_free_bytes;
   LONG peak_outstanding_bytes;
};

struct Jcontext
{  void *pool;
   void *objpool;
   void *varpool;
   struct Token token;       /* Token for use by the parser */
   struct Token *nexttoken;
   void *program;
   LIST(Jobject) objects;
   LIST(Jobject) tmp;           /* Temporary object list during function execution */
   ULONG obj_created;           /* Count of Newobject() calls for this context */
   ULONG obj_disposed;          /* Count of Disposeobject() calls for this context */
   struct Jcontext *truecontext;      /* If this is a temporary context (eg in an eval) point to true context */
   void *try;                 /* current try block */
   struct Variable *throw;    /* Variable holding last throw or runtime error object */
   struct Value *throwval;    /* pointer to jc->throw->val */
   struct Element *elt;       /* currently executing element */
   LIST(This) thislist;       /* this stack */
   long gc;                   /* garbage collect if less than 0 */
   long nogc;                 /* don't auto gc if <0 */
   USHORT flags;
   USHORT dflags;             /* Debugger state */
   UBYTE *screenname;
   long generation;
   struct Jobject *fscope;    /* Scope to add functions and global variables to. */
   struct Jobject *jthis;
   ULONG protkey;             /* Runtime protection key */
   ULONG userdata;
   Jfeedback *feedback;
   long linenr;               /* Starting line # of source */
   UBYTE *sourcename;         /* Optional source name for stack traces (e.g. filename). */
   ULONG fbtime;              /* Time (s) when to give next feedback */
   ULONG warntime;            /* Time (s) when to give warning */
   ULONG warnmem;             /* Memory amount when to give warning */
   struct Variable *valvar;   /* Variable holding execution result */
   struct Value *val;         /* Pointer to valvar->val */
   struct Variable *varref;   /* Reference to assignable variable */
   struct Variable *result;   /* Last program execution result */
   USHORT complete;           /* Normal or abrubt completion, see below */
   LIST(Function) functions;  /* Function call stack. Last one is JS global scope,
                               * for standard JS defined variables. */
   LIST(Label) labels;        /* List / stack of of currently valid labels */
   struct Jobject *o;         /* Pointer last object constructed by Object */
   struct Label *curlabel;    /* Label associated current with break or continue */
   struct Jobject *tostring;  /* General default toString function */
   struct Jobject *eval;      /* General standard eval function */
   struct Jobject *object;    /* Standard Object constructor function */
   struct Jobject *boolean;   /* Standard Boolean constructor function */
   struct Jobject *function;  /* Standard Function constructor function */
   struct Jobject *number;    /* Standard Number constructor function */
   struct Jobject *string;    /* Standard String constructor function */
   struct Jobject *array;     /* Standard Array constructor function */
   struct Jobject *regexp;    /* Standard Regular Expression function */
   struct Jobject *error;      /* Standard Error constructor function */
   struct Jobject *nativeErrors[NUM_ERRORTYPES]; /*Native error constructors */
   BOOL jstrace;               /* when TRUE, Runtimeerror / Dumpjscallstack may emit stacktrace frames */
   BOOL errconsole;            /* when TRUE, Errorrequester prints to stderr (CLI hosts) */
   struct JAtomStr *jatom_buckets[JATOM_NUM_BUCKETS]; /* root context only; see Jatomroot() */
};


#define GC_THRESHOLD 256  /* 256 */        /* number of objects created before first garbage collection */

#define JCF_ERROR       0x0001   /* Compiler error occurred */
#define JCF_IGNORE      0x0002   /* Ignore all errors */
#define JCF_ERRORS      0x0004   /* Show compilation error requesters */

#define EXF_KEEPREF     0x0100   /* Variable reference is valid */
#define EXF_CONSTRUCT   0x0200   /* Function was called as constructor */
#define EXF_STOP        0x0400   /* Feedback function requested stop */
#define EXF_ASGONLY     0x0800   /* Rhs of . operator is only for assign use */
#define EXF_ERRORS      0x1000   /* Show runtime error requesters */
#define EXF_DONTSTOP    0x2000   /* Don't stop after error encountered */
#define EXF_WARNINGS    0x4000   /* Issue runtime loop warnings */

#define DEBF_DEBUG      0x0001   /* Use debugger at all */
#define DEBF_DBREAK     0x0002   /* Debug break at next element */
#define DEBF_DOPEN      0x0004   /* Debuger was opened, close when ready */

#define ECO_NORMAL   0
#define ECO_CONTINUE 1
#define ECO_BREAK    2
#define ECO_RETURN   3
#define ECO_THROW    4

/*-----------------------------------------------------------------------*/

struct Value
{  UBYTE type;       /* type, see below */
   UBYTE attr;       /* type-specific attributes, see below */
   union
   {  double nvalue; /* number */
      BOOL bvalue;   /* boolean */
      UBYTE *svalue; /* string */
      struct
      {  struct Jobject *ovalue; /* object or function. NOTE: can be NULL. */
         struct Jobject *fthis;  /* (this) object for this function */
      } obj;
   } value;
};

#define VTP_UNDEFINED   0
#define VTP_NUMBER      1
#define VTP_BOOLEAN     2
#define VTP_STRING      3
#define VTP_OBJECT      4  /* Can be function too */

/* Attributes for number */
#define VNA_VALID       0
#define VNA_NAN         1  /* nvalue=0.0 */
#define VNA_INFINITY    2  /* nvalue=1.0 */
#define VNA_NEGINFINITY 3  /* nvalue=-1.0 */

/* Hook function when variable is assigned to.
 * Returns TRUE if it understands the function, FALSE if default
 * action should be taken. */
typedef BOOL Varhookfunc(struct Varhookdata *data);

struct Varhookdata
{  struct Jcontext *jc;       /* Execution context */
   USHORT code;               /* Function code */
   struct Variable *var;      /* Variable affected (that has this hook defined) */
   void *hookdata;            /* Private data for hook */
   struct Variable *value;    /* Value to set variable to (VHC_SET) or to get into (VHC_GET) */
   UBYTE *name;               /* Name of variable to get or set */
};

#define VHC_SET         1  /* Set variable to this value */
#define VHC_GET         2  /* Get variable value */

struct Variable
{  NODE(Variable);
   UBYTE *name;
   struct Value val;
   Varhookfunc *hook;         /* Hook to call when variable is changed */
   void *hookdata;            /* Private data for use in hook */
   ULONG protkey;             /* Data protection key */
   USHORT flags;
};

#define VARF_HIDDEN     0x0001   /* Property is hidden (doesn't show up in for(in)) */
#define VARF_SYNONYM    0x0002   /* Use variable pointed to by hookdata instead. */
#define VARF_DONTDELETE 0x0004   /* Property cannot be deleted */
#define VARF_NAMEATOMSHARED 0x0008   /* name points into atom table; do not FREE */

/* Hook function when property is added to object.
 * Returns TRUE if it understands the function, FALSE if default
 * action should be taken. */
typedef BOOL Objhookfunc(struct Objhookdata *data);

struct Objhookdata
{  struct Jcontext *jc;       /* Execution context */
   USHORT code;               /* Function code */
   struct Jobject *jo;        /* Object affected */
   UBYTE *name;               /* Name of property to add */
};

#define OHC_ADDPROPERTY 1  /* Add a property */

/* hook to call when object is disposed */

typedef void Objdisposehookfunc(void *internal);

struct Jobject
{  NODE(Jobject);
   struct Jcontext *jc;
   long type;
   struct Jobject *constructor;
   struct Jobject *prototype; /* Pointer to the first in this objects prototype chain */
   LIST(Variable) properties;
   struct JPropIdxEnt *propidx[JPROP_IDX_BUCKETS]; /* generic prop fast path */
   struct Elementfunc *function;
   Objhookfunc *hook;         /* Hook to call when property is added */
   void *internal;
   Objdisposehookfunc *dispose;   /* Destructor function for (internal) */
   USHORT flags;
   USHORT dumpnr;             /* Id number for debugger dump */
   long keepnr;               /* If >0, mark as used from here */
   BOOL notdisposed;          /* this is set to 1 and reset to 0 on disposal */
   struct Variable *var;      /* The variable this object is assigned to */
};

#define OBJF_CLEARING   0x0001   /* Clearing this object */
#define OBJF_TEMP       0x0002   /* Temporary object */
#define OBJF_USED       0x0004   /* Don't sweep away */
#define OBJF_ASFUNCTION 0x0008   /* Object may be called as function to obtain its properties */

#define OBJT_USER     0x10000000    /* User object (usually html DOM) */
#define OBJT_GENERIC  0x00000000    /* Generic object */
#define OBJT_ARRAY    0x00000001    /* Internal Array object */
#define OBJT_BOOLEAN  0x00000002    /* Internal Boolean */
#define OBJT_STRING   0x00000003    /* String Object */
#define OBJT_REGEXP   0x00000004    /* Regular Expression */
#define OBJT_NUMBER   0x00000005    /* Internal Number Object */
#define OBJT_MATH     0x00000006    /* Internal Math Object */
#define OBJT_FUNCTION 0x00000007    /* Function Object */
#define OBJT_DATE     0x00000008    /* Internal Date Object */
#define OBJT_ERROR    0x00000009    /* Internal Error Object */

/*-----------------------------------------------------------------------*/

struct Function
{  NODE(Function);
   LIST(Variable) local;      /* Arguments and local variables */
   LIST(With) with;           /* with() stack */
   struct Jobject *arguments; /* Arguments array */
   struct Jobject *def;       /* Function definition */
   struct Jobject *fscope;    /* Global scope for this function */
   struct Value retval;       /* Value of last return statement */
};

struct With
{  NODE(With);
   struct Jobject *jo;
   USHORT flags;
};

#define WITHF_GLOBAL    0x0001   /* Global scope on top level, visible everywhere. */

struct Label
{
    NODE(Label);
    void *elt;              /* the element (probably statement) to which label refers */
    UBYTE *name;            /* string representing label */
};

struct This
{
    NODE(This);
    struct Jobject *this;
};

#define RETVAL(jc)      &((jc)->functions.first->retval)

/*-----------------------------------------------------------------------*/

struct Jbuffer
{  void *pool;
   UBYTE *buffer;
   long length;            /* Length of string in buffer */
   long size;              /* Size of buffer */
};

/*-----------------------------------------------------------------------*/

#define JTT_KEYWORD     0x0000
#define JTT_IDENTIFIER  0x1000
#define JTT_LITERAL     0x2000
#define JTT_SEPARATOR   0x3000
#define JTT_OPERATOR    0x4000

enum JTOKEN_IDS
{  JT_BREAK=JTT_KEYWORD+1,
   JT_CONTINUE,JT_DELETE,JT_DO,JT_ELSE,JT_FOR,JT_FUNCTION,
   JT_IF,JT_IN,JT_NEW,JT_RETURN,JT_THIS,
   JT_TYPEOF,JT_VAR,JT_VOID,JT_WHILE,JT_WITH,
   JT_INSTANCEOF,JT_THROW,JT_TRY,JT_CATCH,JT_FINALLY,
   JT_SWITCH,JT_CASE,JT_DEFAULT,

   JT_IDENTIFIER=JTT_IDENTIFIER+1,
   
   JT_INTEGERLIT=JTT_LITERAL+1,
   JT_FLOATLIT,JT_BOOLEANLIT,JT_STRINGLIT,JT_NULLLIT,JT_REGEXPLIT,
   
   JT_LEFTPAR=JTT_SEPARATOR+1,JT_RIGHTPAR,
   JT_LEFTBRACE,JT_RIGHTBRACE,
   JT_LEFTBRACKET,JT_RIGHTBRACKET,
   JT_SEMICOLON,JT_COMMA,JT_NEWLINE,
   
   JT_ASSIGN=JTT_OPERATOR+1,
   JT_GT,JT_LT,JT_NOT,JT_BITNEG,JT_CONDIF,JT_CONDELSE,JT_DOT,
   JT_EQ,JT_LE,JT_GE,JT_NE,JT_EXEQ,JT_NEXEQ,JT_AND,JT_OR,JT_INC,JT_DEC,
   JT_PLUS,JT_MINUS,JT_MULT,JT_DIV,JT_REM,JT_BITAND,JT_BITOR,JT_BITXOR,
   JT_SHLEFT,JT_SHRIGHT,JT_USHRIGHT,
   JT_APLUS,JT_AMINUS,JT_AMULT,JT_ADIV,JT_AREM,JT_ABITAND,JT_ABITOR,JT_ABITXOR,
   JT_ASHLEFT,JT_ASHRIGHT,JT_AUSHRIGHT,

#ifdef JSDEBUG
   JT_DEBUG=JTT_KEYWORD+63,
#endif
#ifdef JSADDRESS
   JT_ADDRESS=JTT_KEYWORD+64,
#endif
};

/*-----------------------------------------------------------------------*/

struct Element             /* A compiler element */
{  UWORD type;            /* Element type */
   long generation;
   short linenr;
   void *sub1,*sub2,*sub3,*sub4;
                           /* Child elements. */
};

/* There should really be a difference between integers and floats in this way */
/* Ultimately they both get converted to double, so quick fix is to store ivalue*/
/* as a double. Stops very large numbers going negative */

struct Elementint          /* An integer type literal */
{  UWORD type;
   long generation;
   short linenr;
   double ivalue;
};

struct Elementfloat        /* A float type literal */
{  UWORD type;
   long generation;
   short linenr;
   double fvalue;
};

struct Elementstring       /* A string literal, or ID name */
{  UWORD type;
   long generation;
   short linenr;
   UBYTE *svalue;
};

struct Elementregexp        /* A regular expression literal */
{
    UWORD type;
    long generation;
    short linenr;
    UBYTE *pattern;
    UBYTE *flags;
};

/* array literal uses Elementlist */

/* object literal uses Elementlist */

struct Elementlist         /* A list type compiler element */
{  UWORD type;            /* Element type */
   long generation;
   short linenr;
   LIST(Elementnode) subs; /* List of child element references */
};

struct Elementfunc         /* A function [object] definition */
{  UWORD type;            /* ET_FUNCTION */
   long generation;
   short linenr;
   UBYTE *name;            /* Function name or NULL */
   LIST(Elementnode) subs; /* List of arguments */
   void *body;             /* Function body */
   struct Jobject *fscope; /* Scope for which function is defined, i.e. scope to look
                            * for global variables used in function body. */
   struct JBytecodeChunk *bcode; /* optional fast path; NULL if not emitted or unsupported */
};

struct Elementfuncref      /* Return a function reference */
{
    UWORD type;            /* ET_FUNCREF */
    long generation;
    short linenr;
    struct Jobject *func;  /* the object holding the function */
};

struct Elementswitch       /* a switch statement */
{
    UWORD type;            /* ET_SWITCH */
    long generation;
    short linenr;
    void *cond;
    LIST(Elementnode) subs;
    void *defaultcase;
};

struct Elementcase
{
    UWORD type;            /* ET_CASE */  /* ET_DEFAULT */
    long generation;
    short linenr;
    void *expr;            /* case label or expression null for default */
    BOOL isdefault;        /* is the default case */
};

struct Elementtry
{
    UWORD type;
    long generation;
    short linenr;
    struct Jobject *throw;
    void *try;
    void *catch;
    void *catchvar;
    void *finally;
};

struct Elementnode         /* A listnode reference to another element */
{  NODE(Elementnode);
   void *sub;
};

/* Element types serve as ID of polymorphic elements, and as index in element class table */
enum ELEMENT_TYPES
{  
   /* List type elements */
   ET_PROGRAM=1,           /* List of functions and/or statements */
   ET_CALL,                /* List, 1st sub is function, rest is arguments */
   ET_COMPOUND,            /* List of elements */
   ET_VARLIST,             /* List of ET_VAR which are variable declarations */
   
   ET_TRY,
   /* Function type elements */
   ET_FUNCTION,
   ET_FUNCREF,

   /* Nullary elements */
   ET_BREAK,ET_CONTINUE,
   ET_THIS,
   ET_NULL,
   ET_EMPTY,
   
   /* Unary elements */
   ET_NEGATIVE,ET_POSITIVE,ET_NOT,ET_BITNEG,
   ET_PREINC,ET_PREDEC,ET_POSTINC,ET_POSTDEC,
   ET_NEW,ET_DELETE,ET_TYPEOF,ET_VOID,ET_RETURN,ET_THROW,
   ET_INTERNAL,            /* Internal function */
   ET_FUNCEVAL,            /* Evaluate Function object body */
   ET_CASE,
   
   /* Binary elements */
   ET_PLUS,ET_MINUS,ET_MULT,ET_DIV,ET_REM,ET_BITAND,ET_BITOR,ET_BITXOR,
   ET_SHLEFT,ET_SHRIGHT,ET_USHRIGHT,
   ET_EQ,ET_NE,ET_EXEQ,ET_NEXEQ,ET_LT,ET_GT,ET_LE,ET_GE,
   ET_AND,ET_OR,
   ET_ASSIGN,
   ET_APLUS,ET_AMINUS,ET_AMULT,ET_ADIV,ET_AREM,ET_ABITAND,ET_ABITOR,ET_ABITXOR,
   ET_ASHLEFT,ET_ASHRIGHT,ET_AUSHRIGHT,
   ET_COMMA,ET_IN,ET_INSTANCEOF,
   ET_WHILE,ET_DO,ET_WITH,ET_DOT,ET_INDEX,
   ET_VAR,
   
   /* Ternary elements */
   ET_COND,ET_IF,ET_FORIN,ET_SWITCH,
   
   /* Quarnery elements */
   ET_FOR,
   
   /* Literals and identifier */
   ET_INTEGER,ET_FLOAT,ET_BOOLEAN,ET_STRING,
   ET_IDENTIFIER,ET_REGEXP,ET_ARRAY,ET_OBJECTLIT,
   ET_LABEL,

#ifdef JSDEBUG
   ET_DEBUG,
#endif
#ifdef JSADDRESS
   ET_ADDRESS,
#endif
};

/*-----------------------------------------------------------------------*/

typedef void Internfunc(void *);

/* Runtime error type constants */
#define NTE_EVAL      "EvalError"
#define NTE_TYPE      "TypeError"
#define NTE_RANGE     "RangeError"
#define NTE_URI       "URIError"
#define NTE_SYNTAX    "SyntaxError"
#define NTE_REFERENCE "ReferenceError"
#define NTE_GENERAL   NULL

/*-----------------------------------------------------------------------*/

#endif
