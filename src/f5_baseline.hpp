#pragma once

#include <cmath>

#include "f5_latency_predictor.hpp"

namespace rm_assessment {

// Runnable constant-velocity baseline with per-sample input checks. Validate
// its behavior against LatencySample's time contract and report comparisons
// using the public evaluator; the starter is not a reference solution.
// 原基线把 processing_delay_sec（已经发生的标称延迟）当成提前量，而docs/DATA_CONTRACTS.md F5规定命中时刻是 now + 飞行时间，
// 预测时长要从观测时刻算起，即 观测年龄 + 飞行时间。两者只在观测被截断到零的前几帧和时延分段处不同——正是原基线算错的地方
class BaselineLatencyPredictor final : public LatencyPredictor {
 public:
  Prediction predict(const LatencySample& sample) const override {
    const double age = sample.now_sec - sample.observed_timestamp_sec;
    const double horizon = age + sample.flight_time_sec;
    // 要求 stale 结果也必须有限，因此位置非有限时退回 0
    const double fallback = std::isfinite(sample.observed_x_px) ? sample.observed_x_px : 0.0;
    if (!std::isfinite(age) || !std::isfinite(horizon) ||
        !std::isfinite(sample.estimated_velocity_pxps) ||
        !std::isfinite(sample.observed_x_px) || sample.processing_delay_sec < 0.0 ||
        sample.flight_time_sec < 0.0 || horizon < 0.0 || age < -1e-6 || age > 0.50) {
      return Prediction{true, 0.0, fallback};
    }
    return Prediction{false, horizon,
                      sample.observed_x_px + sample.estimated_velocity_pxps * horizon};
  }
};

}  // namespace rm_assessment
