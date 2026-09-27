#!/usr/bin/env python3
"""Check F1-F4 publication fixtures, not candidate algorithm correctness.

Uses only the Python standard library and resolves data relative to this file.
The checks cover documented schemas, units, and observable exercise scenarios;
they do not reconstruct hidden pairing/trajectory labels or evaluate solutions.
"""

import csv
import itertools
import json
import math
from pathlib import Path
import unittest


DATA = Path(__file__).resolve().parents[1] / "data"
F1_COLUMNS = (
    "frame", "timestamp", "cx", "cy", "length", "angle_deg", "brightness", "color",
)
F2_COLUMNS = (
    "frame", "timestamp", "observation_id", "x_px", "y_px", "score", "color",
    "measurement_valid",
)
F3_TARGET_FIELDS = {
    "id", "distance_m", "bearing_rad", "threat", "is_enemy", "visible",
    "operator_priority", "age_ms", "track_age_frames",
}


def reject_constant(value):
    raise ValueError(f"Non-standard JSON number: {value}")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def read_jsonl(path):
    with path.open(encoding="utf-8") as stream:
        return [json.loads(line, parse_constant=reject_constant,
                           object_pairs_hook=unique_object) for line in stream]


def usable(target):
    """Published eligibility predicate; no ranking or state machine."""
    return target["is_enemy"] and target["visible"] and 0 <= target["age_ms"] <= 200


def runs(rows, predicate):
    """Return consecutive fixture rows for which a condition is true."""
    return [list(group) for active, group in itertools.groupby(rows, predicate) if active]


