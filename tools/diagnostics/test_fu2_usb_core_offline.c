// SPDX-License-Identifier: MIT
// Synthetic only: no USB, BPF or audio APIs.
#include <linux/types.h>
#include <stdio.h>
#include "fu2_usb_core_logic.h"

static int failures, checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); ++failures; \
} } while (0)

static struct fu2_core_event event(__u8 type, __u8 request, __u16 value,
                                   __u16 length, int result)
{
    struct fu2_core_event e = {0};
    e.entry_ns = 1000000001;
    e.exit_ns = 1000000200;
    e.correlation_id = 11;
    e.request_type = type;
    e.request = request;
    e.value = value;
    e.index = 0x0200;
    e.requested_len = length;
    e.result = result;
    e.data_len = result >= 0 ? length : 0;
    e.busnum = 3;
    e.devnum = 7;
    return e;
}

static void volume(struct fu2_core_event *e, int raw)
{
    e->data[0] = raw & 255;
    e->data[1] = (raw >> 8) & 255;
}

int main(void)
{
    struct fu2_core_state state = {.armed_ns = 1000000000};
    struct fu2_core_event e;
    struct fu2_core_pending p = {.device_ptr = 1, .data_ptr = 2,
        .value = 0x0200, .index = 0x0200, .size = 2,
        .request_type = 0x21, .request = 1};
    const unsigned pipe = 2u << 30;

    CHECK(fu2_core_identity(0x29c2, 0x0004));
    CHECK(!fu2_core_identity(0x29c3, 0x0004));
    CHECK(!fu2_core_identity(0x29c2, 0x0005));
    CHECK(fu2_core_fields(pipe, 1, 0xa1, 0x0200, 0x0200, 2));
    CHECK(!fu2_core_fields(pipe, 1, 0xa1, 0x0200, 0x3300, 2));
    CHECK(!fu2_core_fields(pipe, 1, 0xa1, 0x0200, 0x0202, 2));
    CHECK(!fu2_core_fields(pipe | (1u << 15), 1, 0xa1, 0x0200, 0x0200, 2));
    CHECK(!fu2_core_fields(3u << 30, 1, 0xa1, 0x0200, 0x0200, 2));
    CHECK(!fu2_core_fields(pipe, 3, 0xa1, 0x0200, 0x0200, 2));
    CHECK(!fu2_core_fields(pipe, 1, 0xc0, 0x0200, 0x0200, 2));
    CHECK(fu2_core_matches_pending(&p, 1, 2, 1, 0x21, 0x0200, 0x0200, 2));
    CHECK(!fu2_core_matches_pending(NULL, 1, 2, 1, 0x21, 0x0200, 0x0200, 2));
    CHECK(!fu2_core_matches_pending(&p, 1, 3, 1, 0x21, 0x0200, 0x0200, 2));
    CHECK(!fu2_core_matches_pending(&p, 4, 2, 1, 0x21, 0x0200, 0x0200, 2));

    // Phase 8A.9 observed reconnect values.
    e = event(0xa1, 1, 0x0200, 2, 2); volume(&e, -1024);
    CHECK(fu2_core_kind_of(&e) == CORE_VOLUME_GET);
    CHECK(fu2_core_q88(e.data) == -1024);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK);
    e = event(0x21, 1, 0x0200, 2, 2); volume(&e, -15360);
    CHECK(fu2_core_kind_of(&e) == CORE_VOLUME_SET);
    CHECK(fu2_core_q88(e.data) == -15360);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK);
    e = event(0xa1, 1, 0x0200, 2, 2); volume(&e, -1024);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK); // hypothetical stale GET
    CHECK(fu2_core_q88(e.data) == -1024);
    volume(&e, -15360);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK); // measured reconnect GET
    CHECK(fu2_core_q88(e.data) == -15360);
    e = event(0x21, 1, 0x0200, 2, 2); volume(&e, -896);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK); // resolution +0.5 dB
    e = event(0xa1, 1, 0x0200, 2, 2); volume(&e, -896);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK);
    e = event(0x21, 1, 0x0200, 2, 2); volume(&e, -1024);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK); // restore attempt
    e.result = -71; e.data_len = 0;
    CHECK(fu2_core_accept(&state, &e) == CORE_ERROR); // failed restore
    e = event(0x21, 1, 0x0200, 2, 2); volume(&e, 0);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK); // sticky fallback attempt
    e = event(0xa1, 1, 0x0200, 2, 2); volume(&e, -1024);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK); // hypothetical old min readback
    e = event(0x21, 1, 0x0200, 2, 2); volume(&e, 0);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK); // hypothetical max test
    e = event(0xa1, 1, 0x0200, 2, 2); volume(&e, -1024);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK); // hypothetical old max readback
    e = event(0xa1, 2, 0x0200, 8, 8);
    e.data[0] = 1; e.data[2] = 0; e.data[3] = 0xc4;
    e.data[6] = 0x80; // one range: -60, 0, 0.5 dB
    CHECK(fu2_core_kind_of(&e) == CORE_RANGE);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK);
    e = event(0xa1, 1, 0x0100, 1, 1); e.data[0] = 0;
    CHECK(fu2_core_kind_of(&e) == CORE_MUTE_GET);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK);
    e = event(0x21, 1, 0x0100, 1, 1); e.data[0] = 1;
    CHECK(fu2_core_kind_of(&e) == CORE_MUTE_SET);
    CHECK(fu2_core_accept(&state, &e) == CORE_OK);
    e.result = -110; e.data_len = 0;
    CHECK(fu2_core_accept(&state, &e) == CORE_ERROR);
    e = event(0xa1, 1, 0x0200, 2, 1);
    CHECK(fu2_core_accept(&state, &e) == CORE_SHORT);
    e = event(0xa1, 1, 0x0200, 2, 2); e.data_len = 1;
    CHECK(fu2_core_accept(&state, &e) == CORE_BAD_DATA);
    e.data_len = 2; e.data_error = 1;
    CHECK(fu2_core_accept(&state, &e) == CORE_BAD_DATA);
    e = event(0xa1, 1, 0x0200, 2, 2); e.devnum = 8;
    CHECK(fu2_core_accept(&state, &e) == CORE_SECOND_DEVICE);
    e = event(0xa1, 1, 0x0200, 2, 2); e.exit_ns = e.entry_ns - 1;
    CHECK(fu2_core_accept(&state, &e) == CORE_BAD_TIME);
    e = event(0xa1, 1, 0x0200, 2, 2); e.index = 0x3300;
    CHECK(fu2_core_accept(&state, &e) == CORE_IGNORE);
    CHECK(!fu2_core_deadline(1000000000, 120999999999ULL));
    CHECK(fu2_core_deadline(1000000000, 121000000000ULL));
    CHECK(fu2_core_deadline(1000000000, 999999999ULL));
    state.events = FU2_CORE_MAX_EVENTS;
    e = event(0xa1, 1, 0x0200, 2, 2);
    CHECK(fu2_core_accept(&state, &e) == CORE_CAP);
    // Independent in-flight records and a missing return must be detectable.
    struct fu2_core_pending concurrent = p;
    concurrent.device_ptr = 4;
    concurrent.data_ptr = 5;
    CHECK(fu2_core_matches_pending(&p, 1, 2, 1, 0x21, 0x0200, 0x0200, 2));
    CHECK(fu2_core_matches_pending(&concurrent, 4, 5, 1, 0x21,
                                    0x0200, 0x0200, 2));
    CHECK(!fu2_core_matches_pending(&p, 4, 5, 1, 0x21, 0x0200, 0x0200, 2));
    CHECK(fu2_core_incomplete(1, 0)); // missing return at shutdown
    CHECK(fu2_core_incomplete(0, 1)); // ring/map loss
    CHECK(!fu2_core_incomplete(0, 0));
    printf("fu2_usb_core_offline: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
