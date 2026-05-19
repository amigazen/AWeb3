/**********************************************************************
 * 
 * This file is part of the AWeb distribution
 *
 * Copyright (C) 2025 amigazen project
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

/* cidregistry.h - Content-ID part registry for cid: URL support */

#ifndef AWEB_CIDREGISTRY_H
#define AWEB_CIDREGISTRY_H

#include "object.h"

/* Initialize CID registry */
extern BOOL Initcidregistry(void);

/* Register a part for cid: or data: URL lookup
 * For cid: URLs: referer_url is required, part_id is the Content-ID
 * For data: URLs: referer_url can be NULL, part_id is the full data: URL string */
extern BOOL Registercidpart(UBYTE *referer_url, UBYTE *part_id,
                            UBYTE *content_type, UBYTE *data, long datalen);

/* Find a part by referer and part ID (Content-ID for cid:, or data: URL string for data:)
 * For cid: URLs: referer_url is required, part_id is the Content-ID
 * For data: URLs: referer_url can be NULL, part_id is the full data: URL string */
extern BOOL Findcidpart(UBYTE *referer_url, UBYTE *part_id,
                        UBYTE **content_type, UBYTE **data, long *datalen);

/* Unregister all parts for a referer URL (cleanup)
 * For cid: URLs: unregisters all parts for this referer
 * For data: URLs: pass NULL as referer_url and use part_id as the data: URL string */
extern void Unregistercidparts(UBYTE *referer_url);

/* Cleanup CID registry */
extern void Cleanupcidregistry(void);

#endif /* AWEB_CIDREGISTRY_H */

