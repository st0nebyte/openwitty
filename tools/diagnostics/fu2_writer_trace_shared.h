// SPDX-License-Identifier: MIT
#pragma once

// Shared fixed-size event; only short UAC2 Feature Unit 2 control data.
struct fu2_event {
    __u64 submit_ns;
    __u64 complete_ns;
    __u32 pid;
    __u32 tid;
    __s32 result;
    __s32 stack_len;
    __u16 w_value;
    __u16 w_index;
    __u16 requested_len;
    __u8 request_type;
    __u8 request;
    __u8 data_len;
    __u8 data[8];
    char comm[16];
    __u64 stack[10];
};
