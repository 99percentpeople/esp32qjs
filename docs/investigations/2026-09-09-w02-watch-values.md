# W-02 五类扩展 watch value snapshot

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批扩展既有全量事件 descriptor 的五个 typed converter，不新增专属操作 API。
完整目标继续按[剩余清单](2026-09-08-wifi-api-remaining.md)推进。

## SDK 字段审查与正式契约

权威来源是固定 SDK 的 `components/esp_wifi/include/esp_wifi_types_generic.h`。
以下类型都是固定大小结构；按明确字段复制，不透传结构字节、padding 或 opaque
context。没有变长尾部、SDK report 指针或新的析构义务进入观察队列。

| SDK event/type | 公开 data | 限制 |
| --- | --- | --- |
| FTM_REPORT / wifi_event_ftm_report_t | peerAddress、statusId、rttRawNs、rttEstimatedNs、distanceCm、reportEntries | 只在 FTM_STATUS_SUCCESS 读取/交付测量和条目数，否则为 null；不调用 get_report，不消费/释放 SDK 详细报告 |
| ACTION_TX_STATUS / wifi_event_action_tx_status_t | interfaceId、statusId、operationId、channel | 不复制 context；成功可能先 TX_DONE 再 DURATION_COMPLETED，观察事件不是框架 Future 完成 |
| ROC_DONE / wifi_event_roc_done_t | statusId、operationId、channel | 不复制 context；op_id 不是框架精确 operation identity |
| AP_WRONG_PASSWORD / wifi_event_ap_wrong_password_t | address | 只有 MAC，不存在密码字段 |
| STA_BEACON_OFFSET_UNSTABLE / wifi_event_sta_beacon_offset_unstable_t | beaconSuccessRate | 保留 finite SDK float；公开头文件没有明确 fraction/percentage 范围，不推断换算；非有限值不可用 |

RTT 摘要单位是 ns，距离是 cm，区别于详细 FTM entry 的 ps 时间字段。uint32
测量使用 unsigned storage 和 JS_NewUint32，覆盖完整 0..UINT32_MAX，不经有符号
JS 整数转换。所有数字、MAC 都是值副本；SDK callback 返回后可以释放原输入。

## 队列与完成边界

capture 仍为已有 mutex 的零等待尝试、零等待 queue send、drop-newest 和饱和计数。
没有在 callback 中分配 JS/native heap 或调用测量报告 getter。现有 Future/control
更新和观察 fanout 顺序未变。global watch 不成为 Action/ROC/FTM 的 operation owner。
原生 context 可能承载内部 cookie，既不复制到 ingress，也不发布给 JS。

事件记录仅将旧 scalar 空间改为明确的 signed/unsigned/float union，count 与原
ssid length 共用同一字节。C5 ELF DWARF 实测 wifi_watch_event_t 仍为 88 bytes，
证据 `build/w02-watch-values-event-size.txt`；32-entry ingress 和订阅 item 大小不变。
源类型 WiFiEvent 的五个 discriminant 从 data:null 更新为对应 typed payload|null。
缺少/无效 payload 保持 unsupported-layout。raw 布局仍未批准，includeRawEventData
仍返回 unavailable reason。其余 TWT/neighbor converter、raw export，以及 W08
专属 FTM/Action/ROC/TWT 操作与资源生命周期继续保持未完成。

## 验证与未运行项

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建 exit 0，
日志 `build/w02-watch-values-c5-build.txt`。binary 2,774,160 bytes，比上一批增加
656。九项静态 Radio/owner/policy/restart 账本不变；源码/SDK header hash、链接和
仓库边界见 `build/w02-watch-values-evidence.json`。

新增 deferred fixture 提取生产 descriptor/capture/converter 和实际 SDK 声明，注入
mutex/queue/采样时间边界。用例覆盖输入释放后的独立副本、uint32 高位、FTM 失败
不读取测量、context 不进入记录/JS、NaN/null、未知/敏感 payload 不解引用、SSID
字节交付、队列满/锁竞争/sequence 耗尽/计数饱和及第 N 次 JS 失败和 movable GC。
仅 AST 解析，未导入、编译或执行，不报告这些行为测试已通过。

manifest 49 classes/469 functions、features 27、live SDK schema 35 STA/21 AP、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map、whitespace 检查通过。
Host/Python/VM/GC/OOM/fault、C3/S3/feature-disabled、完整 Wi-Fi 阶段与实机功能均
not-run，按约定集中执行；长 soak 放到 BLE API 完成后。没有刷写、串口操作、
擦除 workspace、前端构建、提交、推送或更新根 gitlink。
