# PC audio resource compatibility

This candidate extends automatic PC asset loading. It does not establish
complete audio-mod compatibility or listening/gameplay parity.

## Supported inputs

```
mods/my-mod/PSX_DATA/noise/noise.bin
mods/my-mod/PSX_DATA/WAV/ENEMY/DOG/SND-001.WAV
```

The existing flat paths also work. No sound assets are shipped. The feature
runs only when a mod supplies one of these files; without these files, the original PS1
path remains in use. Use `tools/pc_mod.py check` before import as documented in
[PC_RESOURCE_COMPATIBILITY.md](PC_RESOURCE_COMPATIBILITY.md).

### The PC noise archive

The verified source archive is 16,355,328 bytes. Its reference SHA-256 is
`03ecede4126b645e9fbe0d9447c93aa9ef0ad919114ee40f7a312d4823135965`.
The supported PS1 NOISE.BIN is 16,625,664 bytes. These are different layouts:
PS1 block15 (270,336 bytes) is absent from PC; block264 has different sample
data. Ordered matching accounts for all source bytes, including duplicate
banks at distinct positions. Matching a duplicate bank at its first occurrence
would assign modifications to the wrong instance and is not used.

`modfs_pc_noise.h` contains only offsets, lengths, CRCs and allocation metadata,
not sample bytes. It describes 376 nonempty source ranges: 374 writable banks
and two retained reference-only blocks (264 and273). Block273 has no supported
editable bank shape in this adapter. The omitted PS1 block15 is never written.

For each source bank:

- An unchanged reference bank leaves the corresponding PS1 bytes intact. This
  also preserves the PS1 version of block264 and earlier mod replacements.
- A changed supported bank must retain its part offsets, VAB structural header,
  program/waveform counts and original main/sound-memory bounds. Audio tones,
  sample bytes and non-structural values can change within that contract.
- VAB size-table entry zero must remain zero, as in every audited original bank.
  The native consumer includes this entry in its address/size sum; treating it
  as unused would allow writes with invalid sample offsets or allocation.
- Any incompatible bank, source-size mismatch or changed reference-only block
  rejects the entire file before native writes. No partial bank set is applied.

The whole file is an ordered delta against the supported PC reference, rather
than a direct PS1 archive replacement. A completely unchanged PC archive is a
no-op and can coexist with individual sound replacements. Changed PC archives
and individual replacements in the same NOISE archive are conservatively
reported as conflicts by the importer; precise cross-bank merging is not
claimed. A different PC distribution needs a separately verified reference
profile; matching file size alone is insufficient.

The adapter retains existing disc sectors, file locations, CD requests and
interrupt/playback ownership. It does not enlarge the 512 KiB sound memory,
change sound events or introduce another mixer.

Metadata can be reproduced from owned original files with:

```
python tools/mod_noisenames.py PC/noise/noise.bin disc.bin new-profile.h
```

This tool deliberately refuses additional unknown differences or trailing bytes
and writes only a new output file. It does not modify originals or install a
new profile automatically.

### Verified loose WAV events

`WAV/ENEMY/DOG/SND-001.WAV` targets NOISE block23, waveform2. The proof is the
original event path, not a filename/PCM similarity guess:

- PC executable `0043D630` reaches `00471260(11,1)` through its locomotion
  branch. The dispatcher constructs the DOG/SND-001 filename and loads it
  through `0057F970`.
- PS1 enemy program lookup maps kind11 to overlay51 (Cerberus), and the original
  bank table maps it to bankfile3+20=23.
- `cerberus.c:cerberus_StepSound` calls the native enemy sound with spatial
  tone12 and mono cue1. The original VAB23 maps mono1 and spatial12/13 to VAG2.

The existing converter resamples the supplied WAV to the original bank's pitch
basis and encodes it into the bounded original sample allocation. The original
SPU event, panning and playback continue to own the sound. Resampling/encoding
is not bit-identical to the source PCM and must not be described as such.
A longer sound can still be cut by the established bank budget; existing
runtime diagnostics report that limit. General unlimited sound assets remain
open.

### PC sounds that are the PS1's recordings

`modfs_pc_sfx.h` is written by `tools/mod_sfxnames.py` from the PC's
`PSX_DATA` and an `extract` folder: each PC loose WAV is compared with every
bank sound (both at 11025 Hz mono, full normalized cross-correlation at the
best lag). A pair goes in only when it is the same recording: at least 0.94,
no other bank sound within 0.08, lengths within 0.8-1.25. 45 PC files
qualify: the zombies', dog's, crows', spider's, crocodile's, Ivy's and a
licker's sounds, eight key sounds and one event; the PC's door, footstep and
weapon sounds are other recordings. The method gives the event-verified
DOG/SND-001 -> block 23 sound 2 independently (0.959). A replacement goes to
that sound and its copies in the other banks. Checked in the headless
runner: in stage 2 room 1, 328 of 416 notes played were the mod's sounds
given by the PC's names (wav/enemy/dog).

