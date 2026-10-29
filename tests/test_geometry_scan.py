import base64
import importlib.util
import json
import sys
import unittest
from array import array
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


video = load("build_pattern_video")
analysis = load("analyze_geometry")


class ScheduleTest(unittest.TestCase):
    def setUp(self):
        self.schedule = video.build_schedule(columns=16, rows=9, segment=1.5, cycles=2)

    def test_blocks_tile_the_frame(self):
        blocks = self.schedule["blockOrder"]
        self.assertEqual(len(blocks), 144)
        self.assertEqual(sum(b["w"] * b["h"] for b in blocks), 1920 * 1080)
        for block in blocks:
            self.assertGreater(block["w"], 0)
            self.assertGreater(block["h"], 0)
            self.assertLessEqual(block["x"] + block["w"], 1920)
            self.assertLessEqual(block["y"] + block["h"], 1080)

    def test_segments_are_contiguous_and_non_overlapping(self):
        segments = self.schedule["segments"]
        self.assertEqual(len(segments), 2 * (2 + 144))
        for previous, current in zip(segments, segments[1:]):
            self.assertAlmostEqual(previous["end"], current["start"], places=3)
            self.assertLessEqual(previous["start"], previous["end"])
        self.assertAlmostEqual(segments[-1]["end"], self.schedule["durationSeconds"], places=3)

    def test_one_drawbox_per_distinct_stimulus(self):
        filters = video.filter_graph(self.schedule)
        self.assertEqual(len(filters), 1 + 144)
        self.assertTrue(all("enable=" in entry for entry in filters))
        self.assertIn("+", filters[0], "second cycle must reuse the beacon drawbox")

    def test_bad_input_is_rejected(self):
        with self.assertRaises(ValueError):
            video.build_schedule(columns=0)
        with self.assertRaises(ValueError):
            video.build_schedule(segment=0)
        with self.assertRaises(ValueError):
            video.build_schedule(cycles=0)


class ColourSpaceTest(unittest.TestCase):
    def test_matches_native_transfer_function(self):
        self.assertEqual(analysis.srgb_light(0), 0.0)
        self.assertEqual(analysis.srgb_light(255), 1.0)
        self.assertAlmostEqual(analysis.srgb_light(10), 10 / 255 / 12.92, places=12)
        self.assertAlmostEqual(analysis.srgb_light(128), 0.21586050011389926, places=12)
        values = [analysis.srgb_light(code) for code in range(256)]
        self.assertEqual(values, sorted(values))

    def test_encode_inverts_decode(self):
        for code in range(256):
            self.assertLessEqual(abs(analysis.srgb_encode(analysis.srgb_light(code)) - code), 1)


class PayloadTest(unittest.TestCase):
    def test_decode_bins_layout(self):
        values = array("H", [((region * 3 + channel) * 256 + b) % 65536
                             for region in range(16) for channel in range(3)
                             for b in range(256)])
        if sys.byteorder != "little":
            values.byteswap()
        encoded = base64.b64encode(values.tobytes()).decode("ascii")
        self.assertEqual(len(encoded), 32768)
        decoded = analysis.decode_bins(encoded)
        self.assertEqual(len(decoded), 16)
        self.assertEqual(len(decoded[0]), 3)
        self.assertEqual(len(decoded[0][0]), 256)
        for region in (0, 7, 15):
            for channel in range(3):
                for b in (0, 1, 128, 255):
                    self.assertEqual(decoded[region][channel][b],
                                     ((region * 3 + channel) * 256 + b) % 65536)

    def test_wrong_payload_is_rejected(self):
        with self.assertRaises(ValueError):
            analysis.decode_bins(base64.b64encode(b"\x00" * 16).decode("ascii"))


class FitTest(unittest.TestCase):
    def test_box_marginal_sums_to_one(self):
        for cells in (9, 16):
            self.assertAlmostEqual(sum(analysis.box_marginal(0.24, 0.75, cells)), 1.0, places=12)

    def test_box_fit_recovers_known_extent(self):
        observed = analysis.box_marginal(0.24, 0.75, 16)
        fit = analysis.fit_box(observed)
        self.assertAlmostEqual(fit["x0"], 0.24, delta=1.0 / 64)
        self.assertAlmostEqual(fit["x1"], 0.75, delta=1.0 / 64)
        self.assertLess(fit["rmse"], 0.01)

    def test_gaussian_is_preferred_over_box_for_a_bell(self):
        observed = analysis.gaussian_marginal(0.5, 0.18, 16)
        fitted = analysis.fit_axis(observed)
        self.assertEqual(fitted["preferred"], "gaussian")
        self.assertLess(fitted["gaussian"]["normalisedRmse"],
                        fitted["box"]["normalisedRmse"])

    def test_rectangle_is_preferred_over_gaussian_for_a_box(self):
        observed = analysis.box_marginal(0.1, 0.9, 16)
        fitted = analysis.fit_axis(observed)
        self.assertEqual(fitted["preferred"], "box")


