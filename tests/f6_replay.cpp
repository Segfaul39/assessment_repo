// F6 复盘回放：恰好三个 JSONL 输入，打印结构化的 ReplayIssue，并检查
// 重复运行一致性与截断/空文件/缺文件等边界不会崩溃。
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "f6_baseline.hpp"

namespace {

using rm_assessment::BaselineReplayAnalyzer;
using rm_assessment::ReplayIssue;

bool SameRun(const std::vector<ReplayIssue>& a, const std::vector<ReplayIssue>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].timestamp_sec != b[i].timestamp_sec || a[i].type != b[i].type ||
        a[i].evidence != b[i].evidence || a[i].safety_relevant != b[i].safety_relevant) {
      return false;
    }
  }
  return true;
}

void Print(const std::vector<ReplayIssue>& issues) {
  std::map<std::string, std::pair<int, int>> histogram;  // type -> {count, safety}
  for (const auto& issue : issues) {
    auto& entry = histogram[issue.type];
    ++entry.first;
    if (issue.safety_relevant) ++entry.second;
  }
  std::cout << "ISSUE COUNT BY TYPE (total=" << issues.size()
            << ", safety_relevant="
            << std::count_if(issues.begin(), issues.end(),
                             [](const ReplayIssue& i) { return i.safety_relevant; })
            << ")\n";
  for (const auto& [type, entry] : histogram) {
    std::cout << "  " << type << "  count=" << entry.first << " safety=" << entry.second << '\n';
  }
  std::cout << "\nISSUES (timestamp type safety evidence)\n";
  for (const auto& issue : issues) {
    std::cout << "  " << issue.timestamp_sec << "  " << issue.type << "  "
              << (issue.safety_relevant ? "SAFETY" : "info") << "  " << issue.evidence << '\n';
  }
}

// 边界用例：截断的末行、完全空文件、打不开的文件都必须只报证据、不崩溃。
int CheckEdgeCases() {
  int failures = 0;
  const auto require = [&](bool ok, const char* what) {
    std::cout << "  " << (ok ? "[PASS] " : "[FAIL] ") << what << '\n';
    if (!ok) ++failures;
  };
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "f6_replay_edge_cases";
  std::filesystem::create_directories(directory);
  const auto write = [&](const char* name, const std::string& body) {
    std::ofstream out(directory / name);
    out << body;
    return (directory / name).string();
  };

  const std::string two_frames =
      "{\"timestamp\":0.0,\"frame_id\":0,\"track_id\":\"a\",\"state\":\"TRACK\"}\n"
      "{\"timestamp\":0.0166666667,\"frame_id\":1,\"track_id\":\"a\",\"state\":\"TRACK\"}\n";
  const std::string fire_once =
      "{\"timestamp\":0.0,\"frame_id\":0,\"selected_id\":\"a\",\"fire_enable\":1,\"reason\":\"TRACKING\"}\n"
      "{\"timestamp\":0.0166666667,\"frame_id\":1,\"selected_id\":\"a\",\"fire_enable\":1,\"reason\":\"TRACKING\"}\n";

  BaselineReplayAnalyzer analyzer;
  const auto mentions = [](const std::vector<ReplayIssue>& issues, const std::string& needle) {
    return std::any_of(issues.begin(), issues.end(), [&](const ReplayIssue& issue) {
      return issue.evidence.find(needle) != std::string::npos;
    });
  };

  // 1) 检测日志被截断：第 1 帧的记录不存在，必须按缺行报出证据而不是崩溃。
  const std::string truncated_body =
      "{\"timestamp\":0.0,\"frame_id\":0,\"target_id\":\"a\"}\n{\"timestamp\":0.0166666667,\"frame_i";
  const std::string tracking_path = write("tracking.jsonl", two_frames);
  const std::string commands_path = write("commands.jsonl", fire_once);
  const auto truncated =
      analyzer.analyze(write("detections.jsonl", truncated_body), tracking_path, commands_path);
  require(mentions(truncated, "frames 1-1"), "truncated final line leaves frame 1 as a reported gap");

  // 2) 检测文件为空：其余两份日志仍在，必须按缺行报出证据而不是崩溃。
  const auto empty_detections = analyzer.analyze(write("empty.jsonl", ""), tracking_path, commands_path);
  require(mentions(empty_detections, "frames 0-1"), "empty detections log is reported as a gap");

  // 3) 文件不存在：报 missing_log 并带上模块名。
  const auto missing =
      analyzer.analyze((directory / "nope.jsonl").string(), tracking_path, commands_path);
  require(mentions(missing, "detections") && mentions(missing, "cannot be opened"),
          "missing detections file is reported with the module name");
  require(mentions(missing, "frames 0-1"),
          "missing detections log still allows the other two logs to be cross-checked");

  // 4) 每个边界输入的两次运行必须完全一致。
  require(SameRun(truncated, analyzer.analyze(write("detections.jsonl", truncated_body),
                                              tracking_path, commands_path)),
          "truncated input is reproducible");
  require(SameRun(empty_detections,
                  analyzer.analyze(write("empty.jsonl", ""), tracking_path, commands_path)),
          "empty input is reproducible");

  std::error_code ignored;
  std::filesystem::remove_all(directory, ignored);
  return failures;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: f6_replay <detections.jsonl> <tracking.jsonl> <commands.jsonl>\n";
    return 1;
  }
  BaselineReplayAnalyzer analyzer;
  const auto issues = analyzer.analyze(argv[1], argv[2], argv[3]);
  Print(issues);

  const auto second = analyzer.analyze(argv[1], argv[2], argv[3]);
  const bool reproducible = SameRun(issues, second);
  std::cout << "\nreproducible=" << (reproducible ? "yes" : "no") << '\n';

  const int failures = CheckEdgeCases();
  const bool ok = reproducible && failures == 0;
  std::cout << (ok ? "RESULT: PASS" : "RESULT: FAIL") << '\n';
  return ok ? 0 : 2;
}
