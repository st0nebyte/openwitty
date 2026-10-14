"""USB transport (read-only in this release).

usbfs only accepts interface-recipient control requests for interfaces the
calling process has claimed, and the AudioControl interface is owned by
snd-usb-audio. `read_message` therefore REFUSES to run unless the caller
explicitly allows detaching the kernel driver (which drops the sound card
for a moment). See PROTOCOL.md 'Linux access problem'.
"""
from __future__ import annotations

import usb.core
import usb.util

from . import protocol as P


class DeviceNotFound(RuntimeError):
    pass


def find_devices():
    return [d for d in usb.core.find(find_all=True, idVendor=P.LEWITT_VID)]


def describe(dev) -> dict:
    out = {"vid": dev.idVendor, "pid": dev.idProduct, "bus": dev.bus, "address": dev.address}
    for field, idx in (("manufacturer", dev.iManufacturer), ("product", dev.iProduct),
                       ("serial", dev.iSerialNumber)):
        try:
            out[field] = usb.util.get_string(dev, idx) if idx else None
        except Exception as e:  # permission / no string
            out[field] = f"<unreadable: {e}>"
    ifaces = []
    try:
        for cfg in dev:
            for i in cfg:
                ifaces.append({"number": i.bInterfaceNumber, "alt": i.bAlternateSetting,
                               "class": i.bInterfaceClass, "subclass": i.bInterfaceSubClass,
                               "protocol": i.bInterfaceProtocol})
    except Exception as e:
        ifaces.append({"error": str(e)})
    out["interfaces"] = ifaces
    return out


def audio_control_interface(dev):
    """Return (interface_number, bcdADC) of the AudioControl interface."""
    for cfg in dev:
        for i in cfg:
            if i.bInterfaceClass == 1 and i.bInterfaceSubClass == 1:
                bcd = None
                extra = bytes(i.extra_descriptors)
                if len(extra) >= 5 and extra[1] == 0x24 and extra[2] == 0x01:
                    bcd = extra[3] | (extra[4] << 8)
                return i.bInterfaceNumber, bcd
    raise DeviceNotFound("no AudioControl interface")


def read_message(dev, msg_id: int, *, allow_detach_kernel_driver: bool = False) -> bytes:
    info = P.MESSAGES[msg_id]
    if not info.readable:
        raise ValueError(f"{info.name} is write-only")
    if not allow_detach_kernel_driver:
        raise PermissionError(
            "reading requires claiming the AudioControl interface, which detaches "
            "snd-usb-audio and briefly removes the sound card; pass "
            "allow_detach_kernel_driver=True to proceed")
    intf, bcd = audio_control_interface(dev)
    uac1 = bcd is not None and bcd < 0x0200
    req = P.REQ_UAC1_GET_MEM if uac1 else P.REQ_UAC2_MEM
    reattach = False
    if dev.is_kernel_driver_active(intf):
        dev.detach_kernel_driver(intf)
        reattach = True
    try:
        usb.util.claim_interface(dev, intf)
        data = dev.ctrl_transfer(P.BM_GET, req, P.wvalue_for(msg_id),
                                 P.windex_for(intf), info.size, timeout=500)
        return bytes(data)
    finally:
        try:
            usb.util.release_interface(dev, intf)
        finally:
            if reattach:
                dev.attach_kernel_driver(intf)