class SeparabilityTest(unittest.TestCase):
    def test_rectangle_factorises(self):
        grid = [[0.0] * 8 for _ in range(6)]
        for gy in range(2, 5):
            for gx in range(3, 6):
                grid[gy][gx] = 1.0
        self.assertLess(analysis.separability(grid, 8, 6), 1e-9)

    def test_l_shape_does_not(self):
        grid = [[0.0] * 8 for _ in range(6)]
        for gy in range(6):
            grid[gy][0] = 1.0
        for gx in range(8):
            grid[5][gx] = 1.0
        self.assertGreater(analysis.separability(grid, 8, 6), 0.2)


class AlignmentTest(unittest.TestCase):
    def test_beacon_anchor_is_the_falling_edge(self):
        samples = []
        for index, bright in enumerate([False, True, True, True, False, False]):
            value = 255 if bright else 0
            samples.append({"t": index * 0.1, "regions": [[value] * 3 for _ in range(16)]})
        anchors = analysis.beacon_anchors(samples)
        self.assertEqual(len(anchors), 1)
        # Last white sample at 0.3, first block sample at 0.4.
        self.assertAlmostEqual(anchors[0], 0.35, places=6)

    def test_single_frame_glitch_is_not_a_beacon(self):
        samples = []
        for index, bright in enumerate([False, True, False, False]):
            value = 255 if bright else 0
            samples.append({"t": index * 0.1, "regions": [[value] * 3 for _ in range(16)]})
        self.assertEqual(analysis.beacon_anchors(samples), [])

    def test_short_launch_splash_is_not_a_beacon(self):
        samples, clock = [], [0.0]

        def push(bright, count):
            for _ in range(count):
                value = 255 if bright else 0
                samples.append({"t": clock[0], "regions": [[value] * 3 for _ in range(16)]})
                clock[0] += 1.0 / 6.0

        push(True, 4)     # 0.5 s of app launch splash
        push(False, 12)
        push(True, 12)    # 1.8 s of real beacon
        push(False, 12)
        anchors = analysis.beacon_anchors(samples, 0.7)
        self.assertEqual(len(anchors), 1)
        self.assertGreater(anchors[0], 3.0)

    def test_extra_anchors_keep_the_matching_cycles(self):
        cycles = [{"index": 0}, {"index": 1}]
        self.assertEqual(analysis.match_cycles([1.0, 224.0, 448.0], cycles), [224.0, 448.0])
        self.assertEqual(analysis.match_cycles([224.0], cycles), [224.0])


class SyntheticPipelineTest(unittest.TestCase):
    """The whole path, from a known rectangle set back to fitted extents."""

    @classmethod
    def setUpClass(cls):
        cls.schedule = video.build_schedule(columns=16, rows=9, segment=1.5, cycles=2)
        cls.profile = json.loads(
            (ROOT / "profiles" / "65QNED87A6B.BEUFLJP.json").read_text())
        cls.recording = analysis.synthetic_recording(cls.schedule, cls.profile["rectangles"])
        cls.report = analysis.analyse(cls.recording, cls.schedule, cls.profile)

    def test_both_beacons_found(self):
        self.assertEqual(self.report["beacons"], 2)
        self.assertEqual(self.report["cyclesUsed"], 2)

    def test_every_region_accounts_for_its_area(self):
        for result in self.report["regions"]:
            self.assertAlmostEqual(result["coverageSum"], 1.0, delta=0.05)

    def test_rectangles_recovered_within_one_cell(self):
        saved = {rect["region"]: rect for rect in self.profile["rectangles"]}
        for result in self.report["regions"]:
            rect = saved[result["region"]]
            self.assertAlmostEqual(result["x"]["box"]["x0"], rect["x0"], delta=1.0 / 16)
            self.assertAlmostEqual(result["x"]["box"]["x1"], rect["x1"], delta=1.0 / 16)
            self.assertAlmostEqual(result["y"]["box"]["x0"], rect["y0"], delta=1.0 / 9)
            self.assertAlmostEqual(result["y"]["box"]["x1"], rect["y1"], delta=1.0 / 9)

    def test_saved_rectangles_stay_rectangles(self):
        for result in self.report["regions"]:
            self.assertLess(result["separability"], 0.05)
            self.assertEqual(result["x"]["preferred"], "box")

    def test_every_region_has_a_histogram(self):
        for result in self.report["regions"]:
            self.assertIn("histogram", result)
            self.assertEqual(len(result["histogram"]), 256)


if __name__ == "__main__":
    unittest.main()
