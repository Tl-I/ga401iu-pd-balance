#pragma once
/* Shared GA401IU transport; platform supplies checked port I/O and session state.
 * Port ordering and charger restrictions are shared by both backends.
 * Neither backend serializes independent firmware/SMM accesses. */

static UCHAR
EcReadXdataByteLocked(_In_ USHORT Address)
{
    UCHAR value;

    WRITE_PORT_UCHAR(GA401EC_PAGE_PORT,
                     (UCHAR)(0xC0u | ((Address >> 8) & 0x3Fu)));
    WRITE_PORT_UCHAR(GA401EC_INDEX_PORT, (UCHAR)(Address & 0xFFu));
    value = READ_PORT_UCHAR(GA401EC_DATA_PORT);
    return value;
}

static USHORT
EcReadXdataWordLocked(_In_ USHORT Address)
{
    UCHAR lo = EcReadXdataByteLocked(Address);
    UCHAR hi = EcReadXdataByteLocked((USHORT)(Address + 1u));
    return (USHORT)(lo | ((USHORT)hi << 8));
}

static BOOLEAN
IsAllowedChargerRegister(_In_ UCHAR Register)
{
    switch (Register) {
    case 0x14: /* ChargeCurrentLimit */
    case 0x15: /* MaxSystemVoltage */
    case 0x39: /* Control0 */
    case 0x3A: /* Information1 */
    case 0x3C: /* Control1 */
    case 0x3D: /* Control2 */
    case 0x3E: /* MinSystemVoltage */
    case 0x3F: /* AdapterCurrentLimit1 */
    case 0x4C: /* Control3 */
    case 0x4D: /* Information */
    case 0xFE: /* Manufacturer ID */
    case 0xFF: /* Device ID */
        return TRUE;
    default:
        return FALSE;
    }
}

static NTSTATUS
VendorWaitLocked(_In_ UCHAR SetMask)
{
    ULONG i;
    for (i = 0; i < 1000u; ++i) {
        UCHAR s = READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)0x258);
        if (s == 0xFFu) return STATUS_DEVICE_NOT_CONNECTED;
        if ((s & 2u) == 0u && (s & 1u) == SetMask)
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(10u);
    }
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
VendorReadPairLocked(_In_ UCHAR Index, _In_ UCHAR Value)
{
    static const USHORT ports[] = {0x258, 0x258, 0x257, 0x257};
    UCHAR bytes[] = {0xFF, 0xAB, Index, Value};
    ULONG i;
    NTSTATUS status;
    /* Only stage a read-word request to this board's PD charger. */
    if (!((Index == 5u && (Value == 8u || Value == 3u)) ||
          (Index == 6u && Value == 9u) ||
          (Index == 8u && (Value == 0x12u || Value == 0x16u)) ||
          (Index == 9u && (IsAllowedChargerRegister(Value) || Value == 0x12u || Value == 0x3Bu || Value == 9u || Value == 10u || Value == 15u || Value == 16u)) ||
          (Index == 4u && (Value == 0u || Value == 0x10u))))
        return STATUS_INVALID_PARAMETER;
    for (i = 0; i < RTL_NUMBER_OF(ports); ++i) {
        status = VendorWaitLocked(0u);
        if (!NT_SUCCESS(status)) return status;
        WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)ports[i], bytes[i]);
    }
    return VendorWaitLocked(0u);
}

static NTSTATUS
VendorResultLocked(_In_ UCHAR Index, _Out_ PUCHAR Value)
{
    static const USHORT ports[] = {0x258, 0x258, 0x257, 0x257};
    UCHAR bytes[] = {0xFF, 0xDE, Index, 0};
    NTSTATUS status;
    ULONG i;
    if (Index != 3u && Index != 8u && Index != 9u)
        return STATUS_INVALID_PARAMETER;
    for (i = 0; i < RTL_NUMBER_OF(ports); ++i) {
        status = VendorWaitLocked(0u);
        if (!NT_SUCCESS(status)) return status;
        WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)ports[i], bytes[i]);
    }
    status = VendorWaitLocked(1u);
    if (!NT_SUCCESS(status)) return status;
    *Value = READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)0x257);
    return STATUS_SUCCESS;
}

