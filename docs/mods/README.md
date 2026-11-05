# Mod support: design notes

For modders: [MODDING_GUIDE.md](../MODDING_GUIDE.md) (short) and
[MODDING.md](../MODDING.md) (reference); plugins: [PLUGINS.md](../PLUGINS.md).
The code is in `port/src/mods/` ([its map](../../port/src/mods/README.md)).

These notes are for whoever works on the mod support itself:

| File | What's in it |
| --- | --- |
| [STATUS.md](STATUS.md) | Every part of the mod support (M01-M31): what it does and how it was checked; what's still open; the limits and the memory gates |
| [PC_COMPATIBILITY.md](PC_COMPATIBILITY.md) | How the PC version's files are recognized: the overview, audio, message pictures, diaries and the portable importer |
| [MEMORY.md](MEMORY.md) | The model and animation arena in extended RAM, and its save-state rules |
