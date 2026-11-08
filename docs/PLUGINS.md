# Plugins: changing how the game behaves

A plugin does in RESPeCt what a DLL mod does with the PC version: native
code that hooks the game's functions and reads and writes its memory, to
change anything, from how fast a zombie walks to how a weapon works. A DLL
written for the PC's `gun_bio.exe` can't run here (it patches that
program's own x86 code), but writing the same mod is easier: the game's
code is decompiled, with readable names, and every function can be hooked
by name.

## A plugin in five minutes

1. Install a C compiler for 64-bit Windows: [LLVM](https://github.com/llvm/llvm-project/releases)
   with the MinGW target (`clang --target=x86_64-w64-mingw32`), or
   [MSYS2](https://www.msys2.org/)'s `mingw-w64-x86_64-gcc`.
2. Write `my_mod.c`:

```c
#include "respect_plugin.h"
#include "respect_game.h"            /* the game's functions by name */

static const RespectApi *api;

/* After zombie_SpeedByDistance has set a zombie's walking speed: double it. */
static void faster(void *user, uint32_t addr) {
    uint32_t zombie = api->get_reg(RESPECT_A0);
    int16_t *speed = RESPECT_RAM(api, zombie + 0x866);
    if (*(uint8_t *)RESPECT_RAM(api, zombie + 0x315) <= 10) *speed = *speed * 2;
}

RESPECT_EXPORT int respect_plugin_init(const RespectApi *a) {
    api = a;
    a->hook(RG_zombie_SpeedByDistance, 0, faster, 0);
    return 0;                         /* 0: ready */
}
```

3. Build it (the headers are in RESPeCt's `port/include`):

```
clang --target=x86_64-w64-mingw32 -shared -O2 -o my_mod.dll my_mod.c -I port/include
```

4. Put it in a mod: `mods/my-mod/plugins/my_mod.dll`, switch **Plugins** on
   in F1 > Mods, start the game. `resurv.log` shows the plugin starting and
   anything it logs.

`port/plugins/zombie_speed.c` is this example complete (with the speed in a
text file); `port/plugins/example.c` logs rooms and messages and gives
health back over time.

## What a plugin can do

Through `RespectApi` (`port/include/respect_plugin.h`):

| | |
| --- | --- |
| `hook(addr, before, after, user)` | run your code before and/or after a game function, however it is called. `before` returns 1 to skip the function (set `V0` to its result) |
| `call(addr, a0, a1, a2, a3)` | call a game function, get its result |
| `get_reg` / `set_reg` | the function's arguments (`RESPECT_A0`..`A3`) and result (`RESPECT_V0`) |
| `ram`, `RESPECT_RAM(api, addr)` | the game's memory, by the game's addresses |
| `vram` | the graphics memory (1024 x 512) |
| `on_frame(cb, user)` | your code once a frame |
| `log(...)` | a line in `resurv.log` |
| `mod_dir` | your mod's folder, for your own settings files |

## Finding your way in the game

- **Functions:** `port/include/respect_game.h` names the 1138 functions
  RESPeCt runs as C (`RG_zombie_Update`, `RG_room_Load`,
  `RG_enemy_AttackInfo`...). What each does, its arguments and the memory it
  uses are described in its file in `port/decomp` (named in the header): for
  example `zombie.c` is the zombie, `hunter.c` the Hunter, `combat.c` the
  damage, `room_change.c` the doors. Other functions can be hooked by address
  too (`symbol_addrs.txt`).
- **Enemy and stage programs:** they are loaded into shared places (enemies
  at 0x800F8000 or 0x80100000, stages at 0x800F2C50), so a hook there runs
  for whichever program is loaded: check the enemy's type (`+0x315`) or the
  stage, as the example does. The zombie's functions in `respect_game.h` are
  those of slot B; slot A's are 0x8000 lower.
- **Game data without code:** much doesn't need a plugin at all:
  `data/COMBAT.txt` and `data/ROOMS.txt` change damage, hit points and where
  enemies are (see MODDING_GUIDE.md).

### Useful memory

| What | Address | |
| --- | --- | --- |
| Player's health | `0x800A8584` | s16, up to 200 |
| Player's position, rotation | `0x800A85D8` (x, y, z: s32), `0x800A8604` (s16 x3) | the player record is at `0x800A85C0` |
| Weapon held | `*(u32 *)0x800A8580` | points at the weapon's id (s16), its ammo at +4 |
| Stage, room | `0x800A866C`, `0x800B3F08` (s32) | the one being loaded: `0x800B4BA0`, `0x800B3E20` |
| Enemies | `*(u32 *)0x800AB510`, count `0x800AF530` | 0x8C4 bytes each: `+0x198` position, `+0x315` type, `+0x318` state, `+0x31E` hit points, `+0x866` walking speed, `+0x868` distance to the player (all fields: `port/decomp/enemy.c`) |
| Event flags | `0x80062FC0` | the story's progress (`port/decomp/stagprog.h`) |

### From the PC version

Where your PC DLL hooked or read something, this is the PS1 game's own:

| PC (`gun_bio.exe`) | RESPeCt (PS1 address) | |
| --- | --- | --- |
| `00479600` frame loop | `8006CAA0` | player, then the enemies in slot order |
| `0043A8A0` an enemy's update | `RG_enemy_CallProgram` (`8008DE84`) to each program; the zombie's `RG_zombie_Update` (`80105180`) | |
| `0043BC70` an enemy hits the player | `8008445C` | |
| `004609D0` enemy movement | `8007F404`, walls by `8007CB9C` | against the room's own walls |
| `005F3DAC` the room id | `0x800B3F08` (room), `0x800A866C` (stage) | |

## Good to know

- A plugin is a program: it runs only when the player switches plugins on.
- Hooks run on the game's thread between its instructions: keep them short.
- A quick save keeps the game's memory, not your plugin's own variables.
- Without plugins (or with plugins that only watch), the game is exactly the
  PS1's: functions are built with room for a hook at their start, nothing
  else changes.
