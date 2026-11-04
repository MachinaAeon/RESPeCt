"""Build an offline Windows importer bundle from an existing CPython installation.

No downloads, installation or game data. Output must be a new directory.
The source checkout and the chosen interpreter are never modified.
"""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys,zipfile

TABLES=['disc_ids.h','modfs_pc.h','modfs_bmp.h','modfs_pack.h','modfs_wav.h',
        'modfs_pc_sfx.h','modfs_pc_pickups.h','modfs_pc_noise.h','modfs_pc_messages.h','modfs_pc_diaries.h']

def build(output, python_root, compiler):
    root=Path(__file__).resolve().parents[1];output=Path(output);runtime=Path(python_root)
    if output.exists():raise ValueError('Output already exists; choose a new directory.')
    for name in ['python.exe','pythonw.exe','python311.dll','LICENSE.txt','Lib','DLLs','tcl']:
        if not(runtime/name).exists():raise ValueError('This recipe requires a complete existing CPython3.11 installation: '+name)
    if not(runtime/'Lib/tkinter').is_dir():raise ValueError('The chosen runtime has no Tkinter.')
    import pc_mod
    pc_mod.Catalog(root/'port/src')  # refuse missing/inconsistent production metadata
    output.mkdir(parents=True);target=output/'runtime';target.mkdir()
    for p in runtime.iterdir():
        if p.name in ['python.exe','pythonw.exe','python3.dll','python311.dll','LICENSE.txt'] or p.name.startswith('vcruntime')and p.suffix.lower()=='.dll':
            if p.is_symlink():raise ValueError('Interpreter files must not be symlinks.')
            shutil.copy2(p,target/p.name)
    (target/'DLLs').mkdir()
    for p in sorted((runtime/'DLLs').iterdir()):
        if p.suffix.lower() in {'.dll','.pyd'} and not p.stem.startswith(('_test','_ctypes_test')):
            if p.is_symlink():raise ValueError('Runtime extensions must not be symlinks.')
            shutil.copy2(p,target/'DLLs'/p.name)
    shutil.copytree(runtime/'tcl',target/'tcl',ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
    with zipfile.ZipFile(target/'python311.zip','w',compression=zipfile.ZIP_DEFLATED)as z:
        for p in sorted((runtime/'Lib').rglob('*.py')):
            rel=p.relative_to(runtime/'Lib')
            if any(part in {'site-packages','__pycache__','test','tests','idlelib','turtledemo','ensurepip'}for part in rel.parts):continue
            if p.is_symlink():raise ValueError('Standard library sources must not be symlinks.')
            z.write(p,rel.as_posix())
    (target/'python311._pth').write_text('python311.zip\n.\nDLLs\n../tools\n',encoding='utf-8')
    (output/'tools').mkdir();(output/'port/src').mkdir(parents=True)
    for name in ['pc_mod.py','pc_noise.py','pc_mod_gui.py','pc_mod_launcher.c','build_pc_mod_installer.py']:
        shutil.copy2(root/'tools'/name,output/'tools'/name)
    for name in TABLES:shutil.copy2(root/'port/src'/name,output/'port/src'/name)
    shutil.copy2(root/'LICENSE',output/'LICENSE')
    (output/'README.txt').write_text('''RESPeCt Mod Installer

Extract this entire folder, then run RESPeCt Mod Installer.exe.
No Python installation, source checkout, admin rights or Internet access is required.
1. Browse to the mod source folder (or its PSX_DATA folder).
2. Browse to the game folder containing RESPeCt.exe and choose a new mod name.
3. Check package, read any blockers, then Import into game.

Unknown/incompatible files block the entire import. Existing mod folders are never
overwritten. Original game data, source files and order.txt are not modified.
If order.txt defines activation, add the displayed new mod name there yourself.
Restart the game before testing a changed mod. Keep saves with matching mod files.
Recognition is not proof of decoding, memory fit or gameplay compatibility.

No game data, disc image, textures or sounds are included. This developer candidate
uses the source checkout's compatibility tables. It does not support every PC mod.

Licenses: LICENSE covers the importer; runtime/LICENSE.txt covers CPython and its
bundled components. Tcl/Tk notices remain under runtime/tcl. Runtime files come
from the developer's existing CPython3.11 installation; no download is performed.
''',encoding='utf-8')
    subprocess.run([str(compiler),'--target=x86_64-w64-mingw32','-O2','-municode','-mwindows',
                    str(root/'tools/pc_mod_launcher.c'),'-o',str(output/'RESPeCt Mod Installer.exe'),'-luser32'],check=True)
    manifest={p.relative_to(output).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
              for p in sorted(output.rglob('*'))if p.is_file()}
    (output/'bundle-manifest.json').write_text(json.dumps({'python_version':'3.11','files':manifest},indent=2),encoding='utf-8')
    return output

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--python-root',type=Path,default=Path(sys.executable).parent)
    parser.add_argument('--cc',type=Path,required=True)
    args=parser.parse_args();print(build(args.output,args.python_root,args.cc))
