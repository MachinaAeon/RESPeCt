# Mod support: the code

What a mod can change, and how a modder uses it, is in
[docs/MODDING_GUIDE.md](../../../docs/MODDING_GUIDE.md) (short) and
[docs/MODDING.md](../../../docs/MODDING.md) (reference).

| File | What it does |
| --- | --- |
| `modfs.c` | The mods folder: finding the mods and their files, serving the disc's sectors with the mods' files in place, growing archives past the disc's end, the log, quick-save fixes. It includes its parts below, in order: one translation unit, so they share its state and helpers (the C tests include `modfs.c` the same way) |
| `modfs_sound.inc` | Streams (voices, music, movies' sound) and sound effects from WAVs, the PC's sound names |
| `modfs_music.inc` | A room's music: its sequence, or a recording played in its place |
| `modfs_pictures.inc` | TIMs, the PC's BMPs, `PICTURES/<file>/<n>.png`, HD versions |
| `modfs_texts.inc` | Rooms' messages, items, UI texts, fonts |
| `modfs_data.inc` | `data/COMBAT.txt`, `data/ROOMS.txt`, `data/SCRIPTS.txt` |
| `modfs_movies.inc` | Movies' frames from pictures |
| `modfs_lzss.inc`, `modfs_packed.inc` | The game's LZSS; blocks of the packed archives (OBJ, EV_OBJ, ITEMDAT) |
| `modfs_enemies.inc` | Enemies and bosses from the PC's NAB/NAC, skins as pictures |
| `modfs_extract.inc` | Writing out the originals (`extract`) |
| `modfs_diary.inc`, `modfs_pc_*.inc`, `modfs_pc_text.h`, `modfs_flc.h` | Diary pages; the PC's message pictures, diaries, title labels and book animation |
| `modio.c` | Files the game reads synchronously (fonts, pictures) staged from mods |
| `modmem.c`, `modfs_memory.c` | Extended RAM for mods: the model arena and the archives' sizes |
| `modplug.c` | Plugins: loading them, hooks on the game's functions ([docs/PLUGINS.md](../../../docs/PLUGINS.md)) |
| `modvideo.c` | Movies as video files through FFmpeg's DLLs |
| `tables/` | Generated tables: where the PC version's files are on the PS1 disc, which PC sound or music is which PS1 one. Each says which script in `tools/mod_tables/` wrote it |

Tests: `python tools/tests/c_tests.py` (asset-free). Design notes and
the evidence for each part: [docs/mods/](../../../docs/mods/README.md).
