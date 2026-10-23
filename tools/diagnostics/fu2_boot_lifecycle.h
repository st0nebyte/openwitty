// SPDX-License-Identifier: MIT
#pragma once
// Pure, offline-testable lifecycle for a future bounded FU2 boot observer.
// No I/O, no BPF, no USB: every side effect goes through injected `ops`.
// Include <linux/types.h> (or vmlinux.h) before this header.
#include <stdint.h>
#include "fu2_usb_core_logic.h"

enum fu2_lc_state { LC_IDLE, LC_ATTACHED, LC_ARMED, LC_OBSERVING, LC_DONE,
                    LC_CLEANUP_FAILED };

enum fu2_lc_reason {
    LCR_NONE, LCR_NORMAL_TAIL, LCR_EVENT_CAP, LCR_HARD_DEADLINE,
    LCR_NO_FIRST_EVENT, LCR_TOTAL_DEADLINE, LCR_TARGET_ERROR,
    LCR_SECOND_DEVICE, LCR_LOSS, LCR_MISSED_WINDOW, LCR_ATTACH_FAILED,
    LCR_SIGNAL, LCR_READER_FAULT, LCR_SUPERVISOR
};

enum fu2_lc_integrity { LCI_UNKNOWN, LCI_OK, LCI_INCOMPLETE, LCI_LOSS };

enum fu2_lc_outcome { LCO_NONE, LCO_COMPLETE, LCO_TRUNCATED, LCO_NO_DATA,
                      LCO_INVALID };

enum { FU2_LC_MAX_LINKS = 4,
       LC_FAIL_DETACH = 1, LC_FAIL_COUNTERS = 2, LC_FAIL_READER = 4,
       LC_FAIL_MAPS = 8, LC_FAIL_VERIFY = 16 };

struct fu2_lc_config {
    uint64_t first_event_deadline_ns; // after attach
    uint64_t total_deadline_ns;       // after attach
    uint64_t hard_after_first_ns;     // after first matching event
    uint64_t tail_quiet_ns;           // quiet period after last event
    uint64_t cleanup_grace_ns;        // supervisor slack beyond app limits
    unsigned max_events;
};

static inline struct fu2_lc_config fu2_lc_default_config(void)
{
    struct fu2_lc_config c;
    c.first_event_deadline_ns = 90ULL * 1000000000ULL;
    c.total_deadline_ns = 180ULL * 1000000000ULL;
    c.hard_after_first_ns = (uint64_t)FU2_CORE_MAX_SECONDS * 1000000000ULL;
    c.tail_quiet_ns = 10ULL * 1000000000ULL;
    c.cleanup_grace_ns = 15ULL * 1000000000ULL;
    c.max_events = FU2_CORE_MAX_EVENTS;
    return c;
}

// The independent backstop (e.g. a service manager's runtime limit) must fire
// strictly after every application-level limit, so it only matters if the
// reader itself hangs.
static inline uint64_t fu2_lc_supervisor_limit_ns(const struct fu2_lc_config *c)
{
    uint64_t app = c->total_deadline_ns;
    if (c->first_event_deadline_ns + c->hard_after_first_ns > app)
        app = c->first_event_deadline_ns + c->hard_after_first_ns;
    return app + c->cleanup_grace_ns;
}

// Each op returns 0 on success. Only link ids registered through
// fu2_lc_register_link() are ever passed to detach_link().
struct fu2_lc_ops {
    void *ctx;
    int (*detach_link)(void *ctx, int link_id);
    // lost: genuine loss only. overcap: events intentionally suppressed at the cap.
    int (*read_counters)(void *ctx, uint64_t *lost, uint64_t *pending,
                         uint64_t *overcap);
    int (*release_reader)(void *ctx);
    int (*destroy_maps)(void *ctx);
    int (*verify_clean)(void *ctx);
};

struct fu2_lc {
    enum fu2_lc_state state;
    enum fu2_lc_reason reason;
    enum fu2_core_verdict target_verdict;
    enum fu2_lc_integrity integrity;
    struct fu2_lc_config cfg;
    struct fu2_lc_ops ops;
    struct fu2_core_state core;
    int links[FU2_LC_MAX_LINKS];
    unsigned link_count;
    unsigned events;
    unsigned stop_requests;
    unsigned cleanup_runs;
    unsigned fail_mask;
    uint64_t attach_ns, first_ns, last_ns, lost, pending_left;
    uint64_t overcap_lost;
    int started_once;
};

