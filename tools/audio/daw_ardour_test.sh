#!/bin/sh
# OpenWitty DAW-1..4: headless Ardour 9 session on the CONNECT 2 (ALSA backend): 4 mono tracks (capture 1-4), one
# stereo track (capture 3/4), 1 kHz -> playback 1 and 2 kHz -> playback 2 at -30 dBFS, records for N seconds.
# Plays audible tones on the device outputs: speakers muted/off, headphones off the head.
# Usage: daw_ardour_test.sh <new-session-dir> [seconds]   then: wavinfo.py <dir>/interchange/*/audiofiles/*.wav
# Environment: DAW_BACKEND=JACK (pipewire-jack) with DAW_IN_PREFIX / DAW_OUT_PREFIX (port-name prefixes of the CONNECT 2
# nodes), DAW_LIST_ONLY=1 (list ports only, no tracks/tones), DAW_BUFFER=<frames>.
set -eu
dir=$1; secs=${2:-60}
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp --suffix=.lua)
{ printf 'IN_DEV = "CONNECT 2"; OUT_DEV = "CONNECT 2"; SESSION_DIR = "%s"; SECONDS = %s; TONECHECK = false\n' "$dir" "$secs"
  printf 'BACKEND = "%s"; BUFFER = %s; LIST_ONLY = %s\n' "${DAW_BACKEND:-ALSA}" "${DAW_BUFFER:-1024}" \
         "$([ -n "${DAW_LIST_ONLY:-}" ] && echo true || echo false)"
  [ -n "${DAW_IN_PREFIX:-}" ] && printf 'IN_PREFIX = "%s"\n' "$DAW_IN_PREFIX"
  [ -n "${DAW_OUT_PREFIX:-}" ] && printf 'OUT_PREFIX = "%s"\n' "$DAW_OUT_PREFIX"
  cat "$here/daw_ardour.lua"; } > "$tmp"
ardour9-lua "$tmp"; rc=$?; rm -f "$tmp"; exit $rc
