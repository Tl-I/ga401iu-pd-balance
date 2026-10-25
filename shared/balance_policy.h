#pragma once

/* Pure state logic. SOC is remaining/full capacity, in tenths of a percent. */
typedef struct {
    int seen;
    int connected;
    int target;
    int armed;
    int hold;
} BALANCE_POLICY;

static int BalancePolicyStep(BALANCE_POLICY* p, int connected, int target,
                             int soc, int recharge)
{
    int first = !p->seen;
    int trigger = target * 10 - 5;
    p->seen = 1;
    if (!connected || target < 25 || target > 100) {
        p->connected = 0;
        p->armed = p->hold = 0;
        p->target = target;
        return 0;
    }
    if (!p->connected || p->target != target) {
        p->hold = 0;
        p->armed = first || soc < trigger;
    }
    p->connected = 1;
    p->target = target;
    if (recharge) {
        p->hold = p->armed = 0;
        return 0;
    }
    if (p->hold && (soc <= target * 10 - 50 || soc >= target * 10 + 5)) {
        p->hold = 0;
        p->armed = soc < trigger;
        return 0;
    }
    if (!p->hold) {
        if (soc < trigger) p->armed = 1;
        if (p->armed && soc >= trigger) p->hold = 1;
    }
    return p->hold;
}

static int BalanceNextCurrent(int current, int batteryMa)
{
    int delta = (batteryMa > 8 || batteryMa < -8) ? -batteryMa / 2 : 0;
    int next;
    if (delta > 512) delta = 512;
    if (delta < -512) delta = -512;
    next = current + delta;
    if (next < 4) next = 4;
    if (next > 2160) next = 2160;
    return (next + 2) & ~3;
}
