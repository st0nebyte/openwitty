// SPDX-License-Identifier: MIT
#pragma once

// Pure, offline-testable classification. This header performs no I/O.
#include <stdbool.h>
#include <stdint.h>
#include "fu2_writer_trace_shared.h"

enum fu2_kind { FU2_INVALID, FU2_VOLUME_GET, FU2_VOLUME_SET,
                FU2_MUTE_GET, FU2_MUTE_SET, FU2_VOLUME_RANGE };
enum fu2_verdict { FU2_IGNORE, FU2_OK, FU2_FAILED, FU2_SHORT,
                   FU2_MISSING_ENTRY, FU2_MISSING_DATA, FU2_BAD_TIME,
                   FU2_SECOND_DEVICE };
enum { FU2_MAX_EVENTS = 256, FU2_MAX_SECONDS = 120,
       FU2_RECONNECT_WAIT_SECONDS = 30 };

struct fu2_observer_state {
    uint64_t armed_ns;
    uint64_t first_event_ns;
    unsigned events;
    uint8_t busnum;
    uint8_t devnum;
    bool device_seen;
    bool stopped;
};

// Resource cleanup is injectable so failure-path ordering can be tested
// without libbpf or root. The live loader supplies libbpf destructors.
struct fu2_resources {
    void *reader;
    void *probe;
};

static inline void fu2_cleanup_resources(struct fu2_resources *resources,
                                          void (*free_reader)(void *),
                                          void (*destroy_probe)(void *))
{
    if (resources->reader) {
        free_reader(resources->reader);
        resources->reader = NULL;
    }
    if (resources->probe) {
        destroy_probe(resources->probe);
        resources->probe = NULL;
    }
}

static inline bool fu2_may_arm(bool target_absent, bool module_loaded)
{
    return target_absent && module_loaded;
}

static inline bool fu2_has_loss(uint64_t dropped)
{
    return dropped != 0;
}

static inline enum fu2_kind fu2_kind_of(const struct fu2_event *e)
{
    unsigned selector = e->w_value >> 8;
    if (e->vendor != 0x29c2 || e->product != 0x0004 ||
        e->w_index != 0x0200 ||
        (e->request_type != 0xa1 && e->request_type != 0x21))
        return FU2_INVALID;
    if (selector == 2 && e->request_type == 0xa1 && e->request == 2)
        return FU2_VOLUME_RANGE;
    if (e->request != 1)
        return FU2_INVALID;
    if (selector == 2)
        return e->request_type == 0x21 ? FU2_VOLUME_SET : FU2_VOLUME_GET;
    if (selector == 1)
        return e->request_type == 0x21 ? FU2_MUTE_SET : FU2_MUTE_GET;
    return FU2_INVALID;
}

static inline unsigned fu2_expected_length(enum fu2_kind kind)
{
    switch (kind) {
    case FU2_VOLUME_GET: case FU2_VOLUME_SET: return 2;
    case FU2_MUTE_GET: case FU2_MUTE_SET: return 1;
    case FU2_VOLUME_RANGE: return 8;
    default: return 0;
    }
}

static inline enum fu2_verdict fu2_classify(struct fu2_observer_state *state,
                                             const struct fu2_event *e)
{
    enum fu2_kind kind = fu2_kind_of(e);
    unsigned expected = fu2_expected_length(kind);
    if (kind == FU2_INVALID)
        return FU2_IGNORE;
    if (state->stopped || state->events >= FU2_MAX_EVENTS)
        return FU2_FAILED;
    ++state->events;
    if (e->complete_ns < e->submit_ns || e->submit_ns < state->armed_ns)
        return FU2_BAD_TIME;
    if (!state->device_seen) {
        state->device_seen = true;
        state->busnum = e->busnum;
        state->devnum = e->devnum;
        state->first_event_ns = e->complete_ns;
    } else if (state->busnum != e->busnum || state->devnum != e->devnum) {
        return FU2_SECOND_DEVICE;
    }
    if (!e->entry_seen)
        return FU2_MISSING_ENTRY;
    if (e->result < 0)
        return FU2_FAILED;
    // snd_usb_ctl_msg() returns usb_control_msg()'s byte count, not a
    // zero-on-success convention. A short OUT is not a successful SET.
    if ((unsigned)e->result != expected || e->requested_len != expected)
        return FU2_SHORT;
    if (e->capture_error || e->data_len != expected)
        return FU2_MISSING_DATA;
    if ((kind == FU2_MUTE_GET || kind == FU2_MUTE_SET) && e->data[0] > 1)
        return FU2_MISSING_DATA;
    if (kind == FU2_VOLUME_RANGE &&
        ((unsigned)e->data[0] | ((unsigned)e->data[1] << 8)) != 1)
        return FU2_MISSING_DATA;
    return FU2_OK;
}

static inline bool fu2_deadline_reached(const struct fu2_observer_state *state,
                                         uint64_t now_ns)
{
    if (now_ns < state->armed_ns)
        return true;
    if (now_ns - state->armed_ns >=
        (uint64_t)FU2_MAX_SECONDS * 1000000000ULL)
        return true;
    return !state->device_seen && now_ns - state->armed_ns >=
        (uint64_t)FU2_RECONNECT_WAIT_SECONDS * 1000000000ULL;
}

static inline int16_t fu2_signed_le16(const uint8_t *bytes)
{
    return (int16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}
