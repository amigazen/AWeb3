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

/* xhrjs.h - AWeb XMLHttpRequest JavaScript interface */

#ifndef AWEB_XHRJS_H
#define AWEB_XHRJS_H

#include "jslib.h"

/* Initialize XHR JavaScript support */
extern void Initxhrjs(void);

/* Cleanup XHR JavaScript support */
extern void Exitxhrjs(void);

/* Add the JS XMLHttpRequest() constructor to this object */
extern void Addxhrconstructor(struct Jcontext *jc,struct Jobject *parent);

/* XHR source update handler - called from url.c */
extern long Xhrsrcupdate(void *urlobj,struct Amsrcupdate *ams);

#endif

