import argparse
import atexit
import bisect
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

REASCRIPT_PATH = Path(__file__).resolve().with_name("rea_midi_export.lua")

subprocs: List[subprocess.Popen] = []
atexit.register(lambda: [p.kill() for p in subprocs if p.poll() is None])


def write_text_if_changed(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8")


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


def parse_midi_events(midi_path: Path) -> Tuple[int, List[Dict[str, object]]]:
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
    return division, events


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


def tick_to_ms(
    tick: int,
    seg_ticks: List[int],
    seg_cum_us: List[float],
    seg_tempo_us_per_qn: List[int],
    division: int,
) -> int:
    idx = bisect.bisect_right(seg_ticks, tick) - 1
    if idx < 0:
        idx = 0
    us = seg_cum_us[idx] + ((tick - seg_ticks[idx]) * seg_tempo_us_per_qn[idx]) / float(
        division
    )
    return int(round(us / 1000.0))


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
    division: int, events: List[Dict[str, object]]
) -> Dict[str, object]:
    seg_ticks, seg_cum_us, seg_tempo_us_per_qn = build_tempo_segments(events, division)

    patch_defs_by_orig: Dict[int, Dict[str, object]] = {}
    used_patch_ids: set[int] = set()
    song_events: List[Dict[str, object]] = []

    for ev in events:
        tick = int(ev["tick"])
        time_ms = tick_to_ms(tick, seg_ticks, seg_cum_us, seg_tempo_us_per_qn, division)

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
                        "etype": "note_on",
                        "patch_orig": channel,
                        "note_number": note,
                        "velocity": vel,
                    }
                )
            elif msg_type == 0x80 or (msg_type == 0x90 and vel == 0):
                song_events.append(
                    {
                        "time_ms": time_ms,
                        "etype": "note_off",
                        "patch_orig": channel,
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
                        "etype": "timesig",
                        "numerator": numerator,
                        "denominator": denominator,
                    }
                )

    orig_patch_ids = sorted(used_patch_ids)
    patch_map = {orig: i for i, orig in enumerate(orig_patch_ids)}

    patch_defs_local: List[Dict[str, object]] = []
    for orig in orig_patch_ids:
        parsed = patch_defs_by_orig.get(orig)
        if parsed is None:
            ops = default_patch_ops()
        else:
            ops = list(parsed["ops"])
        patch_defs_local.append({"local_idx": patch_map[orig], "ops": ops})

    for ev in song_events:
        if "patch_orig" in ev:
            ev["patch_idx"] = patch_map[int(ev["patch_orig"])]

    order = {
        "note_off": 0,
        "patch": 1,
        "note_on": 2,
        "tempo": 3,
        "timesig": 4,
        "marker": 5,
    }
    song_events.sort(key=lambda e: (int(e["time_ms"]), order.get(str(e["etype"]), 10)))

    duration_ms = 0
    if song_events:
        duration_ms = int(song_events[-1]["time_ms"])

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
        "#include <shared/audio/song.h>",
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
    lines.append(f"    .duration_ms = {int(model['duration_ms'])}u,")
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
    division, events = parse_midi_events(midi_path)
    model = build_song_model(division, events)

    header_content = emit_song_header(symbol, rpp_path, model)
    write_text_if_changed(header_out, header_content)


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

    return 0


if __name__ == "__main__":
    sys.exit(main())
