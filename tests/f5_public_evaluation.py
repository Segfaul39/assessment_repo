#!/usr/bin/env python3
"""Generate and score the deterministic F5 public exercise (standard library).

Predictions receive only the measurement CSV. Synthetic motion below is used
by fixture generation and offline scoring, never as an extra predictor input.
Reusing this known trajectory in a predictor violates the task's causal input
contract; hidden sequences use different motion and timing.

    python3 tests/f5_public_evaluation.py generate --output /tmp/f5.csv
    python3 tests/f5_public_evaluation.py score --predictions /tmp/predictions.csv
    python3 tests/f5_public_evaluation.py self-test

The prediction CSV must have exactly these columns, one row per input frame:
frame,stale,predict_horizon_sec,predicted_x_px
stale accepts 0/1 or false/true. Values must be finite, including stale rows.
The scorer reports candidate coverage and compares all methods on the same
accepted rows. It also reports both baselines on all rows, so suppressing hard
rows cannot silently improve the comparison. No accuracy threshold is imposed.
"""

import argparse
import csv
import io
import json
import math
from pathlib import Path
import random
import sys
from tempfile import TemporaryDirectory


MEASUREMENT_COLUMNS = (
    "frame", "timestamp_s", "observed_timestamp_s", "observed_x_px",
    "estimated_velocity_pxps", "fixed_processing_delay_s", "flight_time_s",
)
PREDICTION_COLUMNS = (
    "frame", "stale", "predict_horizon_sec", "predicted_x_px",
)
SEED = 77
FRAME_COUNT = 600
FIXED_DELAY_SEC = 0.06
PUBLIC_MEASUREMENTS = Path(__file__).resolve().parents[1] / "data/F5/F5_latency_sequence.csv"


def motion(timestamp):
    """Synthetic fixture truth; never pass this function to a predictor."""
    if timestamp < 4.0:
        return 320 + 70 * timestamp, 70.0
    if timestamp < 8.0:
        return 600 - 55 * (timestamp - 4.0), -55.0
    phase = 0.85 * (timestamp - 8.0)
    return 380 + 110 * math.sin(phase), 93.5 * math.cos(phase)


def measurements():
    """Match the public fixture, including its original position/noise draws.

    Both noisy measurements refer to the same capture time. In particular,
    velocity cannot reveal a reversal that happened after that capture time.
    """
    rng = random.Random(SEED)
    rows = []
    for frame in range(FRAME_COUNT):
        now = frame / 60.0
        delay = 0.06 if frame < 180 else (0.12 if frame < 360 else 0.18)
        flight = 0.085 if frame % 29 == 0 else 0.045
        observed = max(0, now - delay)
        x, velocity = motion(observed)
        rows.append(dict(zip(MEASUREMENT_COLUMNS, (
            frame, round(now, 6), round(observed, 6),
            round(x + rng.gauss(0, 1.2), 5),
            round(velocity + rng.gauss(0, 4), 5), delay, flight,
        ))))
    return rows


def generate(destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=MEASUREMENT_COLUMNS)
        writer.writeheader()
        writer.writerows(measurements())


def verify_measurements(source):
    """Refuse to score unrelated inputs against this fixture's truth."""
    with source.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if tuple(reader.fieldnames or ()) != MEASUREMENT_COLUMNS:
            raise ValueError("measurement CSV columns do not match the public F5 schema")
        actual = list(reader)
    expected = measurements()
    if len(actual) != len(expected):
        raise ValueError("measurement CSV must contain the 600 generated public rows")
    for line, (actual_row, expected_row) in enumerate(zip(actual, expected), 2):
        if None in actual_row:
            raise ValueError(f"measurement CSV line {line} has extra fields")
        for column in MEASUREMENT_COLUMNS:
            value = float(actual_row[column])
            if not math.isfinite(value) or value != expected_row[column]:
                raise ValueError(f"measurement CSV line {line}: {column} differs from generated input")
    return expected


