#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "f2_tracker.hpp"

namespace rm_assessment {

// Small executable baseline. It confirms a target twice, follows the nearest
// plausible observation, predicts briefly through a gap, and then releases.
// The matching gate and state timings are intentionally imperfect.
// 在原基线上就地补齐要求的四点：
//   1) 关联门限随丢失时长增长——目标重新出现时位置已经变化，固定门限永远匹配不上；
//   2) 释放改为按时间戳计算：连续没有有效匹配超过 2 秒进入 Lost 并清空 center_px原基线按固定帧数释放，帧率变化时行为不一致）；
//   3) 丢失期间的匹配需连续确认 2 帧才恢复 Tracking，误检不能 立即抢锁；
//   4) 重捕获确认后照常更新位置与速度。
// 时间量一律用 timestamp_sec 之差；帧数只用于输出，不参与任何判定。
class BaselineReacquisitionTracker final : public ReacquisitionTracker {
 public:
  // 阈值单位：像素、秒、帧。
  static constexpr double kMinScore = 0.35;            // 参与关联的最低分数
  static constexpr double kLostAfterSec = 2.0;         // 无有效匹配多久释放（契约值）
  static constexpr double kGateBasePx = 80.0;          // Tracking 时的关联门限
  static constexpr double kGateTempLostPx = 130.0;     // 刚进入 TempLost 时的门限
  static constexpr double kGateGrowthPxPerSec = 90.0;  // TempLost 期间门限增长速率
  static constexpr double kGateMaxPx = 200.0;          // 门限上限
  static constexpr int kConfirmFrames = 2;             // 新目标与重捕获的确认帧数

  TrackOutput update(double timestamp_sec,
                     const std::vector<AnonymousObservation>& observations) override {
    const bool have_center = center_.has_value();
    // dt 是本次调用与上一次调用的时间差，每次都推进，避免丢失期间重复累加外推。
    const double dt =
        have_center && timestamp_sec > last_timestamp_ ? timestamp_sec - last_timestamp_ : 0.0;
    last_timestamp_ = timestamp_sec;
    const double lost_sec = have_center ? std::max(0.0, timestamp_sec - last_match_sec_) : 0.0;

    const float predicted_x =
        have_center ? center_->first + velocity_.first * static_cast<float>(dt) : 0.0F;
    const float predicted_y =
        have_center ? center_->second + velocity_.second * static_cast<float>(dt) : 0.0F;

    double gate = kGateBasePx;
    if (state_ == TrackState::TempLost) {
      gate = std::min(kGateMaxPx, kGateTempLostPx + kGateGrowthPxPerSec * lost_sec);
    } else if (state_ == TrackState::Lost) {
      gate = kGateMaxPx;
    }
    // 确认中的重捕获以首个候选为参照，避免用已经过时的预测值去卡第二帧。
    const float ref_x = reacquire_frames_ > 0 ? reacquire_x_ : predicted_x;
    const float ref_y = reacquire_frames_ > 0 ? reacquire_y_ : predicted_y;

    const AnonymousObservation* best = nullptr;
    double best_cost = std::numeric_limits<double>::infinity();
    for (const auto& observation : observations) {
      if (!observation.valid || observation.score < kMinScore) continue;
      const double dx = have_center ? observation.x_px - ref_x : 0.0;
      const double dy = have_center ? observation.y_px - ref_y : 0.0;
      const double distance = std::sqrt(dx * dx + dy * dy);
      if (distance <= gate && (best == nullptr || distance < best_cost ||
                               (distance == best_cost && observation.score > best->score))) {
        best = &observation;
        best_cost = distance;
      }
    }

    if (best != nullptr) {
      if (state_ == TrackState::TempLost) {
        // 丢失期间要求连续 kConfirmFrames 帧确认，单帧误检抢不走锁。
        if (reacquire_frames_ == 0) {
          reacquire_x_ = best->x_px;
          reacquire_y_ = best->y_px;
        }
        ++reacquire_frames_;
        missed_frames_ = 0;
        if (reacquire_frames_ < kConfirmFrames) {
          return TrackOutput{state_, center_, missed_frames_};  // 仍输出有限预测值
        }
        // 确认通过：用新观测更新位置与速度，恢复跟踪。
        if (center_.has_value() && lost_sec > 0.0) {
          velocity_.first = (best->x_px - center_->first) / static_cast<float>(lost_sec);
          velocity_.second = (best->y_px - center_->second) / static_cast<float>(lost_sec);
        }
        center_ = std::make_pair(best->x_px, best->y_px);
        last_match_sec_ = timestamp_sec;
        reacquire_frames_ = 0;
        state_ = TrackState::Tracking;
        return TrackOutput{state_, center_, missed_frames_};
      }
      if (center_.has_value() && dt > 0.0) {
        velocity_.first = (best->x_px - center_->first) / static_cast<float>(dt);
        velocity_.second = (best->y_px - center_->second) / static_cast<float>(dt);
      }
      center_ = std::make_pair(best->x_px, best->y_px);
      last_match_sec_ = timestamp_sec;
      missed_frames_ = 0;
      reacquire_frames_ = 0;
      if (state_ == TrackState::Idle || state_ == TrackState::Lost) {
        state_ = TrackState::Confirming;
        confirmation_frames_ = 1;
      } else if (state_ == TrackState::Confirming) {
        ++confirmation_frames_;
        if (confirmation_frames_ >= kConfirmFrames) state_ = TrackState::Tracking;
      } else {
        state_ = TrackState::Tracking;
      }
    } else if (state_ == TrackState::Tracking || state_ == TrackState::Confirming ||
               state_ == TrackState::TempLost) {
      ++missed_frames_;
      reacquire_frames_ = 0;
      if (state_ != TrackState::Confirming) {
        state_ = TrackState::TempLost;
        // 短丢失期间允许有限预测：按最后已知速度外推一步。
        if (center_.has_value()) {
          center_->first += velocity_.first * static_cast<float>(dt);
          center_->second += velocity_.second * static_cast<float>(dt);
        }
      }
      // 连续没有有效匹配超过 2 秒必须进入 Lost 并清空 center_px。
      if (have_center && timestamp_sec - last_match_sec_ > kLostAfterSec) {
        state_ = TrackState::Lost;
        center_.reset();
        velocity_ = {0.0F, 0.0F};
        reacquire_frames_ = 0;
      }
    }
    return TrackOutput{state_, center_, missed_frames_};
  }

  void reset() override {
    state_ = TrackState::Idle;
    center_.reset();
    velocity_ = {0.0F, 0.0F};
    last_timestamp_ = 0.0;
    last_match_sec_ = 0.0;
    missed_frames_ = 0;
    confirmation_frames_ = 0;
    reacquire_frames_ = 0;
  }

 private:
  TrackState state_ = TrackState::Idle;
  std::optional<std::pair<float, float>> center_;
  std::pair<float, float> velocity_{0.0F, 0.0F};
  double last_timestamp_ = 0.0;
  double last_match_sec_ = 0.0;  // 最后一次有效匹配的时刻，用于丢失时长
  int missed_frames_ = 0;
  int confirmation_frames_ = 0;
  int reacquire_frames_ = 0;   // 重捕获的连续确认计数
  float reacquire_x_ = 0.0F;   // 确认期内首个候选的位置
  float reacquire_y_ = 0.0F;
};

}  // namespace rm_assessment
