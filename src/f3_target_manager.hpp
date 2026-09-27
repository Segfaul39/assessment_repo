#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rm_assessment {

struct TargetSnapshot {
  // ID text does not encode allegiance; is_enemy is authoritative.
  std::string id;
  double distance_m = 0.0;
  double bearing_rad = 0.0;
  // Comparable upstream score: larger values mean a higher-priority target.
  double threat = 0.0;
  bool is_enemy = false;
  bool visible = true;
  // Advisory only; this flag must never bypass the enemy/visibility/age checks.
  bool operator_priority = false;
  // Age of the supplied upstream snapshot at timestamp_ms, not the manager's
  // accumulated loss duration. Only 0 <= age_ms <= 200 is usable.
  int age_ms = 0;
  // Upstream maturity hint, not elapsed time since this manager acquired a lock.
  int track_age_frames = 0;
};

struct TargetDecision {
  std::optional<std::string> selected_id;
  bool fire_enable = false;
  std::string state;
  std::string reason;
};

class TargetManager {
 public:
  virtual ~TargetManager() = default;
  // One call per JSONL row, in timestamp_ms order, without resetting at changes
  // of the descriptive "case" field. Commands are target IDs; nullopt, an empty
  // string, or "auto" means automatic selection. The baseline also accepts the
  // legacy "lock:<id>" spelling. An invalid command cannot select an unusable
  // target. TEMP_LOST may retain selected_id, but must set fire_enable=false.
  // In automatic mode, rank initial candidates by descending threat, then
  // ascending distance_m, then ascending id. A challenger must exceed the
  // current threat by at least 0.1 continuously for at least 500 ms to switch.
  // Retain a lost identity while timestamp_ms - last_usable_ms <= 1200; release
  // after that interval. A legal operator ID immediately selects its usable
  // enemy target. reset() clears the lock, timers, and challenger history.
  virtual TargetDecision update(double timestamp_ms,
                                const std::vector<TargetSnapshot>& targets,
                                const std::optional<std::string>& operator_command) = 0;
  virtual void reset() = 0;
};

// A runnable but deliberately twitchy policy is provided in
// f3_baseline.hpp. F3 is about replacing that policy with hysteresis and safe
// release; it is not a request to build a detector or tracker.

}  // namespace rm_assessment
