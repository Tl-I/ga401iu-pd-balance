#pragma once
/* GA401IU.222 AsusEcDxeSmm FUN_1bc0 calls FUN_1ae8(600,599)
 * before FF-prefixed transactions: consume pending output first.
 * Recovery only, under the transport mutex. No command/charger writes.
 * Bound each attempt to four bytes; never drain while input is busy.
 * This mutex cannot exclude independent firmware/SMM transactions. */
static NTSTATUS BalanceReadyLocked(void)
{
    for (unsigned int n = 0; n <= 4u; ++n) {
        UCHAR ready = READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)0x258);
        if (ready == 0xFFu) return STATUS_DEVICE_NOT_CONNECTED;
        if (ready & 2u) return STATUS_DEVICE_BUSY;
        if (!(ready & 1u)) return STATUS_SUCCESS;
        if (n == 4u) break;
        (void)READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)0x257);
    }
    return STATUS_DEVICE_BUSY;
}
