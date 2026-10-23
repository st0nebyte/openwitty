// SPDX-License-Identifier: MIT
#pragma once
// Include after vmlinux.h in BPF, or <linux/types.h> in offline userspace tests.

// Only Feature Unit 2 control data; no descriptors, serials or audio payload.
enum { FU2_CORE_MAX_EVENTS = 256, FU2_CORE_MAX_SECONDS = 120 };
struct fu2_core_event {
    __u64 entry_ns;
    __u64 exit_ns;
    __u64 correlation_id; // transient TID, for entry/exit matching only
    __s32 result;
    __u16 value;
    __u16 index;
    __u16 requested_len;
    __u8 request_type;
    __u8 request;
    __u8 busnum;
    __u8 devnum;
    __u8 data_len;
    __u8 data_error;
    __u8 data[8];
};

struct fu2_core_pending {
    __u64 entry_ns;
    __u64 device_ptr; // map-only; never emitted
    __u64 data_ptr;   // map-only; valid until synchronous return
    __u16 value;
    __u16 index;
    __u16 size;
    __u8 request_type;
    __u8 request;
    __u8 data_len;
    __u8 data_error;
    __u8 data[8];
};
