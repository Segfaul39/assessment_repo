#!/usr/bin/env python3
"""Public data preparation: extract anonymous visible geometry from an F1 video.

Requires Python 3, NumPy and OpenCV. This is data preparation, not a candidate
submission requirement. No simulator labels, complete endpoints or object IDs
are read. Optional seeded clutter represents false detections, not video pixels.
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
import random

import cv2
import numpy as np


FIELDS = [
    "frame", "timestamp", "cx", "cy", "length", "angle_deg", "brightness", "color",
]


def visible_candidates(image: np.ndarray) -> list[dict]:
    """Fit the major axis of each connected patch of visible red/blue pixels."""
    channels = image.astype(np.int16)
    candidates = []
    for color, channel in ((0, 2), (1, 0)):
        dominant = channels[:, :, channel]
        other = np.maximum(channels[:, :, 1], channels[:, :, 2 - channel])
        mask = ((dominant >= 120) & (dominant - other >= 60)).astype(np.uint8)
        count, labels, stats, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)
        for label in range(1, count):
            if stats[label, cv2.CC_STAT_AREA] < 6:
                continue
            ys, xs = np.nonzero(labels == label)
            points = np.column_stack((xs, ys)).astype(np.float64)
            center = points.mean(axis=0)
            centered = points - center
            _, axes = np.linalg.eigh(centered.T @ centered)
            axis = axes[:, -1]
            projection = centered @ axis
            length = float(np.ptp(projection) + 1.0)
            if length < 2.0:
                continue
            candidates.append({
                "cx": round(float(center[0]), 3),
                "cy": round(float(center[1]), 3),
                "length": round(length, 3),
                "angle_deg": round(math.degrees(math.atan2(axis[1], axis[0])) % 180.0, 3) % 180.0,
                "brightness": round(float(dominant[ys, xs].mean()) / 255.0, 3),
                "color": color,
            })
    return candidates


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--video", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timestamp-hz", required=True, type=float,
                        help="Simulation clock frequency; distinct from playback FPS.")
    parser.add_argument("--clutter-period", type=int, default=0,
                        help="Add one synthetic anonymous false detection every N frames; 0 disables.")
    parser.add_argument("--seed", type=int, default=2026)
    args = parser.parse_args()
    if not math.isfinite(args.timestamp_hz) or args.timestamp_hz <= 0:
        parser.error("--timestamp-hz must be finite and positive")
    if args.clutter_period < 0:
        parser.error("--clutter-period must be nonnegative")
    if args.video.resolve() == args.output.resolve():
        parser.error("--output must not overwrite --video")

    video = cv2.VideoCapture(str(args.video))
    if not video.isOpened():
        raise RuntimeError(f"Cannot open video: {args.video}")
    frame_count = int(video.get(cv2.CAP_PROP_FRAME_COUNT))
    playback_fps = video.get(cv2.CAP_PROP_FPS)
    rng = random.Random(args.seed)
    rows = []
    empty_frames = 0
    frame = 0
    try:
        while True:
            ok, image = video.read()
            if not ok:
                break
            candidates = visible_candidates(image)
            if args.clutter_period and frame % args.clutter_period == 0:
                height, width = image.shape[:2]
                candidates.append({
                    "cx": round(rng.uniform(0.05 * width, 0.95 * width), 3),
                    "cy": round(rng.uniform(0.08 * height, 0.92 * height), 3),
                    "length": round(rng.uniform(5, 15), 3),
                    "angle_deg": round(rng.uniform(0, 180), 3) % 180.0,
                    "brightness": 0.35,
                    "color": rng.randrange(2),
                })
            candidates.sort(key=lambda item: (item["cx"], item["cy"], item["color"], item["length"]))
            rows.extend({"frame": frame, "timestamp": frame / args.timestamp_hz, **item}
                        for item in candidates)
            empty_frames += int(not candidates)
            frame += 1
    finally:
        video.release()
    if frame == 0 or (frame_count > 0 and frame != frame_count):
        raise RuntimeError(f"Incomplete video decode: {frame}/{frame_count} frames")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    print(f"{args.output}: {frame} frames, {len(rows)} candidates, {empty_frames} empty frames; "
          f"simulation {args.timestamp_hz:g} Hz, playback {playback_fps:g} FPS")


if __name__ == "__main__":
    main()
