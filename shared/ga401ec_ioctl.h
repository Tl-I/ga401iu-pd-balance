#pragma once

#define GA401EC_DEVICE_TYPE 0x8337u

#define IOCTL_GA401EC_GET_SNAPSHOT \
    CTL_CODE(GA401EC_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_GA401EC_READ_CHARGER \
    CTL_CODE(GA401EC_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_GA401EC_PROBE_SIO \
    CTL_CODE(GA401EC_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_GA401EC_ACPI_READ_TEST \
    CTL_CODE(GA401EC_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_GA401EC_ENABLE_PD_PASS_THROUGH_ONCE \
    CTL_CODE(GA401EC_DEVICE_TYPE, 0x804, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_GA401EC_READ_TPS_REGISTER \
    CTL_CODE(GA401EC_DEVICE_TYPE, 0x805, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_GA401EC_PROBE_INDEXED_WRITE \
    CTL_CODE(GA401EC_DEVICE_TYPE, 0x806, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_GA401EC_PROBE_NATIVE_SMBUS \
    CTL_CODE(GA401EC_DEVICE_TYPE, 0x807, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_GA401EC_SET_PD_CHARGE_ZERO_ONCE \
    CTL_CODE(GA401EC_DEVICE_TYPE, 0x808, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define GA401EC_SNAPSHOT_VERSION 1u

#define IOCTL_GA401EC_VENDOR_READ_PROBE \
    CTL_CODE(GA401EC_DEVICE_TYPE, 0x809, METHOD_BUFFERED, FILE_READ_ACCESS)
typedef struct _GA401EC_VENDOR_READ_PROBE {
    unsigned long Status;
    unsigned char Completed;
    unsigned char Indexed[6];
    unsigned char Vendor[6];
} GA401EC_VENDOR_READ_PROBE;

typedef struct _GA401EC_SNAPSHOT {
    unsigned long Version;
    unsigned long Size;
    unsigned char ChargeTargetPercent; /* XDATA 0x0570 */
    unsigned char Reserved0;
    unsigned short BatteryPercent;     /* XDATA 0x0520, EC cached SBS RSOC */
    unsigned char SourceFlags;         /* XDATA 0x05AA: bit0 barrel, bit1 PD */
    unsigned char PowerFlags;          /* XDATA 0x0580 */
    unsigned char ChargeState;         /* XDATA 0x0583 */
    unsigned char BatteryState;        /* XDATA 0x0584 */
    unsigned short PdChargeCurrent;    /* XDATA 0x0564, intended ISL9238 reg 14h */
    unsigned short PdMaxSystemVoltage; /* XDATA 0x0566, intended ISL9238 reg 15h */
    unsigned short PdCurrentTimer;     /* XDATA 0x0568 */
    unsigned short PdVoltageTimer;     /* XDATA 0x056A */
    unsigned short PdContractCurrent;  /* XDATA 0x0BC8, EC PD contract cache */
    unsigned short PdContractVoltage;  /* XDATA 0x0BCA, EC PD contract cache */
    unsigned short PdContractWatts;    /* XDATA 0x0BCC, EC calculated */
    unsigned char PdStatusFlags;       /* XDATA 0x0BC6 */
    unsigned char PdRefreshFlags;      /* XDATA 0x0BE6 */
    unsigned char PdRefreshCountdown;  /* XDATA 0x0BE7 */
    unsigned char Reserved1;
    unsigned char Smbus3Status;        /* live EC register XDATA 0x1C35 */
    unsigned char Smbus3Control;       /* live EC register XDATA 0x1C36 */
    unsigned char Smbus3Command;       /* live EC register XDATA 0x1C37 */
    unsigned char Smbus3Address;       /* live EC register XDATA 0x1C38 */
    unsigned char Smbus3Data0;         /* live EC register XDATA 0x1C39 */
    unsigned char Smbus3Data1;         /* live EC register XDATA 0x1C3A */
    unsigned char Smbus3Error;         /* live EC register XDATA 0x1C3B */
    unsigned char Smbus3Extra;         /* live EC register XDATA 0x1C3C */
} GA401EC_SNAPSHOT;

#define GA401EC_CHARGER_READ_VERSION 1u

typedef struct _GA401EC_CHARGER_READ {
    unsigned long Version;
    unsigned char Register;
    unsigned char TransactionStatus;
    unsigned short Value;
} GA401EC_CHARGER_READ;

#define GA401EC_PD_PASS_THROUGH_VERSION 1u

typedef struct _GA401EC_PD_PASS_THROUGH_RESULT {
    unsigned long Version;
    unsigned char TargetPercent;
    unsigned char BatteryPercent;
    unsigned char SourceFlags;
    unsigned char ChargeState;
    unsigned short PdContractCurrent;
    unsigned short PdContractVoltage;
    unsigned short ChargeCurrentBefore;
    unsigned short ChargeCurrentAfter;
    unsigned short Control2Before;
    unsigned short Control2Requested;
    unsigned short Control2After;
    unsigned short BatteryVoltage;
    unsigned short BatteryCurrentRaw;
    unsigned char ZeroCurrentWriteStatus;
    unsigned char PassThroughWriteStatus;
    unsigned char ReadbackTransactionStatus;
} GA401EC_PD_PASS_THROUGH_RESULT;

#define GA401EC_TPS_READ_VERSION 1u
#define GA401EC_TPS_MAX_DATA 16u

typedef struct _GA401EC_TPS_READ {
    unsigned long Version;
    unsigned char Register;
    unsigned char Length;
    unsigned char TransactionStatus;
    unsigned char Reserved;
    unsigned char Data[GA401EC_TPS_MAX_DATA];
} GA401EC_TPS_READ;

#define GA401EC_INDEXED_WRITE_PROBE_VERSION 1u

typedef struct _GA401EC_INDEXED_WRITE_PROBE {
    unsigned long Version;
    unsigned short Address;
    unsigned char Before;
    unsigned char After;
} GA401EC_INDEXED_WRITE_PROBE;

#define GA401EC_SIO_PROBE_VERSION 1u

typedef struct _GA401EC_SIO_PROBE {
    unsigned long Version;
    unsigned short ConfigPort;
    unsigned char ChipIdHigh;
    unsigned char ChipIdLow;
    unsigned char AltChipIdHigh;
    unsigned char AltChipIdLow;
    unsigned char ChargeTargetPercent;
    unsigned char BatteryPercent;
    unsigned char SourceFlags;
    unsigned char Reserved[3];
} GA401EC_SIO_PROBE;

#define GA401EC_ACPI_READ_TEST_VERSION 1u

typedef struct _GA401EC_ACPI_READ_TEST {
    unsigned long Version;
    unsigned long AcpiStatus;
    unsigned long MethodStatus;
    unsigned char IndexedTarget;   /* port 381/382/383, XDATA 0570 */
    unsigned char AcpiTarget;      /* EC0.RRAM(0xCC, 0x0570) */
    unsigned char AcpiSmbus3[8];   /* EC0.RRAM(0xCC, 0x1C35..1C3C) */
    unsigned char PmcTarget;        /* EC command 92h, address 0570h */
    unsigned char PmcSmbus3[8];     /* EC command 92h, 1C35h..1C3Ch */
    unsigned char Reserved;
    unsigned long PmcStatus;
} GA401EC_ACPI_READ_TEST;

#define GA401EC_NATIVE_SMBUS_PROBE_VERSION 1u

typedef struct _GA401EC_NATIVE_SMBUS_PROBE {
    unsigned long Version;
    unsigned long AcpiStatus;
    unsigned long StageStatus;
    unsigned long ReadStatus;
    unsigned char ResponseLength;
    unsigned char Response[32];
} GA401EC_NATIVE_SMBUS_PROBE;

#define IOCTL_GA401EC_RESTORE_PD_TEST_CURRENT CTL_CODE(GA401EC_DEVICE_TYPE, 0x80A, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_GA401EC_READ_BQ CTL_CODE(GA401EC_DEVICE_TYPE, 0x80B, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_GA401EC_BALANCE CTL_CODE(GA401EC_DEVICE_TYPE, 0x80C, METHOD_BUFFERED, FILE_WRITE_ACCESS)
typedef struct _GA401EC_BALANCE {
    unsigned long Version;
    unsigned long Action; /* 0: sample, 1: acquire, 2: set, 3: release, 4: monitor, 5: passive ready */
    unsigned short Requested;
    unsigned short Original;
    unsigned short Current;
    unsigned short BatteryVoltage;
    short BatteryCurrent;
    unsigned short BatteryRemaining;
    unsigned short BatteryFull;
    unsigned short SocTenths;
    unsigned short ContractVoltage;
    unsigned short ContractCurrent;
    unsigned char TargetPercent;
    unsigned char Eligible;
    unsigned short Control1;
    unsigned short Control2;
    unsigned short Information2;
    unsigned short EcCurrent;
    unsigned char Source;
    unsigned char BatteryPercent;
    unsigned char Completion;
    unsigned char Active;
} GA401EC_BALANCE;
