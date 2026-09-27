#pragma once

namespace rm_assessment {

struct LatencySample {
  // CSV timestamp_s: current processing/command-send time in seconds. Sending
  // is immediate in this exercise; there is no additional serial/queue delay.
  double now_sec = 0.0;
  // CSV observed_timestamp_s: capture time of both observation and velocity.
  double observed_timestamp_sec = 0.0;
  double observed_x_px = 0.0;
  // Upstream estimate at observed_timestamp_sec, using no later observations.
  double estimated_velocity_pxps = 0.0;
  // CSV fixed_processing_delay_s: nominal delay already elapsed before now.
  // Despite the historical column name, it varies between sequence phases.
  // It is not a second delay to add after now_sec; timestamps determine age.
  double processing_delay_sec = 0.0;
  // Remaining command-send-to-hit time; hit time is now_sec + flight_time_sec.
  double flight_time_sec = 0.0;
};

struct Prediction {
  // A stale prediction must not be used as a valid compensated aim position.
  bool stale = false;
  // Duration from the observation time to the predicted hit time, in seconds.
  double predict_horizon_sec = 0.0;
  double predicted_x_px = 0.0;
};

class LatencyPredictor {
 public:
  virtual ~LatencyPredictor() = default;
  virtual Prediction predict(const LatencySample& sample) const = 0;
};

// f5_baseline.hpp contains a runnable one-line constant-velocity baseline.
// The task is to correct its time semantics and stale handling, not to create
// a camera, serial driver, or projectile model.

}  // namespace rm_assessment
