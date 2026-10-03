// F2 公开序列回放：按 frame 分组，每帧调用一次 update，报告首次锁定、重捕获事件与最终释放帧；并针对契约改动过的规则做几条定点校验。
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "f2_baseline.hpp"

namespace {

using rm_assessment::AnonymousObservation;
using rm_assessment::BaselineReacquisitionTracker;
using rm_assessment::TrackOutput;
using rm_assessment::TrackState;

const char* Name(TrackState state) {
  switch (state) {
    case TrackState::Idle: return "Idle";
    case TrackState::Confirming: return "Confirming";
    case TrackState::Tracking: return "Tracking";
    case TrackState::TempLost: return "TempLost";
    case TrackState::Lost: return "Lost";
  }
  return "?";
}

struct Frame {
  int id = 0;
  double timestamp_sec = 0.0;
  std::vector<AnonymousObservation> observations;  // 只含 measurement_valid=1 的行
};

std::vector<Frame> ReadFrames(const std::string& path) {
  std::ifstream in(path);
  std::vector<Frame> frames;
  std::string line;
  std::getline(in, line);  // 表头
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::stringstream ss(line);
    std::vector<std::string> field;
    std::string item;
    while (std::getline(ss, item, ',')) field.push_back(item);
    if (field.size() < 8) continue;
    const int frame_id = std::atoi(field[0].c_str());
    if (frames.empty() || frames.back().id != frame_id) {
      Frame frame;
      frame.id = frame_id;
      frame.timestamp_sec = std::atof(field[1].c_str());
      frames.push_back(frame);
    }
    // measurement_valid=0 是空帧占位行，其余字段为空——先判有效位再解析。
    if (field[7] != "1") continue;
    AnonymousObservation observation;
    observation.frame = frame_id;
    observation.timestamp_sec = frames.back().timestamp_sec;
    observation.x_px = static_cast<float>(std::atof(field[3].c_str()));
    observation.y_px = static_cast<float>(std::atof(field[4].c_str()));
    observation.score = static_cast<float>(std::atof(field[5].c_str()));
    observation.valid = true;
    frames.back().observations.push_back(observation);
  }
  return frames;
}

// 定点校验：驱动真实类，检查契约改动过的规则确实生效。
int CheckChangedRules() {
  int failures = 0;
  const auto require = [&](bool ok, const char* what) {
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what << '\n';
    if (!ok) ++failures;
  };
  const auto observe = [](double x, double y, float score) {
    AnonymousObservation o;
    o.x_px = static_cast<float>(x);
    o.y_px = static_cast<float>(y);
    o.score = score;
    return o;
  };

  // 1) 连续没有有效匹配超过 2 秒才释放，且释放后 center_px 清空。
  {
    BaselineReacquisitionTracker tracker;
    for (int i = 0; i < 10; ++i) tracker.update(i / 30.0, {observe(100 + i, 200, 0.9F)});
    const TrackOutput at_1s = tracker.update(1.0, {});
    for (int i = 31; i <= 91; ++i) tracker.update(i / 30.0, {});
    const TrackOutput at_3s = tracker.update(91 / 30.0, {});
    require(at_1s.state == TrackState::TempLost && at_1s.center_px.has_value(),
            "1 s gap stays TempLost with a finite prediction");
    require(at_3s.state == TrackState::Lost && !at_3s.center_px.has_value(),
            "gap over 2 s releases and clears center_px");
  }

  // 2) 丢失期间的单帧亮误检不能立即抢锁。
  {
    BaselineReacquisitionTracker tracker;
    for (int i = 0; i < 10; ++i) tracker.update(i / 30.0, {observe(100 + i, 200, 0.9F)});
    tracker.update(10 / 30.0, {});
    tracker.update(11 / 30.0, {});
    const TrackOutput decoy = tracker.update(12 / 30.0, {observe(140, 200, 0.99F)});
    require(decoy.state == TrackState::TempLost, "single bright false detection must not steal the lock");
    const TrackOutput confirmed = tracker.update(13 / 30.0, {observe(141, 200, 0.99F)});
    require(confirmed.state == TrackState::Tracking, "two consistent frames do reacquire");
  }

  // 3) 输出恒有限；空观测不产生 NaN。
  {
    BaselineReacquisitionTracker tracker;
    tracker.update(0.0, {});
    tracker.update(NAN, {observe(1, 1, 0.9F)});
    const TrackOutput out = tracker.update(1.0, {observe(1, 1, 0.9F)});
    require(!out.center_px.has_value() || (std::isfinite(out.center_px->first) &&
                                           std::isfinite(out.center_px->second)),
            "outputs stay finite on malformed input");
  }
  return failures;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "data/F2/F2_observations.csv";
  const std::vector<Frame> frames = ReadFrames(path);
  if (frames.empty()) {
    std::cerr << "cannot read frames from " << path << '\n';
    return 1;
  }

  BaselineReacquisitionTracker tracker;
  TrackState previous = TrackState::Idle;
  int first_lock = -1;
  int reacquisitions = 0;
  int releases = 0;
  int last_release = -1;
  int non_finite = 0;
  std::cout << "frame,state,center_x,state_change\n";
  for (const auto& frame : frames) {
    const TrackOutput out = tracker.update(frame.timestamp_sec, frame.observations);
    if (out.center_px.has_value()) {
      if (!std::isfinite(out.center_px->first) || !std::isfinite(out.center_px->second)) {
        ++non_finite;
      }
    }
    if (out.state == TrackState::Tracking && previous != TrackState::Tracking && first_lock < 0) {
      first_lock = frame.id;
    }
    if (out.state == TrackState::Tracking && previous == TrackState::TempLost) ++reacquisitions;
    if (out.state == TrackState::Lost && previous != TrackState::Lost) {
      ++releases;
      last_release = frame.id;
    }
    // 契约：Lost 状态必须没有 center_px。
    if (out.state == TrackState::Lost && out.center_px.has_value()) ++non_finite;
    if (out.state != previous) {
      std::cout << frame.id << ',' << Name(out.state) << ','
                << (out.center_px.has_value() ? out.center_px->first : 0.0F) << ','
                << Name(previous) << "->" << Name(out.state) << '\n';
      previous = out.state;
    }
  }

  std::cout << "frames=" << frames.size() << " first_lock_frame=" << first_lock
            << " reacquire_events=" << reacquisitions << " release_events=" << releases
            << " last_release_frame=" << last_release << " non_finite_outputs=" << non_finite
            << '\n';
  const int failures = CheckChangedRules();
  const bool ok = failures == 0 && non_finite == 0;
  std::cout << (ok ? "RESULT: PASS" : "RESULT: FAIL") << '\n';
  return ok ? 0 : 2;
}