static inline void fu2_lc_init(struct fu2_lc *lc, const struct fu2_lc_config *cfg,
                               const struct fu2_lc_ops *ops)
{
    unsigned i;
    lc->state = LC_IDLE;
    lc->reason = LCR_NONE;
    lc->target_verdict = CORE_OK;
    lc->integrity = LCI_UNKNOWN;
    lc->cfg = *cfg;
    lc->ops = *ops;
    lc->core.armed_ns = 0;
    lc->core.events = 0;
    lc->core.busnum = 0;
    lc->core.devnum = 0;
    lc->core.device_seen = 0;
    for (i = 0; i < FU2_LC_MAX_LINKS; ++i)
        lc->links[i] = -1;
    lc->link_count = lc->events = lc->stop_requests = lc->cleanup_runs = 0;
    lc->fail_mask = 0;
    lc->attach_ns = lc->first_ns = lc->last_ns = lc->lost = lc->pending_left = 0;
    lc->overcap_lost = 0;
    lc->started_once = 0;
}

static inline int fu2_lc_done(const struct fu2_lc *lc)
{
    return lc->state == LC_DONE || lc->state == LC_CLEANUP_FAILED;
}

// Registers an experiment-owned link before/at attach. Ids outside this list
// can never be detached by the lifecycle.
static inline int fu2_lc_register_link(struct fu2_lc *lc, int link_id)
{
    if (lc->state != LC_IDLE || link_id < 0 || lc->link_count >= FU2_LC_MAX_LINKS)
        return -1;
    lc->links[lc->link_count++] = link_id;
    return 0;
}

static inline void fu2_lc_cleanup(struct fu2_lc *lc)
{
    unsigned i;
    uint64_t lost = 0, pending = 0, overcap = 0;
    ++lc->cleanup_runs; // exactly once per lifecycle; never retried automatically
    for (i = 0; i < lc->link_count; ++i)
        if (lc->ops.detach_link(lc->ops.ctx, lc->links[i]) != 0)
            lc->fail_mask |= LC_FAIL_DETACH;
    // Pending/lost are read after detaching so no new records can appear.
    if (lc->ops.read_counters(lc->ops.ctx, &lost, &pending, &overcap) != 0) {
        lc->fail_mask |= LC_FAIL_COUNTERS;
    } else {
        lc->lost = lost;
        lc->pending_left = pending;
        lc->overcap_lost = overcap; // expected after a cap stop; never counted as loss
        lc->integrity = lost != 0 ? LCI_LOSS :
                        (fu2_core_incomplete(pending, lost) ? LCI_INCOMPLETE : LCI_OK);
    }
    if (lc->ops.release_reader(lc->ops.ctx) != 0)
        lc->fail_mask |= LC_FAIL_READER;
    if (lc->ops.destroy_maps(lc->ops.ctx) != 0)
        lc->fail_mask |= LC_FAIL_MAPS;
    if (lc->ops.verify_clean(lc->ops.ctx) != 0)
        lc->fail_mask |= LC_FAIL_VERIFY;
    lc->state = lc->fail_mask ? LC_CLEANUP_FAILED : LC_DONE;
}

// First request wins; repeated requests are counted and otherwise ignored.
static inline void fu2_lc_stop(struct fu2_lc *lc, enum fu2_lc_reason reason)
{
    ++lc->stop_requests;
    if (fu2_lc_done(lc) || lc->cleanup_runs != 0)
        return;
    lc->reason = reason;
    fu2_lc_cleanup(lc);
}

// Attach is performed by the caller; the lifecycle only accounts for it.
static inline int fu2_lc_attached(struct fu2_lc *lc, uint64_t now_ns, int ok)
{
    if (lc->started_once || lc->state != LC_IDLE)
        return -1; // no automatic retry, no second observer
    lc->started_once = 1;
    if (!ok) {
        fu2_lc_stop(lc, LCR_ATTACH_FAILED);
        return -1;
    }
    lc->attach_ns = now_ns;
    lc->core.armed_ns = now_ns;
    lc->state = LC_ATTACHED;
    return 0;
}

