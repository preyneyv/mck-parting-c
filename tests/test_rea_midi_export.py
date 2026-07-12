import importlib.util
import pathlib
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


if __name__ == "__main__":
    unittest.main()
