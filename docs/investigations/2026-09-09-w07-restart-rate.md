# W-07 TX rate 的 restart 快照与恢复

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [interval 恢复](2026-09-09-w07-restart-interval.md)。完整 Wi-Fi 与实机功能验收仍待完成。

## 原生实现

TX rate core 新增 restart capture、identity capacity 和 replay，使用现有 accepted record、
参数 validator 与 SDK writer。仅保存当前 generation 的 known 配置；未配置接口保持 absent，
不会补默认 rate。stale、uncertain、无有效 write identity 或无足够身份空间时捕获失败。
Station/AP 配置均属于同一生命周期冻结快照，不引入第二套 rate writer 或临时租约机制。

Radio 在捕获之前拒绝未归还的 Raw TX rate lease/restore-pending。每次物理重试在 shutdown
前检查完整重放所需 identity 空间，失败保留原 snapshot/token。SDK 成功 deinit 仍通过原有
invalidate 清除当前记录并保留 identity 游标；没有用 runtime restart 代替物理终止证据。

配置恢复在 stopped 状态、start 前执行，遵守固定 SDK `esp_wifi_config_80211_tx` 契约。
每个接口成功才置完成位；失败保留已成功前缀和原始配置。下一物理 generation 重试清除
完成位并全部重放，不把失败记录捕获成原值。同代 core 的后缀重试不会重复成功接口；
Radio 故障后的正式协调仍走完整物理恢复边界。

owner 发布前核对本代 known/非 uncertain、精确 write identity 及 phymode/rate/ersu/dcm。
此 API 没有 getter，校验的是 SDK 成功接受的记录，不是实际空口速率；SDK writer 继续
使用独立输入副本，避免 SDK 修改指针内容污染冻结快照或 accepted record。

完整 PHY/band、AUTO country fallback、停机前 TX power 捕获、公开 restart/helper 协调、
AP 临时 rate 与其他高级 API 仍待完成。永久 AP rate 的恢复不等于 AP 临时租约已实现。

## 证据

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）最终构建 exit 0，
`build/w07-restart-rate-c5-build.txt`。首次构建发现内部函数与已有 JS 参数 capture 重名，
修正为独立 `restart_capture` 名称后重新构建通过；原日志保留为
`build/w07-restart-rate-c5-build-initial.txt`。

binary 2,761,840 bytes，比前批增加 368。checkpoint getter 反汇编为 640 bytes（前批 608），
固定控制结构 40 bytes（前批 32）。其他已记录的 Radio/ESP-NOW/Raw TX/policy/interval 静态
对象大小不变；没有测量 live heap、largest block 或回收。

capture/replay/capacity 与内部 restart 在生产对象中编译，但当前 ELF 因公开入口未接线
仍删除；最终 checkpoint 验证和诊断已链接。未增加 callable `wifi.driver.restart()`。

待执行 fixture 使用实际 rate core、实际 Radio checkpoint 和 SDK writer，SDK/heap/锁注入。
新增 Station/AP 保存/恢复、原值未被 SDK 输入修改污染、部分失败/成功前缀不重写、旧代
拒绝、未知/借用拒绝、identity 耗尽、最终 identity 不一致和失败后安全清除。仅 AST 检查，
未导入、编译或执行 Host fixture；不声称竞争缺陷已运行复现或 RF 验收通过。

manifest 49 classes/464 functions、feature 27、STA/AP schema 35/21（live SDK header）、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和 whitespace 检查通过。
汇总 `build/w07-restart-rate-evidence.json`。

Host/Python/VM/GC/OOM/竞争、完整 restart、C3/S3/feature-disabled、NAN/coex-enabled、实机、
RF/共存、soak 均 not-run。未刷写、串口操作、擦除 workspace、前端构建、提交、推送或
更新根 gitlink。全部 Wi-Fi API 完成后集中阶段测试和实机功能测试；长 soak 留到 BLE API 完成后。
