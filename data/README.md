# 公开输入数据

数据按题号分目录，候选人只读取自己所选题目的目录。

| 目录 | 文件 | 用途 |
|---|---|---|
| `F1/` | `F1_single_video.mp4`, `F1_multi_video.mp4`, `F1_single_lightbar_candidates.csv`, `F1_lightbar_candidates.csv` | 单车/多车可见像素灯条候选；视频只供可视化 |
| `F2/` | `F2_observations.csv` | 多候选、丢失和重捕获跟踪 |
| `F3/` | `F3_target_snapshots.jsonl` | 连续多目标锁定和切换 |
| `F4/` | `F4_ballistic_cases.jsonl` | 弹道有效性和边界 |
| `F5/` | `F5_latency_sequence.csv` | 处理时延和飞行时间补偿 |
| `F6/` | `F6_detections.jsonl`, `F6_tracking.jsonl`, `F6_commands.jsonl` | 三份必需的分析器输入 |
| `F6/` | `F6_public_issue_times.json` | 可选人工核对检查点，不传给分析器，不是完整答案 |

运行时输入不包含隐藏配对、轨迹或命中真值。F6 另附部分公开检查点，F5 另有公开合成评分工具；它们不能作为被测算法的输入。隐藏测试会更换部分数值、顺序或故障窗口，只检查已经声明的行为。

F1 的两份 CSV 只包含匿名候选：`frame,timestamp,cx,cy,length,angle_deg,brightness,color`，不含左右标签或真值 ID。提取脚本 `tools/prepare_f1_candidates.py` 从本目录的公开 MP4 解码像素生成候选，组织者重建时额外需要 OpenCV 和 NumPy，普通考生直接读取 CSV。

```bash
python3 tools/prepare_f1_candidates.py --video data/F1/F1_multi_video.mp4 --output data/F1/F1_lightbar_candidates.csv --timestamp-hz 60
python3 tools/prepare_f1_candidates.py --video data/F1/F1_single_video.mp4 --output data/F1/F1_single_lightbar_candidates.csv --timestamp-hz 60
```

命令从仓库根目录执行。两个视频各 600 帧、30 fps 慢放；CSV 时间为帧号除以 60。空帧没有候选行，回放应遍历 `0..599`，对空帧传空向量。F2 空帧则使用 `measurement_valid=0` 占位行。其他字段映射、F3/F4 阈值和 F5 评估方式统一见 [数据与行为契约](../docs/DATA_CONTRACTS.md)。
