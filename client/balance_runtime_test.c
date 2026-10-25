#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <assert.h>
#include "../shared/ga401ec_ioctl.h"

static ULONGLONG fakeTime;
static ULONGLONG firstFailure, lastRecoveryProbe;
static int mode, injected, acquisitions, releases, writes, owned, events;
static int policySamples, fullSamples, chargerTimeouts;
static ULONGLONG lastWrite, maxWriteGap;
static unsigned short fakeCurrent;
static ULONGLONG WINAPI FakeTick(void) { return fakeTime; }
static HANDLE WINAPI FakeEvent(LPSECURITY_ATTRIBUTES a, BOOL b, BOOL c, LPCWSTR d)
{ (void)a; (void)b; (void)c; (void)d; return (HANDLE)(ULONG_PTR)++events; }
static BOOL WINAPI FakeReset(HANDLE h) { (void)h; return TRUE; }
static BOOL WINAPI FakeClose(HANDLE h) { (void)h; return TRUE; }
static BOOL WINAPI FakeHandler(PHANDLER_ROUTINE h, BOOL add) { (void)h; (void)add; return TRUE; }
static DWORD WINAPI FakeWait(HANDLE h, DWORD ms)
{ fakeTime += ms; return mode == 6 && h == (HANDLE)1 && fakeTime >= 240000 ? WAIT_OBJECT_0 : WAIT_TIMEOUT; }
static BOOL WINAPI FakeIoctl(HANDLE h, DWORD code, LPVOID in, DWORD inSize,
                             LPVOID out, DWORD outSize, LPDWORD returned, LPOVERLAPPED overlapped)
{
    GA401EC_BALANCE* r = out;
    (void)h; (void)in; (void)inSize; (void)outSize; (void)overlapped;
    assert(code == IOCTL_GA401EC_BALANCE);
    if(mode==10 && fakeTime>=4000 && (r->Action==0 || r->Action==5)) {
        if(r->Action==5)r->Source=1;
        injected++;SetLastError(ERROR_IO_DEVICE);return FALSE;
    }
    if (mode == 12 && !injected && fakeTime >= 60000 && r->Action == 2) {
        firstFailure = fakeTime;
        injected++;
        SetLastError(ERROR_SEM_TIMEOUT);
        return FALSE;
    }
    if (mode == 12 && injected && r->Action == 5) {
        assert(fakeTime - firstFailure >= 2000); /* No immediate replay after a failed refresh. */
        lastRecoveryProbe = fakeTime;
    }
    if (mode == 5 && fakeTime >= 4000 && fakeTime < 40000 &&
        (r->Action == 0 || r->Action == 3)) {
        injected++;
        SetLastError(ERROR_SEM_TIMEOUT);
        return FALSE;
    }
    if ((mode == 4 && fakeTime >= 4000) || (mode == 6 && fakeTime >= 4000 && fakeTime < 180000)) {
        if (!firstFailure) firstFailure = fakeTime;
        else {
            assert(r->Action == 5); /* No command or write during persistent outage. */
            assert(fakeTime - firstFailure >= 2000);
            if (lastRecoveryProbe) assert(fakeTime - lastRecoveryProbe == 2000);
            lastRecoveryProbe = fakeTime;
        }
        injected++;
        SetLastError(ERROR_SEM_TIMEOUT);
        return FALSE;
    }
    if ((mode == 1 && !injected && fakeTime >= 4000 && r->Action == 0) ||
        (mode == 2 && fakeTime >= 4000 && fakeTime < 12000 && r->Action == 0)) {
        injected++;
        SetLastError(ERROR_SEM_TIMEOUT);
        return FALSE;
    }
    r->Version = 2;
    r->Eligible = 1;
    r->Source = 2;
    r->TargetPercent = 85;
    r->SocTenths = 850;
    if(mode == 7 || mode == 8)r->SocTenths=840; // Below entry threshold, inside recovered hold band.
    if(mode == 9 && fakeTime >= 4000){
        r->Source=1;r->Eligible=0;
        if(r->Action==0){SetLastError(ERROR_NOT_READY);return FALSE;}
    }
    r->BatteryRemaining = 4250;
    r->BatteryFull = 5000;
    r->ContractVoltage = 20000;
    r->ContractCurrent = 3000;
    r->EcCurrent = 0;
    r->BatteryCurrent = 0;
    r->BatteryVoltage = 16000;
    r->Information2 = 0x4557;
    if (r->Action == 4) policySamples++;
    if (r->Action == 0) fullSamples++;
    if (mode == 11 && owned && fakeTime - lastWrite >= 175000) {
        /* Match the hardware: the register retains its value after timeout,
         * while charging stops. Reading it does not refresh the watchdog. */
        chargerTimeouts++;
        r->BatteryCurrent = -1600;
        r->Information2 = 0x4457;
    }
    if (mode == 3 && fakeTime >= 4000) {
        r->TargetPercent = 90;
        r->SocTenths = fakeTime < 12000 ? 850 : 895;
    }
    if (r->Action == 1) { assert(!owned); owned = 1; acquisitions++; }
    if (r->Action == 2) {
        assert(owned);
        assert(!(mode == 3 && fakeTime >= 4000 && fakeTime < 12000));
        fakeCurrent = r->Requested;
        if (writes && fakeTime - lastWrite > maxWriteGap) maxWriteGap = fakeTime - lastWrite;
        lastWrite = fakeTime;
        writes++;
    }
    if (r->Action == 3 && owned) { owned = 0; fakeCurrent = 0; releases++; }
    r->Active = (unsigned char)owned;
    r->Current = fakeCurrent;
    *returned = sizeof(*r);
    return TRUE;
}

