// SPDX-License-Identifier: MIT
// Approval-gated, bounded observer. Never sends USB or ALSA requests.
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fu2_writer_trace.skel.h"
#include "fu2_observer_logic.h"

enum { MAX_SYMBOLS = 400000 };

struct symbol {
    unsigned long long address;
    char name[128];
};

static struct symbol *symbols;
static size_t symbol_count;
static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t interrupted;
static struct fu2_observer_state observation;
static bool capture_error;

static void free_reader(void *reader)
{
    ring_buffer__free(reader);
}

static void destroy_probe(void *probe)
{
    fu2_writer_trace_bpf__destroy(probe);
}

// 1 = match, 0 = other value, -2 = absent field, -1 = unreadable sysfs.
static int sysfs_field_matches(const char *entry, const char *field,
                               const char *expected)
{
    char path[384], value[32];
    FILE *input;
    if (snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/%s",
                 entry, field) >= (int)sizeof(path))
        return -1;
    input = fopen(path, "r");
    if (!input)
        return errno == ENOENT ? -2 : -1;
    size_t length = strlen(expected);
    if (!fgets(value, sizeof(value), input)) {
        fclose(input);
        return -1;
    }
    fclose(input);
    return strncmp(value, expected, length) == 0 &&
           (value[length] == '\n' || value[length] == '\0');
}

// Passive sysfs presence test only: no usbfs open or control request.
// Return -1 if sysfs cannot be inspected, never assume absence on error.
static int connect2_present(void)
{
    DIR *directory = opendir("/sys/bus/usb/devices");
    struct dirent *entry;
    int found = 0;
    if (!directory)
        return -1;
    errno = 0;
    while ((entry = readdir(directory))) {
        int vendor = sysfs_field_matches(entry->d_name, "idVendor", "29c2");
        int product = vendor == 1 ?
            sysfs_field_matches(entry->d_name, "idProduct", "0004") : 0;
        if (vendor == -1 || (vendor == 1 && product < 0)) {
            found = -1;
            break;
        }
        if (vendor == 1 && product == 1) {
            found = 1;
            break;
        }
        errno = 0;
    }
    if (!entry && errno)
        found = -1;
    closedir(directory);
    return found;
}

static void stop_on_signal(int signo)
{
    stop_requested = 1;
    if (signo != SIGALRM)
        interrupted = 1;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int compare_symbols(const void *a, const void *b)
{
    const struct symbol *sa = a, *sb = b;
    return (sa->address > sb->address) - (sa->address < sb->address);
}

static void load_symbols(void)
{
    FILE *input = fopen("/proc/kallsyms", "r");
    unsigned long long address;
    char kind, name[128], line[256];
    if (!input)
        return;
    symbols = calloc(MAX_SYMBOLS, sizeof(*symbols));
    if (!symbols) {
        fclose(input);
        return;
    }
    while (symbol_count < MAX_SYMBOLS && fgets(line, sizeof(line), input)) {
        if (sscanf(line, "%llx %c %127s", &address, &kind, name) != 3 ||
            address == 0)
            continue;
        symbols[symbol_count].address = address;
        snprintf(symbols[symbol_count].name,
                 sizeof(symbols[symbol_count].name), "%s", name);
        ++symbol_count;
    }
    fclose(input);
    qsort(symbols, symbol_count, sizeof(*symbols), compare_symbols);
}

static const char *lookup_symbol(unsigned long long address)
{
    size_t low = 0, high = symbol_count;
    if (!address || !symbol_count)
        return "?";
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (symbols[mid].address <= address)
            low = mid + 1;
        else
            high = mid;
    }
    return low ? symbols[low - 1].name : "?";
}

