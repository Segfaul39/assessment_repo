#pragma once

namespace rm_assessment {

struct BallisticResult {
  // Invalid results request no firing solution and contain finite zero values;
  // callers must check valid before using pitch_rad or flight_time_sec.
  bool valid = false;
  // Positive pitch is upward; radians. Successful results use the low arc
  // within [-pi/4, pi/4] and a strictly positive flight time in seconds.
  double pitch_rad = 0.0;
  double flight_time_sec = 0.0;
};

class BallisticSolver {
 public:
  virtual ~BallisticSolver() = default;
  // No drag, g = 9.80665 m/s^2. distance_m is horizontal range; height_m is
  // target height relative to the muzzle, positive upward. All inputs must be
  // finite, distance_m > 0.05, 0.1 < bullet_speed_mps <= 60, |height_m| <= 10.
  // For valid results, x=v*cos(pitch)*t and y=v*sin(pitch)*t-g*t*t/2 must each
  // reproduce their input coordinate within 1e-4 m. A physically unreachable
  // target or a low arc outside the permitted pitch interval is invalid.
  virtual BallisticResult solve(double distance_m, double height_m,
                                double bullet_speed_mps) const = 0;
};

// See f4_baseline.hpp for the runnable low-arc baseline. The behavior contract
// above describes the submitted implementation, not a guarantee of the starter.

}  // namespace rm_assessment
