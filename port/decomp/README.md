# Decompiling: replacing translated functions with hand-written C

`port/gen/` is the game's machine code translated to C, one C function per
game function, instruction by instruction. The files here replace some of
those with readable C that does exactly the same thing to the game's RAM.
A function listed in `tools/recomp/native.txt` (`module address name`)
isn't called as its translation any more: every call goes to `n_<name>()`.

The aim is always **identical behaviour**. A decompiled function is done
when the equivalence tests show the game's RAM identical to the translated
original's, frame for frame, in every scenario. Fixes and port features are
separate changes, made afterwards on top of readable code.

## Tools

| Command | What |
|---|---|
| `tools/dis.sh ADDR...` | A function's disassembly (TEST module; `-m asm/1300.s` for the kernel, `-m asm/enemprog/ovl50.s` etc. for overlays), with the targets of its jump tables |
| `tools/eq/profile.sh WORK [scenario...]` | Every translated function the scenarios run: call count, size, and `LIVE n` (see below). The list of what to decompile next |
| `tools/eq/run.sh BASE NEW OUT [scenario...]` | The equivalence test: replays input scenarios with two builds, compares RAM every 50 frames (outside the stacks) and the screenshots |
| `tools/eq/coverage.sh BASE WORK` | Decompiled functions the scenarios never reach (keep it at none: an unreached function is untested) |
| `tools/eq/sscompat.sh BASE NEW WORK` | Save states made by BASE (while loading rooms, at game over, in the save screen...) continue identically in NEW |
| `tools/eq/ssmake.sh BASE NEW WORK [frames...]` | NEW can still make save states (the opening movie, the title, the story text, in game), and both builds continue them as NEW's own run goes on |

`BASE` is a headless build (`port/build/linux/resurv_headless`) from before
the change. The scenarios need the disc image and the save states in
`testdata/` (not in git).

Scenarios: `intro` (new game), `weapons`, `walk`, `menu` (status screen),
`ammo`, `launcher`, `reloads`, `heavy`, `fight1`, `hurt1`, `fight2`,
`fight1s`, `fight1g` (zombies), `door` (a door into the next room), `death`
(killed, game over, continue).

### Save states still needed

Code that no save state reaches yet, so the scenarios can't test it. Make the
save (F5 in the game) and put it in `testdata/`:

- **Sewer water, player in it.** Bruce standing in or right next to the water
  in a sewer room. Tests the water drivers' player floor-height and slope
  path (`water_height`/`water_slope` in `gs_drivers.c`, flag at
  0x800A8674). `area11` has a water room, but the player never stands over
  the surface there.

## Steps

1. Pick functions from `profile.sh` output that the scenarios run (and that
   aren't `LIVE` 60 or more, see below). Work through a related group: a
   caller and its callees, one subsystem.
2. Read the disassembly (`tools/dis.sh`). The game was compiled with GCC,
   some files without optimisation (every variable through `$fp`), most with.
3. Write the C in a file here (by subsystem, with a header comment saying
   what the code does), each function as `void n_<name>(void)` with a
   comment giving its address and what it does. Add the line to
   `tools/recomp/native.txt`.
4. `python3 tools/recomp/recomp.py test` (the module the function is in;
   `funcs.c` is always rewritten), `make -C port`.
5. `tools/eq/run.sh BASE port/build/linux/resurv_headless OUT` and
   `tools/eq/coverage.sh BASE WORK`: every scenario RAM-identical, and every
   decompiled function reached. When something differs, `cmp.py` names the
   first frame and the RAM ranges; find whose variable that is and compare
   the C against the disassembly again.

## Conventions (see `decomp.h`)

- Arguments arrive in `A0`..`A3`, the result goes in `V0`. Set `V0` exactly
  as the original leaves it whenever a caller could use it (look at the
  callers: anything reading `$v0` after the `jal`).
- `GV(type, addr)` is the game variable at `addr`; `GA(type, addr)` an
  array. Use the original's access width and signedness (`lh` is `s16`,
  `lhu` is `u16`, `lb`/`lbu` likewise): a `sh` stores 16 bits even if the
  value was computed as 32.
- `GV`/`GA` only reach main RAM. A pointer that can point at the scratchpad
  (0x1F800000, where some code keeps its stack or work data) goes through
  `gp()` or `MEM_R32`/`MEM_W32` (recomp.h).
- Call game code with `gcall(addr, a0, a1, a2, a3)` / `GCALLn`, by address
  (whether that function is translated or native). Arguments 5 and up go on
  the game's stack at sp+0x10: `gstack_alloc` (see `inventory.c`). A local
  whose address the original passes to a callee goes on the game's stack
  too.
