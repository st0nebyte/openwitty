-- OpenWitty DAW-1..4 test for Ardour 9 (headless; ALSA backend, or JACK = pipewire-jack). See docs/FEATURE_COMPLETION_ROADMAP.md "DAW".
-- Run: tools/audio/daw_ardour_test.sh <session-dir> [seconds]  (owner present; speakers muted; headphones off).
local IN_DEV  = IN_DEV  or "CONNECT 2"
local OUT_DEV = OUT_DEV or "CONNECT 2"
local SECS    = SECONDS or 10
local DIR     = SESSION_DIR
local BACKEND = BACKEND or "ALSA"
local LIST_ONLY = LIST_ONLY or false   -- only list the ports, no tracks, no tones
local function log(...) print("DAW", ...) end

local be = AudioEngine:set_backend(BACKEND, "", "")
if BACKEND == "ALSA" then
  be:set_input_device_name(IN_DEV)
  be:set_output_device_name(OUT_DEV)
end
be:set_sample_rate(48000)
be:set_buffer_size(BUFFER or 1024)
log("backend " .. BACKEND .. " in=" .. IN_DEV .. " out=" .. OUT_DEV)
local s = create_session(DIR, "daw", 48000)
assert(s, "session not created (engine start failed?)")
log("engine running:", AudioEngine:running(), "rate", Session:nominal_sample_rate())

-- DAW-1: physical ports
local e = Session:engine()
local function ports(flags)
  local _, t = e:get_backend_ports("", ARDOUR.DataType("audio"), flags, C.StringVector())
  local out = {} for n in t[4]:iter() do out[#out+1] = n end return out
end
local caps  = ports(ARDOUR.PortFlags.IsOutput | ARDOUR.PortFlags.IsPhysical)
local plays = ports(ARDOUR.PortFlags.IsInput  | ARDOUR.PortFlags.IsPhysical)
for _, n in ipairs(caps)  do log("DAW-1 capture port:", n, e:get_pretty_name_by_name(n)) end
for _, n in ipairs(plays) do log("DAW-1 playback port:", n, e:get_pretty_name_by_name(n)) end
-- JACK (PipeWire) lists every device: keep only the ports whose name starts with IN_PREFIX / OUT_PREFIX
local function keep(list, prefix)
  if not prefix then return list end
  local out = {} for _, n in ipairs(list) do if n:sub(1, #prefix) == prefix then out[#out+1] = n end end return out
end
caps, plays = keep(caps, IN_PREFIX), keep(plays, OUT_PREFIX)
for _, n in ipairs(caps)  do log("selected capture port:", n) end
for _, n in ipairs(plays) do log("selected playback port:", n) end
if LIST_ONLY then Session:save_state("", false, false, false, false, false); log("list only: done"); return end

-- tracks: 4 mono + 1 stereo, inputs wired explicitly, outputs disconnected (no monitoring path)
local function track(nin, name)
  local tl = Session:new_audio_track(nin, 2, ARDOUR.RouteGroup(), 1, name, ARDOUR.PresentationInfo.max_order, ARDOUR.TrackMode.Normal, false)
  local t = tl:front()
  t:input():disconnect_all(nil); t:output():disconnect_all(nil)
  return t
end
local names = {"Mic", "Instrument", "LoopL", "LoopR"}
local tracks = {}
for i, nm in ipairs(names) do
  local t = track(1, nm)
  if caps[i] then t:input():audio(0):connect(caps[i]) end
  tracks[#tracks+1] = t
end
local st = track(2, "LoopStereo")
if caps[3] and caps[4] then st:input():audio(0):connect(caps[3]); st:input():audio(1):connect(caps[4]) end
tracks[#tracks+1] = st
local wired = { {caps[1]}, {caps[2]}, {caps[3]}, {caps[4]}, {caps[3], caps[4]} }
for i, t in ipairs(tracks) do
  local parts = {}
  for ch, port in ipairs(wired[i]) do
    parts[#parts+1] = "ch" .. ch .. "<-" .. tostring(port) .. ":" .. tostring(port and t:input():audio(ch-1):connected_to(port))
  end
  log("track", t:name(), "inputs", t:n_inputs():n_audio(), table.concat(parts, " "))
end

-- DAW-4 tone buses: 1 kHz -> playback 1 only, 2 kHz -> playback 2 only, -30 dBFS, not via master
local function tone(name, freq, port)
  local rl = Session:new_audio_route(1, 1, ARDOUR.RouteGroup(), 1, name, ARDOUR.PresentationInfo.Flag.AudioBus, ARDOUR.PresentationInfo.max_order)
  local r = rl:front()
  r:input():disconnect_all(nil); r:output():disconnect_all(nil)
  local p = ARDOUR.LuaAPI.new_luaproc(Session, "SinGen")
  assert(not p:isnil(), "SinGen not found")
  r:add_processor_by_index(p, 0, nil, true)
  ARDOUR.LuaAPI.set_processor_param(p, 0, freq)
  ARDOUR.LuaAPI.set_processor_param(p, 1, -30)
  if port then r:output():audio(0):connect(port) end
  log("tone bus", name, freq, "Hz -30 dBFS ->", port or "(not connected)")
  return r
end
local tl_ = tone("ToneL", 1000, plays[1]); local tr_ = tone("ToneR", 2000, plays[2])
if TONECHECK then   -- dry run only: record the two tone buses internally to verify the generator settings
  local tc = track(2, "ToneCheck")
  tc:input():audio(0):connect(tl_:output():audio(0):name()); tc:input():audio(1):connect(tr_:output():audio(0):name())
  tracks[#tracks+1] = tc
end

-- record
for _, t in ipairs(tracks) do t:rec_enable_control():set_value(1, PBD.GroupControlDisposition.NoGroup) end
Session:maybe_enable_record(false)
Session:request_roll(ARDOUR.TransportRequestSource.TRS_UI)
log("RECORDING for", SECS, "s")
for i = 1, SECS * 5 do ARDOUR.LuaAPI.usleep(200000) end
Session:request_stop(false, false, ARDOUR.TransportRequestSource.TRS_UI)
ARDOUR.LuaAPI.usleep(1000000)
log("stopped")
Session:save_state("", false, false, false, false, false)
close_session()
log("done")
