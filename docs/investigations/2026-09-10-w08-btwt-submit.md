# W-08 广播 setup 提交与结果持有

本批将广播 setup 的 SDK 调用、原生任务和发送前结果身份连接起来。公开
Agreement/Future、关闭/teardown、联合退休及其释放仍待实现；没有注册广播
setup 占位 API。

## 原生边界

固定 C5 SDK `esp_wifi_sta_btwt_setup` 使用 ioctl operation 117，将配置指针
放入 message +12，保留初始化、分配和同步 ioctl 检查；原生 handler 检查
当前 AP 能力后，把同一配置交给 `ieee80211_btwt_setup`。本批继续走公开 SDK，
没有以直接 builder 调用代替该路径。

新增 worker 提交 helper 使用独立 boot 存储中的 8 B config。原生 wrapper
按精确地址识别托管调用，只进入一次，记录当前 native task；只有同一次
原生调用的发送前 capture 可以交回 TX revision。检查广播 ID、command、
task、driver-called 和已有身份，拒绝重复/不匹配绑定。其他 task 或非托管
调用不会被自动认领为当前 owner。

结果在原始 output 前置 held，随后 TXFAIL、输出错误、response/dwell、
连接关闭都保留同一身份。记录完成和 timer 清空不能解除 held；后继 SDK
setup 会在共享 timeout/table 改写前被拒绝。同 ID 的旧 Future 因而不会
因为断连或 timer 结束而丢失结果。其他 ID 保持独立准入。

托管 setup 要求调用者已经选择 modem sleep，不替应用开启省电模式。
SDK error、实际 driver error 与身份交接错误分别保存。公开 SDK 意外提前
返回时不读取可能仍被 native 改写的配置，不清空 dispatch 存储，也不允许
下一次调用覆盖它；没有声称 runtime restart 可以恢复这种故障。

## 未完成的生命周期

held 只是结果所有权保留，**不是**原生排空证明。本批没有加入由 timeout、
断连或空 bitmap 直接释放的捷径。下一步需要 pending cancel/建立后 teardown、
TX/timer/native/event 联合退休与精确释放，然后才能连接 Radio/Agreement/
Future、公开 setup/close 和错误诊断。

新 worker helper 和原生 process hooks 编译入 framework archive；公开广播
setup 尚未注册，因此 worker dispatch 还没有公开 caller。实际现有 TX 路径
已连接身份绑定和 held 准入；当前非托管流量不凭空产生持有者。

## 内存与验证

dispatch 对象 36 B（结果 28 B、native task、占用标记），访问由临界区串行化。held
使用原结果标记的空余 bit，timer entry 仍 96 B、延迟池仍 3072 B，TX 主账本
768 B 与可选 storage 1568 B 不变。没有为每个广播 ID 另建 owner 结果池。

C5 HE、C5 no-SoftAP、C5 Wi-Fi-disabled、C3、S3 五个生产构建通过，最终
日志无 warning/error。C5 镜像 3028656 B、no-SoftAP 2905632 B，分别
增加 976 / 944 B；其他三个镜像不变。生产 object 与 archive 成员一致，
原 IRAM/recycler 闭包保持，共享 SDK 与 build-local patch hash 不变。
manifest 53 classes / 520 functions、27 feature docs、SDK schema/map、严格
TypeScript、MQuickJS 61 sources / 61 snippets 和空白检查均通过。
证据记录为 `build/w08-btwt-submit-evidence.json`。竞争 fixture 使用生产 dispatch、timer
result/held helper 和实际 native wrapper，SDK 公共调用/原始 builder 作为
可注入边界；覆盖同步提前 callback、观察队列失败、重复进入/绑定、foreign
task、同 ID 准入、断连保留、原生与公共错误、缺失 TX 身份、配置预验证、
显式 PS 准入、身份预算耗尽和不确定 SDK 返回。

fixture 仅 AST，未导入/编译/执行。Wi-Fi API 全部完成后集中运行/实机测试；
长 soak 留到 BLE API 完成后。未刷写、串口操作、提交、推送或更新根 gitlink。