def parse_predictions(stream, count=FRAME_COUNT):
    reader = csv.DictReader(stream)
    if tuple(reader.fieldnames or ()) != PREDICTION_COLUMNS:
        raise ValueError("prediction CSV must have columns: " + ",".join(PREDICTION_COLUMNS))
    predictions = {}
    for line, row in enumerate(reader, 2):
        if None in row or any(value is None for value in row.values()):
            raise ValueError(f"prediction CSV line {line} has the wrong number of fields")
        try:
            frame = int(row["frame"])
            stale_text = row["stale"].strip().lower()
            if stale_text not in ("0", "1", "false", "true"):
                raise ValueError("stale must be 0/1 or false/true")
            stale = stale_text in ("1", "true")
            horizon = float(row["predict_horizon_sec"])
            position = float(row["predicted_x_px"])
            if not math.isfinite(horizon) or not math.isfinite(position) or horizon < 0:
                raise ValueError("horizon and position must be finite, with horizon >= 0")
            if not 0 <= frame < count or frame in predictions:
                raise ValueError("frame is out of range or duplicated")
        except ValueError as error:
            raise ValueError(f"prediction CSV line {line}: {error}") from error
        predictions[frame] = {"stale": stale, "horizon": horizon, "position": position}
    if len(predictions) != count:
        raise ValueError(f"prediction CSV must cover all {count} frames, including stale rows")
    return predictions


def error_metrics(errors):
    if not errors:
        return {"count": 0, "mae_px": None, "rmse_px": None, "max_abs_px": None}
    return {
        "count": len(errors),
        "mae_px": sum(abs(error) for error in errors) / len(errors),
        "rmse_px": math.sqrt(sum(error * error for error in errors) / len(errors)),
        "max_abs_px": max(abs(error) for error in errors),
    }


def score(rows, predictions):
    errors = {"zero_compensation": [], "fixed_compensation": [], "candidate": []}
    accepted_errors = {name: [] for name in errors}
    horizon_errors = []
    for row in rows:
        frame = row["frame"]
        prediction = predictions[frame]
        hit_time = row["timestamp_s"] + row["flight_time_s"]
        hit_x = motion(hit_time)[0]
        values = {
            "zero_compensation": row["observed_x_px"],
            "fixed_compensation": row["observed_x_px"] + row["estimated_velocity_pxps"]
            * (FIXED_DELAY_SEC + row["flight_time_s"]),
            "candidate": prediction["position"],
        }
        for name, value in values.items():
            error = value - hit_x
            if name != "candidate" or not prediction["stale"]:
                errors[name].append(error)
            if not prediction["stale"]:
                accepted_errors[name].append(error)
        if not prediction["stale"]:
            horizon_errors.append(prediction["horizon"] - (hit_time - row["observed_timestamp_s"]))
    accepted = len(errors["candidate"])
    return {
        "fixture": {"name": "F5_public_synthetic", "seed": SEED, "frames": len(rows)},
        "time_contract": "hit_time = timestamp_s + flight_time_s; sending is immediate",
        "reference": "deterministic synthetic position at hit_time, evaluated offline only",
        "fixed_compensation": "observed_x + estimated_velocity * (0.06 + flight_time_s)",
        "candidate_accepted_frames": accepted,
        "candidate_stale_frames": len(rows) - accepted,
        "candidate_coverage": accepted / len(rows),
        "baseline_all_frames": {
            name: error_metrics(errors[name])
            for name in ("zero_compensation", "fixed_compensation")
        },
        "comparison_on_candidate_accepted_frames": {
            name: error_metrics(values) for name, values in accepted_errors.items()
        },
        "candidate_horizon_max_abs_error_sec": max(map(abs, horizon_errors), default=None),
    }


