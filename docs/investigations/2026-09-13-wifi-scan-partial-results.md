# Wi-Fi Scan 超时返回部分结果

日期：2026-09-13。范围为 `wifi.scan()`；不修改 BLE/Mesh 扫描契约。
本地 ESP-IDF：`fff9895c82d744c7237be8847347bdd1b07c6643`。

## 原因与当前契约

原扫描 Future 没有 `on_timeout`，通用 Future 的 deadline 分支直接拒绝，
随后取消扫描并进入 AP-list 清理；调用方无法取得超时前已经发现的 AP。

唯一 v1 现在统一返回 `WiFiScanResult`：

```js
var result = wifi.scan({ timeoutMs: 500, maxRecords: 16 });
print(result.records.length, result.complete, result.timedOut);
```

- 正常完成：`{ records, complete: true, timedOut: false }`。
- 扫描 deadline 到期：先停止原生扫描，读取本次已发现的 AP，再返回
  `{ records, complete: false, timedOut: true }`。
- 尚未启动即到期，或者没有发现 AP：`records` 为空，不读取其他扫描结果。
- 同代完成事件已在队列中：先处理它，避免把已经完成的扫描误标为超时。
- SDK 停止/读取/清理失败、内存不足仍报错；显式取消仍取消并丢弃结果。
  外层 `Future.timeout()` 包装器保持其原有语义。

`complete` 表示扫描正常结束，`maxRecords` 仍可截断返回列表。`timeoutMs`
是整个操作的扫描 deadline，不保证严格在该毫秒数内返回；同步 SDK 停止及
结果复制/清理可能增加耗时。API 类型、文档及相关设备 JS 调用方已同步。

## 生产路径与 ownership

`wifi_scan_future_on_timeout` 复用通用 Future 已有的成功超时返回接口，
不修改调度器、不增加计时器或额外等待循环。停止通过新增内部 helper
`esp32_mquickjs_wifi_stop_scan_for_results` 执行，保留结果 owner，禁止先走
会清空 AP-list 的普通取消路径。SDK 调用不在 Wi-Fi 锁内。

[ESP-IDF 扫描事件说明](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/api-guides/wifi-driver/overview.html)
包含显式停止引发的 SCAN_DONE。当前 C5 SDK 的 `esp_wifi_scan_stop` 经
`ieee80211_ioctl` 同步等待 Wi-Fi task 执行 `wifi_scan_stop_process`，后者
调用 `scan_cancel`；本地反汇编证据保存在 `build/wifi-scan-partial/`。
停止返回与框架事件循环已经收到 SCAN_DONE 是两个不同条件。

停止成功后可读取停止扫描的列表；原生过滤器存储、generation 和 Radio
reservation 仍等到 SCAN_DONE 才可复用。新字段
`scan_results_consumed_early` 记录列表已在事件到达前消费，防止迟到事件把
清理过的列表重新标记为待清理。零 AP 时仍沿用原来的清理路径。

竞争测试还复现了一个新路径中的遗漏：结果早读后，SCAN_DONE 恰好在 Future
解绑前到达，此时无 AP-list 清理后缀，`drain_scan` 直接返回，Radio
reservation 未及时归还。取消结尾现在额外检查可释放条件；清理失败或原生
扫描尚未终止时仍保持隔离。

## 验证与限制

本地证据目录：`build/wifi-scan-partial/`。行为断言位于 `tests/c/fixtures/`
并调用生产 helper；Python 负责组合编译与运行，设备 JS 负责公开接口。

- `before-result.json`：旧生产 finish 返回数组，新完成元数据断言失败。
- `before.json`：新增 helper 尚未实现时的链接失败，仅作为开发过程记录。
- `native.json`：首轮 11/11，包括原生命周期及真实 MQuickJS 的结果转换、
  第 N 次分配失败和移动 GC。
- `scan-final.json`：扫描重开、Radio admission 与 teardown 回归；其中
  零 AP 的测试错误地预期不执行解绑，修正夹具后见 `partial-final.json`。
- `partial-final.json`：超时部分结果、启动前超时、零 AP、maxRecords、停止/
  读取失败、已完成事件及旧 generation 事件的 2/2 参数化用例通过。
  注入的 AP 读取边界断言必须先停止或已有完成证据。
- `retirement-before.json`：事件落在早读与解绑之间的 reservation 断言失败；
  `retirement-final.json`：修复后生命周期与 Radio admission 16/16 通过。
- `native-final-summary.json` 汇总最新结果：28 个相关原生用例通过，重复
  执行的用例不重复计数。
- `python-final.log`：公共 manifest、类型和 Wi-Fi 架构检查 15/15 通过。
- MQuickJS 语法、生成 manifest 与 coverage map 检查通过；无新增公共
  callable。C3、S3、C5、C5 Wi-Fi disabled 最终增量构建全部通过；命令及
  退出状态见同目录 `build-matrix.json`。竞争修复后只重编译受影响对象并
  链接，未清理或全量重建，也未构建前端。

未 flash，设备 JS 未执行；真实 RF 部分 AP 列表、空口扫描停止时序和长时间
测试为 `not-run`。Host/SDK 边界注入和构建证据不提升无线功能稳定等级。
