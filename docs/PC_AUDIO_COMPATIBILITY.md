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

This replaces the numbering guesses below: DOG003/005/008/009 and CROW004
are now routed by their recordings (DOG005 to bank 23's sound 6, not the 12
its action number suggested); DOG006 (two equal candidates), CROW002/003
and RICKER004/007 (below the threshold) stay out.

### Withdrawn loose-WAV candidates

Ten previously listed aliases have been removed from automatic routing:
DOG003/005/006/008/009, CROW002/003/004, RICKER004/007. The previous
investigation established dispatcher paths and PS1 VAB tone assignments, but
equated numeric action states across different executables without proving
that their events match. That is insufficient to choose a replacement target.
The importer blocks these filenames; the runtime leaves native sound data
untouched. Use an explicitly identified `NOISE/<block>/<waveform>.WAV` instead.
The separate DOG001 locomotion/step route above remains supported.

Additional checks ruled out a simple numbering explanation: PC families13/14
are dispatched to RICKER without a cue shift; PS1 VAB waveform indices are
one-based and its tone consumer preserves the cue argument. Decoded sample
similarity also disagrees with the two withdrawn RICKER pairings. Similarity
alone is not used to invent replacement aliases. Reintroducing any candidate
requires matching original event semantics, conditions and timing, followed
by natural playback/listening acceptance.

## WAV format validation

The PCM reader and importer now reject truncated fmt/data chunks, empty or
partial PCM frames and unsupported encoding/channel layouts. RIFF traversal
uses checked sizes. The loop metadata reader follows the same bounds. The
resampling frame count and source position avoid integer overflow. Valid
16-bit mono/stereo PCM, including ordinary extra metadata chunks, retains the
existing converter path. No new timing multiplier or gameplay patch is added.

## Evidence and limits

- Asset-free tests cover ordered copying, omitted/different reference banks,
  invalid layouts, memory budgets, reserved VAB entries, structural headers,
  malformed PCM, rate arithmetic, source preservation and conflicts.
- Five sequential real headless runs cover no mod, unchanged PC archive,
  one valid bank delta, a later incompatible bank after a valid delta, and
  the loose DOG WAV. The unchanged and rejected archives match native bytes;
  the valid delta changes exactly its one intended native byte.
- The WAV run changes only NOISE23 waveform2. All other 1694 decoded waveforms
  match their original extracted WAVs. Its final encoded output also matches
  the pre-validation-hardening output for the same valid input.
- CPU, original RAM, cycle counter, native VRAM and displayed inventory pixels
  match controls at the compared frames. The no-mod state also matches the
  preceding candidate. The installed executable and original saves are intact.

These are loading/conversion/regression tests. They do not prove natural dog
encounter playback, acoustic quality, spatial listening, music loops, movie
synchronization or complete interactive input acceptance. No such gameplay
acceptance is claimed. The original PC noise reference does not provide an
automatic identity map for all loose WAV filenames.

### Historical conversion tests are not event proof

Earlier controlled runs demonstrated that the converter could change the
then-configured eight DOG/CROW targets and two RICKER targets while preserving
unrelated waveforms. Those results establish converter isolation only. They
do not validate the semantic assignments, which have now been withdrawn.
Negative regression tests cover all ten rejected aliases, native-route
availability, the independent DOG001 alias, duplicate/conflict handling and
source preservation. No listening or full-game acceptance is claimed.