static NTSTATUS
VendorReadWordLocked(_In_ UCHAR Battery, _In_ UCHAR Register,
                     _Out_ PUSHORT Value, _Out_ PUCHAR Completion)
{
    UCHAR pairs[][2] = {{5,8}, {6,9}, {8,0x12}, {9,Register}, {4,0x10}};
    UCHAR lo = 0, hi = 0;
    ULONG i;
    NTSTATUS status;
    if (Battery == 2u) {
        /* GA401IU EC14664: channel0/address12h is BQ24780S. */
        if (Register != 0x12u && Register != 0x14u && Register != 0x15u &&
            Register != 0x3Bu && Register != 0x3Cu && Register != 0x3Du &&
            Register != 0x3Fu && Register != 0xFEu && Register != 0xFFu)
            return STATUS_INVALID_PARAMETER;
        pairs[0][1] = 3u;
    } else if (Battery) {
        if (Register != 9u && Register != 10u && Register != 15u && Register != 16u) return STATUS_INVALID_PARAMETER;
        pairs[0][1] = 3u; /* EC map1acb4: selector3 -> channel0 */
        pairs[2][1] = 0x16u; /* EC142e8 battery wire address */
    } else if (!IsAllowedChargerRegister(Register)) {
        return STATUS_INVALID_PARAMETER;
    }
    *Value = 0;
    *Completion = 0;
    /* A no-operation request creates a fresh failure marker. Verify via
     * the EC response, avoiding the unvalidated upper indexed RAM window. */
    status = VendorReadPairLocked(4u, 0u);
    if (!NT_SUCCESS(status)) return status;
    status = VendorResultLocked(3u, Completion);
    if (!NT_SUCCESS(status)) return status;
    if (*Completion != 0x40u) return STATUS_DATA_ERROR;
    for (i = 0; i < RTL_NUMBER_OF(pairs); ++i) {
        status = VendorReadPairLocked(pairs[i][0], pairs[i][1]);
        if (!NT_SUCCESS(status)) return status;
    }
    status = VendorResultLocked(3u, Completion);
    if (!NT_SUCCESS(status)) return status;
    if (*Completion != 0x80u) return STATUS_IO_DEVICE_ERROR;
    status = VendorResultLocked(8u, &lo);
    if (!NT_SUCCESS(status)) return status;
    status = VendorResultLocked(9u, &hi);
    if (!NT_SUCCESS(status)) return status;
    *Value = (USHORT)(lo | ((USHORT)hi << 8));
    return STATUS_SUCCESS;
}

static NTSTATUS
VendorReadChargerLocked(_In_ UCHAR Register, _Out_ PUSHORT Value,
                        _Out_ PUCHAR Completion)
{
    return VendorReadWordLocked(FALSE, Register, Value, Completion);
}

static NTSTATUS
WriteIsl9238WordLocked(_In_ UCHAR Register, _In_ USHORT Value,
                       _Out_ PUCHAR TransactionStatus)
{
    UCHAR pairs[][2] =
        {{5,8}, {6,8}, {8,0x12}, {9,0x14}, {0x10,(UCHAR)Value}, {0x11,(UCHAR)(Value >> 8)}, {4,0x10}};
    static const USHORT ports[] = {0x258, 0x258, 0x257, 0x257};
    ULONG i, j;
    NTSTATUS status;
    *TransactionStatus = 0;
    /* Only the already-tested current range; no voltage or FET control writes. */
    if (Register != 0x14u || Value > 0x0870u || (Value & 3u)) return STATUS_NOT_SUPPORTED;
    status = VendorReadPairLocked(4u, 0u);
    if (!NT_SUCCESS(status)) return status;
    status = VendorResultLocked(3u, TransactionStatus);
    if (!NT_SUCCESS(status)) return status;
    if (*TransactionStatus != 0x40u) return STATUS_DATA_ERROR;
    for (i = 0; i < RTL_NUMBER_OF(pairs); ++i) {
        UCHAR bytes[] = {0xFF, 0xAB, pairs[i][0], pairs[i][1]};
        for (j = 0; j < RTL_NUMBER_OF(ports); ++j) {
            status = VendorWaitLocked(0u);
            if (!NT_SUCCESS(status)) return status;
            WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)ports[j], bytes[j]);
        }
    }
    status = VendorResultLocked(3u, TransactionStatus);
    if (!NT_SUCCESS(status)) return status;
    return *TransactionStatus == 0x80u ? STATUS_SUCCESS : STATUS_IO_DEVICE_ERROR;
}

#include "balance_release.h"
#include "balance_ready.h"