### PC sounds by their event

`modfs_pc_sfx_events.h` (`tools/mod_sfxevents.py`, from the PC's
`PSX_DATA`, NOISE.BIN, KERNEL.BIN and TEST.BIN) routes the other PC
recordings by the event the PC plays them for, read from gun_bio (a port
of the PS1 code, `C:\code\PC_GUN_BIO_2\PSX\`):

| PC file | gun_bio | PS1 sound |
| --- | --- | --- |
| `wav/enemy/<kind>/snd-NNN.wav` | 00471260 (kind, NNN) | the kind's bank (TEST 0x800A22BC + 0x14; zombies 30 and 33), sound NNN: program NNN >> 4, tone NNN & 15 |
| `wav/door/XY-n.wav` | 004711C0 (door scene XY, n) | block 0x3C + XY (room_change.c step 9 loads it into bank 6), sound n |
| `wav/foot/NNN-f{L,R}.wav` | 004721C0 (room NNN, floor f) | the room's bank 6, block 0x10E + 0x800A2364[stage] + room, sound f (0x800BB194) |
| `wav/key1/key-NNN.wav` | 004722B0 (NNN) | block 0, sound NNN |
| `wav/weap/<weapon>-<action>.wav` | 00472620 (weapon id, action) | block id + 1 (bank 1, 8008E324): ATTACK 5 (launchers 3), BACK 1, RELOAD 2 (handguns 3), NO 0 |

The PC rooms 0-121 are its stageNNNN() functions, each loading one PS1
stage and room through 00472A10; the table is in the tool, and all 36
matched music WAVs fall in their room's sound set. The enemy rule gives
all 45 recorded pairs above (the repeated tones too: zombie 5/7/9/11, dog
15). 745 more files: 158 enemy, 28 weapon, 248 door, 310 footstep, 1 key;
CHO2 has no PS1 bank. The weapons' actions: gun_bio plays ATTACK on a shot
(8008E368 plays sound 5; the launchers' shot is the round leaving, sound
3), BACK when the spent casing's animation ends (the PS1's clink, sound
1), RELOAD when the last round is in (the handguns' magazine-in, sound 3;
the others load one at a time with sound 2), NO on a dry fire (sound 0).
Headless: 28 handgun shots play sounds 5 and 1, the reload 2 then 3; with
the PC's weapon WAVs all of them are the mod's (bench check 26). The
rooms' event WAVs (`wav/event`) are the PC's recordings of whole events
(`EVE-ALL`, the TV's loop): no PS1 sound is the same event, so they stay
NOISE names. Where several PC files are one PS1 waveform, each goes to the
sound it names and the copies get the lowest ranked (enemies, keys,
footsteps with the left foot first, doors; by name); `pc_mod.py` marks the
others duplicate. Checked headless: through stage 8 room 0's door, the
door scene's notes are the mod's (bench check 25).

The recordings agree with the event routes below: DOG005 is bank 23's sound
5 (waveform 6), not the 12 an action number suggested.

### Earlier withdrawn candidates

DOG006, CROW002/003 and RICKER004/007 were once withdrawn: numeric action
states had been equated across the two executables without proof that the
events match. That proof is now the PC code itself: gun_bio's enemy sound
function (00471260) builds `snd-NNN` from the sound number its callers, the
ported PS1 enemy code, pass, and the PS1 plays that number from the kind's
bank (snd_CueEnemyKind: nibbles = program, tone). Every one of the 45 pairs
found by recording follows it, repeated tones included. These files are
other recordings of those events (their sound differs from the PS1's, as
the earlier comparison found), so they go there by event, like the doors
and footsteps (`modfs_pc_sfx_events.h`).

### Historical conversion tests are not event proof

Earlier controlled runs demonstrated that the converter could change the
then-configured eight DOG/CROW targets and two RICKER targets while preserving
unrelated waveforms. Those results establish converter isolation only. They
do not validate the semantic assignments, which have now been withdrawn.
Negative regression tests cover all ten rejected aliases, native-route
availability, the independent DOG001 alias, duplicate/conflict handling and
source preservation. No listening or full-game acceptance is claimed.
