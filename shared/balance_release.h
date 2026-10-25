#pragma once
/* Keep ownership until actual PD charger state is verified. A non-PD cache
 * observation is not evidence that our last hardware write was undone. */
static NTSTATUS
BalanceReleaseLocked(VOID)
{
    USHORT current = 0, restore;
    UCHAR completion;
    NTSTATUS status;
    if (!g_BalanceOwner) return STATUS_SUCCESS;
    UCHAR ready = READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)0x258);
    if (ready == 0xFFu || (ready & 3u)) return STATUS_DEVICE_BUSY;
    UCHAR source = EcReadXdataByteLocked(0x05AA);
    if (source & 0xF8u) return STATUS_DATA_ERROR;
    if ((source & 3u) != 2u) return STATUS_DEVICE_NOT_READY;
    status = VendorReadChargerLocked(0x14, &current, &completion);
    if (!NT_SUCCESS(status)) return status;
    if (current > 0x0870u || (current & 3u)) return STATUS_DATA_ERROR;
    if (current == g_BalanceLast) {
        restore = EcReadXdataWordLocked(0x0564);
        if (restore > 0x0870u || (restore & 3u)) return STATUS_DATA_ERROR;
        if ((EcReadXdataByteLocked(0x05AA) & 3u) != 2u) return STATUS_DEVICE_NOT_READY;
        status = WriteIsl9238WordLocked(0x14, restore, &completion);
        if (!NT_SUCCESS(status)) return status;
        status = VendorReadChargerLocked(0x14, &current, &completion);
        if (!NT_SUCCESS(status)) return status;
        if (current != restore) return STATUS_DATA_ERROR;
    }
    /* A verified, valid EC change takes precedence over our session snapshot. */
    if ((EcReadXdataByteLocked(0x05AA) & 3u) != 2u) return STATUS_DEVICE_NOT_READY;
    g_BalanceOwner = NULL;
    return STATUS_SUCCESS;
}
