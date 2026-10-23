// SPDX-License-Identifier: MIT
// Approval-gated, bounded observer. Never sends USB or ALSA requests.
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fu2_writer_trace.skel.h"
#include "fu2_writer_trace_shared.h"

enum { MAX_SECONDS = 120, MAX_EVENTS = 256, MAX_SYMBOLS = 400000 };

struct symbol {
    unsigned long long address;
    char name[128];
};

static struct symbol *symbols;
static size_t symbol_count;
static volatile sig_atomic_t stop_requested;
static unsigned event_count;
static uint64_t armed_ns;
static bool capture_error;

static void stop_on_signal(int signo)
{
    (void)signo;
    stop_requested = 1;
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

static int16_t signed_le16(const uint8_t *bytes)
{
    return (int16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static int receive_event(void *context, void *data, size_t size)
{
    const struct fu2_event *event = data;
    const char *operation;
    (void)context;
    if (size != sizeof(*event)) {
        capture_error = true;
        stop_requested = 1;
        return -1;
    }
    if (event_count >= MAX_EVENTS)
        return 0;
    ++event_count;
    operation = event->request_type == 0x21 ? "SET_CUR" :
                event->request == 2 ? "GET_RANGE" : "GET_CUR";
    printf("t=%.6f %-9s selector=%u channel=%u result=%d requested=%u duration_ms=%.3f task=%s/%u",
           (event->complete_ns - armed_ns) / 1e9, operation,
           event->w_value >> 8, event->w_value & 255, event->result,
           event->requested_len,
           event->submit_ns ? (event->complete_ns - event->submit_ns) / 1e6 : -1.0,
           event->comm, event->pid);
    if (event->data_len == 2 && (event->w_value >> 8) == 2)
        printf(" volume_db=%.2f", signed_le16(event->data) / 256.0);
    else if (event->data_len == 1 && (event->w_value >> 8) == 1)
        printf(" mute=%u", event->data[0]);
    else if (event->data_len == 8 && (event->w_value >> 8) == 2)
        printf(" range_count=%u min_db=%.2f max_db=%.2f step_db=%.2f",
               (unsigned)event->data[0] | ((unsigned)event->data[1] << 8),
               signed_le16(event->data + 2) / 256.0,
               signed_le16(event->data + 4) / 256.0,
               signed_le16(event->data + 6) / 256.0);
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
    putchar('\n');
    fflush(stdout);
    if (event->result < 0 ||
        (event->result >= 0 && event->data_len == 0)) {
        fprintf(stderr, "FU2 transfer/capture error; stopping without recovery.\n");
        capture_error = true;
        stop_requested = 1;
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

    if (argc == 2 && strcmp(argv[1], "--dry-run") == 0) {
        puts("OFFLINE ONLY: CONNECT 2 29c2:0004, UAC2 Feature Unit 2/interface 0, selectors volume/mute; 120 s and 256 event caps. No probe loaded.");
        return 0;
    }
    if (argc != 2 || strcmp(argv[1], "--execute") != 0 || geteuid() != 0) {
        fprintf(stderr, "Requires explicit --execute and root; --dry-run loads nothing.\n");
        return 2;
    }
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    load_symbols();
    probe = fu2_writer_trace_bpf__open_and_load();
    if (!probe) {
        fprintf(stderr, "Could not load filtered BPF probe; do not reconnect.\n");
        goto cleanup;
    }
    armed_ns = now_ns();
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
    printf("ARMED: manual reconnect only on explicit go-ahead; hard cap %u s or %u events.\n",
           MAX_SECONDS, MAX_EVENTS);
    fflush(stdout);
    result = 0;
    while (!stop_requested && event_count < MAX_EVENTS &&
           now_ns() - armed_ns < (uint64_t)MAX_SECONDS * 1000000000ULL) {
        poll_result = ring_buffer__poll(reader, 200);
        if (poll_result < 0 && poll_result != -EINTR) {
            fprintf(stderr, "Ring-buffer error %d; stopping.\n", poll_result);
            result = 1;
            break;
        }
        if (bpf_map_lookup_elem(bpf_map__fd(probe->maps.dropped), &zero,
                                &dropped) != 0 || dropped != 0) {
            fprintf(stderr, "Filtered ring-buffer loss or counter failure; stopping.\n");
            result = 1;
            break;
        }
    }
    if (bpf_map_lookup_elem(bpf_map__fd(probe->maps.dropped), &zero, &dropped) != 0)
        result = 1;
    if (dropped) {
        fprintf(stderr, "Lost %llu filtered events; attribution is incomplete.\n",
                (unsigned long long)dropped);
        result = 1;
    }
    if (capture_error)
        result = 1;
    printf("STOPPED: events=%u dropped=%llu status=%s\n", event_count,
           (unsigned long long)dropped, result ? "incomplete" : "complete");
cleanup:
    ring_buffer__free(reader);
    fu2_writer_trace_bpf__destroy(probe); // Detaches experiment-owned links.
    free(symbols);
    return result;
}