#define GetTickCount64 FakeTick
#define CreateEventW FakeEvent
#define ResetEvent FakeReset
#define CloseHandle FakeClose
#define SetConsoleCtrlHandler FakeHandler
#define WaitForSingleObject FakeWait
#define DeviceIoControl FakeIoctl
#include "balance_runtime.h"

int main(void)
{
    for (mode = 1; mode <= 12; ++mode) {
        fakeTime = 0;
        firstFailure = lastRecoveryProbe = 0;
        injected = acquisitions = releases = writes = owned = events = 0;
        policySamples = fullSamples = chargerTimeouts = 0;
        lastWrite = maxWriteGap = 0;
        fakeCurrent = 0;
        g_ResumePolicyTarget=mode==7?85:mode==8?80:0;
        if (mode == 12) {
            assert(RunBalance((HANDLE)1, 130, 1) == 0);
            assert(!owned && acquisitions == 1 && releases == 1);
            assert(injected == 1 && lastRecoveryProbe == 62000);
            assert(writes == 3 && maxWriteGap == 62000);
            continue;
        }
        if (mode == 11) {
            assert(RunBalance((HANDLE)1, 600, 1) == 0);
            assert(!owned && acquisitions == 1 && releases == 1);
            assert(writes == 10 && maxWriteGap == 60000 && chargerTimeouts == 0);
            assert(policySamples == 1 && fullSamples == 299);
            continue;
        }
        if(mode==10){assert(RunBalance((HANDLE)1,0,1)==31);assert(owned && releases==0 && acquisitions==1 && fakeTime==6000);continue;}
        if(mode==9){assert(RunBalance((HANDLE)1,0,1)==31);assert(owned && acquisitions==1 && releases==0 && fakeTime==4000);continue;}
        if(mode>=7){
            assert(RunBalance((HANDLE)1,10,1)==0);
            assert(!owned);
            if(mode==7)assert(acquisitions==1 && writes>0 && releases==1);
            else assert(acquisitions==0 && writes==0);
            assert(g_ResumePolicyTarget==0);
            continue;
        }
        if (mode == 4) {
            assert(RunBalance((HANDLE)1, 140, 1) == 25);
            assert(owned && releases == 0 && acquisitions == 1 && writes == 1);
            assert(injected >= 50 && fakeTime <= 126000);
            continue;
        }
        if (mode == 6) {
            assert(RunBalance((HANDLE)1, 0, 1) == 0);
            assert(fakeTime >= 240000 && !owned && acquisitions == 1 && releases == 1);
            assert(injected >= 80);
            continue;
        }
        assert(RunBalance((HANDLE)1, mode == 5 ? 70 : 30, 1) == 0);
        assert(!owned && fakeCurrent == 0 && writes > 0);
        if (mode == 1) assert(injected == 1);
        if (mode == 5) assert(injected == 18); /* Live samples retry at fixed two seconds. */
        assert(acquisitions == (mode == 3 ? 2 : 1) && releases == (mode == 3 ? 2 : 1));
    }
    puts("PASS: fixed 2s recovery, target/reconnect policy, 60s verified refresh prevents simulated 175s charger timeout, one full sample per hold cycle; simulated transport only");
    return 0;
}
