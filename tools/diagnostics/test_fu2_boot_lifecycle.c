// SPDX-License-Identifier: MIT
// Synthetic only: fake links/maps and local processes. No BPF, USB or audio APIs.
#include <linux/types.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "fu2_boot_lifecycle.h"

static int failures, checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); ++failures; \
} } while (0)

static const uint64_t T0 = 1000000000ULL;
static const uint64_t SEC = 1000000000ULL;

// ---- fake kernel -----------------------------------------------------------
struct fake {
    int present[8];       // link id -> still attached
    int owned[8];         // link id -> experiment-owned
    int reader_open, maps_alive, pins;
    int fail_detach, fail_counters, fail_reader, fail_maps, fail_verify;
    uint64_t lost, pending, overcap;
    int detach_calls, unowned_detach_attempts, counter_reads;
};

static int f_detach(void *c, int id)
{
    struct fake *f = (struct fake *)c;
    ++f->detach_calls;
    if (id < 0 || id >= 8 || !f->owned[id]) {
        ++f->unowned_detach_attempts;
        return -1;
    }
    if (f->fail_detach)
        return -1;
    f->present[id] = 0;
    return 0;
}
static int f_counters(void *c, uint64_t *lost, uint64_t *pending, uint64_t *overcap)
{
    struct fake *f = (struct fake *)c;
    ++f->counter_reads;
    if (f->fail_counters)
        return -1;
    *lost = f->lost;
    *pending = f->pending;
    *overcap = f->overcap;
    return 0;
}
static int f_reader(void *c)
{
    struct fake *f = (struct fake *)c;
    if (f->fail_reader)
        return -1;
    f->reader_open = 0;
    return 0;
}
static int f_maps(void *c)
{
    struct fake *f = (struct fake *)c;
    if (f->fail_maps)
        return -1;
    f->maps_alive = 0;
    return 0;
}
static int f_verify(void *c)
{
    struct fake *f = (struct fake *)c;
    int i;
    if (f->fail_verify)
        return -1;
    for (i = 0; i < 8; ++i)
        if (f->owned[i] && f->present[i])
            return -1;
    return (f->reader_open || f->maps_alive || f->pins) ? -1 : 0;
}

static void fake_init(struct fake *f)
{
    memset(f, 0, sizeof(*f));
    f->present[1] = f->present[2] = f->owned[1] = f->owned[2] = 1; // fentry + fexit
    f->present[6] = f->present[7] = 1;                              // unrelated programs
    f->reader_open = f->maps_alive = 1;
}

static void lc_new(struct fu2_lc *lc, struct fake *f, struct fu2_lc_config cfg)
{
    struct fu2_lc_ops ops;
    fake_init(f);
    ops.ctx = f;
    ops.detach_link = f_detach;
    ops.read_counters = f_counters;
    ops.release_reader = f_reader;
    ops.destroy_maps = f_maps;
    ops.verify_clean = f_verify;
    fu2_lc_init(lc, &cfg, &ops);
    fu2_lc_register_link(lc, 1);
    fu2_lc_register_link(lc, 2);
}

static void lc_armed(struct fu2_lc *lc, struct fake *f, struct fu2_lc_config cfg)
{
    lc_new(lc, f, cfg);
    fu2_lc_attached(lc, T0, 1);
    fu2_lc_verify_window(lc, 0);
}

static struct fu2_core_event ev(__u8 type, __u8 request, __u16 value,
                                __u16 length, int result, int raw)
{
    struct fu2_core_event e;
    memset(&e, 0, sizeof(e));
    e.entry_ns = T0 + 1000;
    e.exit_ns = T0 + 1400;
    e.correlation_id = 11;
    e.request_type = type;
    e.request = request;
    e.value = value;
    e.index = 0x0200;
    e.requested_len = length;
    e.result = result;
    e.data_len = result >= 0 ? (__u8)length : 0;
    e.busnum = 3;
    e.devnum = 2;
    e.data[0] = (__u8)(raw & 255);
    e.data[1] = (__u8)((raw >> 8) & 255);
    return e;
}
static struct fu2_core_event vget(int raw) { return ev(0xa1, 1, 0x0200, 2, 2, raw); }
static struct fu2_core_event vset(int raw) { return ev(0x21, 1, 0x0200, 2, 2, raw); }

// ---- process helpers (fd-closure and signal behaviour) ---------------------
static volatile sig_atomic_t g_term;
static void on_term(int s) { (void)s; g_term = 1; }

static uint64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * SEC + (uint64_t)ts.tv_nsec;
}

