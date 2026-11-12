/**********************************************************************
 * 
 * This file is part of the AWeb distribution
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

/* pluginlib.h
 *
 * AWeb plugin module project dependent header file
 *
 * It is included in startup.h to import project specific
 * constants.
 *
 * In this file, the following macros must be defined:
 *
 * PLUGIN_VERSION       Version of the module to be generated
 * PLUGIN_REVISION      Revision of the module to be generated
 * PLUGIN_LIBNAME       Name of the module, must match the name
 *                      used to open the library
 * PLUGIN_LIBID         Lib ID string of the module
 * PLUGIN_SIZE          Size in bytes of the actual library node
 *                      structure
 *
 * Optionally the following macros can be #define'd if the plugin
 * supports the feature, or #undef'ed if the plugin doesn't
 * support the feature:
 *
 * PLUGIN_COMMANDPLUGIN If the Commandplugin() function is supported
 * PLUGIN_FILTERPLUGIN  If the Filterplugin() function is supported
 *
 */

#include <exec/types.h>
#include <exec/libraries.h>

#define PLUGIN_VERSION     2
#define PLUGIN_REVISION    0
#define PLUGIN_LIBNAME     "filterexample.awebplugin"
#define PLUGIN_LIBID       "filterexample 2.0 " __AMIGADATE__
#define PLUGIN_SIZE        sizeof(struct FilterExampleBase)

struct FilterExampleBase
{  struct Library node;
};

#define PLUGIN_COMMANDPLUGIN
// or: #undef PLUGIN_COMMANDPLUGIN

#define PLUGIN_FILTERPLUGIN
// or: #undef PLUGIN_FILTERPLUGIN
