RESPeCt - Resident Evil Survivor PC port
========================================

RESPeCt is a fan-made PC version of Resident Evil Survivor (PlayStation,
USA) with mouse aiming, widescreen and high resolutions. It's free, and it
contains nothing from the game: it plays your own copy of the disc.

SETTING UP
----------
1. Unzip this folder anywhere you like.
2. Put your disc image of Resident Evil Survivor (USA, SLUS-01087) in the
   same folder as RESPeCt.exe. Any of these work:
     - the .bin and .cue files
     - a .chd file
     - the .zip they came in
3. Run RESPeCt.exe. It finds the disc image, checks it, and
   installs the game's data into a "data" folder next to it (about 430 MB,
   one time only). After that your disc image isn't needed: you can delete
   it, and the game starts straight away from then on.

The disc image must be an exact copy of the USA release. The European and
Japanese releases, and .iso files (which leave out the music and voices),
can't be used; the game tells you if that's what it found.

HD TEXTURES (OPTIONAL)
----------------------
RESPeCt can use HD texture packs made for the Beetle PSX HW emulator. We
recommend Leigiboy's "RE Survivor HQ Texture Pack", downloaded separately
from ModDB:
  https://www.moddb.com/mods/re-survivor-hq-texture-pack/downloads/re-survivor-hq-textures
All credit for the pack goes to Leigiboy. RESPeCt isn't affiliated with
Leigiboy or the pack in any way. If HD textures look wrong in RESPeCt,
please report it to us on GitHub (github.com/MachinaAeon/RESPeCt/issues):
it's most likely something RESPeCt needs to fix, so please don't contact
Leigiboy about it.

To use it, put the pack's .rar (or .zip/.7z) in the same folder as
RESPeCt.exe and start the game. The first time, RESPeCt unpacks it into a
"textures" folder (about a minute) and offers to move the archive to the
Recycle Bin. Windows 11 can unpack .rar and .7z by itself; on Windows 10,
unpack the archive into this folder with 7-Zip or WinRAR instead. An
already unpacked pack folder works too, anywhere in this folder, and
"texture_pack =" in resurv.ini can point to one elsewhere (such as
RetroArch's copy).

F2 turns HD textures on or off (also F1 > Video). They show at resolution
2x and higher. Entering a new room, a texture can show at its original
resolution for a moment while its HD image loads.

RUNNING SLOWLY?
---------------
RESPeCt draws the game with your CPU unless you turn on F1 > Video >
Graphics card: then the graphics card draws it (Direct3D 11), much faster,
and resolutions up to 8x become available. Drawn larger than your screen,
the picture is shrunk to fit and edges come out smooth (HD texture packs
aren't drawn on the graphics card yet). With the CPU the resolution
setting matters most. The first start picks a resolution to
suit your CPU, and if the game still can't keep up it lowers it by itself
(with a message). You can change it any time in F1 > Video > Resolution:
1x is the PlayStation's own, 2x is four times the work, 4x sixteen. HD
textures add a little more.

CONTROLS
--------
Mouse: aim and shoot (left button). In menus the mouse moves the cursor
(left = OK, right = cancel). Keys can be changed in the settings.
  F1          settings (video incl. brightness, sound, keys, cheats)
  F2          HD textures on/off (with a texture pack)
  F5 / F9     save / load a quick save; F6 / F7 pick the slot
  F11         fullscreen (or Alt+Enter)
  Esc         release the mouse
Controller (Xbox controllers; PlayStation and others through Steam Input):
  Left stick / d-pad  move and strafe (menus: move the cursor)
  Right stick         look and aim
  RT fire, LT quick search, A OK / run, B cancel, X reload, Y triangle,
  LB / RB previous / next weapon, Start pause, Back select, L3 sprint
  (stays on until you stop), R3 circle. Hold A to fast-forward cutscenes.
  L3 + R3 opens the settings: LB / RB pages, d-pad choose and change,
  A change, B close. Buttons can be changed in resurv.ini [controller].
The game's own saves (at the typewriters and payphones) go on a memory card
file next to the exe.

FILES IT MAKES
--------------
data\ (the installed game data), resurv.ini (settings), resurv.log (a log, helpful for bug reports),
memcard1.mcd (memory card), saves\ (quick saves).

LEGAL
-----
RESPeCt is an unofficial fan project. It is not affiliated with, endorsed
or sponsored by Capcom. Resident Evil and Resident Evil Survivor are trademarks
of Capcom Co., Ltd. No game code, graphics, sound or other data from the game
is included; you need your own legally obtained copy of the game.

RESPeCt is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 3 (see LICENSE.txt).
It comes with no warranty. Its source code is published alongside each
release.

Third-party components and their licenses: see THIRD_PARTY_LICENSES.txt.
