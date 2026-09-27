# 公开数据与行为契约

本文件定义读取器应如何向 `src/` 接口传值，以及验收使用的单位和行为。弱基线不是参考实现；不得根据 `case`、文件名、帧号或公开检查点编写答案分支。可在自己的设计说明中定义未指定的策略，隐藏测试不得依赖未公开的字段或约定。

## F1：可见灯条候选

`F1_lightbar_candidates.csv` 对应多车视频，`F1_single_lightbar_candidates.csv` 对应单车视频。每帧的行按原 CSV 顺序组成 `vector<LightbarCandidate>`；输出左右索引指向这个向量，按图像 x 坐标解释左右，不是全文件行号。不同颜色不应配对；不能把左右标签或目标 ID 作为输入。

| CSV 字段 | 接口字段 / 单位 |
|---|---|
| `frame` | `frame`，从 0 开始的帧号 |
| `timestamp` | `timestamp_sec`，秒 |
| `cx,cy` | 图像中心坐标，像素，x 向右、y 向下 |
| `length` | 可见像素主轴长度，像素 |
| `angle_deg` | 无向长轴相对图像 +x 轴的角度，`[0,180)` 度，按 180 度周期比较 |
| `brightness` | 可见区域颜色强度，`[0,1]`，不是完整性真值 |
| `color` | `0=red,1=blue` |

两个视频各 600 帧，视频编码为 30 fps、仿真为 60 Hz，因此播放器 2 秒对应 CSV 约 1 秒。必须按视频帧号定位候选；`timestamp=frame/60`。无可见候选的帧没有 CSV 行，回放仍应在这些帧调用 `associate({})`。提取器可能合并接触的亮区或产生压缩噪声，不能假定每行必定是一根完整灯条。单根灯条不足以构造完整配对；有歧义时允许拒绝配对。公开 CSV 无配对真值，精确误检率只在带标签测试中计算。

## F2：匿名观测和空帧

按 `frame` 分组，每帧调用一次 `update(timestamp, observations)`，帧内时间戳必须一致。`frame,timestamp,x_px,y_px,score,measurement_valid` 分别映射为 `frame,timestamp_sec,x_px,y_px,score,valid`；`observation_id` 和 `color` 不传入当前接口。ID 只是记录标识，不是可用于关联的目标身份。有效行的坐标为像素，时间为秒。

`measurement_valid=0` 的占位行只有帧号、时间和有效位，其余为空；先检查有效位，不能尝试将空坐标解析为浮点数。该帧无其他有效行时传入空向量。有效观测可按自定的稳定策略初始化，不指定唯一正确的候选 ID；禁止按 CSV 行序或固定 ID 认定“真目标”。

公开序列为 30 Hz，包含约 1 秒的局部候选缺失，以及结尾第 360–449 帧的 3 秒全空观测。短丢失时预测必须有限，连续没有有效匹配超过 2 秒必须进入 `Lost` 并清空 `center_px`。保持时间、确认次数和匹配门限需要解释；不能在观测已经恢复时反复报告同一次重捕获。`TempLost -> Tracking` 是重捕获事件，接口没有 `Reacquired` 枚举。报告公开状态过程；“误检切换次数”须用有标签的自构造/隐藏测试验证。

## F3：连续目标管理

每行调用一次 `update(timestamp_ms, targets, operator_command)`，时间单位为毫秒；整个文件只在开始时 `reset()`，不能按 `case` 分段重置。目标字段按名称赋值，特别注意 JSON 字段排列与 C++ 聚合成员排列不同，应按字段名解析。

