#pragma once

#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <tuple>
#include <vector>

#include "f6_replay_analyzer.hpp"

namespace rm_assessment {

// 在原基线上就地补齐契约（docs/DATA_CONTRACTS.md F6）要求、而原基线没做的三项：
//   1) detection_dropout：跟踪有记录、检测整行缺失的区间（按连续帧合并）；
//   2) detection_tracking_id_mismatch：同一 frame_id 下检测 ID 与跟踪 ID 不一致；
//   3) fire_on_unsafe_reason：命令自身的 reason 字段写着失效/降级却仍在下发开火。
// 三份日志一律按 frame_id 关联，各模块的时间戳仍按**原始行序**检查（不预先排序，
// 否则会抹掉时间倒退证据）。缺行/空文件/打不开只报证据，不崩溃。
class BaselineReplayAnalyzer final : public ReplayAnalyzer {
 public:
  std::vector<ReplayIssue> analyze(const std::string& detections_path,
                                   const std::string& tracking_path,
                                   const std::string& commands_path) override {
    std::vector<ReplayIssue> issues;
    CheckTimestamps(detections_path, "detections", issues);
    CheckTimestamps(tracking_path, "tracking", issues);
    CheckTimestamps(commands_path, "commands", issues);

    std::map<long long, std::string> detection_ids;
    std::map<long long, Track> tracking;
    std::map<long long, Command> commands;
    ReadDetections(detections_path, detection_ids);
    ReadTracking(tracking_path, tracking);
    ReadCommands(commands_path, commands);

    for (const auto& [frame, command] : commands) {
      const auto state = tracking.find(frame);
      if (command.enabled && (state == tracking.end() || state->second.state != "TRACK")) {
        issues.push_back(ReplayIssue{command.timestamp_sec,
                                     "fire_while_untracked",
                                     "commands frame_id=" + std::to_string(frame) +
                                         " fire_enable>0; tracking state=" +
                                         (state == tracking.end() ? "absent" : state->second.state),
                                     true});
      }
      // reason 只是日志自述，必须和另外两份日志交叉检查：写着失效却仍开火。
      if (command.enabled && UnsafeReason(command.reason)) {
        issues.push_back(ReplayIssue{command.timestamp_sec, "fire_on_unsafe_reason",
                                     "commands frame_id=" + std::to_string(frame) + " reason=" +
                                         command.reason + " while fire_enable>0",
                                     true});
      }
    }

    // 检测缺失区间：跟踪有记录而检测没有。缺一行只证明记录不存在，
    // 不能据此断言相机硬件故障，因此证据里写明范围与不确定性。
    long long range_start = -1;
    long long range_end = -1;
    double range_timestamp = 0.0;
    const auto flush = [&]() {
      if (range_start < 0) return;
      issues.push_back(ReplayIssue{
          range_timestamp, "detection_dropout",
          "detections: frames " + std::to_string(range_start) + "-" + std::to_string(range_end) +
              " have a tracking record but no detections record; a missing row only proves the "
              "record is absent, it is not proof of a camera hardware fault",
          false});
      range_start = -1;
    };
    for (const auto& [frame, track] : tracking) {
      if (detection_ids.count(frame) != 0) {
        flush();
        continue;
      }
      if (range_start < 0) {
        range_start = frame;
        range_timestamp = track.timestamp_sec;
      }
      range_end = frame;
    }
    flush();

    // 同一帧下检测 ID 与跟踪 ID 不一致：带火即属安全相关。
    for (const auto& [frame, detection_id] : detection_ids) {
      const auto track = tracking.find(frame);
      if (track == tracking.end() || track->second.id.empty() || track->second.id == detection_id) {
        continue;
      }
      const auto command = commands.find(frame);
      const bool firing = command != commands.end() && command->second.enabled;
      issues.push_back(ReplayIssue{
          track->second.timestamp_sec, "detection_tracking_id_mismatch",
          "detections frame_id=" + std::to_string(frame) + " target_id=" + detection_id +
              " vs tracking track_id=" + track->second.id +
              (firing ? "; commands fire_enable>0 on the mismatched frame" : ""),
          firing});
    }

    std::sort(issues.begin(), issues.end(), [](const ReplayIssue& a, const ReplayIssue& b) {
      return std::tie(a.timestamp_sec, a.type, a.evidence, a.safety_relevant) <
             std::tie(b.timestamp_sec, b.type, b.evidence, b.safety_relevant);
    });
    return issues;
  }

 private:
  struct Track {
    double timestamp_sec = 0.0;
    std::string state;
    std::string id;
  };

  struct Command {
    double timestamp_sec = 0.0;
    bool enabled = false;
    std::string reason;
  };

  // reason 里出现这些词说明命令自述已经失效或降级，此时开火属于待核查项。
  static bool UnsafeReason(const std::string& reason) {
    for (const char* word : {"STALE", "LOST", "INVALID", "DEGRADED"}) {
      if (reason.find(word) != std::string::npos) return true;
    }
    return false;
  }

  static bool ReadNumber(const std::string& line, const char* key, double& value) {
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(line, match, pattern)) return false;
    value = std::stod(match[1].str());
    return true;
  }

  static bool ReadLong(const std::string& line, const char* key, long long& value) {
    double number = 0.0;
    if (!ReadNumber(line, key, number)) return false;
    value = static_cast<long long>(number);
    return true;
  }

  static bool ReadText(const std::string& line, const char* key, std::string& value) {
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (!std::regex_search(line, match, pattern)) return false;
    value = match[1].str();
    return true;
  }

  static void CheckTimestamps(const std::string& path, const char* module,
                              std::vector<ReplayIssue>& issues) {
    std::ifstream input(path);
    if (!input) {
      issues.push_back(ReplayIssue{0.0, "missing_log", std::string(module) + " log cannot be opened", true});
      return;
    }
    std::string line;
    double previous = -1.0;
    while (std::getline(input, line)) {
      double timestamp = 0.0;
      if (!ReadNumber(line, "timestamp", timestamp)) continue;
      if (previous >= 0.0 && timestamp < previous) {
        issues.push_back(ReplayIssue{timestamp, "timestamp_reversed",
                                     std::string(module) + " timestamp is earlier than the previous record",
                                     true});
      }
      if (previous >= 0.0 && timestamp - previous > 0.05) {
        issues.push_back(ReplayIssue{timestamp, "dropped_interval",
                                     std::string(module) + " has a gap larger than 50 ms", false});
      }
      previous = timestamp;
    }
  }

  static void ReadDetections(const std::string& path, std::map<long long, std::string>& ids) {
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
      long long frame = 0;
      std::string id;
      if (ReadLong(line, "frame_id", frame) && ReadText(line, "target_id", id)) ids[frame] = id;
    }
  }

  static void ReadTracking(const std::string& path, std::map<long long, Track>& tracks) {
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
      long long frame = 0;
      if (!ReadLong(line, "frame_id", frame)) continue;
      Track track;
      ReadNumber(line, "timestamp", track.timestamp_sec);
      ReadText(line, "state", track.state);
      ReadText(line, "track_id", track.id);
      tracks[frame] = track;
    }
  }

  static void ReadCommands(const std::string& path, std::map<long long, Command>& commands) {
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
      long long frame = 0;
      double value = 0.0;
      if (!ReadLong(line, "frame_id", frame) || !ReadNumber(line, "fire_enable", value)) continue;
      Command command;
      command.enabled = value > 0.0;
      ReadNumber(line, "timestamp", command.timestamp_sec);
      ReadText(line, "reason", command.reason);
      commands[frame] = command;
    }
  }
};

}  // namespace rm_assessment
