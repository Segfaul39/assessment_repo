#pragma once

#include <string>
#include <vector>

namespace rm_assessment {

struct ReplayIssue {
  // Event time in seconds from the source log, never a frame number.
  double timestamp_sec = 0.0;
  std::string type;
  // Include module names, frame IDs and the fields supporting the finding.
  std::string evidence;
  bool safety_relevant = false;
};

class ReplayAnalyzer {
 public:
  virtual ~ReplayAnalyzer() = default;
  // Only these three JSONL files are runtime inputs. Public issue times are
  // optional human review checkpoints, not an input or an exhaustive oracle.
  virtual std::vector<ReplayIssue> analyze(const std::string& detections_path,
                                           const std::string& tracking_path,
                                           const std::string& commands_path) = 0;
};

// f6_baseline.hpp includes a small JSONL replay checker. Candidates extend
// the checks and evidence, without needing to invent a logging framework.

}  // namespace rm_assessment
