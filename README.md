#Genesis Plus GX port for the GCW-Zero Handheld Console
![](https://bytebucket.org/shin_nil/genesis-plus-gx-gcw0/raw/aff4ab1481d195141d2d83b31fa0786f5a8c5bdc/gx/images/Main_logo.png)
##Original Code
https://github.com/ekeeke/Genesis-Plus-GX
##GCW-Zero port by:
* Shin-NiL
* David Knight

**Source code available at:**
https://bitbucket.org/shin_nil/genesis-plus-gx-gcw0/

**Development Discussion Thread:**
http://boards.dingoonity.org/gcw-development/genesis-plus-gx-experimental-build/

##ABOUT
>Genesis Plus GX is an open-source emulator focused on accuracy and portability, now emulating SG-1000, Master System, Game Gear, Mega Drive / Genesis and Sega / Mega CD hardware. Initially ported and designed to be running on Gamecube / Wii consoles through libogc / devkitPPC, it is also available for many other platforms through various frontends.

>The source code is based on Genesis Plus 1.3, originally developped by Charles MacDonald (http://cgfm2.emuviews.com). It has been heavily modified, with respect to initial goals and design, in order to improve accuracy of emulation, implementing new features and adding support for extra peripherals, cartridge & systems hardware.

>The result is that Genesis Plus GX is now more a continuation of the original project than a simple port, providing very accurate emulation and 100% compatibility with Genesis / Mega Drive, Sega / Mega CD, Master System, Game Gear & SG-1000 software (including all unlicensed or pirate known dumps), also emulating backwards compatibility modes when available.

>Multi-platform sourcecode is available through SVN and GIT so that other Genesis Plus ports can take advantage of it. The sourcecode is released under a specific non-commercial license, see LICENSE.txt for more informations.

##INSTRUCTIONS

For SG-1000, Game Gear (GG), Master System (SMS) and Mega Drive (Genesis, MD) you don't need to do anything. Just locate the rom file you wish to use (zip files are supported) and run it through the gMenu.

For Sega CD games you will need the correct bios files. These are not included with this distribution for copyright reasons. Once you have located the correct bios you will need to rename them according to the correct region:
* "_/usr/local/home/.genplus/bios/bios_CD_U.bin_"
* "_/usr/local/home/.genplus/bios/bios_CD_E.bin_"
* "_/usr/local/home/.genplus/bios/bios_CD_J.bin_"

If everything is setup correctly you will then need to locate your image files and run them. _.bin/.cue_ and _.iso_ files are supported, _.ogg_ compressed tracks are supported, _.mp3_ is NOT supported for legal reasons. (ogg is better anyway :P)

To access the menu press SELECT+START. Controls can be reconfigured from there. In the menu A selects option, B will go back to the last menu.

####Default controls are:
* D-Pad/A-Stick: Directions
* X: A
* B: B
* A: C
* L: X
* Y: Y
* R: Z
* START: Start
* SELECT: Mode
* SELECT + START: Menu
* SELECT + L: Quicksave to Savestate 1
* SELECT + R: Quickload savestate 1

##CONFIGURATION OPTIONS (Default setting)

###Scaling (On)
The gcw0 has hardware bilinear scaling which when turned on will enlarge the image to fill the screen. By default this keeps aspect ration so you will still see small bars unless the scaled resolution is exactly 320x240. This is particularly useful for SMS and GG games.

###Keep aspect ratio (On)
This does nothing unless Scaling is turned on. If aspect ratio is turned off the hardware scaler will fill the whole screen and there will be no black bars visible.

###Scanlines (GG) (On)
This emulates the vertical scanlines visible on the LCD screen. It is only used when running Game Gear games.

###Mask left bar (SMS) (On)
The Master System often displays a vertical bar on the left of the screen. Setting this to 'On' will remove it.

###Frameskip (0)
In all games except Mega CD this is of no use as the emulation is near perfect speed (with the exception of Virtual Racing). Setting this value will render fewer frames allowing a little speed increase. This improves the performance of FMV scenes in Mega CD games.

###Resume on Save/Load (On)
When saving from the menu, the menu will automatically exit and the game will continue. If turned off it will return you to the menu.

###A-stick (On)
Some users have issues with their analog controls causing erratic movements. Turning off the A-stick will stop this.

###Lock-on (Off)
If enabled, when loading a rom the selected file will be loaded first.
Rename each binary file as follows:
* Game Genie: "_/usr/local/home/.genplus/lock-on/ggenie.bin_"
* Action Replay:  "_/usr/local/home/.genplus/lock-on/areplay.bin_"
* Sonic & Knuckles:  "_/usr/local/home/.genplus/lock-on/sk.bin_" and "_/usr/local/home/.genplus/lock-on/sk2chip.bin_" [additional info](https://code.google.com/p/genplus-gx/issues/detail?id=336)

_*After changing the lock-on option, you must reload the game rom._

##LATEST BUILDS
Latest GCW-Zero builds are available for download from https://bitbucket.org/shin_nil/genesis-plus-gx-gcw0/downloads
