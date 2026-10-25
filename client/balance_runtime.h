#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <winioctl.h>
#include "..\shared\ga401ec_ioctl.h"
#include "..\shared\balance_policy.h"

static volatile LONG g_StopBalance;
static HANDLE g_BalanceStopEvent;
static HANDLE g_RechargeEvent;
static int g_ResumePolicyTarget;
/* ISL9238B section 6.16: unchanged ChargeCurrent still needs a write before
 * the 175s SMBus charger timeout. Keep the hardware timeout enabled. */
#define BALANCE_CURRENT_REFRESH_MS 60000u
static BOOL WINAPI StopBalance(DWORD event)
{
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_StopBalance, 1);
        if (g_BalanceStopEvent) SetEvent(g_BalanceStopEvent);
        return TRUE;
    }
    return FALSE;
}

static int BalanceCall(HANDLE h, GA401EC_BALANCE* r, unsigned long action, unsigned short requested)
{
    DWORD returned;
    /* No immediate retries: a failed EC exchange may have left a partial command. */
    for (int attempt = 0; attempt < 1; ++attempt) {
        ZeroMemory(r, sizeof(*r));
        r->Version = 2;
        r->Action = action;
        r->Requested = requested;
        r->Source = 0xFFu; // Unknown until the transport supplies a valid observation.
        if (DeviceIoControl(h, IOCTL_GA401EC_BALANCE, r, sizeof(*r), r, sizeof(*r), &returned, NULL))
            return returned == sizeof(*r) && r->Version == 2;
        DWORD error = GetLastError();
        fprintf(stderr, "Balance action %lu attempt %d failed: win32=%lu\n", action, attempt + 1, error);
        fflush(stderr);
    }
    return 0;
}

