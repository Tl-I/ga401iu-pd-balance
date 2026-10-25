#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

/* This utility only signals the running controller. It never opens a driver. */
int wmain(int argc, wchar_t** argv)
{
    if (argc != 2 || (wcscmp(argv[1], L"--pd-balance-stop") && wcscmp(argv[1], L"--pd-recharge"))) {
        fputs("Use --pd-balance-stop or --pd-recharge\n", stderr);
        return 2;
    }
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE,
        wcscmp(argv[1], L"--pd-recharge") == 0 ? L"Local\\Ga401PdRecharge" : L"Local\\Ga401PdBalanceStop");
    if (!event) { fputs("No running PD balance session.\n", stderr); return 1; }
    BOOL ok = SetEvent(event);
    CloseHandle(event);
    return ok ? 0 : 1;
}
