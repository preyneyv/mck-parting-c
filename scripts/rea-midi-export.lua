-- @description Headless: Bake send MIDI dest channel into item MIDI channels, then export SMF Type 0 @ 480 PPQN (tempo+timesig, notes, CC, sysex)
-- @version 1.0

local r = reaper
local PPQN = 480
local OUT_ENV = "REAPER_MIDI_OUT"

----------------------------------------------------------------
-- Bitwise compatibility (Lua 5.3 or bit32)
----------------------------------------------------------------
local band, bor, rshift
if _G.bit32 then
  band, bor, rshift = bit32.band, bit32.bor, bit32.rshift
else
  band = function(a,b) return a & b end
  bor  = function(a,b) return a | b end
  rshift = function(a,b) return a >> b end
end

----------------------------------------------------------------
-- Console helper
----------------------------------------------------------------
local function log(s) r.ShowConsoleMsg(tostring(s) .. "\n") end

----------------------------------------------------------------
-- PART 1: Bake send dest channel into take MIDI channels (your embedded action)
----------------------------------------------------------------
local function get_send_dest_channel_1_to_16(track)
  -- category 0 = track sends
  local send_idx = 0
  local num_sends = r.GetTrackNumSends(track, 0)
  if num_sends < 1 then return nil end

  local midiflags = r.GetTrackSendInfo_Value(track, 0, send_idx, "I_MIDIFLAGS")
  -- low 5 bits = src chan; next 5 bits = dest chan (0=orig, 1-16=chan, 31=disabled)
  local dest = math.floor(midiflags / 32) % 32
  if dest == 0 or dest == 31 then return nil end
  if dest < 1 or dest > 16 then return nil end
  return dest
end

local function bake_take_channel(take, chan_1_to_16)
  if not r.TakeIsMIDI(take) then return 0 end
  local ch = chan_1_to_16 - 1 -- API uses 0..15

  local ok, note_cnt, cc_cnt, _ = r.MIDI_CountEvts(take)
  if not ok then return 0 end

  local changed = 0

  -- Notes
  for i = 0, note_cnt - 1 do
    local retval, sel, muted, startppq, endppq, chan, pitch, vel = r.MIDI_GetNote(take, i)
    if retval and chan ~= ch then
      r.MIDI_SetNote(take, i, sel, muted, startppq, endppq, ch, pitch, vel, false)
      changed = changed + 1
    end
  end

  -- CC-like events (CC, PB, PC, chan pressure, poly pressure, etc.)
  for i = 0, cc_cnt - 1 do
    local retval, sel, muted, ppqpos, chanmsg, chan, msg2, msg3 = r.MIDI_GetCC(take, i)
    if retval and chan ~= ch then
      r.MIDI_SetCC(take, i, sel, muted, ppqpos, chanmsg, ch, msg2, msg3, false)
      changed = changed + 1
    end
  end

  r.MIDI_Sort(take)
  return changed
end

local function bake_track(track)
  local dest_chan = get_send_dest_channel_1_to_16(track)
  if not dest_chan then return 0, "no usable send dest channel" end

  local changed = 0
  local item_count = r.CountTrackMediaItems(track)

  for i = 0, item_count - 1 do
    local item = r.GetTrackMediaItem(track, i)
    local take_count = r.CountTakes(item)
    for t = 0, take_count - 1 do
      local take = r.GetTake(item, t)
      if take and r.TakeIsMIDI(take) then
        changed = changed + bake_take_channel(take, dest_chan)
      end
    end
  end

  return changed, ("baked to ch " .. dest_chan)
end

----------------------------------------------------------------
-- PART 2: SMF Type 0 exporter (480 PPQN, tempo+timesig, notes, CC, sysex)
----------------------------------------------------------------
local function u16be(n)
  return string.char(band(rshift(n, 8), 0xFF), band(n, 0xFF))
end

local function u32be(n)
  return string.char(
    band(rshift(n, 24), 0xFF),
    band(rshift(n, 16), 0xFF),
    band(rshift(n,  8), 0xFF),
    band(n, 0xFF)
  )
end

local function vlq(n)
  n = math.floor(n or 0)
  if n < 0 then n = 0 end
  local bytes = { band(n, 0x7F) }
  n = rshift(n, 7)
  while n > 0 do
    table.insert(bytes, 1, bor(band(n, 0x7F), 0x80))
    n = rshift(n, 7)
  end
  local out = {}
  for i = 1, #bytes do out[i] = string.char(bytes[i]) end
  return table.concat(out)
end

local function log2_int(x)
  local p = 0
  while x and x > 1 do
    x = rshift(x, 1)
    p = p + 1
  end
  return p
