#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace rm_assessment {

struct AnonymousObservation {
  std::int64_t frame = 0;
  double timestamp_sec = 0.0;
  float x_px = 0.0F;
  float y_px = 0.0F;
  float score = 0.0F;
  bool valid = true;
};

// Reacquisition is the TempLost -> Tracking transition, not a separate state.
enum class TrackState { Idle, Confirming, Tracking, TempLost, Lost };

struct TrackOutput {
  TrackState state = TrackState::Idle;
  std::optional<std::pair<float, float>> center_px;
  int missed_frames = 0;
};

class ReacquisitionTracker {
 public:
  virtual ~ReacquisitionTracker() = default;
  // Call once per frame, including frames without a valid observation. CSV rows
  // with measurement_valid=0 are frame placeholders and become an empty vector;
  // do not parse their blank coordinates. observation_id/color are not matching
  // inputs. CSV row order and observation IDs do not define persistent identity.
  virtual TrackOutput update(double timestamp_sec,
                             const std::vector<AnonymousObservation>& observations) = 0;
  virtual void reset() = 0;
};

// The repository supplies a small working baseline in f2_baseline.hpp. It is
// intentionally conservative and is the executable starting point for F2;
// candidates improve the state transitions and matching policy rather than
// building a camera or detector.

}  // namespace rm_assessment
