#define WIN32_LEAN_AND_MEAN
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
typedef LONG NTSTATUS;
#include <ntstatus.h>
#include <winioctl.h>
#include <stdio.h>
#include "../shared/ga401ec_ioctl.h"
#include "../shared/balance_journal.h"

/* Direct checked IOCTLs: the upstream DLL discards I/O errors. Do not use it. */
static HANDLE g_Io = INVALID_HANDLE_VALUE;
static DWORD g_IoError;
static HANDLE g_IsaMutex;
static int g_InjectRecovery, g_Injected;
static ULONGLONG g_RunStarted;
static void* g_BalanceOwner;
static USHORT g_BalanceOriginal, g_BalanceLast;
static HANDLE g_JournalFile = INVALID_HANDLE_VALUE;
static DWORD g_JournalSequence;
static DWORD g_JournalTarget;
static BOOL JournalSave(USHORT planned) {
    BALANCE_JOURNAL j = {0};
    LARGE_INTEGER offset;
    DWORD written;
    if (g_JournalFile == INVALID_HANDLE_VALUE) return FALSE;
    j.magic = 0x324A5047u; j.sequence = ++g_JournalSequence;
    j.original = g_BalanceOriginal; j.last = g_BalanceLast; j.planned = planned; j.target = g_JournalTarget;
    j.checksum = JournalChecksum(&j);
    offset.QuadPart = (LONGLONG)(j.sequence & 1u) * sizeof(j);
    return SetFilePointerEx(g_JournalFile, offset, NULL, FILE_BEGIN) &&
        WriteFile(g_JournalFile, &j, sizeof(j), &written, NULL) && written == sizeof(j) && FlushFileBuffers(g_JournalFile);
}
typedef void* PFILE_OBJECT;
#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif
#define GA401EC_PAGE_PORT ((PUCHAR)(ULONG_PTR)0x381)
#define GA401EC_INDEX_PORT ((PUCHAR)(ULONG_PTR)0x382)
#define GA401EC_DATA_PORT ((PUCHAR)(ULONG_PTR)0x383)

static int AllowedPort(USHORT port)
{
    return port == 0x257 || port == 0x258 || (port >= 0x381 && port <= 0x383);
}
static UCHAR SignedRead(PUCHAR address)
{
    USHORT port = (USHORT)(ULONG_PTR)address;
    BYTE data[3] = {(BYTE)port, (BYTE)(port >> 8), 0};
    DWORD bytes = 0;
    if (g_IoError) return 0xFF;
    if (!AllowedPort(port)) { g_IoError = ERROR_ACCESS_DENIED; return 0xFF; }
    if (!DeviceIoControl(g_Io, (DWORD)CTL_CODE(40000u, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS),
                         data, sizeof(data), data, sizeof(data), &bytes, NULL)) {
        g_IoError = GetLastError(); return 0xFF;
    }
    if (bytes < 1) { g_IoError = ERROR_INVALID_DATA; return 0xFF; }
    return data[0];
}
static void SignedWrite(PUCHAR address, UCHAR value)
{
    USHORT port = (USHORT)(ULONG_PTR)address;
    BYTE data[3] = {(BYTE)port, (BYTE)(port >> 8), value};
    DWORD bytes = 0;
    if (g_IoError) return;
    /* Indexed data writes are never required by the balance backend. */
    if (!AllowedPort(port) || port == 0x383) { g_IoError = ERROR_ACCESS_DENIED; return; }
    if (!DeviceIoControl(g_Io, (DWORD)CTL_CODE(40000u, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS),
                         data, sizeof(data), NULL, 0, &bytes, NULL))
        g_IoError = GetLastError();
}
static void SignedStall(ULONG microseconds)
{
    LARGE_INTEGER begin, now, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    do { YieldProcessor(); QueryPerformanceCounter(&now); }
    while ((now.QuadPart - begin.QuadPart) * 1000000 < (LONGLONG)microseconds * frequency.QuadPart);
}
#define READ_PORT_UCHAR SignedRead
#define WRITE_PORT_UCHAR SignedWrite
#define KeStallExecutionProcessor SignedStall
#include "../shared/balance_transport.h"