static void BalanceTimestamp(void)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    printf("%04u-%02u-%02u %02u:%02u:%02u ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

static int RunBalance(HANDLE h, unsigned int seconds, int automatic)
{
    GA401EC_BALANCE r = {0}, monitor;
    BALANCE_POLICY policy = {0};
    ULONGLONG started, lastTick, lastLog = 0, lastWriteTick = 0;
    ULONGLONG recoveryStarted = 0, recoveryNext = 0;
    int result = 0, acquired = 0, recovering = 0, faults = 0, previousEc = -1, lastWanted = -1;
    g_BalanceStopEvent = CreateEventW(NULL, TRUE, FALSE, L"Local\\Ga401PdBalanceStop");
    if (!g_BalanceStopEvent) return 24;
    g_RechargeEvent = CreateEventW(NULL, FALSE, FALSE, L"Local\\Ga401PdRecharge");
    if (!g_RechargeEvent) { CloseHandle(g_BalanceStopEvent); g_BalanceStopEvent = NULL; return 24; }
    ResetEvent(g_BalanceStopEvent);
    ResetEvent(g_RechargeEvent);
    SetConsoleCtrlHandler(StopBalance, TRUE);
    lastTick = started = GetTickCount64();
    BalanceTimestamp(); printf("START mode=%s; threshold=target-0.5%%, release=target-5%%\n", automatic ? "AUTO" : "FORCED-TEST");
    while (!g_StopBalance && (seconds == 0 || GetTickCount64() - started < (ULONGLONG)seconds * 1000u)) {
        ULONGLONG now = GetTickCount64();
        int recharge, wanted, fresh = 0, next, recoveredSample = 0, monitorOk = 1;
        if (recovering) {
            lastTick = now;
            if (now - recoveryStarted >= 120000u && !(automatic && seconds == 0)) {
                result = 25;
                BalanceTimestamp(); puts("FAULT: recovery deadline exceeded; charger restoration UNCONFIRMED; no automatic restart");
                break;
            }
            if (now < recoveryNext) goto Pause;
            recoveryNext = now + 2000u;
            if (!BalanceCall(h, &r, 5, 0)) {
                if (automatic && seconds == 0 && r.Source <= 7u && (r.Source & 3u) != 2u) {
                    BalanceTimestamp(); puts("NON_PD during recovery: supervisor event wait; session retained");
                    result = 31; break;
                }
                goto Pause;
            }
            if (acquired) {
                if (!BalanceCall(h, &r, 0, 0)) goto Pause;
                recoveredSample = 1;
            }
            recovering = faults = 0;
            BalanceTimestamp(); puts("RECOVERY: communication ready; continue live feedback without release/reacquire");
        }
        if (now - lastTick > 10000u) {
            BalancePolicyStep(&policy, 0, 0, 0, 0);
            if (acquired) {
                if (!BalanceCall(h, &r, 3, 0)) goto FailedSample;
                acquired = 0;
            }
            previousEc = -1;
            BalanceTimestamp(); puts("RESUME: waiting for a new threshold crossing");
        }
        lastTick = now;
        if (recoveredSample) monitor=r;
        /* While holding, one complete sample supplies both policy and feedback.
         * Before acquisition, retain the lighter, read-only policy sample. */
        else monitorOk = BalanceCall(h, &monitor, acquired ? 0u : 4u, 0);
        if (automatic && seconds == 0 && monitor.Source <= 7u && (monitor.Source & 3u) != 2u) {
            BalanceTimestamp(); puts("NON_PD: hand off to supervisor power-event wait; retain owned session");
            result = 31; break;
        }
        if (!monitorOk) goto FailedSample;
        if (automatic && g_ResumePolicyTarget) {
            if (monitor.Eligible && monitor.TargetPercent == g_ResumePolicyTarget && monitor.EcCurrent == 0 &&
                monitor.SocTenths > g_ResumePolicyTarget * 10 - 50 && monitor.SocTenths < g_ResumePolicyTarget * 10 + 5) {
                policy.seen=policy.connected=policy.armed=policy.hold=1;policy.target=g_ResumePolicyTarget;
                BalanceTimestamp();puts("RECOVERY RESUME: continuing verified prior hold within its original target band");
            }
            g_ResumePolicyTarget=0; // One recovered session, never a later reconnect.
        }
        recharge = WaitForSingleObject(g_RechargeEvent, 0) == WAIT_OBJECT_0;
        if (policy.hold && previousEc == 0 && monitor.EcCurrent > 0) recharge = 1;
        previousEc = monitor.Eligible ? monitor.EcCurrent : -1;
        wanted = automatic ? BalancePolicyStep(&policy, monitor.Eligible, monitor.TargetPercent,
                                                monitor.SocTenths, recharge) : monitor.Eligible;
        if (recharge) { BalanceTimestamp(); puts("RECHARGE requested: releasing until next threshold crossing"); }
        if (wanted != lastWanted || now - lastLog >= 30000u) {
            BalanceTimestamp();
            printf("%s soc=%u.%u%% target=%u%% source=%02X contract=%umV/%umA armed=%d\n",
                   wanted ? "HOLD" : "WAIT", monitor.SocTenths / 10, monitor.SocTenths % 10,
                   monitor.TargetPercent, monitor.Source, monitor.ContractVoltage, monitor.ContractCurrent, policy.armed);
            lastWanted = wanted;
            lastLog = now;
        }
        if (!wanted) {
            if (acquired) {
                if (!BalanceCall(h, &r, 3, 0)) goto FailedSample;
                acquired = 0;
                BalanceTimestamp(); printf("RELEASE live=%04X ec=%04X source=%02X\n", r.Current, r.EcCurrent, r.Source);
            }
            faults = 0;
            goto Pause;
        }
        if (!acquired) {
            if (!BalanceCall(h, &r, 1, 0)) goto FailedSample;
            acquired = fresh = 1;
            BalanceTimestamp(); printf("ACQUIRE original=%04X battery=%dmA\n", r.Original, r.BatteryCurrent);
        } else r = monitor;
        BalanceTimestamp();
        printf("t=%llu current=%04X battery_mV=%u battery_mA=%d battery_W=%.3f info2=%04X ec=%04X remaining_raw=%u full_raw=%u\n",
               (now - started) / 1000u, r.Current, r.BatteryVoltage, r.BatteryCurrent,
               (double)r.BatteryVoltage * r.BatteryCurrent / 1000000.0,
               r.Information2, r.EcCurrent, r.BatteryRemaining, r.BatteryFull);
        /* Seed from this board's observed operating point, then use feedback. */
        next = fresh ? 96 : BalanceNextCurrent(r.Current, r.BatteryCurrent);
        if (fresh || next != r.Current || GetTickCount64() - lastWriteTick >= BALANCE_CURRENT_REFRESH_MS) {
            int refresh = !fresh && next == r.Current;
            ULONGLONG idle = GetTickCount64() - lastWriteTick;
            if (!BalanceCall(h, &r, 2, (unsigned short)next)) goto FailedSample;
            lastWriteTick = GetTickCount64();
            if (refresh) {
                BalanceTimestamp(); printf("REFRESH current=%04X idle_ms=%llu; charger readback verified\n", r.Current, idle);
            }
        }
        if (faults) { BalanceTimestamp(); puts("RECOVERED: native read/write verified"); }
        faults = 0;
        goto Pause;
FailedSample:
        ++faults;
        recovering = 1;
        recoveryStarted = GetTickCount64();
        recoveryNext = recoveryStarted + 2000u;
        BalanceTimestamp(); printf("RECOVERY fixed_interval=2s faults=%d; bounded pending-response recovery; no retry limit\n", faults);
Pause:
        fflush(stdout);
        if (WaitForSingleObject(g_BalanceStopEvent, 2000) == WAIT_OBJECT_0) break;
    }
    if (acquired && result != 25 && result != 31) {
        if (!BalanceCall(h, &r, 3, 0)) result = 23;
        else { BalanceTimestamp(); printf("STOP restored live=%04X ec=%04X source=%02X active=%u\n",
                                          r.Current, r.EcCurrent, r.Source, r.Active); }
    }
    BalanceTimestamp(); printf("STOP exit=%d\n", result); fflush(stdout);
    SetConsoleCtrlHandler(StopBalance, FALSE);
    CloseHandle(g_RechargeEvent);
    g_RechargeEvent = NULL;
    CloseHandle(g_BalanceStopEvent);
    g_BalanceStopEvent = NULL;
    return result;
}

