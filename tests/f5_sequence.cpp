// F5 公开序列回放：读 F5_latency_sequence.csv，逐帧调用 predict，
// 写出评分器要求的 frame,stale,predict_horizon_sec,predicted_x_px（每个输入帧一行）
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "f5_baseline.hpp"

namespace {

struct Row {
  int frame = 0;
  rm_assessment::LatencySample sample;
};

std::vector<Row> ReadCsv(const std::string& path) {
  std::ifstream in(path);
  std::vector<Row> rows;
  std::string line;
  std::getline(in, line);  // 表头
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::stringstream ss(line);
    std::string field;
    double value[6];
    int frame = 0;
    for (int i = 0; i < 7; ++i) {
      if (!std::getline(ss, field, ',')) return rows;
      if (i == 0) {
        frame = std::atoi(field.c_str());
      } else {
        value[i - 1] = std::atof(field.c_str());
      }
    }
    Row row;
    row.frame = frame;
    row.sample.now_sec = value[0];
    row.sample.observed_timestamp_sec = value[1];
    row.sample.observed_x_px = value[2];
    row.sample.estimated_velocity_pxps = value[3];
    row.sample.processing_delay_sec = value[4];
    row.sample.flight_time_sec = value[5];
    rows.push_back(row);
  }
  return rows;
}

// 契约里必须判 stale 的边界，逐个直接调用接口验证（构造函数里没有默认路径依赖）。
int CheckStaleRules(const rm_assessment::BaselineLatencyPredictor& predictor) {
  using rm_assessment::LatencySample;
  int failures = 0;
  const auto expect_stale = [&](const LatencySample& sample, bool want, const char* what) {
    const bool got = predictor.predict(sample).stale;
    if (got != want) {
      std::cerr << "  [FAIL] " << what << " stale=" << got << " want=" << want << '\n';
      ++failures;
    } else {
      std::cout << "  [PASS] " << what << '\n';
    }
  };
  const LatencySample base{1.0, 0.9, 320.0, 70.0, 0.10, 0.045};
  expect_stale(base, false, "ok sample");
  expect_stale({1.0, 1.02, 320.0, 70.0, 0.0, 0.045}, true, "observation after now");
  expect_stale({1.0, 0.4, 320.0, 70.0, 0.6, 0.045}, true, "age > 0.5 s");
  expect_stale({1.0, 0.5, 320.0, 70.0, 0.5, 0.045}, false, "age == 0.5 s is not stale");
  expect_stale({1.0, 0.9, 320.0, 70.0, -0.01, 0.045}, true, "negative processing delay");
  expect_stale({1.0, 0.9, 320.0, 70.0, 0.10, -0.01}, true, "negative flight time");
  expect_stale({NAN, 0.9, 320.0, 70.0, 0.10, 0.045}, true, "non-finite now");
  expect_stale({1.0, 0.9, NAN, 70.0, 0.10, 0.045}, true, "non-finite position");
  expect_stale({1.0, 0.9, 320.0, INFINITY, 0.10, 0.045}, true, "infinite velocity");
  // stale 结果也必须有限
  const auto non_finite = predictor.predict({1.0, 0.9, NAN, 70.0, 0.10, 0.045});
  if (!std::isfinite(non_finite.predicted_x_px) || !std::isfinite(non_finite.predict_horizon_sec)) {
    std::cerr << "  [FAIL] stale output must stay finite\n";
    ++failures;
  } else {
    std::cout << "  [PASS] stale output stays finite\n";
  }
  return failures;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "data/F5/F5_latency_sequence.csv";
  const std::vector<Row> rows = ReadCsv(path);
  if (rows.empty()) {
    std::cerr << "cannot read rows from " << path << '\n';
    return 1;
  }

  const rm_assessment::BaselineLatencyPredictor predictor;
  // 输出路径可由第二个参数指定；默认 results/f5_predictions.csv。
  // ctest 的工作目录是 build/，所以这里主动创建父目录，避免依赖调用者的 cwd。
  const std::string output_path = argc > 2 ? argv[2] : "results/f5_predictions.csv";
  if (const std::filesystem::path parent = std::filesystem::path(output_path).parent_path();
      !parent.empty()) {
    std::error_code ignored;
    std::filesystem::create_directories(parent, ignored);
  }
  std::ofstream out(output_path);
  if (!out) {
    std::cerr << "cannot write " << output_path << '\n';
    return 1;
  }
  out << "frame,stale,predict_horizon_sec,predicted_x_px\n";

  int stale_count = 0;
  int non_finite = 0;
  double max_horizon_err = 0.0;
  for (const auto& row : rows) {
    const auto prediction = predictor.predict(row.sample);
    if (prediction.stale) ++stale_count;
    if (!std::isfinite(prediction.predict_horizon_sec) ||
        !std::isfinite(prediction.predicted_x_px)) {
      ++non_finite;
    }
    // 命中时刻 = now + 飞行时间，预测时长 = 命中时刻 − 观测时刻。
    const double want = row.sample.now_sec + row.sample.flight_time_sec -
                        row.sample.observed_timestamp_sec;
    if (!prediction.stale) {
      max_horizon_err = std::max(max_horizon_err, std::abs(prediction.predict_horizon_sec - want));
    }
    out << row.frame << ',' << (prediction.stale ? 1 : 0) << ','
        << prediction.predict_horizon_sec << ',' << prediction.predicted_x_px << '\n';
  }

  std::cout << "frames=" << rows.size() << " stale_frames=" << stale_count
            << " coverage=" << (1.0 - static_cast<double>(stale_count) / rows.size())
            << " non_finite_outputs=" << non_finite
            << " horizon_max_abs_err_sec=" << max_horizon_err << '\n';
  const int failures = CheckStaleRules(predictor);
  const bool ok = failures == 0 && non_finite == 0;
  std::cout << (ok ? "RESULT: PASS" : "RESULT: FAIL") << " (wrote predictions)\n";
  return ok ? 0 : 2;
}
