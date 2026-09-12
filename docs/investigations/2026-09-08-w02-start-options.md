# W-02：start 的 mode/storage 与已配置接口恢复

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
已在唯一 v1 注册 `wifi.start(options?: WiFiStartOptions)`，支持 mode=station/ap/
apsta、storage=ram/flash。stop 的 timeout 仍待完成，未添加占位参数。

## 确认的源码缺口与修复

旧 js_wifi_start 总是创建 Station/Application 的 STA lease，再由 ensure_started
合并 requested mode。因此 configure({mode:"apsta",start:false}) 留下的 mode
不会被 start 恢复；旧路径没有根据已配置 AP 准备 netif。本批用同一配置执行器
恢复所有所选接口，模式/存储缺省在 Radio mutex 内读取，不在 JS 层先查再写。
这是源码确认；没有声称运行复现，按用户要求新增测试留到 Wi-Fi API 完成后。

- 无参数、undefined、空对象：已初始化 driver 保留 mode/storage；未配置的冷
  启动默认 Station/RAM，mode=NULL 只回落 mode。start 不执行 Station connect。
- options 只接受 mode/storage，严格 plain object、enum 类型和完整字节匹配；
  unknown key/NUL 后缀/类型错误在 native 查询和初始化前拒绝。capture 使用
  GC roots，失败清零 native selection。内部 start_only 不进入公开类型/注册表。
- 运行中只接受匹配的 mode/storage，不能拿 start 隐式重配或断开；匹配的 AP/
  APSTA 不退休 netif/lease。Wi-Fi Future/原生排空、故障/清理仍阻止入口。
- 已由 CSI/ESP-NOW 等 feature 启动的 Station，可复用既有 Radio。native 准入
  在同一 mutex 内先取得 Application lease，避免旧 owner 在查询和 helper
  分配之间退出并 shutdown，使 storage 从 FLASH 意外回到 RAM。Station helper
  仍走原有精确 lease/初始化路径；不强制关闭其他 feature。

## 已停止/冷启动事务

AP helper 与中央 coordinator 借用同一 token。停止态准入后复用既有清理、
initialize、STA/AP helper prepare、mode/storage 配置和三 owner START 交接。
AP/APSTA 的临时配置缓冲区在已准入但尚未变更 driver 前分配；设置 mode/storage
后、START 前，在精确 stopped/零 owner token 内读取已存 AP 配置，执行原有
AP validator 和非零 channel 的法规检查。失败不广播、不输出部分成功。

start 不向原生配置事务传入 Station/AP config，不重写 AP 凭据；storage 选择
后续 SDK 写入策略，不承诺把已有配置复制到 NVS。AP 必须已有当前受支持且有效
的配置；可先 configure({start:false,accessPoint:...})。读取出的完整凭据仅存在
该临时缓冲区，所有成功/错误出口 secure-zero/free；失败查询还在 native helper
内清零输出。复用原生配置/START 屏障，不因函数返回速度把新路径当成实机完成。

初始化、AP 快照或 START 失败保留中央生命周期清理意图，后续 stop/runtime
cleanup 只清理已接受的后缀，不重放输入配置。WiFiStatus.cleanupStage 增加
start-ap-allocate/start-ap-config；公开错误仍使用 WIFI_START_FAILED 和原始
espCode。返回状态的 JS OOM 发生在 START 之后时保留已启动状态，需先检查 status。

同时修正 SoftAP-disabled apClients stub 的 arity，使新 options 请求进入已有
WIFI_AP_UNSUPPORTED 分支；不再因为 includeIp 实参本身误报旧的无参数 TypeError。

## 验证范围

新增 test_wifi_start_options.py：真实 MQuickJS capture/通用 options helper 的
GC/OOM 与严格输入；生产 begin_start_lifecycle/lease registry 的缺省锁内解析、
停止配置恢复、运行模式/存储冲突、精确 owner 与 Application anchor；生产共同
执行器的 APSTA prepare/校验/三 owner 启动、快照 OOM/SDK 错误、禁止配置输入
重写和完整凭据释放；生产 stopped-token AP copy 的旧 token/活动 owner 拒绝、
SDK/校验/法规失败后的清零。已有配置执行器和公开 stop fixture 同步新调用边界。
这些测试源码已增加，**没有编译或执行测试**。

必要 C5 immutable context 构建、MQuickJS 语法、manifest/features/raw schema、
recorded map、Python AST 和 whitespace 见 build/w02-start-options-evidence.json。
C3/S3/feature-disabled 矩阵、Host C/Python、真实 AP readback 规范化/广播、Station
连接/GC/队列竞争、实机功能均 not-run。长时间 soak 等 BLE API 也完成之后。
不提高 feature 稳定等级；未刷写、串口操作、擦除 workspace、构建前端、提交、
推送或更新父仓库 gitlink。完整 Wi-Fi 目标继续进行，见[剩余清单](2026-09-08-wifi-api-remaining.md)。
