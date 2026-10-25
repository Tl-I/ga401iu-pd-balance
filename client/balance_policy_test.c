#include <assert.h>
#include <stdio.h>
#include "../shared/balance_policy.h"

int main(void)
{
    BALANCE_POLICY p = {0};
    assert(!BalancePolicyStep(&p, 1, 85, 840, 0));
    assert(!BalancePolicyStep(&p, 1, 85, 844, 0));
    assert(BalancePolicyStep(&p, 1, 85, 845, 0));
    assert(BalancePolicyStep(&p, 1, 85, 820, 0));
    assert(!BalancePolicyStep(&p, 1, 85, 800, 0));
    assert(!BalancePolicyStep(&p, 1, 85, 830, 0));
    assert(BalancePolicyStep(&p, 1, 85, 845, 0));
    assert(!BalancePolicyStep(&p, 0, 85, 845, 0));
    assert(!BalancePolicyStep(&p, 1, 85, 845, 0));
    assert(!BalancePolicyStep(&p, 1, 85, 844, 0));
    assert(BalancePolicyStep(&p, 1, 85, 845, 0));
    assert(!BalancePolicyStep(&p, 1, 85, 845, 1));
    assert(!BalancePolicyStep(&p, 1, 85, 846, 0));
    assert(!BalancePolicyStep(&p, 1, 85, 840, 0));
    assert(BalancePolicyStep(&p, 1, 85, 845, 0));
    assert(!BalancePolicyStep(&p, 1, 90, 845, 0));
    assert(BalancePolicyStep(&p, 1, 90, 895, 0));
    assert(!BalancePolicyStep(&p, 1, 90, 905, 0));
    assert(!BalancePolicyStep(&p, 1, 10, 100, 0));
    /* Startup while already at the target; communication loss must not mutate p. */
    p = (BALANCE_POLICY){0};
    assert(BalancePolicyStep(&p, 1, 85, 850, 0));
    for (int i = 0; i < 5; ++i) assert(p.hold);
    assert(BalancePolicyStep(&p, 1, 85, 830, 0));
    assert(BalanceNextCurrent(96, 0) == 96);
    assert(BalanceNextCurrent(96, 8) == 96);
    assert(BalanceNextCurrent(96, 24) == 84);
    assert(BalanceNextCurrent(96, -24) == 108);
    assert(BalanceNextCurrent(4, 32767) == 4);
    assert(BalanceNextCurrent(2160, -32768) == 2160);
    for (int c = 0; c <= 2160; c += 4)
        for (int b = -3000; b <= 3000; b += 37) {
            int n = BalanceNextCurrent(c, b);
            assert(n >= 4 && n <= 2160 && (n & 3) == 0);
        }
    puts("PASS: thresholds, hysteresis, reconnect, recharge, target changes, fault latch and current bounds");
    return 0;
}
