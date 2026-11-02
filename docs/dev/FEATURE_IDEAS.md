# Feature ideas (not planned yet)

Features the official Windows app does not have but OpenWitty could, collected 2026-11-01. None of them is
scheduled: the owner decides after the private beta week and the final review (`docs/dev/FINAL_REVIEW_PLAN.md`).
Effort: S = small, M = moderate.

| # | Idea | What it gives | Builds on | Effort |
|---|---|---|---|---|
| 1 | **Shortcuts and a control interface** (D-Bus or the service socket) | A mic-mute shortcut and push-to-talk that mute the CONNECT 2 itself (the real privacy mute for every app); KDE shortcuts, scripts or a Stream Deck can switch presets, Dim, outputs | the background service (single device owner) | M |
| 2 | **Mic status in the tray** | Tray icon turns red while the microphone is live; "Mute mic" in its menu | tray + service | S |
| 3 | **Automatic presets per app** | Switches settings when an app starts recording (e.g. Discord: Denoise + Comp; Ardour: processing off) and back afterwards; PipeWire shows which app records | presets, #1 | M |
| 4 | **Undo and change history** | "Undo last change" and a short list ("10:42 gain 39 → 45 dB"), e.g. after an accidental wheel turn on a knob | verified writes (every change is already recorded) | S |
| 5 | **Clipping warning** | A desktop notification when the mic or instrument clips while recording | input meters | S |
| 6 | **Quick mic check** | Records 5 s and plays it back to check the sound before a call | PipeWire | S |

Suggested order if wanted: 2 and 4 (small, noticed daily), then 1 as the base for 3.

Deliberately not on the list: a volume limit (owner decision, 2026-10-31).
