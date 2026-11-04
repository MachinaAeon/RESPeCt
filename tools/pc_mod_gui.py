"""Folder-based desktop front end to the strict PC asset package importer.

Importing a package never launches the game or establishes gameplay compatibility.
The portable build includes its interpreter and the production metadata tables.
"""
from pathlib import Path
import json,os,queue,sys,threading
import pc_mod


def catalog():
    return pc_mod.Catalog(Path(__file__).resolve().parents[1]/'port/src')


def destination(game, name):
    game=Path(game)
    if (not name or name!=name.strip() or name[0] in '._#;' or name[-1] in '. '
        or len(name.encode('utf-8'))>100 or any(ord(c)<32 or c in '/\\:<>"|?*' for c in name)
        or pc_mod.RESERVED.match(name)):
        raise pc_mod.PackageError('Choose a plain mod folder name (no reserved names, paths or leading dot/underscore/comment marker).')
    if not game.is_dir() or not (game/'RESPeCt.exe').is_file():
        raise pc_mod.PackageError('Select the game folder containing RESPeCt.exe.')
    for p in [game,game/'mods']:
        if os.path.lexists(p) and (pc_mod.is_link(p.lstat()) or not p.is_dir()):
            raise pc_mod.PackageError('The game and mods folders must be ordinary directories, not links.')
    return game/'mods'/name


def install(source, game, name, metadata=None):
    target=destination(game,name)
    report=pc_mod.import_package(source,target,metadata or catalog())
    notice=('Imported. order.txt controls activation: add '+name+' there before restarting. Existing priorities were preserved.'
            if (Path(game)/'mods/order.txt').exists() else 'Imported. Restart RESPeCt to load the mod. The compatibility report is inside its folder.')
    return target,report,notice


def main(argv=None):
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',type=Path,help='inspect a package without showing the window')
    parser.add_argument('--report',type=Path)
    parser.add_argument('--smoke-report',type=Path,help=argparse.SUPPRESS)
    parser.add_argument('--smoke-close',action='store_true',help=argparse.SUPPRESS)
    args=parser.parse_args(argv)
    if args.check:
        return pc_mod.main(['check',str(args.check),*(['--report',str(args.report)]if args.report else[])])
    if args.report:parser.error('--report requires --check')
    # These are local child-process settings; no Windows configuration changes.
    portable=Path(sys.executable).parent
    if (portable/'tcl/tcl8.6').is_dir():
        os.environ['TCL_LIBRARY']=str(portable/'tcl/tcl8.6')
        os.environ['TK_LIBRARY']=str(portable/'tcl/tk8.6')
    import tkinter as tk
    from tkinter import ttk,filedialog,messagebox
    root=tk.Tk();root.title('RESPeCt Mod Installer');root.minsize(720,490)
    frame=ttk.Frame(root,padding=16);frame.pack(fill='both',expand=True)
    source=tk.StringVar();game=tk.StringVar();name=tk.StringVar(value='My mod')
    ttk.Label(frame,text='Import a PC asset mod',font=('Segoe UI',14,'bold')).grid(row=0,column=0,columnspan=3,sticky='w')
    ttk.Label(frame,text='Original game data stays intact. Unknown or incompatible files block the entire import.',wraplength=680).grid(row=1,column=0,columnspan=3,sticky='w',pady=(4,14))
    controls=[]
    def browse(value):
        chosen=filedialog.askdirectory(parent=root,mustexist=True)
        if chosen:value.set(chosen)
    for row,label,value in [(2,'Mod source folder',source),(3,'RESPeCt game folder',game),(4,'New mod name',name)]:
        ttk.Label(frame,text=label).grid(row=row,column=0,sticky='w',pady=5)
        entry=ttk.Entry(frame,textvariable=value,width=60);entry.grid(row=row,column=1,sticky='ew',padx=8);controls.append(entry)
        if row!=4:
            button=ttk.Button(frame,text='Browse...',command=lambda v=value:browse(v));button.grid(row=row,column=2);controls.append(button)
    ttk.Label(frame,text='Close the game before testing changed mods. Import does not update order.txt or old save states.',wraplength=680).grid(row=5,column=0,columnspan=3,sticky='w',pady=8)
    output=tk.Text(frame,height=13,wrap='word',state='disabled');output.grid(row=7,column=0,columnspan=3,sticky='nsew',pady=10)
    status=tk.StringVar(value='Choose a mod folder, then Check package. Recognition is not gameplay verification.')
    ttk.Label(frame,textvariable=status,wraplength=680).grid(row=8,column=0,columnspan=3,sticky='w')
    buttons=ttk.Frame(frame);buttons.grid(row=6,column=0,columnspan=3,sticky='w')
    events=queue.Queue();busy=False
    try:metadata=catalog()
    except (OSError,pc_mod.PackageError) as exc:
        messagebox.showerror('Incomplete importer','Cannot read the bundled compatibility tables. Extract the complete folder.\n'+str(exc),parent=root)
        root.destroy();return 2
    def display(text):
        output.configure(state='normal');output.delete('1.0','end');output.insert('end',text);output.configure(state='disabled')
    def start(importing):
        nonlocal busy
        if busy:return
        values=(source.get(),game.get(),name.get())
        if not values[0]:messagebox.showerror('Choose a source','Choose the mod source folder.',parent=root);return
        busy=True
        for widget in controls:widget.configure(state='disabled')
        check.configure(state='disabled');copy.configure(state='disabled');status.set('Checking and validating files...')
        def work():
            try:
                if importing:
                    target,report,notice=install(*values,metadata);events.put((report,notice+'\n'+str(target),None))
                else:events.put((pc_mod.inspect_package(values[0],metadata),'Check complete. No files were copied.',None))
            except Exception as exc:events.put((None,'Import/check stopped.',str(exc)))
        threading.Thread(target=work,daemon=True).start()
    check=ttk.Button(buttons,text='Check package',command=lambda:start(False));check.pack(side='left')
    copy=ttk.Button(buttons,text='Import into game',command=lambda:start(True),state='disabled');copy.pack(side='left',padx=10)
    def changed(*unused):
        if not busy:copy.configure(state='disabled')
    for value in [source,game,name]:value.trace_add('write',changed)
    def poll():
        nonlocal busy
        try:report,notice,error=events.get_nowait()
        except queue.Empty:root.after(100,poll);return
        busy=False
        for widget in controls:widget.configure(state='normal')
        check.configure(state='normal');status.set(notice)
        if error:display(error)
        else:
            summary=report['summary'];display('; '.join(f'{v} {k}'for k,v in summary.items())+'\n\n'+'\n'.join(f"{r['status'].upper()} {r['source']}: {r['reason']}"for r in report['files'])+'\n\nRecognition is not successful runtime decoding or gameplay verification.')
            if not summary['blocked'] and summary['recognized']:copy.configure(state='normal')
        root.after(100,poll)
    def close():
        if busy:messagebox.showinfo('Import in progress','Wait for the current check/import to finish before closing.',parent=root)
        else:root.destroy()
    root.protocol('WM_DELETE_WINDOW',close);frame.columnconfigure(1,weight=1);frame.rowconfigure(7,weight=1)
    root.after(100,poll)
    if args.smoke_report:
        def ready():
            with args.smoke_report.open('x',encoding='utf-8')as f:json.dump({'window_ready':True,'tk':root.tk.call('info','patchlevel'),'metadata_loaded':True},f)
            if args.smoke_close:root.after(200,root.destroy)
        root.after_idle(ready)
    root.mainloop();return 0


if __name__=='__main__':raise SystemExit(main())
