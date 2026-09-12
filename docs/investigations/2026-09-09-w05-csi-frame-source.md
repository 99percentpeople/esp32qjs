# W-05：CSI 单帧 wire Source 与参数关闭边界

已接入 `frame.source({format: "esp32qjs-csi/1"})`，与 Batch 共用 wire Source
构造/迭代/释放实现。原始 CSI 字节 Source 改为 `frame.sampleSource()`，没有
保留旧 source() 行为或增加兼容别名。Frame/Batch wire 方法都要求精确 format。
单帧使用同一 32/40/256 协议、frame count=1，没有另一套单帧格式。

本批 firmware HEAD `d7db8d1e40ee9f6a522c22806252b2ef712843a7`，SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。修改尚未提交。

## 源码发现与修复

旧 Batch Source 先解析并保存 Batch adapter，随后读取 options.format。
plain-options 校验只校验对象种类和枚举键，未禁止 getter；MQuickJS 支持 getter。
format getter 可调用 Batch.close()，该方法清空 opaque 并立即释放 Batch adapter；
旧构造器随后仍读取保存指针的 frame_count/events。这是源码可达的释放后访问路径，
本批没有先运行故障复现，也不声称已经取得运行测试通过证据；用户要求集中测试。

新的公共入口只在参数读取后解析 Frame/Batch adapter。format 读取一次，不进行
对象到字符串强制转换，getter 异常原样传播；getter 关闭 owner 后按 stale owner
失败。源码不再持有跨用户 JS 求值的原生 Frame/Batch 指针。

## 原生 Source 所有权

共用 helper 在 1–128 范围内验证 count 和有界分配大小，按确切 event identity
逐项 retain，并用 retained_count 记录已取得的引用数量；失败释放本次已取得
的引用与控制区，不改变公开 Frame/Batch 的原有 owner。

Frame 的单个 event 和 Batch 的数组只在无 JS 执行的 native 分配/retain 阶段读取；
第一次 JS 对象分配前已经完成所有必要 retain 和控制区编码。新 Source 的 JS owner
为 undefined，单帧 raw sample Source 也独立持有 native lease，避免为数据保留整份
公共 Frame/Batch 和复制出的 info 对象。metadata/payload 仍受确切 slot lease 保护。

wire 迭代继续输出控制区、每帧原 CSI span、0–3 字节零 padding；单帧最后一段同样
对齐。读取完成或取消即释放 payload 引用和控制区，known length 保留原总长度。
JS Source 包装对象在 native iterator 活跃时销毁，仍延迟到 iterator close 释放。
关闭/消费过的 Source 不能重新打开。

没有改变 CSI 的单个未释放 pool 限制：旧 owner 存活时 reopen 仍拒绝。没有添加
correlated packet、packetSource 占位方法、多代 pool 或另一套资源预算。

## 契约与测试源码

源类型、头文件、原生注册、manifest、API/协议文档、02 任务书与剩余表同步。
既有 associated hardware 脚本改用 sampleSource，并增加单帧 wire 总长度检查；
该脚本的新增长度检查不能替代 wire 传输/解码或 RF 验收。

`test_wifi_csi_storage_gc.py` 继续调用生产资源池、Frame/Batch 适配器、通用
ByteSpanSource 和真实 MQuickJS。测试源码扩展为：

- 16 种转换/所有权场景，逐次 native/JS 分配失败与移动 GC；新增单帧 wire、非对齐
  CSI/final padding、无效 metadata、读取控制区后取消、native reader 活跃时关闭
  Source 包装对象。部分场景把资源生命周期置为 CLOSED，调用真实 pool 退休 helper，
  检查 reader 仍持有数据，最后释放后才归还 pool。
- Frame/Batch 原生 source/close 方法注册进测试 VM，20 个实际 JS 选项场景覆盖
  缺失/null/空对象/undefined format、内嵌 NUL、未知键、getter 关闭 owner 后 GC、
  getter 原始异常、只 GC 不关闭以及一次读取。未用独立状态机替代这些公共入口。

上述测试源码本批只做 Python AST 检查，没有编译或运行。实际修复验证、Host 全量、
三目标/feature-disabled 矩阵与实机功能仍依赖 Wi-Fi API 完成后的阶段验收；长期 soak
按用户要求等 BLE API 也完成。

## 本批已执行的检查

使用既有不可变 Build Context `firmware-ci-esp32c5-representative`，C5 构建通过；
最终 ELF 确认链接 sampleSource、单帧/Batch source 和共用 CSI/wire encoder。
镜像 `0x28c7d0`，相对上批 `0x28c6b0` 增加 `0x120`（288）字节；app 分区余量 15%。

MQuickJS 61 sources / 48 snippets；manifest 47 classes / 417 functions；features 27；
config schema 35 STA / 21 AP，live SDK 匹配；严格 TypeScript declarations、recorded
SDK map、测试文件 Python AST、whitespace 通过。SDK 工作区保持干净。

证据索引 `build/w05-csi-frame-source-evidence.json`；构建日志
`build/w05-csi-frame-source-c5-build.txt`。没有 flash/串口/擦除 workspace/前端构建、
提交/推送或 root gitlink 更新。没有新增 RF、heap 或长周期通过证据。
