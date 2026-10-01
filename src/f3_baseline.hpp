#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include "f3_target_manager.hpp"

namespace rm_assessment {
  
// Deliberately twitchy baseline: after filtering unusable/friendly entries it
// chooses the highest threat on every call and immediately drops a lost lock.
// F3 candidates add switch hysteresis and temporary-loss handling.
// 在原基线上就地补齐契约（docs/DATA_CONTRACTS.md F3）要求的三件事：
//   1) 切换滞回：挑战者 threat 高出至少 0.1 且连续保持 500 ms 才自动切换；
//   2) TEMP_LOST：当前目标不可用时保留 selected_id 并禁火，超过 1200 ms 才释放；
//   3) 操作手指令就是目标 ID（原基线只认 "lock:" 前缀，指令从未生效）。
// 时间一律用 timestamp_ms 之差计算；case 字段只是场景说明，不重置状态。
class BaselineTargetManager final : public TargetManager {
 public:
  
  static constexpr int kMaxAgeMs = 200;           // 快照可用年龄上限
  static constexpr double kSwitchMargin = 0.1;    // 挑战者需高出的 threat
  static constexpr double kSwitchDwellMs = 500;   // 需连续占优时长
  static constexpr double kReleaseMs = 1200;      // 丢失后保持锁定的时长
  // 契约写的是 高至少 0.1 ，而 0.60-0.50 在二进制下是 0.09999999999999998，
  static constexpr double kEps = 1e-9;

  TargetDecision update(double timestamp_ms,
                        const std::vector<TargetSnapshot>& targets,
                        const std::optional<std::string>& operator_command) override {
    // 可用性过滤：仅敌军、可见、0<=age_ms<=200；无效数值一并排除。
    const auto usable = [](const TargetSnapshot& target) {
      return target.is_enemy && target.visible && target.age_ms >= 0 &&
             target.age_ms <= kMaxAgeMs && std::isfinite(target.threat) &&
             std::isfinite(target.distance_m);
    };
    // 契约的自动选择排序：threat 降序 -> 距离升序 -> ID 字典序。
    const auto better = [](const TargetSnapshot& a, const TargetSnapshot& b) {
      if (a.threat != b.threat) return a.threat > b.threat;
      if (a.distance_m != b.distance_m) return a.distance_m < b.distance_m;
      return a.id < b.id;
    };
    const TargetSnapshot* best = nullptr;
    for (const auto& target : targets) {
      if (usable(target) && (best == nullptr || better(target, *best))) best = &target;
    }

    // 操作手指令：公开格式就是目标 ID（null/空串/auto 表示自动）。兼容旧基线的lock:<id> 拼写。合法的可用敌军 ID 立即覆盖自动选择；非法指令被忽略，不清除既有安全锁定，也绝不锁到友军。
    if (operator_command.has_value() && !operator_command->empty() &&
        *operator_command != "auto") {
      // Public JSONL commands contain the target ID itself. Accept the old
      // lock:<id> spelling too, so existing replay adapters keep working.
      const std::string requested = operator_command->rfind("lock:", 0) == 0
                                        ? operator_command->substr(5)
                                        : *operator_command;
      for (const auto& target : targets) {
        if (target.id == requested && usable(target)) {
          current_id_ = target.id;
          last_usable_ms_ = timestamp_ms;
          challenger_id_.reset();
          return TargetDecision{current_id_, true, "TRACKING", "operator command"};
        }
      }
    }

    if (current_id_.has_value()) {
      const TargetSnapshot* incumbent = nullptr;
      for (const auto& target : targets) {
        if (target.id == *current_id_ && usable(target)) incumbent = &target;
      }
      if (incumbent != nullptr) {
        last_usable_ms_ = timestamp_ms;
        // 滞回：只有稳定占优足够久才切换；优势中断或挑战者变更都重新计时。
        if (best != nullptr && best->id != *current_id_ &&
            best->threat - incumbent->threat + kEps >= kSwitchMargin) {
          if (!challenger_id_.has_value() || *challenger_id_ != best->id) {
            challenger_id_ = best->id;
            challenger_since_ms_ = timestamp_ms;
          } else if (timestamp_ms - challenger_since_ms_ >= kSwitchDwellMs) {
            current_id_ = best->id;
            challenger_id_.reset();
          }
        } else {
          challenger_id_.reset();
        }
        return TargetDecision{current_id_, true, "TRACKING", "lock retained"};
      }
      // 当前目标不可用：TEMP_LOST 保留 selected_id 但禁火；超时才释放。
      if (timestamp_ms - last_usable_ms_ <= kReleaseMs) {
        return TargetDecision{current_id_, false, "TEMP_LOST", "target unavailable"};
      }
      current_id_.reset();
    }

    // 无锁定（首次或刚释放）：立即按排序规则选中当前最优目标。
    challenger_id_.reset();
    if (best == nullptr) {
      return TargetDecision{std::nullopt, false, "IDLE", "no usable enemy"};
    }
    current_id_ = best->id;
    last_usable_ms_ = timestamp_ms;
    return TargetDecision{current_id_, true, "TRACKING", "auto acquire"};
  }

  void reset() override {
    current_id_.reset();
    challenger_id_.reset();
    last_usable_ms_ = 0.0;
  }

 private:
  std::optional<std::string> current_id_;
  std::optional<std::string> challenger_id_;  // 当前观察中的挑战者及其起始时刻
  double challenger_since_ms_ = 0.0;
  double last_usable_ms_ = 0.0;  // 当前目标最后一次可用的时刻
};

}  // namespace rm_assessment
