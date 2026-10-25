#define WIN32_LEAN_AND_MEAN
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <assert.h>
#include <stdio.h>
#include "../shared/balance_journal.h"
typedef LONG NTSTATUS;
#define NT_SUCCESS(s) ((NTSTATUS)(s)>=0)
static void* g_BalanceOwner;
static USHORT g_BalanceLast;
static UCHAR fakeSource,fakeReady;
static USHORT fakeCurrent,request;
static int reads,writes,failRead,failWrite,badReadback;
static int drained, sticky;
static UCHAR FakePort(PUCHAR p){if((ULONG_PTR)p==0x257){drained++;if(!sticky)fakeReady=0;return 0x80;}return fakeReady;}
#define READ_PORT_UCHAR FakePort
#include "../shared/balance_ready.h"
static UCHAR EcReadXdataByteLocked(USHORT a){assert(a==0x05aa);return fakeSource;}
static USHORT EcReadXdataWordLocked(USHORT a){assert(a==0x0564);return request;}
static NTSTATUS VendorReadChargerLocked(UCHAR reg,USHORT* value,UCHAR* completion){assert(reg==0x14);reads++;*completion=0x80;if(failRead)return STATUS_IO_TIMEOUT;*value=fakeCurrent;return STATUS_SUCCESS;}
static NTSTATUS WriteIsl9238WordLocked(UCHAR reg,USHORT value,UCHAR* completion){assert(reg==0x14);writes++;*completion=0x80;if(failWrite)return STATUS_IO_TIMEOUT;if(!badReadback)fakeCurrent=value;return STATUS_SUCCESS;}
#include "../shared/balance_release.h"
static void reset(void){g_BalanceOwner=(void*)1;g_BalanceLast=fakeCurrent=100;request=0;fakeSource=2;fakeReady=0;reads=writes=failRead=failWrite=badReadback=0;}
int main(void){
 fakeReady=1;assert(NT_SUCCESS(BalanceReadyLocked())&&drained==1);
 fakeReady=2;drained=0;assert(BalanceReadyLocked()==STATUS_DEVICE_BUSY&&!drained);
 fakeReady=3;assert(BalanceReadyLocked()==STATUS_DEVICE_BUSY&&!drained);
 fakeReady=0xff;assert(BalanceReadyLocked()==STATUS_DEVICE_NOT_CONNECTED&&!drained);
 fakeReady=1;sticky=1;assert(BalanceReadyLocked()==STATUS_DEVICE_BUSY&&drained==4);sticky=0;
 BALANCE_JOURNAL journal={0x324A5047u,1,0,96,100,80,0};
 journal.checksum=JournalChecksum(&journal);
 assert(JournalValid(&journal));assert(JournalOwns(&journal,96));assert(JournalOwns(&journal,100));assert(!JournalOwns(&journal,104));
 journal.planned=104;assert(!JournalValid(&journal));
 journal.checksum=JournalChecksum(&journal);assert(JournalValid(&journal));
 journal.planned=0xffff;journal.checksum=JournalChecksum(&journal);assert(!JournalValid(&journal));
 for(int s=0;s<4;s++){if(s==2)continue;reset();fakeSource=(UCHAR)s;assert(BalanceReleaseLocked()==STATUS_DEVICE_NOT_READY);assert(g_BalanceOwner&&reads==0&&writes==0);fakeSource=2;assert(NT_SUCCESS(BalanceReleaseLocked()));assert(!g_BalanceOwner&&fakeCurrent==request&&writes==1);}
 reset();fakeReady=1;assert(BalanceReleaseLocked()==STATUS_DEVICE_BUSY);assert(g_BalanceOwner&&!reads&&!writes);
 reset();failRead=1;assert(!NT_SUCCESS(BalanceReleaseLocked()));assert(g_BalanceOwner&&!writes);
 reset();failWrite=1;assert(!NT_SUCCESS(BalanceReleaseLocked()));assert(g_BalanceOwner);
 reset();badReadback=1;assert(!NT_SUCCESS(BalanceReleaseLocked()));assert(g_BalanceOwner);
 reset();fakeCurrent=0x870;assert(NT_SUCCESS(BalanceReleaseLocked()));assert(!g_BalanceOwner&&!writes);
 reset();fakeCurrent=0xffff;assert(!NT_SUCCESS(BalanceReleaseLocked()));assert(g_BalanceOwner&&!writes);
 reset();request=0xffff;assert(!NT_SUCCESS(BalanceReleaseLocked()));assert(g_BalanceOwner&&!writes);
 puts("PASS: non-PD keeps restoration pending without SMBus commands; reconnect restores and verifies; busy/error/invalid/readback failure retain ownership; valid EC override respected");return 0;
}
