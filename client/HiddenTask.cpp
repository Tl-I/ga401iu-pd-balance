#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <string>
#pragma comment(lib,"shell32.lib")
// GUI subsystem launcher: never allocate a console, including before PowerShell starts.
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
 if(!argv||argc!=3){if(argv)LocalFree(argv);return 2;}
 std::wstring exe=argv[1],script=argv[2];LocalFree(argv);
 if(exe.find(L'"')!=std::wstring::npos||script.find(L'"')!=std::wstring::npos)return 2;
 std::wstring command=L"\""+exe+L"\" -NoProfile -NonInteractive -WindowStyle Hidden -File \""+script+L"\"";
 STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;
 PROCESS_INFORMATION process{};
 if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process))return (int)GetLastError();
 CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);
 DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);return (int)code;
}