// The write end of `pipefd` stands in for a non-pinned BPF link fd: the kernel
// destroys such links when the last fd closes, which includes abrupt death.
static int fd_closed_by_peer(int rfd, int wait_ms)
{
    struct timespec ts = {0, 1000000};
    char b;
    int i;
    for (i = 0; i < wait_ms; ++i) {
        ssize_t n = read(rfd, &b, 1);
        if (n == 0)
            return 1; // EOF: every write end closed
        nanosleep(&ts, NULL);
    }
    return 0;
}

static int f_detach_close_fd(void *c, int id)
{
    int *fd = (int *)c;
    (void)id;
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
    return 0;
}
static int ok_counters(void *c, uint64_t *l, uint64_t *p, uint64_t *o) { (void)c; *l = 0; *p = 0; *o = 0; return 0; }
static int ok_nop(void *c) { (void)c; return 0; }

static pid_t spawn_reader(int mode, int *rfd)
{
    int p[2];
    pid_t pid;
    if (pipe(p) != 0)
        return -1;
    pid = fork();
    if (pid == 0) {
        struct fu2_lc lc;
        struct fu2_lc_ops ops;
        struct fu2_lc_config cfg = fu2_lc_default_config();
        int linkfd = p[1];
        struct sigaction sa;
        close(p[0]);
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_term;
        if (mode == 3)
            sa.sa_handler = SIG_IGN; // hung reader that ignores SIGTERM
        sigaction(SIGTERM, &sa, NULL);
        ops.ctx = &linkfd;
        ops.detach_link = f_detach_close_fd;
        ops.read_counters = ok_counters;
        ops.release_reader = ok_nop;
        ops.destroy_maps = ok_nop;
        ops.verify_clean = ok_nop;
        fu2_lc_init(&lc, &cfg, &ops);
        fu2_lc_register_link(&lc, 1);
        fu2_lc_attached(&lc, mono_ns(), 1);
        fu2_lc_verify_window(&lc, 0);
        if (mode == 0) { // clean application-level stop
            fu2_lc_stop(&lc, LCR_NORMAL_TAIL);
            _exit(fu2_lc_done(&lc) && lc.state == LC_DONE ? 0 : 3);
        }
        if (mode == 1) // abrupt crash: no cleanup code runs
            raise(SIGKILL);
        if (mode == 2) { // cooperative SIGTERM handling
            while (!g_term)
                usleep(1000);
            fu2_lc_stop(&lc, LCR_SIGNAL);
            _exit(lc.state == LC_DONE && lc.reason == LCR_SIGNAL ? 0 : 3);
        }
        for (;;) // mode 3: hang forever
            usleep(1000);
    }
    close(p[1]);
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    *rfd = p[0];
    return pid;
}

