#pragma once

#include <cmath>
#include <limits>

#include "f4_ballistic_solver.hpp"

namespace rm_assessment {

// Runnable no-drag low-arc baseline. Angles are radians and flight time is
// seconds. Validate its behavior against BallisticSolver's documented contract
// and provide boundary tests as part of the submission.
class BaselineBallisticSolver final : public BallisticSolver {
 public:
  BallisticResult solve(double distance_m, double height_m,
                        double bullet_speed_mps) const override {
    constexpr double gravity = 9.80665;
    if (!std::isfinite(distance_m) || !std::isfinite(height_m) ||
        !std::isfinite(bullet_speed_mps) || distance_m <= 0.05 ||
        bullet_speed_mps <= 0.1 || bullet_speed_mps > 60.0 ||
        std::abs(height_m) > 10.0) {
      return {};
    }
    const double speed2 = bullet_speed_mps * bullet_speed_mps;
    const double discriminant = speed2 * speed2 - gravity *
        (gravity * distance_m * distance_m + 2.0 * height_m * speed2);
    if (!std::isfinite(discriminant) || discriminant < 0.0) return {};
    const double tan_pitch = (speed2 - std::sqrt(discriminant)) /
                             (gravity * distance_m);
    const double pitch = std::atan(tan_pitch);
    const double horizontal_speed = bullet_speed_mps * std::cos(pitch);
    if (!std::isfinite(pitch) || !std::isfinite(horizontal_speed) ||
        horizontal_speed <= 1e-6) return {};
    const double flight_time = distance_m / horizontal_speed;
    if (!std::isfinite(flight_time)) return {};
    return BallisticResult{true, pitch, flight_time};
  }
};

}  // namespace rm_assessment