static NTSTATUS
BalanceLocked(_In_ PFILE_OBJECT Owner, _Inout_ GA401EC_BALANCE* r)
{
    NTSTATUS status;
    USHORT voltage, control3;
    if (r->Version != 2u || r->Action > 5u) return STATUS_INVALID_PARAMETER;
    if (g_BalanceOwner && g_BalanceOwner != Owner) return STATUS_DEVICE_BUSY;
    if (r->Action == 5u) {
        r->Active = g_BalanceOwner != NULL;
        r->Source = EcReadXdataByteLocked(0x05AA);
        if ((r->Source & 3u) != 2u) return STATUS_DEVICE_NOT_READY;
        return BalanceReadyLocked();
    }
    if (r->Action == 3u) {
        status = BalanceReleaseLocked();
        r->Active = g_BalanceOwner != NULL;
        r->Original = g_BalanceOriginal;
        r->Source = EcReadXdataByteLocked(0x05AA);
        r->EcCurrent = EcReadXdataWordLocked(0x0564);
        r->Current = 0xFFFFu;
        if (NT_SUCCESS(status) && (r->Source & 3u) == 2u)
            status = VendorReadChargerLocked(0x14, &r->Current, &r->Completion);
        return status;
    }
    r->Source = EcReadXdataByteLocked(0x05AA);
    r->BatteryPercent = EcReadXdataByteLocked(0x0520);
    r->EcCurrent = EcReadXdataWordLocked(0x0564);
    r->TargetPercent = EcReadXdataByteLocked(0x0570);
    r->ContractVoltage = EcReadXdataWordLocked(0x0BCA);
    r->ContractCurrent = EcReadXdataWordLocked(0x0BC8);
    r->Eligible = (r->Source & 3u) == 2u && r->BatteryPercent >= 20u &&
        r->BatteryPercent <= 100u && r->ContractVoltage == 20000u && r->ContractCurrent >= 2800u;
    r->Active = g_BalanceOwner == Owner;
    if (!r->Eligible) return r->Action == 4u ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
    status = VendorReadWordLocked(TRUE, 15, &r->BatteryRemaining, &r->Completion);
    if (!NT_SUCCESS(status)) return status;
    status = VendorReadWordLocked(TRUE, 16, &r->BatteryFull, &r->Completion);
    if (!NT_SUCCESS(status)) return status;
    if (!r->BatteryFull || r->BatteryRemaining > r->BatteryFull)
        return STATUS_DATA_ERROR;
    r->SocTenths = (USHORT)(((ULONG)r->BatteryRemaining * 1000u) / r->BatteryFull);
    if (r->Action == 4u) return STATUS_SUCCESS;
    status = VendorReadChargerLocked(0x3C, &r->Control1, &r->Completion);
    if (!NT_SUCCESS(status)) return status;
    status = VendorReadChargerLocked(0x3D, &r->Control2, &r->Completion);
    if (!NT_SUCCESS(status)) return status;
    status = VendorReadChargerLocked(0x4D, &r->Information2, &r->Completion);
    if (!NT_SUCCESS(status)) return status;
    if ((r->Control1 & 0x1804u) || (r->Control2 & 1u) ||
        (r->Information2 & 0x5000u) != 0x4000u) return STATUS_DEVICE_NOT_READY;
    status = VendorReadChargerLocked(0x14, &r->Current, &r->Completion);
    if (!NT_SUCCESS(status)) return status;
    status = VendorReadWordLocked(TRUE, 9, &r->BatteryVoltage, &r->Completion);
    if (!NT_SUCCESS(status)) return status;
    status = VendorReadWordLocked(TRUE, 10, (PUSHORT)&r->BatteryCurrent, &r->Completion);
    if (!NT_SUCCESS(status)) return status;
    if (r->BatteryVoltage < 12000u || r->BatteryVoltage > 17400u) return STATUS_DEVICE_NOT_READY;
    if (r->Action == 1u) {
        if (g_BalanceOwner) return STATUS_DEVICE_BUSY;
        if (r->Current > 0x0870u || (r->Current & 3u)) return STATUS_DEVICE_NOT_READY;
        status = VendorReadChargerLocked(0xFE, &voltage, &r->Completion);
        if (!NT_SUCCESS(status)) return status;
        if (voltage != 0x0049u) return STATUS_DEVICE_NOT_READY;
        status = VendorReadChargerLocked(0xFF, &voltage, &r->Completion);
        if (!NT_SUCCESS(status)) return status;
        if (voltage != 0x000Cu) return STATUS_DEVICE_NOT_READY;
        status = VendorReadChargerLocked(0x15, &voltage, &r->Completion);
        if (!NT_SUCCESS(status)) return status;
        status = VendorReadChargerLocked(0x4C, &control3, &r->Completion);
        if (!NT_SUCCESS(status)) return status;
        if (voltage < 12000u || voltage > 0x43F0u || (control3 & 0x0400u))
            return STATUS_DEVICE_NOT_READY;
        g_BalanceOriginal = r->Current;
        g_BalanceLast = r->Current;
        g_BalanceOwner = Owner;
    } else if (r->Action == 2u) {
        if (g_BalanceOwner != Owner) return STATUS_ACCESS_DENIED;
        if (r->Requested > 0x0870u || (r->Requested & 3u)) return STATUS_INVALID_PARAMETER;
        /* Record before sending: cleanup covers an uncertain write completion. */
        g_BalanceLast = r->Requested;
        status = WriteIsl9238WordLocked(0x14, r->Requested, &r->Completion);
        if (!NT_SUCCESS(status)) return status;
        status = VendorReadChargerLocked(0x14, &r->Current, &r->Completion);
        if (!NT_SUCCESS(status)) return status;
        if (r->Current != r->Requested) return STATUS_DATA_ERROR;
    }
    r->Original = g_BalanceOriginal;
    r->Active = g_BalanceOwner != NULL;
    return STATUS_SUCCESS;
}
