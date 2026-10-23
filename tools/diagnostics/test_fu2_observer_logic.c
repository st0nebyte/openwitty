// SPDX-License-Identifier: MIT
// Synthetic records only. No BPF, USB, ALSA, PipeWire, or privilege use.
#include <linux/types.h>
#include <stdio.h>
#include <string.h>
#include "fu2_observer_logic.h"

static unsigned checks, failures;
static unsigned readers_freed, probes_destroyed;

static void fake_free_reader(void *p) { if (p) ++readers_freed; }
static void fake_destroy_probe(void *p) { if (p) ++probes_destroyed; }

#define CHECK(name, condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL: %s\n", name); ++failures; } } while (0)

static struct fu2_event event(unsigned selector, bool set, bool range)
{
    struct fu2_event e;
    memset(&e, 0, sizeof(e));
    e.vendor = 0x29c2;
    e.product = 0x0004;
    e.w_index = 0x0200;
    e.w_value = selector << 8;
    e.request_type = set ? 0x21 : 0xa1;
    e.request = range ? 2 : 1;
    e.requested_len = range ? 8 : selector == 2 ? 2 : 1;
    e.result = e.requested_len;
    e.entry_seen = 1;
    e.data_len = e.requested_len;
    e.submit_ns = 1000000000ULL;
    e.complete_ns = 1001000000ULL;
    e.busnum = 3;
    e.devnum = 2;
    return e;
}

static struct fu2_observer_state state(void)
{
    struct fu2_observer_state s;
    memset(&s, 0, sizeof(s));
    s.armed_ns = 900000000ULL;
    return s;
}

