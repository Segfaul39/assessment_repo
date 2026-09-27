# RoboMaster 算法组正式考核仓库

## 选择题目

F1-F6 六道题完全独立，可以选择多题，但每个 PR 只提交一道题。先读 `TASKS.md` 和 `docs/DATA_CONTRACTS.md`，再打开所选题目的 `src/f*.hpp`、对应数据目录和测试目录。不要修改其他题目的模块。

## Fork、分支和 PR 流程

1. 在算法组提供的 Git 服务页面 fork 本仓库，不要直接向主仓库推送。
2. 从默认分支创建自己的题目分支，例如 `candidate/f3-target-manager`。
3. 每个题目分支只完成一题，提交源码、测试、设计说明和运行结果。
4. 将分支推送到自己的 fork，并向算法组主仓库发起 Pull Request。
5. PR 标题使用 `F3: target manager hysteresis` 这类格式；PR 描述必须写明题号、改动文件、测试命令和已知限制。
6. 算法组会在隐藏测试环境中重新运行 PR，不接受把隐藏答案、绝对路径或原始 `air_vision_27` 源码提交进来。

PR 模板位于 `.github/PULL_REQUEST_TEMPLATE.md`。如果只想讨论设计，可以先开 Draft PR；正式提交前必须让本地编译和合同检查通过。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

需要 CMake 3.16+、C++17 编译器和 Python 3.8+（发布数据检查与公开合成评估器只用 Python 标准库）。`src/f*_baseline.hpp` 是可调用的弱基线类；`contract_compile_test` 做接口冒烟检查，`starter_regression_test` 检查输入格式和时间单位等脚手架契约，Python 测试检查发布数据一致性。这些测试不是六道题的算法通过证明。

除 F6 基线自带简单 JSONL 读取外，候选人需要为所选题目补充文件读取和回放，先跑出基线结果，再替换策略。CSV/JSONL 到接口的映射见 `docs/DATA_CONTRACTS.md`。这些基线不是参考答案，算法组的隐藏数据和评分实现不会放入仓库；F5 另附公开合成评估器，用来统一公开误差报告。

题目允许使用 C++17 标准库。

## 数据位置

数据已经按题目放在本仓库的 `data/` 下。候选人的程序必须从命令行参数读取输入路径，不得写死算法组工作区路径。

F6 分析器只接收三份 JSONL；`F6_public_issue_times.json` 仅供人工核对部分检查点。数据角色、F1 提取命令和 F5 评估命令见 `data/README.md` 与 `docs/DATA_CONTRACTS.md`。

仓库中的 `tests/` 和 `tools/` 均可供考生查看：测试检查公开材料与基础接口，F1 工具仅处理公开视频，F5 评估器仅重建公开合成轨迹。F5 的评分真值和 F6 的公开检查点不得作为被测算法输入。隐藏数据、隐藏评分器和组织者内部审计记录不属于本仓库。
