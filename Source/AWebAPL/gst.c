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

/* gst.c - aweb gst */

#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/soundclass.h>
#include <devices/audio.h>
#include <devices/clipboard.h>
#include <devices/printer.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/stdio.h>
#include <dos/rdargs.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/semaphores.h>
#include <graphics/gfxmacros.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/displayinfo.h>
#include <graphics/scale.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <intuition/imageclass.h>
#include <intuition/pointerclass.h>
#include <intuition/sghooks.h>
#include <intuition/intuition.h>
#include <gadgets/colorwheel.h>
#include <gadgets/gradientslider.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <libraries/iffparse.h>
#include <libraries/locale.h>
#include <rexx/rxslib.h>
#include <rexx/storage.h>
#include <utility/date.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <reaction/reaction.h>
#include <reaction/reaction_author.h>
#include <reaction/reaction_class.h>
#include <reaction/reaction_macros.h>
#include <reaction/reaction_prefs.h>
#include <classes/window.h>
#include <classes/requester.h>
#include <images/bevel.h>
#include <images/bitmap.h>
#include <images/drawlist.h>
#include <images/glyph.h>
#include <images/label.h>
#include <images/led.h>
#include <images/penmap.h>
#include <gadgets/button.h>
#include <gadgets/checkbox.h>
#include <gadgets/chooser.h>
#include <gadgets/clicktab.h>
#include <gadgets/colorwheel.h>
#include <gadgets/fuelgauge.h>
#include <gadgets/getfile.h>
#include <gadgets/getfont.h>
#include <gadgets/getscreenmode.h>
#include <gadgets/integer.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/listbrowser.h>
#include <gadgets/listview.h>
#include <gadgets/page.h>
#include <gadgets/radiobutton.h>
#include <gadgets/scroller.h>
#include <gadgets/slider.h>
#include <gadgets/space.h>
#include <gadgets/string.h>
#include <gadgets/tabs.h>
#include <gadgets/virtual.h>
#include <intuition/intuition.h>
#include <intuition/imageclass.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>

#include "ezlists.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/iffparse.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/locale.h>
#include <proto/icon.h>
#include <proto/bevel.h>
#include <proto/label.h>
#include <proto/fuelgauge.h>
#include <proto/window.h>
#include <proto/string.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/space.h>
#include <proto/slider.h>
#include <proto/scroller.h>
#include <proto/chooser.h>
#include <proto/button.h>
#include <proto/checkbox.h>
#include <proto/clicktab.h>
#include <proto/colorwheel.h>
#include <proto/datatypes.h>
#include <proto/diskfont.h>
#include <proto/keymap.h>
#include <proto/layers.h>
#include <proto/radiobutton.h>
#include <proto/virtual.h>
#include <proto/drawlist.h>
#include <proto/penmap.h>
#include <proto/bitmap.h>
#include <proto/getfile.h>
#include <proto/getfont.h>
#include <proto/getscreenmode.h>
#include <proto/integer.h>
#include <proto/requester.h>

