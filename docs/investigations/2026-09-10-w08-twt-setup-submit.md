# W-08 iTWT setup 调用与身份交接

基线 firmware `d7db8d1`、SDK `fff9895c82`，接续
[原生结果记录](2026-09-10-w08-twt-setup-result.md)。本批编码从 worker 调用公开
SDK setup、绑定精确结果身份和交接 driver/API 错误的路径。Radio/Agreement
调用方与完整退休仍待接入，不增加公开 JS API 或提升 Candidate 等级。

## 保留 SDK 入口

固定 C5 `esp_wifi_sta_itwt_setup` 检查初始化、Wi-Fi 启动状态、Station/node、AP
能力、配置范围、ID 和 established 容量。它分配 24 B ioctl 消息，把原配置
指针写入偏移 12，operation 为 110，handler 为 `wifi_sta_itwt_setup_process`。
`ieee80211_ioctl` 从外部 task 提交后使用同步 semaphore 等待；该 operation
不属于异步返回例外。消息分配/API lock/停止或队列拒绝可在 native handler
之前返回错误。native task 情况走同步 `ieee80211_ioctl_process`。

新 helper 调用原公开 API，保留这些检查和 SDK 锁，不在私有 Action ioctl 中
绕过它们，也不从 setup native wrapper 递归调用公开 API。Radio admission、
关联/信道/PS policy 和 lease 的有效期仍由后续调用者承担。

原始 `ieee80211_api.o` 与 `ieee80211_ioctl.o` 均重新与固定 SDK archive
member 比对；共享 SDK 和已有 build-local 补丁不变。这里是静态 ABI/调用链
证据，未执行 SDK semaphore/RTOS 竞争测试。

## 稳定配置与精确身份

提交前完成纯 native 参数验证，再占用一个同步调用槽，并预留上一批的结果
记录。SDK 只接收 boot-stable 配置副本，不持有 JS、Future 或调用者输入地址。
同一时刻只允许一个公开 SDK setup 调用；这一同步调用槽与八个结果/Agreement
记录分别管理。调用者原配置、SDK flow writeback、AP setup 结果继续分离。

native wrapper 根据精确配置地址和本次非零结果 identity 领取一次调用，
再执行已有 slot/ID/flow/timer 准入。managed 请求复用已预留的记录；foreign
SDK 请求仍走自身 native 预留，不会因为同一 request ID 而领走 managed 身份。
重复进入、旧 identity 和错误地址不得再次调用 driver。

三个阶段分开记录：进入 wrapper、即将调用 driver、wrapper 已完成。无论
native 准入是否失败，正常 wrapper 返回都会发布完成阶段；managed 结果的
submitting 标记一直保留到外部同步 API 返回。提前 setup event 不提前结束
提交，也不释放正在交接的配置。输出非零 identity 在错误返回时仍归调用者，
必须退休；没有把 SDK 拒绝当作所有原生资源已经释放。

返回的内部 dispatch 分别保存：

- `sdk_error`：公开 API 的最终返回值，包含原有 timer 错误传播。
- `driver_error`：实际原生 driver handler 返回值；仅在 driver_called 且
  native_completed 时有效，避免被随后 timer 错误覆盖。
- `handoff_error`：调用阶段不符合已审查同步契约时的交接故障。

结果记录中的观察投递错误独立保留。正常交接后释放同步调用槽，结果记录
继续存活；之后的请求使用新身份。

## 异常返回

如果 API 返回成功却未进入 wrapper，或返回时 wrapper 尚未完成，不能认为
SDK 已排空。helper 保留 occupied 槽、稳定配置和 submitting 结果，拒绝新
调用覆盖；输出原 SDK 错误及独立 handoff fault，不自动重试。

这种异常下 native 可能还在写回配置，因此输出使用原请求副本和受锁保护的
阶段字段，不读取正在写回的 buffer。当前不提供解除该故障的 runtime restart
承诺，保留设备重启需求；完整物理恢复和退休协调仍属于剩余工作。

## 验证与实际状态

五份 immutable Build Context 生产构建通过：

| Context / build directory | 镜像字节 | 相对结果记录批次 |
| --- | ---: | ---: |
| c5-roaming / wireless-c5-roaming | 2,976,656 | 0 |
| c5-no-softap / wireless-c5-no-softap | 2,853,056 | 0 |
| c5-disabled / wireless-c5-disabled | 459,024 | 0 |
| c3 / wireless-c3 | 2,665,696 | 0 |
| s3 / wireless-s3 | 2,570,768 | 0 |

使用 SDK export 后的
`.venv/bin/python scripts/remote.py --build-context build/wireless-contexts/<context> --build-dir <directory> --assume y build`。
新增 native driver error 保留后重建；五份最终日志无 compiler warning/error，
生产 object 与 archive member 一致。

**本批桥接和 native setup wrapper 仍只在 archive，没有 Radio/Agreement caller，
尚未进入最终 ELF。** 新同步槽的 C5 编译尺寸为 40 B，dispatch 为 36 B；这些
尚不是本镜像新增的静态 SRAM。没有新增 heap 分配层，复用结果 ledger 的
448 B 预算。已链接的 40 个跟踪静态对象、镜像尺寸、probe Future capture 和
现有 setup timer/事件发布路径均不变。这些尺寸不替代实机 heap 比较。

manifest 52 classes/513 functions、feature 文档 27、SDK schema STA35/AP21、
严格 TypeScript、MQuickJS 61 sources/60 snippets、Wi-Fi coverage map 和 whitespace
检查通过。证据在 `build/w08-twt-setup-submit-evidence.json` 及同名前缀的
summary、构建日志、SDK/production object disassembly；公开调用方接入后仍需
核对实际 ELF 中 SDK handler 指针经过 wrapper，不能以当前 archive 替代。

新增 `test_wifi_twt_setup_submit.py` 描述生产 validators、结果 registry、提交
helper 和 native wrapper 之间的交接。SDK preflight/dispatch、原始 driver、
heap 和事件投递是注入边界；覆盖预验证/分配拒绝、原始 API 错误、重复提交、
提前事件、请求/writeback/AP 配置分离、timer 与 driver 错误同时出现、未完成
返回和保留存储。既有 SDK fixture 更新了 managed hook 边界。

fixture 仅 AST，未 import、编译或运行；没有 SDK semaphore、RF 或退休运行
证明。Radio/Future/Agreement、setup TX/RF 关联、teardown/suspend/broadcast
和完整 native/event/HW 退休继续保留。全部 Wi-Fi API 完成后集中运行与实机
测试，长 soak 留到 BLE API 完成后。未串口/烧录/擦除 workspace、构建前端、
提交、推送或更新根 gitlink。
