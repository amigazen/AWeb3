/**********************************************************************
 *
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2026 amigazen project
 *
 * ES3-oriented bytecode scaffold for a future stack VM (second codegen path).
 * The AST interpreter remains authoritative until wired; this documents the
 * intended opcode set and chunk layout for C89 implementations.
 *
 **********************************************************************/

#ifndef JBYTECODE_H
#define JBYTECODE_H

#include <exec/types.h>

struct Jcontext;

#define JBCC_EMPTY   0
#define JBCC_NUMBER  1
#define JBCC_STRING  2

struct JBytecodeChunk
{
   UBYTE *code;
   ULONG length;
   ULONG maxstack;
   ULONG numconsts;
   void **consts;
   UBYTE *ctypes;
};

enum JBC_OPCODE
{
   JBC_OP_NOP = 0,
   JBC_OP_UNDEFINED,
   JBC_OP_NULL,
   JBC_OP_TRUE,
   JBC_OP_FALSE,
   JBC_OP_NUMBER,
   JBC_OP_STRING,
   JBC_OP_GETGLOBAL,
   JBC_OP_SETGLOBAL,
   JBC_OP_GETLOCAL,
   JBC_OP_SETLOCAL,
   JBC_OP_GETPROP,
   JBC_OP_SETPROP,
   JBC_OP_DELPROP,
   JBC_OP_CALL,
   JBC_OP_CONSTRUCT,
   JBC_OP_ADD,
   JBC_OP_SUB,
   JBC_OP_MUL,
   JBC_OP_DIV,
   JBC_OP_MOD,
   JBC_OP_EQ,
   JBC_OP_NE,
   JBC_OP_LT,
   JBC_OP_LE,
   JBC_OP_GT,
   JBC_OP_GE,
   JBC_OP_JUMP,
   JBC_OP_JUMP_IF_FALSE,
   JBC_OP_RETURN,
   JBC_OP_THROW
};

#endif
