#pragma once

#include <cmath>

#include "f5_latency_predictor.hpp"

namespace rm_assessment {

// Runnable constant-velocity baseline with per-sample input checks. Validate
// its behavior against LatencySample's time contract and report comparisons
// using the public evaluator; the starter is not a reference solution.
class BaselineLatencyPredictor final : public LatencyPredictor {
 public:
  Prediction predict(const LatencySample& sample) const override {
    const double age = sample.now_sec - sample.observed_timestamp_sec;
    const double horizon = sample.processing_delay_sec + sample.flight_time_sec;
    if (!std::isfinite(age) || !std::isfinite(horizon) || horizon < 0.0 ||
        age < -0.001 || age > 0.50) {
      return Prediction{true, 0.0, sample.observed_x_px};
    }
    return Prediction{false, horizon,
                      sample.observed_x_px + sample.estimated_velocity_pxps * horizon};
  }
};

}  // namespace rm_assessment