end

local function get_project_dir_and_name()
  local _, projfn = r.EnumProjects(-1, "")
  local _, projname = r.GetProjectName(0, "")

  local dir = ""
  if projfn and projfn ~= "" then
    dir = projfn:match("^(.*)[/\\][^/\\]+$") or ""
  else
    dir = r.GetProjectPath("") or ""
  end

  projname = (projname and projname ~= "") and projname or "untitled"
  projname = projname:gsub("%.rpp$", "")
  return dir, projname
end

local function add_initial_tempo_timesig_if_missing(meta)
  local have_tempo0, have_ts0 = false, false

  for i = 1, #meta do
    if meta[i].tick == 0 then
      local b1 = meta[i].bytes:byte(1)
      local b2 = meta[i].bytes:byte(2)
      if b1 == 0xFF and b2 == 0x51 then have_tempo0 = true end
      if b1 == 0xFF and b2 == 0x58 then have_ts0 = true end
    end
  end

  -- ✅ Use tempo map at time 0, not Master_GetTempo()
  if not have_tempo0 then
    local bpm = reaper.TimeMap2_GetDividedBpmAtTime(0, 0.0)
    if not bpm or bpm <= 0 then bpm = 120 end

    local us_per_qn = math.floor((60.0 / bpm) * 1000000.0 + 0.5)
    local t1 = band(rshift(us_per_qn, 16), 0xFF)
    local t2 = band(rshift(us_per_qn,  8), 0xFF)
    local t3 = band(us_per_qn, 0xFF)
    table.insert(meta, { tick = 0, bytes = string.char(0xFF, 0x51, 0x03, t1, t2, t3), order = 0 })
  end

  if not have_ts0 then
    local num, denom = reaper.TimeMap_GetTimeSigAtTime(0, 0.0)
    if not num or num == 0 then num = 4 end
    if not denom or denom == 0 then denom = 4 end
    local dd = log2_int(denom)
    local cc, bb = 24, 8
    table.insert(meta, { tick = 0, bytes = string.char(0xFF, 0x58, 0x04, band(num,0xFF), band(dd,0xFF), cc, bb), order = 1 })
  end
end


local function collect_tempo_timesig_meta(ppqn)
  local meta = {}
  local cnt = r.CountTempoTimeSigMarkers(0)

  for i = 0, cnt - 1 do
    local ok, timepos, _, _, bpm, tsn, tsd = r.GetTempoTimeSigMarker(0, i)
    if ok then
      local tick
      if timepos == 0 then
          tick = 0
      else
          local qn = reaper.TimeMap2_timeToQN(0, timepos)
          tick = math.floor(qn * ppqn + 0.5)
      end

      -- Tempo: FF 51 03 tt tt tt
      local us_per_qn = math.floor((60.0 / bpm) * 1000000.0 + 0.5)
      local t1 = band(rshift(us_per_qn, 16), 0xFF)
      local t2 = band(rshift(us_per_qn,  8), 0xFF)
      local t3 = band(us_per_qn, 0xFF)
      table.insert(meta, { tick = tick, bytes = string.char(0xFF, 0x51, 0x03, t1, t2, t3), order = 0 })

      -- Time signature: FF 58 04 nn dd cc bb
      local dd = log2_int(tsd)
      local cc, bb = 24, 8
      table.insert(meta, { tick = tick, bytes = string.char(0xFF, 0x58, 0x04, band(tsn,0xFF), band(dd,0xFF), cc, bb), order = 1 })
    end
  end

  add_initial_tempo_timesig_if_missing(meta)
  return meta
end