class PublicDataTest(unittest.TestCase):
    def assert_finite(self, value):
        self.assertIsInstance(value, (int, float))
        self.assertNotIsInstance(value, bool)
        self.assertTrue(math.isfinite(value), f"Non-finite numeric input: {value}")

    def read_csv(self, path, columns):
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            self.assertEqual(tuple(reader.fieldnames), columns)
            rows = list(reader)
        self.assertTrue(rows)
        for row in rows:
            self.assertEqual(set(row), set(columns))
            self.assertNotIn(None, row.values())
        return rows

    def test_f1_anonymous_candidates_and_video_assets(self):
        for csv_name, video_name in (
            ("F1_lightbar_candidates.csv", "F1_multi_video.mp4"),
            ("F1_single_lightbar_candidates.csv", "F1_single_video.mp4"),
        ):
            with self.subTest(csv=csv_name):
                video = DATA / "F1" / video_name
                self.assertGreater(video.stat().st_size, 1024)
                with video.open("rb") as stream:
                    self.assertEqual(stream.read(8)[4:], b"ftyp")
                rows = self.read_csv(DATA / "F1" / csv_name, F1_COLUMNS)
                frames = []
                for row in rows:
                    frame = int(row["frame"])
                    frames.append(frame)
                    self.assertGreaterEqual(frame, 0)
                    self.assertLess(frame, 600)
                    for column in F1_COLUMNS:
                        self.assert_finite(float(row[column]))
                    self.assertAlmostEqual(float(row["timestamp"]), frame / 60.0, delta=1e-6)
                    self.assertGreater(float(row["length"]), 0)
                    self.assertGreaterEqual(float(row["angle_deg"]), 0)
                    self.assertLess(float(row["angle_deg"]), 180)
                    self.assertGreaterEqual(float(row["brightness"]), 0)
                    self.assertLessEqual(float(row["brightness"]), 1)
                    self.assertIn(row["color"], ("0", "1"))
                self.assertEqual(frames, sorted(frames))

    def test_f2_valid_observations_and_explicit_empty_frames(self):
        rows = self.read_csv(DATA / "F2/F2_observations.csv", F2_COLUMNS)
        frames = {}
        for row in rows:
            frame = int(row["frame"])
            timestamp = float(row["timestamp"])
            self.assert_finite(timestamp)
            self.assertGreaterEqual(frame, 0)
            self.assertAlmostEqual(timestamp, frame / 30.0, delta=1e-6)
            self.assertIn(row["measurement_valid"], ("0", "1"))
            frames.setdefault(frame, []).append(row)
            if row["measurement_valid"] == "0":
                for column in ("observation_id", "x_px", "y_px", "score", "color"):
                    self.assertEqual(row[column], "")
            else:
                self.assertTrue(row["observation_id"])
                for column in ("x_px", "y_px", "score"):
                    self.assert_finite(float(row[column]))
                self.assertGreaterEqual(float(row["score"]), 0)
                self.assertLessEqual(float(row["score"]), 1)
                self.assertIn(row["color"], ("red", "blue"))
        self.assertEqual(list(frames), list(range(max(frames) + 1)))
        row_frames = [int(row["frame"]) for row in rows]
        self.assertEqual(row_frames, sorted(row_frames))
        empty = []
        for frame, observations in frames.items():
            if any(row["measurement_valid"] == "0" for row in observations):
                self.assertEqual(len(observations), 1, "An empty-frame placeholder must stand alone")
                empty.append(frame)
        self.assertTrue(empty, "Missing explicit empty frames")
        last_valid = max(frame for frame, observations in frames.items()
                         if any(row["measurement_valid"] == "1" for row in observations))
        self.assertTrue(all(frame in empty for frame in range(last_valid + 1, max(frames) + 1)))
        self.assertGreater((max(frames) - last_valid) / 30.0, 2.0,
                           "The public tail must allow observing the 2-second release deadline")

    def test_f3_schema_time_and_observable_commands(self):
        rows = read_jsonl(DATA / "F3/F3_target_snapshots.jsonl")
        self.assertTrue(rows)
        valid_override = False
        rejected_friend = False
        for row in rows:
            self.assertEqual(set(row), {"case", "timestamp_ms", "targets", "operator_command"})
            self.assertIsInstance(row["case"], str)
            self.assert_finite(row["timestamp_ms"])
            self.assertIsInstance(row["targets"], list)
            ids = []
            for target in row["targets"]:
                self.assertEqual(set(target), F3_TARGET_FIELDS)
                self.assertIsInstance(target["id"], str)
                self.assertTrue(target["id"])
                ids.append(target["id"])
                for field in ("distance_m", "bearing_rad", "threat", "age_ms", "track_age_frames"):
                    self.assert_finite(target[field])
                self.assertGreater(target["distance_m"], 0)
                for field in ("age_ms", "track_age_frames"):
                    self.assertIsInstance(target[field], int)
                    self.assertGreaterEqual(target[field], 0)
                for field in ("is_enemy", "visible", "operator_priority"):
                    self.assertIsInstance(target[field], bool)
            self.assertEqual(len(ids), len(set(ids)))
            command = row["operator_command"]
            if command not in (None, "", "auto"):
                self.assertIsInstance(command, str)
                self.assertFalse(command.startswith("lock:"), "Public commands use raw IDs")
                requested = next((target for target in row["targets"] if target["id"] == command), None)
                self.assertIsNotNone(requested, "Public command scenarios must have observable evidence")
                rejected_friend |= not requested["is_enemy"]
                valid_override |= usable(requested) and any(
                    usable(other) and other["threat"] > requested["threat"] for other in row["targets"])
        timestamps = [row["timestamp_ms"] for row in rows]
        self.assertTrue(all(second - first == 100 for first, second in zip(timestamps, timestamps[1:])))
        self.assertTrue(valid_override, "No legal command can be distinguished from automatic ranking")
        self.assertTrue(rejected_friend, "Missing command that must be rejected because the target is friendly")

    def test_f3_loss_windows_are_distinguishable(self):
        rows = read_jsonl(DATA / "F3/F3_target_snapshots.jsonl")
        timestamps = {row["timestamp_ms"]: index for index, row in enumerate(rows)}
        short_dropout = False
        ids = {target["id"] for row in rows for target in row["targets"]}
        for target_id in ids:
            def invisible(row):
                return any(target["id"] == target_id and target["is_enemy"] and not target["visible"]
                           for target in row["targets"])
            for group in runs(rows, invisible):
                first = timestamps[group[0]["timestamp_ms"]]
                last = timestamps[group[-1]["timestamp_ms"]]
                if first == 0 or last + 1 == len(rows):
                    continue
                before, after = rows[first - 1], rows[last + 1]
                usable_before = any(target["id"] == target_id and usable(target) for target in before["targets"])
                usable_after = any(target["id"] == target_id and usable(target) for target in after["targets"])
                short_dropout |= usable_before and usable_after and (
                    0 < after["timestamp_ms"] - before["timestamp_ms"] <= 1200)
        stale_runs = runs(rows, lambda row: bool(row["targets"]) and
                          all(target["age_ms"] > 200 for target in row["targets"]))
        self.assertTrue(short_dropout, "Missing an observable dropout that fits the identity hold window")
        self.assertTrue(any(group[-1]["timestamp_ms"] - group[0]["timestamp_ms"] > 1200
                            for group in stale_runs),
                        "All-stale input must persist long enough to observe identity release")

    def test_f4_numeric_input_schema_and_boundary_coverage(self):
        rows = read_jsonl(DATA / "F4/F4_ballistic_cases.jsonl")
        self.assertTrue(rows)
        ids = []
        physical_no_solution = below_range = above_range = extreme_up = extreme_down = False
        invalid_distance = invalid_speed = invalid_height = False
        gravity = 9.80665
        for row in rows:
            self.assertEqual(set(row), {"case", "distance_m", "height_m", "speed_mps"})
            self.assertIsInstance(row["case"], (str, int))
            ids.append(row["case"])
            for field in ("distance_m", "height_m", "speed_mps"):
                self.assert_finite(row[field])
            distance, height, speed = row["distance_m"], row["height_m"], row["speed_mps"]
            invalid_distance |= distance <= 0.05
            invalid_speed |= not 0.1 < speed <= 60
            invalid_height |= abs(height) > 10
            if distance <= 0.05 or not 0.1 < speed <= 60 or abs(height) > 10:
                continue
            discriminant = speed ** 4 - gravity * (gravity * distance ** 2 + 2 * height * speed ** 2)
            physical_no_solution |= discriminant < 0
            if height == 0:
                range_ratio = distance * gravity / speed ** 2
                below_range |= 0.99 < range_ratio < 1
                above_range |= 1 < range_ratio < 1.01
            if discriminant >= 0:
                # Geometry and the height reached at -45 degrees identify
                # extreme fixtures without implementing a ballistic solver.
                extreme_up |= height > distance
                extreme_down |= (distance * gravity < speed ** 2 and
                                 height < -distance - gravity * distance ** 2 / speed ** 2)
        self.assertEqual(len(ids), len(set(ids)))
        for label, covered in (("physical no-solution", physical_no_solution),
                               ("just below maximum range", below_range),
                               ("just above maximum range", above_range),
                               ("extreme upward angle", extreme_up),
                               ("extreme downward angle", extreme_down),
                               ("invalid distance", invalid_distance),
                               ("invalid speed", invalid_speed),
                               ("invalid height", invalid_height)):
            with self.subTest(scenario=label):
                self.assertTrue(covered, f"Missing public F4 scenario: {label}")


if __name__ == "__main__":
    unittest.main()
