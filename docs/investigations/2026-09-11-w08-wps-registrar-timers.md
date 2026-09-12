# W-08 registrar 协商定时器身份与启动失败

继续 AP registrar 原生生命周期。此批处理 registrar 的 PBC walk-time 和 PIN
selected-registrar 两类定时器，不代表完整原生排空或公开 Session 已完成。

## 源码确认与实现

固定 SDK 给这两类 eloop timer 直接传 `struct wps_registrar *`。取消操作只移除
仍在 timeout list 的项目，无法用其返回值证明已经取出的回调不会到达；deinit
后同地址复用时也没有代际校验。PBC/PIN 启动还忽略 timer 注册失败，仍会返回
成功、发布 selected/active 状态，可能失去原生 120 秒期限。

生成的 `wps_registrar.c` 现采用以下规则：

- 每次 arm 消耗一个 boot 内不复用的 64-bit ticket；耗尽明确失败。两个回调
  参数只编码 ticket 的高低位，不保存 SDK 对象指针。
- 活跃对象采用 SDK allocation 内的链表节点和两个 ticket 槽；实际 owner
  数仍受既有 WPS 单例准入约束。回调只在活跃表中匹配 ticket，先撤销该 ticket，
  再调用真实协议 timeout。过期、重复、关闭后或重开后的旧 ticket 不解引用对象。
- 注册新 timer 成功后才撤销旧 timer；失败保留旧 ticket 和协商状态，但已消耗
  的数字身份不回滚。deinit 在释放 registrar 前撤销两类 ticket 并从活跃表移除。
- PBC 先取得 timer 才修改 selected/PBC 状态；PIN 先取得 timer 才发布新 PIN
  或撤销旧 wildcard PIN。失败清理本次未发布 PIN，返回原错误。
- AP start 保留 mode/status/timer 的原始错误，并沿既有失败分支恢复前一状态。
  状态恢复本身失败的诊断和后缀保留仍属于后续托管关闭接入。

SDK 编译关闭的 NFC 分支也改用相同数字 timer 入口，避免在生成源中留下原始
registrar 指针 timer；本批没有 NFC-enabled 构建或 NFC 功能验收。

修改仍通过哈希固定的 build-local patcher，共享 ESP-IDF 不变。没有新增公开
占位接口，也没有把这两个 timer 的隔离当成 EAP/TX/所有队列已经排空。

## 验证边界

`test_idf_wps_registrar_timers.py` 使用生产 timer registry/arm/cancel/dispatch、
真实 PBC/PIN admission 和 AP start 函数体，登记原版 timer OOM 仍成功、重设
失败保留、已取消回调、重复回调、同地址重开、两个对象隔离、低 32 位跨界、
64-bit identity 耗尽及错误传播用例。只解析 AST；没有导入、编译或执行测试。
源码确认的问题尚未取得动态复现/修复通过证据。

八配置生产构建与静态产物核对通过，证据为
`build/w08-wps-registrar-timers-evidence.json`（245 项 hash）。三个启用 registrar
的配置实际编译八份替换源，其他 Wi-Fi 配置编译四份，disabled 不接入 patch。
已核对 generated source、compile commands、archive 对象与 ELF；静态契约和
MQuickJS 语法检查通过。manifest 仍为 55 classes / 538 functions。

| 配置 | 镜像 bytes | 相对上一批增量 | app 分区剩余 bytes |
| --- | ---: | ---: | ---: |
| C3 | 2791040 | 0 | 354688 |
| S3 | 2683312 | 0 | 462416 |
| C5 roaming/WPS | 3177568 | 0 | 1016736 |
| C5 no-SoftAP | 3053408 | 0 | 92320 |
| C5 disabled | 459024 | 0 | 2686704 |
| C3 registrar | 2820368 | 512 | 325360 |
| S3 registrar | 2710144 | 400 | 435584 |
| C5 registrar | 3206784 | 528 | 987520 |

三目标对象 DWARF 显示 registrar allocation 为 280 B；新 ticket counter 与
活跃表头的静态符号分别为 8 B 和 4 B。没有另建静态 timer pool。记录见
`build/w08-wps-registrar-timers-layout.json`，这些不是实机峰值/稳态 heap 结果。

后续仍需 AP PIN 辅助 timer、EAP/延迟
station-remove 回调身份、原生结果 owner、队列满终态、IE/失败清理、调度参数
保留及 Radio/Session/Future/runtime。实机与集中运行为 `not-run`，长 soak
保持在 BLE API 完成后执行。
