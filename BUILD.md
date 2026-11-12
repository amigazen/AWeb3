# Build Instructions

Follow these instructions to build AWeb 3.6 or later versions. AWeb 3.4 does not build "out of the box" in the form originally released as open source. AWeb 3.6 restores the ability to build AWeb easily using just SAS/C and standard SDKs.

Building AWeb requires the following prerequisites:
- ToolKit - this can be obtained from https://www.github.com/amigazen/ToolKit/ or you can follow the instructions here to configure it yourself
- SAS/C 6.58 - this can be obtained from archive.org
- NDK 3.2 Release 4 - this can be obtained from the Hyperion website or from Aminet
- AmiSSL 5.20 SDK or later - this can be obtained from Aminet
- Cybergraphics SDK (default RTG backend) - typically available via ToolKit / Aminet
- P96 SDK - optional alternate RTG backend (included in the ToolKit SDK); enable with `AWEBGFXFLAGS=DEFINE=AWEB_USE_P96`
- reaction.lib - a compatible reimplementation of this is included in the ToolKit SDK

## ToolKit

Building AWeb 3 is most easily achieved if your development environment is setup following the ToolKit standard. See TOOLKIT.md or https://github.com/amigazen/ToolKit

Since ToolKit itself is a work in progress, here is a guide to setting up your development tools in the correct to build AWeb and any project configured to build with the ToolKit SDK, including all projects from amigazen project:

### ToolKit directories & assigns

ToolKit
- SDK
- Include_h
- Include_i
- emodules
- lib
- C
- Tools
- Libs

- **SDK:** - assigned to the top level directory containing your ToolKit SDK
- **include:** - assigned to SDK:Include_h/, SDK:Include_i/, NDK:Include_h/, NDK:Include_i/
- **netinclude:** - assigned to NDK:SANA+RoadshowTCP-IP/netinclude/
- **sslinclude:** - assigned to wherever the AmiSSL include folder has been unpacked - SDK:AmiSSL/Developer/include/ is suggested if the AmiSSL archive is unpacked to SDK:AmiSSL
- **lib:** - assigned to SDK:lib/, NDK:lib/ in that order 

Apart from this, a minimum ToolKit setup requires at least the following items:
- A supported C compiler - one of SAS/C, GCC or VBCC depending on the project. In this case AWeb currently builds only with SAS/C
- An Amiga Native Developer Kit - currently 3.2 Release 4

### SDK-Startup

If you use a User-Startup or SDK-Startup script to setup your development environment, you can add the following lines:

```
;BEGIN ToolKit 
Assign SDK: 
... TODO ...
;END ToolKit
```

TODO

### C Compiler

Building AWeb requires the SAS/C C compiler, fully patched up to version 6.58. Although a commercial product long since abandoned by its publisher, copies can be found on archive.org among other places. It remains probably the best all round C compiler and development system for the classic Amiga platform in terms of the quality of its features and generated code. The experimental PowerPC fat binary version of SAS/C has not been tested at the time of writing, for creating WarpOS compatible binaries.

ToolKit expects SAS/C to be installed in the default configuration:
- sc: assign set to the SAS/C install directory (SDK:sc/ is recommended but not mandatory)
- sc:c will therefore contain the SAS/C sc compiler and other command line tools, and this should also be added to the Path
- sc:lib will contain the sc.lib variants and other startup, math library and debugging code. Crucially, do NOT put any other .lib files in this folder such as NDK .lib files
- sc:include will contain ONLY the SAS/C Standard C Library headers. Again, do NOT put your NDK headers or other header files here, they have their own directories

That's it, nothing else is required to setup SAS/C for use with ToolKit

### Native Developer Kit

The NDK or 'Native Developer Kit' is the standard Amiga SDK for writing 'native' i.e. C API software for the Amiga.

The latest NDK as of the time of writing is version 3.2 release 4 or 'NDK3.2R4.lha' and available from Hyperion. 

