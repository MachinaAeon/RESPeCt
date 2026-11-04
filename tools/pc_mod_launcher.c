/* Portable importer entry point. No shell, PATH lookup, game launch or installer. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <wchar.h>
#include <stdio.h>

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR command,int show) {
    wchar_t base[32768],python[32768],script[32768],line[65536];
    DWORD n=GetModuleFileNameW(NULL,base,32768);
    if(!n || n>=32768) return 1;
    wchar_t *slash=wcsrchr(base,L'\\');if(!slash)return 1;*slash=0;
    if(swprintf(python,32768,L"%ls\\runtime\\pythonw.exe",base)<0 ||
       swprintf(script,32768,L"%ls\\tools\\pc_mod_gui.py",base)<0 ||
       swprintf(line,65536,L"\"%ls\" -I \"%ls\"",python,script)<0)return 1;
    STARTUPINFOW startup={0};startup.cb=sizeof(startup);PROCESS_INFORMATION process;
    if(!CreateProcessW(python,line,NULL,NULL,FALSE,0,NULL,base,&startup,&process)) {
        MessageBoxW(NULL,L"Cannot start the bundled importer. Extract the complete folder, including runtime and tools. No Python installation is required.",L"RESPeCt Mod Installer",MB_OK|MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return 0;
}
