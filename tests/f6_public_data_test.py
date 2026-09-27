"""Check published F6 review claims against logs, not a candidate's analyzer."""

import copy
import json
import math
from pathlib import Path
import unittest

DATA = Path(__file__).resolve().parents[1] / "data" / "F6"


def load_logs():
    return {
        name: [json.loads(line) for line in
               (DATA / ("F6_" + name + ".jsonl")).read_text(encoding="utf-8").splitlines()]
        for name in ("detections", "tracking", "commands")
    }


def verify_checkpoint(point, logs):
    """Raise on a false published claim, including interval interiors."""
    maps = {name: {row["frame_id"]: row for row in rows} for name, rows in logs.items()}
    frame = point["frame_id"]
    kind = point["type"]
    source = point.get("module", "tracking")
    assert math.isclose(point["timestamp"], maps[source][frame]["timestamp"], abs_tol=1e-9)
    end = point.get("end_frame_id_exclusive", frame + 1)
    assert end > frame
    for current in range(frame, end):
        det = maps["detections"].get(current)
        track = maps["tracking"][current]
        cmd = maps["commands"][current]
        if kind in ("dropout_window", "unsafe_fire"):
            assert det is None and track["state"] == "TEMP_LOST"
            assert cmd["fire_enable"] == (1 if kind == "unsafe_fire" else 0)
        elif kind == "id_switch":
            assert det is not None and det["target_id"] != track["track_id"]
            assert cmd["selected_id"] == track["track_id"] and cmd["fire_enable"] == 1
        elif kind == "reacquire":
            assert maps["tracking"][current - 1]["state"] == "TEMP_LOST"
            assert current - 1 not in maps["detections"]
            assert det is not None and track["state"] == "TRACK"
        elif kind == "timestamp_reversed":
            rows = logs[source]
            index = next(i for i, row in enumerate(rows) if row["frame_id"] == current)
            assert index > 0 and rows[index]["timestamp"] < rows[index - 1]["timestamp"]
        elif kind == "manual_review":
            assert det is not None and track["state"] == "TRACK"
            assert det["target_id"] == track["track_id"] == cmd["selected_id"]
        else:
            raise AssertionError("unsupported published checkpoint: " + kind)
    if "end_frame_id_exclusive" in point:
        # These public windows end with a usable, consistent tracking record.
        assert maps["tracking"][end]["state"] == "TRACK"
        assert end in maps["detections"]


class PublishedReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.logs = load_logs()
        cls.points = json.loads((DATA / "F6_public_issue_times.json").read_text(encoding="utf-8"))

    def test_log_schema_and_join_keys(self):
        fields = {
            "detections": {"timestamp", "frame_id", "target_id", "x_px", "y_px", "score"},
            "tracking": {"timestamp", "frame_id", "track_id", "state", "x_px", "y_px", "age_ms"},
            "commands": {"timestamp", "frame_id", "selected_id", "fire_enable", "reason"},
        }
        for module, rows in self.logs.items():
            self.assertTrue(rows)
            self.assertEqual(len(rows), len({row["frame_id"] for row in rows}))
            for row in rows:
                self.assertEqual(set(row), fields[module])
                self.assertIs(type(row["frame_id"]), int)
                self.assertGreaterEqual(row["frame_id"], 0)
                self.assertTrue(math.isfinite(row["timestamp"]))
                if module == "commands":
                    self.assertIs(type(row["fire_enable"]), int)
                    self.assertIn(row["fire_enable"], (0, 1))
        frames = {row["frame_id"] for row in self.logs["tracking"]}
        self.assertEqual(frames, {row["frame_id"] for row in self.logs["commands"]})
        self.assertTrue({row["frame_id"] for row in self.logs["detections"]} <= frames)

    def test_every_review_claim_has_evidence(self):
        kinds = {point["type"] for point in self.points}
        self.assertEqual(kinds, {"dropout_window", "reacquire", "id_switch", "unsafe_fire",
                                 "timestamp_reversed", "manual_review"})
        for point in self.points:
            with self.subTest(point=point):
                verify_checkpoint(point, self.logs)

    def test_wrong_review_time_is_rejected(self):
        point = copy.deepcopy(next(p for p in self.points if p["type"] == "reacquire"))
        point["timestamp"] -= 1 / 60
        with self.assertRaises(AssertionError):
            verify_checkpoint(point, self.logs)

    def test_fabricated_id_or_fire_claim_is_rejected(self):
        for kind in ("id_switch", "unsafe_fire"):
            point = next(p for p in self.points if p["type"] == kind)
            logs = copy.deepcopy(self.logs)
            # Mutate an interior frame so checking only the start cannot pass.
            frame = point["end_frame_id_exclusive"] - 1
            if kind == "id_switch":
                row = next(r for r in logs["detections"] if r["frame_id"] == frame)
                row["target_id"] = next(r["track_id"] for r in logs["tracking"]
                                         if r["frame_id"] == frame)
            else:
                next(r for r in logs["commands"] if r["frame_id"] == frame)["fire_enable"] = 0
            with self.subTest(kind=kind), self.assertRaises(AssertionError):
                verify_checkpoint(point, logs)


if __name__ == "__main__":
    unittest.main()
