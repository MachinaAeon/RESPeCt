# Portable PC mod importer

The desktop importer reuses `tools/pc_mod.py` for package inspection and atomic
installation. It does not maintain a second compatibility catalog. Its Windows
bundle contains a private interpreter, standard library, Tk and the production
mapping tables. Authors do not need Python, administrator rights, network access
or a source checkout to run the resulting bundle.

This is an asset-package importer, not a mod manager or a guarantee that every
Windows resource is supported. Unknown resources block the entire package.

## Use the bundle

1. Extract the complete importer folder, keeping `runtime`, `tools` and `port`.
2. Run **RESPeCt Mod Installer.exe**.
3. Browse to the mod's source folder and the game folder containing RESPeCt.exe.
4. Choose a new mod folder name and use **Check package**. Read any blockers.
5. Use **Import into game**, then restart the game to test the mod.

The importer creates `mods/<name>` and a compatibility report inside it. Existing
destinations are refused. It never copies just the recognised fraction, replaces
original game files, starts the game or changes `order.txt`. If that file controls
mod activation, add the displayed name there yourself; the GUI reports this case.
There is no automatic reordering, disabling or save-state migration. Keep states
with the matching mod files and priority order.

The window remains responsive during inspection/copying; closing is deferred
until the current operation finishes. Failures are shown in the result area.
Changing source/game/name requires a fresh check before the import button is
enabled. The import itself always repeats validation, so changing source files
after a check does not bypass the production importer's safeguards.

## Rebuild with existing tools

The current offline recipe requires an existing complete Windows CPython3.11
installation including Tk, and an existing Clang/MinGW target toolchain. It does
not download or install them. From the checkout:

```
python tools/build_pc_mod_installer.py --output path/to/new-bundle --python-root path/to/Python311 --cc path/to/clang.exe
```

The output must be new; an existing directory is refused. It contains no game
data. The launcher invokes only its relative `runtime/pythonw.exe` and
`tools/pc_mod_gui.py`, using an isolated module path. Installed packages, user
site directories and Python environment variables are not dependencies. Source
tables are copied at build time; rebuild the bundle after changing converters
or compatibility tables. Its manifest records bundled-file SHA256 hashes.

`LICENSE` covers the importer source. CPython's bundled notices remain in
`runtime/LICENSE.txt`; Tcl/Tk notices remain under `runtime/tcl`. Do not add
game assets or the installed disc to the bundle. Compiled bundles stay outside
tracked source. Packaging for other Python versions/platforms is not established.

## Verification and limits

`python tools/tests/pc_mod_gui_test.py` uses synthetic packages and the real
inspection/copy backend. It covers safe destinations, invalid names, existing
folders, blocked files, source preservation and unchanged activation priorities.

The source-built bundle was copied outside the source checkout to a Unicode
path, then executed with empty PATH and hostile PYTHONHOME/PYTHONPATH values.
Inspection/import and existing/unknown-file refusals passed. The actual native
launcher opened the Tk window; source/game entry, check, import, refusal of an
existing destination, blocked-file display and normal closure were observed
with synthetic data. The copied file's hash matched its source. Folder-picker
dialogs, large-package progress and every Windows system have not been accepted.

One earlier diagnostic import received Windows error5 during the atomic rename;
the source remained intact and no destination was published. The same bundled
and installed interpreters then passed three controlled diagnostic imports,
and the final isolated suite plus GUI import passed. The intermittent OS error's
cause is unconfirmed. It remains a reliability limit; no silent retry/timer or
partial-copy workaround was introduced. The GUI reports OS failures so the author
can inspect the target and retry explicitly.

Successful recognition/import is not runtime decoding, memory-fit or gameplay
acceptance. Remaining asset coverage is tracked in PC_RESOURCE_COMPATIBILITY.md.
