"""Wire-format definitions for the CONNECT 2 control protocol.

Everything here was derived from disassembly of the official macOS Control
Center 5.2.2 (symbols `LWT::Connect2::APIv1::*`). See PROTOCOL.md for the
evidence behind each constant. Nothing in this module talks to hardware.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

LEWITT_VID = 0x29C2
# Product IDs referenced by Connect2::defaultConf in the official app.
# UNVERIFIED which PID is which mode until a device is attached.
CONNECT2_PID_CANDIDATES = (0x0002, 0x0004, 0x000A)

AC_UNIT_ID = 0x33          # high byte of wIndex
API_MAJOR = 1              # high byte of wValue

MSG_SETTINGS = 0           # R/W, 0x80 bytes
MSG_STATUS = 2             # R,    0x40 bytes
MSG_VERSION = 3            # R,    0x40 bytes
MSG_TRIGGERS = 4           # W,    0x40 bytes


@dataclass(frozen=True)
class MessageInfo:
    name: str
    msg_id: int
    size: int
    readable: bool
    writable: bool


MESSAGES = {
    MSG_SETTINGS: MessageInfo("iface_settings_t", MSG_SETTINGS, 0x80, True, True),
    MSG_STATUS: MessageInfo("iface_status_t", MSG_STATUS, 0x40, True, False),
    MSG_VERSION: MessageInfo("iface_version_t", MSG_VERSION, 0x40, True, False),
    MSG_TRIGGERS: MessageInfo("iface_triggers_t", MSG_TRIGGERS, 0x40, False, True),
}


def wvalue_for(msg_id: int) -> int:
    """wValue = (apiMajorVersion << 8) | messageId."""
    return (API_MAJOR << 8) | msg_id


def windex_for(interface: int) -> int:
    """wIndex = (unit id 0x33 << 8) | AudioControl interface number."""
    return (AC_UNIT_ID << 8) | (interface & 0xFF)


# bmRequestType / bRequest, UAC2 'MEM' vs UAC1 GET_MEM/SET_MEM
BM_GET = 0xA1   # device-to-host, class, interface
BM_SET = 0x21   # host-to-device, class, interface
REQ_UAC2_MEM = 0x03
REQ_UAC1_GET_MEM = 0x85
REQ_UAC1_SET_MEM = 0x05


def q88_to_db(raw: int) -> float:
    return raw / 256.0


def db_to_q88(db: float) -> int:
    return int(round(db * 256.0))


# --- settings struct (0x80 bytes, little-endian, packed) -------------------
# Offsets are exact (derived from getter/setter code). Names are the app's
# own property identifiers; semantic grouping is marked in PROTOCOL.md.
def _i16(b: bytes, o: int) -> int:
    return struct.unpack_from("<h", b, o)[0]


def settings_decode(raw: bytes) -> dict:
    if len(raw) != MESSAGES[MSG_SETTINGS].size:
        raise ValueError(f"settings blob must be 0x80 bytes, got {len(raw):#x}")
    b = raw
    return {
        "input1": {
            "gain_db": q88_to_db(_i16(b, 0)),     # clamp 0..85 dB when writing
            "mute": bool(b[2]),
            "high_pass": bool(b[3]),
            "clipguard": bool(b[4]),
            "phantom_power": bool(b[5]),
            "preamp_type": b[6],                  # 0 clean, 1 warm, 2 vivid
            "denoise": bool(b[7]),
            "compress": bool(b[9]),
        },
        "input2": {
            "gain_db": q88_to_db(_i16(b, 10)),
            "mute": bool(b[12]),
            "high_pass": bool(b[13]),
            "clipguard": bool(b[14]),
            "preamp_type": b[15],
            "denoise": bool(b[16]),
            "compress": bool(b[18]),
        },
        "outputs": {
            "mute_at_19": bool(b[19]),
            "volume_at_20_db": q88_to_db(_i16(b, 20)),
            "mute_at_22": bool(b[22]),
            "volume_at_27_db": q88_to_db(_i16(b, 27)),
            "mute_at_29": bool(b[29]),
            "dim": bool(b[32]),
            "mono": bool(b[33]),
            "balance": -_i16(b, 35) / 256.0,
            "monitor_flag_34": bool(b[34]),
        },
        "device": {
            "output_source_23": b[23],
            "flag_31": bool(b[31]),
            "high_volume_speaker_24": bool(b[24]),
            "native_volume_mapping_59": b[59],
            "flag_57": bool(b[57]),
            "mobile_streaming_67": bool(b[67]),
        },
        "_raw": raw.hex(),
    }
