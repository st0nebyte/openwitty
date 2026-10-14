"""Linux control library for the LEWITT CONNECT 2 (clean-room, see PROTOCOL.md)."""
from .protocol import (  # noqa: F401
    MSG_SETTINGS, MSG_STATUS, MSG_VERSION, MSG_TRIGGERS,
    MESSAGES, wvalue_for, settings_decode, q88_to_db, db_to_q88,
)
