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
    __type(value, struct pending_transfer);
} inflight SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} dropped SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} emitted SEC(".maps");

struct pending_transfer {
    __u64 submit_ns;
    __u64 device_ptr;
    __u16 value;
    __u16 index;
    __u16 size;
    __u8 request;
    __u8 request_type;
    __u8 data_len;
    __u8 capture_error;
    __u8 data[8];
};

static __always_inline void mark_loss(void)
{
    __u32 zero = 0;
    __u64 *lost = bpf_map_lookup_elem(&dropped, &zero);
    if (lost)
        __sync_fetch_and_add(lost, 1);
}

static __always_inline int is_target(struct usb_device *dev, __u8 request,
                                     __u8 request_type, __u16 value, __u16 index)
{
    __u16 vendor = 0, product = 0;
    __u8 selector = value >> 8;

    if (!dev || index != FEATURE_UNIT2_INTERFACE0 ||
        (selector != VOLUME_SELECTOR && selector != MUTE_SELECTOR) ||
        (request_type != 0xa1 && request_type != 0x21) ||
        (request != UAC_CUR && request != UAC_RANGE) ||
        (request == UAC_RANGE &&
         (request_type != 0xa1 || selector != VOLUME_SELECTOR)))
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
    __u64 key;
    struct pending_transfer pending = {};
    if (!is_target(dev, request, request_type, value, index))
        return 0;
    key = bpf_get_current_pid_tgid();
    pending.submit_ns = bpf_ktime_get_ns();
    pending.device_ptr = (__u64)dev;
    pending.value = value;
    pending.index = index;
    pending.size = size;
    pending.request = request;
    pending.request_type = request_type;
    // OUT data belongs to the caller at fentry. Capture the attempted value
    // before usb_control_msg() and keep it even if the transfer later fails.
    if (request_type == 0x21) {
        if (data && (value >> 8) == VOLUME_SELECTOR && size == 2) {
            if (!bpf_probe_read_kernel(pending.data, 2, data))
                pending.data_len = 2;
            else
                pending.capture_error = 1;
        } else if (data && (value >> 8) == MUTE_SELECTOR && size == 1) {
            if (!bpf_probe_read_kernel(pending.data, 1, data))
                pending.data_len = 1;
            else
                pending.capture_error = 1;
        } else {
            pending.capture_error = 1;
        }
    }
    if (bpf_map_update_elem(&inflight, &key, &pending, BPF_NOEXIST))
        mark_loss(); // Nested call, stale TID, or map full: never overwrite.
    return 0;
}

SEC("fexit/snd_usb_ctl_msg")
int BPF_PROG(fu2_complete, struct usb_device *dev, unsigned int pipe,
             __u8 request, __u8 request_type, __u16 value, __u16 index,
             void *data, __u16 size, int result)
{
    struct fu2_event *event;
    __u64 key, now;
    __u32 zero = 0;
    __u64 *count;
    struct pending_transfer *pending;

    if (!is_target(dev, request, request_type, value, index))
        return 0;

    key = bpf_get_current_pid_tgid();
    now = bpf_ktime_get_ns();
    pending = bpf_map_lookup_elem(&inflight, &key);
    if (!pending || pending->device_ptr != (__u64)dev ||
        pending->request != request || pending->request_type != request_type ||
        pending->value != value || pending->index != index ||
        pending->size != size) {
        mark_loss();
        bpf_map_delete_elem(&inflight, &key);
        return 0;
    }
    count = bpf_map_lookup_elem(&emitted, &zero);
    if (!count || __sync_fetch_and_add(count, 1) >= 256) {
        mark_loss();
        bpf_map_delete_elem(&inflight, &key);
        return 0;
    }
    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event) {
        mark_loss();
        bpf_map_delete_elem(&inflight, &key);
        return 0;
    }
    __builtin_memset(event, 0, sizeof(*event));
    event->submit_ns = pending->submit_ns;
    event->complete_ns = now;
    event->entry_seen = 1;
    event->capture_error = pending->capture_error;
    event->data_len = pending->data_len;
    __builtin_memcpy(event->data, pending->data, sizeof(event->data));
    event->vendor = CONNECT2_VENDOR;
    event->product = CONNECT2_PRODUCT;
    event->busnum = BPF_CORE_READ(dev, bus, busnum);
    event->devnum = BPF_CORE_READ(dev, devnum);
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
    if (request_type == 0xa1 && data && result >= 0) {
        if ((value >> 8) == VOLUME_SELECTOR && request == UAC_RANGE &&
            request_type == 0xa1 && size >= 8 && result >= 8) {
            if (!bpf_probe_read_kernel(event->data, 8, data))
                event->data_len = 8;
            else
                event->capture_error = 1;
        } else if ((value >> 8) == VOLUME_SELECTOR && size >= 2 &&
                   result >= 2) {
            if (!bpf_probe_read_kernel(event->data, 2, data))
                event->data_len = 2;
            else
                event->capture_error = 1;
        } else if ((value >> 8) == MUTE_SELECTOR && size >= 1 &&
                   result >= 1) {
            if (!bpf_probe_read_kernel(event->data, 1, data))
                event->data_len = 1;
            else
                event->capture_error = 1;
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