- `is_enemy` 是敌我属性，不按 ID 的 red/blue 文本推断；`visible` 表示当前是否可见。
- `age_ms` 是当前快照的上游信息年龄，不是本管理器的丢失计时器。仅敌军、可见且 `0<=age_ms<=200` 的目标可用于选取和开火；无效数值也应排除。
- 自动首次选择按 `threat` 降序、距离升序、ID 字典序打破平局。挑战者须比当前目标 `threat` 高至少 0.1，且连续优势至少 500 ms，才自动切换；优势中断或挑战者变更需重新计时。
- 当前目标不可用时进入 `TEMP_LOST` 并禁火，可以保留 `selected_id`；从最后有效可见时刻算，经过时间 `<=1200 ms` 保留，`>1200 ms` 释放。不能用每一行重复给出的 `age_ms` 代替累计时间。
- `operator_command` 为原始目标 ID，例如 `red_alpha`；`null`、空字符串或 `auto` 表示自动模式，适配为 `nullopt` 或等价值。合法且可用的敌军 ID 立即覆盖自动选择；无效指令被忽略，不得清除既有安全锁定或锁到友军。基线额外兼容 `lock:<id>`，公开格式不要求此前缀。
- `operator_priority` 和 `track_age_frames` 是上游提示，不能绕过敌我、可见性和年龄检查；状态和 `reason` 用可解释字符串，`TEMP_LOST` 必须禁火。

公开短丢失阶段为 6500–7400 ms，7500 ms 重现；全目标过期阶段为 12000–13900 ms，新目标在 14000 ms 出现。后者足够观察到超过 1200 ms 后的释放。隐藏数据会改变绝对时间，策略须使用时间差。

## F4：无阻力低抛解

`distance_m,height_m,speed_mps` 分别传入水平距离、目标相对枪口高度（向上为正）、弹速。`case` 仅是记录标识。所有输入必须有限，输入域为 `distance_m>0.05`、`0.1<speed_mps<=60`、`abs(height_m)<=10`。取 `g=9.80665 m/s²`，返回低抛解，俯仰角向上为正且在 `[-π/4,π/4]`，飞行时间严格为正、单位秒。

成功结果按以下式回代，水平和高度误差各不超过 `1e-4 m`：

```text
x = speed_mps * cos(pitch_rad) * flight_time_sec
y = speed_mps * sin(pitch_rad) * flight_time_sec
    - 0.5 * 9.80665 * flight_time_sec²
```

超输入域、物理无解、低抛角越界或非有限结果统一返回 `valid=false,pitch_rad=0,flight_time_sec=0`。上层先检查 `valid`，不能把零角误当作可开火。JSONL 包含物理无解、最大射程两侧和极端角度的有限数值样本；NaN/Inf 通过 C++ 直接调用接口测试，不能写非标准 JSON。

## F5：观测到命中的时间语义

| CSV 字段 | `LatencySample` 字段 / 含义 |
|---|---|
| `timestamp_s` | `now_sec`，处理完成且立即发送命令的时刻 |
| `observed_timestamp_s` | `observed_timestamp_sec`，观测采样时刻 |
| `observed_x_px` | 观测时刻的带噪位置，像素 |
| `estimated_velocity_pxps` | 同一观测时刻的上游速度估计，像素/秒，不是 now 时刻真值 |
| `fixed_processing_delay_s` | `processing_delay_sec`，已经发生的标称延迟，虽然历史列名含 fixed，实际分阶段变化 |
| `flight_time_s` | `flight_time_sec`，从发送到命中的剩余飞行时间 |

本题额外发送/排队延迟为零，`hit=now+flight_time`。`predict_horizon_sec` 从观测时刻计到命中时刻；标称处理延迟不再作为未来延迟叠加。初始观测时间截断到零，实际观测年龄以两个时间戳之差为准。公开序列 now 递增，但延迟增加会让收到的 observed timestamp 回退；需要解释对重复/旧观测和历史不足的处理，不可读取后续行来给当前预测补信息。接口方法是 `const`，允许自行增加受控历史状态或先由读取器准备因果历史，但不能更改输入时间含义。

无效/非有限输入、观测在 now 之后、负延迟或负飞行时间、观测年龄超过 0.5 秒均须 `stale=true`，输出数值仍须有限；stale 结果不能用于有效瞄准。完整序列没有覆盖所有边界，需自行构造测试。

公开 CSV 没有命中真值列。公开合成评估器可精确重建该 CSV 的带噪输入，并在预测器之外使用可重现的运动函数计算命中参考位置。因此可以评分本仓库公开 CSV 的回放结果，但这些是合成误差，不是实车误差或隐藏评分。合成真值和运动分段不得传入预测器或写成按帧答案。使用方式（从仓库根目录执行）：

