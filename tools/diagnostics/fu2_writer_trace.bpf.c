// SPDX-License-Identifier: GPL-2.0-only
// Approval-gated diagnostic: observes only CONNECT 2 UAC2 Feature Unit 2 controls.
#include "vmlinux.h"
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "fu2_writer_trace_shared.h"

#define CONNECT2_VENDOR 0x29c2
#define CONNECT2_PRODUCT 0x0004
#define FEATURE_UNIT2_INTERFACE0 0x0200
#define VOLUME_SELECTOR 2
#define MUTE_SELECTOR 1
#define UAC_CUR 1
#define UAC_RANGE 2

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 18);
} events SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 128);
    __type(key, __u64);
    __type(value, __u64);
} inflight SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} dropped SEC(".maps");

static __always_inline int is_target(struct usb_device *dev, __u8 request,
                                     __u8 request_type, __u16 value, __u16 index)
{
    __u16 vendor = 0, product = 0;
    __u8 selector = value >> 8;

    if (!dev || index != FEATURE_UNIT2_INTERFACE0 ||
        (selector != VOLUME_SELECTOR && selector != MUTE_SELECTOR) ||
        (request_type != 0xa1 && request_type != 0x21) ||
        (request != UAC_CUR && request != UAC_RANGE) ||
        (request == UAC_RANGE && request_type != 0xa1))
        return 0;
    bpf_core_read(&vendor, sizeof(vendor), &dev->descriptor.idVendor);
    bpf_core_read(&product, sizeof(product), &dev->descriptor.idProduct);
    return vendor == CONNECT2_VENDOR && product == CONNECT2_PRODUCT;
}

SEC("fentry/snd_usb_ctl_msg")
int BPF_PROG(fu2_submit, struct usb_device *dev, unsigned int pipe,
             __u8 request, __u8 request_type, __u16 value, __u16 index,
             void *data, __u16 size)
{
    __u64 key, now;
    if (!is_target(dev, request, request_type, value, index))
        return 0;
    key = bpf_get_current_pid_tgid();
    now = bpf_ktime_get_ns();
    bpf_map_update_elem(&inflight, &key, &now, BPF_ANY);
    return 0;
}

SEC("fexit/snd_usb_ctl_msg")
int BPF_PROG(fu2_complete, struct usb_device *dev, unsigned int pipe,
             __u8 request, __u8 request_type, __u16 value, __u16 index,
             void *data, __u16 size, int result)
{
    struct fu2_event *event;
    __u64 key, *started, now;
    __u32 zero = 0;
    __u64 *lost;

    if (!is_target(dev, request, request_type, value, index))
        return 0;

    key = bpf_get_current_pid_tgid();
    now = bpf_ktime_get_ns();
    started = bpf_map_lookup_elem(&inflight, &key);
    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event) {
        lost = bpf_map_lookup_elem(&dropped, &zero);
        if (lost)
            __sync_fetch_and_add(lost, 1);
        bpf_map_delete_elem(&inflight, &key);
        return 0;
    }
    __builtin_memset(event, 0, sizeof(*event));
    event->submit_ns = started ? *started : 0;
    event->complete_ns = now;
    event->pid = key >> 32;
    event->tid = (__u32)key;
    event->result = result;
    event->request_type = request_type;
    event->request = request;
    event->w_value = value;
    event->w_index = index;
    event->requested_len = size;
    bpf_get_current_comm(event->comm, sizeof(event->comm));

    // This buffer is the already copied, short class-control data from
    // snd_usb_ctl_msg(); never access an audio-streaming transfer buffer.
    if (data && result >= 0) {
        if ((value >> 8) == VOLUME_SELECTOR && request == UAC_RANGE &&
            request_type == 0xa1 && size >= 8 && result >= 8) {
            if (!bpf_probe_read_kernel(event->data, 8, data))
                event->data_len = 8;
        } else if ((value >> 8) == VOLUME_SELECTOR && size >= 2 &&
                   result >= 2) {
            if (!bpf_probe_read_kernel(event->data, 2, data))
                event->data_len = 2;
        } else if ((value >> 8) == MUTE_SELECTOR && size >= 1 &&
                   result >= 1) {
            if (!bpf_probe_read_kernel(event->data, 1, data))
                event->data_len = 1;
        }
    }

    if (request_type == 0x21 && request == UAC_CUR)
        event->stack_len = bpf_get_stack(ctx, event->stack,
                                         sizeof(event->stack), 0);
    bpf_ringbuf_submit(event, 0);
    bpf_map_delete_elem(&inflight, &key);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
