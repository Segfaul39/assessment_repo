// F3 公开序列回放：每行调用一次 update（全程只 reset 一次，case 变化不重置），
// 报告切换次数、友军锁定、TEMP_LOST 与释放时刻；并针对契约规定的四个阈值做定点校验。
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include "f3_baseline.hpp"

namespace {

using rm_assessment::BaselineTargetManager;
using rm_assessment::TargetDecision;
using rm_assessment::TargetSnapshot;

bool Number(const std::string& text, const char* key, double& value) {
  const std::regex pattern(std::string("\"") + key + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
  std::smatch match;
  if (!std::regex_search(text, match, pattern)) return false;
  value = std::stod(match[1].str());
  return true;
}

bool Text(const std::string& text, const char* key, std::string& value) {
  const std::regex pattern(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch match;
  if (!std::regex_search(text, match, pattern)) return false;
  value = match[1].str();
  return true;
}

bool Flag(const std::string& text, const char* key, bool& value) {
  const std::regex pattern(std::string("\"") + key + "\"\\s*:\\s*(true|false)");
  std::smatch match;
  if (!std::regex_search(text, match, pattern)) return false;
  value = match[1].str() == "true";
  return true;
}

struct Row {
  double timestamp_ms = 0.0;
  std::vector<TargetSnapshot> targets;
  std::optional<std::string> command;
};

std::vector<Row> ReadRows(const std::string& path) {
  std::ifstream input(path);
  std::vector<Row> rows;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    Row row;
    if (!Number(line, "timestamp_ms", row.timestamp_ms)) continue;
    // operator_command 可能是 null，也可能是目标 ID 或 "auto"。
    if (line.find("\"operator_command\":null") == std::string::npos) {
      std::string command;
      if (Text(line, "operator_command", command) && !command.empty() && command != "auto") {
        row.command = command;
      }
    }
    // targets 是对象数组；按 "},{" 切开后逐个按字段名解析（JSON 字段顺序与C++ 聚合成员顺序不同，不能按下标赋值）。
    const std::size_t begin = line.find("\"targets\":[");
    const std::size_t end = line.rfind(']');
    if (begin != std::string::npos && end != std::string::npos && end > begin) {
      std::string body = line.substr(begin + 11, end - begin - 11);
      std::size_t position = 0;
      while (position < body.size()) {
        const std::size_t split = body.find("},{", position);
        const std::string chunk = body.substr(
            position, split == std::string::npos ? std::string::npos : split - position);
        TargetSnapshot target;
        double value = 0.0;
        if (Text(chunk, "id", target.id)) {
          Number(chunk, "distance_m", target.distance_m);
          Number(chunk, "bearing_rad", target.bearing_rad);
          Number(chunk, "threat", target.threat);
          Flag(chunk, "is_enemy", target.is_enemy);
          Flag(chunk, "visible", target.visible);
          Flag(chunk, "operator_priority", target.operator_priority);
          if (Number(chunk, "age_ms", value)) target.age_ms = static_cast<int>(value);
          if (Number(chunk, "track_age_frames", value)) {
            target.track_age_frames = static_cast<int>(value);
          }
          row.targets.push_back(target);
        }
        if (split == std::string::npos) break;
        position = split + 3;
      }
    }
    rows.push_back(row);
  }
  return rows;
}

TargetSnapshot Make(const std::string& id, double threat, double distance, int age_ms = 20) {
  TargetSnapshot target;
  target.id = id;
  target.threat = threat;
  target.distance_m = distance;
  target.age_ms = age_ms;
  target.is_enemy = true;
  target.visible = true;
  return target;
}

// 定点校验：驱动真实类，确认契约规定的阈值确实生效。
int CheckThresholds() {
  int failures = 0;
  const auto require = [&](bool ok, const char* what) {
    std::cout << "  " << (ok ? "[PASS] " : "[FAIL] ") << what << '\n';
    if (!ok) ++failures;
  };

  // 1) 挑战者高出正好 0.1 才切换（0.60-0.50 在二进制下差一点点，必须仍判过关）。
  {
    BaselineTargetManager manager;
    const std::vector<TargetSnapshot> both{Make("a", 0.50, 2.0), Make("b", 0.60, 2.0)};
    manager.update(0.0, {Make("a", 0.50, 2.0)}, std::nullopt);  // 先只给 a，建立锁定
    manager.update(400.0, both, std::nullopt);                  // b 出现并占优，开始计时
    const auto before = manager.update(899.0, both, std::nullopt);
    const auto after = manager.update(900.0, both, std::nullopt);
    require(before.selected_id == std::optional<std::string>("a"),
            "advantage of exactly 0.1 does not switch before 500 ms");
    require(after.selected_id == std::optional<std::string>("b"),
            "advantage of exactly 0.1 switches at 500 ms");
  }

  // 2) 短暂反超（400 ms）不切换。
  {
    BaselineTargetManager manager;
    const std::vector<TargetSnapshot> ahead{Make("a", 0.50, 2.0), Make("b", 0.80, 2.0)};
    manager.update(0.0, {Make("a", 0.50, 2.0)}, std::nullopt);  // 先只给 a，建立锁定
    manager.update(1000.0, ahead, std::nullopt);
    manager.update(1400.0, ahead, std::nullopt);  // 占优仅持续 400 ms
    const auto out = manager.update(1450.0, {Make("a", 0.50, 2.0), Make("b", 0.40, 2.0)}, std::nullopt);
    require(out.selected_id == std::optional<std::string>("a"),
            "a brief 400 ms score crossing does not switch");
  }

  // 3) 保持窗口：1100 ms 仍保留并禁火，超过 1200 ms 才释放。
  {
    BaselineTargetManager manager;
    const std::vector<TargetSnapshot> only{Make("a", 0.7, 2.0)};
    manager.update(0.0, only, std::nullopt);
    const auto held = manager.update(1200.0, {}, std::nullopt);
    const auto gone = manager.update(1300.0, {}, std::nullopt);
    require(held.state == "TEMP_LOST" && held.selected_id == std::optional<std::string>("a") &&
                !held.fire_enable,
            "retained and fire-disabled at 1200 ms");
    require(gone.selected_id == std::nullopt, "released after 1200 ms");
  }

  // 4) 首次选择排序：threat 降序 -> 距离升序 -> ID 字典序。
  {
    BaselineTargetManager manager;
    const auto out = manager.update(
        0.0, {Make("z", 0.5, 1.0), Make("a", 0.5, 1.0), Make("m", 0.5, 0.5)}, std::nullopt);
    require(out.selected_id == std::optional<std::string>("m"),
            "ties break by smaller distance first, then by smaller id");
  }

  // 5) 非法操作手指令（友军）被忽略，既不锁友军也不清除既有锁定。
  {
    TargetSnapshot ally = Make("ally", 1.0, 1.0);
    ally.is_enemy = false;
    BaselineTargetManager manager;
    manager.update(0.0, {Make("a", 0.7, 2.0), ally}, std::nullopt);
    const auto out = manager.update(100.0, {Make("a", 0.7, 2.0), ally}, std::string("ally"));
    require(out.selected_id == std::optional<std::string>("a"),
            "an illegal operator command is ignored and keeps the existing lock");
  }
  return failures;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "data/F3/F3_target_snapshots.jsonl";
  const std::vector<Row> rows = ReadRows(path);
  if (rows.empty()) {
    std::cerr << "cannot read rows from " << path << '\n';
    return 1;
  }

  BaselineTargetManager manager;  // 整个文件只在开始时 reset 一次
  std::optional<std::string> previous;
  int switches = 0;           // 已有锁定被换成另一个目标
  int operator_switches = 0;  // 其中由操作手指令直接触发的
  int acquisitions = 0;       // 从无锁定（首次或释放后）建立锁定
  int ally_selections = 0;
  int temp_lost_entries = 0;
  int reacquisitions = 0;  // TEMP_LOST -> TRACKING，仍是同一目标
  int releases = 0;        // TEMP_LOST -> 释放
  int fire_without_usable = 0;
  std::string previous_state;

  std::cout << "timestamp_ms,selected_id,state,fire_enable,reason\n";
  for (const auto& row : rows) {
    const TargetDecision out = manager.update(row.timestamp_ms, row.targets, row.command);
    if (out.selected_id.has_value() && !previous.has_value()) {
      ++acquisitions;
    } else if (out.selected_id.has_value() && out.selected_id != previous) {
      ++switches;
      if (row.command.has_value()) ++operator_switches;
    }
    previous = out.selected_id;
    if (out.selected_id.has_value()) {
      bool usable = false;
      for (const auto& target : row.targets) {
        if (target.id == *out.selected_id && !target.is_enemy) ++ally_selections;
        if (target.id == *out.selected_id && target.is_enemy && target.visible &&
            target.age_ms >= 0 && target.age_ms <= 200) {
          usable = true;
        }
      }
      if (out.fire_enable && !usable) ++fire_without_usable;
    }
    if (out.state == "TEMP_LOST" && previous_state != "TEMP_LOST") ++temp_lost_entries;
    if (previous_state == "TEMP_LOST" && out.state == "TRACKING") ++reacquisitions;
    if (previous_state == "TEMP_LOST" && out.state != "TEMP_LOST" &&
        out.state != "TRACKING") {
      ++releases;
    }
    previous_state = out.state;
    std::cout << row.timestamp_ms << ',' << (out.selected_id ? *out.selected_id : "-") << ','
              << out.state << ',' << (out.fire_enable ? 1 : 0) << ',' << out.reason << '\n';
  }

  std::cout << "rows=" << rows.size() << " switches=" << switches
            << " operator_switches=" << operator_switches << " acquisitions=" << acquisitions
            << " ally_selections=" << ally_selections
            << " temp_lost_entries=" << temp_lost_entries
            << " reacquisitions=" << reacquisitions << " releases=" << releases
            << " fire_without_usable=" << fire_without_usable << '\n';
  const int failures = CheckThresholds();
  const bool ok = failures == 0 && ally_selections == 0 && fire_without_usable == 0;
  std::cout << (ok ? "RESULT: PASS" : "RESULT: FAIL") << '\n';
  return ok ? 0 : 2;
}
