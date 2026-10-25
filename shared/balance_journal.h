#pragma once
#include <stdint.h>
typedef struct BALANCE_JOURNAL {
    uint32_t magic, sequence, original, last, planned, target, checksum;
} BALANCE_JOURNAL;
static uint32_t JournalChecksum(const BALANCE_JOURNAL* j) {
    return 0xA719CD35u ^ j->magic ^ j->sequence ^ j->original ^ j->last ^ j->planned ^ j->target;
}
static int JournalCurrentValid(uint32_t value) { return value <= 0x0870u && !(value & 3u); }
static int JournalValid(const BALANCE_JOURNAL* j) {
    return j->magic == 0x324A5047u && j->sequence &&
        j->checksum == JournalChecksum(j) && JournalCurrentValid(j->original) &&
        JournalCurrentValid(j->last) && JournalCurrentValid(j->planned) && j->target >= 25 && j->target <= 100;
}
static int JournalOwns(const BALANCE_JOURNAL* j, uint32_t current) {
    return JournalValid(j) && (current == j->last || current == j->planned);
}
