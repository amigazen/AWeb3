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

/* popup.h - AWeb popup menu interface */

#ifndef AWEB_POPUP_H
#define AWEB_POPUP_H

#include "object.h"

/*--- popup tags ---*/

#define AOPUP_Dummy        AOBJ_DUMMYTAG(AOTP_POPUP)

#define AOPUP_Target       (AOPUP_Dummy+1)    /* NEW */
   /* (void *) Object will be sent
    * - AOM_SET with AOPUP_Inquire
    * - AOM_UPDATE with AOPUP_Command when the item was selected.
    * More than one target can be given, the popup menu holds all choices
    * with separator between targets. */

#define AOPUP_Left         (AOPUP_Dummy+2)    /* NEW */
#define AOPUP_Top          (AOPUP_Dummy+3)    /* NEW */
   /* (long) Rastport coordinates of popup position */

#define AOPUP_Window       (AOPUP_Dummy+4)    /* NEW */
   /* (void *) WINDOW object that popup belongs to. */

#define AOPUP_Inquire      (AOPUP_Dummy+5)    /* */
   /* (void *) The POPUP object that wants information.
    * Send AOM_SET messages to it, setting AOPUP_Title and AOPUP_Command. */

#define AOPUP_Title        (AOPUP_Dummy+6)    /* SET */
   /* (UBYTE *) Title for the popup menu item */

#define AOPUP_Command      (AOPUP_Dummy+7)    /* SET,UPDATE*/
   /* (UBYTE *) Command for the popup menu item.
    * Also send in AOM_UPDATE back to the target if item was selected.
    * If popup menu closes without command selection, this attribute is sent
    * back with NULL data. */

#define AOPUP_    (AOPUP_Dummy+)    /* */
#define AOPUP_    (AOPUP_Dummy+)    /* */

void Closeopenpopups(void);

#endif