def self_test():
    rows = measurements()
    assert rows == measurements(), "fixture generation must be deterministic"
    assert tuple(rows[0]) == MEASUREMENT_COLUMNS, "truth must not enter measurement input"
    assert rows[0]["observed_x_px"] == 320.3237, "preserve public position noise"
    assert rows[240]["observed_timestamp_s"] < 4.0
    assert rows[240]["estimated_velocity_pxps"] > 50, "no future reversal in velocity"
    assert rows[480]["observed_timestamp_s"] < 8.0
    assert rows[480]["estimated_velocity_pxps"] < -35, "no future acceleration in velocity"
    assert rows[180]["observed_timestamp_s"] < rows[179]["observed_timestamp_s"]
    assert rows[360]["observed_timestamp_s"] < rows[359]["observed_timestamp_s"]
    rng = random.Random(SEED)
    for row in rows:
        # Reconstruct the capture time before CSV rounding, as the generator does.
        observed = max(0, row["frame"] / 60.0 - row["fixed_processing_delay_s"])
        x, velocity = motion(observed)
        assert row["observed_x_px"] == round(x + rng.gauss(0, 1.2), 5)
        assert row["estimated_velocity_pxps"] == round(velocity + rng.gauss(0, 4), 5)
        assert row["observed_timestamp_s"] <= row["timestamp_s"]
    assert error_metrics([3.0, -4.0]) == {
        "count": 2, "mae_px": 3.5, "rmse_px": math.sqrt(12.5), "max_abs_px": 4.0,
    }
    # Independently calculated case: at t=1.1, x=397. Already elapsed nominal
    # delay must not change that hit time; it is deliberately inconsistent here.
    hand_row = dict(zip(MEASUREMENT_COLUMNS, (0, 1.0, 0.9, 383.0, 70.0, 0.9, 0.1)))
    hand_report = score([hand_row], {0: {"stale": False, "horizon": 0.2, "position": 390.0}})
    hand_errors = hand_report["comparison_on_candidate_accepted_frames"]
    assert math.isclose(hand_errors["zero_compensation"]["mae_px"], 14.0)
    assert math.isclose(hand_errors["fixed_compensation"]["mae_px"], 2.8)
    assert math.isclose(hand_errors["candidate"]["mae_px"], 7.0)
    assert hand_report["candidate_horizon_max_abs_error_sec"] < 1e-12
    # An oracle is used only to test the scorer; it is not a candidate solution.
    oracle = {}
    for row in rows:
        hit = row["timestamp_s"] + row["flight_time_s"]
        oracle[row["frame"]] = {"stale": False,
                                "horizon": hit - row["observed_timestamp_s"],
                                "position": motion(hit)[0]}
    report = score(rows, oracle)
    assert report["candidate_coverage"] == 1.0
    assert report["candidate_horizon_max_abs_error_sec"] == 0.0
    assert report["comparison_on_candidate_accepted_frames"]["candidate"]["mae_px"] == 0.0
    for prediction in oracle.values():
        prediction["stale"] = True
    report = score(rows, oracle)
    assert report["candidate_coverage"] == 0.0
    assert report["comparison_on_candidate_accepted_frames"]["candidate"]["mae_px"] is None
    assert report["baseline_all_frames"]["zero_compensation"]["count"] == FRAME_COUNT
    assert parse_predictions(io.StringIO(",".join(PREDICTION_COLUMNS) + "\n0,false,0.1,320\n"), 1)[0]["position"] == 320
    malformed = (
        "0,false,0.1,nan\n", "0,false,-1,320\n", "0,maybe,0.1,320\n",
        "0,false,0.1,320\n0,false,0.1,320\n", "1,false,0.1,320\n", "",
        "0,false,inf,320\n", "0,true,0.1,inf\n", "0,false,0.1\n",
        "0,false,0.1,320,extra\n", "0.5,false,0.1,320\n",
    )
    for body in malformed:
        try:
            parse_predictions(io.StringIO(",".join(PREDICTION_COLUMNS) + "\n" + body), 1)
        except ValueError:
            pass
        else:
            raise AssertionError("malformed predictions must fail")
    with TemporaryDirectory(prefix="f5-evaluator-self-test-") as directory:
        fixture = Path(directory) / "measurements.csv"
        generate(fixture)
        assert verify_measurements(fixture) == rows
        nan_row = dict(rows[0], observed_x_px=float("nan"))
        future_velocity = dict(rows[240], estimated_velocity_pxps=-60.77476)
        corrupted_inputs = (
            rows[:-1], [rows[0]] + rows[:-1], [nan_row] + rows[1:],
            rows[:240] + [future_velocity] + rows[241:],
        )
        for corrupted in corrupted_inputs:
            with fixture.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=MEASUREMENT_COLUMNS)
                writer.writeheader()
                writer.writerows(corrupted)
            try:
                verify_measurements(fixture)
            except ValueError:
                pass
            else:
                raise AssertionError("missing/duplicate/nonfinite/leaking input rows must fail")
    verify_measurements(PUBLIC_MEASUREMENTS)
    print("F5 evaluator self-test passed: causal measurements, metrics, coverage, CSV validation")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    generator = commands.add_parser("generate", help="write deterministic measurement inputs, without truth columns")
    generator.add_argument("--output", type=Path, required=True)
    scorer = commands.add_parser("score", help="score candidate output against offline synthetic truth")
    scorer.add_argument("--predictions", type=Path, required=True)
    scorer.add_argument("--measurements", type=Path, default=PUBLIC_MEASUREMENTS)
    commands.add_parser("self-test", help="verify generation and the evaluation contract")
    args = parser.parse_args()
    try:
        if args.command == "generate":
            generate(args.output)
            print(f"Wrote {FRAME_COUNT} F5 measurement rows to {args.output}; no truth columns")
        elif args.command == "score":
            rows = verify_measurements(args.measurements)
            with args.predictions.open(newline="", encoding="utf-8") as stream:
                predictions = parse_predictions(stream)
            print(json.dumps(score(rows, predictions), indent=2, allow_nan=False))
        else:
            self_test()
    except (OSError, ValueError, TypeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