// Ordering proof: call this only AFTER attach completed. If the audio module
// is still absent now, it can only initialise (and send its first mixer
// request) after the hooks exist.
static inline int fu2_lc_verify_window(struct fu2_lc *lc, int audio_module_present)
{
    if (lc->state != LC_ATTACHED)
        return -1;
    if (audio_module_present) {
        fu2_lc_stop(lc, LCR_MISSED_WINDOW);
        return -1;
    }
    lc->state = LC_ARMED;
    return 0;
}

static inline void fu2_lc_on_event(struct fu2_lc *lc, const struct fu2_core_event *e,
                                   uint64_t now_ns)
{
    enum fu2_core_verdict v;
    if (fu2_lc_done(lc))
        return;
    if (lc->state != LC_ARMED && lc->state != LC_OBSERVING) {
        fu2_lc_stop(lc, LCR_MISSED_WINDOW); // traffic seen before the ordering proof
        return;
    }
    v = fu2_core_accept(&lc->core, e);
    if (v == CORE_IGNORE)
        return;
    if (v == CORE_CAP) {
        fu2_lc_stop(lc, LCR_EVENT_CAP);
        return;
    }
    if (v != CORE_OK) {
        lc->target_verdict = v;
        fu2_lc_stop(lc, v == CORE_SECOND_DEVICE ? LCR_SECOND_DEVICE : LCR_TARGET_ERROR);
        return;
    }
    if (lc->state == LC_ARMED) {
        lc->state = LC_OBSERVING;
        lc->first_ns = now_ns;
    }
    lc->last_ns = now_ns;
    if (++lc->events >= lc->cfg.max_events)
        fu2_lc_stop(lc, LCR_EVENT_CAP);
}

static inline void fu2_lc_on_lost(struct fu2_lc *lc, uint64_t lost_now)
{
    if (fu2_lc_done(lc))
        return;
    if (lost_now != 0)
        fu2_lc_stop(lc, LCR_LOSS);
}

static inline void fu2_lc_tick(struct fu2_lc *lc, uint64_t now_ns)
{
    if (fu2_lc_done(lc) || lc->state == LC_IDLE)
        return;
    if (now_ns < lc->attach_ns) { // clock went backwards: fail closed
        fu2_lc_stop(lc, LCR_TOTAL_DEADLINE);
        return;
    }
    if (now_ns - lc->attach_ns >= lc->cfg.total_deadline_ns) {
        fu2_lc_stop(lc, LCR_TOTAL_DEADLINE);
    } else if (lc->state != LC_OBSERVING &&
               now_ns - lc->attach_ns >= lc->cfg.first_event_deadline_ns) {
        fu2_lc_stop(lc, LCR_NO_FIRST_EVENT);
    } else if (lc->state == LC_OBSERVING) {
        if (now_ns - lc->first_ns >= lc->cfg.hard_after_first_ns)
            fu2_lc_stop(lc, LCR_HARD_DEADLINE);
        else if (now_ns - lc->last_ns >= lc->cfg.tail_quiet_ns)
            fu2_lc_stop(lc, LCR_NORMAL_TAIL);
    }
}

static inline enum fu2_lc_outcome fu2_lc_outcome_of(const struct fu2_lc *lc)
{
    if (!fu2_lc_done(lc))
        return LCO_NONE;
    if (lc->fail_mask != 0 || lc->integrity != LCI_OK ||
        lc->reason == LCR_TARGET_ERROR || lc->reason == LCR_SECOND_DEVICE ||
        lc->reason == LCR_LOSS || lc->reason == LCR_MISSED_WINDOW ||
        lc->reason == LCR_ATTACH_FAILED || lc->reason == LCR_READER_FAULT)
        return LCO_INVALID;
    if (lc->events == 0)
        return LCO_NO_DATA;
    return lc->reason == LCR_NORMAL_TAIL ? LCO_COMPLETE : LCO_TRUNCATED;
}