```bash
python3 tests/f5_public_evaluation.py generate --output results/f5_measurements.csv
# 自行实现的回放程序读取上面的测量，写出 results/f5_predictions.csv
python3 tests/f5_public_evaluation.py score --predictions results/f5_predictions.csv --measurements results/f5_measurements.csv
```

预测 CSV 列为 `frame,stale,predict_horizon_sec,predicted_x_px`，每个输入帧恰好一行。评分在相同有效帧比较零补偿、固定处理补偿（0.06 秒加飞行时间）和候选方案的 MAE/RMSE，并报告有效覆盖率，不能通过将困难帧全标为 stale 隐藏误差。真正命中时刻始终是 `now+flight_time`。`score` 不传 `--measurements` 时默认核对本仓库公开 CSV；它拒绝将其他轨迹误当成本合成轨迹评分。

## F6：三份日志与人工检查点

`ReplayAnalyzer::analyze(detections_path,tracking_path,commands_path)` 只读取三个 JSONL。公开文件是 60 Hz 的合成复盘数据，正常 `timestamp` 单位为秒；用 `frame_id` 关联，同帧时间差也属于待检查证据。逐模块保留文件行序检查时间倒退，不能先排序消除故障。

| 日志 | 主要字段 / 含义 |
|---|---|
| detections | `target_id,x_px,y_px,score`；检测缺失时整行不存在，不代表可以确定相机硬件故障 |
| tracking | `track_id,state,x_px,y_px,age_ms`；`TRACK` 为可跟踪状态，`TEMP_LOST` / `ID_SWITCH` 应禁火；丢失时坐标是空字符串，不能按零坐标使用 |
| commands | `selected_id,fire_enable,reason`；空 ID 表示未选择，`fire_enable` 为整数 `0/1`；reason 仅是日志自述，必须和另外两份日志交叉检查 |

`age_ms` 为日志生产者记录的上游状态年龄提示，单位毫秒，不能替代根据时间戳计算的持续丢失时间。缺一行、空文件或文件打不开时应报告证据而不崩溃。完整 JSON 解析器由候选人选择，基线正则读取器仅演示当前扁平格式，不保证覆盖任意 JSON。

`F6_public_issue_times.json` 是可选人工核对资料，**不是第四个输入，也不是完整故障清单**。每项 `frame_id` 定位起点，`timestamp` 是该事件来源日志时间；含 `end_frame_id_exclusive` 时范围为 `[frame_id,end_frame_id_exclusive)`。重捕获点是恢复的第一帧；`manual_review` 是正常对照点，不要求输出故障。时间倒退项的 `module` 指定时间戳来源，不能用 `frame_id/60` 覆盖该真实异常值。

| 公开检查点 | 帧 / 时间 | 可观察证据 |
|---|---|---|
| 第一段检测缺失 | `[180,205)` / 3.0 秒起 | 无检测、TEMP_LOST、禁火；第 205 帧约 3.416667 秒恢复 |
| ID 不一致 | `[360,368)` / 6.0 秒起 | 检测 red_4，跟踪/选择仍 red_3，带火命令 |
| 第二段检测缺失 | `[420,436)` / 7.0 秒起 | 无检测、TEMP_LOST、禁火；第 436 帧约 7.266667 秒恢复 |
| 丢失时开火 | `[510,516)` / 8.5 秒起 | 无检测、TEMP_LOST、fire_enable=1；第 516 帧恢复 |
| 命令时间倒退 | 第 540 帧 / 命令时间 8.95 秒 | 前一行第 539 帧约 8.983333 秒，跟踪同帧仍为 9.0 秒 |
| 正常人工复查 | 第 588 帧 / 9.8 秒 | ID、状态和命令一致，不预设故障 |

`ReplayIssue.timestamp_sec` 报告来源日志的秒数，不是帧号。`evidence` 应含模块名、帧号、冲突字段，`safety_relevant` 表示安全影响。相同输入需输出相同结果和顺序。缺失区间报告的时刻可能是下一条可见记录，允许与人工窗口起点不同，但必须在证据中说明范围，不能只按检查点秒数比对字符串。
