# Testing

`ctest --test-dir build` — **90 CTest entries**: 87 GoogleTest cases in `lewitt_tests` (protocol, session, outputs, recovered findings, hardening), `gui_integration` (22 GUI/backend cases, offscreen, simulated device), `visual_geometry`, and 1 legacy Python test.

| Suite | Kind | Hardware-verified? |
|---|---|---|
| Types/Q88/Settings/Status/Trigger (`test_protocol.cpp`) | byte vectors hand-built from disassembled offsets/encodings (not from the encoder) | **No** — derived from static analysis |
| Session / Mock / UsbTransport policy (`test_session.cpp`) | simulated: timeout, short read, disconnect/replug, stall, size checks, write-refusal | n/a (simulated) |
| USB error taxonomy (EACCES/EBUSY/EPIPE mapping), stall≠disconnect, failed-commit keeps edit, real-policy write refusal, unknown enum round-trip, field disjointness | simulated / unit | No |
| python_legacy_protocol | legacy prototype | No |

Hardware validation order (when the device is attached) is in `TODO.md`.

## Added in this phase (all simulated / unit; none hardware-verified)
Outputs (speaker/headphone independence, loopback mute), monitor state machine
(12 transitions), input/playback mix inversion, LED RGB/brightness,
output meter offsets, 96 kHz DSP restriction (session + GUI), risk
assessment (phantom / high-volume / routing), simulated auto-gain workflow,
GUI backend round trips, QML load without errors, unplug/replug and clean
shutdown, USB backend read-only.

## Phase 5B additions
Verification class: **simulated / unit / visual-regression**; none hardware-verified.
* Sync: failed write kept and flagged, retry, discard, external device change merged
  without losing a pending edit, identical value sends no command.
* Trigger set: all 12 command words distinct and as recovered; mode switch refused on
  non-simulated transports even with every policy flag on.
* GUI: simulated mode switch restarts and preserves settings; auto-gain bracket.
* Visual: `visual_geometry` (dividers ±4 px, region colours ±8) — guards regressions,
  not a pixel-accuracy claim. Manual: `tools/visual/compare.py` against local references.
* Measured (offscreen *software* renderer, so indicative only): no-device idle 0 CPU ticks
  /10 s; mock with 25 Hz meters ≈ 10 % of one core; RSS ≈ 65 MB; startup to first frame
  ≈ 1.5 s.

## Phase 5C: hardening (all simulated / unit / fixture based; **no hardware verification**)
| Area | Tests |
|---|---|
| Error taxonomy | stable names; suggestions distinguish permission / ownership / stall |
| Lifecycle | stalls never escalate; busy/denied don't count as dead link; 3 timeouts → `Error`, `reconnect()` recovers; success resets counter; oversized/short responses rejected, last good state kept; reconnect keeps pending edits and adopts device-side changes; unsupported PID refused; capabilities from handshake |
| Write safety | destructive words never pass on hardware (guard + `UsbTransport` independently); unknown trigger words / payload / sizes refused; unknown & reserved settings bytes preserved; unsupported enum / boolean raw values not rewritten; edits outside known writable bytes refused |
| Concurrency | 8 threads × 10 reads serialised (max 1 in flight, all succeed); cancel aborts queued + future requests; shutdown during an in-flight (hung) request completes < 1 s and `close()` does not deadlock; session cancel is not a link failure |
| Discovery | hot-plug diff events incl. re-enumeration to the other PID; mode classification; raw descriptor parsing fixture (UAC2 header, endpoints); sysfs fixture tree incl. driver symlink |
| GUI | shutdown with 400 queued commands; busy / EACCES / stall reported without disconnect; USB mode without device stays quiet and read-only; unsent edits retained/retry/discard; device-side change merged |

### Sanitizers (development builds: `-DLEWITT_SANITIZE=…`)
| Build | Scope | Result |
|---|---|---|
| ASan + UBSan | core + GUI, all 90 entries | clean for our code. LeakSanitizer reports third-party allocations in fontconfig/Qt text layout during GUI tests; suppressed in `tests/lsan.supp` (our own symbols are not suppressed; none were reported) |
| TSan | core `lewitt_tests` (87 cases incl. concurrency/cancel) | clean, no warnings. Needs `setarch $(uname -m) -R` because this kernel's ASLR entropy trips TSan's startup mapping check; Qt is not instrumented, so the GUI suite is **not** run under TSan (documented limitation) |

### Benchmarks (`cmake --build build --target lewitt_bench lewitt_bench_gui`; mock only)
Release build, this machine, three runs agreed within ~15 %:

| Benchmark | ns/op |
|---|---|
| `SettingsMessage::fromBytes` | ~15 |
| settings decode (12 accessors) | ~13 |
| `mergeExternalChanges` (62 fields) | ~47 |
| `verifyOnlyKnownFieldsChanged` | ~85 |
| `MockTransport::read` / via `SerializedTransport` | ~14 / ~53 (≈ 38 ns decorator cost) |
| session tick: status / settings (merge) | ~62 / ~70 |
| edit + commit ×2 (write + re-read) | ~530 |

GUI path (offscreen): `setValue` → echoed state median 0.03 ms, p95 0.06 ms, max 0.3 ms;
simulated unplug→replug→Connected ≈ 40 ms (dominated by the 40 ms poll tick). Idle:
no-device USB mode ≈ 0 CPU ticks / 10 s; mock with 25 Hz meters ≈ 11 % of one core on the
software renderer; RSS ≈ 65 MB. Real USB latency (ms) will dominate all of the above.
