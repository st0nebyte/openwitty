// SPDX-License-Identifier: MIT
#pragma once
#include "fu2_usb_core_shared.h"

enum fu2_core_kind { CORE_OTHER, CORE_VOLUME_GET, CORE_VOLUME_SET,
                     CORE_RANGE, CORE_MUTE_GET, CORE_MUTE_SET };
enum fu2_core_verdict { CORE_OK, CORE_IGNORE, CORE_ERROR,
                        CORE_SHORT, CORE_BAD_DATA, CORE_BAD_TIME,
                        CORE_SECOND_DEVICE, CORE_CAP };

struct fu2_core_state {
    __u64 armed_ns;
    __u32 events;
    __u8 busnum;
    __u8 devnum;
    __u8 device_seen;
};

static inline int fu2_core_fields(unsigned int pipe, __u8 request,
                                  __u8 type, __u16 value, __u16 index,
                                  __u16 size)
{
    // usb_pipecontrol() and usb_pipeendpoint() from installed usb.h.
    if (((pipe >> 30) & 3) != 2 || ((pipe >> 15) & 15) != 0 ||
        index != 0x0200 || (value != 0x0200 && value != 0x0100))
        return 0;
    return (type == 0xa1 && request == 1 &&
            size == (value == 0x0200 ? 2 : 1)) ||
           (type == 0xa1 && request == 2 && value == 0x0200 && size == 8) ||
           (type == 0x21 && request == 1 &&
            size == (value == 0x0200 ? 2 : 1));
}

static inline int fu2_core_identity(__u16 vendor, __u16 product)
{
    return vendor == 0x29c2 && product == 0x0004;
}

static inline int fu2_core_matches_pending(const struct fu2_core_pending *item,
                                            __u64 dev, __u64 data,
                                            __u8 request, __u8 type,
                                            __u16 value, __u16 index, __u16 size)
{
    return item && item->device_ptr == dev && item->data_ptr == data &&
           item->request == request && item->request_type == type &&
           item->value == value && item->index == index && item->size == size;
}

static inline enum fu2_core_kind fu2_core_kind_of(const struct fu2_core_event *e)
{
    if (!fu2_core_fields(2u << 30, e->request, e->request_type,
                          e->value, e->index, e->requested_len))
        return CORE_OTHER;
    if (e->request == 2)
        return CORE_RANGE;
    if (e->value == 0x0200)
        return e->request_type == 0x21 ? CORE_VOLUME_SET : CORE_VOLUME_GET;
    return e->request_type == 0x21 ? CORE_MUTE_SET : CORE_MUTE_GET;
}

static inline int fu2_core_q88(const __u8 data[2])
{
    return (__s16)((__u16)data[0] | ((__u16)data[1] << 8));
}

static inline enum fu2_core_verdict fu2_core_accept(struct fu2_core_state *state,
                                                     const struct fu2_core_event *e)
{
    enum fu2_core_kind kind = fu2_core_kind_of(e);
    if (kind == CORE_OTHER)
        return CORE_IGNORE;
    if (state->events >= FU2_CORE_MAX_EVENTS)
        return CORE_CAP;
    ++state->events;
    if (e->entry_ns < state->armed_ns || e->exit_ns < e->entry_ns)
        return CORE_BAD_TIME;
    if (!state->device_seen) {
        state->device_seen = 1;
        state->busnum = e->busnum;
        state->devnum = e->devnum;
    } else if (state->busnum != e->busnum || state->devnum != e->devnum) {
        return CORE_SECOND_DEVICE;
    }
    if (e->result < 0)
        return CORE_ERROR;
    if (e->result != e->requested_len)
        return CORE_SHORT;
    if (e->data_error || e->data_len != e->requested_len)
        return CORE_BAD_DATA;
    if ((kind == CORE_MUTE_GET || kind == CORE_MUTE_SET) && e->data[0] > 1)
        return CORE_BAD_DATA;
    if (kind == CORE_RANGE &&
        (((__u16)e->data[0] | ((__u16)e->data[1] << 8)) != 1))
        return CORE_BAD_DATA;
    return CORE_OK;
}

static inline int fu2_core_deadline(__u64 armed_ns, __u64 now_ns)
{
    return now_ns < armed_ns || now_ns - armed_ns >=
           (__u64)FU2_CORE_MAX_SECONDS * 1000000000ULL;
}

static inline int fu2_core_incomplete(__u64 pending_count, __u64 lost)
{
    return pending_count != 0 || lost != 0;
}