static BOOL WINAPI SignedBalanceIoctl(HANDLE h, DWORD code, LPVOID input, DWORD inputSize,
    LPVOID output, DWORD outputSize, LPDWORD returned, LPOVERLAPPED overlapped)
{
    NTSTATUS status;
    void* ownerBefore = g_BalanceOwner;
    (void)h; (void)overlapped;
    *returned = 0;
    if (code != IOCTL_GA401EC_BALANCE || inputSize != sizeof(GA401EC_BALANCE) ||
        outputSize != sizeof(GA401EC_BALANCE) || input != output) {
        SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
    }
    g_IoError = 0;
    if (g_InjectRecovery && !g_Injected && ((GA401EC_BALANCE*)output)->Action == 0 &&
        GetTickCount64() - g_RunStarted >= 10000u) {
        /* Software-only fault injection. No partial hardware exchange is issued. */
        g_Injected = 1;
        fputs("TEST: injecting one client timeout without accessing EC\n", stderr);
        SetLastError(ERROR_SEM_TIMEOUT); return FALSE;
    }
    DWORD wait = WaitForSingleObject(g_IsaMutex, 1000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        SetLastError(ERROR_BUSY); return FALSE;
    }
    GA401EC_BALANCE* request = (GA401EC_BALANCE*)output;
    /* Persist both the prior and intended value BEFORE a charger write. A crash
       can leave either value behind. Alternating slots retain the previous record. */
    if (request->Action == 2u && (!g_BalanceOwner || !JournalSave(request->Requested))) {
        ReleaseMutex(g_IsaMutex); SetLastError(ERROR_WRITE_FAULT); return FALSE;
    }
    status = BalanceLocked((PFILE_OBJECT)1, request);
    if (NT_SUCCESS(status) && request->Action == 1u && g_JournalFile != INVALID_HANDLE_VALUE) {
        g_JournalTarget = request->TargetPercent;
        if (!JournalSave(g_BalanceLast)) {g_BalanceOwner = NULL; status = STATUS_UNSUCCESSFUL;} // Acquire itself did not write hardware.
    }
    ReleaseMutex(g_IsaMutex);
    /* A cache I/O failure must not masquerade as disconnection/release. */
    if (g_IoError && ownerBefore) g_BalanceOwner = ownerBefore;
    if (g_IoError || !NT_SUCCESS(status)) {
        /* Preserve failure details without another command or draining response data. */
        UCHAR portStatus = g_IoError ? 0xFFu : SignedRead((PUCHAR)(ULONG_PTR)0x258);
        fprintf(stderr,"TRANSPORT action=%lu ntstatus=%08lX io=%lu status258=%02X output_ready=%u input_busy=%u owner=%u\n",
            request->Action,(unsigned long)status,g_IoError,portStatus,portStatus&1u,(portStatus>>1)&1u,g_BalanceOwner!=NULL);
        SetLastError(g_IoError ? g_IoError : status == STATUS_IO_TIMEOUT ? ERROR_SEM_TIMEOUT :
            status == STATUS_DEVICE_BUSY ? ERROR_BUSY : status == STATUS_DATA_ERROR ?
            ERROR_INVALID_DATA : ERROR_IO_DEVICE);
        return FALSE;
    }
    *returned = sizeof(GA401EC_BALANCE);
    return TRUE;
}
#define DeviceIoControl SignedBalanceIoctl
#include "balance_runtime.h"
#undef DeviceIoControl
#include "board_check.h"

