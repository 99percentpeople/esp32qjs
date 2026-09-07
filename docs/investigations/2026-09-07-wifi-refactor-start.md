# 第一阶段收尾与 W-00 输入采集

用户决定：长时间验证在全部无线功能完成后集中执行。本轮继续短路径正确性检查，
并启动 W-00 的输入采集；没有把长时间测试延期解释为短竞争测试已经通过。

## 第一阶段新增修复

`future_expire_deadlines()` 先于 ready queue/poll 执行，因此 BLE 扫描/广播即使已
同步启动并置 completed，仍可能在 JS 结果转换前走 timeout。原来的
`ble_future_on_timeout()` 忽略 `ble_gap_disc_cancel()` / `ble_gap_adv_stop()` 的失败，
无条件清除 active；随后 destroy 会跳过取消保护并释放原生可见存储。

新增 `test_timeout_failed_stop_retains_native_storage` 编译实际 timeout 分支和
实际 destructor。扫描、广播两种失败注入均先复现提前释放；修复后仅在成功或
`BLE_HS_EALREADY` 时清除 active。失败保持原生 owner，destroy 交给已有 orphan
cleanup；成功重试和 already-stopped 路径也通过。公开 Future 仍按原契约超时，
不新增 API 或新的关闭机制。

验证：Host C 65/65、Python 340/340、MQuickJS 59 个源文件/47 个文档片段、
manifest 43 classes/384 functions，以及 C3/S3/C5/C5-disabled 四个原有合法
Build Context 增量构建全部通过。命令与基线见
[核心调查](2026-09-07-wireless-core.md)。本次日志为
`build/wireless-core-evidence/closeout-*.txt`，新增源码及四目标镜像 hash 记录在
`closeout-summary.json`。没有刷写设备；此前设备结果不覆盖本次额外 timeout 修复。

F-03 全 SDK callback-entry/stop 调度和 F-08 全 JS allocator/移动 GC 覆盖仍未完成。
F-CORE 保留收尾状态；这些短测试缺口与延期的长时间测试分别记录。

## 第二阶段实际进度

W-00 **in-progress / inputs-only**。新增
[`inspect_idf_wifi_inputs.py`](../../scripts/inspect_idf_wifi_inputs.py)，读取已构建的
`project_description.json`、`compile_commands.json` 和 sdkconfig，使用各目标
原有交叉编译器及参数预处理 16 份公共头文件。保存完整 translation unit、逐头文件
条件声明（含结构字段/宏）及 source/config/manifest hash；缺少输入会失败。
不从函数名字猜测 JS 映射，也不修改 actual manifest。

```sh
.venv/bin/python scripts/inspect_idf_wifi_inputs.py \
  --build-dir build/wireless-c3 \
  --build-dir build/wireless-s3 \
  --build-dir build/wireless-c5 \
  --output build/wifi-refactor-inputs
```

三目标提取通过，IDF 都为 `fff9895c82d744c7237be8847347bdd1b07c6643`。
现有 Wi-Fi/CSI/ESP-NOW actual manifest 合计 45 个 callable 条目；这是源码注册
清单，不是所有目标都启用的能力列表。输出 `inputs.json` 标记
`coverageReviewed: false`，生成文件保留在 ignored build 目录。

已发现五份头文件的条件文本在三目标间有差异：`esp_wifi.h`、`esp_wifi_he.h`、
`esp_wifi_he_types.h`、`esp_wifi_types_generic.h`、`local/esp_wifi_types_native.h`。
必须逐目标审查参数/字段；仅统计同名函数不能证明覆盖完整。

W-00 下一步为解析上述声明并审查逐 symbol/字段 disposition、implementation、
contract、validation，生成正式覆盖映射并接入未知符号检查。目前没有声明 W-00
验收完成，也没有实现 W-01 多 owner/restart、Monitor、Raw TX 或新的 CSI wire。

## 集中验收账本

| 类别 | 当前安排与状态 |
| --- | --- |
| 短竞争、错误注入、输入、生成物、target build | 随每次实现执行；现存 F-03/F-08 缺口继续收尾 |
| 有界硬件关闭/重开、GC、restart | 保留此前真实结果；新增 timeout 修复设备验证 not-run |
| 500 次完整生命周期、长时间泄漏/吞吐、共存 soak、时间回绕 | deferred / not-run；全部功能完成后集中验证 |
| BLE/GATT 对端、ESP-NOW 双机、CSI RF | not-run；缺少对端/采集条件的项目逐项保留，不能由 Host 或本机 driver success 替代 |

集中内存比较继续使用预热后、等待原生任务回收的相同静止状态，立即读取
internal/PSRAM free、largest block 和 owner/pool 账本；不保存惰性 getter 根对象
作为 before。feature-stability 不提升。
