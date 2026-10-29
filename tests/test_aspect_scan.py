import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


video = load("build_aspect_video")
aspect = load("analyze_aspect")


class GeometryTest(unittest.TestCase):
    def test_rect_is_centred_and_keeps_its_ratio(self):
        for ratio in (2.76, 2.39, 16 / 9, 4 / 3, 9 / 16):
            x, y, w, h = video.aspect_rect(ratio)
            self.assertAlmostEqual(w / h, ratio, delta=ratio * 0.01)
            self.assertLessEqual(abs((x + w / 2) - 960), 1)
            self.assertLessEqual(abs((y + h / 2) - 540), 1)
            self.assertTrue(0 <= x and x + w <= 1920)
            self.assertTrue(0 <= y and y + h <= 1080)

    def test_picture_band_matches_the_crop(self):
        x0, y0, x1, y1 = aspect.picture_band(2.39)
        self.assertEqual((x0, x1), (0.0, 1.0))
        self.assertAlmostEqual(y0, 0.128, delta=0.01)
        self.assertAlmostEqual(y1, 0.872, delta=0.01)
        x0, y0, x1, y1 = aspect.picture_band(9 / 16)
        self.assertEqual((y0, y1), (0.0, 1.0))
        self.assertAlmostEqual(x0, 0.34, delta=0.02)
        self.assertAlmostEqual(x1, 0.66, delta=0.02)
        self.assertEqual(aspect.picture_band(16 / 9), (0.0, 0.0, 1.0, 1.0))


class ScheduleTest(unittest.TestCase):
    def test_segments_are_contiguous_and_ratios_kept(self):
        schedule = video.build_schedule(ratios=[2.76, 2.39, 4 / 3], segment=2.0, cycles=2)
        self.assertEqual(len(schedule["blockOrder"]), 3)
        segments = schedule["segments"]
        for previous, current in zip(segments, segments[1:]):
            self.assertAlmostEqual(previous["end"], current["start"], places=3)
        self.assertAlmostEqual(segments[-1]["end"], schedule["durationSeconds"], places=3)
        ratios = [row["ratio"] for row in segments if row["kind"] == "block" and
                  row["cycle"] == 0]
        self.assertEqual(ratios, [2.76, 2.39, 1.3333])

    def test_one_drawbox_per_distinct_rectangle(self):
        schedule = video.build_schedule(ratios=[2.39, 2.35, 4 / 3], segment=2.0, cycles=2)
        filters = video.filter_graph(schedule)
        self.assertEqual(len(filters), 1 + 3)
        self.assertTrue(all("enable=" in entry for entry in filters))


class SyntheticTest(unittest.TestCase):
    def test_r14_tracks_the_bar(self):
        import json
        schedule = video.build_schedule(ratios=[2.39, 2.20, 16 / 9], segment=1.0, cycles=1)
        profile = json.loads((ROOT / "profiles" / "65QNED87A6B.BEUFLJP.json").read_text())
        recording = aspect.synthetic_recording(schedule, profile["rectangles"])
        report, table = aspect.analyse(recording, schedule)
        self.assertEqual(report["beacons"], 1)
        self.assertLess(table[2.39][14], 0.02)          # r14 fully inside the bar
        self.assertGreater(table[1.7778][14], 0.98)     # no bar, r14 sees picture
        self.assertGreater(table[2.39][2], 0.98)        # r2 always sees the picture
        self.assertLess(abs(table[2.2][14] - 0.20), 0.05)


if __name__ == "__main__":
    unittest.main()
