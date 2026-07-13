import importlib.util
import pathlib
import struct
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "rea_midi_export", ROOT / "scripts" / "rea_midi_export.py")
EXPORTER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(EXPORTER)


class EngineTickExportTests(unittest.TestCase):
    def test_default_tempo_maps_to_960_hz_ticks(self):
        segments = EXPORTER.build_tempo_segments([], 480)
        self.assertEqual(EXPORTER.tick_to_engine_tick(480, *segments, 480), 480)
        self.assertEqual(EXPORTER.tick_to_engine_tick(240, *segments, 480), 240)

    def test_song_model_keeps_ms_and_engine_tick_timelines(self):
        events = [{
            "tick": 480,
            "seq": 0,
            "kind": "chan",
            "msg_type": 0x90,
            "channel": 0,
            "d1": 60,
            "d2": 100,
        }]
        model = EXPORTER.build_song_model(480, 960, events)
        self.assertEqual(model["events"][0]["time_ms"], 500)
        self.assertEqual(model["events"][0]["time_tick"], 480)
        self.assertEqual(model["duration_ms"], 1000)
        self.assertEqual(model["duration_ticks"], 960)


def operator():
    return {
        "freq_mult": 1 << 16,
        "level_q15": 1000,
        "mode": 0,
        "a": 1,
        "d": 2,
        "s_q31": 1 << 30,
        "r": 3,
    }


def beatline_model():
    events = [
        {"time_ms": 10, "time_tick": 10, "etype": "note_on",
         "patch_idx": 0, "note_number": 48, "velocity": 100},
        {"time_ms": 20, "time_tick": 20, "etype": "note_on",
         "patch_idx": 1, "note_number": 49, "velocity": 100},
        {"time_ms": 30, "time_tick": 50, "etype": "note_off",
         "patch_idx": 1, "note_number": 49, "velocity": 0},
        {"time_ms": 40, "time_tick": 60, "etype": "note_on",
         "patch_idx": 2, "note_number": 72, "velocity": 90},
        {"time_ms": 80, "time_tick": 100, "etype": "note_off",
         "patch_idx": 2, "note_number": 72, "velocity": 0},
    ]
    return {
        "patches": [{"local_idx": index, "ops": [operator()] * 4}
                    for index in range(3)],
        "events": events,
        "duration_ms": 100,
        "duration_ticks": 120,
        "bpm_q8": 120 * 256,
        "numerator": 4,
        "denominator": 4,
    }


class BeatlineFileTests(unittest.TestCase):
    def test_unregistered_file_has_no_ranked_binding(self):
        contents = EXPORTER.emit_beatline_file(
            "Custom Song", "Charter", beatline_model())
        magic, version, header_size = struct.unpack_from("<IHH", contents)
        file_size, ruleset, flags = struct.unpack_from("<III", contents, 8)
        self.assertEqual(
            (magic, version, header_size, file_size, ruleset, flags),
            (EXPORTER.BEATLINE_FILE_MAGIC,
             EXPORTER.BEATLINE_FILE_FORMAT_VERSION,
             EXPORTER.BEATLINE_FILE_HEADER_BYTES, len(contents),
             EXPORTER.BEATLINE_SCORING_RULESET, 0),
        )
        self.assertEqual(struct.unpack_from("<QQQ", contents, 24), (0, 0, 0))
        patches_offset, patch_count, events_offset, event_count = (
            struct.unpack_from("<IIII", contents, 76)
        )
        self.assertEqual((patch_count, event_count), (1, 2))
        self.assertEqual(contents[patches_offset], 2)
        normal_offset, normal_count, hard_offset, hard_count = (
            struct.unpack_from("<IIII", contents, 92)
        )
        self.assertEqual((normal_count, hard_count), (1, 1))
        self.assertEqual(
            struct.unpack_from("<IBBH", contents, normal_offset),
            (10, 0, 0, 0),
        )
        self.assertEqual(
            struct.unpack_from("<IBBH", contents, hard_offset),
            (20, 0, 1, 30),
        )

    def test_registered_binding_preserves_uint64_ids(self):
        ids = (0x0123456789ABCDEF, 0x1020304050607080,
               0xFFEEDDCCBBAA9988)
        contents = EXPORTER.emit_beatline_file(
            "Ranked Song", "Charter", beatline_model(), *ids)
        self.assertEqual(struct.unpack_from("<I", contents, 16)[0], 1)
        self.assertEqual(struct.unpack_from("<QQQ", contents, 24), ids)

    def test_partial_ranked_binding_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "all three"):
            EXPORTER.emit_beatline_file(
                "Broken", "Charter", beatline_model(), 1, 2, 0)


if __name__ == "__main__":
    unittest.main()