int main(void)
{
    struct fu2_lc lc;
    struct fake f;
    struct fu2_lc_config cfg = fu2_lc_default_config();
    struct fu2_core_event e;
    int i;

    // Supervisor limit is strictly later than every application limit.
    CHECK(fu2_lc_supervisor_limit_ns(&cfg) > cfg.total_deadline_ns);
    CHECK(fu2_lc_supervisor_limit_ns(&cfg) >
          cfg.first_event_deadline_ns + cfg.hard_after_first_ns);

    // 1. Reconnect-style success -> quiet tail -> complete, clean.
    lc_armed(&lc, &f, cfg);
    CHECK(lc.state == LC_ARMED);
    e = vget(-1024);   fu2_lc_on_event(&lc, &e, T0 + 1 * SEC);
    CHECK(lc.state == LC_OBSERVING);
    e = vset(-15360);  fu2_lc_on_event(&lc, &e, T0 + 1 * SEC);
    e = vget(-15360);  fu2_lc_on_event(&lc, &e, T0 + 1 * SEC);
    e = vset(-896);    fu2_lc_on_event(&lc, &e, T0 + 1 * SEC);
    e = vget(-896);    fu2_lc_on_event(&lc, &e, T0 + 1 * SEC);
    e = vset(-1024);   fu2_lc_on_event(&lc, &e, T0 + 1 * SEC); // successful restore
    CHECK(lc.events == 6 && !fu2_lc_done(&lc));
    fu2_lc_tick(&lc, T0 + 5 * SEC);
    CHECK(!fu2_lc_done(&lc));
    fu2_lc_tick(&lc, T0 + 11 * SEC);
    CHECK(lc.state == LC_DONE && lc.reason == LCR_NORMAL_TAIL);
    CHECK(fu2_lc_outcome_of(&lc) == LCO_COMPLETE && lc.integrity == LCI_OK);

    // 2. Hypothetical stale GET is recorded; lifecycle does not judge values.
    lc_armed(&lc, &f, cfg);
    e = vget(-1024);  fu2_lc_on_event(&lc, &e, T0 + SEC);
    e = vset(-15360); fu2_lc_on_event(&lc, &e, T0 + SEC);
    e = vget(-1024);  fu2_lc_on_event(&lc, &e, T0 + SEC); // stale
    CHECK(lc.events == 3 && lc.state == LC_OBSERVING);

    // 3. Boot-style sticky rejection and maximum fallback; no restore.
    lc_armed(&lc, &f, cfg);
    e = vget(-1024);  fu2_lc_on_event(&lc, &e, T0 + SEC);
    e = vset(-15360); fu2_lc_on_event(&lc, &e, T0 + SEC);
    e = vget(-1024);  fu2_lc_on_event(&lc, &e, T0 + SEC);
    e = vset(0);      fu2_lc_on_event(&lc, &e, T0 + SEC);   // 0 dB fallback attempt
    CHECK(lc.events == 4 && lc.state == LC_OBSERVING);
    fu2_lc_tick(&lc, T0 + 12 * SEC);
    CHECK(fu2_lc_outcome_of(&lc) == LCO_COMPLETE);

    // 4. Failed final restoration stops immediately and invalidates the trace.
    lc_armed(&lc, &f, cfg);
    e = vget(-1024);  fu2_lc_on_event(&lc, &e, T0 + SEC);
    e = vset(-1024);  e.result = -71; e.data_len = 0;
    fu2_lc_on_event(&lc, &e, T0 + SEC);
    CHECK(lc.state == LC_DONE && lc.reason == LCR_TARGET_ERROR);
    CHECK(lc.target_verdict == CORE_ERROR && fu2_lc_outcome_of(&lc) == LCO_INVALID);

    // 5. Missing first target event -> deadline; no data.
    lc_armed(&lc, &f, cfg);
    fu2_lc_tick(&lc, T0 + 89 * SEC);
    CHECK(!fu2_lc_done(&lc));
    fu2_lc_tick(&lc, T0 + 90 * SEC);
    CHECK(lc.reason == LCR_NO_FIRST_EVENT && fu2_lc_outcome_of(&lc) == LCO_NO_DATA);

    // 6. Missing return event: a pending record remains after detach.
    lc_armed(&lc, &f, cfg);
    e = vget(-1024); fu2_lc_on_event(&lc, &e, T0 + SEC);
    f.pending = 1;
    fu2_lc_tick(&lc, T0 + 12 * SEC);
    CHECK(lc.integrity == LCI_INCOMPLETE && fu2_lc_outcome_of(&lc) == LCO_INVALID);

    // 7. Lost BPF event, observed live and at final accounting.
    lc_armed(&lc, &f, cfg);
    e = vget(-1024); fu2_lc_on_event(&lc, &e, T0 + SEC);
    fu2_lc_on_lost(&lc, 1);
    CHECK(lc.reason == LCR_LOSS && fu2_lc_outcome_of(&lc) == LCO_INVALID);
    lc_armed(&lc, &f, cfg);
    f.lost = 2;
    e = vget(-1024); fu2_lc_on_event(&lc, &e, T0 + SEC);
    fu2_lc_tick(&lc, T0 + 12 * SEC);
    CHECK(lc.integrity == LCI_LOSS && fu2_lc_outcome_of(&lc) == LCO_INVALID);

    // 8. Malformed GET payload.
    lc_armed(&lc, &f, cfg);
    e = vget(-1024); e.data_len = 1;
    fu2_lc_on_event(&lc, &e, T0 + SEC);
    CHECK(lc.reason == LCR_TARGET_ERROR && lc.target_verdict == CORE_BAD_DATA);
    lc_armed(&lc, &f, cfg);
    e = vget(-1024); e.data_error = 1;
    fu2_lc_on_event(&lc, &e, T0 + SEC);
    CHECK(lc.target_verdict == CORE_BAD_DATA);

    // 9. Short transfer.
    lc_armed(&lc, &f, cfg);
    e = vget(-1024); e.result = 1;
    fu2_lc_on_event(&lc, &e, T0 + SEC);
    CHECK(lc.reason == LCR_TARGET_ERROR && lc.target_verdict == CORE_SHORT);

    // 10. Negative USB completion.
    lc_armed(&lc, &f, cfg);
    e = vget(-1024); e.result = -110; e.data_len = 0;
    fu2_lc_on_event(&lc, &e, T0 + SEC);
    CHECK(lc.reason == LCR_TARGET_ERROR && lc.target_verdict == CORE_ERROR);

    // 11. Event limit (small configured cap, and the default 256).
    {
        struct fu2_lc_config small = cfg;
        small.max_events = 3;
        lc_armed(&lc, &f, small);
        for (i = 0; i < 5; ++i) {
            e = vget(-1024);
            fu2_lc_on_event(&lc, &e, T0 + SEC);
        }
        CHECK(lc.reason == LCR_EVENT_CAP && lc.events == 3);
        CHECK(fu2_lc_outcome_of(&lc) == LCO_TRUNCATED);
    }
    lc_armed(&lc, &f, cfg);
    for (i = 0; i < 300; ++i) {
        e = vget(-1024);
        fu2_lc_on_event(&lc, &e, T0 + SEC);
    }
    CHECK(lc.reason == LCR_EVENT_CAP && lc.events == 256);
    CHECK(f.detach_calls == 2); // late events after DONE caused nothing
    // Over-cap suppression is counted separately and is benign after a cap stop...
    lc_armed(&lc, &f, cfg);
    f.overcap = 3;
    for (i = 0; i < 256; ++i) {
        e = vget(-1024);
        fu2_lc_on_event(&lc, &e, T0 + SEC);
    }
    CHECK(lc.reason == LCR_EVENT_CAP && lc.integrity == LCI_OK && lc.overcap_lost == 3);
    CHECK(fu2_lc_outcome_of(&lc) == LCO_TRUNCATED);
    // ...whereas any genuine loss at the same moment stays invalidating, even at the cap.
    lc_armed(&lc, &f, cfg);
    f.overcap = 3;
    f.lost = 1;
    for (i = 0; i < 256; ++i) {
        e = vget(-1024);
        fu2_lc_on_event(&lc, &e, T0 + SEC);
    }
    CHECK(lc.reason == LCR_EVENT_CAP && lc.integrity == LCI_LOSS);
    CHECK(fu2_lc_outcome_of(&lc) == LCO_INVALID);
    // A cap stop with pending (missing return) records remains incomplete.
    lc_armed(&lc, &f, cfg);
    f.pending = 1;
    for (i = 0; i < 256; ++i) {
        e = vget(-1024);
        fu2_lc_on_event(&lc, &e, T0 + SEC);
    }
    CHECK(lc.integrity == LCI_INCOMPLETE && fu2_lc_outcome_of(&lc) == LCO_INVALID);
    // 12. Time deadlines: hard limit after first event; total limit; clock regress.
    lc_armed(&lc, &f, cfg);
    e = vget(-1024); fu2_lc_on_event(&lc, &e, T0 + SEC);
    for (i = 1; i <= 24; ++i) { // keep traffic alive every 5 s
        e = vget(-1024);
        fu2_lc_on_event(&lc, &e, T0 + SEC + (uint64_t)i * 5 * SEC);
        fu2_lc_tick(&lc, T0 + SEC + (uint64_t)i * 5 * SEC);
    }
    CHECK(lc.state == LC_DONE && lc.reason == LCR_HARD_DEADLINE);
    CHECK(fu2_lc_outcome_of(&lc) == LCO_TRUNCATED);
    lc_armed(&lc, &f, cfg);
    e = vget(-1024); fu2_lc_on_event(&lc, &e, T0 + 100 * SEC); // late first event
    fu2_lc_tick(&lc, T0 + 180 * SEC);
    CHECK(lc.reason == LCR_TOTAL_DEADLINE);
    lc_armed(&lc, &f, cfg);
    fu2_lc_tick(&lc, T0 - 1);
    CHECK(lc.state == LC_DONE && lc.reason == LCR_TOTAL_DEADLINE);

    // 13. Second device.
    lc_armed(&lc, &f, cfg);
    e = vget(-1024); fu2_lc_on_event(&lc, &e, T0 + SEC);
    e = vget(-1024); e.devnum = 9; fu2_lc_on_event(&lc, &e, T0 + SEC);
    CHECK(lc.reason == LCR_SECOND_DEVICE && fu2_lc_outcome_of(&lc) == LCO_INVALID);

    // Ordering proof: module already present after attach, or traffic before proof.
    lc_new(&lc, &f, cfg);
    fu2_lc_attached(&lc, T0, 1);
    CHECK(fu2_lc_verify_window(&lc, 1) != 0 && lc.reason == LCR_MISSED_WINDOW);
    lc_new(&lc, &f, cfg);
    fu2_lc_attached(&lc, T0, 1);
    e = vget(-1024); fu2_lc_on_event(&lc, &e, T0 + SEC); // before verify_window
    CHECK(lc.reason == LCR_MISSED_WINDOW && fu2_lc_outcome_of(&lc) == LCO_INVALID);
    lc_new(&lc, &f, cfg);
    CHECK(fu2_lc_attached(&lc, T0, 0) != 0 && lc.reason == LCR_ATTACH_FAILED);

    // 14-15, 17. Reader process: clean exit, crash, SIGTERM, hung + SIGKILL.
    {
        int r;
        int st;
        pid_t pid = spawn_reader(0, &r);
        waitpid(pid, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0 && fd_closed_by_peer(r, 200));
        close(r);

        pid = spawn_reader(1, &r); // 14. reader crash: no cleanup code runs
        waitpid(pid, &st, 0);
        CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL && fd_closed_by_peer(r, 200));
        close(r);

        pid = spawn_reader(2, &r); // cooperative SIGTERM
        usleep(50000);
        kill(pid, SIGTERM);
        waitpid(pid, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0 && fd_closed_by_peer(r, 200));
        close(r);

        pid = spawn_reader(3, &r); // 15. hung reader ignores SIGTERM
        usleep(50000);
        kill(pid, SIGTERM);
        usleep(100000);
        CHECK(!fd_closed_by_peer(r, 20)); // still holding its "link"
        kill(pid, SIGKILL);               // supervisory forced shutdown
        waitpid(pid, &st, 0);
        CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL && fd_closed_by_peer(r, 200));
        close(r);
    }
    lc_armed(&lc, &f, cfg);
    fu2_lc_stop(&lc, LCR_SUPERVISOR);
    CHECK(lc.reason == LCR_SUPERVISOR && lc.state == LC_DONE);
    CHECK(fu2_lc_outcome_of(&lc) == LCO_NO_DATA);

    // 16. Cleanup failures: each step, no retry, state CLEANUP_FAILED.
    for (i = 0; i < 5; ++i) {
        lc_armed(&lc, &f, cfg);
        f.fail_detach = i == 0; f.fail_counters = i == 1; f.fail_reader = i == 2;
        f.fail_maps = i == 3; f.fail_verify = i == 4;
        e = vget(-1024); fu2_lc_on_event(&lc, &e, T0 + SEC);
        fu2_lc_stop(&lc, LCR_SIGNAL);
        // A failed detach/reader/map release also leaves residue, so verification
        // is expected to report it as well; the primary failing step must be set.
        CHECK(lc.state == LC_CLEANUP_FAILED && (lc.fail_mask & (1u << i)) != 0);
        CHECK(lc.cleanup_runs == 1 && fu2_lc_outcome_of(&lc) == LCO_INVALID);
    }
    lc_armed(&lc, &f, cfg);
    f.pins = 1; // a pinned object left behind is detected by verification
    fu2_lc_stop(&lc, LCR_SIGNAL);
    CHECK(lc.state == LC_CLEANUP_FAILED && (lc.fail_mask & LC_FAIL_VERIFY));

    // 17. Repeated stop requests: one cleanup, first reason wins.
    lc_armed(&lc, &f, cfg);
    fu2_lc_stop(&lc, LCR_SIGNAL);
    fu2_lc_stop(&lc, LCR_SUPERVISOR);
    fu2_lc_stop(&lc, LCR_NORMAL_TAIL);
    CHECK(lc.stop_requests == 3 && lc.cleanup_runs == 1 && lc.reason == LCR_SIGNAL);
    CHECK(f.detach_calls == 2 && f.counter_reads == 1);

    // 18. No automatic retry: a finished lifecycle refuses to attach or register again.
    CHECK(fu2_lc_attached(&lc, T0 + 500 * SEC, 1) != 0);
    CHECK(fu2_lc_register_link(&lc, 3) != 0);
    CHECK(lc.cleanup_runs == 1 && fu2_lc_done(&lc));
    fu2_lc_tick(&lc, T0 + 900 * SEC);
    CHECK(lc.cleanup_runs == 1);

    // 19. Never detaches unrelated programs; only registered ids are passed on.
    lc_armed(&lc, &f, cfg);
    fu2_lc_stop(&lc, LCR_SIGNAL);
    CHECK(f.unowned_detach_attempts == 0);
    CHECK(f.present[6] == 1 && f.present[7] == 1);
    CHECK(f.present[1] == 0 && f.present[2] == 0);

    // 20. No persistent experiment state after successful cleanup.
    CHECK(f.reader_open == 0 && f.maps_alive == 0 && f.pins == 0);
    CHECK(lc.state == LC_DONE && lc.fail_mask == 0 && lc.integrity == LCI_OK);

    printf("fu2_boot_lifecycle: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
