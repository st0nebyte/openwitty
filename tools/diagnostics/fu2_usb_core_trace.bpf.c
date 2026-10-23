// SPDX-License-Identifier: GPL-2.0-only
// Offline prototype. No loader or boot-time attachment is installed.
#include "vmlinux.h"
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "fu2_usb_core_shared.h"
#include "fu2_usb_core_logic.h"

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 128);
    __type(key, __u64);
    __type(value, struct fu2_core_pending);
} pending SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 16);
} events SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} lost SEC(".maps"), emitted SEC(".maps");

static __always_inline void mark_loss(void)
{
    __u32 zero = 0;
    __u64 *count = bpf_map_lookup_elem(&lost, &zero);
    if (count)
        __sync_fetch_and_add(count, 1);
}

static __always_inline int target(struct usb_device *dev, unsigned int pipe,
                                  __u8 request, __u8 type, __u16 value,
                                  __u16 index, __u16 size)
{
    __u16 vendor = 0, product = 0;
    if (!dev || !fu2_core_fields(pipe, request, type, value, index, size))
        return 0;
    bpf_core_read(&vendor, sizeof(vendor), &dev->descriptor.idVendor);
    bpf_core_read(&product, sizeof(product), &dev->descriptor.idProduct);
    return fu2_core_identity(vendor, product);
}

SEC("fentry/usb_control_msg")
int BPF_PROG(core_entry, struct usb_device *dev, unsigned int pipe,
             __u8 request, __u8 type, __u16 value, __u16 index,
             void *data, __u16 size, int timeout)
{
    struct fu2_core_pending item = {};
    __u64 key;
    (void)ctx;
    (void)timeout;
    if (!target(dev, pipe, request, type, value, index, size))
        return 0;
    key = bpf_get_current_pid_tgid();
    item.entry_ns = bpf_ktime_get_ns();
    item.device_ptr = (__u64)dev;
    item.data_ptr = (__u64)data;
    item.value = value;
    item.index = index;
    item.size = size;
    item.request = request;
    item.request_type = type;
    // USB core receives a kmemdup'd buffer from snd_usb_ctl_msg().
    // Capture attempted OUT value before the synchronous transfer changes it.
    if (type == 0x21) {
        int failed = 1;
        if (data && size == 1)
            failed = bpf_probe_read_kernel(item.data, 1, data);
        else if (data && size == 2)
            failed = bpf_probe_read_kernel(item.data, 2, data);
        if (failed)
            item.data_error = 1;
        else
            item.data_len = size;
    }
    if (bpf_map_update_elem(&pending, &key, &item, BPF_NOEXIST))
        mark_loss(); // Nested call/map exhaustion: never overwrite a record.
    return 0;
}

SEC("fexit/usb_control_msg")
int BPF_PROG(core_exit, struct usb_device *dev, unsigned int pipe,
             __u8 request, __u8 type, __u16 value, __u16 index,
             void *data, __u16 size, int timeout, int result)
{
    __u64 key, now;
    __u32 zero = 0;
    __u64 *count;
    struct fu2_core_pending *item;
    struct fu2_core_event *event;
    (void)ctx;
    (void)timeout;
    if (!target(dev, pipe, request, type, value, index, size))
        return 0;
    key = bpf_get_current_pid_tgid();
    now = bpf_ktime_get_ns();
    item = bpf_map_lookup_elem(&pending, &key);
    if (!fu2_core_matches_pending(item, (__u64)dev, (__u64)data,
                                   request, type, value, index, size)) {
        mark_loss();
        bpf_map_delete_elem(&pending, &key);
        return 0;
    }
    count = bpf_map_lookup_elem(&emitted, &zero);
    if (!count || __sync_fetch_and_add(count, 1) >= FU2_CORE_MAX_EVENTS) {
        mark_loss();
        bpf_map_delete_elem(&pending, &key);
        return 0;
    }
    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event) {
        mark_loss();
        bpf_map_delete_elem(&pending, &key);
        return 0;
    }
    __builtin_memset(event, 0, sizeof(*event));
    event->entry_ns = item->entry_ns;
    event->exit_ns = now;
    event->correlation_id = key;
    event->result = result;
    event->value = value;
    event->index = index;
    event->requested_len = size;
    event->request_type = type;
    event->request = request;
    event->busnum = BPF_CORE_READ(dev, bus, busnum);
    event->devnum = BPF_CORE_READ(dev, devnum);
    event->data_len = item->data_len;
    event->data_error = item->data_error;
    __builtin_memcpy(event->data, item->data, sizeof(event->data));
    if (type == 0xa1 && result == size) {
        int failed = 1;
        if (data && size == 1)
            failed = bpf_probe_read_kernel(event->data, 1, data);
        else if (data && size == 2)
            failed = bpf_probe_read_kernel(event->data, 2, data);
        else if (data && size == 8)
            failed = bpf_probe_read_kernel(event->data, 8, data);
        if (failed)
            event->data_error = 1;
        else
            event->data_len = size;
    }
    bpf_ringbuf_submit(event, 0);
    bpf_map_delete_elem(&pending, &key);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