Unpack the archive (again, SDK:NDK/ is recommended but not mandatory)

Ensure the following directives are in your User-Startup or SDK-Startup script:
- Assign lib: <NDK path>/lib ADD
- Assign include: <NDK path>/Include_h ADD
- Assign include: <NDK path>/Include_i ADD
- Assign netinclude: <NDK path>/SANA+RoadshowTCP-IP/netinclude
- Path <NDK path>/C ADD

Copy Catcomp from the Tools/Catcomp directory in the NDK, to somewhere in your path, such as the C folder from the NDK. This will be needed to build the locale module.

### Third Party SDKs

AWeb requires the following Third Party SDKs:
- AmiSSL - unpack this to wherever you prefer, and set sslinclude: to point to the AmiSSL/Developer/include directory
- P96 - TODO (but just make sure the header files are in your include path)
- ttengine.library is used, but the header files are included in the AWeb project for convenience

## Prerequisites

AWeb 3 requires:

- SAS/C compiler 6.58
- NDK3.2 Release 4
- catcomp from the NDK3.2 somewhere in your Path
- AmiSSL 5.20 SDK or later
- A version of reaction.lib the auto open static library for ReAction - one is available in the ToolKit SDK, or versions from older NDKs will work. This is required because NDK3.2R4 is missing the colorwheel and gradientslider pragmas
- P96 SDK integrated into your ToolKit SDK 
- include: assign pointed at NDK headers
- netinclude: assign pointed at Roadshow headers (included in NDK3.2)
- sslinclude: assign pointed at AmiSSL headers

## Compiler 

The current AWeb 3.6 requires SAS/C

| SAS/C | VBCC | GCC |
|-------|------|-----|
| [x]   | [ ]  | [ ] |

Additional compiler options may be added in the future.

## Project Inventory

TODO

## Built Target Inventory

- aweb - builds the AWeb core binary
- awebcfg - builds the AWebCfg prefs tool
- awebjs - builds the standalone JavaScript interpreter test tool
- aweblib/about.aweblib - builds the new about: protocol handler
- aweblib/arexx.aweblib - builds the arexx plugin
- aweblib/authorize.aweblib - builds the password manager plugin
- aweblib/awebjs.aweblib - builds the JavaScript engine plugin
- aweblib/cachebrowser.aweblib - builds the disk cache browser plugin
- aweblib/ftp.aweblib - builds the ftp: protocol handler for file transfers
- aweblib/gopher.aweblib - builds the gopher: protocol handler for gopher resources
- aweblib/history.aweblib - builds the navigation history browser plugin
- aweblib/hotlist.aweblib - builds the bookmarks hotlist manager plugin
- aweblib/mail.aweblib - builds the mailto: handler plugin for email
- aweblib/news.aweblib - builds the NNTP client plugin for Usenet news
- aweblib/print.aweblib - builds the printing plugin
- aweblib/startup.aweblib - builds the startup splash screen and loading progress plugin
- clean - deletes build artifacts from the project Source directory
- install - copies the latest built binaries to their proper locations in the project's Internet/AWeb directory from where they can be tested with the supporting assets and packaged for release


## How To Build AWeb

```
cd Source/AWebAPL
smake
```

### RTG graphics backend (CyberGfx default / Picasso96 optional)

AWeb uses CyberGfx call shapes via `AWebApiAPL/include/awebgfx.h`. The
**default** build opens `cybergraphics.library`. To build against
`Picasso96API.library` instead (same sources):

```
smake AWEBGFXFLAGS=DEFINE=AWEB_USE_P96
```

Do the same in each plugin directory (GIF/PNG/JFIF/WebP/SVG/PDF). Delete
`#?.o` when switching backends so objects are not mixed.

Or to build individual targets one by one

