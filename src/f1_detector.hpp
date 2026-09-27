#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace rm_assessment {

struct LightbarCandidate {
  // Zero-based video frame. CSV timestamps use the simulation clock (seconds),
  // which is independent of the MP4 playback rate. Group one frame per call.
  std::int64_t frame = 0;
  double timestamp_sec = 0.0;
  // Visible-pixel geometry in image coordinates: +x right, +y down, pixels.
  float cx = 0.0F;
  float cy = 0.0F;
  float length = 0.0F;
  // Undirected major-axis angle from image +x, degrees in [0, 180).
  // Parallel axes can differ by nearly 180 degrees across the wrap boundary.
  float angle_deg = 0.0F;
  // Mean dominant color-channel intensity of the visible patch, in [0, 1].
  float brightness = 0.0F;
  std::uint8_t color = 0;  // 0=red, 1=blue
};

struct ArmorPair {
  // Indices into this call's input vector, not CSV line numbers or target IDs.
  int left_index = -1;
  int right_index = -1;
  float confidence = 0.0F;
  bool complete = false;  // both bars survive the visibility/geometry checks
};

// F1 operates after the supplied lightbar extractor. Candidates are anonymous;
// no target/armor truth ID is present in the public input. The candidate does
// not need to implement image decoding, BGR/HSV segmentation, or a neural
// detector; the public CSV is the detector-to-associator contract. Frames with
// no CSV rows are empty input vectors; reset any sequence history between videos.
class ArmorAssociator {
 public:
  virtual ~ArmorAssociator() = default;
  virtual std::vector<ArmorPair> associate(
      const std::vector<LightbarCandidate>& candidates) = 0;
};

}  // namespace rm_assessment
