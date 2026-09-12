# W-08 iTWT setup TX 回调隔离

基线 firmware `d7db8d1`、SDK `fff9895c82`，接续
[setup 调用交接](2026-09-10-w08-twt-setup-submit.md)。本批补内部 TX 身份保护，
没有增加公开 Agreement API；完整 setup/teardown/suspend、退休和 RF 关联仍待完成。

## 确认的原生路径

固定 C5 `he_twt_setup_txcb` 用 TX body 的 dialog 查找当前 pending 表，要求
匹配槽处于 phase 0。dialog 在 `ieee80211_itwt_setup` 中按 63 取模、跳过零，
会复用。回调没有携带原请求身份；即使未找到 pending，或当前 phase 已改变，
原函数仍按 TX flow 清除 `s_tmp_itwt_id`。因此旧 TX 可以查到复用 dialog 的
新请求，或清除同 flow 的后续临时 ID。以上来自当前 SDK 原始 archive 的
反汇编审查，尚未执行动态复现，不写成竞争测试通过。

原 setup builder 先保存 pending/config/dialog/request ID、phase 0 并安装
response timer，再经 `he_send_action_twt_setup` 进入 `ht_action_output`。
这个边界已能绑定既有 timer ledger 的不可复用 32 位身份。

发送前 body 位置由 metadata 的 robust 标记决定（24/32 字节），此时 MAC
header 尚未由 `ieee80211_mgmt_output` 填写，不能读其 protected 位推断位置。
完成时沿用原回调的 EB prefix（flag 0x2000 对应 8 字节）和 header protected
位（24/32 字节）算法。校验 native body 长度、类别、setup IE 和 individual
协商类型；不修改原 PMF 构造、加密或安全要求。

## 生产实现

TX ledger 在发送前记录对应 response timer identity。领取要求当前关联 node
值、唯一 pending dialog、flow、request ID、phase 0 和 temporary ID 一致；
回调再次核对同一数字身份及当前 native 状态。旧 node 只比较地址值，不解引用。
已触发、已取消、变更为 dwell 或重新安装的 timer 不再授权旧 TX。

完成回调只接受精确存活 EB 一次，并在原 callback 返回前保留 ledger 槽。
旧/未知/重复 individual callback 不进入原 SDK 函数，因此不能走其无匹配时的
清除分支；bTWT 仍进入原广播分支。没有用 dialog 本身充当 operation identity，
也没有因 timer identity 相同而跳过当前 native table 校验。

无法记录身份时，拒绝发送、记录 sticky TX fault、归还本次已分配 EB；若检测到
重复提交同一个仍在飞行的 EB，则保留原 owner，不再次回收。builder 的原始
非零返回链继续传递错误。已建立的 pending/timer 不视为自动排空，仍需后续
退休处理；SDK setup 准入也拒绝已有 TX fault，避免再次创建 pending。

回收路径保留 output/callback/recycler pins，原 recycler 返回后只访问独立
ledger，不读取可能已释放或复用的 EB。driver 调用和 native table 检查均在
critical section 外，native mutation/完成继续遵循 SDK Wi-Fi task 顺序。

## 构建、链接和预算

五份 immutable Build Context 生产构建通过：

| Context | 镜像字节 | 相对上一批 |
| --- | ---: | ---: |
| c5-roaming | 2,977,968 | +1,312 |
| c5-no-softap | 2,854,368 | +1,312 |
| c5-disabled | 459,024 | 0 |
| c3 | 2,665,696 | 0 |
| s3 | 2,570,768 | 0 |

SDK export 后使用
`.venv/bin/python scripts/remote.py --build-context build/wireless-contexts/<context> --build-dir wireless-<context> --assume y build`。
生产 object 与 archive member 一致，新增路径仅在 C5/HE/Wi-Fi gate 中启用。

最终 C5 ELF 中 `ieee80211_he_attach` 将包装后的 setup callback 传给 index 19
的 `ic_register_tx_cb`，后者进入 `ppRegisterTxCallback`；setup builder 实际
调用包装后的 output。原 bTWT 转发继续保留，无需新增 SDK archive 指令补丁。
原 SDK objects 与当前 archive 重新比对；共享 SDK 保持干净，现有 build-local
archive hash 不变。完整 setup submit/native admission 仍只在 archive，尚待
Radio/Agreement caller，不能用这批 callback 链接证明其已公开可调用。

TX entry 从 8 B 变为 12 B，64 槽 boot-retained INTERNAL|8BIT 延迟分配预算
从 512 B 增至 768 B（增加 256 B）；该预算也适用于共用 ledger 的 probe。
没有另建 TX pool 或静态数组。`s_twt_tx` 仍为 36 B，既有 40 个跟踪静态对象
尺寸不变，共用 recycler 的 IRAM 调用路径保持内部内存。预算/ELF 尺寸不替代
预热后的实机 internal/PSRAM free、largest block 和 owner/pool 账本比较。

manifest 52 classes/513 functions、feature 文档 27、schema STA35/AP21、严格
TypeScript、MQuickJS 61 sources/60 snippets、Wi-Fi coverage 和 whitespace
检查通过。证据为 `build/w08-twt-setup-tx-evidence.json`、同名前缀的构建日志、
summary 和原始/最终 disassembly。

## 后置验收

四份生产 fixture 更新后仅作 AST 检查，没有 import、编译或执行：

- TX wrappers：同 dialog/flow 的旧完成、重复/提前完成、callback 内回收、EB
  复用、PMF offset/prefix、短 body、分配失败、重复提交和 bTWT 转发。
- setup timer：原生 tuple 完全相同也必须取得新数字身份；已触发、dwell、关联
  失效等状态不能授权旧 TX。
- SDK：temporary ID、phase 与当前 pending/request 匹配。
- submit：已有 TX fault 时拒绝 driver mutation，保留本次 dispatch 身份与错误。

Host 的 64 位 metadata pointer 与 C5 EB byte 61 重叠；TX fixture 仅将该 byte
移到 Host byte 64，生产偏移仍由 C5 编译/SDK/ELF 检查，不将 Host 布局当 ABI 证据。

阶段竞争、实际 PMF/TX、RF late RX、关闭/重开、GC、队列饱和、runtime restart、
完整 native/event/HW 退休均为 `not-run`。全部 Wi-Fi API 完成后集中运行与实机
验证；长 soak 留到 BLE API 完成后。未串口/烧录/擦除 workspace、构建前端、
提交、推送或更新父仓库 gitlink。