local function read_prism_sysex_from_gmem(ch0, stride)
  stride = stride or 512
  local base = ch0 * stride

  local len = math.floor(reaper.gmem_read(base) or 0)
  if len <= 0 or len > stride - 1 then return nil end

  local t = {}
  for i = 0, len - 1 do
    local b = math.floor(reaper.gmem_read(base + 1 + i) or 0) & 0xFF
    t[#t+1] = string.char(b)
  end

  local msg = table.concat(t)
  if #msg < 2 then return nil end
  if msg:byte(1) ~= 0xF0 then return nil end
  if msg:byte(#msg) ~= 0xF7 then return nil end
  return msg
end

local function inject_prism_sysex_before_first_event(chan_events)
  -- Attach to the same gmem name used by the JSFX: options:gmem=prism_syx
  reaper.gmem_attach("prism_syx")

  -- Find where to insert: before the first MIDI event (or at tick 0 if none)
  local first_tick = 0
  local first_order = 1000000
  if chan_events and #chan_events > 0 then
    for i = 1, #chan_events do
      local e = chan_events[i]
      if e.tick == first_tick then
        first_order = math.min(first_order, e.order or first_order)
      end
    end
  end

  -- Insert 16 SysEx messages in channel order
  -- Give them an order just before the earliest event at that tick.
  local insert_order = (first_order or 1000000) - 100
  local stride = 512

  for ch0 = 0, 15 do
    local raw = read_prism_sysex_from_gmem(ch0, stride)
    if raw then
      -- Convert raw (F0 ... F7) to SMF SysEx event:
      -- status = first byte (F0), data = bytes after it (includes ... F7)
      local status = raw:byte(1)
      local data = raw:sub(2)
      local smf = string.char(status) .. vlq(#data) .. data

      table.insert(chan_events, {
        tick = first_tick,
        bytes = smf,
        order = insert_order + ch0, -- stable order
      })
    end
  end
end


local function collect_channel_events(ppqn)
  local evts = {}
  local num_tracks = r.CountTracks(0)

  for tr = 0, num_tracks - 1 do
    local track = r.GetTrack(0, tr)
    local num_items = r.CountTrackMediaItems(track)

    for it = 0, num_items - 1 do
      local item = r.GetTrackMediaItem(track, it)
      local num_takes = r.CountTakes(item)

      for tk = 0, num_takes - 1 do
        local take = r.GetTake(item, tk)
        if take and r.TakeIsMIDI(take) then
          local note_cnt, cc_cnt, txt_cnt = r.MIDI_CountEvts(take)

          -- Notes: write NoteOn + NoteOff (vel 0)
          for i = 0, note_cnt - 1 do
            local ok, _, muted, startppq, endppq, chan, pitch, vel = r.MIDI_GetNote(take, i)
            if ok and not muted then
              local start_qn = r.MIDI_GetProjQNFromPPQPos(take, startppq)
              local end_qn   = r.MIDI_GetProjQNFromPPQPos(take, endppq)
              local st = math.floor(start_qn * ppqn + 0.5)
              local en = math.floor(end_qn   * ppqn + 0.5)

              local status_on  = bor(0x90, band(chan, 0x0F))
              local status_off = bor(0x80, band(chan, 0x0F))
              evts[#evts+1] = { tick = st, bytes = string.char(status_on,  band(pitch,0x7F), band(vel,0x7F)), order = 10 }
              evts[#evts+1] = { tick = en, bytes = string.char(status_off, band(pitch,0x7F), 0x00),           order = 11 }
            end
          end

          -- CC-like events: includes CC, pitch bend, program change, channel pressure, etc.
          for i = 0, cc_cnt - 1 do
            local ok, _, muted, ppqpos, chanmsg, _, msg2, msg3 = r.MIDI_GetCC(take, i)
            if ok and not muted then
              local qn = r.MIDI_GetProjQNFromPPQPos(take, ppqpos)
              local tick = math.floor(qn * ppqn + 0.5)

              local status = band(chanmsg, 0xFF)
              local hi = band(status, 0xF0)

              if hi == 0xC0 or hi == 0xD0 then
                evts[#evts+1] = { tick = tick, bytes = string.char(status, band(msg2,0x7F)), order = 20 }
              else
                evts[#evts+1] = { tick = tick, bytes = string.char(status, band(msg2,0x7F), band(msg3,0x7F)), order = 20 }
              end
            end
          end

          -- SysEx: raw F0/F7 events from take
          for i = 0, txt_cnt - 1 do
            local ok, _, muted, ppqpos, _, msg = r.MIDI_GetTextSysexEvt(take, i, "")
            if ok and not muted and msg and #msg > 0 then
              local b0 = msg:byte(1)
              if b0 == 0xF0 or b0 == 0xF7 then
                local qn = r.MIDI_GetProjQNFromPPQPos(take, ppqpos)
                local tick = math.floor(qn * ppqn + 0.5)

                -- SMF SysEx: F0/F7 + VLQ(length of data following) + data
                local data = msg:sub(2)
                local out = string.char(b0) .. vlq(#data) .. data
                evts[#evts+1] = { tick = tick, bytes = out, order = 30 }
              end
            end
          end
        end
      end
    end
  end

  return evts
end

local function write_type0_midi(filepath, ppqn, meta_events, chan_events)
  local all = {}
  for i = 1, #meta_events do all[#all+1] = meta_events[i] end
  for i = 1, #chan_events do all[#all+1] = chan_events[i] end

  table.sort(all, function(a,b)
    if a.tick ~= b.tick then return a.tick < b.tick end
    return (a.order or 0) < (b.order or 0)
  end)

  local chunks = {}
  local last_tick = 0

  for i = 1, #all do
    local e = all[i]
    local dt = e.tick - last_tick
    if dt < 0 then dt = 0 end
    chunks[#chunks+1] = vlq(dt)
    chunks[#chunks+1] = e.bytes
    last_tick = e.tick
  end

  -- End of track
  chunks[#chunks+1] = vlq(0) .. string.char(0xFF, 0x2F, 0x00)

  local track_data = table.concat(chunks)
  local mthd = "MThd" .. u32be(6) .. u16be(0) .. u16be(1) .. u16be(ppqn)
  local mtrk = "MTrk" .. u32be(#track_data) .. track_data

  local f, err = io.open(filepath, "wb")
  if not f then return false, err end
  f:write(mthd)
  f:write(mtrk)
  f:close()
  return true, nil
end

----------------------------------------------------------------
-- MAIN
----------------------------------------------------------------
local function run_bake_and_export()
  r.ClearConsole()
  r.PreventUIRefresh(1)
  r.Undo_BeginBlock()

  -- Bake selected tracks
  local sel_tracks = r.CountSelectedTracks(0)
  if sel_tracks == 0 then
    -- No dialogs in headless mode; just log and exit.
    log("ERROR: No selected tracks. Select the MIDI source tracks before running.")
    r.Undo_EndBlock("Headless bake+export (no selected tracks)", -1)
    r.PreventUIRefresh(-1)
    return
  end

  local total_changed, tracks_done = 0, 0
  for i = 0, sel_tracks - 1 do
    local tr = r.GetSelectedTrack(0, i)
    local changed, why = bake_track(tr)
    total_changed = total_changed + changed
    tracks_done = tracks_done + 1
    -- log("Track "..(i+1)..": "..tostring(why)..", changed "..tostring(changed))
  end

  log(("Baked channels: %d tracks, %d events changed"):format(tracks_done, total_changed))

  -- Export
  local out = os.getenv(OUT_ENV)
  if not out or out == "" then
    local dir, name = get_project_dir_and_name()
    out = (dir ~= "" and (dir .. "/" .. name) or name) .. ".type0_480.mid"
  end
  out = out:gsub("\\", "/")

  local meta = collect_tempo_timesig_meta(PPQN)
  local chan = collect_channel_events(PPQN)
  inject_prism_sysex_before_first_event(chan)
  local ok, err = write_type0_midi(out, PPQN, meta, chan)

  r.Undo_EndBlock(("Headless bake+export (%d tracks, %d baked)"):format(tracks_done, total_changed), -1)
  r.PreventUIRefresh(-1)

  if not ok then
    log("MIDI export FAILED: " .. tostring(err))
  else
    log("Wrote MIDI: " .. out)
  end

  -- Optional: quit REAPER after export (uncomment for batch jobs)
  r.Main_OnCommand(40004, 0) -- File: Quit REAPER
end

-- Wait until project is actually loaded (tempo map ready), then run.
local last_state = -1
local stable_ticks = 0
local MAX_TICKS = 300         -- ~300 defer cycles; plenty
local NEED_STABLE = 15        -- require 15 consecutive stable cycles

local function wait_until_loaded_then_run()
  local state = reaper.GetProjectStateChangeCount(0)

  if state == last_state then
    stable_ticks = stable_ticks + 1
  else
    stable_ticks = 0
    last_state = state
  end

  -- Also sanity-check that tempo query isn't "obviously default because not loaded yet"
  -- This isn't perfect (project could truly be 120), but combined with stability works well.
  local bpm0 = reaper.TimeMap2_GetDividedBpmAtTime(0, 0.0) or 120

  if stable_ticks >= NEED_STABLE then
    -- At this point the project state has stopped changing -> tempo map is typically ready.
    -- Optional: log bpm0 so you can verify it matches the project
    reaper.ShowConsoleMsg(("Tempo at 0.0s: %.6f\n"):format(bpm0))
    run_bake_and_export()
    return
  end

  if stable_ticks % 10 == 0 then
    reaper.ShowConsoleMsg(("Waiting for load... state=%d bpm0=%.6f stable=%d/%d\n")
      :format(state, bpm0, stable_ticks, NEED_STABLE))
  end

  if MAX_TICKS <= 0 then
    reaper.ShowConsoleMsg("ERROR: Project never stabilized; aborting.\n")
    return
  end
  MAX_TICKS = MAX_TICKS - 1

  reaper.defer(wait_until_loaded_then_run)
end

-- Kick it off
reaper.defer(wait_until_loaded_then_run)
