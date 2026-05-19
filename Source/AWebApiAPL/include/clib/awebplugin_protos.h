#ifndef CLIB_AWEBPLUGIN_H
#define CLIB_AWEBPLUGIN_H

ULONG AmethodA(struct Aobject *object,struct Amessage *amessage);
ULONG Amethod(struct Aobject *object,...);
ULONG AmethodasA(ULONG objtype,struct Aobject *object,
   struct Amessage *amessage);
ULONG Amethodas(ULONG objtype,struct Aobject *object,...);

APTR AnewobjectA(ULONG objtype,struct TagItem *taglist);
APTR Anewobject(ULONG objtype,...);
void Adisposeobject(struct Aobject *object);
ULONG AsetattrsA(struct Aobject *object,struct TagItem *taglist);
ULONG Asetattrs(struct Aobject *object,...);
ULONG AgetattrsA(struct Aobject *object,struct TagItem *taglist);
ULONG Agetattrs(struct Aobject *object,...);
ULONG Agetattr(struct Aobject *object,ULONG attrid);
ULONG AupdateattrsA(struct Aobject *object,struct TagItem *maplist,
   struct TagItem *taglist);
ULONG Aupdateattrs(struct Aobject *object,struct TagItem *maplist,...);
ULONG Arender(struct Aobject *object,struct Coords *coords,LONG minx,
   LONG miny,LONG maxx,LONG maxy,USHORT flags,APTR text);
ULONG Aaddchild(struct Aobject *object,struct Aobject *child,ULONG relation);
ULONG Aremchild(struct Aobject *object,struct Aobject *child,ULONG relation);
ULONG Anotify(struct Aobject *object,struct Amessage *amessage);

APTR Allocobject(ULONG objtype,LONG size,struct Amset *amset);
ULONG AnotifysetA(struct Aobject *object,struct TagItem *taglist);
ULONG Anotifyset(struct Aobject *object,...);

APTR Allocmem(ULONG size,ULONG flags);
STRPTR Dupstr(STRPTR string,LONG length);
void Freemem(APTR mem);

struct Coords *Clipcoords(struct Aobject *cframe,struct Coords *coords);
void Unclipcoords(struct Coords *coords);
void Erasebg(struct Aobject *cframe,struct Coords *coords,LONG minx,LONG miny,
   LONG maxx,LONG maxy);

struct Aobject *Aweb(void);

void AsetattrsasyncA(struct Aobject *task,struct TagItem *taglist);
void Asetattrsasync(struct Aobject *task,...);
ULONG Waittask(ULONG signals);
struct Taskmsg *Gettaskmsg(void);
void Replytaskmsg(struct Taskmsg *taskmsg);
ULONG Checktaskbreak(void);
ULONG Updatetask(struct Amessage *amessage);
ULONG UpdatetaskattrsA(struct TagItem *taglist);
ULONG Updatetaskattrs(ULONG attrid,...);
ULONG Obtaintasksemaphore(struct SignalSemaphore *semaphore);

LONG Avprintf(char *format,ULONG *args);
LONG Aprintf(char *format,...);

struct RastPort *Obtainbgrp(struct Aobject *cframe,struct Coords *coords,
   LONG minx,LONG miny,LONG maxx,LONG maxy);
void Releasebgrp(struct RastPort *bgrp);

void Setfiltertype(void *handle,char *type);
void Writefilter(void *handle,char *data,long length);

long Registercidpart(char *referer_url,char *content_id,char *content_type,char *data,long datalen);
void Unregistercidparts(char *referer_url);

long Awebcommand(long portnr,char *command,char *resultbuf,long length);

#endif
