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

`BASE` is a headless build (`port/build/linux/resurv_headless`) from before
the change. The scenarios need the disc image and the save states in
`testdata/` (not in git).

Scenarios: `intro` (new game), `weapons`, `walk`, `menu` (status screen),
`ammo`, `launcher`, `reloads`, `heavy`, `fight1`, `hurt1`, `fight2`,
`fight1s`, `fight1g` (zombies), `door` (a door into the next room), `death`
(killed, game over, continue), `phone` (saving at the payphone: save screen,
memory card), `gload` (loading a saved game from the title screen).

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
- Names: what it does (`room_Change`, `gameover_Step`), unique across
  `native.txt`. Constants and fields get names or comments where known.
- A wrapper (`~name` in native.txt) instead runs around the translation (it
  calls `f_<module>_<addr>` itself), for port features that need to change
  what a function does without rewriting it.

## Save states and `LIVE`

A save state is made at a moment the game waits (a loop's poll), never
while native code is running (`g_native_depth`): it waits for that to
return. So a function that runs for a long time, over many frames (the main
loop, a menu that waits for input inside itself), must stay translated, or
save states could never be made. `profile.sh` marks every function that was
running when the game polled with `LIVE n`, n the most frames it had been
running then; don't decompile one with n of 60 or more (a second). Waiting
a few frames, through a room load say, is fine.

Every decompiled function keeps its translation too (`f_<module>_<addr>`,
`RtFunc.resume`): a save state made before the function was decompiled can
have it on its call chain, and continues in the translation.