int main(void)
{
    struct fu2_event e;
    struct fu2_observer_state s;
    struct fu2_resources resources;
    int dummy_reader = 1, dummy_probe = 2;

    e = event(2, false, false); s = state();
    e.data[0] = 0; e.data[1] = 0xfc; // -4 dB Q8.8
    CHECK("volume GET", fu2_classify(&s, &e) == FU2_OK &&
          fu2_signed_le16(e.data) == -1024);
    e = event(2, true, false); s = state();
    e.data[0] = 0; e.data[1] = 0xfc;
    CHECK("volume SET submission", fu2_classify(&s, &e) == FU2_OK &&
          fu2_kind_of(&e) == FU2_VOLUME_SET);
    e = event(1, true, false); s = state(); e.data[0] = 0;
    CHECK("mute SET", fu2_classify(&s, &e) == FU2_OK &&
          fu2_kind_of(&e) == FU2_MUTE_SET);
    e = event(1, false, false); s = state(); e.data[0] = 1;
    CHECK("mute GET", fu2_classify(&s, &e) == FU2_OK &&
          fu2_kind_of(&e) == FU2_MUTE_GET);
    e = event(2, false, false); s = state(); e.w_value |= 1;
    CHECK("non-master channel remains labeled", fu2_classify(&s, &e) == FU2_OK &&
          (e.w_value & 255) == 1);
    e = event(2, false, true); s = state();
    e.data[0] = 1; e.data[1] = 0; e.data[2] = 0; e.data[3] = 0xc4;
    e.data[4] = 0; e.data[5] = 0; e.data[6] = 0x80;
    CHECK("GET_RANGE", fu2_classify(&s, &e) == FU2_OK &&
          fu2_signed_le16(e.data + 2) == -15360 &&
          fu2_signed_le16(e.data + 6) == 128);
    CHECK("cannot arm before disconnect", !fu2_may_arm(false, true));
    CHECK("can arm only with module and absence", fu2_may_arm(true, true) &&
          !fu2_may_arm(true, false));
    e = event(2, false, false); s = state(); e.devnum = 7;
    CHECK("new address after arm", fu2_classify(&s, &e) == FU2_OK &&
          s.device_seen && s.devnum == 7);
    e = event(1, false, false); s = state(); e.result = -71; e.data_len = 0;
    CHECK("active -71 fails closed", fu2_classify(&s, &e) == FU2_FAILED);
    e = event(2, false, false); s = state(); e.data_len = 1;
    CHECK("truncated payload", fu2_classify(&s, &e) == FU2_MISSING_DATA);
    e = event(1, true, false); s = state(); e.result = -32;
    CHECK("failed SET is not applied", fu2_classify(&s, &e) == FU2_FAILED &&
          e.data_len == 1);
    e = event(2, true, false); s = state(); e.result = 0;
    CHECK("short OUT is not success", fu2_classify(&s, &e) == FU2_SHORT);
    e = event(2, false, false); s = state(); e.vendor = 0xffff;
    CHECK("wrong VID", fu2_classify(&s, &e) == FU2_IGNORE);
    e = event(2, false, false); s = state(); e.product = 0xffff;
    CHECK("wrong PID", fu2_classify(&s, &e) == FU2_IGNORE);
    e = event(2, false, false); s = state(); e.w_index = 0x0201;
    CHECK("wrong interface", fu2_classify(&s, &e) == FU2_IGNORE);
    e = event(2, false, false); s = state(); e.w_index = 0x3300;
    CHECK("entity 0x33", fu2_classify(&s, &e) == FU2_IGNORE);
    e = event(2, false, false); s = state();
    CHECK("first event", fu2_classify(&s, &e) == FU2_OK);
    e.devnum = 8;
    CHECK("second appearance stops", fu2_classify(&s, &e) == FU2_SECOND_DEVICE);
    s = state();
    CHECK("reconnect wait boundary", !fu2_deadline_reached(&s, s.armed_ns +
          29999999999ULL) && fu2_deadline_reached(&s, s.armed_ns +
          30000000000ULL));
    s = state(); s.device_seen = true;
    CHECK("120 second boundary", !fu2_deadline_reached(&s, s.armed_ns +
          119999999999ULL) && fu2_deadline_reached(&s, s.armed_ns +
          120000000000ULL));
    s = state(); s.events = FU2_MAX_EVENTS;
    e = event(2, false, false);
    CHECK("256 event cap", fu2_classify(&s, &e) == FU2_FAILED);
    e = event(2, false, false); s = state(); e.entry_seen = 0;
    CHECK("missing fentry", fu2_classify(&s, &e) == FU2_MISSING_ENTRY);
    e = event(2, false, false); s = state(); e.capture_error = 1;
    CHECK("capture failure", fu2_classify(&s, &e) == FU2_MISSING_DATA);
    e = event(2, false, false); s = state(); e.complete_ns = e.submit_ns - 1;
    CHECK("non-monotonic event", fu2_classify(&s, &e) == FU2_BAD_TIME);
    e = event(1, true, false); s = state(); e.data[0] = 2;
    CHECK("invalid mute value", fu2_classify(&s, &e) == FU2_MISSING_DATA);
    e = event(2, false, true); s = state(); e.data[0] = 2;
    CHECK("unsupported multi-range", fu2_classify(&s, &e) == FU2_MISSING_DATA);
    // Failure-path cleanup ordering/idempotence without loading libbpf.
    s = state(); s.stopped = true; e = event(2, false, false);
    CHECK("stopped observer rejects events", fu2_classify(&s, &e) == FU2_FAILED);
    CHECK("ring loss mandates stop", !fu2_has_loss(0) && fu2_has_loss(1));
    resources.reader = NULL; resources.probe = NULL;
    fu2_cleanup_resources(&resources, fake_free_reader, fake_destroy_probe);
    CHECK("cleanup before load", readers_freed == 0 && probes_destroyed == 0);
    resources.probe = &dummy_probe;
    fu2_cleanup_resources(&resources, fake_free_reader, fake_destroy_probe);
    CHECK("cleanup after load failure", readers_freed == 0 && probes_destroyed == 1 &&
          resources.probe == NULL);
    resources.reader = &dummy_reader; resources.probe = &dummy_probe;
    fu2_cleanup_resources(&resources, fake_free_reader, fake_destroy_probe);
    CHECK("cleanup after reader/attach failure", readers_freed == 1 &&
          probes_destroyed == 2 && resources.reader == NULL && resources.probe == NULL);
    fu2_cleanup_resources(&resources, fake_free_reader, fake_destroy_probe);
    CHECK("cleanup idempotent", readers_freed == 1 && probes_destroyed == 2);

    printf("FU2 offline classification: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
