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

/* hotlist.h - AWeb hotlist private */

struct Hotbase             /* Base entry */
{  NODE(Hotbase);
   short nodetype;
   UBYTE *title;           /* Title of entry */
   UBYTE *url;             /* Url of entry */
   ULONG date;             /* Date of entry */
   short used;             /* Number of groups this entry is used in */
   struct Node *lnode;     /* Edit window list node */
};

struct Hotitem             /* Hotlist entry */
{  NODE(Hotitem);
   short nodetype;
   struct Hotitem *owner;  /* Owner group or NULL for root group */
   short type;             /* Entry or group, see below */
   struct Hotbase *base;   /* Base data of entry */
   UBYTE *name;            /* Group name */
   LIST(Hotitem) subitems; /* Sublist when group */
   struct Node *lnode;     /* Edit window list node (root nodes only) */
   struct Node *gnode;     /* Edit window list node */
   ULONG showchildren;     /* LBFLG_SHOWCHILDREN or zero */
};

#define HNT_BASE        1
#define HNT_ITEM        2

#define HITEM_ENTRY     0  /* Hotlist entry */
#define HITEM_GROUP     1  /* Visible group */
#define HITEM_HGROUP    2  /* Hidden group */

struct Hotwindow
{  struct Aobject object;
   void *task;
   struct SignalSemaphore *hotsema;
   LIST(Hotbase) *hotbase;
   LIST(Hotitem) *hotlist;
   struct Library *libbase;
   short libentry;
   ULONG windowkey;
   UBYTE *screenname;
   short x,y,w,h;
   BOOL autoclose;
   USHORT flags;
};

#define HOTF_BREAKING      0x0001   /* Breaking the subtask */
#define HOTF_WASOPEN       0x0002   /* Window was open whenscreen became invalid */

#define AOHOT_Dummy        AOBJ_DUMMYTAG(AOTP_HOTLIST)

/* From subtask to main task: */
#define AOHOT_Close        (AOHOT_Dummy+1)
   /* (BOOL) Window was closed. */

#define AOHOT_Dimx         (AOHOT_Dummy+2)
#define AOHOT_Dimy         (AOHOT_Dummy+3)
#define AOHOT_Dimw         (AOHOT_Dummy+4)
#define AOHOT_Dimh         (AOHOT_Dummy+5)
   /* (long) Changed window dimensions */

#define AOHOT_Follow       (AOHOT_Dummy+6)
   /* (UBYTE *) Url name to open */

#define AOHOT_Libentry     (AOHOT_Dummy+7)
   /* (short) Lib entry nr (0 for viewer, 1 for manager */

#define AOHOT_Manager      (AOHOT_Dummy+8)
   /* (BOOL) Start the manager */

/* From main task to subtask: */
#define AOHOT_Tofront      (AOHOT_Dummy+101)
   /* (BOOL) Push window to front */

#define AOHOT_Addentry     (AOHOT_Dummy+102)
   /* (BOOL) A single entry was added */

#define AOHOT_Dispose      (AOHOT_Dummy+103)
   /* (BOOL) Dispose the gadget lists and disable */

#define AOHOT_Windowkey    (AOHOT_Dummy+104)

#define AOHOT_Changed      (AOHOT_Dummy+105)
   /* (BOOL) Hotlist was changed by manager */

#define AOHOT_Newlist      (AOHOT_Dummy+106)
   /* (BOOL) Rebuild gadget lists and enable */

#define AOHOT_Save         (AOHOT_Dummy+107)
   /* (BOOL) Request to save the current list */

#define AOHOT_Restore      (AOHOT_Dummy+108)
   /* (BOOL) Request to restore the list */

#define AOHOT_Vchanged     (AOHOT_Dummy+109)
   /* (BOOL) Hotlist group hide/show was changed by viewer */

#define AOHOT_    (AOHOT_Dummy+)
#define AOHOT_    (AOHOT_Dummy+)

#ifdef AWEB4
/* Runtime Hotlist menu entries (built from aweb.hotlist, not from prefs). */
extern void Freehotlistmenus(void);
extern long Hotlistmenuentrycount(void);
extern void Hotlistmenuaddentries(struct NewMenu *newmenus,long *index,
   short mnum,short *inum,short *snum);
extern struct Menuentry *Hotlistmenuentryfromnum(USHORT menunum);
extern BOOL Ishotlistmenuentry(struct Menuentry *me);
#endif
