# 2026-09-08 F-CORE 收尾

本记录对应 firmware `4026f7f` 后的修复性重构。F-CORE 的已确认缺陷及本轮指定的
短竞争、分配失败、移动 GC 和所有权转移风险已完成收尾；W-01 可以进入实施。
F-HARDWARE 未通过。按用户安排，长时间、完整生命周期、RF 与共存验收集中后置，
本次没有刷写设备、擦除 workspace、构建前端或更新父仓库 gitlink。

固定输入仍为 ESP-IDF `fff9895c82d744c7237be8847347bdd1b07c6643`、NimBLE
`139cada0ae932957fa06ba37d17e3c9c2c95c773` 和 MQuickJS
`47deb40fe9c9f548b2eb7ccf56ce2270694f667c`。之前各轮发现、失败日志和修复证据
保留在 [实施记录](2026-09-07-wifi-refactor-start.md)，本页补最后一批 F-08。

## 本轮确认并修复的问题

1. `ble_server_event_to_js()` 在写入事件出队后发现连接已经消失时直接返回异常，
   没有归还事件持有的原生池槽。现在由转换函数完成未交付事件的清理，且旧 adapter
   generation 不能归还当前池的槽。
2. 同一转换函数已经构造 connection handle、随后 event object/属性分配失败时，
   临时 handle 的 finalizer 会触发 adapter orphan close。现在失败时先撤销临时
   handle；失败转换不会关闭无关连接。
3. 共用 ByteSource 的数组长度/元素使用 `JS_ToInt32()`，接受截断的小数及回绕后的
   大整数。现在只接受有限 numeric integer；保留已有 `INT32_MAX` 数组长度上限，
   元素仍限定在 0..255。ByteView 的 offset 校验本来正确，只增加回归证据。

前两项修复前测试失败见 `build/wireless-payload-before.txt`，修复后为
`build/wireless-payload-after.txt`。第三项修复前见 `build/wireless-input-before.txt`，
修复后见 `build/wireless-input-after.txt`。没有为通过任务而改写正确的转换、CSI
资源池、ESP-NOW recovery 或队列 retain 机制。

## 可复现的测试边界

`tests/support/wireless_vm_fixture.py` 链接 vendored MQuickJS、实际 ByteView/Source
实现、生产 class finalizer 和 core property helper。它在原生 allocator 与可分配
JS API 入口逐个注入失败，在允许触发 GC 的 VM 分配入口强制真实压缩 GC。native
malloc 不触发虚构的 GC；setter 的按值参数由 VM 自己 root，不在入口前移动它们。
多对象转换断言实际 rooted value 已移动。native allocation ledger 检查重复释放
和泄漏，JS roots 由真实 VM 管理并在测试结束检查归零。

这是生产实现的 API/SDK 边界故障注入，不是另写一个测试状态机，也不是 VM 内部
每个 malloc、完整 NimBLE Host 或全部失败组合的穷举证明。额外用户类只链接本次
需要的构造/终结路径，保留生产 class id；正式完整 registration 另由 manifest 和
目标构建核验。

| 风险链 | 生产实现与本轮证据 |
| --- | --- |
| ByteView/ByteSpanSource | `test_wireless_byte_storage_gc.py`：owned/retained 构造、每步失败、close 幂等、busy read lease、失败 opener/缺失 iterator、Source owner 属性写入失败、GC finalizer；非法 length/byte/offset |
| BLE scan/notification/server payload | `test_wireless_payload_gc.py`：生产 pool、payload copy → 实际 owned ByteView → event object，逐 API/native allocation 失败、GC、原生槽重新可获取；server stale connection 和临时 handle finalizer |
| Wi-Fi status/scan | `test_wireless_status_gc.py`：嵌套 Radio status、scan entry/result array，driver 读取失败、native records 分配与所有 JS 构造失败、移动 GC |
| ESP-NOW receive/peer/status | 同上两组测试：生产接收池槽 → ByteView → event，peer rate config、session/tx queue/power-save status |
| CSI Frame/Batch/View/Source | `test_wifi_csi_storage_gc.py`：合成 callback 输入实际 resources/native pool/native lease；Frame info/layout、两帧 Batch 转移、samples/copySamples/source、control writer、iterator、close/finalizer；每步失败后 owner 归零 |
| BLE Future capture | `test_ble_capture_gc.py`：scan、read/write、MTU、scanner close、common connection operation、discovery 的生产 capture/release；state/payload/queue API/三个 discovery 数组失败；非法参数后的逆序回滚 |
| ESP-NOW open capture | `test_espnow_capture_allocations.py`：生产 capture、RX/TX 分配、open release、begin/finish close、storage reset；发送队列启用/禁用，队列 retain 和 Radio reserve 失败；JS root/native/Radio/queue 账本归零且 PMK 清零 |

Capture/注册核对沿用一套责任分工：参数/state 分配失败由 capture 自己回滚；
成功 capture 后 Future destroy 释放 roots，已提交的 native operation 另保留 storage；
scanner/advertiser/subscription 的 queue 和 payload 由对应 source 的释放路径回收；
连接/GATT cache、GATT server 定义由 adapter pool cleanup 回收。关闭类薄 capture
不新增 pool。公共操作 registry 耗尽和 stale identity 由原有生产 wireless core 测试
覆盖；Wi-Fi runtime queue/timer/EventGroup 失败和 worker queue 创建失败由原有 Host C
资源测试覆盖。BLE driver stop/deinit 的失败后缀重试保持原有生产 helper 测试。
上述分组说明各边界的证据，不把代表性 SDK fixtures 宣称为每个公开方法所有参数
组合都执行过。

CSI 测试确认：公共 Frame/Batch 关闭后 retained view/source 仍可读；有租约时
resources deinit 被拒绝；最后 owner 消失后 pool 归还。继续保留单个未释放 pool，
不增加多代 pool，也不改变 v1 wire。合成 CSI 测试不能替代真实 RF。

## 验证与交接

- Host C：65/65；Python：407/407。新增 capture 的 discovery 分支另作定向复测。
- 合法 immutable Build Context：C3 representative、S3 representative-psram、
  C5 representative、C5 disabled、C5 wireless-inventory（NAN-Sync）五项构建通过。
- MQuickJS：59 源文件/47 文档片段；API manifest：43 classes/384 functions；
  feature 文档：27；五配置 Wi-Fi 清单：1,267 项。
- 本轮日志：`build/fcore-final-*`；最终源码、日志、五镜像 hash 归档于
  `build/fcore-final-evidence.json`。原有 SRAM payload 优化保留；此次最后修复不新增
  静态池。此前连接 mutex 的 88 bytes 静态 RAM 账本仍适用。
- 本轮设备 warm-state internal/PSRAM free/largest block 对比为 **not-run**；
  没有用 Host GC/allocator 账本替代该结果。设备长测、BLE/GATT 对端、ESP-NOW 双机、
  CSI RF、500 次完整生命周期及共存仍为 **deferred / not-run**，稳定等级不提升。

02 的 W-00 覆盖清单与 CI 工具随本次整理提交；W-01 的 once → runtime lifecycle、
多 owner 与其后续能力仍是下一阶段工作。本次没有提前增加正式 API、类型或注册项。