static int receive_event(void *context, void *data, size_t size)
{
    const struct fu2_event *event = data;
    enum fu2_kind kind;
    enum fu2_verdict verdict;
    const char *operation;
    (void)context;
    if (size != sizeof(*event)) {
        capture_error = true;
        stop_requested = 1;
        return -1;
    }
    kind = fu2_kind_of(event);
    verdict = fu2_classify(&observation, event);
    if (verdict == FU2_IGNORE) {
        fprintf(stderr, "Unexpected non-target record; stopping.\n");
        capture_error = true;
        stop_requested = 1;
        return -1;
    }
    operation = kind == FU2_VOLUME_RANGE ? "GET_RANGE" :
                kind == FU2_VOLUME_SET || kind == FU2_MUTE_SET ?
                "SET_CUR" : "GET_CUR";
    printf("t=%.6f %-9s selector=%u channel=%u result=%d requested=%u duration_ms=%.3f task=%s/%u",
           event->complete_ns >= observation.armed_ns ?
           (event->complete_ns - observation.armed_ns) / 1e9 : -1.0, operation,
           event->w_value >> 8, event->w_value & 255, event->result,
           event->requested_len,
           event->submit_ns && event->complete_ns >= event->submit_ns ?
           (event->complete_ns - event->submit_ns) / 1e6 : -1.0,
           event->comm, event->pid);
    if (event->data_len == 2 && (kind == FU2_VOLUME_GET || kind == FU2_VOLUME_SET))
        printf(" %svolume_db=%.2f", kind == FU2_VOLUME_SET ? "attempted_" : "",
               fu2_signed_le16(event->data) / 256.0);
    else if (event->data_len == 1 && (kind == FU2_MUTE_GET || kind == FU2_MUTE_SET))
        printf(" %smute=%u", kind == FU2_MUTE_SET ? "attempted_" : "",
               event->data[0]);
    else if (event->data_len == 8 && kind == FU2_VOLUME_RANGE)
        printf(" range_count=%u min_db=%.2f max_db=%.2f step_db=%.2f",
               (unsigned)event->data[0] | ((unsigned)event->data[1] << 8),
               fu2_signed_le16(event->data + 2) / 256.0,
               fu2_signed_le16(event->data + 4) / 256.0,
               fu2_signed_le16(event->data + 6) / 256.0);
    if (event->request_type == 0x21) {
        printf(" stack=");
        if (event->stack_len <= 0) {
            printf("unavailable");
        } else {
            int depth = event->stack_len / (int)sizeof(event->stack[0]);
            if (depth > 10)
                depth = 10;
            for (int i = 0; i < depth; ++i)
                printf("%s%s", i ? ">" : "", lookup_symbol(event->stack[i]));
        }
    }
    printf(" verdict=%d%s\n", verdict,
           kind == FU2_VOLUME_SET || kind == FU2_MUTE_SET ?
           " (attempt only; physical result unproven)" : "");
    fflush(stdout);
    if (verdict != FU2_OK) {
        fprintf(stderr, "FU2 transfer/capture/correlation error; stopping without recovery.\n");
        capture_error = true;
        stop_requested = 1;
        observation.stopped = true;
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct fu2_writer_trace_bpf *probe = NULL;
    struct ring_buffer *reader = NULL;
    struct sigaction action = { .sa_handler = stop_on_signal };
    uint32_t zero = 0;
    uint64_t dropped = 0;
    int result = 1, poll_result;
    struct fu2_resources resources;

    if (argc == 2 && strcmp(argv[1], "--dry-run") == 0) {
        puts("OFFLINE ONLY: CONNECT 2 Feature Unit 2/interface 0, volume/mute; arm only after passive device-absence check; 30 s reconnect wait, 120 s and 256 event caps. No probe loaded.");
        return 0;
    }
    if (argc != 2 || strcmp(argv[1], "--execute") != 0 || geteuid() != 0) {
        fprintf(stderr, "Requires explicit --execute and root; --dry-run loads nothing.\n");
        return 2;
    }
    if (!fu2_may_arm(connect2_present() == 0,
                     access("/sys/module/snd_usb_audio", F_OK) == 0)) {
        fprintf(stderr, "Module unavailable, target still present, or passive absence check failed; no probe loaded.\n");
        return 1;
    }
    // A disappearing sysfs node can precede the last teardown callback.
    // Require a short stable-absence interval before loading either hook.
    if (sleep(2) != 0 || connect2_present() != 0 ||
        access("/sys/module/snd_usb_audio", F_OK) != 0) {
        fprintf(stderr, "Target absence/module state did not remain stable; no probe loaded.\n");
        return 1;
    }
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGALRM, &action, NULL);
    load_symbols();
    probe = fu2_writer_trace_bpf__open_and_load();
    if (!probe) {
        fprintf(stderr, "Could not load filtered BPF probe; do not reconnect.\n");
        goto cleanup;
    }
    if (connect2_present() != 0) {
        fprintf(stderr, "Target appeared before arm; aborting without attach.\n");
        goto cleanup;
    }
    observation.armed_ns = now_ns();
    if (fu2_writer_trace_bpf__attach(probe) != 0) {
        fprintf(stderr, "Could not attach filtered BPF probe; do not reconnect.\n");
        goto cleanup;
    }
    reader = ring_buffer__new(bpf_map__fd(probe->maps.events), receive_event,
                              NULL, NULL);
    if (!reader) {
        fprintf(stderr, "Could not create bounded reader; do not reconnect.\n");
        goto cleanup;
    }
    printf("ARMED: device must already be absent; manual reconnect only on explicit go-ahead; cap %u s/%u events, %u s wait for first target event.\n",
           FU2_MAX_SECONDS, FU2_MAX_EVENTS, FU2_RECONNECT_WAIT_SECONDS);
    fflush(stdout);
    alarm(FU2_MAX_SECONDS);
    result = 0;
    while (!stop_requested && observation.events < FU2_MAX_EVENTS &&
           !fu2_deadline_reached(&observation, now_ns())) {
        poll_result = ring_buffer__poll(reader, 200);
        if (poll_result < 0 && poll_result != -EINTR) {
            fprintf(stderr, "Ring-buffer error %d; stopping.\n", poll_result);
            result = 1;
            break;
        }
        if (bpf_map_lookup_elem(bpf_map__fd(probe->maps.dropped), &zero,
                                &dropped) != 0 || fu2_has_loss(dropped)) {
            fprintf(stderr, "Filtered ring-buffer loss or counter failure; stopping.\n");
            result = 1;
            break;
        }
    }
    if (!observation.device_seen) {
        fprintf(stderr, "No target FU2 event before reconnect wait deadline; stopping.\n");
        result = 1;
    }
    if (bpf_map_lookup_elem(bpf_map__fd(probe->maps.dropped), &zero, &dropped) != 0)
        result = 1;
    if (fu2_has_loss(dropped)) {
        fprintf(stderr, "Lost %llu filtered events; attribution is incomplete.\n",
                (unsigned long long)dropped);
        result = 1;
    }
    if (capture_error)
        result = 1;
    if (interrupted) {
        fprintf(stderr, "Interrupted before bounded completion; attribution is incomplete.\n");
        result = 1;
    }
    printf("STOPPED: events=%u dropped=%llu status=%s\n", observation.events,
           (unsigned long long)dropped, result ? "incomplete" : "complete");
cleanup:
    alarm(0);
    resources.reader = reader;
    resources.probe = probe;
    fu2_cleanup_resources(&resources, free_reader, destroy_probe);
    free(symbols);
    return result;
}