static int RecoverPending(int legacy, USHORT expected)
{
    wchar_t path[MAX_PATH];
    BALANCE_JOURNAL records[2] = {0}, saved = {0};
    GA401EC_BALANCE r;
    DWORD bytes = 0;
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!length || length + 9 >= MAX_PATH) return 27;
    wcscat_s(path, MAX_PATH, L".pending");
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 27;
    BOOL read = ReadFile(file, records, sizeof(records), &bytes, NULL);
    CloseHandle(file);
    if (!read) return 27;
    for (int i = 0; i < 2; ++i)
        if (bytes >= (DWORD)((i + 1) * sizeof(BALANCE_JOURNAL)) && JournalValid(&records[i]) && records[i].sequence > saved.sequence) saved = records[i];
    if (!BalanceCall(g_Io, &r, 5, 0)) return 29;
    if (!BalanceCall(g_Io, &r, 0, 0)) return 29;
    if (!JournalCurrentValid(r.EcCurrent)) return 28;
    if (r.Current != r.EcCurrent) {
        if (!(legacy ? r.Current == expected : JournalOwns(&saved, r.Current))) {
            fputs("RECOVERY BLOCKED: actual current is not owned by the saved session.\n", stderr); return 28;
        }
        if (!BalanceCall(g_Io, &r, 1, 0)) return 29;
        if (!(legacy ? r.Current == expected : JournalOwns(&saved, r.Current))) {
            g_BalanceOwner = NULL; // Read-only acquire; no charger write has occurred.
            fputs("RECOVERY BLOCKED: current changed before guarded release.\n", stderr); return 28;
        }
        if (!BalanceCall(g_Io, &r, 3, 0)) return 29;
    }
    if (!BalanceCall(g_Io, &r, 0, 0) || r.Current != r.EcCurrent || !JournalCurrentValid(r.EcCurrent)) return 29;
    printf("RECOVERY VERIFIED live=%04X ec=%04X source=%02X; pending session resolved\n", r.Current, r.EcCurrent, r.Source);
    if (JournalValid(&saved) && r.TargetPercent == saved.target) {
        wchar_t resume[MAX_PATH];
        wcscpy_s(resume, MAX_PATH, path); wcscpy_s(resume + length, MAX_PATH - length, L".resume");
        HANDLE next = CreateFileW(resume, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
        if(next == INVALID_HANDLE_VALUE)return 27;
        BOOL ok=WriteFile(next,&saved,sizeof(saved),&bytes,NULL) && bytes==sizeof(saved) && FlushFileBuffers(next);
        CloseHandle(next);if(!ok)return 27;
    }
    if (!DeleteFileW(path)) return 27;
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    HANDLE mutex;
    int result = 0;
    unsigned target, soc, source, voltage, current, status;
    BOOL admin = FALSE;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID admins = NULL;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins)) {
        CheckTokenMembership(NULL, admins, &admin); FreeSid(admins);
    }
    if (!admin || !CheckBoard()) return 2;
    int sample = argc == 2 && wcscmp(argv[1], L"--sample") == 0;
    int verifyIdle = argc == 2 && wcscmp(argv[1], L"--verify-idle") == 0;
    int recoveryTest = argc == 2 && wcscmp(argv[1], L"--recovery-test") == 0;
    int recoverPending = argc == 2 && wcscmp(argv[1], L"--recover-pending") == 0;
    int recoverLegacy = argc == 3 && wcscmp(argv[1], L"--recover-legacy") == 0;
    unsigned long legacyExpected = 0;
    if (recoverLegacy) {
        wchar_t* end;
        legacyExpected = wcstoul(argv[2], &end, 16);
        if (end == argv[2] || *end || !JournalCurrentValid(legacyExpected)) return 1;
    }
    int automatic = argc == 3 && wcscmp(argv[1], L"--pd-auto") == 0;
    unsigned long seconds = 0;
    if (automatic) {
        wchar_t* end;
        seconds = wcstoul(argv[2], &end, 10);
        if (end == argv[2] || *end || seconds > 3600) return 1;
    }
    if (!automatic && !recoverLegacy && (argc != 2 || (!sample && !verifyIdle && !recoveryTest && !recoverPending && wcscmp(argv[1], L"--check") != 0))) {
        fputs("Use --check, --sample, --pd-auto seconds, or --recovery-test (60s).\n", stderr);
        return 1;
    }
    HANDLE old = CreateFileW(L"\\\\.\\Ga401Ec", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (old != INVALID_HANDLE_VALUE) { CloseHandle(old); fputs("Old driver is loaded; stop it first.\n", stderr); return 3; }
    mutex = CreateMutexW(NULL, FALSE, L"Global\\Ga401PdController");
    if (!mutex) return 3;
    DWORD wait = WaitForSingleObject(mutex, 1000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) { CloseHandle(mutex); return 3; }
    g_IsaMutex = CreateMutexW(NULL, FALSE, L"Global\\Access_ISABUS.HTP.Method");
    if (!g_IsaMutex) { ReleaseMutex(mutex); CloseHandle(mutex); return 3; }
    g_Io = CreateFileW(L"\\\\.\\inpoutx64", GENERIC_READ | GENERIC_WRITE, 0, NULL,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_Io == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Signed driver open failed: %lu\n", GetLastError()); result = 3;
    } else {
        DWORD cacheWait = WaitForSingleObject(g_IsaMutex, 1000);
        if (cacheWait != WAIT_OBJECT_0 && cacheWait != WAIT_ABANDONED) g_IoError = ERROR_BUSY;
        target = EcReadXdataByteLocked(0x0570);
        soc = EcReadXdataByteLocked(0x0520);
        source = EcReadXdataByteLocked(0x05AA);
        voltage = EcReadXdataWordLocked(0x0BCA);
        current = EcReadXdataWordLocked(0x0BC8);
        status = SignedRead((PUCHAR)(ULONG_PTR)0x258);
        if (cacheWait == WAIT_OBJECT_0 || cacheWait == WAIT_ABANDONED) ReleaseMutex(g_IsaMutex);
        if (g_IoError || target > 100 || soc > 100 || (source & ~7u)) {
            fprintf(stderr, "Cache check failed: win32=%lu target=%u soc=%u source=%02X\n", g_IoError, target, soc, source);
            result = 4;
        } else printf("SIGNED-IO OK target=%u%% soc=%u%% source=%02X contract=%umV/%umA vendor_status=%02X; no charger writes or SMBus commands\n",
                      target, soc, source, voltage, current, status);
        if (!result && (sample || (verifyIdle && (source & 3u) == 2u))) {
            GA401EC_BALANCE r;
            ULONGLONG start = GetTickCount64();
            if (!BalanceCall(g_Io, &r, 0, 0)) result = 5;
            else printf("SAMPLE current=%04X voltage=%umV battery=%dmA remaining=%u/%u soc=%u.%u%% control1=%04X control2=%04X info2=%04X elapsed=%llums; no charger writes\n",
                r.Current, r.BatteryVoltage, r.BatteryCurrent, r.BatteryRemaining, r.BatteryFull,
                r.SocTenths / 10, r.SocTenths % 10, r.Control1, r.Control2, r.Information2, GetTickCount64() - start);
            if (!result && verifyIdle && (r.Current != r.EcCurrent || r.EcCurrent > 0x0870u || (r.EcCurrent & 3u))) {
                fputs("Idle verification failed: charger differs from EC request.\n", stderr);
                result = 28;
            }
        }
        if (!result && verifyIdle && (source & 3u) != 2u) {
            fputs("Idle verification deferred: PD absent; previous session remains unresolved.\n", stderr);
            result = 29;
        }
        if (!result && verifyIdle) puts("IDLE VERIFIED: guarded charger current equals EC request; no charger writes");
        if (!result && (recoverPending || recoverLegacy)) {
            if ((source & 3u) != 2u) result = 29;
            else result = RecoverPending(recoverLegacy, (USHORT)legacyExpected);
        }
        if (!result && (recoveryTest || automatic)) {
            wchar_t marker[MAX_PATH];
            DWORD length = GetModuleFileNameW(NULL, marker, MAX_PATH);
            HANDLE pending = INVALID_HANDLE_VALUE;
            if (length && length + 9 < MAX_PATH) {
                wcscat_s(marker, MAX_PATH, L".pending");
                pending = CreateFileW(marker, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
            }
            if (pending == INVALID_HANDLE_VALUE) {
                fputs("Refusing start: previous session unresolved or pending marker cannot be created.\n", stderr);
                result = 27;
            } else {
                const char note[] = "Session may own charger current. Do not restart until restoration is verified.\n";
                DWORD written;
                if (!WriteFile(pending, note, sizeof(note)-1, &written, NULL) || written != sizeof(note)-1 || !FlushFileBuffers(pending)) result = 27;
                else {
                    g_JournalFile = pending;
                    g_JournalSequence = 0;
                    if(automatic) {
                        wchar_t resume[MAX_PATH]; BALANCE_JOURNAL prior={0}; DWORD readBytes=0;
                        wcscpy_s(resume,MAX_PATH,marker);wcscpy_s(resume+length,MAX_PATH-length,L".resume");
                        HANDLE previous=CreateFileW(resume,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
                        if(previous!=INVALID_HANDLE_VALUE){
                            if(ReadFile(previous,&prior,sizeof(prior),&readBytes,NULL) && readBytes==sizeof(prior) && JournalValid(&prior))g_ResumePolicyTarget=(int)prior.target;
                            CloseHandle(previous);
                            if(!DeleteFileW(resume)){g_ResumePolicyTarget=0;result=27;}
                        }
                    }
                    g_InjectRecovery = recoveryTest;
                    g_RunStarted = GetTickCount64();
                    if(!result)result = RunBalance(g_Io, recoveryTest ? 60 : (unsigned int)seconds, automatic);
                    if (g_BalanceOwner) {
                        fputs("Session retained for supervisor recovery; charger restoration not yet verified\n", stderr);
                        if (!result) result = 23;
                    }
                    if (recoveryTest && !g_Injected && !result) result = 26;
                }
                CloseHandle(pending);
                g_JournalFile = INVALID_HANDLE_VALUE;
                if (!g_BalanceOwner && result != 25 && !DeleteFileW(marker)) result = 27;
            }
        }
        CloseHandle(g_Io);
    }
    CloseHandle(g_IsaMutex); ReleaseMutex(mutex); CloseHandle(mutex);
    return result;
}
