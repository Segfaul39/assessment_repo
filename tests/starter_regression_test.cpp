#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "f3_baseline.hpp"
#include "f6_baseline.hpp"

namespace {
void Require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

// Only files created by this test are removed; never recursively remove a path.
struct ReplayFixture {
  std::filesystem::path directory;
  ReplayFixture() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    directory = std::filesystem::temp_directory_path() /
                ("assessment-replay-" + std::to_string(suffix));
    Require(std::filesystem::create_directory(directory), "fixture directory collision");
  }
  ~ReplayFixture() {
    std::error_code ignored;
    for (const char* name : {"detections.jsonl", "tracking.jsonl", "commands.jsonl"}) {
      std::filesystem::remove(directory / name, ignored);
    }
    std::filesystem::remove(directory, ignored);
  }
  std::string Path(const char* name) const { return (directory / name).string(); }
  void Write(const char* name, const std::string& contents) const {
    std::ofstream out(Path(name));
    out << contents;
    Require(out.good(), "cannot write fixture");
  }
};

void CheckPublicOperatorCommand() {
  rm_assessment::TargetSnapshot requested;
  requested.id = "target_a";
  requested.is_enemy = true;
  requested.distance_m = 2.0;
  requested.threat = 0.2;
  requested.age_ms = 20;
  auto other = requested;
  other.id = "target_b";
  other.threat = 0.9;
  auto ally = requested;
  ally.id = "ally";
  ally.is_enemy = false;
  ally.threat = 1.0;
  rm_assessment::BaselineTargetManager manager;
  const std::vector<rm_assessment::TargetSnapshot> targets{requested, other, ally};
  const auto choice = manager.update(0.0, targets, requested.id);
  Require(choice.selected_id == requested.id, "raw public operator ID must be recognized");
  manager.reset();
  const auto rejected = manager.update(100.0, targets, ally.id);
  Require(rejected.selected_id != ally.id, "operator command must not select an ally");
}

void CheckReplayUnitsAndMissingRows() {
  ReplayFixture fixture;
  fixture.Write("detections.jsonl", "");
  fixture.Write("tracking.jsonl",
                "{\"timestamp\":12.25,\"frame_id\":700,\"state\":\"TEMP_LOST\"}\n");
  fixture.Write("commands.jsonl",
                "{\"timestamp\":12.25,\"frame_id\":700,\"fire_enable\":1}\n"
                "{\"timestamp\":12.26,\"frame_id\":701,\"fire_enable\":1}\n"
                "{\"timestamp\":12.20,\"frame_id\":702,\"fire_enable\":0}\n");
  rm_assessment::BaselineReplayAnalyzer analyzer;
  const auto run = [&] {
    return analyzer.analyze(fixture.Path("detections.jsonl"),
                            fixture.Path("tracking.jsonl"),
                            fixture.Path("commands.jsonl"));
  };
  const auto first = run();
  const auto second = run();
  Require(first.size() == second.size(), "repeated replay changed the number of findings");
  // Candidates may add findings, choose issue names and use any stable order.
  // Check public event-time/evidence requirements without pinning the starter's
  // exact issue count, category strings or vector indices.
  const auto has_event = [&](double timestamp, const std::string& evidence,
                             bool requires_safety) {
    return std::any_of(first.begin(), first.end(), [&](const auto& issue) {
      return issue.timestamp_sec == timestamp && !issue.type.empty() &&
             issue.evidence.find(evidence) != std::string::npos &&
             (!requires_safety || issue.safety_relevant);
    });
  };
  Require(has_event(12.20, "commands", false),
          "preserve input-order timestamp reversal evidence");
  Require(has_event(12.25, "700", true),
          "ReplayIssue time must use command seconds, not frame ID 700");
  Require(has_event(12.26, "701", true),
          "missing tracking row must produce evidence, not a crash");
  for (std::size_t i = 0; i < first.size(); ++i) {
    Require(first[i].timestamp_sec == second[i].timestamp_sec &&
                first[i].type == second[i].type && first[i].evidence == second[i].evidence &&
                first[i].safety_relevant == second[i].safety_relevant,
            "identical input must produce identical ordered findings");
  }
  const auto missing = analyzer.analyze(fixture.Path("missing.jsonl"),
                                        fixture.Path("tracking.jsonl"),
                                        fixture.Path("commands.jsonl"));
  Require(std::any_of(missing.begin(), missing.end(), [](const auto& issue) {
            return !issue.type.empty() && issue.evidence.find("detections") != std::string::npos;
          }),
          "missing input must be reported");
}
}  // namespace

int main() {
  try {
    CheckPublicOperatorCommand();
    CheckReplayUnitsAndMissingRows();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