- Arithmetic: MIPS adds wrap. Keep sums that can overflow in `u32` (signed
  overflow is undefined in C). `div` by zero: `mips_div`. `slt` is a signed
  compare, `sltu` unsigned: mind which.
- Order of memory accesses matters only when they can overlap (a store
  through a pointer between two reads of a global, say); otherwise write it
  naturally.
- GTE (`mtc2`/`ctc2`/`lwc2`/`cop2`/`mfc2`/`swc2`): `gte_write_data`,
  `gte_write_ctrl`, `gte_command`, `gte_read_data`. Store with
  `gte_swc2(addr, reg)` as the original does (`swc2`), never as a plain
  store: the high-resolution renderer keeps fractional screen positions per
  RAM address written that way.
- A jump table (`jr` through a table): read the table from RAM (or with
  `tools/dis.sh`) to see which case is which. The translation's
  `switch (t_)` lists the targets by address, not in table order.
- Hardware registers (SIO, CD-ROM, timers) read with `MEM_R8`/`MEM_R16`/
  `MEM_R32` in the original's order: reads can have side effects (a
  response FIFO pops). Emulated time only advances at polls, so a wait loop
  on hardware needs its `LOOP_POLL` exactly where the original branches
  back.
- Functions run from the interrupt handler (libpad, libcd) use libetc's
  interrupt stack (8005A2A0-8005B24C); tools/eq ignores it like the
  thread stacks.
- Game code called from a native saves the s-registers it uses on its
  stack, and stale stack bytes can later be read as data (a buffer reused
  from old stack space). Where a native calls game code and the RAM
  doesn't match, give the callees the original's s-register values and
  write the original's register saves into the `FRAME` (see
  `libsnd_SeqCalledTbyT`, lib_snd.c).
- Names: what it does (`room_Change`, `gameover_Step`), unique across
  `native.txt`. Constants and fields get names or comments where known.
- A wrapper (`~name` in native.txt) instead runs around the translation (it
  calls `f_<module>_<addr>` itself), for port features that need to change
  what a function does without rewriting it.

## Enemy programs in both slots

Every enemy program is on the disc twice, built for enemy slot B
(0x80100000, ENEMPROG 49-64) and slot A (0x800F8000, 33-48), the same
code relocated by 0x8000. Decompile the slot-B one (the slot the game
prefers); `tools/recomp/slot_twins.py` then writes `twin_<file>_a.c` with
the overlay's addresses shifted and lists those functions at the end of
native.txt. Rerun it after changing an enemy file. Write an address the
way the original builds it: a value the original leaves from a `lui` is
`LUI_OF(address)` (cerberus.c), so the twin gets its own slot's value.
`RT_ENEMY_SLOT_A=1` makes a test build load enemy programs into slot A
first; comparing a build against one without the twins, both with it set,
tests the twins (`RT_ENEMY_SLOT_A=1 eqall.sh ...`).

## Save states and `LIVE`

A save state is made at a moment the game waits (a loop's poll), and only
where it can be continued: in translated code, or at a `SAVE_POLL`, and
never below a native that called game code with `gcall`
(`g_native_depth`): it waits for that to return. Game threads switched
away don't count (each thread has its own depth): while the mode's thread
waits in a mode switch, saves are made in the main thread's frame wait
(VSync, 0x800255F8, whose wait 0x80025770 polls with `SAVE_POLL`).

So a native that runs for a long time, over many frames, in the thread
that is running (a movie, a menu that waits for input inside itself), must
let the waits below it make save states. `profile.sh` marks every function
that was running when the game polled with `LIVE n`, n the most frames it
had been running then. Waiting a few frames, through a room load say, is
fine; for one with n of 60 or more (a second):

- keep the original's frame exactly (`FRAME`, the registers saved at the
  original's offsets, `$fp` set if it uses one);
- call the code it waits through with `gcall_ss(ra, ...)` (libgs.h), not
  `gcall_ra`: the callee isn't counted as running below a native. A save
  made down there continues in this function's translation at ra, with
  the saved registers, so before the call set every register the
  translation reads after ra before writing it (the s registers it keeps
  its loop state in, as the original has them);
- a wait loop of its own polls with `SAVE_POLL(pc)` (poll.h), with the
  registers the translation uses from that poll on set the same way.

`tools/eq/sscompat.sh` checks save states made by the base build;
`tools/eq/ssmake.sh BASE NEW WORK [frames]` has the new build make save
states through the intro scenario (the opening movie, the title, the
story text, the game) and checks that one is made at the frame asked and
that both builds continue it as the new build's own run goes on.

Every decompiled function keeps its translation too (`f_<module>_<addr>`,
`RtFunc.resume`): a save state made before the function was decompiled can
have it on its call chain, and continues in the translation.