```
smake aweb
smake awebcfg
smake awebjs
smake aweblib/about.aweblib
smake aweblib/arexx.aweblib
smake aweblib/authorize.aweblib
smake aweblib/awebjs.aweblib
smake aweblib/gopher.aweblib
smake aweblib/ftp.aweblib
smake aweblib/hotlist.aweblib
smake aweblib/mail.aweblib
smake aweblib/cachebrowser.aweblib
smake aweblib/news.aweblib
smake aweblib/history.aweblib
smake aweblib/print.aweblib
smake aweblib/startup.aweblib
```

This creates binaries called AWeb, AWebCfg and AWebJS in the AWebAPL folder, as well as all the aweblib plugins

The smakefile is not quite configured correctly to ensure that only changed files get rebuilt. This can be exacerbated if using an emulator where host filesystem timestamps change on access rather than write. So it is sometimes more convenient to just make the objects you know have changed, and then relink the aweb binary, like this:

```
smake parse.o
slink with aweb.lnk
```

## How To Build AWeb Plugins

To build the individual AWeb Plugins for enhanced GIF, JPEG and PNG support:

```
cd Source/AWebGifAPL
smake
cd /AWebJfifAPL
smake
cd /AWebPngAPL
smake
```

## How To Build AWebGet

TODO as AWebGet does not currently build

## How to Clean

To delete all build artifacts in the project directory, you can run this command:

```
smake clean
```

However, if you need to make a fresh build, it is recommended simply to run:

```
smake -u all
```

to force a refresh of all targets.

## How To Release

To prepare a release build, run:

```
smake install
```

This will copy the newly created binaries into the correct locations in this project's Internet/AWeb/ directory (deliberately, not your system's Internet directory). Note therefore that it is not 'installing' the build to the local system, the install target is simply preparing the 

To make a release archive called for example 'AWebXY.lha' then run:

```
cd Internet
lha -xer r /archive/AWebXY.lha AWeb AWeb.info 
```

This will create an archive containing the whole AWeb directory and it's icon called 'AWebXY.lha' suitable for distribution - it can simply be unpacked and AWeb is ready to run. No installer script required.

## How to Run

The AWeb binary will run from whichever directory it's copied into.

AWeb will automatically set it's assign path "AWeb:" to the current directory if it does not already exist (and clean it up on exit, if it created it itself)

Do note however that it will not find its AWebCfg prefs tools, its Docs/ or its aweblib/ and awebplugin/ directories if they are not in the same directory (it will search for them in "PROGDIR:" also), or in the directory to which "AWeb:" is assigned if that is different.

AWeb command line and ToolType options:
- URL/M - Open the URL(s) and/or file(s) provided each in an AWeb window. Up to 16 resources can be passed. This is also the default option. New in AWeb 3.6, local files can be opened without the need to specify the file:// protocol
- HTTPDEBUG/S - this option outputs extensive network debugging output. It is suggested to pipe this output to a file on a corruption-proof filesystem in case of data loss in the event of a bug in AWeb taking down the whole system.
- HAIKU=VVIIV/S - this option replaces all error messages with haikus. Yes, really.
- LOCAL/S - Legacy option that treats all URLs passed as local files.
- CONFIG/K - Load a different prefs config than the default. If this parameter is provided, the prefs will load and save from ENVARC:AWeb3/<configname>/ instead of simply ENVARC:AWeb3/
- HOTLIST/K - Load the bookmarks hotlist from the given file instead
- NOSTARTUP/S - Do not load the startup.aweblib plugin even if it is installed, and therefore do not show the splash screen (New in Alpha 4)

The following additional command line options are not built by default in AWeb 3.6 but may be enabled by the developer in custom builds. Inspect the source code for more information:
- NOPOOL/S - disables use of memory pools
- SPECDEBUG/S - activates debugging mode
- BLOCK/K/N - sets block size default
- PROFILE/S - enables profiling
- OODEBUG/K - specify and out of memory debugging mode
- OOMETHOD/K - specify and out of memory debugging method
- OODELAY/S - delay simulating out of memory
- USETEMP/S - use temporary files
- OOKDEBUG/S - out of memory kill debug mode