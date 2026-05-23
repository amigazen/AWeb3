/**********************************************************************
 * 
 * This file is part of the AWeb-II distribution
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

// easy lists

#ifndef EZLISTS_H
#define EZLISTS_H

#define LIST(name) \
struct\
{ struct name *first;\
  struct name *tail;\
  struct name *last;\
}

#define NODE(name) \
struct name *next;\
struct name *prev

#define FULLNODE(name) \
struct name *next;\
struct name *prev;\
unsigned char ln_Type;\
char ln_Pri;\
char *ln_Name

#define ADDHEAD(l,n) AddHead((struct List *)(l),(struct Node *)(n))
#define ADDTAIL(l,n) AddTail((struct List *)(l),(struct Node *)(n))

/* only use full nodes for this macro!! */
#define ENQUEUE(l,n) Enqueue((struct List *)(l),(struct Node *)(n))

#define REMOVE(n) (void *)Remove((struct Node *)(n))
#define REMHEAD(l) (void *)RemHead((struct List *)(l))
#define REMTAIL(l) (void *)RemTail((struct List *)(l))
#define NEWLIST(l) NewList((struct List *)(l))
#define INSERT(l,n,a) Insert((struct List *)(l),(struct Node *)(n),(struct Node *)(a))
#define ISEMPTY(l) (((struct List *)(l))->lh_Head->ln_Succ==NULL)

#endif
