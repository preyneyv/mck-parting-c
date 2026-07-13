import argparse
import atexit
import bisect
import os
import struct
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

REASCRIPT_PATH = Path(__file__).resolve().with_name("rea_midi_export.lua")
ENGINE_TICK_RATE = 960
BEATLINE_FILE_MAGIC = 0x4E4C5442
BEATLINE_FILE_FORMAT_VERSION = 1
BEATLINE_FILE_HEADER_BYTES = 256
BEATLINE_SCORING_RULESET = 1
BEATLINE_FILE_FLAG_RANKED = 1
BEATLINE_MAX_NOTES = 1024

subprocs: List[subprocess.Popen] = []
atexit.register(lambda: [p.kill() for p in subprocs if p.poll() is None])


def write_text_if_changed(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = content.encode("utf-8")
    if path.exists() and path.read_bytes() == encoded:
        return
    path.write_bytes(encoded)


def write_bytes_if_changed(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() == content:
        return
    path.write_bytes(content)


def sanitize_symbol(raw: str) -> str:
    out = []
    for ch in raw:
        if ch.isalnum() or ch == "_":
            out.append(ch)
        else:
            out.append("_")
    symbol = "".join(out).strip("_")
    if not symbol:
        symbol = "song"
    if symbol[0].isdigit():
        symbol = f"song_{symbol}"
    return symbol


def read_u16be(data: bytes, pos: int) -> Tuple[int, int]:
    return (data[pos] << 8) | data[pos + 1], pos + 2


def read_u32be(data: bytes, pos: int) -> Tuple[int, int]:
    return (
        (data[pos] << 24)
        | (data[pos + 1] << 16)
        | (data[pos + 2] << 8)
        | data[pos + 3],
        pos + 4,
    )


def read_vlq(data: bytes, pos: int) -> Tuple[int, int]:
    value = 0
    while True:
        if pos >= len(data):
            raise ValueError("Unexpected EOF while reading VLQ")
        b = data[pos]
        pos += 1
        value = (value << 7) | (b & 0x7F)
        if (b & 0x80) == 0:
            break
    return value, pos


def parse_midi_events(midi_path: Path) -> Tuple[int, int, List[Dict[str, object]]]:
    data = midi_path.read_bytes()
    pos = 0

    if data[pos : pos + 4] != b"MThd":
        raise ValueError("Invalid MIDI header (MThd missing)")
    pos += 4
    hdr_len, pos = read_u32be(data, pos)
    if hdr_len != 6:
        raise ValueError(f"Unsupported MIDI header length: {hdr_len}")
    midi_format, pos = read_u16be(data, pos)
    ntrks, pos = read_u16be(data, pos)
    division, pos = read_u16be(data, pos)

    if division == 0:
        raise ValueError("Invalid MIDI division (0)")
    if midi_format not in (0, 1):
        raise ValueError(f"Unsupported MIDI format: {midi_format}")

    events: List[Dict[str, object]] = []
    song_end_tick = 0
    seq = 0

    for _ in range(ntrks):
        if data[pos : pos + 4] != b"MTrk":
            raise ValueError("Invalid MIDI track header (MTrk missing)")
        pos += 4
        track_len, pos = read_u32be(data, pos)
        end = pos + track_len
        if end > len(data):
            raise ValueError("Invalid MIDI track length")

        tick = 0
        running_status: Optional[int] = None
        while pos < end:
            delta, pos = read_vlq(data, pos)
            tick += delta
            if tick > song_end_tick:
                song_end_tick = tick
            if pos >= end:
                break

            status = data[pos]
            if status < 0x80:
                if running_status is None:
                    raise ValueError("Running status used without previous status")
                status = running_status
            else:
                pos += 1
                if status < 0xF0:
                    running_status = status
                else:
                    running_status = None

            if status == 0xFF:
                if pos >= end:
                    raise ValueError("Malformed meta event")
                meta_type = data[pos]
                pos += 1
                length, pos = read_vlq(data, pos)
                payload = data[pos : pos + length]
                pos += length

                if meta_type == 0x2F:
                    break

                events.append(
                    {
                        "tick": tick,
                        "seq": seq,
                        "kind": "meta",
                        "meta_type": meta_type,
                        "payload": payload,
                    }
                )
                seq += 1
                continue

            if status in (0xF0, 0xF7):
                length, pos = read_vlq(data, pos)
                payload = data[pos : pos + length]
                pos += length
                raw = bytes([status]) + payload
                if raw and raw[0] == 0xF0 and raw[-1] != 0xF7:
                    raw += b"\xf7"
                events.append({"tick": tick, "seq": seq, "kind": "sysex", "raw": raw})
                seq += 1
                continue

            msg_type = status & 0xF0
            channel = status & 0x0F
            if msg_type in (0xC0, 0xD0):
                data_len = 1
            else:
                data_len = 2

            if pos + data_len > end:
                raise ValueError("Malformed channel event")

            d1 = data[pos]
            pos += 1
            d2 = 0
            if data_len == 2:
                d2 = data[pos]
                pos += 1

            events.append(
                {
                    "tick": tick,
                    "seq": seq,
                    "kind": "chan",
                    "msg_type": msg_type,
                    "channel": channel,
                    "d1": d1,
                    "d2": d2,
                }
            )
            seq += 1

        pos = end

    events.sort(key=lambda e: (int(e["tick"]), int(e["seq"])))
    return division, song_end_tick, events


def parse_prism_set_patch_sysex(raw: bytes) -> Optional[Dict[str, object]]:
    if len(raw) < 6:
        return None
    if raw[0] != 0xF0 or raw[-1] != 0xF7:
        return None
    if raw[1] != 0x7D:
        return None
    cmd = raw[2]
    version = raw[3]
    if cmd != 0x01 or version != 0x01:
        return None

    payload = raw[4:-1]
    i = 0

    def read_u7() -> int:
        nonlocal i
        if i >= len(payload):
            raise ValueError("SysEx payload underflow")
        b = payload[i]
        i += 1
        if b & 0x80:
            raise ValueError("SysEx payload contains non-7bit byte")
        return b

    def read_u16_7x3() -> int:
        b0 = read_u7()
        b1 = read_u7()
        b2 = read_u7()
        return b0 | (b1 << 7) | (b2 << 14)

    def read_u32_7x5() -> int:
        b0 = read_u7()
        b1 = read_u7()
        b2 = read_u7()
        b3 = read_u7()
        b4 = read_u7()
        return b0 | (b1 << 7) | (b2 << 14) | (b3 << 21) | (b4 << 28)

    patch_idx = read_u7()
    ops = []
    for _ in range(4):
        ops.append(
            {
                "freq_mult": read_u7(),
                "level_q15": read_u16_7x3(),
                "mode": read_u7(),
                "a": read_u32_7x5(),
                "d": read_u32_7x5(),
                "s_q31": read_u32_7x5(),
                "r": read_u32_7x5(),
            }
        )

    return {"patch_idx": patch_idx, "ops": ops}


def build_tempo_segments(
    events: List[Dict[str, object]], division: int
) -> Tuple[List[int], List[float], List[int]]:
    tempo_points: List[Tuple[int, int]] = [(0, 500000)]

    for ev in events:
        if ev["kind"] != "meta" or ev["meta_type"] != 0x51:
            continue
        payload = bytes(ev["payload"])
        if len(payload) != 3:
            continue
        us_per_quarter = (payload[0] << 16) | (payload[1] << 8) | payload[2]
        tempo_points.append((int(ev["tick"]), us_per_quarter))

    tempo_points.sort(key=lambda x: x[0])

    merged: List[Tuple[int, int]] = []
    for tick, tempo in tempo_points:
        if merged and merged[-1][0] == tick:
            merged[-1] = (tick, tempo)
        else:
            merged.append((tick, tempo))

    seg_ticks: List[int] = []
    seg_tempo_us_per_qn: List[int] = []
    seg_cum_us: List[float] = []

    cum_us = 0.0
    prev_tick = merged[0][0]
    prev_tempo = merged[0][1]
    seg_ticks.append(prev_tick)
    seg_tempo_us_per_qn.append(prev_tempo)
    seg_cum_us.append(cum_us)

    for tick, tempo in merged[1:]:
        delta_ticks = tick - prev_tick
        cum_us += (delta_ticks * prev_tempo) / float(division)
        seg_ticks.append(tick)
        seg_tempo_us_per_qn.append(tempo)
        seg_cum_us.append(cum_us)
        prev_tick = tick
        prev_tempo = tempo

    return seg_ticks, seg_cum_us, seg_tempo_us_per_qn


def tick_to_us(
    tick: int,
    seg_ticks: List[int],
    seg_cum_us: List[float],
    seg_tempo_us_per_qn: List[int],
    division: int,
) -> float:
    idx = bisect.bisect_right(seg_ticks, tick) - 1
    if idx < 0:
        idx = 0
    us = seg_cum_us[idx] + ((tick - seg_ticks[idx]) * seg_tempo_us_per_qn[idx]) / float(
        division
    )
    return us


def tick_to_ms(
    tick: int,
    seg_ticks: List[int],
    seg_cum_us: List[float],
    seg_tempo_us_per_qn: List[int],
    division: int,
) -> int:
    us = tick_to_us(tick, seg_ticks, seg_cum_us, seg_tempo_us_per_qn, division)
    return int(round(us / 1000.0))


def tick_to_engine_tick(
    tick: int,
    seg_ticks: List[int],
    seg_cum_us: List[float],
    seg_tempo_us_per_qn: List[int],
    division: int,
) -> int:
    us = tick_to_us(tick, seg_ticks, seg_cum_us, seg_tempo_us_per_qn, division)
    return int(round(us * ENGINE_TICK_RATE / 1_000_000.0))


def default_patch_ops() -> List[Dict[str, int]]:
    return [
        {
            "freq_mult": 1,
            "level_q15": 16384,
            "mode": 0,
            "a": 2,
            "d": 40,
            "s_q31": 1610612736,
            "r": 40,
        },
        {
            "freq_mult": 1,
            "level_q15": 0,
            "mode": 0,
            "a": 0,
            "d": 0,
            "s_q31": 0,
            "r": 0,
        },
        {
            "freq_mult": 1,
            "level_q15": 0,
            "mode": 0,
            "a": 0,
            "d": 0,
            "s_q31": 0,
            "r": 0,
        },
        {
            "freq_mult": 1,
            "level_q15": 0,
            "mode": 0,
            "a": 0,
            "d": 0,
            "s_q31": 0,
            "r": 0,
        },
    ]


def build_song_model(
    division: int, song_end_tick: int, events: List[Dict[str, object]]
) -> Dict[str, object]:
    seg_ticks, seg_cum_us, seg_tempo_us_per_qn = build_tempo_segments(events, division)

    patch_defs_by_orig: Dict[int, Dict[str, object]] = {}
    used_patch_ids: set[int] = set()
    song_events: List[Dict[str, object]] = []

    for ev in events:
        tick = int(ev["tick"])
        time_ms = tick_to_ms(tick, seg_ticks, seg_cum_us, seg_tempo_us_per_qn, division)
        time_tick = tick_to_engine_tick(
            tick, seg_ticks, seg_cum_us, seg_tempo_us_per_qn, division
        )

        if ev["kind"] == "sysex":
            parsed = parse_prism_set_patch_sysex(bytes(ev["raw"]))
            if parsed is not None:
                patch_idx = int(parsed["patch_idx"])
                patch_defs_by_orig[patch_idx] = parsed
                used_patch_ids.add(patch_idx)
            continue

        if ev["kind"] == "chan":
            msg_type = int(ev["msg_type"])
            channel = int(ev["channel"])
            note = int(ev["d1"])
            vel = int(ev["d2"])
            used_patch_ids.add(channel)

            if msg_type == 0x90 and vel > 0:
                song_events.append(
                    {
                        "time_ms": time_ms,
                        "time_tick": time_tick,
                        "etype": "note_on",
                        "patch_idx": channel,
                        "note_number": note,
                        "velocity": vel,
                    }
                )
            elif msg_type == 0x80 or (msg_type == 0x90 and vel == 0):
                song_events.append(
                    {
                        "time_ms": time_ms,
                        "time_tick": time_tick,
                        "etype": "note_off",
                        "patch_idx": channel,
                        "note_number": note,
                    }
                )
            continue

        if ev["kind"] == "meta":
            meta_type = int(ev["meta_type"])
            payload = bytes(ev["payload"])
            if meta_type == 0x51 and len(payload) == 3:
                us_per_quarter = (payload[0] << 16) | (payload[1] << 8) | payload[2]
                song_events.append(
                    {
                        "time_ms": time_ms,
                        "time_tick": time_tick,
                        "etype": "tempo",
                        "us_per_quarter": us_per_quarter,
                    }
                )
            elif meta_type == 0x58 and len(payload) >= 2:
                numerator = int(payload[0])
                denominator = 1 << int(payload[1])
                song_events.append(
                    {
                        "time_ms": time_ms,
                        "time_tick": time_tick,
                        "etype": "timesig",
                        "numerator": numerator,
                        "denominator": denominator,
                    }
                )

    # Preserve original sparse channel/patch identity (no compact remapping),
    # but only emit patches that were actually referenced in MIDI/SysEx.
    orig_patch_ids = set()
    for patch_idx in used_patch_ids:
        if 0 <= int(patch_idx) < 32:
            orig_patch_ids.add(int(patch_idx))

    patch_defs_local: List[Dict[str, object]] = []
    for orig in sorted(orig_patch_ids):
        parsed = patch_defs_by_orig.get(orig)
        if parsed is None:
            ops = default_patch_ops()
        else:
            ops = list(parsed["ops"])
        patch_defs_local.append({"local_idx": orig, "ops": ops})

    order = {
        "note_off": 0,
        "patch": 1,
        "note_on": 2,
        "tempo": 3,
        "timesig": 4,
        "marker": 5,
    }
    song_events.sort(key=lambda e: (int(e["time_ms"]), order.get(str(e["etype"]), 10)))

    duration_ms = tick_to_ms(
        int(song_end_tick),
        seg_ticks,
        seg_cum_us,
        seg_tempo_us_per_qn,
        division,
    )
    if song_events:
        duration_ms = max(duration_ms, int(song_events[-1]["time_ms"]))
    duration_ticks = tick_to_engine_tick(
        int(song_end_tick),
        seg_ticks,
        seg_cum_us,
        seg_tempo_us_per_qn,
        division,
    )
    if song_events:
        duration_ticks = max(duration_ticks, int(song_events[-1]["time_tick"]))

    first_tempo = next((e for e in song_events if e["etype"] == "tempo"), None)
    us_per_q = 500000 if first_tempo is None else int(first_tempo["us_per_quarter"])
    bpm_q8 = int(round((60000000.0 / float(us_per_q)) * 256.0))

    first_ts = next((e for e in song_events if e["etype"] == "timesig"), None)
    numerator = 4 if first_ts is None else int(first_ts["numerator"])
    denominator = 4 if first_ts is None else int(first_ts["denominator"])

    return {
        "patches": patch_defs_local,
        "events": song_events,
        "duration_ms": duration_ms,
        "duration_ticks": duration_ticks,
        "bpm_q8": bpm_q8,
        "numerator": numerator,
        "denominator": denominator,
    }


def emit_song_header(symbol: str, source_rpp: str, model: Dict[str, object]) -> str:
    patches = list(model["patches"])
    events = list(model["events"])

    lines = [
        "// Auto-generated by scripts/rea_midi_export.py. Do not edit.",
        f"// Source: {source_rpp}",
        "",
        "#pragma once",
        "",
        "#include <prism/audio.h>",
        "",
        f"static const audio_song_header_t {symbol}_header = {{",
        f"    .bpm_q8 = {int(model['bpm_q8'])}u,",
        f"    .numerator = {int(model['numerator'])}u,",
        f"    .denominator = {int(model['denominator'])}u,",
        "};",
        "",
    ]

    lines.append(f"static const audio_song_patch_event_t {symbol}_patches[] = {{")
    for patch in patches:
        lines.append("    {")
        lines.append(f"        .patch_idx = {int(patch['local_idx'])}u,")
        lines.append("        .patch = {")
        lines.append("            .ops = {")
        for op in patch["ops"]:
            mode = (
                "AUDIO_SYNTH_OP_MODE_FREQ_MOD"
                if int(op["mode"]) == 1
                else "AUDIO_SYNTH_OP_MODE_ADDITIVE"
            )
            lines.append("                {")
            lines.append(f"                    .freq_mult = {int(op['freq_mult'])},")
            lines.append(f"                    .level = (q1x15){int(op['level_q15'])},")
            lines.append(f"                    .mode = {mode},")
            lines.append(
                "                    .env = "
                + "{"
                + f".a = {int(op['a'])}u, .d = {int(op['d'])}u, "
                + f".s = (q1x31){int(op['s_q31'])}, .r = {int(op['r'])}u"
                + "},"
            )
            lines.append("                },")
        lines.append("            },")
        lines.append("        },")
        lines.append("    },")
    lines.append("};")
    lines.append("")

    lines.append(f"static const uint32_t {symbol}_event_ticks[] = {{")
    for ev in events:
        lines.append(f"    {int(ev['time_tick'])}u,")
    lines.append("};")
    lines.append("")

    lines.append(f"static const audio_song_event_t {symbol}_events[] = {{")
    for ev in events:
        etype = str(ev["etype"])
        t = int(ev["time_ms"])
        if etype == "note_on":
            lines.append(
                "    "
                + "{"
                + f".time_ms = {t}u, .type = AUDIO_SONG_EVENT_NOTE_ON, "
                + ".data.note_on = "
                + "{"
                + f".patch_idx = {int(ev['patch_idx'])}u, "
                + f".note_number = {int(ev['note_number'])}u, "
                + f".velocity = {int(ev['velocity'])}u"
                + "}"
                + "},"
            )
        elif etype == "note_off":
            lines.append(
                "    "
                + "{"
                + f".time_ms = {t}u, .type = AUDIO_SONG_EVENT_NOTE_OFF, "
                + ".data.note_off = "
                + "{"
                + f".patch_idx = {int(ev['patch_idx'])}u, "
                + f".note_number = {int(ev['note_number'])}"
                + "}"
                + "},"
            )
        elif etype == "tempo":
            lines.append(
                "    "
                + "{"
                + f".time_ms = {t}u, .type = AUDIO_SONG_EVENT_TEMPO, "
                + ".data.tempo = "
                + "{"
                + f".us_per_quarter = {int(ev['us_per_quarter'])}u"
                + "}"
                + "},"
            )
        elif etype == "timesig":
            lines.append(
                "    "
                + "{"
                + f".time_ms = {t}u, .type = AUDIO_SONG_EVENT_TIMESIG, "
                + ".data.timesig = "
                + "{"
                + f".numerator = {int(ev['numerator'])}u, "
                + f".denominator = {int(ev['denominator'])}u"
                + "}"
                + "},"
            )
    lines.append("};")
    lines.append("")

    lines.append(f"static const audio_song_asset_t {symbol}_song = {{")
    lines.append(f"    .header = &{symbol}_header,")
    lines.append(f"    .patches = {symbol}_patches,")
    lines.append(f"    .patch_count = {len(patches)}u,")
    lines.append(f"    .events = {symbol}_events,")
    lines.append(f"    .event_count = {len(events)}u,")
    lines.append(f"    .event_ticks = {symbol}_event_ticks,")
    lines.append(f"    .duration_ms = {int(model['duration_ms'])}u,")
    lines.append(f"    .duration_ticks = {int(model['duration_ticks'])}u,")
    lines.append("    .loop_start_ms = 0u,")
    lines.append("    .loop_end_ms = 0u,")
    lines.append("};")
    lines.append("")

    return "\n".join(lines)


def build_song_header_from_midi(
    rpp_path: str,
    midi_path: Path,
    header_out: Path,
    symbol: str,
) -> None:
    division, song_end_tick, events = parse_midi_events(midi_path)
    model = build_song_model(division, song_end_tick, events)

    header_content = emit_song_header(symbol, rpp_path, model)
    write_text_if_changed(header_out, header_content)


def _align4(value: int) -> int:
    return (value + 3) & ~3


def _beatline_notes(events: List[Dict[str, object]], patch_idx: int):
    notes: List[Tuple[int, int, int, int]] = []
    hold_start: List[Optional[int]] = [None, None]
    for event in events:
        if int(event.get("patch_idx", -1)) != patch_idx:
            continue
        event_type = str(event["etype"])
        note = int(event.get("note_number", -1))
        tick = int(event["time_tick"])
        lane = 0 if note in (48, 49) else 1 if note in (60, 61) else -1
        if lane < 0:
            continue
        if event_type == "note_on":
            if note in (48, 60):
                notes.append((tick, lane, 0, 0))
            elif note in (49, 61):
                hold_start[lane] = tick
        elif event_type == "note_off" and note in (49, 61):
            start = hold_start[lane]
            if start is None:
                continue
            duration = max(0, tick - start)
            duration = min(duration, 0xFFFF)
            notes.append((start, lane, 1 if duration else 0, duration))
            hold_start[lane] = None

    for lane, start in enumerate(hold_start):
        if start is not None:
            notes.append((start, lane, 0, 0))
    notes.sort(key=lambda note: (note[0], note[1]))
    if len(notes) > BEATLINE_MAX_NOTES:
        raise ValueError(
            f"difficulty {patch_idx} has {len(notes)} notes; maximum is "
            f"{BEATLINE_MAX_NOTES}"
        )
    return notes


def emit_beatline_file(
    title: str,
    artist: str,
    model: Dict[str, object],
    track_id: int = 0,
    normal_chart_id: int = 0,
    hard_chart_id: int = 0,
) -> bytes:
    if not title or not artist:
        raise ValueError(".beatline title and artist must not be empty")
    title_bytes = title.encode("utf-8")
    artist_bytes = artist.encode("utf-8")
    if len(title_bytes) > 0xFFFF or len(artist_bytes) > 0xFFFF:
        raise ValueError(".beatline title and artist must fit uint16 lengths")
    ids = (track_id, normal_chart_id, hard_chart_id)
    if any(value < 0 or value > 0xFFFFFFFFFFFFFFFF for value in ids):
        raise ValueError("ranked IDs must be uint64 values")
    ranked = all(value != 0 for value in ids)
    if ranked != any(value != 0 for value in ids):
        raise ValueError("ranked metadata requires all three IDs")

    events = list(model["events"])
    normal_notes = _beatline_notes(events, 0)
    hard_notes = _beatline_notes(events, 1)

    audio_patch_ids = {
        int(event["patch_idx"])
        for event in events
        if str(event["etype"]) in ("note_on", "note_off")
        and int(event["patch_idx"]) >= 2
    }
    patches = [
        patch for patch in model["patches"]
        if int(patch["local_idx"]) in audio_patch_ids
    ]
    if any(patch_id >= 16 for patch_id in audio_patch_ids):
        raise ValueError("Beatline audio patches must use channels 2-15")
    audio_events = [
        event for event in events
        if str(event["etype"]) in ("note_on", "note_off")
        and int(event["patch_idx"]) >= 2
    ]

    output = bytearray(BEATLINE_FILE_HEADER_BYTES)

    def append_aligned(payload: bytes) -> int:
        while len(output) != _align4(len(output)):
            output.append(0)
        offset = len(output)
        output.extend(payload)
        return offset

    title_offset = append_aligned(title_bytes + b"\0")
    artist_offset = append_aligned(artist_bytes + b"\0")

    patch_data = bytearray()
    for patch in patches:
        patch_data.extend(struct.pack("<B3x", int(patch["local_idx"])))
        for op in patch["ops"]:
            patch_data.extend(
                struct.pack(
                    "<ihBBIIiI",
                    int(op["freq_mult"]),
                    int(op["level_q15"]),
                    int(op["mode"]),
                    0,
                    int(op["a"]),
                    int(op["d"]),
                    int(op["s_q31"]),
                    int(op["r"]),
                )
            )
    patches_offset = append_aligned(bytes(patch_data))

    event_data = bytearray()
    for event in audio_events:
        event_data.extend(
            struct.pack(
                "<IBBBB",
                int(event["time_ms"]),
                0 if event["etype"] == "note_on" else 1,
                int(event["patch_idx"]),
                int(event["note_number"]),
                int(event.get("velocity", 0)),
            )
        )
    events_offset = append_aligned(bytes(event_data))

    def note_bytes(notes) -> bytes:
        return b"".join(struct.pack("<IBBH", *note) for note in notes)

    normal_notes_offset = append_aligned(note_bytes(normal_notes))
    hard_notes_offset = append_aligned(note_bytes(hard_notes))

    struct.pack_into("<IHH", output, 0, BEATLINE_FILE_MAGIC,
                     BEATLINE_FILE_FORMAT_VERSION,
                     BEATLINE_FILE_HEADER_BYTES)
    struct.pack_into("<IIII", output, 8, len(output),
                     BEATLINE_SCORING_RULESET,
                     BEATLINE_FILE_FLAG_RANKED if ranked else 0, 0)
    struct.pack_into("<QQQ", output, 24, *ids)
    struct.pack_into("<III", output, 48, int(model["bpm_q8"]),
                     int(model["duration_ms"]),
                     int(model["duration_ticks"]))
    struct.pack_into("<BBH", output, 60, int(model["numerator"]),
                     int(model["denominator"]), 0)
    struct.pack_into("<IHHI", output, 64, title_offset, len(title_bytes),
                     len(artist_bytes), artist_offset)
    struct.pack_into("<IIIIIIII", output, 76,
                     patches_offset, len(patches),
                     events_offset, len(audio_events),
                     normal_notes_offset, len(normal_notes),
                     hard_notes_offset, len(hard_notes))
    return bytes(output)


def build_beatline_from_midi(
    midi_path: Path,
    output: Path,
    title: str,
    artist: str,
    track_id: int = 0,
    normal_chart_id: int = 0,
    hard_chart_id: int = 0,
) -> None:
    division, song_end_tick, events = parse_midi_events(midi_path)
    model = build_song_model(division, song_end_tick, events)
    write_bytes_if_changed(
        output,
        emit_beatline_file(title, artist, model, track_id,
                           normal_chart_id, hard_chart_id),
    )


def run_export(rpp_path: Path, midi_out_path: Path, reaper_path: str) -> bool:
    midi_out_path.parent.mkdir(parents=True, exist_ok=True)
    prev_mtime = midi_out_path.stat().st_mtime if midi_out_path.exists() else None

    env = dict(os.environ)
    env["REAPER_EXPORT_MIDI_PATH"] = str(midi_out_path)

    proc = subprocess.Popen(
        [
            reaper_path,
            "-newinst",
            "-nosplash",
            str(rpp_path),
            str(REASCRIPT_PATH),
        ],
        env=env,
    )
    subprocs.append(proc)

    while proc.poll() is None:
        mtime = midi_out_path.stat().st_mtime if midi_out_path.exists() else None
        if mtime is not None and mtime != prev_mtime:
            while True:
                time.sleep(0.1)
                m2 = midi_out_path.stat().st_mtime if midi_out_path.exists() else None
                if m2 == mtime:
                    break
                mtime = m2
            proc.kill()
            return True
        time.sleep(0.5)

    print("Error: REAPER process exited without exporting MIDI.")
    return False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export REAPER RPP to MIDI and optionally generate header-only C song structs"
    )
    parser.add_argument("--rpp", required=True, help="Input .rpp project file")
    parser.add_argument("--midi-out", help="Output MIDI file path (.mid)")
    parser.add_argument("--song-header-out", help="Output C header path (.h)")
    parser.add_argument("--beatline-out", help="Output .beatline track file")
    parser.add_argument("--title", help="Beatline track title")
    parser.add_argument("--artist", help="Beatline track artist")
    parser.add_argument("--track-id", type=int, default=0)
    parser.add_argument("--normal-chart-id", type=int, default=0)
    parser.add_argument("--hard-chart-id", type=int, default=0)
    parser.add_argument(
        "--song-source-out",
        help="Deprecated: source output is no longer generated (header-only)",
    )
    parser.add_argument(
        "--header-out", help="Deprecated alias for old raw MIDI header output"
    )
    parser.add_argument("--symbol", help="Base C symbol for generated data")
    parser.add_argument(
        "--reaper-cli",
        required=True,
        help="Path to REAPER executable",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    rpp_raw = args.rpp
    if not rpp_raw:
        print("Error: provide --rpp <path>.", file=sys.stderr)
        return 2

    rpp_path = Path(rpp_raw).resolve()
    if not rpp_path.is_file():
        print(f"Error: {rpp_path} is not a valid file.", file=sys.stderr)
        return 2

    midi_out = (
        Path(args.midi_out).resolve() if args.midi_out else rpp_path.with_suffix(".mid")
    )

    reaper_cli = Path(args.reaper_cli)
    if not reaper_cli.exists():
        print(f"Error: REAPER not found at: {args.reaper_cli}", file=sys.stderr)
        return 2

    if not run_export(rpp_path, midi_out, args.reaper_cli):
        return 1

    if args.header_out:
        print(
            "Error: --header-out is deprecated. Use --song-header-out.",
            file=sys.stderr,
        )
        return 2

    if args.song_source_out:
        print(
            "Warning: --song-source-out is deprecated and ignored (header-only output).",
            file=sys.stderr,
        )

    if args.song_header_out:
        symbol = sanitize_symbol(args.symbol or ("sounds_" + rpp_path.stem))
        relative_rpp = str(rpp_path.relative_to(Path(__file__).parent.parent)).replace(
            "\\", "/"
        )
        build_song_header_from_midi(
            relative_rpp,
            midi_out,
            Path(args.song_header_out).resolve(),
            symbol,
        )

    if args.beatline_out:
        if not args.title or not args.artist:
            print("Error: --beatline-out requires --title and --artist.",
                  file=sys.stderr)
            return 2
        build_beatline_from_midi(
            midi_out,
            Path(args.beatline_out).resolve(),
            args.title,
            args.artist,
            args.track_id,
            args.normal_chart_id,
            args.hard_chart_id,
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())
