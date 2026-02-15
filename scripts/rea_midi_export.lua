-- @description Headless: Bake send MIDI dest channel into item MIDI channels, then export SMF Type 0 @ 480 PPQN (tempo+timesig, notes, CC, sysex)
-- @version 1.0

local r = reaper
local PPQN = 480
local OUT_ENV = "REAPER_EXPORT_MIDI_PATH"

----------------------------------------------------------------
-- Bitwise compatibility (Lua 5.3 or bit32)
----------------------------------------------------------------
local band, bor, rshift
if _G.bit32 then
  band, bor, rshift = bit32.band, bit32.bor, bit32.rshift
else
  band   = function(a, b) return a & b end
  bor    = function(a, b) return a | b end
  rshift = function(a, b) return a >> b end
end

----------------------------------------------------------------
-- Console helper
----------------------------------------------------------------
local function log(s) r.ShowConsoleMsg(tostring(s) .. "\n") end

local function u16be(n)
  return string.char(band(rshift(n, 8), 0xFF), band(n, 0xFF))
end

local function u32be(n)
  return string.char(
    band(rshift(n, 24), 0xFF),
    band(rshift(n, 16), 0xFF),
    band(rshift(n, 8), 0xFF),
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

  -- Use tempo map at time 0, not Master_GetTempo()
  if not have_tempo0 then
    local bpm = reaper.TimeMap2_GetDividedBpmAtTime(0, 0.0)
    if not bpm or bpm <= 0 then bpm = 120 end

    local us_per_qn = math.floor((60.0 / bpm) * 1000000.0 + 0.5)
    local t1 = band(rshift(us_per_qn, 16), 0xFF)
    local t2 = band(rshift(us_per_qn, 8), 0xFF)
    local t3 = band(us_per_qn, 0xFF)
    table.insert(meta, { tick = 0, bytes = string.char(0xFF, 0x51, 0x03, t1, t2, t3), order = 0 })
  end

  if not have_ts0 then
    local num, denom = reaper.TimeMap_GetTimeSigAtTime(0, 0.0)
    if not num or num == 0 then num = 4 end
    if not denom or denom == 0 then denom = 4 end
    local dd = log2_int(denom)
    local cc, bb = 24, 8
    table.insert(meta,
      { tick = 0, bytes = string.char(0xFF, 0x58, 0x04, band(num, 0xFF), band(dd, 0xFF), cc, bb), order = 1 })
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
      local t2 = band(rshift(us_per_qn, 8), 0xFF)
      local t3 = band(us_per_qn, 0xFF)
      table.insert(meta, { tick = tick, bytes = string.char(0xFF, 0x51, 0x03, t1, t2, t3), order = 0 })

      -- Time signature: FF 58 04 nn dd cc bb
      local dd = log2_int(tsd)
      local cc, bb = 24, 8
      table.insert(meta,
        { tick = tick, bytes = string.char(0xFF, 0x58, 0x04, band(tsn, 0xFF), band(dd, 0xFF), cc, bb), order = 1 })
    end
  end

  add_initial_tempo_timesig_if_missing(meta)
  return meta
end

local function collect_channel_events(ppqn)
  local evts = {}
  local num_tracks = r.CountTracks(0)

  local function for_each_looped_time(t0, item_pos, item_end, loop_period, cb)
    if not loop_period or loop_period <= 0 then
      if t0 >= item_pos and t0 < item_end then cb(t0) end
      return
    end

    local k = 0
    while true do
      local t = t0 + k * loop_period
      if t >= item_end then break end
      if t >= item_pos then cb(t) end
      k = k + 1
    end
  end

  local function is_eot_meta(msgbytes)
    return (#msgbytes >= 3 and msgbytes:byte(1) == 0xFF and msgbytes:byte(2) == 0x2F)
  end

  local function wrap_sysex_for_smf(msgbytes)
    -- msgbytes begins with 0xF0 or 0xF7. SMF requires a VLQ length after the status byte.
    local status = msgbytes:byte(1)
    local payload = msgbytes:sub(2)
    return string.char(status) .. vlq(#payload) .. payload
  end

  local function kind_and_order_for_bytes(bytes)
    -- Used only for stable sorting; we keep your existing "order" convention.
    local b0 = bytes:byte(1)

    -- Meta first (but after your tempo/timesig meta which uses order 0/1)
    if b0 == 0xFF then return 5 end

    -- Note-off before note-on at same tick
    local hi = band(b0, 0xF0)
    if hi == 0x80 then return 11 end
    if hi == 0x90 then
      if #bytes >= 3 and bytes:byte(3) == 0 then return 11 end
      return 10
    end

    -- SysEx after channel messages by default
    if b0 == 0xF0 or b0 == 0xF7 then return 30 end

    -- Everything else (CC, PB, PC, AT, system common, etc.)
    return 20
  end

  for tr = 0, num_tracks - 1 do
    local track = r.GetTrack(0, tr)
    local num_items = r.CountTrackMediaItems(track)

    for it = 0, num_items - 1 do
      local item = r.GetTrackMediaItem(track, it)
      local item_pos = r.GetMediaItemInfo_Value(item, "D_POSITION")
      local item_len = r.GetMediaItemInfo_Value(item, "D_LENGTH")
      local item_end = item_pos + item_len

      local take = r.GetActiveTake(item)
      if take and r.TakeIsMIDI(take) then
        -- Loop source handling (keep your behavior)
        local loopsrc = r.GetMediaItemInfo_Value(item, "B_LOOPSRC") or 0
        local loop_period = nil
        if loopsrc == 1 then
          local src = r.GetMediaItemTake_Source(take)
          if src then
            local src_len = r.GetMediaSourceLength(src)
            local playrate = r.GetMediaItemTakeInfo_Value(take, "D_PLAYRATE") or 1.0
            if src_len and src_len > 0 and playrate > 0 then
              loop_period = src_len / playrate
            end
          end
        end

        local ok, packed = r.MIDI_GetAllEvts(take, "")
        if ok and type(packed) == "string" then
          local idx = 1
          local ppqpos = 0
          local packlen = #packed

          while idx <= packlen do
            local ok_unpack, offset, flags, msgbytes
            ok_unpack, offset, flags, msgbytes, idx =
                pcall(string.unpack, "<i4Bs4", packed, idx) -- common REAPER pattern

            if not ok_unpack then
              -- fallback for older/alternate builds where flags may be 4 bytes
              ok_unpack, offset, flags, msgbytes, idx =
                  pcall(string.unpack, "<i4i4s4", packed, idx)
            end

            if not ok_unpack then break end

            ppqpos = ppqpos + offset

            if msgbytes and #msgbytes > 0 then
              local muted = (band(flags or 0, 2) ~= 0)
              if not muted and not is_eot_meta(msgbytes) then
                local b0 = msgbytes:byte(1)
                local ev_time = r.MIDI_GetProjTimeFromPPQPos(take, ppqpos)

                for_each_looped_time(ev_time, item_pos, item_end, loop_period, function(t)
                  local qn = reaper.TimeMap2_timeToQN(0, t)
                  local tick = math.floor(qn * ppqn + 0.5)

                  local outbytes = msgbytes
                  if b0 == 0xF0 or b0 == 0xF7 then
                    outbytes = wrap_sysex_for_smf(msgbytes)
                  end

                  evts[#evts + 1] = {
                    tick = tick,
                    bytes = outbytes,
                    order = kind_and_order_for_bytes(outbytes),
                  }
                end)
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
  for i = 1, #meta_events do all[#all + 1] = meta_events[i] end
  for i = 1, #chan_events do all[#all + 1] = chan_events[i] end

  table.sort(all, function(a, b)
    if a.tick ~= b.tick then return a.tick < b.tick end
    return (a.order or 0) < (b.order or 0)
  end)

  local chunks = {}
  local last_tick = 0

  for i = 1, #all do
    local e = all[i]
    local dt = e.tick - last_tick
    if dt < 0 then dt = 0 end
    chunks[#chunks + 1] = vlq(dt)
    chunks[#chunks + 1] = e.bytes
    last_tick = e.tick
  end

  -- End of track
  chunks[#chunks + 1] = vlq(0) .. string.char(0xFF, 0x2F, 0x00)

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
  r.PreventUIRefresh(1)

  -- Select all items
  r.Main_OnCommand(40182, 0) -- Item: Select all items

  local sel_items = r.CountSelectedMediaItems(0)
  if sel_items == 0 then
    log("ERROR: No items available to select.")
    r.PreventUIRefresh(-1)
    return
  end

  -- Apply track/take FX to items (MIDI Output)
  r.Main_OnCommand(40436, 0)

  -- Export
  local out = os.getenv(OUT_ENV)
  if not out or out == "" then
    log("ERROR: No output path specified.")
    r.PreventUIRefresh(-1)
    return
  end
  out = out:gsub("\\", "/")

  local meta = collect_tempo_timesig_meta(PPQN)
  local chan = collect_channel_events(PPQN)
  log("Collected channel events: " .. tostring(#chan))
  -- inject_prism_sysex_before_first_event(chan)
  local ok, err = write_type0_midi(out, PPQN, meta, chan)


  if not ok then
    log("MIDI export FAILED: " .. tostring(err))
  else
    log("Wrote MIDI: " .. out)
  end

  r.PreventUIRefresh(-1)
end

-- Wait until project is actually loaded (tempo map ready), then run.
local last_state = -1
local stable_ticks = 0
local MAX_TICKS = 300  -- ~300 defer cycles; plenty
local NEED_STABLE = 15 -- require 15 consecutive stable cycles

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
    log(("Tempo at 0.0s: %.6f\n"):format(bpm0))
    run_bake_and_export()
    return
  end

  if stable_ticks % 10 == 0 then
    log(("Waiting for load... state=%d bpm0=%.6f stable=%d/%d\n")
      :format(state, bpm0, stable_ticks, NEED_STABLE))
  end

  if MAX_TICKS <= 0 then
    log("ERROR: Project never stabilized; aborting.\n")
    return
  end
  MAX_TICKS = MAX_TICKS - 1

  reaper.defer(wait_until_loaded_then_run)
end

-- Kick it off
reaper.defer(wait_until_loaded_then_run)
