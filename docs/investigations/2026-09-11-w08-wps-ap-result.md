# W-08 AP WPS 原生结果 owner 与事件交付

此批接入 AP registrar 的结果存储和精确事件身份。公开 AP Session、原生关闭/
排空、Radio 以及 Future 仍未接入，不能据此宣称 AP registrar 已可用。

## 本批实现

- `wifi_ap_wps_init` 在创建 SDK 状态前绑定唯一结果 owner；绑定/分配失败不会
  继续初始化。按需分配记录，32-bit identity 在 boot 内不回绕，耗尽明确失败。
  SDK 初始化失败和 deinit 都通知 detach；无外部保留时清零释放。
- 固定 SDK 的 `wps_context.cb_ctx` 在 AP 路径改为数字 identity。事件 callback
  先用 identity 查找仍绑定的 context，再处理事件；不会从一个已释放的 context
  指针开始做身份校验。同地址重新初始化时，旧 identity 无法命中新对象。
- PIN 和首个成功/失败/超时/PBC overlap 保存在原生记录；status 只有 metadata，
  不携带 PIN。PIN 使用 copy/commit，转换前复制不会消费；commit、终态、关闭
  意图、detach 和 free 都清零秘密副本。输入长度和失败原因范围先检查。
- 六个 hostapd 事件及初始化 PIN 统一走结果入口，先提交原生终态再尝试发布
  默认 SDK 事件。未保留的 SDK 路径采用零等待事件；队列失败不能回滚原生状态。
  已保留的托管路径完全不向默认队列发布事件/PIN，供后续 worker/Future 读取。
- 保留原生结果和 SDK 堆状态的寿命分开。关闭意图只撤销交付，不能表示驱动停止。
  绑定过的保留记录在 SDK detach 后继续阻止 lane 复用；此批没有提供跳过排空的
  release。仅从未绑定的 reservation 可以按精确 identity 放弃。
- Station/AP 的结果保留参与共享 WPS 准入；不能在另一角色仍持有结果时创建
  新操作或改写 factory/type/status。实际 Radio/AP 配置租约仍待后续接入。

所有 result 操作和 SDK hook 都在 Wi-Fi task 上执行，跨任务只读 held 原子提示。
32-bit result identity 与上一批 64-bit 每次 arm ticket 分别标识操作与定时器；
没有把二者当作同一计数器，也不依赖原始 context 地址避免复用。

三目标生产对象的 DWARF 显示：按需 result record 为 44 B，metadata status 为
28 B；三个静态符号分别为指针 4 B、identity counter 4 B、held 提示 1 B（不含
链接对齐）。布局记录见 `build/w08-wps-ap-result-layout.json`。这不是运行峰值
或预热后的 heap 验收，不替代后续 internal/PSRAM free/largest-block 比较。

## 验证边界

新增 `test_idf_wps_ap_result.py` 编译完整生产 include，allocator、SDK admission
和 event-post 为受控边界。用例登记了队列饱和、PIN copy/commit、首终态保留、
同地址重开、旧 identity、畸形 payload、关闭意图、detach 后结果保留、跨角色
互斥、OOM 和 identity 耗尽。现有初始化用例同步更新 hook 边界。

按阶段要求仅做 Python AST 解析，没有导入、编译或运行 fixture。队列满和迟到
事件的动态证据仍为 `not-run`，不能将源码与目标构建当成这些用例通过。

八配置生产构建及静态产物核对通过，证据为
`build/w08-wps-ap-result-evidence.json`（273 项 hash）。首轮通过日志保留；复核后
将 Station 的 AP-held 检查前移至 native allocation 之前，再完成八配置构建。
现有 `test_idf_wps_native.py` 补入该准入用例，仍仅 AST。

已核对 result header/include 的生成副本、八份 SDK 替换源的实际选择、编译对象
与 archive/ELF，并确认六个 hostapd 事件已接入结果 owner。manifest 55 classes /
538 functions、类型、27 feature docs、配置 schema、SDK coverage 分类和 MQuickJS
61 sources / 63 snippets 检查通过。这些不替代动态回调/队列/GC 测试。

| 配置 | 镜像 bytes | 相对上一批增量 | app 分区剩余 bytes |
| --- | ---: | ---: | ---: |
| C3 | 2791040 | 0 | 354688 |
| S3 | 2683312 | 0 | 462416 |
| C5 roaming/WPS | 3177568 | 0 | 1016736 |
| C5 no-SoftAP | 3053408 | 0 | 92320 |
| C5 disabled | 459024 | 0 | 2686704 |
| C3 registrar | 2821568 | 1200 | 324160 |
| S3 registrar | 2711344 | 1200 | 434384 |
| C5 registrar | 3207984 | 1200 | 986320 |

后续仍需完整 SDK init/start/stop 错误
阶段、IE 失败后缀、AP PIN/EAP/延迟 station-remove 回调隔离、原生排空和 retained
结果释放、保留 IPC 参数、Radio/AP 租约，以及公开 Session/Future/runtime。
BLE 仍在 Wi-Fi 集中验收后；长 soak 仍留到 BLE API 完成后。
